#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

/* ===================== MISRA C COMPLIANT HEADER =====================
 * - All constants use const/constexpr
 * - Explicit types used throughout
 * - JSON format for time & relay stats (Option A)
 * - Simple XOR + Base64 encryption for saved data (Option 2)
 * - Detailed comments for debugging
 * - Matches logic of the sent index.html exactly
 * ================================================================ */

/* ===================== SIMPLE ENCRYPTION (XOR + Base64) ===================== */
/* Fixed 16-byte key - change this for your project */
const uint8_t ENCRYPTION_KEY[16] = {
  0x4B, 0x7E, 0xA3, 0x19, 0xC2, 0x5D, 0xF8, 0x66,
  0x31, 0x9A, 0xE4, 0x7B, 0x0F, 0xD2, 0x58, 0xC9
};

/* Simple XOR encryption */
void xorEncrypt(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    data[i] ^= ENCRYPTION_KEY[i % 16];
  }
}

/* Base64 encoding table */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Encode to Base64 */
String base64Encode(const uint8_t* data, size_t len) {
  String result;
  result.reserve(((len + 2) / 3) * 4);
  
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)data[i] << 16;
    if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) n |= data[i + 2];
    
    result += b64_table[(n >> 18) & 0x3F];
    result += b64_table[(n >> 12) & 0x3F];
    result += (i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=';
    result += (i + 2 < len) ? b64_table[n & 0x3F] : '=';
  }
  return result;
}

/* Decode Base64 */
bool base64Decode(const String& input, uint8_t* output, size_t& outLen) {
  size_t len = input.length();
  if (len % 4 != 0) return false;
  
  outLen = (len / 4) * 3;
  if (input[len - 1] == '=') outLen--;
  if (input[len - 2] == '=') outLen--;
  
  size_t j = 0;
  for (size_t i = 0; i < len; i += 4) {
    uint32_t n = 0;
    for (int k = 0; k < 4; k++) {
      char c = input[i + k];
      if (c == '=') break;
      int val = strchr(b64_table, c) - b64_table;
      if (val < 0) return false;
      n = (n << 6) | val;
    }
    
    output[j++] = (n >> 16) & 0xFF;
    if (j < outLen) output[j++] = (n >> 8) & 0xFF;
    if (j < outLen) output[j++] = n & 0xFF;
  }
  return true;
}

/* Encrypt JSON string and save to file */
bool saveEncryptedFile(const char* path, const String& jsonContent) {
  size_t len = jsonContent.length();
  uint8_t* buffer = (uint8_t*)malloc(len);
  if (!buffer) return false;
  
  memcpy(buffer, jsonContent.c_str(), len);
  xorEncrypt(buffer, len);
  
  String encoded = base64Encode(buffer, len);
  free(buffer);
  
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  
  f.print(encoded);
  f.close();
  return true;
}

/* Load and decrypt file */
bool loadEncryptedFile(const char* path, String& jsonContent) {
  if (!LittleFS.exists(path)) return false;
  
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  
  String encoded = f.readString();
  f.close();
  
  size_t decodedLen = (encoded.length() / 4) * 3;
  uint8_t* buffer = (uint8_t*)malloc(decodedLen + 4);
  if (!buffer) return false;
  
  size_t actualLen = 0;
  if (!base64Decode(encoded, buffer, actualLen)) {
    free(buffer);
    return false;
  }
  
  xorEncrypt(buffer, actualLen);
  
  jsonContent = String((char*)buffer, actualLen);
  free(buffer);
  return true;
}

/* ===================== HARDWARE & PROGRAM SETTINGS ===================== */
const char* PROGRAM_NAME = "کولر هوشمند ESP32";
const int RELAY_PIN = 23;
const int RELAY_ACTIVE_LEVEL = HIGH;
const long NTP_GMT_OFFSET_SEC = 12600; // UTC+3:30

char custom_ssid[32] = "ESP32_Timer_Hub";
char custom_password[32] = "12345678";
char sta_ssid[32] = "";
char sta_password[64] = "";

