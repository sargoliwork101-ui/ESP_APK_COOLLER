#!/usr/bin/env bash
# اسکریپت ساخت APK در محیط لینوکس/مک (نیاز به JDK 11+ و Android SDK دارد)
set -e
cd "$(dirname "$0")/android"

if [ ! -f "local.properties" ]; then
  if [ -n "$ANDROID_HOME" ]; then
    echo "sdk.dir=$ANDROID_HOME" > local.properties
  elif [ -d "$HOME/Android/Sdk" ]; then
    echo "sdk.dir=$HOME/Android/Sdk" > local.properties
  fi
fi

if [ ! -f "./gradlew" ]; then
  echo "در حال ساخت Gradle Wrapper (نیاز به نصب بودن gradle روی سیستم)..."
  gradle wrapper --gradle-version 8.4
fi

./gradlew assembleDebug
echo "✅ APK ساخته شد: android/app/build/outputs/apk/debug/app-debug.apk"
