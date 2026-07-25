#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "mbedtls/aes.h" // کتابخانه بومی و شتاب‌دهی شده سخت‌افزاری ESP32 برای رمزنگاری متقارن AES

// ============================================================
//  بخش تنظیمات و ثابت‌ها
//  تمام مقادیر قابل تغییر در اینجا جمع شده‌اند.
//  فقط همین مقادیر را تغییر دهید؛ نیازی به دست زدن به بقیه کد نیست.
// ============================================================

// نام برنامه — در تب مرورگر و نوار بالای پنل نمایش داده می‌شود.
// اگر خواستید نام دستگاه را عوض کنید، فقط این مقدار را تغییر دهید.
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
// تابش دائمی امواج نزدیک انسان. وقتی فعال باشد، AP به‌صورت دوره‌ای روشن و خاموش می‌شود؛
// در بازه‌ی «خاموش»، طبیعتاً گوشی موقتاً به پنل دسترسی ندارد (چون خودِ شبکه قطع است) — این رفتار
// عمدی و بخشی از هدف این قابلیت است. زمان‌بندی سناریوها و رله در تمام این مدت بدون وقفه ادامه دارد
// چون به AP هیچ وابستگی‌ای ندارد.
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
// دستور مهم برای هر توسعه‌دهنده یا هوش مصنوعی آینده:
// اگر ساختار یا معنای دادهٔ هر فایل ذخیره‌شده را تغییر می‌دهی، شمارهٔ نسخهٔ همان
// فایل را افزایش بده، CURRENT REVISION بالا را هم یک واحد زیاد کن، و مسیر خواندن
// نسخهٔ قبلی (migration) را حفظ کن. هرگز دادهٔ قدیمی کاربر را بی‌هشدار حذف نکن.
// ============================================================================
const int STORAGE_FORMAT_REVISION = 6;
const int SCENARIOS_FILE_VERSION = 2;
const int WIFI_FILE_VERSION = 5; // نسخه 5: حذف کلید جداگانهٔ STA و افزودن زمان‌بندی روشن/خاموش اتصال مودم با staOnMinutes/staOffMinutes
const int TIME_FILE_VERSION = 2;
const int RELAY_STAT_FILE_VERSION = 2;
const int OVERRIDE_FILE_VERSION = 2;
const int PROTECTION_FILE_VERSION = 1;
const int NTP_META_FILE_VERSION = 1;

// زمان‌سنج (Watchdog) اختصاصی ESP32 به ثانیه — در صورت هنگ کردن برد، خودش ری‌استارت می‌شود.
const int WDT_TIMEOUT_SEC = 8;

// فاصله ذخیره پشتیبان دوره‌ای ساعت روی حافظه (برای مقاومت در برابر قطع برق).
// مقدار فعلی هر ۵ دقیقه یک‌بار است.
const unsigned long TIME_SAVE_INTERVAL = 5UL * 60UL * 1000UL;

// فاصله تلاش برای گرفتن ساعت از اینترنت (NTP) تا وقتی که در همین روشن بودن دستگاه هنوز موفق نشده.
// همیشه بلافاصله بعد از هر روشن شدن/ریست برد یک تلاش فوری هم انجام می‌شود (فارغ از این فاصله)،
// و اگر آن تلاش موفق نبود، از همان لحظه هر چند وقت یک‌بار که اینجا مشخص شده دوباره تلاش می‌کند.
// مقدار فعلی هر ۱ دقیقه یک‌بار است. برای تغییر، فقط همین عدد را عوض کنید.
const unsigned long NTP_RETRY_INTERVAL_MS = 1UL * 60UL * 1000UL;

// فاصله بررسی مجدد ساعت از اینترنت بعد از اینکه یک‌بار در همین روشن بودن دستگاه با موفقیت سنکرون شد.
// چون دیگر ساعت درست است، نیازی به چک مکرر نیست؛ این فقط برای اطمینان از عدم انحراف ساعت داخلی برد
// در بلندمدت است. مقدار فعلی هر ۱ ساعت یک‌بار است. برای تغییر، فقط همین عدد را عوض کنید.
const unsigned long NTP_RECHECK_INTERVAL_MS = 1UL * 60UL * 60UL * 1000UL;

WebServer server(80);

void feedWatchdog();

// ساختار هر سناریو
struct Scenario {
  bool active = false;   // وجود داشتن سناریو در لیست؛ حذف کردن این مقدار را false می‌کند
  bool enabled = false;  // فعال/غیرفعال بودن اجرای سناریو؛ با حذف کردن فرق دارد
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
// تاریخ میلادی و روز هفته: ۰=شنبه ... ۶=جمعه
int currentYear = 2026, currentMonth = 1, currentDay = 1;
int currentWeekday = 3;
unsigned long lastTick = 0;

// وضعیت کنترل دستی رله (0 = خودکار/بر اساس سناریو، 1 = روشن دستی)
int manual_override = 0;

// پرچم همگام‌سازی ساعت با گوشی
bool time_synchronized = false;

// --- وضعیت واقعی دریافت ساعت از اینترنت (NTP) در همین نوبت روشن بودن دستگاه ---
// نکته مهم: time_synchronized از روی فایل ذخیره‌شده (حتی سنکرون قدیمی با گوشی) در همان لحظه بوت لود می‌شود
// و همیشه true می‌ماند. اگر از همان متغیر برای فاصله‌ی تلاش مجدد NTP استفاده شود، در صورتی که ساعت قبلاً
// (چه با گوشی و چه از بوت قبلی) سنکرون بوده، برد فکر می‌کند NTP هم قبلاً موفق بوده و به‌جای هر ۱ دقیقه،
// هر ۱ ساعت یک‌بار تلاش می‌کند؛ در نتیجه با اینکه به مودم وصل است، ساعت واقعی اینترنت ممکن است تا یک ساعت
// دریافت نشود و کاربر فکر می‌کند "وصل شد ولی ساعت نگرفت". برای همین یک پرچم جداگانه فقط برای همین بوت داریم.
bool ntp_synced_this_boot = false;

// آیا تا به حال حداقل یک‌بار ساعت با موفقیت از اینترنت گرفته شده (برای نمایش در پنل)
bool ntpEverSucceeded = false;
// آخرین زمانِ مطلقِ دریافت موفق ساعت از اینترنت که باید حتی بعد از ریست هم باقی بماند.
bool ntpLastSuccessValid = false;
int ntpLastSuccessYear = 2026, ntpLastSuccessMonth = 1, ntpLastSuccessDay = 1;
int ntpLastSuccessHour = 0, ntpLastSuccessMinute = 0, ntpLastSuccessSecond = 0;
const char* NTP_META_FILE = "/ntp.json";

// زمان‌بندی داخلی تلاش NTP. این‌ها عمداً global هستند تا هنگام تغییر تنظیمات مودم/اینترنت
// بتوانیم تلاش بعدی را فوراً از نو برنامه‌ریزی کنیم.
unsigned long lastNtpCheckMillis = 0;
bool ntpFirstCheckPending = true;

// --- وضعیت دقیق اتصال STA (مودم اینترنت) برای نمایش سه‌حالته در پنل (نارنجی/سبز/قرمز) ---
// آیا از زمان روشن شدن دستگاه، حداقل یک‌بار به مودم اینترنت وصل شده؛ برای تشخیص «هنوز هیچ‌وقت وصل نشده»
// (حالت در حال اتصال، نارنجی) از «قبلاً وصل بوده و الان قطع شده» (حالت قطعی، قرمز).
bool staEverConnectedThisBoot = false;

// متغیرهای مدیریت ریستارت غیر بلاک کننده
unsigned long resetMillis = 0;
bool pendingReset = false;

// Rate limiting endpointهای وب: جلوگیری از اسپم لمس/درخواست و استهلاک فلش یا رله.
unsigned long lastToggleManualRequest = 0;
unsigned long lastSaveScenarioRequest = 0;
unsigned long lastSyncRequest = 0;
unsigned long lastSaveApRequest = 0;
unsigned long lastSaveStaRequest = 0;
unsigned long lastSaveProtectionRequest = 0;
unsigned long lastSaveApCycleRequest = 0;
unsigned long lastStatusRequest = 0;

// ============ مدیریت ذخیره امن زمان (برای مقاومت در برابر نوسان برق) ============
// به‌جای یک فایل، از دو فایل به صورت چرخشی (Round-Robin) استفاده می‌شود.
// اگر برق درست وسط نوشتن یکی از فایل‌ها قطع شود، فایل دیگر همچنان سالم و قابل استفاده باقی می‌ماند.
const char* TIME_FILES[2] = {"/time0.txt", "/time1.txt"};
int timeFileSlot = 0;                 // فایلی که نوبت نوشتن در آن است
unsigned long timeSaveSeq = 0;        // شماره‌ی افزایشی برای تشخیص جدیدترین رکورد هنگام خواندن
unsigned long lastTimeSaveMillis = 0; // زمان آخرین ذخیره‌سازی دوره‌ای
// =================================================================================

// ============ آمار سوییچ و مدت‌کارکرد رله (برای پایش سلامت/عمر کمپرسور) ============
// همان تکنیک دو-فایلی چرخشی بالا برای مقاومت در برابر قطعی برق اینجا هم استفاده می‌شود.
unsigned long relaySwitchCount = 0;       // تعداد دفعاتی که کمپرسور واقعاً روشن شده (هر بار خاموش→روشن یک واحد)
unsigned long relayTotalOnSeconds = 0;    // مجموع ثانیه‌های روشن بودن که تا کنون به صورت قطعی ذخیره شده
unsigned long relayOnSinceMillis = 0;     // لحظه (millis) شروع دوره‌ی روشن بودن جاری؛ فقط وقتی relayCurrentlyOnForStats=true معتبر است
bool relayCurrentlyOnForStats = false;    // آیا هم‌اکنون یک دوره‌ی «روشن» در حال اندازه‌گیری است
int lastRelayStatState = -1;              // آخرین وضعیت منطقی رله که برای آمار ثبت شده (جدا از منطق اصلی سناریو/دستی)
const char* RELAY_STAT_FILES[2] = {"/relaystat0.txt", "/relaystat1.txt"};
int relayStatFileSlot = 0;
unsigned long relayStatSaveSeq = 0;
unsigned long lastRelayStatSaveMillis = 0; // زمان آخرین ذخیره‌سازی دوره‌ای پیشرفت مدت‌کارکرد جاری
// =================================================================================

// تعریف توابع سیستم
void loadScenarios();
bool saveScenarios();
void loadWiFiSettings();
bool saveWiFiSettings();
void loadOverrideSetting();
void loadProtectionSettings();
bool saveProtectionSettings();
bool saveOverrideSetting();
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
void handleRoot();
void handleGetScenarios();
void handleGetConfig();
void handleOptions();
void sendCorsHeaders();
bool saveNvsString(const char* key, const String &payload);
bool loadNvsString(const char* key, String &payload);
void handleSaveScenario();
void handleSyncTime();
void handleGetStatus();
void handleToggleManual();
void handleSaveAP();
void handleSaveSTA();
void handleSaveProtection();
void handleSaveApCycle();
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

// بررسی با تفاضل unsigned long برای سازگاری با سرریز millis().
// پاسخ 429 به مرورگر برمی‌گردد و هیچ تغییری در رله یا فایل‌ها انجام نمی‌شود.
bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs) {
  unsigned long now = millis();
  if (lastRequest != 0 && (unsigned long)(now - lastRequest) < minIntervalMs) {
    server.send(429, "text/plain", "Too Many Requests");
    return false;
  }
  lastRequest = now;
  return true;
}

