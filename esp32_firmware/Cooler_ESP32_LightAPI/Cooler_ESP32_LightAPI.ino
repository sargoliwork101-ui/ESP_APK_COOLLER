/* =========================================================================
 *  Cooler ESP32 - Light API Firmware (for use with Android/WebView app)
 * =========================================================================
 *  این نسخه:
 *    • کل HTML/CSS/JS سنگین از روی فلش/پروگرم ESP32 حذف شده.
 *    • فقط REST/JSON API برای کنترل باقی مانده (کد ثابت < 2KB).
 *    • دو endpoint جدید اضافه شده:
 *        - GET /config    -> تنظیمات AP/STA/AP-cycle/protection (بدون پسورد)
 *        - GET /scenarios -> لیست سناریوهای فعال فعلی
 *    • CORS (Access-Control-Allow-Origin: *) برای همه‌ی پاسخ‌ها فعال است
 *      تا اپ اندروید (و حتی نسخه‌ی PWA) بدون مشکل با برد صحبت کند.
 *    • ظاهر و منطق UI دقیقاً مثل قبل، اما روی گوشی (WebView App یا PWA).
 *  منطق اصلی (سناریو، ساعت، NTP، رله، محافظت، آمار، چرخه‌ی AP/STA،
 *  رمزنگاری AES پسوردها، ذخیره‌سازی امن LittleFS، Watchdog) هیچ تغییری
 *  نکرده و فقط بخش ارسال HTML حذف شده است.
 * ========================================================================= */

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include "mbedtls/aes.h"

// ============================================================
//  بخش تنظیمات و ثابت‌ها (بدون تغییر نسبت به نسخه‌ی اصلی)
// ============================================================
const char* PROGRAM_NAME    = "کولر هوشمند ESP32";
const char* PROGRAM_TAGLINE = "ESP32 · TIMER HUB";
const int  RELAY_PIN = 23;
const int  RELAY_ACTIVE_LEVEL = HIGH;
const long NTP_GMT_OFFSET_SEC = 12600;
const unsigned char AES_KEY[16] = {
  0x4F, 0xA1, 0xC3, 0x92, 0x0E, 0x77, 0xB5, 0x2D,
  0x8C, 0x1A, 0x6F, 0xE4, 0x33, 0xD9, 0x5B, 0x88
};
char custom_ssid[32]     = "ESP32_Timer_Hub";
char custom_password[32] = "12345678";
char sta_ssid[32]     = "";
char sta_password[64] = "";
bool internet_enabled = true;
int  staOnMinutes  = 10;
int  staOffMinutes = 0;
const int MIN_STA_ON_MINUTES = 1;
const int MIN_STA_OFF_MINUTES = 0;
const int MAX_STA_CYCLE_MINUTES = 1440;
bool staCurrentlyOn = true;
unsigned long staCycleLastToggleMillis = 0;

bool apCycleEnabled = false;
int  apOnMinutes  = 10;
int  apOffMinutes = 5;
const int MIN_AP_CYCLE_MINUTES = 1;
const int MAX_AP_CYCLE_MINUTES = 1440;
bool apCurrentlyOn = true;
unsigned long apCycleLastToggleMillis = 0;
int apTxPowerLevel = 3;
const int MAX_AP_TX_POWER_LEVEL = 3;

const int MAX_SCENARIOS = 20;
int antiShortCycleMinutes = 3;
const int MAX_ANTI_SHORT_CYCLE_MINUTES = 1440;
unsigned long lastRelayOffMillis = 0;

const size_t SCENARIOS_JSON_CAPACITY = 6144;

const int STORAGE_FORMAT_REVISION = 6;
const int SCENARIOS_FILE_VERSION = 2;
const int WIFI_FILE_VERSION = 5;
const int TIME_FILE_VERSION = 2;
const int RELAY_STAT_FILE_VERSION = 2;
const int OVERRIDE_FILE_VERSION = 2;
const int PROTECTION_FILE_VERSION = 1;
const int NTP_META_FILE_VERSION = 1;

const int WDT_TIMEOUT_SEC = 8;
const unsigned long TIME_SAVE_INTERVAL = 5UL * 60UL * 1000UL;
const unsigned long NTP_RETRY_INTERVAL_MS = 1UL * 60UL * 1000UL;
const unsigned long NTP_RECHECK_INTERVAL_MS = 1UL * 60UL * 60UL * 1000UL;

WebServer server(80);

void feedWatchdog();

// ===== CORS helper =====
void sendCORS(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}
void handleOptions(){
  sendCORS();
  server.send(204, "text/plain", "");
}

struct Scenario {
  bool active = false;
  bool enabled = false;
  int startHour = 0;
  int startMinute = 0;
  int endHour = 0;
  int endMinute = 0;
  uint8_t weekdays = 0x7F;
};
Scenario scenarios[MAX_SCENARIOS];

int currentHour = 0, currentMinute = 0, currentSecond = 0;
int currentYear = 2026, currentMonth = 1, currentDay = 1;
int currentWeekday = 3;
unsigned long lastTick = 0;

int manual_override = 0;
bool time_synchronized = false;
bool ntp_synced_this_boot = false;
bool ntpEverSucceeded = false;
bool ntpLastSuccessValid = false;
int ntpLastSuccessYear=2026, ntpLastSuccessMonth=1, ntpLastSuccessDay=1;
int ntpLastSuccessHour=0, ntpLastSuccessMinute=0, ntpLastSuccessSecond=0;
const char* NTP_META_FILE = "/ntp.json";

unsigned long lastNtpCheckMillis = 0;
bool ntpFirstCheckPending = true;
bool staEverConnectedThisBoot = false;

unsigned long resetMillis = 0;
bool pendingReset = false;

unsigned long lastToggleManualRequest = 0;
unsigned long lastSaveScenarioRequest = 0;
unsigned long lastSyncRequest = 0;
unsigned long lastSaveApRequest = 0;
unsigned long lastSaveStaRequest = 0;
unsigned long lastSaveProtectionRequest = 0;
unsigned long lastSaveApCycleRequest = 0;
unsigned long lastStatusRequest = 0;
unsigned long lastConfigRequest = 0;
unsigned long lastScenariosRequest = 0;

const char* TIME_FILES[2] = {"/time0.txt", "/time1.txt"};
int timeFileSlot = 0;
unsigned long timeSaveSeq = 0;
unsigned long lastTimeSaveMillis = 0;

