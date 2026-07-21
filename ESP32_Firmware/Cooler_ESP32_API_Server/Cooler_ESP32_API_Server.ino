#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include "mbedtls/aes.h" // کتابخانه بومی و شتاب‌دهی شده سخت‌افزاری ESP32 برای رمزنگاری متقارن AES

// ============================================================
//  بخش تنظیمات و ثابت‌ها
//  تمام مقادیر قابل تغییر در اینجا جمع شده‌اند.
//  فقط همین مقادیر را تغییر دهید؛ نیازی به دست زدن به بقیه کد نیست.
// ============================================================

// نام برنامه — در تب مرورگر و نوار بالای پنل نمایش داده می‌شود.
const char* PROGRAM_NAME    = "کولر هوشمند ESP32";

// زیرنویس/برند کوچک زیر نام برنامه در نوار بالا (مثلاً مدل برد).
const char* PROGRAM_TAGLINE = "ESP32 · TIMER HUB";

// پایه (پین) خروجی متصل به رله روی برد ESP32-WROOM.
// GPIO23 یک پایه امن برای خروجی است. اگر رله روی پایه دیگری وصل است، فقط این عدد را تغییر دهید.
// نکته: GPIO3 (پایه RX0 سریال) برای رله پیشنهاد نمی‌شود.
const int  RELAY_PIN = 23;

// سطح منطقی که رله را روشن می‌کند:
// رله با فرمان HIGH روشن می‌شود → HIGH  /  با فرمان LOW روشن می‌شود (Active-Low) → LOW
const int  RELAY_ACTIVE_LEVEL = HIGH;

// اختلاف زمان محلی با UTC بر حسب ثانیه. مقدار فعلی UTC+03:30 (ایران) است.
// برای مناطق دیگر فقط این عدد را تغییر دهید (مثلاً UTC+03:30 = 12600).
const long NTP_GMT_OFFSET_SEC = 12600;

// کلید ۱۶ بایتی (۱۲۸ بیتی) برای رمزنگاری و رمزگشایی متقارن رمزهای عبور
// برای تغییر و شخصی‌سازی امنیت سیستم خود، می‌توانید مقادیر این آرایه هگز را به مقادیر تصادفی دیگر تغییر دهید.
const unsigned char AES_KEY[16] = {
  0x4F, 0xA1, 0xC3, 0x92, 0x0E, 0x77, 0xB5, 0x2D,
  0x8C, 0x1A, 0x6F, 0xE4, 0x33, 0xD9, 0x5B, 0x88
};

// نام و رمز شبکه Access Point خود دستگاه (برای اتصال مستقیم گوشی به برد).
// رمز AP طبق قوانین ESP32 باید حداقل ۸ کاراکتر باشد.
char custom_ssid[32]     = "ESP32_Timer_Hub";
char custom_password[32] = "12345678";

// تنظیمات اتصال به مودم/روتر اینترنت (اختیاری).
// اگر خالی بماند، برد فقط Access Point خودش را نگه می‌دارد.
// رمز مودم اختیاری است ولی اگر وارد شود بهتر است حداقل ۸ کاراکتر باشد.
char sta_ssid[32]     = "";
char sta_password[64] = "";

// وضعیت فعال بودن استفاده از اینترنت/NTP روی اتصال STA.
// وقتی false باشد، برد اصلاً به مودم وصل نمی‌شود و از اینترنت/NTP استفاده نمی‌کند.
bool internet_enabled = true;

// چرخه‌ی زمانی اتصال مودم اینترنت (STA).
// اگر مدت خاموش بودن ۰ باشد یعنی اتصال STA همیشه روشن می‌ماند و هیچ چرخه‌ای اعمال نمی‌شود.
int  staOnMinutes  = 10;
int  staOffMinutes = 0;
const int MIN_STA_ON_MINUTES = 1;
const int MIN_STA_OFF_MINUTES = 0;
const int MAX_STA_CYCLE_MINUTES = 1440;
bool staCurrentlyOn = true;
unsigned long staCycleLastToggleMillis = 0;

// ============ چرخه‌ی روشن/خاموش‌سازی دوره‌ای AP (فرستنده وای‌فای خود برد) ============
// هدف: هم پایین آمدن دمای برد (چون رادیوی وای‌فای یکی از منابع اصلی گرمای برد است) و هم کاهش
// تابش دائمی امواج نزدیک انسان. وقتی فعال باشد، AP به‌صورت دوره‌ای روشن و خاموش می‌شود.
bool apCycleEnabled = false;          // پیش‌فرض خاموش تا رفتار فعلی کاربرهای قبلی تغییر نکند
int  apOnMinutes  = 10;               // مدت روشن بودن AP در هر دوره (دقیقه)
int  apOffMinutes = 5;                // مدت خاموش بودن AP در هر دوره (دقیقه)
const int MIN_AP_CYCLE_MINUTES = 1;
const int MAX_AP_CYCLE_MINUTES = 1440;
bool apCurrentlyOn = true;            // وضعیت واقعی فعلی AP (برای مدیریت چرخه)
unsigned long apCycleLastToggleMillis = 0;

// سطح قدرت سیگنال‌دهی AP/رادیوی وای‌فای برد (روی کل رادیو اعمال می‌شود، چون ESP32 یک تنظیم توان واحد دارد):
// 0=کم، 1=متوسط، 2=زیاد، 3=حداکثر (مقدار پیش‌فرض سازنده و رفتار قبلی برنامه)
int apTxPowerLevel = 3;
const int MAX_AP_TX_POWER_LEVEL = 3;

// حداکثر تعداد سناریوهای قابل تعریف در پنل.
const int MAX_SCENARIOS = 20;

// حداقل زمان خاموش‌بودن کمپرسور پیش از روشن‌شدن مجدد (دقیقه). صفر = غیرفعال.
int antiShortCycleMinutes = 3;
const int MAX_ANTI_SHORT_CYCLE_MINUTES = 1440;
unsigned long lastRelayOffMillis = 0;

// ظرفیت بافر JSON سناریوها (با حاشیه اطمینان محاسبه شده تا هرگز خطای حافظه ندهد).
const size_t SCENARIOS_JSON_CAPACITY = 6144;
// ============================================================================
// STORAGE / MIGRATION VERSION BANNER — CURRENT REVISION: 6
// ============================================================================
const int STORAGE_FORMAT_REVISION = 6;
const int SCENARIOS_FILE_VERSION = 2;
const int WIFI_FILE_VERSION = 5;
const int TIME_FILE_VERSION = 2;
const int RELAY_STAT_FILE_VERSION = 2;
const int OVERRIDE_FILE_VERSION = 2;
const int PROTECTION_FILE_VERSION = 1;
const int NTP_META_FILE_VERSION = 1;

// زمان‌سنج (Watchdog) اختصاصی ESP32 به ثانیه — در صورت هنگ کردن برد، خودش ری‌استارت می‌شود.
const int WDT_TIMEOUT_SEC = 8;