// ================== Watchdog مخصوص ESP32 ==================
// در نسخه ESP8266 از ESP.wdtEnable/ESP.wdtFeed استفاده می‌شد؛
// در ESP32 باید از Task Watchdog رسمی خود ESP-IDF استفاده کنیم.

void setupWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  // سازگار با Arduino-ESP32 Core نسخه 3 به بعد
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&twdt_config);
#else
  // سازگار با Arduino-ESP32 Core نسخه 2.x
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL); // اضافه کردن loop task فعلی به Watchdog
}

void feedWatchdog() {
  esp_task_wdt_reset();
}
// ============================================================

// تابع کمکی برای سوئیچ کردن رله بر اساس منطق (Active-Low یا Active-High)
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

// اعمال سطح قدرت سیگنال انتخاب‌شده روی رادیوی وای‌فای برد.
// نکته مهم: در ESP32 این تنظیم روی کل رادیو اعمال می‌شود (هم AP هم STA)، چون فقط یک رادیوی
// فیزیکی مشترک وجود دارد؛ تفکیک جداگانه‌ی توان برای AP و STA در سخت‌افزار ESP32 ممکن نیست.
void applyApTxPower() {
  wifi_power_t p;
  switch (apTxPowerLevel) {
    case 0:  p = WIFI_POWER_5dBm;   break; // کم — کمترین مصرف و گرما، برد کوتاه‌تر
    case 1:  p = WIFI_POWER_11dBm;  break; // متوسط
    case 2:  p = WIFI_POWER_15dBm;  break; // زیاد
    default: p = WIFI_POWER_19_5dBm; break; // حداکثر — همان مقدار پیش‌فرض قبلی برنامه
  }
  WiFi.setTxPower(p);
  // برای تشخیص مشکل احتمالی سخت‌افزار/کتابخانه: اگر واقعاً اعمال شده باشد، مقدار خوانده‌شده باید
  // با p یکسان باشد. اگر متفاوت بود یعنی چیزی (مثلاً یک اتصال STA در حال انجام) آن را نادیده گرفته.
  Serial.print("TX Power set to level "); Serial.print(apTxPowerLevel);
  Serial.print(" -> requested="); Serial.print((int)p);
  Serial.print(" actual="); Serial.println((int)WiFi.getTxPower());
}

// روشن/خاموش کردن واقعی رادیوی AP. خاموش کردن با پارامتر true به softAPdisconnect باعث می‌شود
// AP کاملاً از حالت پخش خارج شود (نه فقط قطع کلاینت‌ها)، و دوباره فراخوانی softAP آن را برمی‌گرداند.
// اتصال STA (اینترنت) و تمام منطق رله/سناریو/ساعت کاملاً مستقل از این وضعیت باقی می‌ماند.
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