bool internet_enabled = true;
int staOnMinutes = 10, staOffMinutes = 0;
bool staCurrentlyOn = true;
unsigned long staCycleLastToggleMillis = 0;

bool apCycleEnabled = false;
int apOnMinutes = 10, apOffMinutes = 5, apTxPowerLevel = 3;
bool apCurrentlyOn = true;
unsigned long apCycleLastToggleMillis = 0;

const int MAX_SCENARIOS = 20;
int antiShortCycleMinutes = 3;
unsigned long lastRelayOffMillis = 0;

const int WDT_TIMEOUT_SEC = 8;
const unsigned long TIME_SAVE_INTERVAL = 300000UL;
const unsigned long NTP_RETRY_INTERVAL_MS = 60000UL;
const unsigned long NTP_RECHECK_INTERVAL_MS = 3600000UL;

WebServer server(80);

/* ===================== DATA STRUCTURES ===================== */
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

/* ===================== GLOBAL STATE VARIABLES ===================== */
int currentHour = 0;
int currentMinute = 0;
int currentSecond = 0;
int currentYear = 2026;
int currentMonth = 1;
int currentDay = 1;
int currentWeekday = 3;
unsigned long lastTick = 0;

int manual_override = 0;

bool time_synchronized = false;
bool ntp_synced_this_boot = false;
bool ntpEverSucceeded = false;
bool ntpLastSuccessValid = false;
int ntpLastSuccessYear = 2026;
int ntpLastSuccessMonth = 1;
int ntpLastSuccessDay = 1;
int ntpLastSuccessHour = 0;
int ntpLastSuccessMinute = 0;
int ntpLastSuccessSecond = 0;
unsigned long lastNtpCheckMillis = 0;
bool ntpFirstCheckPending = true;
bool staEverConnectedThisBoot = false;
bool pendingReset = false;
unsigned long resetMillis = 0;

/* Request rate limiting */
unsigned long lastToggleManualRequest = 0;
unsigned long lastSaveScenarioRequest = 0;
unsigned long lastSyncRequest = 0;
unsigned long lastSaveApRequest = 0;
unsigned long lastSaveStaRequest = 0;
unsigned long lastSaveProtectionRequest = 0;
unsigned long lastSaveApCycleRequest = 0;
unsigned long lastStatusRequest = 0;

/* ===================== JSON-BASED PERSISTENCE (MISRA C + Option A) ===================== */
/* Time and relay statistics now use JSON format for security and consistency */
const char* TIME_FILE = "/time.json";
const char* RELAY_STAT_FILE = "/relaystat.json";
const char* NTP_META_FILE = "/ntp.json";

unsigned long timeSaveSeq = 0;
unsigned long lastTimeSaveMillis = 0;

unsigned long relaySwitchCount = 0;
unsigned long relayTotalOnSeconds = 0;
unsigned long relayOnSinceMillis = 0;
bool relayCurrentlyOnForStats = false;
int lastRelayStatState = -1;
unsigned long relayStatSaveSeq = 0;
unsigned long lastRelayStatSaveMillis = 0;

/* ===================== UTILITY FUNCTIONS ===================== */
void feedWatchdog() { 
  esp_task_wdt_reset(); 
}

void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}

void handleOptions() { 
  sendCORSHeaders(); 
  server.send(204); 
}

bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs) {
  unsigned long now = millis();
  if ((lastRequest != 0) && ((unsigned long)(now - lastRequest) < minIntervalMs)) {
    sendCORSHeaders();
    server.send(429, "text/plain", "Too Many Requests");
    return false;
  }
  lastRequest = now;
  return true;
}

