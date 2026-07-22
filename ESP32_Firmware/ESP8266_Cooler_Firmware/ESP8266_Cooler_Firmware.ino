#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

/* ========================================================
   ESP8266-01 COOLER FIRMWARE (Standalone Version)
   - Fully independent from ESP32 version
   - Same encryption (XOR + Base64) with 32-byte key
   - Same API endpoints for compatibility with the web app
   - Works completely offline (99% of the time)
   - JSON storage with encryption
   ======================================================== */

/* ===================== HARDWARE SETTINGS ===================== */
const int RELAY_PIN = 0;                    // GPIO0 on ESP8266-01
const int RELAY_ACTIVE_LEVEL = LOW;         // Most relays on ESP-01 are active LOW

/* ===================== ENCRYPTION (Same as ESP32) ===================== */
const uint8_t ENCRYPTION_KEY[32] = {
  0x7B, 0xE4, 0x2A, 0x91, 0xC5, 0x6F, 0xD8, 0x33,
  0x4A, 0xB7, 0x9E, 0x12, 0xF0, 0x5D, 0x88, 0xC3,
  0x1E, 0xA9, 0x74, 0x2B, 0xF6, 0x8C, 0x3D, 0x50,
  0xE1, 0x9B, 0x27, 0x6A, 0xD4, 0x3F, 0x85, 0x1C
};

void xorEncrypt(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    data[i] ^= ENCRYPTION_KEY[i % 32];
  }
}

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

bool base64Decode(const String& input, uint8_t* output, size_t& outLen) {
  size_t len = input.length();
  if (len % 4 != 0) return false;
  outLen = (len / 4) * 3;
  if (input[len-1] == '=') outLen--;
  if (input[len-2] == '=') outLen--;
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

bool loadEncryptedFile(const char* path, String& jsonContent) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String encoded = f.readString();
  f.close();
  size_t decodedLen = (encoded.length() / 4) * 3 + 4;
  uint8_t* buffer = (uint8_t*)malloc(decodedLen);
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

/* ===================== NETWORK ENCRYPTION ===================== */
String encryptForNetwork(const String& json) {
  size_t len = json.length();
  uint8_t* buffer = (uint8_t*)malloc(len);
  if (!buffer) return "";
  memcpy(buffer, json.c_str(), len);
  xorEncrypt(buffer, len);
  String result = base64Encode(buffer, len);
  free(buffer);
  return result;
}

String decryptFromNetwork(const String& encrypted) {
  size_t decodedLen = (encrypted.length() / 4) * 3 + 4;
  uint8_t* buffer = (uint8_t*)malloc(decodedLen);
  if (!buffer) return "";
  size_t actualLen = 0;
  if (!base64Decode(encrypted, buffer, actualLen)) {
    free(buffer);
    return "";
  }
  xorEncrypt(buffer, actualLen);
  String result = String((char*)buffer, actualLen);
  free(buffer);
  return result;
}

/* ===================== CONFIG ===================== */
char custom_ssid[32] = "ESP8266_Cooler";
char custom_password[32] = "12345678";
char sta_ssid[32] = "";
char sta_password[64] = "";

bool internet_enabled = true;
int staOnMinutes = 10, staOffMinutes = 0;
bool staCurrentlyOn = true;
unsigned long staCycleLastToggleMillis = 0;

const int MAX_SCENARIOS = 20;
int antiShortCycleMinutes = 3;
unsigned long lastRelayOffMillis = 0;

ESP8266WebServer server(80);

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

/* ===================== GLOBAL STATE ===================== */
int currentHour = 0, currentMinute = 0, currentSecond = 0;
int currentYear = 2026, currentMonth = 1, currentDay = 1, currentWeekday = 3;
unsigned long lastTick = 0;
int manual_override = 0;
bool time_synchronized = false;

unsigned long lastToggleManualRequest = 0;
unsigned long lastSaveScenarioRequest = 0;
unsigned long lastSyncRequest = 0;
unsigned long lastStatusRequest = 0;

unsigned long relaySwitchCount = 0, relayTotalOnSeconds = 0, relayOnSinceMillis = 0;
bool relayCurrentlyOnForStats = false;
int lastRelayStatState = -1;

/* ===================== FILE PATHS ===================== */
const char* TIME_FILE = "/time.json";
const char* RELAY_STAT_FILE = "/relaystat.json";
const char* NTP_META_FILE = "/ntp.json";

/* ===================== CORE FUNCTIONS ===================== */
void setRelay(bool state) {
  digitalWrite(RELAY_PIN, state ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

void loadScenarios() {
  String json;
  if (!loadEncryptedFile("/scenarios.json", json)) return;
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, json)) return;
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

void saveTimeSetting() {
  StaticJsonDocument<256> doc;
  doc["hour"] = currentHour;
  doc["minute"] = currentMinute;
  doc["second"] = currentSecond;
  doc["sync"] = time_synchronized ? 1 : 0;
  doc["year"] = currentYear;
  doc["month"] = currentMonth;
  doc["day"] = currentDay;
  doc["weekday"] = currentWeekday;
  String json;
  serializeJson(doc, json);
  saveEncryptedFile(TIME_FILE, json);
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
    currentYear = doc["year"] | 2026;
    currentMonth = doc["month"] | 1;
    currentDay = doc["day"] | 1;
    currentWeekday = doc["weekday"] | 3;
  }
}

void saveRelayStats() {
  StaticJsonDocument<128> doc;
  doc["switchCount"] = relaySwitchCount;
  doc["onSeconds"] = relayTotalOnSeconds;
  String json;
  serializeJson(doc, json);
  saveEncryptedFile(RELAY_STAT_FILE, json);
}

void loadRelayStats() {
  String json;
  if (!loadEncryptedFile(RELAY_STAT_FILE, json)) return;
  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, json)) {
    relaySwitchCount = doc["switchCount"] | 0;
    relayTotalOnSeconds = doc["onSeconds"] | 0;
  }
}