// مدیریت چرخه‌ی دوره‌ای AP در loop(). اگر چرخه غیرفعال باشد، فقط مطمئن می‌شویم AP روشن بماند
// (حالت عادی/فعلی برنامه). اگر فعال باشد، بین بازه‌ی روشن و خاموش بر اساس تنظیمات کاربر جابه‌جا می‌شود.
// نکته مهم: تا وقتی حداقل یک گوشی/دستگاه به AP وصل است، تایمر چرخه اصلاً پیش نمی‌رود و AP خاموش
// نمی‌شود؛ فقط بعد از قطع شدن آخرین دستگاه، شمارش مدت «روشن» از نو شروع می‌شود.
void manageApCycle() {
  if (!apCycleEnabled) {
    if (!apCurrentlyOn) {
      setApRadioState(true);
      apCycleLastToggleMillis = millis();
    }
    return;
  }

  // اگر همین الان حداقل یک کلاینت (گوشی/دستگاه) به AP وصل است، چرخه کاملاً متوقف می‌ماند:
  // AP روشن نگه داشته می‌شود و لحظه‌ی شروع شمارش هر بار به «همین الان» به‌روزرسانی می‌شود تا
  // به محض قطع شدن آخرین دستگاه، بازه‌ی «روشن» با شمارش کامل از نو آغاز شود.
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

// این تابع متن ساده رمز را می‌گیرد و به یک رشته رمزگذاری‌شده (تبدیل شده به هگز) برمی‌گرداند.
String encryptPassword(const char* password, size_t bufferSize) {
  if (strlen(password) == 0) return ""; // اگر پسورد خالی بود، چیزی رمزنگاری نمی‌شود

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);

  // محاسبه سایز بافر به مضرب ۱۶ (مورد نیاز الگوریتم AES)
  size_t paddedSize = (bufferSize + 15) / 16 * 16;
  unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
  if (!input) return "";
  strncpy((char*)input, password, bufferSize - 1); // کپی رمز بدون سرریز حافظه

  unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
  if (!output) {
    free(input);
    return "";
  }

  // رمزنگاری بلوک به بلوک (هر بلوک ۱۶ بایت)
  for (size_t i = 0; i < paddedSize; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
  }
  mbedtls_aes_free(&aes);

  // تبدیل بایت‌های رمزگذاری‌شده به رشته قابل ذخیره (HEX)
  // پیشوند ENC: مشخص می‌کند که این رشته رمزنگاری شده است
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

// این تابع مقادیر را از فایل می‌خواند و در صورتی که رمز شده باشد باز می‌کند.
// اگر تنظیمات از قبل (نسخه قدیمی) به صورت متن ساده ذخیره شده باشد، هوشمندانه آن را می‌فهمد.
void loadAndDecryptPassword(const char* savedValue, char* outputBuffer, size_t bufferSize) {
  String val = String(savedValue);

  if (val.length() == 0) {
    outputBuffer[0] = '\0';
    return;
  }

  // بررسی اینکه آیا اطلاعات رمزنگاری شده است؟
  if (val.startsWith("ENC:")) {
    String hexString = val.substring(4);
    size_t len = hexString.length();

    // اعتبارسنجی طول رشته
    if (len % 32 != 0) return;

    size_t paddedSize = len / 2;
    unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
    if (!input) return;

    // تبدیل رشته HEX به بایت برای پردازش رمزگشایی
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

    // رمزگشایی بلوک به بلوک
    for (size_t i = 0; i < paddedSize; i += 16) {
      mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
    }
    mbedtls_aes_free(&aes);

    // کپی داده‌های رمزگشایی شده داخل متغیر هدف در برنامه
    size_t copySize = paddedSize < bufferSize ? paddedSize : bufferSize - 1;
    memcpy(outputBuffer, output, copySize);
    outputBuffer[copySize] = '\0';

    free(input);
    free(output);
  } else {
    // مقدار ساده است (فایل از قبل با فرمت قدیمی روی برد ذخیره شده بوده)
    strncpy(outputBuffer, savedValue, bufferSize - 1);
    outputBuffer[bufferSize - 1] = '\0';
  }
}

void setup() {
  // راه‌اندازی سریال در ESP32؛ پایه رله از RX0 جدا شده تا تداخل سریال ایجاد نشود
  Serial.begin(115200);

  // فعال‌سازی واچ‌داگ مخصوص ESP32 با تایم‌اوت ۸ ثانیه
  setupWatchdog();

  // راه‌اندازی حافظه داخلی سیستم
  // در ESP32 با پارامتر true اگر LittleFS برای اولین بار mount نشود، خودکار فرمت می‌شود
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed even after formatting!");
  }

  // لود کردن تنظیمات ذخیره شده
  loadScenarios();
  loadWiFiSettings(); // در این بخش رمزها خودکار رمزگشایی می‌شوند
  loadOverrideSetting(); // لود کردن وضعیت دستی
  loadProtectionSettings(); // لود کردن محافظت ضد روشن/خاموش شدن سریع
  loadTimeSetting();
  loadRelayStats();      // لود کردن آمار سوییچ/مدت‌کارکرد رله ذخیره شده
  loadNtpSuccessInfo();  // لود کردن آخرین دریافت موفق ساعت از اینترنت

  // تنظیم پین رله
  pinMode(RELAY_PIN, OUTPUT);

  // *** اعمال فوری وضعیت کلید دستی بلافاصله پس از راه‌اندازی ***
  // در صورتی که قبل از قطعی برق رله روی حالت دستی روشن بوده، دوباره بی‌قید و شرط روشن می‌ماند
  if (manual_override == 1) {
    setRelay(true);
  } else {
    setRelay(false);
  }

  // *** هماهنگ کردن آمار رله با وضعیت واقعی سخت‌افزار بلافاصله بعد از بوت ***
  // مهم: اگر رله از قبل (قبل از این ریست/قطعی برق) روشن بوده و همچنان روشن می‌ماند، این ادامه‌ی همان
  // دوره‌ی کارکرد کمپرسور است، نه یک سوییچ جدید؛ پس این را در lastRelayStatState بدون افزایش شمارنده
  // ثبت می‌کنیم تا اولین بررسی در checkScenarios() آن را به اشتباه یک «سوییچ جدید» حساب نکند.
  lastRelayStatState = (manual_override == 1) ? 1 : 0;
  // بعد از بوت، اگر رله خاموش است یک دوره‌ی محافظه‌کارانه شروع می‌شود؛ چون وضعیت واقعی
  // کمپرسور درست پیش از قطع برق قابل اعتماد نیست.
  if (lastRelayStatState == 0) lastRelayOffMillis = millis();
  if (lastRelayStatState == 1) {
    relayOnSinceMillis = millis();
    relayCurrentlyOnForStats = true;
  }

  // راه‌اندازی وای‌فای به صورت همزمان: AP برای اتصال مستقیم گوشی + STA برای اتصال اختیاری به اینترنت
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);              // جلوگیری از نوشتن بی‌مورد تنظیمات وای‌فای روی فلش داخلی ESP32
  WiFi.setAutoReconnect(true);         // در فاز روشنِ STA اگر اتصال به مودم قطع شد، خود برد دوباره تلاش کند
  WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
  applyApTxPower();      // اعمال سطح قدرت سیگنال ذخیره‌شده (یا پیش‌فرض حداکثر)
  apCurrentlyOn = true;
  apCycleLastToggleMillis = millis();
  Serial.println("Access Point Started");

  // آغاز فاز اولیهٔ اتصال STA. اگر اینترنت فعال و تنظیمات مودم موجود باشد، اتصال برقرار می‌شود؛
  // در غیر این صورت STA خاموش می‌ماند تا بعداً از پنل فعال شود.
  staCurrentlyOn = true;
  staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  // تعریف کنترل‌کننده‌های وب‌سرور
  server.enableCORS(true);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scenarios", HTTP_GET, handleGetScenarios);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/sync", HTTP_POST, handleSyncTime);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  server.on("/save-ap", HTTP_POST, handleSaveAP);
  server.on("/save-sta", HTTP_POST, handleSaveSTA);
  server.on("/save-protection", HTTP_POST, handleSaveProtection);
  server.on("/save-ap-cycle", HTTP_POST, handleSaveApCycle);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) handleOptions();
    else { sendCorsHeaders(); server.send(404, "application/json", "{\"error\":\"not_found\"}"); }
  });

  server.begin();
  lastTick = millis();
  lastTimeSaveMillis = millis();
}

