# کولر هوشمند ESP — اپ اندروید + فیرم‌ویر سبک چندبردی

این پروژه شامل سه بخش است:

1. **اپ اندروید (Native WebView)** — ظاهر و منطق دقیقاً مطابق پنل اصلی، اما تمام HTML/CSS/JS
   درون اپ قرار می‌گیرد (نه روی فلش ESP).
2. **وب (PWA-ready)** — همان فایل‌های `web/` که می‌توانند روی هر هاست استاتیک هم اجرا شوند.
3. **فیرم‌ویر `Cooler_LightAPI`** — برای ESP32 **و** ESP8266 (از جمله ESP-01). فقط API
   JSON می‌دهد و هیچ HTML سنگینی را سرو نمی‌کند. انتخاب کتابخانه‌ها (mbedtls یا AES
   نرم‌افزاری، LittleFS یا SPIFFS، WDT و ...) به‌صورت خودکار بر اساس برد انتخاب می‌شود.

---

## ساختار پوشه

```
CoolerApp/
├── README.md
├── build.sh / build.bat       # اسکریپت‌های ساخت APK
├── android/                    # پروژه Android Studio
│   ├── build.gradle
│   ├── settings.gradle
│   ├── gradle/wrapper/...
│   ├── local.properties.example
│   └── app/
│       ├── build.gradle        # وابستگی‌ها + تسک خودکار کپی web/* به assets
│       ├── proguard-rules.pro
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── assets/web/…    (خودکار از web/ کپی می‌شود)
│           ├── java/ir/cooler/smart/
│           │   ├── MainActivity.java
│           │   └── AndroidBridge.java  # پل بومی HTTP (رفع CORS/cleartext)
│           └── res/…
├── web/                        # UI پنل (همان ظاهر اصلی)
│   ├── index.html
│   ├── app.js
│   ├── manifest.json
│   └── fonts/Vazirmatn-*.woff2  # فونت محلی برای حالت AP آفلاین
└── firmware/
    └── Cooler_LightAPI/         # فیرم‌ویر چندبردی
        ├── Cooler_LightAPI.ino
        ├── logic.ino            # منطق load/save / endpointها / checkScenarios
        ├── helpers.h            # توابع کمکی فایل + پیش‌اعلان‌ها
        ├── board.h              # انتخاب خودکار هدر برد
        ├── board_esp32.h        # پیکربندی ESP32 (با mbedtls بومی)
        ├── board_esp8266.h      # پیکربندی ESP8266 (LittleFS + AES نرم‌افزاری)
        ├── soft_aes.h           # پیاده‌سازی نرم‌افزاری AES-128-ECB (فقط ESP8266)
        ├── platformio.ini
        └── README.md
```

---

## گام ۱: فلش فیرم‌ویر

پوشه‌ی `firmware/Cooler_LightAPI/` را در **Arduino IDE** باز کنید:

1. برد مورد نظر را از منوی **Tools → Board** انتخاب کنید:
   - برای ESP32: `ESP32 Dev Module`
   - برای ESP8266-01: `Generic ESP8266 Module` (Flash Size ≥ 1MB)
   - برای NodeMCU/Wemos/D1 Mini: برد متناظر.
2. کتابخانه **ArduinoJson v6.x** را نصب کنید.
3. فایل `Cooler_LightAPI.ino` را روی برد فلش کنید.
4. تنظیمات قبلی شما (سناریوها، SSIDها، محافظت، ساعت و ...) از حافظه فلش خوانده می‌شوند
   و **هیچ‌کدام پاک نمی‌شوند**.

> پین رله پیش‌فرض برای ESP32، **GPIO23** و برای ESP8266-01 **GPIO2** است. برای تغییر پین
> به `README.md` داخل پوشه firmware مراجعه کنید.

> ⚠️ اگر در زمان فلش نسخه‌ی قبلی (HTML-serving) را روی برد داشته‌اید، کماکان پس از
> فلش این فیرم‌ویر همه‌ی فایل‌های LittleFS/SPIFFS حفظ و خوانده می‌شوند.

---

## گام ۲: ساخت APK

