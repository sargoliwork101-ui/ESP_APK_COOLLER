/* =========================================================================
 *  board_esp32.h  —  پیکربندی مخصوص ESP32 (WROOM/WROVER/DevKit/...)
 * ========================================================================= */
#ifndef BOARD_ESP32_H
#define BOARD_ESP32_H

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include "mbedtls/aes.h"

#define BOARD_NAME_STR    "ESP32"
#define BOARD_TAGLINE_STR "ESP32 · Light-API"

#ifndef RELAY_PIN
  #define RELAY_PIN 23
#endif

typedef WebServer WebServer_t;

// --------- File-system (LittleFS) ---------
#define FS_INIT()        do{ if(!LittleFS.begin(true)) Serial.println("LittleFS mount failed"); }while(0)
#define FS_EXISTS(p)     LittleFS.exists(p)
#define FS_OPEN_READ(p)  LittleFS.open(p,"r")
#define FS_OPEN_WRITE(p) LittleFS.open(p,"w")
#define FS_REMOVE(p)     LittleFS.remove(p)

// --------- Watchdog ---------
static inline void _wdt_init(){
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR>=3
  esp_task_wdt_config_t c={8*1000,(1<<portNUM_PROCESSORS)-1,true};
  esp_task_wdt_init(&c);
#else
  esp_task_wdt_init(8,true);
#endif
  esp_task_wdt_add(NULL);
}
static inline void _wdt_feed(){ esp_task_wdt_reset(); }
#define SETUP_WATCHDOG() _wdt_init()
#define FEED_WATCHDOG()  _wdt_feed()

// --------- Tx power ---------
static inline void boardApplyTxPower(int lvl){
  wifi_power_t p;
  switch(lvl){
    case 0: p=WIFI_POWER_5dBm; break;
    case 1: p=WIFI_POWER_11dBm; break;
    case 2: p=WIFI_POWER_15dBm; break;
    default:p=WIFI_POWER_19_5dBm; break;
  }
  WiFi.setTxPower(p);
}

// --------- NTP / Time ---------
static inline void boardConfigTime(){
  configTime(12600,0,"ir.pool.ntp.org","ntp.nic.ir","pool.ntp.org");
}
static inline bool boardNtpSync(int&h,int&m,int&s,int&y,int&mo,int&d,int&wd){
  time_t now=time(nullptr); struct tm* t=localtime(&now);
  if(!t||t->tm_year+1900<2024) return false;
  h=t->tm_hour;m=t->tm_min;s=t->tm_sec;
  y=t->tm_year+1900; mo=t->tm_mon+1; d=t->tm_mday;
  wd=(t->tm_wday+1)%7;
  return true;
}

// --------- Heap / Restart / Serial ---------
#define FREE_HEAP()  ((size_t)ESP.getFreeHeap())
#define RESTART()    ESP.restart()
#define SERIAL_BEGIN(b) Serial.begin(b)

// --------- AES رمزنگاری — دقیقاً همان کد اصلی با mbedtls سخت‌افزاری ESP32
static const unsigned char _AES_KEY[16] PROGMEM = {
  0x4F,0xA1,0xC3,0x92,0x0E,0x77,0xB5,0x2D,
  0x8C,0x1A,0x6F,0xE4,0x33,0xD9,0x5B,0x88
};
static inline String encryptPassword(const char* p, size_t bufSize){
  if(!p||!*p) return String("");
  mbedtls_aes_context aes; mbedtls_aes_init(&aes); mbedtls_aes_setkey_enc(&aes,_AES_KEY,128);
  size_t pad=(bufSize+15)/16*16;
  unsigned char *in=(unsigned char*)calloc(pad,1); if(!in){mbedtls_aes_free(&aes);return "";}
  strncpy((char*)in,p,bufSize-1);
  unsigned char *out=(unsigned char*)calloc(pad,1);
  if(!out){free(in);mbedtls_aes_free(&aes);return "";}
  for(size_t i=0;i<pad;i+=16) mbedtls_aes_crypt_ecb(&aes,MBEDTLS_AES_ENCRYPT,in+i,out+i);
  mbedtls_aes_free(&aes);
  String r="ENC:";
  for(size_t i=0;i<pad;i++){char h[3];snprintf(h,3,"%02x",out[i]);r+=h;}
  free(in);free(out);
  return r;
}
static inline void loadAndDecryptPassword(const char* sv,char* out,size_t sz){
  if(!sv||!*sv){out[0]=0;return;}
  String v(sv);
  if(!v.startsWith("ENC:")){strncpy(out,sv,sz-1);out[sz-1]=0;return;}
  String hx=v.substring(4); size_t len=hx.length();
  if(len%32!=0) return;
  size_t pad=len/2;
  unsigned char *in=(unsigned char*)calloc(pad,1); if(!in) return;
  for(size_t i=0;i<pad;i++){char c[3]={hx[i*2],hx[i*2+1],0};in[i]=(unsigned char)strtol(c,nullptr,16);}
  mbedtls_aes_context aes; mbedtls_aes_init(&aes); mbedtls_aes_setkey_dec(&aes,_AES_KEY,128);
  unsigned char *ob=(unsigned char*)calloc(pad,1); if(!ob){free(in);mbedtls_aes_free(&aes);return;}
  for(size_t i=0;i<pad;i+=16) mbedtls_aes_crypt_ecb(&aes,MBEDTLS_AES_DECRYPT,in+i,ob+i);
  mbedtls_aes_free(&aes);
  size_t cs=pad<sz-1?pad:sz-1; memcpy(out,ob,cs); out[cs]=0;
  free(in);free(ob);
}

#endif // BOARD_ESP32_H