void loop() {
  feedWatchdog(); // تغذیه واچ‌داگ مخصوص ESP32

  // واچ‌داگ نرم‌افزاری برای بررسی سلامت رم (برای جلوگیری از هنگ کردن کامل تحت تداخل نویز)
  // یک افت لحظه‌ای و گذرای حافظه (مثلاً وسط پردازش یک درخواست وب) طبیعی است؛
  // فقط وقتی چند بار پیاپی زیر آستانه بمانیم، یعنی واقعاً نشتی/کمبود داریم و باید ری‌استارت کنیم.
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

  // مدیریت ریستارت نرم‌افزاری امن
  if (pendingReset && (millis() - resetMillis > 2000)) {
    saveTimeSetting();
    ESP.restart();
  }

  server.handleClient();
  updateClock();
  manageStaCycle();  // مدیریت دوره‌ای قطع/وصل اتصال مودم اینترنت (STA)
  tryNtpSync();      // گرفتن ساعت از اینترنت در صورت وصل بودن به مودم، بدون نیاز به گوشی
  checkScenarios();
  manageApCycle();   // مدیریت چرخه‌ی دوره‌ای روشن/خاموش AP (برای کاهش دما و تابش دائمی)

  // ذخیره پشتیبان دوره‌ای زمان (هر ۵ دقیقه یک‌بار) تا در صورت قطعی برق ناگهانی،
  // حداکثر چند دقیقه از ساعت عقب بمانیم. این کار جدا از ذخیره فوری قبل از هر فرمان قطع/وصل رله است.
  if (time_synchronized && (millis() - lastTimeSaveMillis >= TIME_SAVE_INTERVAL)) {
    saveTimeSetting();
  }

  // ذخیره پیشرفت دوره‌ی جاری «روشن بودن» رله (هر همان فاصله TIME_SAVE_INTERVAL) تا اگر کمپرسور مدت
  // طولانی روشن بماند و درست وسط آن برق قطع شود، حداکثر همین چند دقیقه از مدت‌کارکرد از دست برود.
  // نقطه‌ی شروع دوره جلو کشیده می‌شود تا هنگام محاسبه بعدی، مدت قبلاً ذخیره‌شده دوباره شمارش نشود.
  if (relayCurrentlyOnForStats && (millis() - lastRelayStatSaveMillis >= TIME_SAVE_INTERVAL)) {
    unsigned long elapsedSec = (millis() - relayOnSinceMillis) / 1000UL;
    relayTotalOnSeconds += elapsedSec;
    relayOnSinceMillis += elapsedSec * 1000UL;
    saveRelayStats();
  }

  // --- محافظت اضافه در برابر بازنشانی خاموش/بی‌صدای قدرت سیگنال توسط استک وای‌فای ---
  // دقیقاً مشابه تکنیک بازنشانی pinMode رله در ادامه: هر ۳۰ ثانیه یک‌بار سطح قدرت سیگنال انتخابی
  // کاربر دوباره روی رادیو اعمال می‌شود، چون برخی رویدادهای داخلی وای‌فای (اتصال/قطع STA، اسکن و...)
  // می‌توانند بی‌سروصدا این مقدار را به پیش‌فرض (حداکثر) برگردانند.
  static unsigned long lastTxPowerRefresh = 0;
  if (millis() - lastTxPowerRefresh >= 30000UL) {
    applyApTxPower();
    lastTxPowerRefresh = millis();
  }

  // --- محافظت اضافه در برابر نویز رله (EMI) ---
  // هر ۳۰ ثانیه یک‌بار، جهت پین رله (OUTPUT) دوباره تنظیم می‌شود. نویز شدید ناشی از قطع/وصل
  // رله مجاور در موارد نادر می‌تواند حتی رجیستر پیکربندی جهت پین (نه فقط مقدار خروجی) را دستکاری کند؛
  // این کار هزینه‌ی تقریباً صفر دارد و تضمین می‌کند پین همیشه به‌عنوان خروجی باقی بماند.
  static unsigned long lastPinModeRefresh = 0;
  if (millis() - lastPinModeRefresh >= 30000UL) {
    pinMode(RELAY_PIN, OUTPUT);
    lastPinModeRefresh = millis();
  }

  // جایگزین delay(10) برای جلوگیری از مسدود شدن هسته و تنفس بهتر پردازنده و وای‌فای
  yield();
}

void updateClock() {
  // از while به‌جای if استفاده شده: اگر loop() به هر دلیلی (نوشتن روی فلش، تاخیر وای‌فای و ...)
  // بیش از ۱ ثانیه بلاک شود، ساعت باید همان لحظه جبران شود، نه اینکه طی چند اجرای بعدی loop()
  // به‌تدریج جمع شود.
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
  // جلوگیری از ذخیره‌سازی/لاگ بی‌دلیل با کش کردن وضعیت منطقی قبلی
  static int lastKnownState = -1;
  bool desiredState;

  // اگر کنترل دستی روشن است، هیچ سناریویی بررسی نمی‌شود
  if (manual_override == 1) {
    desiredState = true;
    if (lastKnownState != 1) {
      if (time_synchronized) saveTimeSetting(); // ذخیره زمان قبل از فرمان قطع/وصل رله
      lastKnownState = 1;
    }
  }
  // اگر پس از قطع برق هنوز ساعت همگام‌سازی نشده باشد، رله خاموش می‌ماند
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
      // سناریوی حذف‌شده یا غیرفعال فقط نمایش داده/ذخیره می‌شود و اجرا نمی‌شود
      if (!scenarios[i].active || !scenarios[i].enabled) continue;

      int startMinutes = scenarios[i].startHour * 60 + scenarios[i].startMinute;
      int endMinutes = scenarios[i].endHour * 60 + scenarios[i].endMinute;
      uint8_t selectedDays = scenarios[i].weekdays;

      if (startMinutes == endMinutes || selectedDays == 0) continue;
      // در سناریوی عبور از نیمه‌شب، بخش بعد از نیمه‌شب متعلق به روز شروع سناریو است.
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
      saveTimeSetting(); // ذخیره زمان قبل از فرمان قطع/وصل رله
      lastKnownState = newKnownState;
    }
  }

  // --- محافظت در برابر نویز رله مجاور (EMI) ---
  // پین رله در هر اجرای loop() دوباره نوشته می‌شود، نه فقط زمانی که وضعیت منطقی عوض شده باشد.
  // دلیل: اگر نویز الکتریکی حاصل از قطع/وصل رله کناری باعث برگشتن اتفاقی رجیستر خروجی GPIO شود،
  // قبلاً چون فقط با تغییر lastKnownState دوباره digitalWrite زده می‌شد، فریمور متوجه این "پرش" پین
  // نمی‌شد و رله تا تغییر منطقی بعدی در وضعیت غلط می‌ماند. دیگیتال‌رایت هزینه‌ی محاسباتی ناچیزی دارد،
  // پس نوشتن مکرر آن هیچ ضرری ندارد ولی از ماندگاری اثر نویز روی پین جلوگیری می‌کند.
  // توجه: ذخیره‌سازی فلش (saveTimeSetting) و لاگ فقط روی تغییر واقعی state انجام می‌شود، نه هر بار.

  // --- Anti Short-Cycle Protection ---
  // هر فرمان روشن‌شدن، از جمله حالت دستی، تا سپری‌شدن حداقل زمان خاموشی نگه داشته می‌شود.
  if (desiredState && antiShortCycleMinutes > 0 && lastRelayStatState == 0) {
    unsigned long requiredMs = (unsigned long)antiShortCycleMinutes * 60UL * 1000UL;
    if (millis() - lastRelayOffMillis < requiredMs) desiredState = false;
  }

  // --- آمار سوییچ و مدت‌کارکرد رله (برای پایش سلامت/عمر کمپرسور) ---
  // این بخش کاملاً جدا از lastKnownState بالا کار می‌کند تا به منطق اصلی سناریو/دستی دست زده نشود؛
  // فقط تغییر واقعی وضعیت خروجی رله را (فارغ از دلیل: سناریو یا دستی) برای آمار ثبت می‌کند.
  int newRelayStatState = desiredState ? 1 : 0;
  if (lastRelayStatState != newRelayStatState) {
    if (newRelayStatState == 1) {
      // کمپرسور تازه روشن شد: یک سوییچ جدید و شروع اندازه‌گیری این دوره
      relaySwitchCount++;
      relayOnSinceMillis = millis();
      relayCurrentlyOnForStats = true;
    } else {
      // کمپرسور تازه خاموش شد: مدت این دوره به مجموع اضافه می‌شود
      if (relayCurrentlyOnForStats) {
        relayTotalOnSeconds += (millis() - relayOnSinceMillis) / 1000UL;
      }
      relayCurrentlyOnForStats = false;
      lastRelayOffMillis = millis();
    }
    lastRelayStatState = newRelayStatState;
    saveRelayStats(); // ذخیره فوری هر رویداد واقعی سوییچ، مشابه ذخیره فوری ساعت قبل از فرمان رله
  }

  setRelay(desiredState);
}

// تنظیمات اصلی در NVS ذخیره می‌شوند؛ NVS مستقل از پارتیشن LittleFS/SPIFFS است و
// با خاموش/روشن شدن یا تعویض معمول فریمور پاک نمی‌شود. LittleFS فقط برای مهاجرت نسخه قدیمی خوانده می‌شود.
const char* NVS_NAMESPACE = "cooler";

bool saveNvsString(const char* key, const String &payload) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.printf("NVS open failed for key %s\n", key);
    return false;
  }
  size_t written = preferences.putString(key, payload);
  String verified = preferences.getString(key, "");
  preferences.end();
  bool ok = (written > 0 && verified == payload);
  if (!ok) Serial.printf("NVS write verification failed for key %s\n", key);
  return ok;
}

bool loadNvsString(const char* key, String &payload) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, true)) return false;
  bool exists = preferences.isKey(key);
  if (exists) payload = preferences.getString(key, "");
  preferences.end();
  return exists && payload.length() > 0;
}