unsigned long relaySwitchCount = 0;
unsigned long relayTotalOnSeconds = 0;
unsigned long relayOnSinceMillis = 0;
bool relayCurrentlyOnForStats = false;
int lastRelayStatState = -1;
const char* RELAY_STAT_FILES[2] = {"/relaystat0.txt", "/relaystat1.txt"};
int relayStatFileSlot = 0;
unsigned long relayStatSaveSeq = 0;
unsigned long lastRelayStatSaveMillis = 0;

void loadScenarios();
void saveScenarios();
void loadWiFiSettings();
void saveWiFiSettings();
void loadOverrideSetting();
void loadProtectionSettings();
void saveProtectionSettings();
void saveOverrideSetting();
void saveTimeSetting();
void loadTimeSetting();
bool readTimeFile(const char* path, int &h, int &m, int &s, int &sync, unsigned long &seq, int &y, int &mon, int &d, int &wd);
void advanceDate();
bool isLeapYear(int year);
void saveRelayStats();
void loadRelayStats();
bool readRelayStatFile(const char* path, unsigned long &sc, unsigned long &tos, unsigned long &seq);
void loadNtpSuccessInfo();
void saveNtpSuccessInfo();
void handleSaveScenario();
void handleSyncTime();
void handleGetStatus();
void handleToggleManual();
void handleSaveAP();
void handleSaveSTA();
void handleSaveProtection();
void handleSaveApCycle();
void handleGetConfig();
void handleGetScenarios();
bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs);
void checkScenarios();
void updateClock();
void setRelay(bool state);
void applyApTxPower();
void setApRadioState(bool on);
void manageApCycle();
void setStaConnectionState(bool on);
void manageStaCycle();
void connectToInternetWiFi();
void tryNtpSync();
String encryptPassword(const char* password, size_t bufferSize);
void loadAndDecryptPassword(const char* savedValue, char* outputBuffer, size_t bufferSize);

bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs) {
  unsigned long now = millis();
  if (lastRequest != 0 && (unsigned long)(now - lastRequest) < minIntervalMs) {
    sendCORS(); server.send(429, "text/plain", "Too Many Requests"); return false;
  }
  lastRequest = now;
  return true;
}

void setupWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t twdt_config = { .timeout_ms = WDT_TIMEOUT_SEC * 1000, .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, .trigger_panic = true };
  esp_task_wdt_init(&twdt_config);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL);
}
void feedWatchdog() { esp_task_wdt_reset(); }

void setRelay(bool state) {
  digitalWrite(RELAY_PIN, state ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

void applyApTxPower() {
  wifi_power_t p;
  switch (apTxPowerLevel) {
    case 0:  p = WIFI_POWER_5dBm; break;
    case 1:  p = WIFI_POWER_11dBm; break;
    case 2:  p = WIFI_POWER_15dBm; break;
    default: p = WIFI_POWER_19_5dBm; break;
  }
  WiFi.setTxPower(p);
}
void setApRadioState(bool on) {
  if (on == apCurrentlyOn) return;
  if (on) { WiFi.softAP(custom_ssid, custom_password, 1, 0, 3); applyApTxPower(); }
  else    { WiFi.softAPdisconnect(true); }
  apCurrentlyOn = on;
}
void manageApCycle() {
  if (!apCycleEnabled) { if (!apCurrentlyOn) { setApRadioState(true); apCycleLastToggleMillis = millis(); } return; }
  if (apCurrentlyOn && WiFi.softAPgetStationNum() > 0) { apCycleLastToggleMillis = millis(); return; }
  unsigned long onMs  = (unsigned long)apOnMinutes  * 60000UL;
  unsigned long offMs = (unsigned long)apOffMinutes * 60000UL;
  unsigned long elapsed = millis() - apCycleLastToggleMillis;
  if (apCurrentlyOn && elapsed >= onMs)  { setApRadioState(false); apCycleLastToggleMillis = millis(); }
  else if (!apCurrentlyOn && elapsed >= offMs) { setApRadioState(true); apCycleLastToggleMillis = millis(); }
}

String encryptPassword(const char* password, size_t bufferSize) {
  if (strlen(password) == 0) return "";
  mbedtls_aes_context aes; mbedtls_aes_init(&aes); mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);
  size_t paddedSize = (bufferSize + 15) / 16 * 16;
  unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
  if (!input) return "";
  strncpy((char*)input, password, bufferSize - 1);
  unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
  if (!output) { free(input); return ""; }
  for (size_t i = 0; i < paddedSize; i += 16) mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
  mbedtls_aes_free(&aes);
  String hexString = "ENC:";
  for (size_t i = 0; i < paddedSize; i++) { char hex[3]; sprintf(hex, "%02x", output[i]); hexString += String(hex); }
  free(input); free(output);
  return hexString;
}
void loadAndDecryptPassword(const char* savedValue, char* outputBuffer, size_t bufferSize) {
  String val = String(savedValue);
  if (val.length() == 0) { outputBuffer[0] = '\0'; return; }
  if (val.startsWith("ENC:")) {
    String hexString = val.substring(4); size_t len = hexString.length();
    if (len % 32 != 0) return;
    size_t paddedSize = len / 2;
    unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
    if (!input) return;
    for (size_t i = 0; i < paddedSize; i++) { char hex[3] = {hexString[i*2], hexString[i*2+1], '\0'}; input[i] = (unsigned char)strtol(hex, NULL, 16); }
    mbedtls_aes_context aes; mbedtls_aes_init(&aes); mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);
    unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
    if (!output) { free(input); return; }
    for (size_t i = 0; i < paddedSize; i += 16) mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
    mbedtls_aes_free(&aes);
    size_t copySize = paddedSize < bufferSize ? paddedSize : bufferSize - 1;
    memcpy(outputBuffer, output, copySize); outputBuffer[copySize] = '\0';
    free(input); free(output);
  } else {
    strncpy(outputBuffer, savedValue, bufferSize - 1); outputBuffer[bufferSize - 1] = '\0';
  }
}

