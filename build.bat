@echo off
REM اسکریپت ساخت APK در ویندوز
cd /d "%~dp0android"

if not exist local.properties (
  if defined ANDROID_HOME (
    echo sdk.dir=%ANDROID_HOME:\=/%> local.properties
  ) else if exist "%LOCALAPPDATA%\Android\Sdk" (
    echo sdk.dir=%LOCALAPPDATA:\=/%/Android/Sdk> local.properties
  )
)

if not exist gradlew (
  echo.
  echo [هشدار] فایل gradlew موجود نیست. لطفاً ابتدا یکبار پروژه را در Android Studio باز کنید
  echo        تا Gradle Wrapper به‌طور خودکار ساخته شود، سپس این اسکریپت را اجرا کنید.
  echo.
  pause
  exit /b 1
)

call gradlew.bat assembleDebug
echo.
echo APK output: app\build\outputs\apk\debug\app-debug.apk
pause