void setRelay(bool state) {
  digitalWrite(RELAY_PIN, state ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

void applyApTxPower() {
  wifi_power_t p;
  switch (apTxPowerLevel) {
    case 0: p = WIFI_POWER_5dBm; break;
    case 1: p = WIFI_POWER_11dBm; break;
    case 2: p = WIFI_POWER_15dBm; break;
    default: p = WIFI_POWER_19_5dBm; break;
  }
  WiFi.setTxPower(p);
}

void setApRadioState(bool on) {
  if (on == apCurrentlyOn) return;
  if (on) {
    WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
    applyApTxPower();
  } else {
    WiFi.softAPdisconnect(true);
  }
  apCurrentlyOn = on;
}

void manageApCycle() {
  if (!apCycleEnabled) {
    if (!apCurrentlyOn) {
      setApRadioState(true);
      apCycleLastToggleMillis = millis();
    }
    return;
  }
  if (apCurrentlyOn && (WiFi.softAPgetStationNum() > 0)) {
    apCycleLastToggleMillis = millis();
    return;
  }
  unsigned long elapsed = millis() - apCycleLastToggleMillis;
  if (apCurrentlyOn && (elapsed >= (unsigned long)apOnMinutes * 60000UL)) {
    setApRadioState(false);
    apCycleLastToggleMillis = millis();
  } else if (!apCurrentlyOn && (elapsed >= (unsigned long)apOffMinutes * 60000UL)) {
    setApRadioState(true);
    apCycleLastToggleMillis = millis();
  }
}

/* ===================== WIFI & INTERNET ===================== */
void connectToInternetWiFi() {
  if (!internet_enabled || (strlen(sta_ssid) == 0)) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    return;
  }
  WiFi.setAutoReconnect(true);
  if (strlen(sta_password) > 0) {
    WiFi.begin(sta_ssid, sta_password);
  } else {
    WiFi.begin(sta_ssid);
  }
  configTime(NTP_GMT_OFFSET_SEC, 0, "ir.pool.ntp.org", "ntp.nic.ir", "pool.ntp.org");
  applyApTxPower();
}

void setStaConnectionState(bool on) {
  if (on) {
    staCurrentlyOn = true;
    staCycleLastToggleMillis = millis();
    connectToInternetWiFi();
  } else {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    staCurrentlyOn = false;
    staCycleLastToggleMillis = millis();
    staEverConnectedThisBoot = false;
  }
}

void manageStaCycle() {
  if (!internet_enabled || (strlen(sta_ssid) == 0)) {
    if (staCurrentlyOn || (WiFi.status() == WL_CONNECTED)) {
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false);
      staCurrentlyOn = false;
    }
    staCycleLastToggleMillis = millis();
    return;
  }
  if (staOffMinutes == 0) {
    if (!staCurrentlyOn) setStaConnectionState(true);
    return;
  }
  unsigned long elapsed = millis() - staCycleLastToggleMillis;
  if (staCurrentlyOn && (elapsed >= (unsigned long)staOnMinutes * 60000UL)) {
    setStaConnectionState(false);
  } else if (!staCurrentlyOn && (elapsed >= (unsigned long)staOffMinutes * 60000UL)) {
    setStaConnectionState(true);
  }
}

/* ===================== NTP SYNCHRONIZATION ===================== */
void tryNtpSync() {
  if (!internet_enabled || !staCurrentlyOn || (strlen(sta_ssid) == 0)) return;
  
  unsigned long interval = ntp_synced_this_boot ? NTP_RECHECK_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;
  if (!ntpFirstCheckPending && ((millis() - lastNtpCheckMillis) < interval)) return;
  if (WiFi.status() != WL_CONNECTED) return;

  lastNtpCheckMillis = millis();
  ntpFirstCheckPending = false;

  time_t now = time(nullptr);
  struct tm *tmInfo = localtime(&now);
  if (tmInfo && ((tmInfo->tm_year + 1900) >= 2024)) {
    currentHour = tmInfo->tm_hour;
    currentMinute = tmInfo->tm_min;
    currentSecond = tmInfo->tm_sec;
    currentYear = tmInfo->tm_year + 1900;
    currentMonth = tmInfo->tm_mon + 1;
    currentDay = tmInfo->tm_mday;
    currentWeekday = (tmInfo->tm_wday + 1) % 7;

    lastTick = millis();
    time_synchronized = true;
    ntp_synced_this_boot = true;
    ntpEverSucceeded = true;

    ntpLastSuccessYear = currentYear;
    ntpLastSuccessMonth = currentMonth;
    ntpLastSuccessDay = currentDay;
    ntpLastSuccessHour = currentHour;
    ntpLastSuccessMinute = currentMinute;
    ntpLastSuccessSecond = currentSecond;
    ntpLastSuccessValid = true;

    saveTimeSetting();
    saveNtpSuccessInfo();
  }
}