void setup() {
  Serial.begin(115200);
  setupWatchdog();
  if (!LittleFS.begin(true)) Serial.println("LittleFS Mount Failed!");
  loadScenarios(); loadWiFiSettings(); loadOverrideSetting(); loadProtectionSettings();
  loadTimeSetting(); loadRelayStats(); loadNtpSuccessInfo();
  pinMode(RELAY_PIN, OUTPUT);
  if (manual_override == 1) setRelay(true); else setRelay(false);
  lastRelayStatState = (manual_override == 1) ? 1 : 0;
  if (lastRelayStatState == 0) lastRelayOffMillis = millis();
  if (lastRelayStatState == 1) { relayOnSinceMillis = millis(); relayCurrentlyOnForStats = true; }

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
  applyApTxPower();
  apCurrentlyOn = true; apCycleLastToggleMillis = millis();
  staCurrentlyOn = true; staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  // Endpoints
  server.on("/", HTTP_GET, [](){
    sendCORS();
    server.send(200, "text/html; charset=utf-8",
      "<!DOCTYPE html><html dir=rtl lang=fa><head><meta charset=utf-8>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>" + String(PROGRAM_NAME) + "</title>"
      "<style>body{font-family:Vazirmatn,Tahoma,sans-serif;background:#0a0c0d;color:#eef0f1;text-align:center;padding:40px 20px;}"
      "h1{color:#1de9c4}code{background:#15181a;padding:4px 8px;border-radius:6px;color:#29d3c8;direction:ltr;display:inline-block;}</style>"
      "</head><body><h1>" + String(PROGRAM_NAME) + "</h1>"
      "<p>این دستگاه در حالت <b>Light-API</b> است.</p>"
      "<p>رابط کاربری توسط اپ موبایل یا PWA ارائه می‌شود؛ برد فقط داده می‌فرستد/می‌گیرد.</p>"
      "<p>API endpoints:<br>"
      "<code>GET  /status</code><br>"
      "<code>GET  /config</code><br>"
      "<code>GET  /scenarios</code><br>"
      "<code>POST /toggle-manual</code><br>"
      "<code>POST /sync?...&nbsp;&nbsp;</code><br>"
      "<code>POST /save</code> (JSON)<br>"
      "<code>POST /save-ap</code><br>"
      "<code>POST /save-sta</code><br>"
      "<code>POST /save-ap-cycle</code><br>"
      "<code>POST /save-protection</code>"
      "</p></body></html>");
  });
  server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/sync", HTTP_POST, handleSyncTime);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on("/scenarios", HTTP_GET, handleGetScenarios);
  server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  server.on("/save-ap", HTTP_POST, handleSaveAP);
  server.on("/save-sta", HTTP_POST, handleSaveSTA);
  server.on("/save-protection", HTTP_POST, handleSaveProtection);
  server.on("/save-ap-cycle", HTTP_POST, handleSaveApCycle);
  // CORS preflight
  server.onNotFound([](){
    if (server.method() == HTTP_OPTIONS) { handleOptions(); return; }
    sendCORS(); server.send(404, "text/plain", "Not Found");
  });
  server.begin();
  lastTick = millis();
  lastTimeSaveMillis = millis();
}

void loop() {
  feedWatchdog();
  static int lowHeapStreak = 0;
  if (ESP.getFreeHeap() < 2000) {
    lowHeapStreak++;
    if (lowHeapStreak >= 5) { Serial.println("Memory too low! Restarting."); saveTimeSetting(); ESP.restart(); }
  } else lowHeapStreak = 0;
  if (pendingReset && (millis() - resetMillis > 2000)) { saveTimeSetting(); ESP.restart(); }
  server.handleClient();
  updateClock();
  manageStaCycle();
  tryNtpSync();
  checkScenarios();
  manageApCycle();
  if (time_synchronized && (millis() - lastTimeSaveMillis >= TIME_SAVE_INTERVAL)) saveTimeSetting();
  if (relayCurrentlyOnForStats && (millis() - lastRelayStatSaveMillis >= TIME_SAVE_INTERVAL)) {
    unsigned long elapsedSec = (millis() - relayOnSinceMillis) / 1000UL;
    relayTotalOnSeconds += elapsedSec; relayOnSinceMillis += elapsedSec * 1000UL;
    saveRelayStats();
  }
  static unsigned long lastTxPowerRefresh = 0;
  if (millis() - lastTxPowerRefresh >= 30000UL) { applyApTxPower(); lastTxPowerRefresh = millis(); }
  static unsigned long lastPinModeRefresh = 0;
  if (millis() - lastPinModeRefresh >= 30000UL) { pinMode(RELAY_PIN, OUTPUT); lastPinModeRefresh = millis(); }
  yield();
}

void updateClock() {
  while (millis() - lastTick >= 1000) {
    lastTick += 1000; currentSecond++;
    if (currentSecond >= 60) {
      currentSecond = 0; currentMinute++;
      if (currentMinute >= 60) { currentMinute = 0; currentHour++; if (currentHour >= 24) { currentHour = 0; advanceDate(); } }
    }
  }
}
bool isLeapYear(int year) { return (year%4==0 && year%100!=0) || (year%400==0); }
void advanceDate() {
  const int dim[]={31,28,31,30,31,30,31,31,30,31,30,31};
  int md = dim[currentMonth-1];
  if (currentMonth==2 && isLeapYear(currentYear)) md=29;
  currentDay++;
  if (currentDay>md){currentDay=1;currentMonth++;if(currentMonth>12){currentMonth=1;currentYear++;}}
  currentWeekday=(currentWeekday+1)%7;
}

void checkScenarios() {
  static int lastKnownState = -1;
  bool desiredState;
  if (manual_override == 1) {
    desiredState = true;
    if (lastKnownState != 1) { if (time_synchronized) saveTimeSetting(); lastKnownState = 1; }
  } else if (!time_synchronized) {
    desiredState = false;
    if (lastKnownState != 0) lastKnownState = 0;
  } else {
    desiredState = false;
    int cur = currentHour*60 + currentMinute;
    for (int i=0;i<MAX_SCENARIOS;i++){
      if (!scenarios[i].active || !scenarios[i].enabled) continue;
      int s = scenarios[i].startHour*60+scenarios[i].startMinute;
      int e = scenarios[i].endHour*60+scenarios[i].endMinute;
      uint8_t wd = scenarios[i].weekdays;
      if (s == e || wd == 0) continue;
      int sd = currentWeekday;
      if (s>e && cur<e) sd = (currentWeekday+6)%7;
      if ((wd & (1<<sd))==0) continue;
      bool in = (s<e) ? (cur>=s && cur<e) : (cur>=s || cur<e);
      if (in){ desiredState = true; break; }
    }
    int n = desiredState?1:0;
    if (lastKnownState != n){ saveTimeSetting(); lastKnownState = n; }
  }
  if (desiredState && antiShortCycleMinutes>0 && lastRelayStatState==0){
    unsigned long req = (unsigned long)antiShortCycleMinutes*60UL*1000UL;
    if (millis()-lastRelayOffMillis < req) desiredState = false;
  }
  int nrs = desiredState?1:0;
  if (lastRelayStatState != nrs){
    if (nrs == 1){ relaySwitchCount++; relayOnSinceMillis=millis(); relayCurrentlyOnForStats=true; }
    else { if (relayCurrentlyOnForStats){ relayTotalOnSeconds += (millis()-relayOnSinceMillis)/1000UL; } relayCurrentlyOnForStats=false; lastRelayOffMillis=millis(); }
    lastRelayStatState = nrs; saveRelayStats();
  }
  setRelay(desiredState);
}