// فاصله ذخیره پشتیبان دوره‌ای ساعت روی حافظه (برای مقاومت در برابر قطع برق).
const unsigned long TIME_SAVE_INTERVAL = 5UL * 60UL * 1000UL;

// فاصله تلاش برای گرفتن ساعت از اینترنت (NTP) تا وقتی که در همین روشن بودن دستگاه هنوز موفق نشده.
const unsigned long NTP_RETRY_INTERVAL_MS = 1UL * 60UL * 1000UL;

// فاصله بررسی مجدد ساعت از اینترنت بعد از اینکه یک‌بار در همین روشن بودن دستگاه با موفقیت سنکرون شد.
const unsigned long NTP_RECHECK_INTERVAL_MS = 1UL * 60UL * 60UL * 1000UL;

WebServer server(80);

void feedWatchdog();

// ساختار هر سناریو
struct Scenario {
  bool active = false;   // وجود داشتن سناریو در لیست
  bool enabled = false;  // فعال/غیرفعال بودن اجرای سناریو
  int startHour = 0;
  int startMinute = 0;
  int endHour = 0;
  int endMinute = 0;
  // بیت ۰=شنبه ... بیت ۶=جمعه؛ 127 یعنی هر روز هفته
  uint8_t weekdays = 0x7F;
};

Scenario scenarios[MAX_SCENARIOS];

// متغیرهای زمان داخلی برد
int currentHour = 0;
int currentMinute = 0;
int currentSecond = 0;
int currentYear = 2026, currentMonth = 1, currentDay = 1;
int currentWeekday = 3;
unsigned long lastTick = 0;

// وضعیت کنترل دستی رله (0 = خودکار/بر اساس سناریو، 1 = روشن دستی)
int manual_override = 0; 

// پرچم همگام‌سازی ساعت با گوشی
bool time_synchronized = false;

// وضعیت واقعی دریافت ساعت از اینترنت (NTP) در همین نوبت روشن بودن دستگاه
bool ntp_synced_this_boot = false;

// آیا تا به حال حداقل یک‌بار ساعت با موفقیت از اینترنت گرفته شده (برای نمایش در پنل)
bool ntpEverSucceeded = false;
bool ntpLastSuccessValid = false;
int ntpLastSuccessYear = 2026, ntpLastSuccessMonth = 1, ntpLastSuccessDay = 1;
int ntpLastSuccessHour = 0, ntpLastSuccessMinute = 0, ntpLastSuccessSecond = 0;
const char* NTP_META_FILE = "/ntp.json";

// زمان‌بندی داخلی تلاش NTP
unsigned long lastNtpCheckMillis = 0;
bool ntpFirstCheckPending = true;

// وضعیت دقیق اتصال STA (مودم اینترنت)
bool staEverConnectedThisBoot = false;

// متغیرهای مدیریت ریستارت غیر بلاک کننده
unsigned long resetMillis = 0;
bool pendingReset = false;

// Rate limiting endpointهای وب
unsigned long lastToggleManualRequest = 0;
unsigned long lastSaveScenarioRequest = 0;
unsigned long lastSyncRequest = 0;
unsigned long lastSaveApRequest = 0;
unsigned long lastSaveStaRequest = 0;
unsigned long lastSaveProtectionRequest = 0;
unsigned long lastSaveApCycleRequest = 0;
unsigned long lastStatusRequest = 0;

// مدیریت ذخیره امن زمان (Round-Robin)
const char* TIME_FILES[2] = {"/time0.txt", "/time1.txt"};
int timeFileSlot = 0;
unsigned long timeSaveSeq = 0;
unsigned long lastTimeSaveMillis = 0;

// آمار سوییچ و مدت‌کارکرد رله (Round-Robin)
unsigned long relaySwitchCount = 0;
unsigned long relayTotalOnSeconds = 0;
unsigned long relayOnSinceMillis = 0;
bool relayCurrentlyOnForStats = false;
int lastRelayStatState = -1;
const char* RELAY_STAT_FILES[2] = {"/relaystat0.txt", "/relaystat1.txt"};
int relayStatFileSlot = 0;
unsigned long relayStatSaveSeq = 0;
unsigned long lastRelayStatSaveMillis = 0;

// تعریف توابع سیستم
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
void sendCORSHeaders();
void handleOptions();
void handleRoot();
void handleGetScenarios();
void handleSaveScenario();
void handleSyncTime();
void handleGetStatus();
void handleToggleManual();
void handleSaveAP();
void handleSaveSTA();
void handleSaveProtection();
void handleSaveApCycle();
bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs);
String htmlAttrEscape(const String &raw);
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

// بررسی rate limiting با پاسخ 429
bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs) {
  unsigned long now = millis();
  if (lastRequest != 0 && (unsigned long)(now - lastRequest) < minIntervalMs) {
    sendCORSHeaders();
    server.send(429, "text/plain", "Too Many Requests");
    return false;
  }
  lastRequest = now;
  return true;
}

// ================== Watchdog مخصوص ESP32 ==================
void setupWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&twdt_config);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL);
}

void feedWatchdog() {
  esp_task_wdt_reset();
}

String htmlAttrEscape(const String &raw) {
  String out;
  out.reserve(raw.length() + 8);
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '\'': out += "&#39;";  break;
      case '"':  out += "&quot;"; break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      default:   out += c;        break;
    }
  }
  return out;
}

void setRelay(bool state) {
  if (state) {
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LEVEL);
  } else {
    digitalWrite(RELAY_PIN, !RELAY_ACTIVE_LEVEL);
  }
}

// ============================================================
//  چرخه‌ی روشن/خاموش دوره‌ای AP + کنترل قدرت سیگنال رادیو وای‌فای
// ============================================================
void applyApTxPower() {
  wifi_power_t p;
  switch (apTxPowerLevel) {
    case 0:  p = WIFI_POWER_5dBm;   break;
    case 1:  p = WIFI_POWER_11dBm;  break;
    case 2:  p = WIFI_POWER_15dBm;  break;
    default: p = WIFI_POWER_19_5dBm; break;
  }
  WiFi.setTxPower(p);
  Serial.print("TX Power set to level "); Serial.print(apTxPowerLevel);
  Serial.print(" -> requested="); Serial.print((int)p);
  Serial.print(" actual="); Serial.println((int)WiFi.getTxPower());
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

  if (apCurrentlyOn && WiFi.softAPgetStationNum() > 0) {
    apCycleLastToggleMillis = millis();
    return;
  }

  unsigned long onMs  = (unsigned long)apOnMinutes  * 60000UL;
  unsigned long offMs = (unsigned long)apOffMinutes * 60000UL;
  unsigned long elapsed = millis() - apCycleLastToggleMillis;

  if (apCurrentlyOn && elapsed >= onMs) {
    setApRadioState(false);
    apCycleLastToggleMillis = millis();
  } else if (!apCurrentlyOn && elapsed >= offMs) {
    setApRadioState(true);
    apCycleLastToggleMillis = millis();
  }
}

