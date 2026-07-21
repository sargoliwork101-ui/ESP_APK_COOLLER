/* =========================================================================
 *  board_esp8266.h  —  پیکربندی مخصوص ESP8266 (ESP-01, ESP-12, NodeMCU, D1 mini)
 *  • از soft_aes.h برای رمزنگاری استفاده می‌کند (mbedtls در ESP8266 نیست).
 *  • فایل‌سیستم LittleFS (در هسته جدید) یا SPIFFS.
 * ========================================================================= */
#ifndef BOARD_ESP8266_H
#define BOARD_ESP8266_H

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Esp.h>
#include "soft_aes.h"   // پیاده‌سازی نرم‌افزاری AES (فقط برای ESP8266)

#define BOARD_NAME_STR    "ESP8266"
#define BOARD_TAGLINE_STR "ESP8266 · Light-API"

#ifndef RELAY_PIN
  // برای ESP-01 GPIO2 پیش‌فرض (پین امنی است که به بوت لطمه نمی‌زند).
  // برای NodeMCU/Wemos می‌توانید آن را به D2 (GPIO4) تغییر دهید.
  #define RELAY_PIN 2
#endif

typedef ESP8266WebServer WebServer_t;

// --------- File-system ---------
#define FS_INIT()        do{ if(!LittleFS.begin()){ LittleFS.format(); LittleFS.begin(); } }while(0)
#define FS_EXISTS(p)     LittleFS.exists(p)
#define FS_OPEN_READ(p)  LittleFS.open(p,"r")
#define FS_OPEN_WRITE(p) LittleFS.open(p,"w")
#define FS_REMOVE(p)     LittleFS.remove(p)

// --------- Watchdog ---------
static inline void _wdt_init(){ ESP.wdtDisable(); ESP.wdtEnable(8000); }
static inline void _wdt_feed(){ ESP.wdtFeed(); yield(); }
#define SETUP_WATCHDOG() _wdt_init()
#define FEED_WATCHDOG()  _wdt_feed()

// --------- Tx power ---------
static inline void boardApplyTxPower(int lvl){
  float dbm;
  switch(lvl){
    case 0: dbm=5.0f;  break;
    case 1: dbm=11.0f; break;
    case 2: dbm=15.0f; break;
    default:dbm=20.5f; break;
  }
  WiFi.setOutputPower(dbm);
}

// --------- NTP ---------
static inline void boardConfigTime(){
  configTime(12600, 0, "ir.pool.ntp.org","ntp.nic.ir","pool.ntp.org");
}
static inline bool boardNtpSync(int&h,int&m,int&s,int&y,int&mo,int&d,int&wd){
  time_t now=time(nullptr);
  if(now<1700000000) return false;
  struct tm* t=localtime(&now); if(!t) return false;
  h=t->tm_hour;m=t->tm_min;s=t->tm_sec;
  y=t->tm_year+1900;mo=t->tm_mon+1;d=t->tm_mday;
  wd=(t->tm_wday+1)%7;
  return true;
}

// --------- Heap / Restart ---------
#define FREE_HEAP()  ((size_t)ESP.getFreeHeap())
#define RESTART()    ESP.restart()
#define SERIAL_BEGIN(b) Serial.begin(b)

// --------- AES (از soft_aes.h) ---------
static inline String encryptPassword(const char* p, size_t sz){ return softEncryptPassword(p, sz); }
static inline void loadAndDecryptPassword(const char* sv, char* out, size_t sz){ softLoadAndDecryptPassword(sv, out, sz); }

#endif
