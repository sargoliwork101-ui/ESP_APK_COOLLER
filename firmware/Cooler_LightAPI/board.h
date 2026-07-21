/* =========================================================================
 *  board.h  —  انتخاب خودکار هدر برد بر اساس ARDUINO_ARCH
 *  این فایل «هماهنگ‌کننده» است؛ فقط شامل می‌کند:
 *    • board_esp32.h   وقتی برد ESP32 انتخاب شده
 *    • board_esp8266.h وقتی برد ESP8266 انتخاب شده
 * ========================================================================= */
#ifndef BOARD_H
#define BOARD_H

#if defined(ARDUINO_ARCH_ESP32)
  #include "board_esp32.h"
#elif defined(ARDUINO_ARCH_ESP8266)
  #include "board_esp8266.h"
#else
  #error "⚠️ این میکروکنترلر هنوز پشتیبانی نمی‌شود. فایل board_xxxx.h مربوطه را بسازید."
#endif

// هر هدر برد باید این موارد را تعریف کرده باشد:
//   BOARD_NAME_STR , BOARD_TAGLINE_STR
//   RELAY_PIN (اگر override نشده باشد مقدار پیش‌فرض)
//   RELAY_ACTIVE_LEVEL (اختیاری، پیش‌فرض HIGH)
//   WebServer_t (typedef برای WebServer یا ESP8266WebServer)
//   FS_INIT/EXISTS/OPEN_READ/OPEN_WRITE/REMOVE
//   SETUP_WDT / FEED_WDT
//   boardApplyTxPower(int)
//   boardConfigTime()
//   boardNtpSync(h,m,s,y,mo,d,wd) -> bool
//   FREE_HEAP() / RESTART() / SERIAL_BEGIN()
//   encryptPassword() / loadAndDecryptPassword()

#ifndef RESTART
  #define RESTART()        ESP.restart()
#endif
#ifndef SERIAL_BEGIN
  #define SERIAL_BEGIN(b)  Serial.begin(b)
#endif

#endif // BOARD_H