// ============================================================
//  توابع رمزنگاری سخت‌افزاری (AES) مختص ESP32
// ============================================================
String encryptPassword(const char* password, size_t bufferSize) {
  if (strlen(password) == 0) return "";

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);

  size_t paddedSize = (bufferSize + 15) / 16 * 16;
  unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
  if (!input) return "";
  strncpy((char*)input, password, bufferSize - 1);

  unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
  if (!output) {
    free(input);
    return "";
  }

  for (size_t i = 0; i < paddedSize; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
  }
  mbedtls_aes_free(&aes);

  String hexString = "ENC:";
  for (size_t i = 0; i < paddedSize; i++) {
    char hex[3];
    sprintf(hex, "%02x", output[i]);
    hexString += String(hex);
  }
  
  free(input);
  free(output);
  return hexString;
}

void loadAndDecryptPassword(const char* savedValue, char* outputBuffer, size_t bufferSize) {
  String val = String(savedValue);
  
  if (val.length() == 0) {
    outputBuffer[0] = '\0';
    return;
  }

  if (val.startsWith("ENC:")) {
    String hexString = val.substring(4);
    size_t len = hexString.length();
    if (len % 32 != 0) return; 

    size_t paddedSize = len / 2;
    unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
    if (!input) return;

    for (size_t i = 0; i < paddedSize; i++) {
      char hex[3] = {hexString[i * 2], hexString[i * 2 + 1], '\0'};
      input[i] = (unsigned char)strtol(hex, NULL, 16);
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);

    unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
    if (!output) {
      free(input);
      return;
    }

    for (size_t i = 0; i < paddedSize; i += 16) {
      mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
    }
    mbedtls_aes_free(&aes);

    size_t copySize = paddedSize < bufferSize ? paddedSize : bufferSize - 1;
    memcpy(outputBuffer, output, copySize);
    outputBuffer[copySize] = '\0';
    
    free(input);
    free(output);
  } else {
    strncpy(outputBuffer, savedValue, bufferSize - 1);
    outputBuffer[bufferSize - 1] = '\0';
  }
}

// ============================================================
//  هدرهای CORS و پشتیبانی کامل از Local WebView / Capacitor App
// ============================================================
void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}

void handleOptions() {
  sendCORSHeaders();
  server.send(204);
}

// ============================================================
//  Setup و Loop
// ============================================================
void setup() {
  Serial.begin(115200);
  setupWatchdog(); 

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed even after formatting!");
  }

  loadScenarios();
  loadWiFiSettings(); 
  loadOverrideSetting(); 
  loadProtectionSettings(); 
  loadTimeSetting();     
  loadRelayStats();      
  loadNtpSuccessInfo();  

  pinMode(RELAY_PIN, OUTPUT);

  if (manual_override == 1) {
    setRelay(true);
  } else {
    setRelay(false);
  }

  lastRelayStatState = (manual_override == 1) ? 1 : 0;
  if (lastRelayStatState == 0) lastRelayOffMillis = millis();
  if (lastRelayStatState == 1) {
    relayOnSinceMillis = millis();
    relayCurrentlyOnForStats = true;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);              
  WiFi.setAutoReconnect(true);         
  WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
  applyApTxPower();      
  apCurrentlyOn = true;
  apCycleLastToggleMillis = millis();
  Serial.println("Access Point Started");

  staCurrentlyOn = true;
  staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  // تعریف کنترل‌کننده‌های وب‌سرور (سبک‌شده و بهینه‌شده برای API موبایل)
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/sync", HTTP_POST, handleSyncTime);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/scenarios", HTTP_GET, handleGetScenarios); // API دریافت لیست سناریوها برای اپ موبایل
  server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  server.on("/save-ap", HTTP_POST, handleSaveAP);
  server.on("/save-sta", HTTP_POST, handleSaveSTA);
  server.on("/save-protection", HTTP_POST, handleSaveProtection);
  server.on("/save-ap-cycle", HTTP_POST, handleSaveApCycle);

  // هندلر OPTIONS برای تمام مسیرها جهت جلوگیری از خطای CORS در اپلیکیشن Local WebView / Capacitor
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/save", HTTP_OPTIONS, handleOptions);
  server.on("/sync", HTTP_OPTIONS, handleOptions);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/scenarios", HTTP_OPTIONS, handleOptions);
  server.on("/toggle-manual", HTTP_OPTIONS, handleOptions);
  server.on("/save-ap", HTTP_OPTIONS, handleOptions);
  server.on("/save-sta", HTTP_OPTIONS, handleOptions);
  server.on("/save-protection", HTTP_OPTIONS, handleOptions);
  server.on("/save-ap-cycle", HTTP_OPTIONS, handleOptions);

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
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
  
  static int lowHeapStreak = 0;
  if (ESP.getFreeHeap() < 2000) {
    lowHeapStreak++;
    if (lowHeapStreak >= 5) {
      Serial.println("Memory too low! Restarting safely to prevent freeze.");
      saveTimeSetting();
      ESP.restart();
    }
  } else {
    lowHeapStreak = 0;
  }

  if (pendingReset && (millis() - resetMillis > 2000)) {
    saveTimeSetting(); 
    ESP.restart();
  }

  server.handleClient();
  updateClock();
  manageStaCycle();  
  tryNtpSync();      
  checkScenarios();
  manageApCycle();   

  if (time_synchronized && (millis() - lastTimeSaveMillis >= TIME_SAVE_INTERVAL)) {
    saveTimeSetting();
  }

  if (relayCurrentlyOnForStats && (millis() - lastRelayStatSaveMillis >= TIME_SAVE_INTERVAL)) {
    unsigned long elapsedSec = (millis() - relayOnSinceMillis) / 1000UL;
    relayTotalOnSeconds += elapsedSec;
    relayOnSinceMillis += elapsedSec * 1000UL;
    saveRelayStats();
  }

  static unsigned long lastTxPowerRefresh = 0;
  if (millis() - lastTxPowerRefresh >= 30000UL) {
    applyApTxPower();
    lastTxPowerRefresh = millis();
  }

  static unsigned long lastPinModeRefresh = 0;
  if (millis() - lastPinModeRefresh >= 30000UL) {
    pinMode(RELAY_PIN, OUTPUT);
    lastPinModeRefresh = millis();
  }
  
  yield(); 
}

void updateClock() {
  while (millis() - lastTick >= 1000) {
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
          advanceDate();
        }
      }
    }
  }
}

bool isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

void advanceDate() {
  const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = daysInMonth[currentMonth - 1];
  if (currentMonth == 2 && isLeapYear(currentYear)) maxDay = 29;
  currentDay++;
  if (currentDay > maxDay) { currentDay = 1; currentMonth++; if (currentMonth > 12) { currentMonth = 1; currentYear++; } }
  currentWeekday = (currentWeekday + 1) % 7;
}

