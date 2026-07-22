#include <ESP8266WiFi.h>
#include <LittleFS.h>

/* ========================================================
   ESP8266-01 MEMORY ERASER (Factory Reset Tool)
   - Completely standalone program
   - Erases all LittleFS data (including encrypted files)
   - Use this when the board is not connecting due to encryption issues
   - Upload this sketch, let it run once, then upload the main ESP8266 firmware again
   ======================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP8266 MEMORY ERASER ===");
  Serial.println("Starting LittleFS format...");
  
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed!");
    return;
  }
  
  Serial.println("Formatting LittleFS (this will erase ALL data)...");
  LittleFS.format();
  
  Serial.println("Format completed successfully!");
  Serial.println("All files (wifi.json, time.json, scenarios.json, etc.) have been erased.");
  Serial.println("You can now upload the main ESP8266 Cooler firmware again.");
  Serial.println("=== ERASER FINISHED ===");
}

void loop() {
  // Do nothing after erasing
  delay(10000);
}