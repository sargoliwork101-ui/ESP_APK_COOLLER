#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

// ===================== تنظیمات و پین‌های سخت‌افزاری =====================
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

struct Scenario {
  bool active = false, enabled = false;
  int startHour = 0, startMinute = 0, endHour = 0, endMinute = 0;
  uint8_t weekdays = 0x7F;
};
Scenario scenarios[MAX_SCENARIOS];

int currentHour = 0, currentMinute = 0, currentSecond = 0;
int currentYear = 2026, currentMonth = 1, currentDay = 1, currentWeekday = 3;
unsigned long lastTick = 0;
int manual_override = 0;
bool time_synchronized = false, ntp_synced_this_boot = false, ntpEverSucceeded = false, ntpLastSuccessValid = false;
int ntpLastSuccessYear = 2026, ntpLastSuccessMonth = 1, ntpLastSuccessDay = 1, ntpLastSuccessHour = 0, ntpLastSuccessMinute = 0, ntpLastSuccessSecond = 0;
unsigned long lastNtpCheckMillis = 0;
bool ntpFirstCheckPending = true, staEverConnectedThisBoot = false, pendingReset = false;
unsigned long resetMillis = 0;

unsigned long lastToggleManualRequest = 0, lastSaveScenarioRequest = 0, lastSyncRequest = 0;
unsigned long lastSaveApRequest = 0, lastSaveStaRequest = 0, lastSaveProtectionRequest = 0;
unsigned long lastSaveApCycleRequest = 0, lastStatusRequest = 0;

const char* TIME_FILES[2] = {"/time0.txt", "/time1.txt"};
int timeFileSlot = 0;
unsigned long timeSaveSeq = 0, lastTimeSaveMillis = 0;

unsigned long relaySwitchCount = 0, relayTotalOnSeconds = 0, relayOnSinceMillis = 0;
bool relayCurrentlyOnForStats = false;
int lastRelayStatState = -1;
const char* RELAY_STAT_FILES[2] = {"/relaystat0.txt", "/relaystat1.txt"};
const char* NTP_META_FILE = "/ntp.json";
int relayStatFileSlot = 0;
unsigned long relayStatSaveSeq = 0, lastRelayStatSaveMillis = 0;

void feedWatchdog() { esp_task_wdt_reset(); }

void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}
void handleOptions() { sendCORSHeaders(); server.send(204); }

bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs) {
  unsigned long now = millis();
  if (lastRequest != 0 && (unsigned long)(now - lastRequest) < minIntervalMs) {
    sendCORSHeaders(); server.send(429, "text/plain", "Too Many Requests"); return false;
  }
  lastRequest = now; return true;
}