### روش ساده (پیشنهادی): Android Studio
1. پوشه‌ی `android/` را با **Android Studio** باز کنید.
2. چند ثانیه صبر کنید Gradle Sync تمام شود (اینترنت لازم است).
3. از منوی **Build → Build Bundle(s)/APK(s) → Build APK(s)** خروجی بگیرید.
4. APK در مسیر زیر ساخته می‌شود:
   ```
   android/app/build/outputs/apk/debug/app-debug.apk
   ```

### روش خط فرمان
- لینوکس/مک: `./build.sh`
- ویندوز: `build.bat`

> تسک Gradle به نام `copyWebAssets` قبل از هر بیلد، آخرین نسخه‌ی `web/` را به‌صورت خودکار
> به `android/app/src/main/assets/web/` کپی می‌کند — نیازی به کپی دستی نیست.

---

## گام ۳: نصب و استفاده

1. APK را روی گوشی نصب کنید (اجازه نصب از منابع ناشناس).
2. گوشی را به یکی از دو روش زیر به برد وصل کنید:
   - **مستقیم**: به وای‌فای `ESP32_Timer_Hub` (یا SSID که خودتان تنظیم کرده‌اید) وصل شوید.
   - **از طریق مودم**: هر دو گوشی و برد به یک مودم/روتر وصل باشند.
3. اپ را باز کنید. پیش‌فرض به `192.168.4.1` (IP استاندارد ESP در حالت AP) وصل می‌شود.
4. اگر از مودم استفاده می‌کنید یا IP برد عوض شده، از دکمه **چرخ‌دنده** در بالای صفحه
   (سمت چپ نشانگر اتصال)، آدرس IP جدید برد را وارد و ذخیره کنید.

> ظاهر و تمام منطق (سناریوها، تداخل‌یاب، چرخه AP/STA، محافظ کمپرسور، NTP، پشتیبان‌گیری/بازیابی،
> تاست‌ها، مودال پیشرفت و …) دقیقاً مثل نسخه‌ی اصلی است. تغییر فقط این است که دیگر فایل‌های
> سنگین HTML از روی فلش ESP سرو نمی‌شوند.

---

## پشتیبانی بردها

| برد | هدر برد | فایل‌سیستم | AES |
|-----|---------|-----------|-----|
| ESP32 WROOM/WROVER | `board_esp32.h` | LittleFS | mbedtls سخت‌افزاری (هسته) |
| ESP8266-01 | `board_esp8266.h` | LittleFS (SPIFFS fallback) | `soft_aes.h` (نرم‌افزاری) |
| ESP8266 ESP-12 / NodeMCU | `board_esp8266.h` | LittleFS | `soft_aes.h` |
| Wemos D1 Mini | `board_esp8266.h` | LittleFS | `soft_aes.h` |

**افزودن برد جدید** (مثل ESP32-C3/S2/S3، RP2040 و …): یک فایل `board_xxxx.h` بسازید که
واسط‌های معرفی‌شده در انتهای کامنت‌های `board.h` را پیاده کند و آن را در `#elif` های
فایل `board.h` اضافه کنید.

---

## APIهای فعال روی برد (همه‌ی بردها)

| Method | Path | کاربرد |
|--------|------|--------|
| GET  | `/`               | صفحه کوتاه راهنما |
| GET  | `/status`         | وضعیت زنده JSON (همان فرمت اصلی) |
| GET  | `/config`         | تنظیمات (بدون پسورد) |
| GET  | `/scenarios`      | سناریوهای فعال |
| POST | `/toggle-manual`  | تغییر حالت دستی |
| POST | `/sync`           | همگام‌سازی ساعت |
| POST | `/save`           | ذخیره سناریوها (JSON body) |
| POST | `/save-ap`        | ذخیره SSID/رمز AP |
| POST | `/save-sta`       | ذخیره STA/اینترنت |
| POST | `/save-ap-cycle`  | ذخیره چرخه AP/قدرت سیگنال |
| POST | `/save-protection`| ذخیره محافظت کمپرسور |

همه‌ی پاسخ‌ها هدر `Access-Control-Allow-Origin: *` دارند (هم اپ هم PWA راحت کار کنند).