void checkScenarios() {
  static int lastKnownState = -1; 
  bool desiredState;

  if (manual_override == 1) {
    desiredState = true;
    if (lastKnownState != 1) {
      if (time_synchronized) saveTimeSetting();
      lastKnownState = 1;
    }
  }
  else if (!time_synchronized) {
    desiredState = false;
    if (lastKnownState != 0) {
      lastKnownState = 0;
    }
  }
  else {
    desiredState = false;
    int currentMinutesSinceMidnight = currentHour * 60 + currentMinute;

    for (int i = 0; i < MAX_SCENARIOS; i++) {
      if (!scenarios[i].active || !scenarios[i].enabled) continue;

      int startMinutes = scenarios[i].startHour * 60 + scenarios[i].startMinute;
      int endMinutes = scenarios[i].endHour * 60 + scenarios[i].endMinute;
      uint8_t selectedDays = scenarios[i].weekdays;

      if (startMinutes == endMinutes || selectedDays == 0) continue;
      int scenarioDay = currentWeekday;
      if (startMinutes > endMinutes && currentMinutesSinceMidnight < endMinutes) scenarioDay = (currentWeekday + 6) % 7;
      if ((selectedDays & (1 << scenarioDay)) == 0) continue;

      if (startMinutes < endMinutes) {
        if (currentMinutesSinceMidnight >= startMinutes && currentMinutesSinceMidnight < endMinutes) {
          desiredState = true;
          break;
        }
      } else {
        if (currentMinutesSinceMidnight >= startMinutes || currentMinutesSinceMidnight < endMinutes) {
          desiredState = true;
          break;
        }
      }
    }

    int newKnownState = desiredState ? 1 : 0;
    if (lastKnownState != newKnownState) {
      saveTimeSetting();
      lastKnownState = newKnownState;
    }
  }

  if (desiredState && antiShortCycleMinutes > 0 && lastRelayStatState == 0) {
    unsigned long requiredMs = (unsigned long)antiShortCycleMinutes * 60UL * 1000UL;
    if (millis() - lastRelayOffMillis < requiredMs) desiredState = false;
  }

  int newRelayStatState = desiredState ? 1 : 0;
  if (lastRelayStatState != newRelayStatState) {
    if (newRelayStatState == 1) {
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
    lastRelayStatState = newRelayStatState;
    saveRelayStats(); 
  }

  setRelay(desiredState);
}

void loadScenarios() {
  if (!LittleFS.exists("/scenarios.json")) return;
  
  File configFile = LittleFS.open("/scenarios.json", "r");
  if (!configFile) return;

  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.print("loadScenarios JSON error: ");
    Serial.println(error.c_str());
    return;
  }

  JsonArray array = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int i = 0;
  for (JsonObject v : array) {
    if (i >= MAX_SCENARIOS) break;
    bool itemActive = v["active"] | false;
    scenarios[i].active = itemActive;
    scenarios[i].enabled = v.containsKey("en") ? (bool)v["en"] : itemActive;
    scenarios[i].startHour = v["sh"] | 0;
    scenarios[i].startMinute = v["sm"] | 0;
    scenarios[i].endHour = v["eh"] | 0;
    scenarios[i].endMinute = v["em"] | 0;
    scenarios[i].weekdays = v.containsKey("wd") ? (uint8_t)(v["wd"] | 0x7F) : 0x7F;
    i++;
  }
}

void saveScenarios() {
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  JsonObject root = doc.to<JsonObject>();
  root["version"] = SCENARIOS_FILE_VERSION;
  JsonArray array = root.createNestedArray("items");
  
  for (int i = 0; i < MAX_SCENARIOS; i++) {
    JsonObject obj = array.createNestedObject();
    obj["active"] = scenarios[i].active;
    obj["en"] = scenarios[i].enabled;
    obj["sh"] = scenarios[i].startHour;
    obj["sm"] = scenarios[i].startMinute;
    obj["eh"] = scenarios[i].endHour;
    obj["em"] = scenarios[i].endMinute;
    obj["wd"] = scenarios[i].weekdays;
  }

  File configFile = LittleFS.open("/scenarios.json", "w");
  if (!configFile) return;
  size_t written = serializeJson(doc, configFile);
  configFile.close();

  if (written == 0) {
    Serial.println("saveScenarios write failed!");
    LittleFS.remove("/scenarios.json");
  }
}

void loadWiFiSettings() {
  if (!LittleFS.exists("/wifi.json")) return;
  
  File configFile = LittleFS.open("/wifi.json", "r");
  if (!configFile) return;

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) return;

  if (doc.containsKey("ssid")) { strncpy(custom_ssid, doc["ssid"], 31); custom_ssid[31] = '\0'; }
  if (doc.containsKey("pass")) {
    loadAndDecryptPassword(doc["pass"], custom_password, 32);
  }
  if (doc.containsKey("sta_ssid")) { strncpy(sta_ssid, doc["sta_ssid"], 31); sta_ssid[31] = '\0'; }
  if (doc.containsKey("sta_pass")) {
    loadAndDecryptPassword(doc["sta_pass"], sta_password, 64);
  }

  if (doc.containsKey("internet")) { internet_enabled = doc["internet"] | true; }

  if (doc.containsKey("staEnabled") && !doc.containsKey("staOnMinutes") && !doc.containsKey("staOffMinutes")) {
    bool legacyStaEnabled = doc["staEnabled"] | true;
    if (!legacyStaEnabled) internet_enabled = false;
  }

  if (doc.containsKey("staOnMinutes")) {
    int v = doc["staOnMinutes"] | staOnMinutes;
    staOnMinutes = constrain(v, MIN_STA_ON_MINUTES, MAX_STA_CYCLE_MINUTES);
  }
  if (doc.containsKey("staOffMinutes")) {
    int v = doc["staOffMinutes"] | staOffMinutes;
    staOffMinutes = constrain(v, MIN_STA_OFF_MINUTES, MAX_STA_CYCLE_MINUTES);
  }

  if (doc.containsKey("apCycleEnabled")) { apCycleEnabled = doc["apCycleEnabled"] | false; }
  if (doc.containsKey("apOnMinutes")) {
    int v = doc["apOnMinutes"] | apOnMinutes;
    apOnMinutes = constrain(v, MIN_AP_CYCLE_MINUTES, MAX_AP_CYCLE_MINUTES);
  }
  if (doc.containsKey("apOffMinutes")) {
    int v = doc["apOffMinutes"] | apOffMinutes;
    apOffMinutes = constrain(v, MIN_AP_CYCLE_MINUTES, MAX_AP_CYCLE_MINUTES);
  }
  if (doc.containsKey("apTxPowerLevel")) {
    int v = doc["apTxPowerLevel"] | apTxPowerLevel;
    apTxPowerLevel = constrain(v, 0, MAX_AP_TX_POWER_LEVEL);
  }
}