void loadScenarios() {
  String payload;
  bool loadedFromNvs = loadNvsString("scenarios", payload);

  // مهاجرت یک‌باره از فایل LittleFS نسخه‌های قبلی، بدون حذف داده قدیمی کاربر.
  if (!loadedFromNvs) {
    if (!LittleFS.exists("/scenarios.json")) return;
    File file = LittleFS.open("/scenarios.json", "r");
    if (!file) return;
    payload = file.readString();
    file.close();
  }

  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, payload);
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

  if (!loadedFromNvs) saveScenarios();
}

bool saveScenarios() {
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

  String payload;
  payload.reserve(4096);
  if (serializeJson(doc, payload) == 0) return false;
  return saveNvsString("scenarios", payload);
}

void loadWiFiSettings() {
  String payload;
  bool loadedFromNvs = loadNvsString("wifi", payload);
  if (!loadedFromNvs) {
    if (!LittleFS.exists("/wifi.json")) return;
    File file = LittleFS.open("/wifi.json", "r");
    if (!file) return;
    payload = file.readString();
    file.close();
  }

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, payload)) return;

  if (doc.containsKey("ssid")) { strncpy(custom_ssid, doc["ssid"], 31); custom_ssid[31] = '\0'; }
  if (doc.containsKey("pass")) loadAndDecryptPassword(doc["pass"], custom_password, 32);
  if (doc.containsKey("sta_ssid")) { strncpy(sta_ssid, doc["sta_ssid"], 31); sta_ssid[31] = '\0'; }
  if (doc.containsKey("sta_pass")) loadAndDecryptPassword(doc["sta_pass"], sta_password, 64);
  if (doc.containsKey("internet")) internet_enabled = doc["internet"] | true;

  if (doc.containsKey("staEnabled") && !doc.containsKey("staOnMinutes") && !doc.containsKey("staOffMinutes")) {
    if (!(doc["staEnabled"] | true)) internet_enabled = false;
  }
  if (doc.containsKey("staOnMinutes")) staOnMinutes = constrain((int)(doc["staOnMinutes"] | staOnMinutes), MIN_STA_ON_MINUTES, MAX_STA_CYCLE_MINUTES);
  if (doc.containsKey("staOffMinutes")) staOffMinutes = constrain((int)(doc["staOffMinutes"] | staOffMinutes), MIN_STA_OFF_MINUTES, MAX_STA_CYCLE_MINUTES);
  if (doc.containsKey("apCycleEnabled")) apCycleEnabled = doc["apCycleEnabled"] | false;
  if (doc.containsKey("apOnMinutes")) apOnMinutes = constrain((int)(doc["apOnMinutes"] | apOnMinutes), MIN_AP_CYCLE_MINUTES, MAX_AP_CYCLE_MINUTES);
  if (doc.containsKey("apOffMinutes")) apOffMinutes = constrain((int)(doc["apOffMinutes"] | apOffMinutes), MIN_AP_CYCLE_MINUTES, MAX_AP_CYCLE_MINUTES);
  if (doc.containsKey("apTxPowerLevel")) apTxPowerLevel = constrain((int)(doc["apTxPowerLevel"] | apTxPowerLevel), 0, MAX_AP_TX_POWER_LEVEL);

  if (!loadedFromNvs) saveWiFiSettings();
}

bool saveWiFiSettings() {
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

  String payload;
  payload.reserve(768);
  if (serializeJson(doc, payload) == 0) return false;
  return saveNvsString("wifi", payload);
}

void loadOverrideSetting() {
  String value;
  bool loadedFromNvs = loadNvsString("override", value);
  if (!loadedFromNvs) {
    if (!LittleFS.exists("/override.txt")) { manual_override = 0; return; }
    File file = LittleFS.open("/override.txt", "r");
    if (!file) { manual_override = 0; return; }
    value = file.readString();
    file.close();
  }
  manual_override = value.startsWith("V") ? value.substring(value.indexOf(':') + 1).toInt() : value.toInt();
  manual_override = (manual_override == 1) ? 1 : 0;
  if (!loadedFromNvs) saveOverrideSetting();
}

bool saveOverrideSetting() {
  String payload = "V" + String(OVERRIDE_FILE_VERSION) + ":" + String(manual_override);
  return saveNvsString("override", payload);
}

// تنظیمات محافظ ضد استارت مکرر جدا از Wi-Fi نگهداری می‌شود تا توسعه‌ی آینده مستقل باشد.
void loadProtectionSettings() {
  String payload;
  bool loadedFromNvs = loadNvsString("protection", payload);
  if (!loadedFromNvs) {
    if (!LittleFS.exists("/protection.json")) return;
    File file = LittleFS.open("/protection.json", "r");
    if (!file) return;
    payload = file.readString();
    file.close();
  }
  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, payload) && doc.containsKey("minOffMinutes")) {
    antiShortCycleMinutes = constrain((int)(doc["minOffMinutes"] | 3), 0, MAX_ANTI_SHORT_CYCLE_MINUTES);
    if (!loadedFromNvs) saveProtectionSettings();
  }
}

bool saveProtectionSettings() {
  StaticJsonDocument<128> doc;
  doc["version"] = PROTECTION_FILE_VERSION;
  doc["minOffMinutes"] = antiShortCycleMinutes;
  String payload;
  if (serializeJson(doc, payload) == 0) return false;
  return saveNvsString("protection", payload);
}

// نوشتن مقدار فعلی ساعت روی یکی از دو فایل به نوبت (Round-Robin)
// این کار باعث می‌شود اگر برق درست حین نوشتن قطع شود، نسخه ذخیره شده در فایل دیگر (که دست نخورده مانده) در دسترس بماند
void saveTimeSetting() {
  timeSaveSeq++;
  timeFileSlot = 1 - timeFileSlot; // جابجایی بین فایل ۰ و ۱

  File f = LittleFS.open(TIME_FILES[timeFileSlot], "w");
  if (f) {
    size_t written = f.printf("V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d", TIME_FILE_VERSION, currentHour, currentMinute, currentSecond, time_synchronized ? 1 : 0, timeSaveSeq, currentYear, currentMonth, currentDay, currentWeekday);
    f.close();
    // اگر نوشتن ناقص بود، این فایل را حذف می‌کنیم تا readTimeFile آن را نامعتبر تشخیص دهد
    // و به‌جای یک رکورد خراب، سراغ فایل زوجش (که سالم مانده) برود.
    if (written == 0) {
      Serial.println("saveTimeSetting write failed!");
      LittleFS.remove(TIME_FILES[timeFileSlot]);
    }
  }
  lastTimeSaveMillis = millis();
}

// خواندن و اعتبارسنجی یکی از دو فایل زمان. در صورت معتبر بودن مقادیر، true برمی‌گرداند
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

  // بررسی صحت مقادیر خوانده شده تا در صورت خراب شدن فایل (نوشتن ناقص هنگام قطع برق) استفاده نشود
  if (parsed < 4 || th < 0 || th > 23 || tm < 0 || tm > 59 || ts < 0 || ts > 59 || (tsync != 0 && tsync != 1)) {
    return false;
  }

  if ((fileVersion >= 2 || parsed >= 9) && (ty < 2024 || tmon < 1 || tmon > 12 || td < 1 || td > 31 || twd < 0 || twd > 6)) return false;
  h = th; m = tm; s = ts; sync = tsync; y = ty; mon = tmon; d = td; wd = twd;
  seq = ((fileVersion >= 2 && parsed >= 10) || (fileVersion == 1 && parsed >= 9)) ? tseq : 0; // فایل‌های قدیمیِ بدون تاریخ باید دوباره همگام‌سازی شوند
  return true;
}

