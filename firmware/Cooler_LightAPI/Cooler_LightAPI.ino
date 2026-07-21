/* =========================================================================
 *  Cooler Light-API Firmware — چندبردی (ESP32 / ESP8266)
 * =========================================================================
 *  این فایل .ino فقط حاوی منطق مشترک برنامه است. بخش‌های وابسته به برد
 *  با include کردن board.h در زیر خودکار انتخاب می‌شوند (بدون شرط و پیچیدگی).
 *  کتابخانه‌های اضافی هر برد در فایل‌های جدا (مثل soft_aes.h) کنار برنامه قرار
 *  دارند و فقط برای همان برد کامپایل می‌شوند.
 *
 *  منطق برنامه دقیقاً همان نسخه‌ی اصلی است، فقط:
 *    1) ارسال HTML/CSS/JS از سمت ESP حذف شده (فقط JSON/REST API می‌ماند).
 *    2) دو endpoint جدید اضافه شده: /config و /scenarios (برای پر کردن خودکار فرم در اپ).
 *    3) CORS روی همه‌ی endpointها فعال شده.
 *    4) WDT / file-system / AES با توجه به برد توسط board.h انتخاب می‌شوند.
 * ========================================================================= */

#include <Arduino.h>
#include "board.h"               // انتخاب خودکار هدر مناسب ESP32/ESP8266

// -------------------- ثابت‌ها و تنظیمات (بدون تغییر نسبت به نسخه‌ی اصلی) ----
const char* PROGRAM_NAME    = "کولر هوشمند ESP32";
const char* PROGRAM_TAGLINE = BOARD_TAGLINE_STR;

#ifndef RELAY_ACTIVE_LEVEL
  #define RELAY_ACTIVE_LEVEL HIGH
#endif

const long  NTP_GMT_OFFSET_SEC = 12600;  // UTC+3:30

char custom_ssid[32]     = "ESP32_Timer_Hub";
char custom_password[32] = "12345678";
char sta_ssid[32]     = "";
char sta_password[64] = "";
bool internet_enabled = true;
int  staOnMinutes  = 10;
int  staOffMinutes = 0;
static const int MIN_STA_ON_MIN   = 1, MAX_STA_CYCLE_MIN = 1440;
static const int MIN_STA_OFF_MIN  = 0;
bool staCurrentlyOn = true;
unsigned long staCycleLastToggleMillis = 0;

bool apCycleEnabled = false;
int  apOnMinutes = 10, apOffMinutes = 5;
static const int MIN_AP_CYCLE_MIN = 1, MAX_AP_CYCLE_MIN = 1440;
bool apCurrentlyOn = true;
unsigned long apCycleLastToggleMillis = 0;
int apTxPowerLevel = 3;
static const int MAX_TX_POWER = 3;

static const int MAX_SCENARIOS = 20;
int antiShortCycleMinutes = 3;
static const int MAX_ANTI_SHORT_CYCLE_MIN = 1440;
unsigned long lastRelayOffMillis = 0;

static const size_t SCEN_JSON_CAP = 6144;

unsigned long lastToggleManual=0, lastSaveSc=0, lastSync=0, lastSaveAp=0,
              lastSaveSta=0, lastSaveProt=0, lastSaveApCycle=0, lastStatus=0,
              lastConfig=0, lastScList=0;

struct Scenario{
  bool active=false, enabled=false;
  int startHour=0,startMinute=0,endHour=0,endMinute=0;
  uint8_t weekdays=0x7F;
};
Scenario scenarios[MAX_SCENARIOS];

int currentHour=0,currentMinute=0,currentSecond=0;
int currentYear=2026,currentMonth=1,currentDay=1,currentWeekday=3;
unsigned long lastTick=0;
int manual_override=0;
bool time_synchronized=false;
bool ntp_ever_ok=false, ntp_last_valid=false;
int  ntp_y=2026,ntp_mo=1,ntp_d=1,ntp_h=0,ntp_mi=0,ntp_s=0;
static const char* NTP_FILE="/ntp.json";

bool ntpSyncedThisBoot=false;
bool ntpFirstPending=true;
unsigned long lastNtpCheck=0;
bool staEverOk=false;

bool pendingReset=false;
unsigned long resetMillis=0;

static const char* TIME_FILES[2]={"/time0.txt","/time1.txt"};
int timeSlot=0; unsigned long timeSeq=0, lastTimeSave=0;

unsigned long switchCount=0, totalOnSec=0, onSince=0;
bool onForStats=false; int lastStatState=-1;
static const char* STAT_FILES[2]={"/relaystat0.txt","/relaystat1.txt"};
int statSlot=0; unsigned long statSeq=0, lastStatSave=0;

WebServer_t server(80);

// -------------------- پیش‌اعلان توابع --------------------
void loadScenarios(); void saveScenarios();
void loadWifi(); void saveWifi();
void loadOverride(); void saveOverride();
void loadProt(); void saveProt();
void loadTime(); void saveTime();
bool readTime(const char*,int&,int&,int&,int&,unsigned long&,int&,int&,int&,int&);
void advanceDate(); bool isLeap(int);
void loadStats(); void saveStats();
bool readStat(const char*,unsigned long&,unsigned long&,unsigned long&);
void loadNtpInfo(); void saveNtpInfo();
bool allow(unsigned long&,unsigned long);
void checkScenarios(); void tickClock();
void setRelay(bool);
void setAp(bool); void manageApCycle();
void setStaOn(bool); void manageStaCycle();
void staConnect(); void tryNtp();
// توابع رمزنگاری در board.h / soft_aes.h آمده‌اند (برای ESP32 از mbedtls، برای ESP8266 از soft_aes.h)
String encryptPassword(const char*,size_t);
void   loadAndDecryptPassword(const char*,char*,size_t);