void saveWiFiSettings() {
  StaticJsonDocument<1024> doc;
  doc["version"] = WIFI_FILE_VERSION;
  doc["ssid"] = custom_ssid;
  doc["pass"] = encryptPassword(custom_password, 32);
  doc["sta_ssid"] = sta_ssid;
  doc["sta_pass"] = encryptPassword(sta_password, 64);
  doc["internet"] = internet_enabled;
  doc["staOnMinutes"] = staOnMinutes;
  doc["staOffMinutes"] = staOffMinutes;
  doc["apCycleEnabled"] = apCycleEnabled;
  doc["apOnMinutes"] = apOnMinutes;
  doc["apOffMinutes"] = apOffMinutes;
  doc["apTxPowerLevel"] = apTxPowerLevel;

  File configFile = LittleFS.open("/wifi.json", "w");
  if (!configFile) return;
  size_t written = serializeJson(doc, configFile);
  configFile.close();

  if (written == 0) {
    Serial.println("saveWiFiSettings write failed!");
    LittleFS.remove("/wifi.json");
  }
}

void loadOverrideSetting() {
  if (!LittleFS.exists("/override.txt")) {
    manual_override = 0;
    return;
  }
  File f = LittleFS.open("/override.txt", "r");
  if (f) {
    String val = f.readString();
    manual_override = val.startsWith("V") ? val.substring(val.indexOf(':') + 1).toInt() : val.toInt();
    manual_override = (manual_override == 1) ? 1 : 0;
    f.close();
  }
}

void saveOverrideSetting() {
  File f = LittleFS.open("/override.txt", "w");
  if (f) {
    f.print("V"); f.print(OVERRIDE_FILE_VERSION); f.print(":"); f.print(manual_override);
    f.close();
  }
}

void loadProtectionSettings() {
  if (!LittleFS.exists("/protection.json")) return;
  File f = LittleFS.open("/protection.json", "r"); if (!f) return;
  StaticJsonDocument<128> doc; DeserializationError err = deserializeJson(doc, f); f.close();
  if (!err && doc.containsKey("minOffMinutes")) {
    int value = doc["minOffMinutes"] | 3;
    antiShortCycleMinutes = constrain(value, 0, MAX_ANTI_SHORT_CYCLE_MINUTES);
  }
}

void saveProtectionSettings() {
  StaticJsonDocument<128> doc;
  doc["version"] = PROTECTION_FILE_VERSION; doc["minOffMinutes"] = antiShortCycleMinutes;
  File f = LittleFS.open("/protection.json", "w"); if (!f) return;
  size_t written = serializeJson(doc, f); f.close();
  if (written == 0) {
    Serial.println("saveProtectionSettings write failed!");
    LittleFS.remove("/protection.json");
  }
}

void saveTimeSetting() {
  timeSaveSeq++;
  timeFileSlot = 1 - timeFileSlot; 
  File f = LittleFS.open(TIME_FILES[timeFileSlot], "w");
  if (f) {
    size_t written = f.printf("V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d", TIME_FILE_VERSION, currentHour, currentMinute, currentSecond, time_synchronized ? 1 : 0, timeSaveSeq, currentYear, currentMonth, currentDay, currentWeekday);
    f.close();
    if (written == 0) {
      Serial.println("saveTimeSetting write failed!");
      LittleFS.remove(TIME_FILES[timeFileSlot]);
    }
  }
  lastTimeSaveMillis = millis();
}

bool readTimeFile(const char* path, int &h, int &m, int &s, int &sync, unsigned long &seq, int &y, int &mon, int &d, int &wd) {
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  String val = f.readString();
  f.close();

  int th = -1, tm = -1, ts = -1, tsync = -1, ty = 2026, tmon = 1, td = 1, twd = 3, fileVersion = 1;
  unsigned long tseq = 0;
  int parsed;
  if (val.startsWith("V")) {
    parsed = sscanf(val.c_str(), "V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d", &fileVersion, &th, &tm, &ts, &tsync, &tseq, &ty, &tmon, &td, &twd);
  } else {
    parsed = sscanf(val.c_str(), "%d:%d:%d:%d:%lu:%d:%d:%d:%d", &th, &tm, &ts, &tsync, &tseq, &ty, &tmon, &td, &twd);
  }

  if (parsed < 4 || th < 0 || th > 23 || tm < 0 || tm > 59 || ts < 0 || ts > 59 || (tsync != 0 && tsync != 1)) {
    return false;
  }

  if ((fileVersion >= 2 || parsed >= 9) && (ty < 2024 || tmon < 1 || tmon > 12 || td < 1 || td > 31 || twd < 0 || twd > 6)) return false;
  h = th; m = tm; s = ts; sync = tsync; y = ty; mon = tmon; d = td; wd = twd;
  seq = ((fileVersion >= 2 && parsed >= 10) || (fileVersion == 1 && parsed >= 9)) ? tseq : 0; 
  return true;
}

void loadTimeSetting() {
  int h0, m0, s0, sync0, y0, mon0, d0, wd0; unsigned long seq0;
  int h1, m1, s1, sync1, y1, mon1, d1, wd1; unsigned long seq1;

  bool ok0 = readTimeFile(TIME_FILES[0], h0, m0, s0, sync0, seq0, y0, mon0, d0, wd0);
  bool ok1 = readTimeFile(TIME_FILES[1], h1, m1, s1, sync1, seq1, y1, mon1, d1, wd1);

  bool useSlot0 = false, useSlot1 = false;

  if (ok0 && ok1) {
    if (seq1 >= seq0) useSlot1 = true; else useSlot0 = true;
  } else if (ok0) {
    useSlot0 = true;
  } else if (ok1) {
    useSlot1 = true;
  }

  if (useSlot0) {
    currentHour = h0; currentMinute = m0; currentSecond = s0; currentYear = y0; currentMonth = mon0; currentDay = d0; currentWeekday = wd0;
    time_synchronized = (sync0 == 1 && seq0 > 0); 
    timeFileSlot = 0;
    timeSaveSeq = seq0;
  } else if (useSlot1) {
    currentHour = h1; currentMinute = m1; currentSecond = s1; currentYear = y1; currentMonth = mon1; currentDay = d1; currentWeekday = wd1;
    time_synchronized = (sync1 == 1 && seq1 > 0); 
    timeFileSlot = 1;
    timeSaveSeq = seq1;
  } else {
    currentHour = 0;
    currentMinute = 0;
    currentSecond = 0;
    time_synchronized = false;
  }
}

void saveRelayStats() {
  relayStatSaveSeq++;
  relayStatFileSlot = 1 - relayStatFileSlot;

  File f = LittleFS.open(RELAY_STAT_FILES[relayStatFileSlot], "w");
  if (f) {
    size_t written = f.printf("V%d:%lu:%lu:%lu", RELAY_STAT_FILE_VERSION, relaySwitchCount, relayTotalOnSeconds, relayStatSaveSeq);
    f.close();
    if (written == 0) {
      Serial.println("saveRelayStats write failed!");
      LittleFS.remove(RELAY_STAT_FILES[relayStatFileSlot]);
    }
  }
  lastRelayStatSaveMillis = millis();
}