// بارگذاری آخرین زمان معتبر ذخیره شده، بدون توجه به دلیل ریست شدن برد
// به این ترتیب یک نوسان یا قطعی کوتاه برق، ساعت را کاملاً پاک نمی‌کند و همگام‌سازی مجدد با گوشی لازم نیست
void loadTimeSetting() {
  int h0, m0, s0, sync0, y0, mon0, d0, wd0; unsigned long seq0;
  int h1, m1, s1, sync1, y1, mon1, d1, wd1; unsigned long seq1;

  bool ok0 = readTimeFile(TIME_FILES[0], h0, m0, s0, sync0, seq0, y0, mon0, d0, wd0);
  bool ok1 = readTimeFile(TIME_FILES[1], h1, m1, s1, sync1, seq1, y1, mon1, d1, wd1);

  bool useSlot0 = false, useSlot1 = false;

  if (ok0 && ok1) {
    // هر دو فایل معتبرند: جدیدترین را بر اساس شماره توالی انتخاب کن
    if (seq1 >= seq0) useSlot1 = true; else useSlot0 = true;
  } else if (ok0) {
    useSlot0 = true;
  } else if (ok1) {
    useSlot1 = true;
  }

  if (useSlot0) {
    currentHour = h0; currentMinute = m0; currentSecond = s0; currentYear = y0; currentMonth = mon0; currentDay = d0; currentWeekday = wd0;
    time_synchronized = (sync0 == 1 && seq0 > 0); // فایل قدیمیِ بدون تاریخ باید دوباره همگام‌سازی شود
    timeFileSlot = 0;
    timeSaveSeq = seq0;
  } else if (useSlot1) {
    currentHour = h1; currentMinute = m1; currentSecond = s1; currentYear = y1; currentMonth = mon1; currentDay = d1; currentWeekday = wd1;
    time_synchronized = (sync1 == 1 && seq1 > 0); // فایل قدیمیِ بدون تاریخ باید دوباره همگام‌سازی شود
    timeFileSlot = 1;
    timeSaveSeq = seq1;
  } else {
    // هیچ رکورد معتبری پیدا نشد (اولین راه‌اندازی برد یا خرابی کامل هر دو فایل)
    currentHour = 0;
    currentMinute = 0;
    currentSecond = 0;
    time_synchronized = false;
  }
}

// نوشتن آمار سوییچ/مدت‌کارکرد رله روی یکی از دو فایل به نوبت (Round-Robin)، دقیقاً مشابه ذخیره‌سازی ساعت،
// تا در صورت قطع برق درست وسط نوشتن، نسخه‌ی فایل دیگر سالم و قابل استفاده باقی بماند.
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

// بارگذاری آخرین دریافت موفق ساعت از اینترنت (NTP) از فلش.
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

// ذخیره آخرین دریافت موفق ساعت از اینترنت به‌صورت تاریخ/ساعت مطلق؛ آخرین تلاش ناموفق اصلاً ذخیره نمی‌شود.
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

// خواندن و اعتبارسنجی یکی از دو فایل آمار رله
bool readRelayStatFile(const char* path, unsigned long &sc, unsigned long &tos, unsigned long &seq) {
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  String val = f.readString();
  f.close();

  unsigned long tsc = 0, ttos = 0, tseq = 0;
  int fileVersion = 1;
  int parsed = val.startsWith("V") ? sscanf(val.c_str(), "V%d:%lu:%lu:%lu", &fileVersion, &tsc, &ttos, &tseq) : sscanf(val.c_str(), "%lu:%lu:%lu", &tsc, &ttos, &tseq);
  if (parsed != (val.startsWith("V") ? 4 : 3)) return false; // نوشتن ناقص هنگام قطع برق؛ این رکورد نامعتبر است

  sc = tsc; tos = ttos; seq = tseq;
  return true;
}

// بارگذاری آخرین آمار معتبر سوییچ/مدت‌کارکرد رله، بدون توجه به دلیل ریست شدن برد
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
    // هیچ رکورد معتبری پیدا نشد (اولین راه‌اندازی برد یا خرابی کامل هر دو فایل)
    relaySwitchCount = 0;
    relayTotalOnSeconds = 0;
  }
}

// اتصال اختیاری برد به مودم/روتر برای گرفتن ساعت از اینترنت
// نکته: این اتصال باعث خاموش شدن Access Point برد نمی‌شود؛ گوشی همچنان می‌تواند مستقیم به خود برد وصل شود.
void setStaConnectionState(bool on) {
  if (on) {
    staCurrentlyOn = true;
    staCycleLastToggleMillis = millis();
    connectToInternetWiFi();
  } else {
    WiFi.setAutoReconnect(false); // در فاز خاموش چرخه، اجازهٔ اتصال خودکار مجدد داده نمی‌شود
    WiFi.disconnect(false);
    staCurrentlyOn = false;
    staCycleLastToggleMillis = millis();
    staEverConnectedThisBoot = false; // فاز بعدیِ روشن یک تلاش تازه محسوب می‌شود
  }
}

// مدیریت چرخهٔ دوره‌ای اتصال مودم اینترنت (STA).
// اگر مدت خاموش بودن صفر باشد، اتصال دائماً روشن می‌ماند و اصلاً وارد فاز خاموش نمی‌شود.
void manageStaCycle() {
  if (!internet_enabled || strlen(sta_ssid) == 0) {
    // وقتی اینترنت از پنل خاموش است یا مشخصات مودم وارد نشده، خودِ STA هم بی‌دلیل روشن نگه داشته نمی‌شود.
    if (staCurrentlyOn || WiFi.status() == WL_CONNECTED) {
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false);
      staCurrentlyOn = false;
    }
    staCycleLastToggleMillis = millis();
    return;
  }

  if (staOffMinutes == 0) {
    // حالت دائماً روشن: اگر قبلاً به‌خاطر چرخه خاموش شده بود، همین حالا دوباره وصل شود.
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

// اتصال اختیاری برد به مودم/روتر برای گرفتن ساعت از اینترنت
// نکته: این اتصال باعث خاموش شدن Access Point برد نمی‌شود؛ گوشی همچنان می‌تواند مستقیم به خود برد وصل شود.
void connectToInternetWiFi() {
  if (!internet_enabled) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); // اینترنت غیرفعال است؛ فقط AP برقرار می‌ماند
    return;
  }
  if (strlen(sta_ssid) == 0) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); // اگر وای‌فای اینترنت تنظیم نشده، فقط بخش STA را آزاد کن و AP باقی می‌ماند
    return;
  }

  WiFi.setAutoReconnect(true);
  Serial.print("Connecting STA to modem WiFi: ");
  Serial.println(sta_ssid);

  if (strlen(sta_password) > 0) {
    WiFi.begin(sta_ssid, sta_password);
  } else {
    WiFi.begin(sta_ssid); // پشتیبانی از شبکه‌های بدون رمز در صورت نیاز
  }

  // تنظیم ساعت با اولویت سرورهای داخلی ایران و سپس سرور جهانی
  configTime(NTP_GMT_OFFSET_SEC, 0, "ir.pool.ntp.org", "ntp.nic.ir", "pool.ntp.org");

  // نکته مهم: WiFi.begin() در هسته آردوینوی ESP32 می‌تواند به‌صورت داخلی تنظیم قدرت سیگنال رادیو را
  // به مقدار پیش‌فرض (حداکثر) بازنشانی کند. برای همین بلافاصله بعد از هر تلاش اتصال STA، سطح
  // انتخاب‌شده‌ی کاربر دوباره روی رادیو اعمال می‌شود تا تنظیم «قدرت سیگنال» واقعاً پابرجا بماند.
  applyApTxPower();
}

// تلاش دوره‌ای و غیرمسدودکننده برای دریافت ساعت از اینترنت
void tryNtpSync() {
  // نکته مهم: اینجا از ntp_synced_this_boot استفاده می‌شود، نه time_synchronized.
  // time_synchronized ممکن است از فایل ذخیره‌شده (سنکرون قبلی با گوشی یا بوت قبلی) از همان لحظه بوت true باشد؛
  // اگر آن را ملاک فاصله‌ی تلاش قرار می‌دادیم، برد فکر می‌کرد NTP قبلاً موفق بوده و به‌جای هر ۱ دقیقه
  // هر ۱ ساعت یک‌بار تلاش می‌کرد — یعنی با اینکه به مودم وصل است، ممکن بود تا یک ساعت ساعت واقعی نگیرد.
  unsigned long interval = ntp_synced_this_boot ? NTP_RECHECK_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;

  if (!internet_enabled) return;
  if (!staCurrentlyOn) return;
  if (strlen(sta_ssid) == 0) return;

  if (ntpFirstCheckPending) {
    // اولین بار بعد از این بوت یا بعد از تغییر تنظیمات مودم: فارغ از فاصله‌ی زمانی، همین الان تلاش کن
    ntpFirstCheckPending = false;
  } else if (millis() - lastNtpCheckMillis < interval) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    // تا وقتی واقعاً به مودم وصل نشده‌ایم، زمان آخرین تلاش را جلو نمی‌بریم تا
    // به محض برقراری اتصال، گرفتن ساعت بدون انتظار اضافه انجام شود.
    return;
  }

  lastNtpCheckMillis = millis();

  time_t now = time(nullptr);
  struct tm *tmInfo = localtime(&now);

  // اگر سال معتبر بود یعنی NTP زمان واقعی داده است.
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
    saveTimeSetting();     // ساعت اینترنتی معتبر را ذخیره کن تا در قطعی برق کوتاه از دست نرود
    saveNtpSuccessInfo();  // فقط آخرین دریافت موفق NTP ذخیره می‌شود، نه تلاش‌های ناموفق
  }
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  server.sendHeader("Access-Control-Max-Age", "600");
  server.sendHeader("Cache-Control", "no-store");
}

