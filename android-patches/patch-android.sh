#!/usr/bin/env bash
# این اسکریپت بعد از "npx cap add android" اجرا می‌شود تا اجازه‌ی ارتباط HTTP ساده
# با برد ESP32 را به پروژه‌ی اندروید تازه‌ساخته‌شده اضافه کند.
set -e

MANIFEST="android/app/src/main/AndroidManifest.xml"
XML_DIR="android/app/src/main/res/xml"

echo "Patching Android project for plain-HTTP (cleartext) access to the ESP32..."

mkdir -p "$XML_DIR"
cp android-patches/network_security_config.xml "$XML_DIR/network_security_config.xml"

if ! grep -q "networkSecurityConfig" "$MANIFEST"; then
  sed -i 's|<application|<application android:usesCleartextTraffic="true" android:networkSecurityConfig="@xml/network_security_config"|' "$MANIFEST"
  echo "AndroidManifest.xml patched."
else
  echo "AndroidManifest.xml already patched, skipping."
fi