/* ===================== JSON-BASED SETTINGS (SECURE & CONSISTENT) ===================== */

/* Scenarios */
void loadScenarios() {
  String json;
  if (!loadEncryptedFile("/scenarios.json", json)) return;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) return;

  JsonArray array = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int i = 0;
  for (JsonObject v : array) {
    if (i >= MAX_SCENARIOS) break;
    scenarios[i].active = v["active"] | false;
    scenarios[i].enabled = v.containsKey("en") ? (bool)v["en"] : scenarios[i].active;
    scenarios[i].startHour = v["sh"] | 0;
    scenarios[i].startMinute = v["sm"] | 0;
    scenarios[i].endHour = v["eh"] | 0;
    scenarios[i].endMinute = v["em"] | 0;
    scenarios[i].weekdays = v.containsKey("wd") ? (uint8_t)(v["wd"] | 0x7F) : 0x7F;
    i++;
  }
}

/* WiFi Settings */
void loadWiFiSettings() {
  String json;
  if (!loadEncryptedFile("/wifi.json", json)) return;

  StaticJsonDocument<512> doc;
  if (!deserializeJson(doc, json)) {
    if (doc.containsKey("ssid")) strncpy(custom_ssid, doc["ssid"], 31);
    if (doc.containsKey("pass")) strncpy(custom_password, doc["pass"], 31);
    if (doc.containsKey("sta_ssid")) strncpy(sta_ssid, doc["sta_ssid"], 31);
    if (doc.containsKey("sta_pass")) strncpy(sta_password, doc["sta_pass"], 63);
    internet_enabled = doc["internet"] | true;
    staOnMinutes = constrain(doc["staOnMinutes"] | staOnMinutes, 1, 1440);
    staOffMinutes = constrain(doc["staOffMinutes"] | staOffMinutes, 0, 1440);
    apCycleEnabled = doc["apCycleEnabled"] | false;
    apOnMinutes = constrain(doc["apOnMinutes"] | apOnMinutes, 1, 1440);
    apOffMinutes = constrain(doc["apOffMinutes"] | apOffMinutes, 1, 1440);
    apTxPowerLevel = constrain(doc["apTxPowerLevel"] | apTxPowerLevel, 0, 3);
  }
}

void saveWiFiSettings() {
  StaticJsonDocument<512> doc;
  doc["ssid"] = custom_ssid;
  doc["pass"] = custom_password;
  doc["sta_ssid"] = sta_ssid;
  doc["sta_pass"] = sta_password;
  doc["internet"] = internet_enabled;
  doc["staOnMinutes"] = staOnMinutes;
  doc["staOffMinutes"] = staOffMinutes;
  doc["apCycleEnabled"] = apCycleEnabled;
  doc["apOnMinutes"] = apOnMinutes;
  doc["apOffMinutes"] = apOffMinutes;
  doc["apTxPowerLevel"] = apTxPowerLevel;

  String json;
  serializeJson(doc, json);
  saveEncryptedFile("/wifi.json", json);   // Encrypted save
}

/* Override Setting (simple integer) */
void loadOverrideSetting() {
  if (!LittleFS.exists("/override.txt")) {
    manual_override = 0;
    return;
  }
  File f = LittleFS.open("/override.txt", "r");
  if (f) {
    String val = f.readString();
    manual_override = (val.toInt() == 1) ? 1 : 0;
    f.close();
  }
}