void handleOptions() {
  sendCorsHeaders();
  server.send(204, "text/plain", "");
}

// The UI is bundled in the Android application. ESP32 now serves only small JSON APIs.
void handleRoot() {
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"online\",\"device\":\"ESP32 Cooler\",\"apiVersion\":3,\"storageBackend\":\"NVS\"}");
}

void handleGetScenarios() {
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  JsonArray items = doc.to<JsonArray>();
  for (int i = 0; i < MAX_SCENARIOS; i++) {
    if (!scenarios[i].active) continue;
    JsonObject item = items.createNestedObject();
    item["active"] = true;
    item["en"] = scenarios[i].enabled;
    item["sh"] = scenarios[i].startHour;
    item["sm"] = scenarios[i].startMinute;
    item["eh"] = scenarios[i].endHour;
    item["em"] = scenarios[i].endMinute;
    item["wd"] = scenarios[i].weekdays;
  }
  String body;
  serializeJson(doc, body);
  sendCorsHeaders();
  server.send(200, "application/json", body);
}

// Configuration is requested once when the local Android UI starts. Returning the same
// values that the old generated HTML embedded keeps the panel behaviour unchanged.
void handleGetConfig() {
  StaticJsonDocument<768> doc;
  doc["ssid"] = custom_ssid;
  doc["pass"] = custom_password;
  doc["staSsid"] = sta_ssid;
  doc["staPass"] = sta_password;
  doc["internetEnabled"] = internet_enabled ? 1 : 0;
  doc["staOnMinutes"] = staOnMinutes;
  doc["staOffMinutes"] = staOffMinutes;
  doc["apCycleEnabled"] = apCycleEnabled ? 1 : 0;
  doc["apOnMinutes"] = apOnMinutes;
  doc["apOffMinutes"] = apOffMinutes;
  doc["apTxPowerLevel"] = apTxPowerLevel;
  doc["protectionMinutes"] = antiShortCycleMinutes;
  doc["storageBackend"] = "NVS";
  doc["firmwareApiVersion"] = 3;
  String body;
  serializeJson(doc, body);
  sendCorsHeaders();
  server.send(200, "application/json", body);
}

void handleSaveScenario() {
  if (!allowRequest(lastSaveScenarioRequest, 1200UL)) return;
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  String body = server.arg("plain");
  // تغییر به DynamicJsonDocument برای جلوگیری از Stack Overflow
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
    temp[i].enabled = v["en"] | true; // این کلید اجرای سناریو را بدون حذف کردن کنترل می‌کند
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
    Scenario previous[MAX_SCENARIOS];
    for (int j = 0; j < MAX_SCENARIOS; j++) {
      previous[j] = scenarios[j];
      scenarios[j] = temp[j];
    }
    if (!saveScenarios()) {
      for (int j = 0; j < MAX_SCENARIOS; j++) scenarios[j] = previous[j];
      server.send(500, "text/plain", "Storage Error");
      return;
    }
  }

  server.send(200, "text/plain", "Updated");
}