// -------------------- helpers --------------------
#include "helpers.h"

// -------------------- CORS --------------------
void cors(){
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers","Content-Type");
}
void corsOptions(){ cors(); server.send(204,"text/plain",""); }

bool allow(unsigned long &lr, unsigned long mi){
  unsigned long now=millis();
  if(lr && (unsigned long)(now-lr)<mi){cors();server.send(429,"text/plain","Too Many Requests");return false;}
  lr=now; return true;
}

void setRelay(bool s){ digitalWrite(RELAY_PIN, s?RELAY_ACTIVE_LEVEL:!RELAY_ACTIVE_LEVEL); }
bool isLeap(int y){ return (y%4==0&&y%100!=0)||(y%400==0); }
void advanceDate(){
  static const int m[]={31,28,31,30,31,30,31,31,30,31,30,31};
  int d=m[currentMonth-1]; if(currentMonth==2&&isLeap(currentYear))d=29;
  currentDay++;
  if(currentDay>d){currentDay=1;currentMonth++;if(currentMonth>12){currentMonth=1;currentYear++;}}
  currentWeekday=(currentWeekday+1)%7;
}

// ===========================================================
//   SETUP / LOOP
// ===========================================================
void setup(){
  SERIAL_BEGIN(115200);
  SETUP_WDT();
  FS_INIT();
  loadScenarios(); loadWifi(); loadOverride(); loadProt();
  loadTime(); loadStats(); loadNtpInfo();
  pinMode(RELAY_PIN,OUTPUT);
  setRelay(manual_override==1);
  lastStatState = (manual_override==1)?1:0;
  if(lastStatState==0) lastRelayOffMillis=millis();
  else { onSince=millis(); onForStats=true; }

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false); WiFi.setAutoReconnect(true);
  WiFi.softAP(custom_ssid,custom_password,1,0,3);
  boardApplyTxPower(apTxPowerLevel);
  apCurrentlyOn=true; apCycleLastToggleMillis=millis();
  staCurrentlyOn=true; staCycleLastToggleMillis=millis();
  staConnect();

  server.on("/",HTTP_GET,[](){
    cors();
    String p=String("<!DOCTYPE html><html dir=rtl lang=fa><head><meta charset=utf-8>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>"+String(PROGRAM_NAME)+"</title>"
      "<style>body{font-family:Tahoma,sans-serif;background:#0a0c0d;color:#eef0f1;text-align:center;padding:40px 20px;}"
      "h1{color:#1de9c4}code{background:#15181a;padding:4px 8px;border-radius:6px;color:#29d3c8;direction:ltr;display:inline-block;}</style>"
      "</head><body><h1>"+String(PROGRAM_NAME)+"</h1>"
      "<p>دستگاه در حالت <b>Light-API</b> است.</p>"
      "<p>UI روی اپ موبایل / PWA است. برد فقط REST/JSON سرو می‌دهد.</p>"
      "<p>API endpoints:<br><code>GET /status</code> <code>GET /config</code> <code>GET /scenarios</code><br>"
      "<code>POST /toggle-manual</code> <code>POST /sync</code> <code>POST /save</code><br>"
      "<code>POST /save-ap</code> <code>POST /save-sta</code> <code>POST /save-ap-cycle</code> <code>POST /save-protection</code>"
      "</p><p style='color:#868c90;margin-top:24px;font-size:.8rem'>"+String(BOARD_NAME_STR)+" · "+String(BOARD_TAGLINE_STR)+"</p>"
      "</body></html>");
    server.send(200,"text/html; charset=utf-8",p);
  });
  server.on("/save",HTTP_POST,handleSaveSc);
  server.on("/sync",HTTP_POST,handleSync);
  server.on("/status",HTTP_GET,handleStatus);
  server.on("/config",HTTP_GET,handleConfig);
  server.on("/scenarios",HTTP_GET,handleScList);
  server.on("/toggle-manual",HTTP_POST,handleToggleMan);
  server.on("/save-ap",HTTP_POST,handleSaveAp);
  server.on("/save-sta",HTTP_POST,handleSaveSta);
  server.on("/save-protection",HTTP_POST,handleSaveProt);
  server.on("/save-ap-cycle",HTTP_POST,handleSaveApCycle);
  server.onNotFound([](){ if(server.method()==HTTP_OPTIONS){corsOptions();return;} cors();server.send(404,"text/plain","Not Found"); });
  server.begin();
  lastTick=millis(); lastTimeSave=millis();
}

void loop(){
  FEED_WDT();
  static int lowHeap=0;
  if(FREE_HEAP()<2500){lowHeap++;if(lowHeap>=5){saveTime();RESTART();}}else lowHeap=0;
  if(pendingReset && millis()-resetMillis>2000){saveTime();RESTART();}
  server.handleClient();
  tickClock(); manageStaCycle(); tryNtp(); checkScenarios(); manageApCycle();
  if(time_synchronized && millis()-lastTimeSave>=300000UL) saveTime();
  if(onForStats && millis()-lastStatSave>=300000UL){
    totalOnSec += (millis()-onSince)/1000UL; onSince+=((millis()-onSince)/1000UL)*1000UL; saveStats();
  }
  static unsigned long txR=0,pinR=0;
  if(millis()-txR>=30000UL){boardApplyTxPower(apTxPowerLevel);txR=millis();}
  if(millis()-pinR>=30000UL){pinMode(RELAY_PIN,OUTPUT);pinR=millis();}
  yield();
}

// ===========================================================
//   منطق اصلی (ساعت / سناریو / رله) — دقیقاً منطق نسخه‌ی اصلی
// ===========================================================
#include "logic.ino"