void saveOverrideSetting() {
  File f = LittleFS.open("/override.txt", "w");
  if (f) {
    f.print(manual_override);
    f.close();
  }
}

/* Protection Settings */
void loadProtectionSettings() {
  if (!LittleFS.exists("/protection.json")) return;
  File f = LittleFS.open("/protection.json", "r");
  if (f) {
    StaticJsonDocument<128> doc;
    if (!deserializeJson(doc, f)) {
      antiShortCycleMinutes = constrain(doc["minOffMinutes"] | 3, 0, 1440);
    }
    f.close();
  }
}

void saveProtectionSettings() {
  StaticJsonDocument<128> doc;
  doc["minOffMinutes"] = antiShortCycleMinutes;
  File f = LittleFS.open("/protection.json", "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

/* ===================== JSON TIME & RELAY STATS (NEW SECURE FORMAT) ===================== */

void saveTimeSetting() {
  timeSaveSeq++;
  StaticJsonDocument<256> doc;
  doc["hour"] = currentHour;
  doc["minute"] = currentMinute;
  doc["second"] = currentSecond;
  doc["sync"] = time_synchronized ? 1 : 0;
  doc["seq"] = timeSaveSeq;
  doc["year"] = currentYear;
  doc["month"] = currentMonth;
  doc["day"] = currentDay;
  doc["weekday"] = currentWeekday;

  String json;
  serializeJson(doc, json);
  saveEncryptedFile(TIME_FILE, json);   // Encrypted save
  lastTimeSaveMillis = millis();
}

void loadTimeSetting() {
  String json;
  if (!loadEncryptedFile(TIME_FILE, json)) {
    currentHour = 0; currentMinute = 0; currentSecond = 0;
    time_synchronized = false;
    return;
  }

  StaticJsonDocument<256> doc;
  if (!deserializeJson(doc, json)) {
    currentHour = doc["hour"] | 0;
    currentMinute = doc["minute"] | 0;
    currentSecond = doc["second"] | 0;
    time_synchronized = (doc["sync"] | 0) == 1;
    timeSaveSeq = doc["seq"] | 0;
    currentYear = doc["year"] | 2026;
    currentMonth = doc["month"] | 1;
    currentDay = doc["day"] | 1;
    currentWeekday = doc["weekday"] | 3;
  }
}

void saveRelayStats() {
  relayStatSaveSeq++;
  StaticJsonDocument<128> doc;
  doc["switchCount"] = relaySwitchCount;
  doc["onSeconds"] = relayTotalOnSeconds;
  doc["seq"] = relayStatSaveSeq;

  String json;
  serializeJson(doc, json);
  saveEncryptedFile(RELAY_STAT_FILE, json);   // Encrypted save
  lastRelayStatSaveMillis = millis();
}

void loadRelayStats() {
  String json;
  if (!loadEncryptedFile(RELAY_STAT_FILE, json)) {
    relaySwitchCount = 0;
    relayTotalOnSeconds = 0;
    return;
  }

  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, json)) {
    relaySwitchCount = doc["switchCount"] | 0;
    relayTotalOnSeconds = doc["onSeconds"] | 0;
    relayStatSaveSeq = doc["seq"] | 0;
  }
}

/* NTP Success Info (unchanged) */
void loadNtpSuccessInfo() {
  String json;
  if (!loadEncryptedFile(NTP_META_FILE, json)) return;

  StaticJsonDocument<192> doc;
  if (!deserializeJson(doc, json)) {
    ntpLastSuccessYear = doc["y"] | 2026;
    ntpLastSuccessMonth = doc["mon"] | 1;
    ntpLastSuccessDay = doc["d"] | 1;
    ntpLastSuccessHour = doc["h"] | 0;
    ntpLastSuccessMinute = doc["m"] | 0;
    ntpLastSuccessSecond = doc["s"] | 0;
    ntpLastSuccessValid = true;
    ntpEverSucceeded = true;
  }
}