void handleSyncTime() {
  if (!allowRequest(lastSyncRequest, 1000UL)) return;
  if (server.hasArg("h") && server.hasArg("m") && server.hasArg("s") && server.hasArg("y") && server.hasArg("mon") && server.hasArg("d") && server.hasArg("wd")) {
    int h = server.arg("h").toInt();
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    int y = server.arg("y").toInt(), mon = server.arg("mon").toInt(), d = server.arg("d").toInt(), wd = server.arg("wd").toInt();

    // اعتبارسنجی بازه‌ی مقادیر؛ بدون این چک یک درخواست دستکاری‌شده می‌توانست
    // ساعت داخلی برد را با عددی نامعتبر (مثلاً 99) خراب کند و منطق سناریوها را به‌هم بریزد.
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
  // پنل هر ۱ ثانیه درخواست می‌دهد؛ حد ۲۵۰ms فقط اسپم غیرعادی را رد می‌کند.
  if (!allowRequest(lastStatusRequest, 150UL)) return;
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);

  // خواندن وضعیت منطقی به جای وضعیت خام پین
  int logicalState = (digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL) ? 1 : 0;

  int staConnected = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  int staConfigured = (strlen(sta_ssid) > 0) ? 1 : 0;
  char staIp[16] = "";
  if (staConnected) {
    IPAddress ip = WiFi.localIP();
    snprintf(staIp, sizeof(staIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }

  // --- وضعیت STA برای نمایش در پنل ---
  // 0 = غیرفعال/تنظیم‌نشده
  // 1 = در حال تلاش برای اتصال، هنوز هیچ‌وقت موفق نشده (نارنجی)
  // 2 = متصل (سبز)
  // 3 = قبلاً وصل بوده و الان قطع است (قرمز)
  // 4 = طبق زمان‌بندی، موقتاً در فاز خاموش چرخه قرار دارد
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

  // مجموع مدت‌کارکرد رله برای نمایش زنده: مقدار قطعی‌شده + مدت دوره‌ی جاری (اگر همین الان روشن است)
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

  // وضعیت چرخه‌ی AP: تا چند ثانیه دیگر تا تغییر فاز بعدی (روشن<->خاموش) باقی مانده.
  int apStationCount = apCurrentlyOn ? WiFi.softAPgetStationNum() : 0;
  long apRemainingSec = 0;
  if (apCycleEnabled) {
    unsigned long phaseMs = apCurrentlyOn ? ((unsigned long)apOnMinutes * 60000UL) : ((unsigned long)apOffMinutes * 60000UL);
    unsigned long elapsed = millis() - apCycleLastToggleMillis;
    if (elapsed < phaseMs) apRemainingSec = (long)((phaseMs - elapsed + 999UL) / 1000UL);
  }

  // وضعیت چرخه‌ی STA: فقط وقتی اینترنت فعال و مودم تنظیم شده باشد معنا دارد.
  long staRemainingSec = 0;
  if (internet_enabled && staConfigured && staOffMinutes > 0) {
    unsigned long phaseMs = staCurrentlyOn ? ((unsigned long)staOnMinutes * 60000UL) : ((unsigned long)staOffMinutes * 60000UL);
    unsigned long elapsed = millis() - staCycleLastToggleMillis;
    if (elapsed < phaseMs) staRemainingSec = (long)((phaseMs - elapsed + 999UL) / 1000UL);
  }

  char json[1152];
  snprintf(json, sizeof(json), "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"sta\":%d,\"internetEnabled\":%d,\"staConfigured\":%d,\"staState\":%d,\"staIp\":\"%s\",\"staOnMinutes\":%d,\"staOffMinutes\":%d,\"staPhaseOn\":%d,\"staRemaining\":%ld,\"ntpOk\":%d,\"ntpLastValid\":%d,\"ntpYear\":%d,\"ntpMonth\":%d,\"ntpDay\":%d,\"ntpHour\":%d,\"ntpMinute\":%d,\"ntpSecond\":%d,\"switchCount\":%lu,\"onSeconds\":%lu,\"weekday\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"protectionMinutes\":%d,\"protectionRemaining\":%ld,\"apCycleEnabled\":%d,\"apOn\":%d,\"apOnMinutes\":%d,\"apOffMinutes\":%d,\"apTxPowerLevel\":%d,\"apRemaining\":%ld,\"apClientConnected\":%d}",
    timeStr, logicalState, manual_override, time_synchronized ? 1 : 0, staConnected, internet_enabled ? 1 : 0, staConfigured, staState, staIp,
    staOnMinutes, staOffMinutes, staCurrentlyOn ? 1 : 0, staRemainingSec,
    ntpEverSucceeded ? 1 : 0, ntpLastSuccessValid ? 1 : 0, ntpLastSuccessYear, ntpLastSuccessMonth, ntpLastSuccessDay, ntpLastSuccessHour, ntpLastSuccessMinute, ntpLastSuccessSecond, relaySwitchCount, liveOnSeconds, currentWeekday, currentYear, currentMonth, currentDay, antiShortCycleMinutes, protectionRemainingSec,
    apCycleEnabled ? 1 : 0, apCurrentlyOn ? 1 : 0, apOnMinutes, apOffMinutes, apTxPowerLevel, apRemainingSec, (apStationCount > 0) ? 1 : 0);
  server.send(200, "application/json", json);
}

void handleToggleManual() {
  // مهم‌ترین محدودیت: رله بیشتر از یک‌بار در ۱.۵ ثانیه فرمان دستی نمی‌گیرد.
  if (!allowRequest(lastToggleManualRequest, 1500UL)) return;

  // ابتدا حالت جدید را به‌صورت تراکنشی ذخیره می‌کنیم و فقط در صورت موفقیت به رله اعمال می‌کنیم.
  int previousOverride = manual_override;
  manual_override = (manual_override == 0) ? 1 : 0;
  if (!saveOverrideSetting()) {
    manual_override = previousOverride;
    server.send(500, "text/plain", "Storage Error");
    return;
  }

  if (time_synchronized) saveTimeSetting();
  delay(100); // اجازه اتمام کامل عملیات فلش پیش از فرمان رله
  checkScenarios();
  server.send(200, "text/plain", "OK");
}

// ذخیره تنظیمات فرستنده (Access Point) — شبکه‌ای که گوشی مستقیم به آن وصل می‌شود.
// چون نام/رمز همین شبکه عوض می‌شود، گوشی باید دوباره به شبکه جدید وصل شود؛
// برای اطمینان از اعمال کامل تنظیمات AP، برد ری‌استارت می‌شود.
void handleSaveAP() {
  if (!allowRequest(lastSaveApRequest, 3000UL)) return;
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String new_ssid = server.arg("ssid");
    String new_pass = server.arg("pass");

    // رمز AP خود برد طبق قانون ESP32 باید حداقل ۸ کاراکتر باشد.
    if (new_ssid.length() > 0 && new_pass.length() >= 8) {
      String previousSsid = custom_ssid;
      String previousPassword = custom_password;
      new_ssid.toCharArray(custom_ssid, 32);
      new_pass.toCharArray(custom_password, 32);
      if (!saveWiFiSettings()) {
        previousSsid.toCharArray(custom_ssid, 32);
        previousPassword.toCharArray(custom_password, 32);
        server.send(500, "text/plain", "Storage Error");
        return;
      }
      saveTimeSetting();

      server.send(200, "text/plain", "OK");

      pendingReset = true;
      resetMillis = millis();
      return;
    }
  }
  server.send(400, "text/plain", "Bad Request");
}

// ذخیره تنظیمات اتصال به مودم/روتر اینترنت (STA) + کلید فعال‌سازی اینترنت.
// این بخش شبکهٔ AP خود برد را تغییر نمی‌دهد، پس نیازی به ری‌استارت برد نیست؛
// چرخهٔ STA همان لحظه با تنظیمات جدید از نو شروع می‌شود و AP/ارتباط گوشی دست‌نخورده می‌ماند.
void handleSaveSTA() {
  if (!allowRequest(lastSaveStaRequest, 1500UL)) return;
  String new_sta_ssid = server.hasArg("sta_ssid") ? server.arg("sta_ssid") : "";
  String new_sta_pass = server.hasArg("sta_pass") ? server.arg("sta_pass") : "";
  String new_internet = server.hasArg("internet") ? server.arg("internet") : "";
  String new_sta_on = server.hasArg("sta_on_minutes") ? server.arg("sta_on_minutes") : "";
  String new_sta_off = server.hasArg("sta_off_minutes") ? server.arg("sta_off_minutes") : "";

  // رمز مودم اینترنت اختیاری است؛ اگر وارد شود بهتر است حداقل ۸ کاراکتر باشد.
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

  bool previousInternetEnabled = internet_enabled;
  int previousStaOn = staOnMinutes, previousStaOff = staOffMinutes;
  String previousStaSsid = sta_ssid;
  String previousStaPassword = sta_password;

  internet_enabled = (new_internet == "1");
  staOnMinutes = onVal;
  staOffMinutes = offVal;
  new_sta_ssid.toCharArray(sta_ssid, 32);
  new_sta_pass.toCharArray(sta_password, 64);
  if (!saveWiFiSettings()) {
    internet_enabled = previousInternetEnabled;
    staOnMinutes = previousStaOn;
    staOffMinutes = previousStaOff;
    previousStaSsid.toCharArray(sta_ssid, 32);
    previousStaPassword.toCharArray(sta_password, 64);
    server.send(500, "text/plain", "Storage Error");
    return;
  }

  // اجازه بده تلاش بعدی NTP بلافاصله (هر ۱ دقیقه) انجام شود، نه اینکه منتظر فاصله‌ی قبلی بماند.
  ntp_synced_this_boot = false;
  ntpFirstCheckPending = true;
  lastNtpCheckMillis = 0;
  staEverConnectedThisBoot = false;

  // چرخهٔ STA با تنظیمات جدید از نو شروع می‌شود و ابتدا وارد فاز روشن می‌شود؛
  // اگر اینترنت خاموش باشد یا SSID خالی باشد، connectToInternetWiFi خودش STA را خاموش نگه می‌دارد.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  staCurrentlyOn = true;
  staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  server.send(200, "text/plain", "OK");
}

// ذخیرهٔ زمان محافظت؛ بدون ری‌استارت و با اعتبارسنجی کامل.
void handleSaveProtection() {
  if (!allowRequest(lastSaveProtectionRequest, 1000UL)) return;
  if (!server.hasArg("min_off")) { server.send(400, "text/plain", "Bad Request"); return; }
  String raw = server.arg("min_off");
  if (raw.length() == 0) { server.send(400, "text/plain", "Bad Request"); return; }
  for (size_t i = 0; i < raw.length(); i++) if (!isDigit(raw[i])) { server.send(400, "text/plain", "Bad Request"); return; }
  int value = raw.toInt();
  if (value < 0 || value > MAX_ANTI_SHORT_CYCLE_MINUTES) { server.send(400, "text/plain", "Bad Request"); return; }
  int previousValue = antiShortCycleMinutes;
  antiShortCycleMinutes = value;
  if (!saveProtectionSettings()) {
    antiShortCycleMinutes = previousValue;
    server.send(500, "text/plain", "Storage Error");
    return;
  }
  server.send(200, "text/plain", "OK");
}

// ذخیرهٔ تنظیمات چرخه‌ی روشن/خاموش AP و قدرت سیگنال. بدون ری‌استارت اعمال می‌شود.
// نکته: بلافاصله بعد از ذخیره، تایمر چرخه از نو شروع می‌شود و مطمئن می‌شویم AP روشن است تا
// همین درخواستِ ذخیره (که از طریق خودِ AP ارسال شده) به‌محض ثبت قطع نشود.
void handleSaveApCycle() {
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

  bool previousEnabled = apCycleEnabled;
  int previousOn = apOnMinutes, previousOff = apOffMinutes, previousPower = apTxPowerLevel;
  apCycleEnabled = (enabledRaw == "1");
  apOnMinutes = onVal;
  apOffMinutes = offVal;
  apTxPowerLevel = powerVal;
  applyApTxPower();

  // شروع دوباره‌ی تایمر چرخه و اطمینان از روشن بودن AP همین الان (تا درخواست جاری قطع نشود)
  apCycleLastToggleMillis = millis();
  if (!apCurrentlyOn) setApRadioState(true);

  if (!saveWiFiSettings()) {
    apCycleEnabled = previousEnabled;
    apOnMinutes = previousOn;
    apOffMinutes = previousOff;
    apTxPowerLevel = previousPower;
    applyApTxPower();
    server.send(500, "text/plain", "Storage Error");
    return;
  }
  server.send(200, "text/plain", "OK");
}