void loadScenarios() {
  if (!LittleFS.exists("/scenarios.json")) return;
  File f = LittleFS.open("/scenarios.json","r"); if (!f) return;
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  DeserializationError err = deserializeJson(doc, f); f.close();
  if (err) return;
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int i=0;
  for (JsonObject v : arr){
    if (i>=MAX_SCENARIOS) break;
    bool a = v["active"] | false;
    scenarios[i].active = a;
    scenarios[i].enabled = v.containsKey("en") ? (bool)v["en"] : a;
    scenarios[i].startHour = v["sh"]|0; scenarios[i].startMinute=v["sm"]|0;
    scenarios[i].endHour=v["eh"]|0; scenarios[i].endMinute=v["em"]|0;
    scenarios[i].weekdays = v.containsKey("wd") ? (uint8_t)(v["wd"]|0x7F) : 0x7F;
    i++;
  }
}
void saveScenarios() {
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  JsonObject root = doc.to<JsonObject>(); root["version"]=SCENARIOS_FILE_VERSION;
  JsonArray arr = root.createNestedArray("items");
  for (int i=0;i<MAX_SCENARIOS;i++){
    JsonObject o = arr.createNestedObject();
    o["active"]=scenarios[i].active; o["en"]=scenarios[i].enabled;
    o["sh"]=scenarios[i].startHour; o["sm"]=scenarios[i].startMinute;
    o["eh"]=scenarios[i].endHour; o["em"]=scenarios[i].endMinute;
    o["wd"]=scenarios[i].weekdays;
  }
  File f = LittleFS.open("/scenarios.json","w"); if (!f) return;
  size_t w = serializeJson(doc,f); f.close();
  if (w==0) LittleFS.remove("/scenarios.json");
}

void loadWiFiSettings() {
  if (!LittleFS.exists("/wifi.json")) return;
  File f = LittleFS.open("/wifi.json","r"); if (!f) return;
  StaticJsonDocument<1024> doc; DeserializationError e = deserializeJson(doc,f); f.close();
  if (e) return;
  if (doc.containsKey("ssid")) { strncpy(custom_ssid, doc["ssid"], 31); custom_ssid[31]='\0'; }
  if (doc.containsKey("pass"))  { loadAndDecryptPassword(doc["pass"], custom_password, 32); }
  if (doc.containsKey("sta_ssid")){ strncpy(sta_ssid, doc["sta_ssid"], 31); sta_ssid[31]='\0'; }
  if (doc.containsKey("sta_pass")){ loadAndDecryptPassword(doc["sta_pass"], sta_password, 64); }
  if (doc.containsKey("internet")) internet_enabled = doc["internet"]|true;
  if (doc.containsKey("staEnabled") && !doc.containsKey("staOnMinutes") && !doc.containsKey("staOffMinutes")){
    if (!(doc["staEnabled"]|true)) internet_enabled = false;
  }
  if (doc.containsKey("staOnMinutes"))  { int v=doc["staOnMinutes"]|staOnMinutes;  staOnMinutes=constrain(v,MIN_STA_ON_MINUTES,MAX_STA_CYCLE_MINUTES); }
  if (doc.containsKey("staOffMinutes")) { int v=doc["staOffMinutes"]|staOffMinutes; staOffMinutes=constrain(v,MIN_STA_OFF_MINUTES,MAX_STA_CYCLE_MINUTES); }
  if (doc.containsKey("apCycleEnabled")) apCycleEnabled = doc["apCycleEnabled"]|false;
  if (doc.containsKey("apOnMinutes"))   { int v=doc["apOnMinutes"]|apOnMinutes;   apOnMinutes=constrain(v,MIN_AP_CYCLE_MINUTES,MAX_AP_CYCLE_MINUTES); }
  if (doc.containsKey("apOffMinutes"))  { int v=doc["apOffMinutes"]|apOffMinutes;  apOffMinutes=constrain(v,MIN_AP_CYCLE_MINUTES,MAX_AP_CYCLE_MINUTES); }
  if (doc.containsKey("apTxPowerLevel")){ int v=doc["apTxPowerLevel"]|apTxPowerLevel; apTxPowerLevel=constrain(v,0,MAX_AP_TX_POWER_LEVEL); }
}
void saveWiFiSettings() {
  StaticJsonDocument<1024> doc;
  doc["version"]=WIFI_FILE_VERSION; doc["ssid"]=custom_ssid;
  doc["pass"]=encryptPassword(custom_password,32);
  doc["sta_ssid"]=sta_ssid;
  doc["sta_pass"]=encryptPassword(sta_password,64);
  doc["internet"]=internet_enabled;
  doc["staOnMinutes"]=staOnMinutes; doc["staOffMinutes"]=staOffMinutes;
  doc["apCycleEnabled"]=apCycleEnabled; doc["apOnMinutes"]=apOnMinutes; doc["apOffMinutes"]=apOffMinutes; doc["apTxPowerLevel"]=apTxPowerLevel;
  File f=LittleFS.open("/wifi.json","w"); if(!f) return;
  size_t w=serializeJson(doc,f); f.close();
  if(w==0) LittleFS.remove("/wifi.json");
}

