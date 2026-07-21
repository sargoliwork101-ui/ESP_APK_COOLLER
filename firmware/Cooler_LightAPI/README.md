# فیرم‌ویر Cooler Light-API (چندبردی)

این پوشه یک فیرم‌ویر **سبک** برای کنترل کولر هوشمند است:
- **HTML/CSS/JS حذف شده** (UI روی اپ موبایل / PWA است).
- فقط **JSON REST API** سرو می‌شود (همان endpoint های نسخه‌ی اصلی + دو مورد جدید `/config` و `/scenarios`).
- به‌صورت **خودکار** روی هر دو برد **ESP32** و **ESP8266** (از جمله ESP-01) کامپایل می‌شود.
- **همه‌ی تنظیمات قبلی (LittleFS/SPIFFS)** بدون تغییر پشتیبانی می‌شوند (سازگاری کامل با فایل‌های فلش نسخه‌ی اصلی).

## فایل‌ها

| فایل | توضیح |
|------|------|
| `Cooler_LightAPI.ino` | فایل اصلی؛ منطق مشترک برنامه (setup/loop)، ثابت‌ها، helperها. |
| `logic.ino`        | منطق load/save، HTTP handler ها، checkScenarios، NTP، چرخه AP/STA. |
| `helpers.h`        | پیش‌اعلان‌ها و توابع کمکی فایل. |
| `board.h`          | انتخاب‌کننده‌ی هدر مناسب بر اساس آرشیتکتور (`#include board_xxxx.h`). |
| `board_esp32.h`    | پیکربندی مخصوص ESP32 (LittleFS, mbedtls سخت‌افزاری، WDT، Tx-power، NTP). |
| `board_esp8266.h`  | پیکربندی مخصوص ESP8266 (ESP-01/ESP-12/NodeMCU/D1 mini؛ از LittleFS و AES نرم‌افزاری استفاده می‌کند). |
| `soft_aes.h`       | **پیاده‌سازی نرم‌افزاری AES-128-ECB** (فقط وقتی کامپایل می‌شود که `board_esp8266.h` include شود). ESP32 همچنان از mbedtls بومی خودش استفاده می‌کند. |
| `platformio.ini`   | تنظیمات آماده برای PlatformIO (محیط‌های جدا برای ESP32 / ESP8266 / ESP-01). |

## پین‌های پیش‌فرض رله

| برد | پین رله | سطح فعال |
|-----|---------|---------|
| ESP32 WROOM/WROVER  | **GPIO23** | HIGH |
| ESP8266-01          | **GPIO2**  | HIGH |
| ESP8266 ESP-12 / NodeMCU / D1 mini | **GPIO4 (D2)** | HIGH |

برای تغییر، در انتهای فایل `board_xxxx` مربوطه خط‌های زیر را اضافه کنید:
```cpp
#undef  RELAY_PIN
#define RELAY_PIN X          // عدد GPIO مورد نظر
#undef  RELAY_ACTIVE_LEVEL
#define RELAY_ACTIVE_LEVEL LOW   // اگر رله‌ی شما active-low است
```

## فلش با Arduino IDE

1. بورد پشتیبان را نصب کنید:
   - **ESP32**: Boards Manager → *esp32 by Espressif Systems*
   - **ESP8266**: Boards Manager → *esp8266 by ESP8266 Community*
2. کتابخانه **ArduinoJson** نسخه 6.x را نصب کنید.
3. پوشه‌ی `Cooler_LightAPI/` را باز کنید (فایل `.ino`).
4. برد متناظر را انتخاب کنید:
   - ESP32 → `ESP32 Dev Module`
   - ESP-01 → `Generic ESP8266 Module` (Flash Size: `1MB (FS:64KB)` یا بیشتر)
   - NodeMCU → `NodeMCU 1.0 (ESP-12E Module)`
   - D1 Mini → `LOLIN(WEMOS) D1 R2 & mini`
5. روی برد فلش کنید.

> ⚠️ برای **ESP-01**: رله را فقط به **GPIO2** وصل کنید (GPIO0 در بوت باید HIGH باشد).
> بعد از پروگرم، قبل از بوت عادی پایه‌های GPIO0/GPIO2 را از پروگرمر جدا و آزاد بگذارید.

## فلش با PlatformIO

```bash
cd firmware/Cooler_LightAPI
# ESP32 Dev Module:
pio run -e esp32dev -t upload
# ESP8266-01:
pio run -e esp01_1m -t upload
# NodeMCU:
pio run -e esp12e -t upload
# D1 mini:
pio run -e d1_mini -t upload
```

## APIها

| Method | Path | توضیح |
|--------|------|-------|
| GET  | `/`            | صفحه کوتاه راهنما |
| GET  | `/status`      | وضعیت زنده (JSON، همان فرمت نسخه اصلی) |
| GET  | `/config`      | تنظیمات (بدون پسورد) برای پر کردن خودکار فرم |
| GET  | `/scenarios`   | لیست سناریوهای فعال |
| POST | `/toggle-manual` | تغییر حالت دستی |
| POST | `/sync?h=..&m=..&s=..&y=..&mon=..&d=..&wd=..` | همگام‌سازی ساعت |
| POST | `/save`        | ذخیره سناریوها (بدنه JSON) |
| POST | `/save-ap`     | ذخیره SSID/رمز AP (با ری‌استارت) |
| POST | `/save-sta`    | ذخیره STA / اینترنت |
| POST | `/save-ap-cycle` | ذخیره چرخه AP / قدرت |
| POST | `/save-protection` | ذخیره محافظ کمپرسور |

همه پاسخ‌ها `Access-Control-Allow-Origin: *` دارند.