void setRelay(bool state) {
  digitalWrite(RELAY_PIN, state ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

void applyApTxPower() {
  wifi_power_t p = (apTxPowerLevel == 0) ? WIFI_POWER_5dBm : (apTxPowerLevel == 1) ? WIFI_POWER_11dBm : (apTxPowerLevel == 2) ? WIFI_POWER_15dBm : WIFI_POWER_19_5dBm;
  WiFi.setTxPower(p);
}

void setApRadioState(bool on) {
  if (on == apCurrentlyOn) return;
  if (on) { WiFi.softAP(custom_ssid, custom_password, 1, 0, 3); applyApTxPower(); }
  else { WiFi.softAPdisconnect(true); }
  apCurrentlyOn = on;
}

void manageApCycle() {
  if (!apCycleEnabled) { if (!apCurrentlyOn) { setApRadioState(true); apCycleLastToggleMillis = millis(); } return; }
  if (apCurrentlyOn && WiFi.softAPgetStationNum() > 0) { apCycleLastToggleMillis = millis(); return; }
  unsigned long elapsed = millis() - apCycleLastToggleMillis;
  if (apCurrentlyOn && elapsed >= (unsigned long)apOnMinutes * 60000UL) { setApRadioState(false); apCycleLastToggleMillis = millis(); }
  else if (!apCurrentlyOn && elapsed >= (unsigned long)apOffMinutes * 60000UL) { setApRadioState(true); apCycleLastToggleMillis = millis(); }
}

void connectToInternetWiFi() {
  if (!internet_enabled || strlen(sta_ssid) == 0) { WiFi.setAutoReconnect(false); WiFi.disconnect(false); return; }
  WiFi.setAutoReconnect(true);
  if (strlen(sta_password) > 0) WiFi.begin(sta_ssid, sta_password); else WiFi.begin(sta_ssid);
  configTime(NTP_GMT_OFFSET_SEC, 0, "ir.pool.ntp.org", "ntp.nic.ir", "pool.ntp.org");
  applyApTxPower();
}

void setStaConnectionState(bool on) {
  if (on) { staCurrentlyOn = true; staCycleLastToggleMillis = millis(); connectToInternetWiFi(); }
  else { WiFi.setAutoReconnect(false); WiFi.disconnect(false); staCurrentlyOn = false; staCycleLastToggleMillis = millis(); staEverConnectedThisBoot = false; }
}

void manageStaCycle() {
  if (!internet_enabled || strlen(sta_ssid) == 0) {
    if (staCurrentlyOn || WiFi.status() == WL_CONNECTED) { WiFi.setAutoReconnect(false); WiFi.disconnect(false); staCurrentlyOn = false; }
    staCycleLastToggleMillis = millis(); return;
  }
  if (staOffMinutes == 0) { if (!staCurrentlyOn) setStaConnectionState(true); return; }
  unsigned long elapsed = millis() - staCycleLastToggleMillis;
  if (staCurrentlyOn && elapsed >= (unsigned long)staOnMinutes * 60000UL) setStaConnectionState(false);
  else if (!staCurrentlyOn && elapsed >= (unsigned long)staOffMinutes * 60000UL) setStaConnectionState(true);
}

void tryNtpSync() {
  if (!internet_enabled || !staCurrentlyOn || strlen(sta_ssid) == 0) return;
  unsigned long interval = ntp_synced_this_boot ? NTP_RECHECK_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;
  if (!ntpFirstCheckPending && (millis() - lastNtpCheckMillis < interval)) return;
  if (WiFi.status() != WL_CONNECTED) return;
  lastNtpCheckMillis = millis(); ntpFirstCheckPending = false;
  time_t now = time(nullptr); struct tm *tmInfo = localtime(&now);
  if (tmInfo && (tmInfo->tm_year + 1900) >= 2024) {
    currentHour = tmInfo->tm_hour; currentMinute = tmInfo->tm_min; currentSecond = tmInfo->tm_sec;
    currentYear = tmInfo->tm_year + 1900; currentMonth = tmInfo->tm_mon + 1; currentDay = tmInfo->tm_mday; currentWeekday = (tmInfo->tm_wday + 1) % 7;
    lastTick = millis(); time_synchronized = true; ntp_synced_this_boot = true; ntpEverSucceeded = true;
    ntpLastSuccessYear = currentYear; ntpLastSuccessMonth = currentMonth; ntpLastSuccessDay = currentDay;
    ntpLastSuccessHour = currentHour; ntpLastSuccessMinute = currentMinute; ntpLastSuccessSecond = currentSecond; ntpLastSuccessValid = true;
    saveTimeSetting(); saveNtpSuccessInfo();
  }
}

// ===================== خواندن و نوشتن سبک فایل‌های تنظیمات =====================
void loadScenarios() {
  if (!LittleFS.exists("/scenarios.json")) return;
  File f = LittleFS.open("/scenarios.json", "r"); if (!f) return;
  DynamicJsonDocument doc(4096); DeserializationError err = deserializeJson(doc, f); f.close(); if (err) return;
  JsonArray array = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int i = 0;
  for (JsonObject v : array) {
    if (i >= MAX_SCENARIOS) break;
    scenarios[i].active = v["active"] | false;
    scenarios[i].enabled = v.containsKey("en") ? (bool)v["en"] : scenarios[i].active;
    scenarios[i].startHour = v["sh"] | 0; scenarios[i].startMinute = v["sm"] | 0;
    scenarios[i].endHour = v["eh"] | 0; scenarios[i].endMinute = v["em"] | 0;
    scenarios[i].weekdays = v.containsKey("wd") ? (uint8_t)(v["wd"] | 0x7F) : 0x7F;
    i++;
  }
}

void loadWiFiSettings() {
  if (!LittleFS.exists("/wifi.json")) return;
  File f = LittleFS.open("/wifi.json", "r"); if (!f) return;
  StaticJsonDocument<512> doc; if (!deserializeJson(doc, f)) {
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
  f.close();
}

void saveWiFiSettings() {
  StaticJsonDocument<512> doc;
  doc["ssid"] = custom_ssid; doc["pass"] = custom_password; doc["sta_ssid"] = sta_ssid; doc["sta_pass"] = sta_password;
  doc["internet"] = internet_enabled; doc["staOnMinutes"] = staOnMinutes; doc["staOffMinutes"] = staOffMinutes;
  doc["apCycleEnabled"] = apCycleEnabled; doc["apOnMinutes"] = apOnMinutes; doc["apOffMinutes"] = apOffMinutes; doc["apTxPowerLevel"] = apTxPowerLevel;
  File f = LittleFS.open("/wifi.json", "w"); if (f) { serializeJson(doc, f); f.close(); }
}

void loadOverrideSetting() {
  if (!LittleFS.exists("/override.txt")) { manual_override = 0; return; }
  File f = LittleFS.open("/override.txt", "r"); if (f) { String val = f.readString(); manual_override = (val.startsWith("V") ? val.substring(val.indexOf(':') + 1).toInt() : val.toInt()) == 1 ? 1 : 0; f.close(); }
}
void saveOverrideSetting() { File f = LittleFS.open("/override.txt", "w"); if (f) { f.printf("%d", manual_override); f.close(); } }

void loadProtectionSettings() {
  if (!LittleFS.exists("/protection.json")) return;
  File f = LittleFS.open("/protection.json", "r"); if (f) { StaticJsonDocument<128> doc; if (!deserializeJson(doc, f)) antiShortCycleMinutes = constrain(doc["minOffMinutes"] | 3, 0, 1440); f.close(); }
}
void saveProtectionSettings() { StaticJsonDocument<128> doc; doc["minOffMinutes"] = antiShortCycleMinutes; File f = LittleFS.open("/protection.json", "w"); if (f) { serializeJson(doc, f); f.close(); } }

void saveTimeSetting() {
  timeSaveSeq++; timeFileSlot = 1 - timeFileSlot;
  File f = LittleFS.open(TIME_FILES[timeFileSlot], "w");
  if (f) { f.printf("V2:%d:%d:%d:%d:%lu:%d:%d:%d:%d", currentHour, currentMinute, currentSecond, time_synchronized ? 1 : 0, timeSaveSeq, currentYear, currentMonth, currentDay, currentWeekday); f.close(); }
  lastTimeSaveMillis = millis();
}
bool readTimeFile(const char* path, int &h, int &m, int &s, int &sync, unsigned long &seq, int &y, int &mon, int &d, int &wd) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r"); if (!f) return false; String val = f.readString(); f.close();
  int th = -1, tm = -1, ts = -1, tsync = -1, ty = 2026, tmon = 1, td = 1, twd = 3, fv = 1; unsigned long tseq = 0;
  int parsed = val.startsWith("V") ? sscanf(val.c_str(), "V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d", &fv, &th, &tm, &ts, &tsync, &tseq, &ty, &tmon, &td, &twd) : sscanf(val.c_str(), "%d:%d:%d:%d:%lu:%d:%d:%d:%d", &th, &tm, &ts, &tsync, &tseq, &ty, &tmon, &td, &twd);
  if (parsed < 4 || th < 0 || th > 23 || tm < 0 || tm > 59 || ts < 0 || ts > 59 || (tsync != 0 && tsync != 1)) return false;
  h = th; m = tm; s = ts; sync = tsync; y = ty; mon = tmon; d = td; wd = twd; seq = (parsed >= 9) ? tseq : 0; return true;
}
void loadTimeSetting() {
  int h0, m0, s0, sy0, y0, mon0, d0, wd0; unsigned long sq0; int h1, m1, s1, sy1, y1, mon1, d1, wd1; unsigned long sq1;
  bool ok0 = readTimeFile(TIME_FILES[0], h0, m0, s0, sy0, sq0, y0, mon0, d0, wd0), ok1 = readTimeFile(TIME_FILES[1], h1, m1, s1, sy1, sq1, y1, mon1, d1, wd1);
  if ((ok0 && ok1 && sq1 >= sq0) || (!ok0 && ok1)) { currentHour = h1; currentMinute = m1; currentSecond = s1; currentYear = y1; currentMonth = mon1; currentDay = d1; currentWeekday = wd1; time_synchronized = (sy1 == 1 && sq1 > 0); timeFileSlot = 1; timeSaveSeq = sq1; }
  else if (ok0) { currentHour = h0; currentMinute = m0; currentSecond = s0; currentYear = y0; currentMonth = mon0; currentDay = d0; currentWeekday = wd0; time_synchronized = (sy0 == 1 && sq0 > 0); timeFileSlot = 0; timeSaveSeq = sq0; }
  else { currentHour = 0; currentMinute = 0; currentSecond = 0; time_synchronized = false; }
}

void saveRelayStats() {
  relayStatSaveSeq++; relayStatFileSlot = 1 - relayStatFileSlot;
  File f = LittleFS.open(RELAY_STAT_FILES[relayStatFileSlot], "w"); if (f) { f.printf("V2:%lu:%lu:%lu", relaySwitchCount, relayTotalOnSeconds, relayStatSaveSeq); f.close(); }
  lastRelayStatSaveMillis = millis();
}
bool readRelayStatFile(const char* path, unsigned long &sc, unsigned long &tos, unsigned long &seq) {
  if (!LittleFS.exists(path)) return false; File f = LittleFS.open(path, "r"); if (!f) return false; String val = f.readString(); f.close();
  unsigned long tsc = 0, ttos = 0, tseq = 0; int fv = 1;
  int parsed = val.startsWith("V") ? sscanf(val.c_str(), "V%d:%lu:%lu:%lu", &fv, &tsc, &ttos, &tseq) : sscanf(val.c_str(), "%lu:%lu:%lu", &tsc, &ttos, &tseq);
  if (parsed < 3) return false; sc = tsc; tos = ttos; seq = tseq; return true;
}
void loadRelayStats() {
  unsigned long sc0, tos0, sq0, sc1, tos1, sq1;
  bool ok0 = readRelayStatFile(RELAY_STAT_FILES[0], sc0, tos0, sq0), ok1 = readRelayStatFile(RELAY_STAT_FILES[1], sc1, tos1, sq1);
  if ((ok0 && ok1 && sq1 >= sq0) || (!ok0 && ok1)) { relaySwitchCount = sc1; relayTotalOnSeconds = tos1; relayStatFileSlot = 1; relayStatSaveSeq = sq1; }
  else if (ok0) { relaySwitchCount = sc0; relayTotalOnSeconds = tos0; relayStatFileSlot = 0; relayStatSaveSeq = sq0; }
  else { relaySwitchCount = 0; relayTotalOnSeconds = 0; }
}

void loadNtpSuccessInfo() {
  if (!LittleFS.exists(NTP_META_FILE)) return; File f = LittleFS.open(NTP_META_FILE, "r"); if (!f) return;
  StaticJsonDocument<192> doc; if (!deserializeJson(doc, f)) {
    ntpLastSuccessYear = doc["y"] | 2026; ntpLastSuccessMonth = doc["mon"] | 1; ntpLastSuccessDay = doc["d"] | 1;
    ntpLastSuccessHour = doc["h"] | 0; ntpLastSuccessMinute = doc["m"] | 0; ntpLastSuccessSecond = doc["s"] | 0;
    ntpLastSuccessValid = true; ntpEverSucceeded = true;
  } f.close();
}
void saveNtpSuccessInfo() {
  if (!ntpLastSuccessValid) return; StaticJsonDocument<192> doc;
  doc["y"] = ntpLastSuccessYear; doc["mon"] = ntpLastSuccessMonth; doc["d"] = ntpLastSuccessDay; doc["h"] = ntpLastSuccessHour; doc["m"] = ntpLastSuccessMinute; doc["s"] = ntpLastSuccessSecond;
  File f = LittleFS.open(NTP_META_FILE, "w"); if (f) { serializeJson(doc, f); f.close(); }
}

// ===================== هسته اجرایی و چک سناریوها روی سخت‌افزار =====================
bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
void advanceDate() {
  const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = dim[currentMonth - 1]; if (currentMonth == 2 && isLeapYear(currentYear)) maxDay = 29;
  if (++currentDay > maxDay) { currentDay = 1; if (++currentMonth > 12) { currentMonth = 1; currentYear++; } }
  currentWeekday = (currentWeekday + 1) % 7;
}
void updateClock() {
  while (millis() - lastTick >= 1000) {
    lastTick += 1000;
    if (++currentSecond >= 60) { currentSecond = 0; if (++currentMinute >= 60) { currentMinute = 0; if (++currentHour >= 24) { currentHour = 0; advanceDate(); } } }
  }
}

void checkScenarios() {
  static int lastKnownState = -1; bool desiredState = false;
  if (manual_override == 1) { desiredState = true; if (lastKnownState != 1) { if (time_synchronized) saveTimeSetting(); lastKnownState = 1; } }
  else if (!time_synchronized) { desiredState = false; lastKnownState = 0; }
  else {
    int curMin = currentHour * 60 + currentMinute;
    for (int i = 0; i < MAX_SCENARIOS; i++) {
      if (!scenarios[i].active || !scenarios[i].enabled) continue;
      int sMin = scenarios[i].startHour * 60 + scenarios[i].startMinute, eMin = scenarios[i].endHour * 60 + scenarios[i].endMinute;
      if (sMin == eMin || scenarios[i].weekdays == 0) continue;
      int day = (sMin > eMin && curMin < eMin) ? (currentWeekday + 6) % 7 : currentWeekday;
      if ((scenarios[i].weekdays & (1 << day)) == 0) continue;
      if ((sMin < eMin && curMin >= sMin && curMin < eMin) || (sMin > eMin && (curMin >= sMin || curMin < eMin))) { desiredState = true; break; }
    }
    if (lastKnownState != (desiredState ? 1 : 0)) { saveTimeSetting(); lastKnownState = desiredState ? 1 : 0; }
  }

  // محافظت ضد روشن/خاموش شدن سریع (Anti-Short-Cycle) روی سخت‌افزار
  if (desiredState && antiShortCycleMinutes > 0 && lastRelayStatState == 0 && (millis() - lastRelayOffMillis < (unsigned long)antiShortCycleMinutes * 60000UL)) desiredState = false;

  int newState = desiredState ? 1 : 0;
  if (lastRelayStatState != newState) {
    if (newState == 1) { relaySwitchCount++; relayOnSinceMillis = millis(); relayCurrentlyOnForStats = true; }
    else { if (relayCurrentlyOnForStats) relayTotalOnSeconds += (millis() - relayOnSinceMillis) / 1000UL; relayCurrentlyOnForStats = false; lastRelayOffMillis = millis(); }
    lastRelayStatState = newState; saveRelayStats();
  }
  setRelay(desiredState);
}

// ===================== وب‌سرور سبک و هندلرهای API =====================
void handleRoot() { sendCORSHeaders(); server.send(200, "application/json", "{\"status\":\"online\",\"device\":\"ESP32 Cooler\"}"); }
void handleGetScenarios() {
  sendCORSHeaders(); if (!allowRequest(lastStatusRequest, 100UL)) return;
  if (LittleFS.exists("/scenarios.json")) { File f = LittleFS.open("/scenarios.json", "r"); String out = f.readString(); f.close(); server.send(200, "application/json", out.length() ? out : "[]"); }
  else server.send(200, "application/json", "[]");
}
void handleSaveScenario() {
  sendCORSHeaders(); if (!allowRequest(lastSaveScenarioRequest, 1000UL) || !server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request"); return; }
  File f = LittleFS.open("/scenarios.json", "w"); if (f) { f.print(server.arg("plain")); f.close(); loadScenarios(); server.send(200, "text/plain", "OK"); } else server.send(500, "text/plain", "FS Error");
}
void handleSyncTime() {
  sendCORSHeaders(); if (!allowRequest(lastSyncRequest, 1000UL)) return;
  if (server.hasArg("h") && server.hasArg("y")) {
    currentHour = server.arg("h").toInt(); currentMinute = server.arg("m").toInt(); currentSecond = server.arg("s").toInt();
    currentYear = server.arg("y").toInt(); currentMonth = server.arg("mon").toInt(); currentDay = server.arg("d").toInt(); currentWeekday = server.arg("wd").toInt();
    lastTick = millis(); time_synchronized = true; saveTimeSetting(); server.send(200, "text/plain", "OK");
  } else server.send(400, "text/plain", "Bad Request");
}
void handleGetStatus() {
  sendCORSHeaders(); if (!allowRequest(lastStatusRequest, 150UL)) return;
  char json[1024], timeStr[9], staIp[16] = ""; sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);
  int staConnected = (WiFi.status() == WL_CONNECTED) ? 1 : 0, staConfigured = (strlen(sta_ssid) > 0) ? 1 : 0;
  if (staConnected) { IPAddress ip = WiFi.localIP(); snprintf(staIp, sizeof(staIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]); staEverConnectedThisBoot = true; }
  int staState = (!internet_enabled || !staConfigured) ? 0 : (staOffMinutes > 0 && !staCurrentlyOn) ? 4 : staConnected ? 2 : staEverConnectedThisBoot ? 3 : 1;
  unsigned long liveOn = relayTotalOnSeconds + (relayCurrentlyOnForStats ? (millis() - relayOnSinceMillis) / 1000UL : 0);
  long protRem = (antiShortCycleMinutes > 0 && lastRelayStatState == 0) ? max(0L, (long)(((unsigned long)antiShortCycleMinutes * 60000UL - (millis() - lastRelayOffMillis) + 999UL) / 1000UL)) : 0;
  long apRem = apCycleEnabled ? max(0L, (long)((((apCurrentlyOn ? apOnMinutes : apOffMinutes) * 60000UL) - (millis() - apCycleLastToggleMillis) + 999UL) / 1000UL)) : 0;
  long staRem = (internet_enabled && staConfigured && staOffMinutes > 0) ? max(0L, (long)((((staCurrentlyOn ? staOnMinutes : staOffMinutes) * 60000UL) - (millis() - staCycleLastToggleMillis) + 999UL) / 1000UL)) : 0;
  snprintf(json, sizeof(json), "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"sta\":%d,\"internetEnabled\":%d,\"staConfigured\":%d,\"staState\":%d,\"staIp\":\"%s\",\"staOnMinutes\":%d,\"staOffMinutes\":%d,\"staPhaseOn\":%d,\"staRemaining\":%ld,\"ntpOk\":%d,\"ntpLastValid\":%d,\"ntpYear\":%d,\"ntpMonth\":%d,\"ntpDay\":%d,\"ntpHour\":%d,\"ntpMinute\":%d,\"ntpSecond\":%d,\"switchCount\":%lu,\"onSeconds\":%lu,\"weekday\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"protectionMinutes\":%d,\"protectionRemaining\":%ld,\"apCycleEnabled\":%d,\"apOn\":%d,\"apOnMinutes\":%d,\"apOffMinutes\":%d,\"apTxPowerLevel\":%d,\"apRemaining\":%ld,\"apClientConnected\":%d,\"ssid\":\"%s\",\"staSsid\":\"%s\"}",
    timeStr, digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL ? 1 : 0, manual_override, time_synchronized ? 1 : 0, staConnected, internet_enabled ? 1 : 0, staConfigured, staState, staIp,
    staOnMinutes, staOffMinutes, staCurrentlyOn ? 1 : 0, staRem, ntpEverSucceeded ? 1 : 0, ntpLastSuccessValid ? 1 : 0, ntpLastSuccessYear, ntpLastSuccessMonth, ntpLastSuccessDay, ntpLastSuccessHour, ntpLastSuccessMinute, ntpLastSuccessSecond, relaySwitchCount, liveOn, currentWeekday, currentYear, currentMonth, currentDay, antiShortCycleMinutes, protRem,
    apCycleEnabled ? 1 : 0, apCurrentlyOn ? 1 : 0, apOnMinutes, apOffMinutes, apTxPowerLevel, apRem, (apCurrentlyOn && WiFi.softAPgetStationNum() > 0) ? 1 : 0, custom_ssid, sta_ssid);
  server.send(200, "application/json", json);
}
void handleToggleManual() { sendCORSHeaders(); if (!allowRequest(lastToggleManualRequest, 1000UL)) return; manual_override = 1 - manual_override; saveOverrideSetting(); if (time_synchronized) saveTimeSetting(); checkScenarios(); server.send(200, "text/plain", "OK"); }
void handleSaveAP() {
  sendCORSHeaders(); if (!allowRequest(lastSaveApRequest, 3000UL) || !server.hasArg("ssid") || !server.hasArg("pass")) { server.send(400, "text/plain", "Bad Request"); return; }
  server.arg("ssid").toCharArray(custom_ssid, 32); server.arg("pass").toCharArray(custom_password, 32); saveWiFiSettings(); server.send(200, "text/plain", "OK"); pendingReset = true; resetMillis = millis();
}
void handleSaveSTA() {
  sendCORSHeaders(); if (!allowRequest(lastSaveStaRequest, 1500UL)) return;
  if (server.hasArg("sta_ssid")) server.arg("sta_ssid").toCharArray(sta_ssid, 32); if (server.hasArg("sta_pass")) server.arg("sta_pass").toCharArray(sta_password, 64);
  internet_enabled = (server.arg("internet") == "1"); staOnMinutes = constrain(server.arg("sta_on_minutes").toInt(), 1, 1440); staOffMinutes = constrain(server.arg("sta_off_minutes").toInt(), 0, 1440);
  saveWiFiSettings(); ntp_synced_this_boot = false; ntpFirstCheckPending = true; staEverConnectedThisBoot = false; setStaConnectionState(true); server.send(200, "text/plain", "OK");
}
void handleSaveProtection() { sendCORSHeaders(); if (!allowRequest(lastSaveProtectionRequest, 1000UL) || !server.hasArg("min_off")) return; antiShortCycleMinutes = constrain(server.arg("min_off").toInt(), 0, 1440); saveProtectionSettings(); server.send(200, "text/plain", "OK"); }
void handleSaveApCycle() {
  sendCORSHeaders(); if (!allowRequest(lastSaveApCycleRequest, 1000UL)) return;
  apCycleEnabled = (server.arg("cycle_enabled") == "1"); apOnMinutes = constrain(server.arg("on_minutes").toInt(), 1, 1440); apOffMinutes = constrain(server.arg("off_minutes").toInt(), 1, 1440); apTxPowerLevel = constrain(server.arg("tx_power").toInt(), 0, 3);
  applyApTxPower(); apCycleLastToggleMillis = millis(); if (!apCurrentlyOn) setApRadioState(true); saveWiFiSettings(); server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t twdt_cfg = { .timeout_ms = WDT_TIMEOUT_SEC * 1000, .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, .trigger_panic = true }; esp_task_wdt_init(&twdt_cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL); LittleFS.begin(true);
  loadScenarios(); loadWiFiSettings(); loadOverrideSetting(); loadProtectionSettings(); loadTimeSetting(); loadRelayStats(); loadNtpSuccessInfo();
  pinMode(RELAY_PIN, OUTPUT); setRelay(manual_override == 1);
  lastRelayStatState = (manual_override == 1) ? 1 : 0; if (lastRelayStatState == 0) lastRelayOffMillis = millis(); else { relayOnSinceMillis = millis(); relayCurrentlyOnForStats = true; }
  WiFi.mode(WIFI_AP_STA); WiFi.persistent(false); WiFi.setAutoReconnect(true); WiFi.softAP(custom_ssid, custom_password, 1, 0, 3); applyApTxPower();
  connectToInternetWiFi();
  server.on("/", HTTP_GET, handleRoot); server.on("/scenarios", HTTP_GET, handleGetScenarios); server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/sync", HTTP_POST, handleSyncTime); server.on("/status", HTTP_GET, handleGetStatus); server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  server.on("/save-ap", HTTP_POST, handleSaveAP); server.on("/save-sta", HTTP_POST, handleSaveSTA); server.on("/save-protection", HTTP_POST, handleSaveProtection); server.on("/save-ap-cycle", HTTP_POST, handleSaveApCycle);
  server.onNotFound([]() { if (server.method() == HTTP_OPTIONS) handleOptions(); else { sendCORSHeaders(); server.send(404, "text/plain", "Not Found"); } });
  server.begin(); lastTick = millis(); lastTimeSaveMillis = millis();
}

void loop() {
  feedWatchdog();
  if (pendingReset && (millis() - resetMillis > 2000)) { saveTimeSetting(); ESP.restart(); }
  server.handleClient(); updateClock(); manageStaCycle(); tryNtpSync(); checkScenarios(); manageApCycle();
  if (time_synchronized && (millis() - lastTimeSaveMillis >= TIME_SAVE_INTERVAL)) saveTimeSetting();
  if (relayCurrentlyOnForStats && (millis() - lastRelayStatSaveMillis >= TIME_SAVE_INTERVAL)) { unsigned long elapsed = (millis() - relayOnSinceMillis) / 1000UL; relayTotalOnSeconds += elapsed; relayOnSinceMillis += elapsed * 1000UL; saveRelayStats(); }
  static unsigned long lastRefresh = 0; if (millis() - lastRefresh >= 30000UL) { applyApTxPower(); pinMode(RELAY_PIN, OUTPUT); lastRefresh = millis(); }
  yield();
}