void loadOverrideSetting(){
  manual_override=0;
  if(!LittleFS.exists("/override.txt")) return;
  File f=LittleFS.open("/override.txt","r"); if(!f) return;
  String v=f.readString(); f.close();
  manual_override = v.startsWith("V")? v.substring(v.indexOf(':')+1).toInt() : v.toInt();
  manual_override = (manual_override==1)?1:0;
}
void saveOverrideSetting(){
  File f=LittleFS.open("/override.txt","w"); if(!f) return;
  f.printf("V%d:%d",OVERRIDE_FILE_VERSION,manual_override); f.close();
}
void loadProtectionSettings(){
  if(!LittleFS.exists("/protection.json")) return;
  File f=LittleFS.open("/protection.json","r"); if(!f) return;
  StaticJsonDocument<128> doc; DeserializationError e=deserializeJson(doc,f); f.close();
  if(!e && doc.containsKey("minOffMinutes")){
    int v=doc["minOffMinutes"]|3; antiShortCycleMinutes=constrain(v,0,MAX_ANTI_SHORT_CYCLE_MINUTES);
  }
}
void saveProtectionSettings(){
  StaticJsonDocument<128> doc; doc["version"]=PROTECTION_FILE_VERSION; doc["minOffMinutes"]=antiShortCycleMinutes;
  File f=LittleFS.open("/protection.json","w"); if(!f) return;
  size_t w=serializeJson(doc,f); f.close();
  if(w==0) LittleFS.remove("/protection.json");
}

void saveTimeSetting(){
  timeSaveSeq++; timeFileSlot=1-timeFileSlot;
  File f=LittleFS.open(TIME_FILES[timeFileSlot],"w"); if(!f) return;
  size_t w=f.printf("V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d",TIME_FILE_VERSION,currentHour,currentMinute,currentSecond,time_synchronized?1:0,timeSaveSeq,currentYear,currentMonth,currentDay,currentWeekday);
  f.close(); if(w==0) LittleFS.remove(TIME_FILES[timeFileSlot]);
  lastTimeSaveMillis=millis();
}
bool readTimeFile(const char*p,int&h,int&m,int&s,int&sy,unsigned long&sq,int&y,int&mo,int&d,int&wd){
  if(!LittleFS.exists(p)) return false;
  File f=LittleFS.open(p,"r"); if(!f) return false;
  String v=f.readString(); f.close();
  int th=-1,tm=-1,ts=-1,tsync=-1,ty=2026,tmon=1,td=1,twd=3,fv=1;
  unsigned long tseq=0; int pr;
  if(v.startsWith("V")) pr=sscanf(v.c_str(),"V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d",&fv,&th,&tm,&ts,&tsync,&tseq,&ty,&tmon,&td,&twd);
  else pr=sscanf(v.c_str(),"%d:%d:%d:%d:%lu:%d:%d:%d:%d",&th,&tm,&ts,&tsync,&tseq,&ty,&tmon,&td,&twd);
  if(pr<4||th<0||th>23||tm<0||tm>59||ts<0||ts>59||(tsync!=0&&tsync!=1)) return false;
  if((fv>=2||pr>=9)&&(ty<2024||tmon<1||tmon>12||td<1||td>31||twd<0||twd>6)) return false;
  h=th;m=tm;s=ts;sy=tsync;y=ty;mo=tmon;d=td;wd=twd;
  sq=((fv>=2&&pr>=10)||(fv==1&&pr>=9))?tseq:0;
  return true;
}
void loadTimeSetting(){
  int h0,m0,s0,s0_,y0,mo0,d0,wd0; unsigned long q0;
  int h1,m1,s1,s1_,y1,mo1,d1,wd1; unsigned long q1;
  bool o0=readTimeFile(TIME_FILES[0],h0,m0,s0,s0_,q0,y0,mo0,d0,wd0);
  bool o1=readTimeFile(TIME_FILES[1],h1,m1,s1,s1_,q1,y1,mo1,d1,wd1);
  if(o0&&o1){ if(q1>=q0)o0=false; else o1=false; }
  if(o0){currentHour=h0;currentMinute=m0;currentSecond=s0;currentYear=y0;currentMonth=mo0;currentDay=d0;currentWeekday=wd0;time_synchronized=(s0_==1&&q0>0);timeFileSlot=0;timeSaveSeq=q0;}
  else if(o1){currentHour=h1;currentMinute=m1;currentSecond=s1;currentYear=y1;currentMonth=mo1;currentDay=d1;currentWeekday=wd1;time_synchronized=(s1_==1&&q1>0);timeFileSlot=1;timeSaveSeq=q1;}
  else {currentHour=0;currentMinute=0;currentSecond=0;time_synchronized=false;}
}

void saveRelayStats(){
  relayStatSaveSeq++; relayStatFileSlot=1-relayStatFileSlot;
  File f=LittleFS.open(RELAY_STAT_FILES[relayStatFileSlot],"w"); if(!f) return;
  size_t w=f.printf("V%d:%lu:%lu:%lu",RELAY_STAT_FILE_VERSION,relaySwitchCount,relayTotalOnSeconds,relayStatSaveSeq); f.close();
  if(w==0) LittleFS.remove(RELAY_STAT_FILES[relayStatFileSlot]);
  lastRelayStatSaveMillis=millis();
}
bool readRelayStatFile(const char*p,unsigned long&sc,unsigned long&tos,unsigned long&sq){
  if(!LittleFS.exists(p)) return false;
  File f=LittleFS.open(p,"r"); if(!f) return false;
  String v=f.readString(); f.close();
  unsigned long tsc=0,ttos=0,tseq=0; int fv=1;
  int pr=v.startsWith("V")?sscanf(v.c_str(),"V%d:%lu:%lu:%lu",&fv,&tsc,&ttos,&tseq):sscanf(v.c_str(),"%lu:%lu:%lu",&tsc,&ttos,&tseq);
  if(pr!=(v.startsWith("V")?4:3)) return false;
  sc=tsc;tos=ttos;sq=tseq; return true;
}
void loadRelayStats(){
  unsigned long sc0,tos0,sq0,sc1,tos1,sq1;
  bool o0=readRelayStatFile(RELAY_STAT_FILES[0],sc0,tos0,sq0);
  bool o1=readRelayStatFile(RELAY_STAT_FILES[1],sc1,tos1,sq1);
  if(o0&&o1){ if(sq1>=sq0)o0=false; else o1=false; }
  if(o0){relaySwitchCount=sc0;relayTotalOnSeconds=tos0;relayStatFileSlot=0;relayStatSaveSeq=sq0;}
  else if(o1){relaySwitchCount=sc1;relayTotalOnSeconds=tos1;relayStatFileSlot=1;relayStatSaveSeq=sq1;}
  else {relaySwitchCount=0;relayTotalOnSeconds=0;}
}