void loadNtpSuccessInfo() {
  ntpLastSuccessValid = false;
  if (!LittleFS.exists(NTP_META_FILE)) return;
  File f = LittleFS.open(NTP_META_FILE, "r");
  if (!f) return;

  StaticJsonDocument<192> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;

  int y   = doc["y"]   | 0;
  int mon = doc["mon"] | 0;
  int d   = doc["d"]   | 0;
  int h   = doc["h"]   | -1;
  int m   = doc["m"]   | -1;
  int s   = doc["s"]   | -1;

  if (y < 2024 || mon < 1 || mon > 12 || d < 1 || d > 31 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return;

  ntpLastSuccessYear = y;
  ntpLastSuccessMonth = mon;
  ntpLastSuccessDay = d;
  ntpLastSuccessHour = h;
  ntpLastSuccessMinute = m;
  ntpLastSuccessSecond = s;
  ntpLastSuccessValid = true;
  ntpEverSucceeded = true;
}

void saveNtpSuccessInfo() {
  if (!ntpLastSuccessValid) return;

  StaticJsonDocument<192> doc;
  doc["version"] = NTP_META_FILE_VERSION;
  doc["y"] = ntpLastSuccessYear;
  doc["mon"] = ntpLastSuccessMonth;
  doc["d"] = ntpLastSuccessDay;
  doc["h"] = ntpLastSuccessHour;
  doc["m"] = ntpLastSuccessMinute;
  doc["s"] = ntpLastSuccessSecond;

  File f = LittleFS.open(NTP_META_FILE, "w");
  if (!f) return;
  size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) {
    Serial.println("saveNtpSuccessInfo write failed!");
    LittleFS.remove(NTP_META_FILE);
  }
}

bool readRelayStatFile(const char* path, unsigned long &sc, unsigned long &tos, unsigned long &seq) {
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  String val = f.readString();
  f.close();

  unsigned long tsc = 0, ttos = 0, tseq = 0;
  int fileVersion = 1;
  int parsed = val.startsWith("V") ? sscanf(val.c_str(), "V%d:%lu:%lu:%lu", &fileVersion, &tsc, &ttos, &tseq) : sscanf(val.c_str(), "%lu:%lu:%lu", &tsc, &ttos, &tseq);
  if (parsed != (val.startsWith("V") ? 4 : 3)) return false; 

  sc = tsc; tos = ttos; seq = tseq;
  return true;
}

void loadRelayStats() {
  unsigned long sc0, tos0, seq0;
  unsigned long sc1, tos1, seq1;

  bool ok0 = readRelayStatFile(RELAY_STAT_FILES[0], sc0, tos0, seq0);
  bool ok1 = readRelayStatFile(RELAY_STAT_FILES[1], sc1, tos1, seq1);

  bool useSlot0 = false, useSlot1 = false;

  if (ok0 && ok1) {
    if (seq1 >= seq0) useSlot1 = true; else useSlot0 = true;
  } else if (ok0) {
    useSlot0 = true;
  } else if (ok1) {
    useSlot1 = true;
  }

  if (useSlot0) {
    relaySwitchCount = sc0; relayTotalOnSeconds = tos0;
    relayStatFileSlot = 0; relayStatSaveSeq = seq0;
  } else if (useSlot1) {
    relaySwitchCount = sc1; relayTotalOnSeconds = tos1;
    relayStatFileSlot = 1; relayStatSaveSeq = seq1;
  } else {
    relaySwitchCount = 0;
    relayTotalOnSeconds = 0;
  }
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
  if (!internet_enabled || strlen(sta_ssid) == 0) {
    if (staCurrentlyOn || WiFi.status() == WL_CONNECTED) {
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

  unsigned long onMs  = (unsigned long)staOnMinutes  * 60000UL;
  unsigned long offMs = (unsigned long)staOffMinutes * 60000UL;
  unsigned long elapsed = millis() - staCycleLastToggleMillis;

  if (staCurrentlyOn && elapsed >= onMs) {
    setStaConnectionState(false);
  } else if (!staCurrentlyOn && elapsed >= offMs) {
    setStaConnectionState(true);
  }
}

void connectToInternetWiFi() {
  if (!internet_enabled) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); 
    return;
  }
  if (strlen(sta_ssid) == 0) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); 
    return;
  }

  WiFi.setAutoReconnect(true);
  Serial.print("Connecting STA to modem WiFi: ");
  Serial.println(sta_ssid);

  if (strlen(sta_password) > 0) {
    WiFi.begin(sta_ssid, sta_password);
  } else {
    WiFi.begin(sta_ssid); 
  }

  configTime(NTP_GMT_OFFSET_SEC, 0, "ir.pool.ntp.org", "ntp.nic.ir", "pool.ntp.org");
  applyApTxPower();
}