void checkScenarios() {
  static int lastKnownState = -1;
  bool desiredState = false;

  if (manual_override == 1) {
    desiredState = true;
  } else if (!time_synchronized) {
    desiredState = false;
  } else {
    int curMin = currentHour * 60 + currentMinute;
    for (int i = 0; i < MAX_SCENARIOS; i++) {
      if (!scenarios[i].active || !scenarios[i].enabled) continue;
      int sMin = scenarios[i].startHour * 60 + scenarios[i].startMinute;
      int eMin = scenarios[i].endHour * 60 + scenarios[i].endMinute;
      if (sMin == eMin || scenarios[i].weekdays == 0) continue;
      int day = (sMin > eMin && curMin < eMin) ? ((currentWeekday + 6) % 7) : currentWeekday;
      if ((scenarios[i].weekdays & (1 << day)) == 0) continue;
      if ((sMin < eMin && curMin >= sMin && curMin < eMin) ||
          (sMin > eMin && (curMin >= sMin || curMin < eMin))) {
        desiredState = true;
        break;
      }
    }
  }

  if (desiredState && antiShortCycleMinutes > 0 && lastRelayStatState == 0 &&
      (millis() - lastRelayOffMillis < (unsigned long)antiShortCycleMinutes * 60000UL)) {
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
void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  sendCORSHeaders();
  server.send(204);
}

void handleRoot() {
  sendCORSHeaders();
  server.send(200, "application/json", "{\"status\":\"online\",\"device\":\"ESP8266 Cooler\"}");
}

void handleGetScenarios() {
  sendCORSHeaders();
  String json;
  if (loadEncryptedFile("/scenarios.json", json)) {
    server.send(200, "application/json", encryptForNetwork(json));
  } else {
    server.send(200, "application/json", encryptForNetwork("[]"));
  }
}

void handleSaveScenario() {
  sendCORSHeaders();
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  String decrypted = decryptFromNetwork(server.arg("plain"));
  if (decrypted.length() > 0) {
    saveEncryptedFile("/scenarios.json", decrypted);
    loadScenarios();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Decryption Failed");
  }
}

void handleGetStatus() {
  sendCORSHeaders();
  char json[512];
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);

  int relayState = (digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL) ? 1 : 0;
  unsigned long liveOn = relayTotalOnSeconds + (relayCurrentlyOnForStats ? (millis() - relayOnSinceMillis) / 1000UL : 0);

  snprintf(json, sizeof(json),
    "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"switchCount\":%lu,\"onSeconds\":%lu,"
    "\"year\":%d,\"month\":%d,\"day\":%d,\"weekday\":%d}",
    timeStr, relayState, manual_override, time_synchronized ? 1 : 0,
    relaySwitchCount, liveOn, currentYear, currentMonth, currentDay, currentWeekday);

  server.send(200, "application/json", encryptForNetwork(json));
}

void handleToggleManual() {
  sendCORSHeaders();
  manual_override = 1 - manual_override;
  setRelay(manual_override == 1);
  server.send(200, "text/plain", encryptForNetwork("OK"));
}

void handleSyncTime() {
  sendCORSHeaders();
  String decrypted = decryptFromNetwork(server.arg("plain"));
  if (decrypted.length() == 0) {
    server.send(400, "text/plain", encryptForNetwork("Decryption Failed"));
    return;
  }
  // Simple parsing for demo (can be improved)
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
    server.send(200, "text/plain", encryptForNetwork("OK"));
  } else {
    server.send(400, "text/plain", encryptForNetwork("Bad Request"));
  }
}

/* ===================== SETUP & LOOP ===================== */
void setup() {
  Serial.begin(115200);
  LittleFS.begin();

  loadScenarios();
  loadTimeSetting();
  loadRelayStats();

  pinMode(RELAY_PIN, OUTPUT);
  setRelay(manual_override == 1);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(custom_ssid, custom_password);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scenarios", HTTP_GET, handleGetScenarios);
  server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  server.on("/sync", HTTP_POST, handleSyncTime);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) handleOptions();
    else server.send(404, "text/plain", "Not Found");
  });

  server.begin();
  lastTick = millis();
  Serial.println("ESP8266 Cooler Firmware Started");
}

void loop() {
  server.handleClient();

  // Simple clock
  if (millis() - lastTick >= 1000) {
    lastTick += 1000;
    currentSecond++;
    if (currentSecond >= 60) {
      currentSecond = 0;
      currentMinute++;
      if (currentMinute >= 60) {
        currentMinute = 0;
        currentHour++;
        if (currentHour >= 24) {
          currentHour = 0;
          currentDay++;
          // Simplified date handling
        }
      }
    }
  }

  checkScenarios();
  yield();
}