void loadNtpSuccessInfo(){
  ntpLastSuccessValid=false;
  if(!LittleFS.exists(NTP_META_FILE)) return;
  File f=LittleFS.open(NTP_META_FILE,"r"); if(!f) return;
  StaticJsonDocument<192> doc; DeserializationError e=deserializeJson(doc,f); f.close();
  if(e) return;
  int y=doc["y"]|0,mon=doc["mon"]|0,d=doc["d"]|0,h=doc["h"]|-1,m=doc["m"]|-1,s=doc["s"]|-1;
  if(y<2024||mon<1||mon>12||d<1||d>31||h<0||h>23||m<0||m>59||s<0||s>59) return;
  ntpLastSuccessYear=y;ntpLastSuccessMonth=mon;ntpLastSuccessDay=d;
  ntpLastSuccessHour=h;ntpLastSuccessMinute=m;ntpLastSuccessSecond=s;
  ntpLastSuccessValid=true;ntpEverSucceeded=true;
}
void saveNtpSuccessInfo(){
  if(!ntpLastSuccessValid) return;
  StaticJsonDocument<192> doc;
  doc["version"]=NTP_META_FILE_VERSION;
  doc["y"]=ntpLastSuccessYear;doc["mon"]=ntpLastSuccessMonth;doc["d"]=ntpLastSuccessDay;
  doc["h"]=ntpLastSuccessHour;doc["m"]=ntpLastSuccessMinute;doc["s"]=ntpLastSuccessSecond;
  File f=LittleFS.open(NTP_META_FILE,"w"); if(!f) return;
  size_t w=serializeJson(doc,f); f.close();
  if(w==0) LittleFS.remove(NTP_META_FILE);
}

void setStaConnectionState(bool on){
  if(on){ staCurrentlyOn=true; staCycleLastToggleMillis=millis(); connectToInternetWiFi(); }
  else { WiFi.setAutoReconnect(false); WiFi.disconnect(false); staCurrentlyOn=false; staCycleLastToggleMillis=millis(); staEverConnectedThisBoot=false; }
}
void manageStaCycle(){
  if(!internet_enabled || strlen(sta_ssid)==0){
    if(staCurrentlyOn||WiFi.status()==WL_CONNECTED){ WiFi.setAutoReconnect(false); WiFi.disconnect(false); staCurrentlyOn=false; }
    staCycleLastToggleMillis=millis(); return;
  }
  if(staOffMinutes==0){ if(!staCurrentlyOn) setStaConnectionState(true); return; }
  unsigned long onMs=(unsigned long)staOnMinutes*60000UL, offMs=(unsigned long)staOffMinutes*60000UL;
  unsigned long el=millis()-staCycleLastToggleMillis;
  if(staCurrentlyOn && el>=onMs) setStaConnectionState(false);
  else if(!staCurrentlyOn && el>=offMs) setStaConnectionState(true);
}
void connectToInternetWiFi(){
  if(!internet_enabled){ WiFi.setAutoReconnect(false); WiFi.disconnect(false); return; }
  if(strlen(sta_ssid)==0){ WiFi.setAutoReconnect(false); WiFi.disconnect(false); return; }
  WiFi.setAutoReconnect(true);
  if(strlen(sta_password)>0) WiFi.begin(sta_ssid,sta_password); else WiFi.begin(sta_ssid);
  configTime(NTP_GMT_OFFSET_SEC,0,"ir.pool.ntp.org","ntp.nic.ir","pool.ntp.org");
  applyApTxPower();
}
void tryNtpSync(){
  unsigned long iv = ntp_synced_this_boot ? NTP_RECHECK_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;
  if(!internet_enabled||!staCurrentlyOn||strlen(sta_ssid)==0) return;
  if(ntpFirstCheckPending){ ntpFirstCheckPending=false; }
  else if(millis()-lastNtpCheckMillis<iv) return;
  if(WiFi.status()!=WL_CONNECTED) return;
  lastNtpCheckMillis=millis();
  time_t now=time(nullptr); struct tm* t=localtime(&now);
  if(t && (t->tm_year+1900)>=2024){
    currentHour=t->tm_hour;currentMinute=t->tm_min;currentSecond=t->tm_sec;
    currentYear=t->tm_year+1900;currentMonth=t->tm_mon+1;currentDay=t->tm_mday;
    currentWeekday=(t->tm_wday+1)%7; lastTick=millis();
    time_synchronized=true; ntp_synced_this_boot=true; ntpEverSucceeded=true;
    ntpLastSuccessYear=currentYear;ntpLastSuccessMonth=currentMonth;ntpLastSuccessDay=currentDay;
    ntpLastSuccessHour=currentHour;ntpLastSuccessMinute=currentMinute;ntpLastSuccessSecond=currentSecond;
    ntpLastSuccessValid=true;
    saveTimeSetting(); saveNtpSuccessInfo();
  }
}

// ===================== Handlers =====================
void handleSyncTime(){
  if(!allowRequest(lastSyncRequest,1000UL)) return;
  sendCORS();
  if(server.hasArg("h")&&server.hasArg("m")&&server.hasArg("s")&&server.hasArg("y")&&server.hasArg("mon")&&server.hasArg("d")&&server.hasArg("wd")){
    int h=server.arg("h").toInt(),m=server.arg("m").toInt(),s=server.arg("s").toInt();
    int y=server.arg("y").toInt(),mon=server.arg("mon").toInt(),d=server.arg("d").toInt(),wd=server.arg("wd").toInt();
    if(h<0||h>23||m<0||m>59||s<0||s>59||y<2024||mon<1||mon>12||d<1||d>31||wd<0||wd>6){ server.send(400,"text/plain","Invalid Time Range"); return; }
    currentHour=h;currentMinute=m;currentSecond=s;currentYear=y;currentMonth=mon;currentDay=d;currentWeekday=wd;
    lastTick=millis(); time_synchronized=true; saveTimeSetting(); server.send(200,"text/plain","OK");
  } else server.send(400,"text/plain","Bad Request");
}

void handleToggleManual(){
  if(!allowRequest(lastToggleManualRequest,1500UL)) return;
  sendCORS();
  if(manual_override==0){ manual_override=1; saveOverrideSetting(); if(time_synchronized) saveTimeSetting(); delay(100); checkScenarios(); }
  else { manual_override=0; saveOverrideSetting(); if(time_synchronized) saveTimeSetting(); delay(100); checkScenarios(); }
  server.send(200,"text/plain","OK");
}