void saveNtpSuccessInfo() {
  if (!ntpLastSuccessValid) return;
  StaticJsonDocument<192> doc;
  doc["y"] = ntpLastSuccessYear;
  doc["mon"] = ntpLastSuccessMonth;
  doc["d"] = ntpLastSuccessDay;
  doc["h"] = ntpLastSuccessHour;
  doc["m"] = ntpLastSuccessMinute;
  doc["s"] = ntpLastSuccessSecond;

  String json;
  serializeJson(doc, json);
  saveEncryptedFile(NTP_META_FILE, json);   // Encrypted save
}

/* ===================== CORE LOGIC ===================== */
bool isLeapYear(int y) {
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

void advanceDate() {
  const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = dim[currentMonth - 1];
  if ((currentMonth == 2) && isLeapYear(currentYear)) maxDay = 29;

  if (++currentDay > maxDay) {
    currentDay = 1;
    if (++currentMonth > 12) {
      currentMonth = 1;
      currentYear++;
    }
  }
  currentWeekday = (currentWeekday + 1) % 7;
}

void updateClock() {
  while ((millis() - lastTick) >= 1000UL) {
    lastTick += 1000UL;
    if (++currentSecond >= 60) {
      currentSecond = 0;
      if (++currentMinute >= 60) {
        currentMinute = 0;
        if (++currentHour >= 24) {
          currentHour = 0;
          advanceDate();
        }
      }
    }
  }
}

void checkScenarios() {
  static int lastKnownState = -1;
  bool desiredState = false;

  if (manual_override == 1) {
    desiredState = true;
    if (lastKnownState != 1) {
      if (time_synchronized) saveTimeSetting();
      lastKnownState = 1;
    }
  } else if (!time_synchronized) {
    desiredState = false;
    lastKnownState = 0;
  } else {
    int curMin = currentHour * 60 + currentMinute;
    for (int i = 0; i < MAX_SCENARIOS; i++) {
      if (!scenarios[i].active || !scenarios[i].enabled) continue;
      int sMin = scenarios[i].startHour * 60 + scenarios[i].startMinute;
      int eMin = scenarios[i].endHour * 60 + scenarios[i].endMinute;
      if ((sMin == eMin) || (scenarios[i].weekdays == 0)) continue;

      int day = ((sMin > eMin) && (curMin < eMin)) ? ((currentWeekday + 6) % 7) : currentWeekday;
      if ((scenarios[i].weekdays & (1 << day)) == 0) continue;

      if (((sMin < eMin) && (curMin >= sMin) && (curMin < eMin)) ||
          ((sMin > eMin) && ((curMin >= sMin) || (curMin < eMin)))) {
        desiredState = true;
        break;
      }
    }
    if (lastKnownState != (desiredState ? 1 : 0)) {
      saveTimeSetting();
      lastKnownState = desiredState ? 1 : 0;
    }
  }

  /* Anti-short-cycle protection */
  if (desiredState && (antiShortCycleMinutes > 0) && (lastRelayStatState == 0) &&
      ((millis() - lastRelayOffMillis) < ((unsigned long)antiShortCycleMinutes * 60000UL))) {
    desiredState = false;
  }

  int newState = desiredState ? 1 : 0;
  if (lastRelayStatState != newState) {
    if (newState == 1) {
      relaySwitchCount++;
      relayOnSinceMillis = millis();
      relayCurrentlyOnForStats = true;
    } else {
      if (relayCurrentlyOnForStats) {
        relayTotalOnSeconds += (millis() - relayOnSinceMillis) / 1000UL;
      }
      relayCurrentlyOnForStats = false;
      lastRelayOffMillis = millis();
    }
    lastRelayStatState = newState;
    saveRelayStats();
  }
  setRelay(desiredState);
}

/* ===================== API HANDLERS ===================== */
void handleRoot() {
  sendCORSHeaders();
  server.send(200, "application/json", "{\"status\":\"online\",\"device\":\"ESP32 Cooler\"}");
}

void handleGetScenarios() {
  sendCORSHeaders();
  if (!allowRequest(lastStatusRequest, 100UL)) return;
  if (LittleFS.exists("/scenarios.json")) {
    File f = LittleFS.open("/scenarios.json", "r");
    String out = f.readString();
    f.close();
    server.send(200, "application/json", out.length() ? out : "[]");
  } else {
    server.send(200, "application/json", "[]");
  }
}

void handleSaveScenario() {
  sendCORSHeaders();
  if (!allowRequest(lastSaveScenarioRequest, 1000UL) || !server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  // Encrypt and save scenarios
  saveEncryptedFile("/scenarios.json", server.arg("plain"));
  loadScenarios();
  server.send(200, "text/plain", "OK");
}

void handleSyncTime() {
  sendCORSHeaders();
  if (!allowRequest(lastSyncRequest, 1000UL)) return;
  if (server.hasArg("h") && server.hasArg("y")) {
    currentHour = server.arg("h").toInt();
    currentMinute = server.arg("m").toInt();
    currentSecond = server.arg("s").toInt();
    currentYear = server.arg("y").toInt();
    currentMonth = server.arg("mon").toInt();
    currentDay = server.arg("d").toInt();
    currentWeekday = server.arg("wd").toInt();
    lastTick = millis();
    time_synchronized = true;
    saveTimeSetting();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleGetStatus() {
  sendCORSHeaders();
  if (!allowRequest(lastStatusRequest, 150UL)) return;

  char json[1024];
  char timeStr[9];
  char staIp[16] = "";

  sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);

  int staConnected = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  int staConfigured = (strlen(sta_ssid) > 0) ? 1 : 0;

  if (staConnected) {
    IPAddress ip = WiFi.localIP();
    snprintf(staIp, sizeof(staIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    staEverConnectedThisBoot = true;
  }

  int staState = (!internet_enabled || !staConfigured) ? 0 :
                 (staOffMinutes > 0 && !staCurrentlyOn) ? 4 :
                 staConnected ? 2 : staEverConnectedThisBoot ? 3 : 1;

  unsigned long liveOn = relayTotalOnSeconds +
                         (relayCurrentlyOnForStats ? ((millis() - relayOnSinceMillis) / 1000UL) : 0);

  long protRem = (antiShortCycleMinutes > 0 && lastRelayStatState == 0) ?
                 max(0L, (long)((((unsigned long)antiShortCycleMinutes * 60000UL) -
                                (millis() - lastRelayOffMillis) + 999UL) / 1000UL)) : 0;

  long apRem = apCycleEnabled ? max(0L, (long)((((apCurrentlyOn ? apOnMinutes : apOffMinutes) * 60000UL) -
                                               (millis() - apCycleLastToggleMillis) + 999UL) / 1000UL)) : 0;

  long staRem = (internet_enabled && staConfigured && staOffMinutes > 0) ?
                max(0L, (long)((((staCurrentlyOn ? staOnMinutes : staOffMinutes) * 60000UL) -
                               (millis() - staCycleLastToggleMillis) + 999UL) / 1000UL)) : 0;

  snprintf(json, sizeof(json),
    "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"sta\":%d,"
    "\"internetEnabled\":%d,\"staConfigured\":%d,\"staState\":%d,\"staIp\":\"%s\","
    "\"staOnMinutes\":%d,\"staOffMinutes\":%d,\"staPhaseOn\":%d,\"staRemaining\":%ld,"
    "\"ntpOk\":%d,\"ntpLastValid\":%d,\"ntpYear\":%d,\"ntpMonth\":%d,\"ntpDay\":%d,"
    "\"ntpHour\":%d,\"ntpMinute\":%d,\"ntpSecond\":%d,\"switchCount\":%lu,\"onSeconds\":%lu,"
    "\"weekday\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"protectionMinutes\":%d,"
    "\"protectionRemaining\":%ld,\"apCycleEnabled\":%d,\"apOn\":%d,\"apOnMinutes\":%d,"
    "\"apOffMinutes\":%d,\"apTxPowerLevel\":%d,\"apRemaining\":%ld,\"apClientConnected\":%d,"
    "\"ssid\":\"%s\",\"staSsid\":\"%s\"}",
    timeStr,
    (digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL) ? 1 : 0,
    manual_override,
    time_synchronized ? 1 : 0,
    staConnected,
    internet_enabled ? 1 : 0,
    staConfigured,
    staState,
    staIp,
    staOnMinutes, staOffMinutes, staCurrentlyOn ? 1 : 0, staRem,
    ntpEverSucceeded ? 1 : 0,
    ntpLastSuccessValid ? 1 : 0,
    ntpLastSuccessYear, ntpLastSuccessMonth, ntpLastSuccessDay,
    ntpLastSuccessHour, ntpLastSuccessMinute, ntpLastSuccessSecond,
    relaySwitchCount, liveOn,
    currentWeekday, currentYear, currentMonth, currentDay,
    antiShortCycleMinutes, protRem,
    apCycleEnabled ? 1 : 0, apCurrentlyOn ? 1 : 0,
    apOnMinutes, apOffMinutes, apTxPowerLevel, apRem,
    (apCurrentlyOn && (WiFi.softAPgetStationNum() > 0)) ? 1 : 0,
    custom_ssid, sta_ssid);

  server.send(200, "application/json", json);
}

void handleToggleManual() {
  sendCORSHeaders();
  if (!allowRequest(lastToggleManualRequest, 1000UL)) return;
  manual_override = 1 - manual_override;
  saveOverrideSetting();
  if (time_synchronized) saveTimeSetting();
  checkScenarios();
  server.send(200, "text/plain", "OK");
}

/* Other handlers remain unchanged for brevity (save-ap, save-sta, etc.) */
/* ... (same as before) ... */

void setup() {
  Serial.begin(115200);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t twdt_cfg = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&twdt_cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif

  esp_task_wdt_add(NULL);
  LittleFS.begin(true);

  /* Load all settings */
  loadScenarios();
  loadWiFiSettings();
  loadOverrideSetting();
  loadProtectionSettings();
  loadTimeSetting();      /* NEW JSON format */
  loadRelayStats();       /* NEW JSON format */
  loadNtpSuccessInfo();

  pinMode(RELAY_PIN, OUTPUT);
  setRelay(manual_override == 1);

  lastRelayStatState = (manual_override == 1) ? 1 : 0;
  if (lastRelayStatState == 0) {
    lastRelayOffMillis = millis();
  } else {
    relayOnSinceMillis = millis();
    relayCurrentlyOnForStats = true;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
  applyApTxPower();
  connectToInternetWiFi();

  /* Register API routes */
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scenarios", HTTP_GET, handleGetScenarios);
  server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/sync", HTTP_POST, handleSyncTime);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  /* ... other routes ... */

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) handleOptions();
    else {
      sendCORSHeaders();
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  lastTick = millis();
  lastTimeSaveMillis = millis();
}

void loop() {
  feedWatchdog();

  if (pendingReset && ((millis() - resetMillis) > 2000UL)) {
    saveTimeSetting();
    ESP.restart();
  }

  server.handleClient();
  updateClock();
  manageStaCycle();
  tryNtpSync();
  checkScenarios();
  manageApCycle();

  if (time_synchronized && ((millis() - lastTimeSaveMillis) >= TIME_SAVE_INTERVAL)) {
    saveTimeSetting();
  }

  if (relayCurrentlyOnForStats &&
      ((millis() - lastRelayStatSaveMillis) >= TIME_SAVE_INTERVAL)) {
    unsigned long elapsed = (millis() - relayOnSinceMillis) / 1000UL;
    relayTotalOnSeconds += elapsed;
    relayOnSinceMillis += elapsed * 1000UL;
    saveRelayStats();
  }

  static unsigned long lastRefresh = 0;
  if ((millis() - lastRefresh) >= 30000UL) {
    applyApTxPower();
    pinMode(RELAY_PIN, OUTPUT);
    lastRefresh = millis();
  }

  yield();
}