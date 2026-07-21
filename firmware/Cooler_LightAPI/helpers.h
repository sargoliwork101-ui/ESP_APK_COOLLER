/* =========================================================================
 *  helpers.h — توابع کمکی خواندن/نوشتن فایل، مشترک بین هر دو برد
 * ========================================================================= */
#ifndef HELPERS_H
#define HELPERS_H

#include <Arduino.h>

static String readFileString(const char* p){
  if(!FS_EXISTS(p)) return String();
  File f=FS_OPEN_READ(p); if(!f) return String();
  String v=f.readString(); f.close(); return v;
}
static bool writeFileString(const char* p, const String &s){
  File f=FS_OPEN_WRITE(p); if(!f) return false;
  size_t n=f.print(s); f.close();
  if(n==0){FS_REMOVE(p); return false;}
  return true;
}

// --- پیش‌اعلان‌ها تا در logic.ino در دسترس باشند ---
void loadScenarios(); void saveScenarios();
void loadWifi(); void saveWifi();
void loadOverride(); void saveOverride();
void loadProt(); void saveProt();
void loadTime(); void saveTime();
bool readTime(const char*,int&,int&,int&,int&,unsigned long&,int&,int&,int&,int&);
void loadStats(); void saveStats();
bool readStat(const char*,unsigned long&,unsigned long&,unsigned long&);
void loadNtpInfo(); void saveNtpInfo();
void handleSaveSc(); void handleSync(); void handleStatus();
void handleToggleMan(); void handleSaveAp(); void handleSaveSta();
void handleSaveProt(); void handleSaveApCycle();
void handleConfig(); void handleScList();
void checkScenarios(); void tickClock();
void setAp(bool); void manageApCycle();
void setStaOn(bool); void manageStaCycle();
void staConnect(); void tryNtp();

#endif