void handleSaveAP(){
  if(!allowRequest(lastSaveApRequest,3000UL)) return;
  sendCORS();
  if(server.hasArg("ssid")&&server.hasArg("pass")){
    String ns=server.arg("ssid"),np=server.arg("pass");
    if(ns.length()>0&&np.length()>=8){
      ns.toCharArray(custom_ssid,32); np.toCharArray(custom_password,32);
      saveWiFiSettings(); saveTimeSetting();
      server.send(200,"text/plain","OK");
      pendingReset=true; resetMillis=millis(); return;
    }
  }
  server.send(400,"text/plain","Bad Request");
}

void handleSaveSTA(){
  if(!allowRequest(lastSaveStaRequest,1500UL)) return;
  sendCORS();
  String ns=server.hasArg("sta_ssid")?server.arg("sta_ssid"):"";
  String np=server.hasArg("sta_pass")?server.arg("sta_pass"):"";
  String ni=server.hasArg("internet")?server.arg("internet"):"";
  String non=server.hasArg("sta_on_minutes")?server.arg("sta_on_minutes"):"";
  String noff=server.hasArg("sta_off_minutes")?server.arg("sta_off_minutes"):"";
  if(np.length()>0&&np.length()<8){ server.send(400,"text/plain","Bad Request"); return; }
  if(non.length()==0||noff.length()==0){ server.send(400,"text/plain","Bad Request"); return; }
  for(size_t i=0;i<non.length();i++) if(!isDigit(non[i])){ server.send(400,"text/plain","Bad Request"); return; }
  for(size_t i=0;i<noff.length();i++) if(!isDigit(noff[i])){ server.send(400,"text/plain","Bad Request"); return; }
  int ov=non.toInt(),fv=noff.toInt();
  if(ov<MIN_STA_ON_MINUTES||ov>MAX_STA_CYCLE_MINUTES||fv<MIN_STA_OFF_MINUTES||fv>MAX_STA_CYCLE_MINUTES){ server.send(400,"text/plain","Bad Request"); return; }
  internet_enabled=(ni=="1"); staOnMinutes=ov; staOffMinutes=fv;
  ns.toCharArray(sta_ssid,32); np.toCharArray(sta_password,64);
  saveWiFiSettings();
  ntp_synced_this_boot=false; ntpFirstCheckPending=true; lastNtpCheckMillis=0; staEverConnectedThisBoot=false;
  WiFi.setAutoReconnect(false); WiFi.disconnect(false);
  staCurrentlyOn=true; staCycleLastToggleMillis=millis(); connectToInternetWiFi();
  server.send(200,"text/plain","OK");
}

void handleSaveProtection(){
  if(!allowRequest(lastSaveProtectionRequest,1000UL)) return;
  sendCORS();
  if(!server.hasArg("min_off")){ server.send(400,"text/plain","Bad Request"); return; }
  String v=server.arg("min_off");
  if(v.length()==0){ server.send(400,"text/plain","Bad Request"); return; }
  for(size_t i=0;i<v.length();i++) if(!isDigit(v[i])){ server.send(400,"text/plain","Bad Request"); return; }
  int x=v.toInt();
  if(x<0||x>MAX_ANTI_SHORT_CYCLE_MINUTES){ server.send(400,"text/plain","Bad Request"); return; }
  antiShortCycleMinutes=x; saveProtectionSettings(); server.send(200,"text/plain","OK");
}

void handleSaveApCycle(){
  if(!allowRequest(lastSaveApCycleRequest,1000UL)) return;
  sendCORS();
  if(!server.hasArg("on_minutes")||!server.hasArg("off_minutes")||!server.hasArg("tx_power")){ server.send(400,"text/plain","Bad Request"); return; }
  String onR=server.arg("on_minutes"),ofR=server.arg("off_minutes"),pR=server.arg("tx_power"),enR=server.hasArg("cycle_enabled")?server.arg("cycle_enabled"):"";
  if(onR.length()==0||ofR.length()==0||pR.length()==0){ server.send(400,"text/plain","Bad Request"); return; }
  for(size_t i=0;i<onR.length();i++) if(!isDigit(onR[i])){ server.send(400,"text/plain","Bad Request"); return; }
  for(size_t i=0;i<ofR.length();i++) if(!isDigit(ofR[i])){ server.send(400,"text/plain","Bad Request"); return; }
  for(size_t i=0;i<pR.length();i++)  if(!isDigit(pR[i])) { server.send(400,"text/plain","Bad Request"); return; }
  int on=onR.toInt(),of=ofR.toInt(),pv=pR.toInt();
  if(on<MIN_AP_CYCLE_MINUTES||on>MAX_AP_CYCLE_MINUTES||of<MIN_AP_CYCLE_MINUTES||of>MAX_AP_CYCLE_MINUTES||pv<0||pv>MAX_AP_TX_POWER_LEVEL){ server.send(400,"text/plain","Bad Request"); return; }
  apCycleEnabled=(enR=="1"); apOnMinutes=on; apOffMinutes=of; apTxPowerLevel=pv; applyApTxPower();
  apCycleLastToggleMillis=millis(); if(!apCurrentlyOn) setApRadioState(true);
  saveWiFiSettings(); server.send(200,"text/plain","OK");
}

void handleSaveScenario(){
  if(!allowRequest(lastSaveScenarioRequest,1200UL)) return;
  sendCORS();
  if(!server.hasArg("plain")){ server.send(400,"text/plain","Bad Request"); return; }
  String b=server.arg("plain");
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  DeserializationError err=deserializeJson(doc,b);
  if(err||!doc.is<JsonArray>()){ server.send(400,"text/plain","Invalid JSON"); return; }
  JsonArray arr=doc.as<JsonArray>();
  Scenario tmp[MAX_SCENARIOS]; int i=0;
  for(JsonObject v:arr){
    if(i>=MAX_SCENARIOS) break;
    tmp[i].active=true;
    tmp[i].enabled=v["en"]|true;
    tmp[i].startHour=v["sh"]|0; tmp[i].startMinute=v["sm"]|0;
    tmp[i].endHour=v["eh"]|0;   tmp[i].endMinute=v["em"]|0;
    tmp[i].weekdays=v.containsKey("wd")?(uint8_t)(v["wd"]|0x7F):0x7F;
    i++;
  }
  bool ch=false;
  for(int j=0;j<MAX_SCENARIOS;j++){
    if(scenarios[j].active!=tmp[j].active||scenarios[j].enabled!=tmp[j].enabled||
       scenarios[j].startHour!=tmp[j].startHour||scenarios[j].startMinute!=tmp[j].startMinute||
       scenarios[j].endHour!=tmp[j].endHour||scenarios[j].endMinute!=tmp[j].endMinute||
       scenarios[j].weekdays!=tmp[j].weekdays){ ch=true; break; }
  }
  if(ch){ for(int j=0;j<MAX_SCENARIOS;j++) scenarios[j]=tmp[j]; saveScenarios(); }
  server.send(200,"text/plain","Updated");
}