void tryNtpSync() {
  unsigned long interval = ntp_synced_this_boot ? NTP_RECHECK_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;

  if (!internet_enabled) return;
  if (!staCurrentlyOn) return;
  if (strlen(sta_ssid) == 0) return;

  if (ntpFirstCheckPending) {
    ntpFirstCheckPending = false;
  } else if (millis() - lastNtpCheckMillis < interval) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  lastNtpCheckMillis = millis();

  time_t now = time(nullptr);
  struct tm *tmInfo = localtime(&now);

  if (tmInfo && (tmInfo->tm_year + 1900) >= 2024) {
    currentHour = tmInfo->tm_hour;
    currentMinute = tmInfo->tm_min;
    currentSecond = tmInfo->tm_sec;
    currentYear = tmInfo->tm_year + 1900;
    currentMonth = tmInfo->tm_mon + 1;
    currentDay = tmInfo->tm_mday;
    currentWeekday = (tmInfo->tm_wday + 1) % 7;
    lastTick = millis();

    if (!ntp_synced_this_boot) {
      Serial.println("Time synchronized by internet NTP.");
    }

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

// ============================================================
//  بخش هندلرهای API (بدون تولید HTML حجیم، مخصوص اپلیکیشن موبایل)
// ============================================================

// هندلر ریشه (/) — به جای ۶۷ کیلوبایت HTML و سنگین کردن رم و فلش، فقط یک وضعیت سبک و سریع بازمی‌گرداند.
void handleRoot() {
  sendCORSHeaders();
  server.send(200, "application/json", "{\"app\":\"Cooler ESP32 Timer Hub\",\"status\":\"online\",\"mode\":\"Dedicated API Server for Local WebView / Capacitor Android App\",\"version\":\"2.0\"}");
}

// هندلر دریافت لیست سناریوها مخصوص اپلیکیشن موبایل
void handleGetScenarios() {
  sendCORSHeaders();
  if (!allowRequest(lastStatusRequest, 100UL)) return;

  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  JsonArray array = doc.to<JsonArray>();
  
  for (int i = 0; i < MAX_SCENARIOS; i++) {
    JsonObject obj = array.createNestedObject();
    obj["active"] = scenarios[i].active;
    obj["en"] = scenarios[i].enabled;
    obj["sh"] = scenarios[i].startHour;
    obj["sm"] = scenarios[i].startMinute;
    obj["eh"] = scenarios[i].endHour;
    obj["em"] = scenarios[i].endMinute;
    obj["wd"] = scenarios[i].weekdays;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSaveScenario() {
  sendCORSHeaders();
  if (!allowRequest(lastSaveScenarioRequest, 1200UL)) return;
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  String body = server.arg("plain");
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  if (!doc.is<JsonArray>()) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  JsonArray array = doc.as<JsonArray>();
  Scenario temp[MAX_SCENARIOS]; 

  int i = 0;
  for (JsonObject v : array) {
    if (i >= MAX_SCENARIOS) break;
    temp[i].active = true;
    temp[i].enabled = v["en"] | true; 
    temp[i].startHour = v["sh"] | 0;
    temp[i].startMinute = v["sm"] | 0;
    temp[i].endHour = v["eh"] | 0;
    temp[i].endMinute = v["em"] | 0;
    temp[i].weekdays = v.containsKey("wd") ? (uint8_t)(v["wd"] | 0x7F) : 0x7F;
    i++;
  }

  bool isChanged = false;
  for (int j = 0; j < MAX_SCENARIOS; j++) {
    if (scenarios[j].active != temp[j].active ||
        scenarios[j].enabled != temp[j].enabled ||
        scenarios[j].startHour != temp[j].startHour ||
        scenarios[j].startMinute != temp[j].startMinute ||
        scenarios[j].endHour != temp[j].endHour ||
        scenarios[j].endMinute != temp[j].endMinute ||
        scenarios[j].weekdays != temp[j].weekdays) {
      isChanged = true;
      break;
    }
  }

  if (isChanged) {
    for (int j = 0; j < MAX_SCENARIOS; j++) {
      scenarios[j] = temp[j];
    }
    saveScenarios();
  }
  
  server.send(200, "text/plain", "Updated");
}

void handleSyncTime() {
  sendCORSHeaders();
  if (!allowRequest(lastSyncRequest, 1000UL)) return;
  if (server.hasArg("h") && server.hasArg("m") && server.hasArg("s") && server.hasArg("y") && server.hasArg("mon") && server.hasArg("d") && server.hasArg("wd")) {
    int h = server.arg("h").toInt();
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    int y = server.arg("y").toInt(), mon = server.arg("mon").toInt(), d = server.arg("d").toInt(), wd = server.arg("wd").toInt();

    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 || y < 2024 || mon < 1 || mon > 12 || d < 1 || d > 31 || wd < 0 || wd > 6) {
      server.send(400, "text/plain", "Invalid Time Range");
      return;
    }

    currentHour = h;
    currentMinute = m;
    currentSecond = s;
    currentYear = y; currentMonth = mon; currentDay = d; currentWeekday = wd;
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
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);

  int logicalState = (digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL) ? 1 : 0;

  int staConnected = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  int staConfigured = (strlen(sta_ssid) > 0) ? 1 : 0;
  char staIp[16] = "";
  if (staConnected) {
    IPAddress ip = WiFi.localIP();
    snprintf(staIp, sizeof(staIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }

  int staState;
  if (!internet_enabled || !staConfigured) {
    staState = 0;
  } else if (staOffMinutes > 0 && !staCurrentlyOn) {
    staState = 4;
  } else if (staConnected) {
    staEverConnectedThisBoot = true;
    staState = 2;
  } else if (staEverConnectedThisBoot) {
    staState = 3;
  } else {
    staState = 1;
  }

  unsigned long liveOnSeconds = relayTotalOnSeconds;
  if (relayCurrentlyOnForStats) {
    liveOnSeconds += (millis() - relayOnSinceMillis) / 1000UL;
  }

  long protectionRemainingSec = 0;
  if (antiShortCycleMinutes > 0 && lastRelayStatState == 0) {
    unsigned long requiredMs = (unsigned long)antiShortCycleMinutes * 60UL * 1000UL;
    unsigned long elapsed = millis() - lastRelayOffMillis;
    if (elapsed < requiredMs) protectionRemainingSec = (long)((requiredMs - elapsed + 999UL) / 1000UL);
  }

  int apStationCount = apCurrentlyOn ? WiFi.softAPgetStationNum() : 0;
  long apRemainingSec = 0;
  if (apCycleEnabled) {
    unsigned long phaseMs = apCurrentlyOn ? ((unsigned long)apOnMinutes * 60000UL) : ((unsigned long)apOffMinutes * 60000UL);
    unsigned long elapsed = millis() - apCycleLastToggleMillis;
    if (elapsed < phaseMs) apRemainingSec = (long)((phaseMs - elapsed + 999UL) / 1000UL);
  }

  long staRemainingSec = 0;
  if (internet_enabled && staConfigured && staOffMinutes > 0) {
    unsigned long phaseMs = staCurrentlyOn ? ((unsigned long)staOnMinutes * 60000UL) : ((unsigned long)staOffMinutes * 60000UL);
    unsigned long elapsed = millis() - staCycleLastToggleMillis;
    if (elapsed < phaseMs) staRemainingSec = (long)((phaseMs - elapsed + 999UL) / 1000UL);
  }

  char json[1350];
  snprintf(json, sizeof(json), "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"sta\":%d,\"internetEnabled\":%d,\"staConfigured\":%d,\"staState\":%d,\"staIp\":\"%s\",\"staOnMinutes\":%d,\"staOffMinutes\":%d,\"staPhaseOn\":%d,\"staRemaining\":%ld,\"ntpOk\":%d,\"ntpLastValid\":%d,\"ntpYear\":%d,\"ntpMonth\":%d,\"ntpDay\":%d,\"ntpHour\":%d,\"ntpMinute\":%d,\"ntpSecond\":%d,\"switchCount\":%lu,\"onSeconds\":%lu,\"weekday\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"protectionMinutes\":%d,\"protectionRemaining\":%ld,\"apCycleEnabled\":%d,\"apOn\":%d,\"apOnMinutes\":%d,\"apOffMinutes\":%d,\"apTxPowerLevel\":%d,\"apRemaining\":%ld,\"apClientConnected\":%d,\"ssid\":\"%s\",\"staSsid\":\"%s\"}",
    timeStr, logicalState, manual_override, time_synchronized ? 1 : 0, staConnected, internet_enabled ? 1 : 0, staConfigured, staState, staIp,
    staOnMinutes, staOffMinutes, staCurrentlyOn ? 1 : 0, staRemainingSec,
    ntpEverSucceeded ? 1 : 0, ntpLastSuccessValid ? 1 : 0, ntpLastSuccessYear, ntpLastSuccessMonth, ntpLastSuccessDay, ntpLastSuccessHour, ntpLastSuccessMinute, ntpLastSuccessSecond, relaySwitchCount, liveOnSeconds, currentWeekday, currentYear, currentMonth, currentDay, antiShortCycleMinutes, protectionRemainingSec,
    apCycleEnabled ? 1 : 0, apCurrentlyOn ? 1 : 0, apOnMinutes, apOffMinutes, apTxPowerLevel, apRemainingSec, (apStationCount > 0) ? 1 : 0, custom_ssid, sta_ssid);
  server.send(200, "application/json", json);
}

void handleToggleManual() {
  sendCORSHeaders();
  if (!allowRequest(lastToggleManualRequest, 1500UL)) return;
  if (manual_override == 0) {
    manual_override = 1; 
    saveOverrideSetting(); 
    if (time_synchronized) saveTimeSetting(); 
    delay(100);            
    checkScenarios();      
  } else {
    manual_override = 0; 
    saveOverrideSetting(); 
    if (time_synchronized) saveTimeSetting(); 
    delay(100);
    checkScenarios();      
  }
  
  server.send(200, "text/plain", "OK");
}

void handleSaveAP() {
  sendCORSHeaders();
  if (!allowRequest(lastSaveApRequest, 3000UL)) return;
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String new_ssid = server.arg("ssid");
    String new_pass = server.arg("pass");

    if (new_ssid.length() > 0 && new_pass.length() >= 8) {
      new_ssid.toCharArray(custom_ssid, 32);
      new_pass.toCharArray(custom_password, 32);
      saveWiFiSettings();
      saveTimeSetting();

      server.send(200, "text/plain", "OK");

      pendingReset = true;
      resetMillis = millis();
      return;
    }
  }
  server.send(400, "text/plain", "Bad Request");
}

void handleSaveSTA() {
  sendCORSHeaders();
  if (!allowRequest(lastSaveStaRequest, 1500UL)) return;
  String new_sta_ssid = server.hasArg("sta_ssid") ? server.arg("sta_ssid") : "";
  String new_sta_pass = server.hasArg("sta_pass") ? server.arg("sta_pass") : "";
  String new_internet = server.hasArg("internet") ? server.arg("internet") : "";
  String new_sta_on = server.hasArg("sta_on_minutes") ? server.arg("sta_on_minutes") : "";
  String new_sta_off = server.hasArg("sta_off_minutes") ? server.arg("sta_off_minutes") : "";

  if (new_sta_pass.length() > 0 && new_sta_pass.length() < 8) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  if (new_sta_on.length() == 0 || new_sta_off.length() == 0) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  for (size_t i = 0; i < new_sta_on.length(); i++) if (!isDigit(new_sta_on[i])) { server.send(400, "text/plain", "Bad Request"); return; }
  for (size_t i = 0; i < new_sta_off.length(); i++) if (!isDigit(new_sta_off[i])) { server.send(400, "text/plain", "Bad Request"); return; }

  int onVal = new_sta_on.toInt();
  int offVal = new_sta_off.toInt();
  if (onVal < MIN_STA_ON_MINUTES || onVal > MAX_STA_CYCLE_MINUTES) { server.send(400, "text/plain", "Bad Request"); return; }
  if (offVal < MIN_STA_OFF_MINUTES || offVal > MAX_STA_CYCLE_MINUTES) { server.send(400, "text/plain", "Bad Request"); return; }

  internet_enabled = (new_internet == "1");
  staOnMinutes = onVal;
  staOffMinutes = offVal;
  new_sta_ssid.toCharArray(sta_ssid, 32);
  new_sta_pass.toCharArray(sta_password, 64);
  saveWiFiSettings();

  ntp_synced_this_boot = false;
  ntpFirstCheckPending = true;
  lastNtpCheckMillis = 0;
  staEverConnectedThisBoot = false;

  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  staCurrentlyOn = true;
  staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  server.send(200, "text/plain", "OK");
}

void handleSaveProtection() {
  sendCORSHeaders();
  if (!allowRequest(lastSaveProtectionRequest, 1000UL)) return;
  if (!server.hasArg("min_off")) { server.send(400, "text/plain", "Bad Request"); return; }
  String raw = server.arg("min_off");
  if (raw.length() == 0) { server.send(400, "text/plain", "Bad Request"); return; }
  for (size_t i = 0; i < raw.length(); i++) if (!isDigit(raw[i])) { server.send(400, "text/plain", "Bad Request"); return; }
  int value = raw.toInt();
  if (value < 0 || value > MAX_ANTI_SHORT_CYCLE_MINUTES) { server.send(400, "text/plain", "Bad Request"); return; }
  antiShortCycleMinutes = value;
  saveProtectionSettings();
  server.send(200, "text/plain", "OK");
}

void handleSaveApCycle() {
  sendCORSHeaders();
  if (!allowRequest(lastSaveApCycleRequest, 1000UL)) return;

  if (!server.hasArg("on_minutes") || !server.hasArg("off_minutes") || !server.hasArg("tx_power")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  String onRaw = server.arg("on_minutes");
  String offRaw = server.arg("off_minutes");
  String powerRaw = server.arg("tx_power");
  String enabledRaw = server.hasArg("cycle_enabled") ? server.arg("cycle_enabled") : "";

  if (onRaw.length() == 0 || offRaw.length() == 0 || powerRaw.length() == 0) {
    server.send(400, "text/plain", "Bad Request"); return;
  }
  for (size_t i = 0; i < onRaw.length(); i++) if (!isDigit(onRaw[i])) { server.send(400, "text/plain", "Bad Request"); return; }
  for (size_t i = 0; i < offRaw.length(); i++) if (!isDigit(offRaw[i])) { server.send(400, "text/plain", "Bad Request"); return; }
  for (size_t i = 0; i < powerRaw.length(); i++) if (!isDigit(powerRaw[i])) { server.send(400, "text/plain", "Bad Request"); return; }

  int onVal = onRaw.toInt();
  int offVal = offRaw.toInt();
  int powerVal = powerRaw.toInt();

  if (onVal < MIN_AP_CYCLE_MINUTES || onVal > MAX_AP_CYCLE_MINUTES) { server.send(400, "text/plain", "Bad Request"); return; }
  if (offVal < MIN_AP_CYCLE_MINUTES || offVal > MAX_AP_CYCLE_MINUTES) { server.send(400, "text/plain", "Bad Request"); return; }
  if (powerVal < 0 || powerVal > MAX_AP_TX_POWER_LEVEL) { server.send(400, "text/plain", "Bad Request"); return; }

  apCycleEnabled = (enabledRaw == "1");
  apOnMinutes = onVal;
  apOffMinutes = offVal;
  apTxPowerLevel = powerVal;
  applyApTxPower();

  apCycleLastToggleMillis = millis();
  if (!apCurrentlyOn) setApRadioState(true);

  saveWiFiSettings();
  server.send(200, "text/plain", "OK");
}