void handleGetStatus(){
  if(!allowRequest(lastStatusRequest,150UL)) return;
  sendCORS();
  char ts[9]; sprintf(ts,"%02d:%02d:%02d",currentHour,currentMinute,currentSecond);
  int logState=(digitalRead(RELAY_PIN)==RELAY_ACTIVE_LEVEL)?1:0;
  int stac=(WiFi.status()==WL_CONNECTED)?1:0;
  int stacfg=(strlen(sta_ssid)>0)?1:0;
  char sip[16]="";
  if(stac){ IPAddress ip=WiFi.localIP(); snprintf(sip,sizeof(sip),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]); }
  int staState;
  if(!internet_enabled||!stacfg) staState=0;
  else if(staOffMinutes>0 && !staCurrentlyOn) staState=4;
  else if(stac){ staEverConnectedThisBoot=true; staState=2; }
  else if(staEverConnectedThisBoot) staState=3;
  else staState=1;
  unsigned long liveOn=relayTotalOnSeconds;
  if(relayCurrentlyOnForStats) liveOn += (millis()-relayOnSinceMillis)/1000UL;
  long protRem=0;
  if(antiShortCycleMinutes>0 && lastRelayStatState==0){
    unsigned long req=(unsigned long)antiShortCycleMinutes*60UL*1000UL;
    unsigned long el=millis()-lastRelayOffMillis;
    if(el<req) protRem=(long)((req-el+999UL)/1000UL);
  }
  int apClients = apCurrentlyOn ? WiFi.softAPgetStationNum() : 0;
  long apRem=0;
  if(apCycleEnabled){
    unsigned long ph=apCurrentlyOn?((unsigned long)apOnMinutes*60000UL):((unsigned long)apOffMinutes*60000UL);
    unsigned long el=millis()-apCycleLastToggleMillis;
    if(el<ph) apRem=(long)((ph-el+999UL)/1000UL);
  }
  long staRem=0;
  if(internet_enabled && stacfg && staOffMinutes>0){
    unsigned long ph=staCurrentlyOn?((unsigned long)staOnMinutes*60000UL):((unsigned long)staOffMinutes*60000UL);
    unsigned long el=millis()-staCycleLastToggleMillis;
    if(el<ph) staRem=(long)((ph-el+999UL)/1000UL);
  }
  char json[1152];
  snprintf(json,sizeof(json),
    "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"sta\":%d,\"internetEnabled\":%d,\"staConfigured\":%d,"
    "\"staState\":%d,\"staIp\":\"%s\",\"staOnMinutes\":%d,\"staOffMinutes\":%d,\"staPhaseOn\":%d,\"staRemaining\":%ld,"
    "\"ntpOk\":%d,\"ntpLastValid\":%d,\"ntpYear\":%d,\"ntpMonth\":%d,\"ntpDay\":%d,\"ntpHour\":%d,\"ntpMinute\":%d,\"ntpSecond\":%d,"
    "\"switchCount\":%lu,\"onSeconds\":%lu,\"weekday\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"protectionMinutes\":%d,\"protectionRemaining\":%ld,"
    "\"apCycleEnabled\":%d,\"apOn\":%d,\"apOnMinutes\":%d,\"apOffMinutes\":%d,\"apTxPowerLevel\":%d,\"apRemaining\":%ld,\"apClientConnected\":%d}",
    ts,logState,manual_override,time_synchronized?1:0,stac,internet_enabled?1:0,stacfg,staState,sip,
    staOnMinutes,staOffMinutes,staCurrentlyOn?1:0,staRem,
    ntpEverSucceeded?1:0,ntpLastSuccessValid?1:0,ntpLastSuccessYear,ntpLastSuccessMonth,ntpLastSuccessDay,
    ntpLastSuccessHour,ntpLastSuccessMinute,ntpLastSuccessSecond,relaySwitchCount,liveOn,currentWeekday,
    currentYear,currentMonth,currentDay,antiShortCycleMinutes,protRem,
    apCycleEnabled?1:0,apCurrentlyOn?1:0,apOnMinutes,apOffMinutes,apTxPowerLevel,apRem,(apClients>0)?1:0);
  server.send(200,"application/json",json);
}

// endpoint جدید: تنظیمات (برای پر کردن فرم‌های تب وای‌فای در اپ)
void handleGetConfig(){
  if(!allowRequest(lastConfigRequest,1000UL)) return;
  sendCORS();
  StaticJsonDocument<512> doc;
  doc["apSsid"] = custom_ssid;
  // توجه: هرگز پسورد AP/STA برگردانده نمی‌شود (امنیت)
  doc["staSsid"] = sta_ssid;
  doc["internetEnabled"] = internet_enabled;
  doc["staOnMinutes"] = staOnMinutes; doc["staOffMinutes"] = staOffMinutes;
  doc["apCycleEnabled"] = apCycleEnabled; doc["apOnMinutes"]=apOnMinutes; doc["apOffMinutes"]=apOffMinutes;
  doc["apTxPowerLevel"] = apTxPowerLevel;
  doc["minOffMinutes"] = antiShortCycleMinutes;
  doc["deviceName"] = PROGRAM_NAME; doc["deviceTag"] = PROGRAM_TAGLINE;
  String out; serializeJson(doc,out);
  server.send(200,"application/json",out);
}

// endpoint جدید: سناریوهای فعلی (برای پر کردن خودکار فرم سناریوها در اپ)
void handleGetScenarios(){
  if(!allowRequest(lastScenariosRequest,1000UL)) return;
  sendCORS();
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  JsonArray arr = doc.to<JsonArray>();
  for(int i=0;i<MAX_SCENARIOS;i++){
    if(!scenarios[i].active) continue;
    JsonObject o = arr.createNestedObject();
    o["sh"]=scenarios[i].startHour; o["sm"]=scenarios[i].startMinute;
    o["eh"]=scenarios[i].endHour;   o["em"]=scenarios[i].endMinute;
    o["en"]=scenarios[i].enabled;
    o["wd"]=scenarios[i].weekdays;
  }
  String out; serializeJson(doc,out);
  server.send(200,"application/json",out);
}
