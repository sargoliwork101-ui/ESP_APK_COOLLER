/* logic.ino — منطق مشترک ذخیره‌سازی، HTTP handlerها، checkScenarios و tickClock */

// ====================== CLOCK ======================
void tickClock(){
  while(millis()-lastTick>=1000){
    lastTick+=1000;currentSecond++;
    if(currentSecond>=60){currentSecond=0;currentMinute++;
      if(currentMinute>=60){currentMinute=0;currentHour++;
        if(currentHour>=24){currentHour=0;advanceDate();}}}
  }
}

// ====================== LOAD/SAVE ======================
static const char* SC_FILE="/scenarios.json";
static const char* WF_FILE="/wifi.json";
static const char* OV_FILE="/override.txt";
static const char* PT_FILE="/protection.json";

void loadScenarios(){
  File f=FS_OPEN_READ(SC_FILE); if(!f) return;
  DynamicJsonDocument doc(SCEN_JSON_CAP);
  if(deserializeJson(doc,f)){f.close();return;} f.close();
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int i=0;
  for(JsonObject v:arr){
    if(i>=MAX_SCENARIOS) break;
    bool a=v["active"]|false;
    scenarios[i].active=a;
    scenarios[i].enabled=v.containsKey("en")?(bool)v["en"]:a;
    scenarios[i].startHour=v["sh"]|0;scenarios[i].startMinute=v["sm"]|0;
    scenarios[i].endHour=v["eh"]|0;scenarios[i].endMinute=v["em"]|0;
    scenarios[i].weekdays=v.containsKey("wd")?(uint8_t)(v["wd"]|0x7F):0x7F;
    i++;
  }
}
void saveScenarios(){
  DynamicJsonDocument doc(SCEN_JSON_CAP);
  JsonObject root=doc.to<JsonObject>(); root["version"]=2;
  JsonArray arr=root.createNestedArray("items");
  for(int i=0;i<MAX_SCENARIOS;i++){
    JsonObject o=arr.createNestedObject();
    o["active"]=scenarios[i].active; o["en"]=scenarios[i].enabled;
    o["sh"]=scenarios[i].startHour; o["sm"]=scenarios[i].startMinute;
    o["eh"]=scenarios[i].endHour; o["em"]=scenarios[i].endMinute;
    o["wd"]=scenarios[i].weekdays;
  }
  String s; serializeJson(doc,s);
  if(!writeFileString(SC_FILE,s)) FS_REMOVE(SC_FILE);
}

void loadWifi(){
  File f=FS_OPEN_READ(WF_FILE); if(!f) return;
  StaticJsonDocument<1024> doc;
  if(deserializeJson(doc,f)){f.close();return;} f.close();
  if(doc.containsKey("ssid")){strncpy(custom_ssid,doc["ssid"]|custom_ssid,31);custom_ssid[31]=0;}
  if(doc.containsKey("pass")){loadAndDecryptPassword(doc["pass"],custom_password,32);}
  if(doc.containsKey("sta_ssid")){strncpy(sta_ssid,doc["sta_ssid"]|sta_ssid,31);sta_ssid[31]=0;}
  if(doc.containsKey("sta_pass")){loadAndDecryptPassword(doc["sta_pass"],sta_password,64);}
  if(doc.containsKey("internet")) internet_enabled=doc["internet"]|true;
  if(doc.containsKey("staEnabled")&&!doc.containsKey("staOnMinutes")&&!doc.containsKey("staOffMinutes")){
    if(!(doc["staEnabled"]|true)) internet_enabled=false;
  }
  if(doc.containsKey("staOnMinutes")) staOnMinutes=constrain((int)(doc["staOnMinutes"]|staOnMinutes),MIN_STA_ON_MIN,MAX_STA_CYCLE_MIN);
  if(doc.containsKey("staOffMinutes"))staOffMinutes=constrain((int)(doc["staOffMinutes"]|staOffMinutes),MIN_STA_OFF_MIN,MAX_STA_CYCLE_MIN);
  if(doc.containsKey("apCycleEnabled")) apCycleEnabled=doc["apCycleEnabled"]|false;
  if(doc.containsKey("apOnMinutes"))  apOnMinutes=constrain((int)(doc["apOnMinutes"]|apOnMinutes),MIN_AP_CYCLE_MIN,MAX_AP_CYCLE_MIN);
  if(doc.containsKey("apOffMinutes")) apOffMinutes=constrain((int)(doc["apOffMinutes"]|apOffMinutes),MIN_AP_CYCLE_MIN,MAX_AP_CYCLE_MIN);
  if(doc.containsKey("apTxPowerLevel")) apTxPowerLevel=constrain((int)(doc["apTxPowerLevel"]|apTxPowerLevel),0,MAX_TX_POWER);
}
void saveWifi(){
  StaticJsonDocument<1024> doc;
  doc["version"]=5; doc["ssid"]=custom_ssid; doc["pass"]=encryptPassword(custom_password,32);
  doc["sta_ssid"]=sta_ssid; doc["sta_pass"]=encryptPassword(sta_password,64);
  doc["internet"]=internet_enabled;
  doc["staOnMinutes"]=staOnMinutes; doc["staOffMinutes"]=staOffMinutes;
  doc["apCycleEnabled"]=apCycleEnabled; doc["apOnMinutes"]=apOnMinutes;
  doc["apOffMinutes"]=apOffMinutes; doc["apTxPowerLevel"]=apTxPowerLevel;
  String s; serializeJson(doc,s);
  if(!writeFileString(WF_FILE,s)) FS_REMOVE(WF_FILE);
}

void loadOverride(){
  manual_override=0; if(!FS_EXISTS(OV_FILE)) return;
  String v=readFileString(OV_FILE); if(v.length()==0) return;
  manual_override = v.startsWith("V") ? v.substring(v.indexOf(':')+1).toInt() : v.toInt();
  manual_override = (manual_override==1)?1:0;
}
void saveOverride(){ writeFileString(OV_FILE,String("V2:")+String(manual_override)); }

void loadProt(){
  File f=FS_OPEN_READ(PT_FILE); if(!f) return;
  StaticJsonDocument<128> doc; if(deserializeJson(doc,f)){f.close();return;} f.close();
  if(doc.containsKey("minOffMinutes")) antiShortCycleMinutes=constrain((int)(doc["minOffMinutes"]|3),0,MAX_ANTI_SHORT_CYCLE_MIN);
}
void saveProt(){
  StaticJsonDocument<128> doc; doc["version"]=1; doc["minOffMinutes"]=antiShortCycleMinutes;
  String s; serializeJson(doc,s);
  if(!writeFileString(PT_FILE,s)) FS_REMOVE(PT_FILE);
}

void saveTime(){
  timeSeq++; timeSlot=1-timeSlot;
  char b[80]; snprintf(b,sizeof(b),"V2:%d:%d:%d:%d:%lu:%d:%d:%d:%d",
    currentHour,currentMinute,currentSecond,time_synchronized?1:0,timeSeq,
    currentYear,currentMonth,currentDay,currentWeekday);
  if(!writeFileString(TIME_FILES[timeSlot],String(b))) FS_REMOVE(TIME_FILES[timeSlot]);
  lastTimeSave=millis();
}
bool readTime(const char*p,int&h,int&m,int&s,int&sy,unsigned long&sq,int&y,int&mo,int&d,int&wd){
  if(!FS_EXISTS(p)) return false;
  String v=readFileString(p); if(v.length()==0) return false;
  int th=-1,tm=-1,ts=-1,tsync=-1,ty=2026,tmon=1,td=1,twd=3,fv=1; unsigned long tseq=0;
  int pr = v.startsWith("V") ?
    sscanf(v.c_str(),"V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d",&fv,&th,&tm,&ts,&tsync,&tseq,&ty,&tmon,&td,&twd) :
    sscanf(v.c_str(),"%d:%d:%d:%d:%lu:%d:%d:%d:%d",&th,&tm,&ts,&tsync,&tseq,&ty,&tmon,&td,&twd);
  if(pr<4||th<0||th>23||tm<0||tm>59||ts<0||ts>59||(tsync!=0&&tsync!=1)) return false;
  if((fv>=2||pr>=9)&&(ty<2024||tmon<1||tmon>12||td<1||td>31||twd<0||twd>6)) return false;
  h=th;m=tm;s=ts;sy=tsync;y=ty;mo=tmon;d=td;wd=twd;
  sq=((fv>=2&&pr>=10)||(fv==1&&pr>=9))?tseq:0;
  return true;
}
void loadTime(){
  int h0,m0,s0,s0_,y0,mo0,d0,wd0; unsigned long q0;
  int h1,m1,s1,s1_,y1,mo1,d1,wd1; unsigned long q1;
  bool o0=readTime(TIME_FILES[0],h0,m0,s0,s0_,q0,y0,mo0,d0,wd0);
  bool o1=readTime(TIME_FILES[1],h1,m1,s1,s1_,q1,y1,mo1,d1,wd1);
  if(o0&&o1){if(q1>=q0)o0=false;else o1=false;}
  if(o0){currentHour=h0;currentMinute=m0;currentSecond=s0;currentYear=y0;currentMonth=mo0;currentDay=d0;currentWeekday=wd0;time_synchronized=(s0_==1&&q0>0);timeSlot=0;timeSeq=q0;}
  else if(o1){currentHour=h1;currentMinute=m1;currentSecond=s1;currentYear=y1;currentMonth=mo1;currentDay=d1;currentWeekday=wd1;time_synchronized=(s1_==1&&q1>0);timeSlot=1;timeSeq=q1;}
  else {currentHour=0;currentMinute=0;currentSecond=0;time_synchronized=false;}
}

void saveStats(){
  statSeq++; statSlot=1-statSlot;
  char b[64]; snprintf(b,sizeof(b),"V2:%lu:%lu:%lu",switchCount,totalOnSec,statSeq);
  if(!writeFileString(STAT_FILES[statSlot],String(b))) FS_REMOVE(STAT_FILES[statSlot]);
  lastStatSave=millis();
}
bool readStat(const char*p,unsigned long&sc,unsigned long&tos,unsigned long&sq){
  if(!FS_EXISTS(p)) return false;
  String v=readFileString(p); if(v.length()==0) return false;
  unsigned long tsc=0,ttos=0,tseq=0; int fv=1;
  int pr = v.startsWith("V") ? sscanf(v.c_str(),"V%d:%lu:%lu:%lu",&fv,&tsc,&ttos,&tseq) : sscanf(v.c_str(),"%lu:%lu:%lu",&tsc,&ttos,&tseq);
  if(pr!=(v.startsWith("V")?4:3)) return false;
  sc=tsc;tos=ttos;sq=tseq; return true;
}
void loadStats(){
  unsigned long sc0,tos0,sq0,sc1,tos1,sq1;
  bool o0=readStat(STAT_FILES[0],sc0,tos0,sq0);
  bool o1=readStat(STAT_FILES[1],sc1,tos1,sq1);
  if(o0&&o1){if(sq1>=sq0)o0=false;else o1=false;}
  if(o0){switchCount=sc0;totalOnSec=tos0;statSlot=0;statSaveSeq=sq0;}
  else if(o1){switchCount=sc1;totalOnSec=tos1;statSlot=1;statSaveSeq=sq1;}
  else {switchCount=0;totalOnSec=0;}
}

void loadNtpInfo(){
  ntp_last_valid=false;
  File f=FS_OPEN_READ(NTP_FILE); if(!f) return;
  StaticJsonDocument<192> doc; if(deserializeJson(doc,f)){f.close();return;} f.close();
  int y=doc["y"]|0,mo=doc["mon"]|0,d=doc["d"]|0,h=doc["h"]|-1,m=doc["m"]|-1,s=doc["s"]|-1;
  if(y<2024||mo<1||mo>12||d<1||d>31||h<0||h>23||m<0||m>59||s<0||s>59) return;
  ntp_y=y;ntp_mo=mo;ntp_d=d;ntp_h=h;ntp_mi=m;ntp_s=s;ntp_last_valid=true;ntp_ever_ok=true;
}
void saveNtpInfo(){
  if(!ntp_last_valid) return;
  StaticJsonDocument<192> doc;
  doc["version"]=1; doc["y"]=ntp_y;doc["mon"]=ntp_mo;doc["d"]=ntp_d;
  doc["h"]=ntp_h;doc["m"]=ntp_mi;doc["s"]=ntp_s;
  String s; serializeJson(doc,s);
  if(!writeFileString(NTP_FILE,s)) FS_REMOVE(NTP_FILE);
}

// ====================== WIFI/AP/STA CYCLES ======================
void setAp(bool on){
  if(on==apCurrentlyOn) return;
  if(on){WiFi.softAP(custom_ssid,custom_password,1,0,3);boardApplyTxPower(apTxPowerLevel);}
  else WiFi.softAPdisconnect(true);
  apCurrentlyOn=on;
}
void manageApCycle(){
  if(!apCycleEnabled){if(!apCurrentlyOn){setAp(true);apCycleLastToggleMillis=millis();}return;}
  if(apCurrentlyOn && WiFi.softAPgetStationNum()>0){apCycleLastToggleMillis=millis();return;}
  unsigned long onMs=(unsigned long)apOnMinutes*60000UL, offMs=(unsigned long)apOffMinutes*60000UL;
  unsigned long el=millis()-apCycleLastToggleMillis;
  if(apCurrentlyOn && el>=onMs){setAp(false);apCycleLastToggleMillis=millis();}
  else if(!apCurrentlyOn && el>=offMs){setAp(true);apCycleLastToggleMillis=millis();}
}
void setStaOn(bool on){
  if(on){staCurrentlyOn=true;staCycleLastToggleMillis=millis();staConnect();}
  else {WiFi.setAutoReconnect(false);WiFi.disconnect(false);staCurrentlyOn=false;staCycleLastToggleMillis=millis();staEverOk=false;}
}
void manageStaCycle(){
  if(!internet_enabled||strlen(sta_ssid)==0){
    if(staCurrentlyOn||WiFi.status()==WL_CONNECTED){WiFi.setAutoReconnect(false);WiFi.disconnect(false);staCurrentlyOn=false;}
    staCycleLastToggleMillis=millis(); return;
  }
  if(staOffMinutes==0){if(!staCurrentlyOn)setStaOn(true);return;}
  unsigned long onMs=(unsigned long)staOnMinutes*60000UL, offMs=(unsigned long)staOffMinutes*60000UL;
  unsigned long el=millis()-staCycleLastToggleMillis;
  if(staCurrentlyOn&&el>=onMs) setStaOn(false);
  else if(!staCurrentlyOn&&el>=offMs) setStaOn(true);
}
void staConnect(){
  if(!internet_enabled){WiFi.setAutoReconnect(false);WiFi.disconnect(false);return;}
  if(strlen(sta_ssid)==0){WiFi.setAutoReconnect(false);WiFi.disconnect(false);return;}
  WiFi.setAutoReconnect(true);
  if(strlen(sta_password)>0) WiFi.begin(sta_ssid,sta_password); else WiFi.begin(sta_ssid);
  boardConfigTime(); boardApplyTxPower(apTxPowerLevel);
}
void tryNtp(){
  unsigned long iv = ntpSyncedThisBoot ? 3600000UL : 60000UL;
  if(!internet_enabled||!staCurrentlyOn||strlen(sta_ssid)==0) return;
  if(ntpFirstPending){ntpFirstPending=false;}
  else if(millis()-lastNtpCheck<iv) return;
  if(WiFi.status()!=WL_CONNECTED) return;
  lastNtpCheck=millis();
  if(boardNtpSync(currentHour,currentMinute,currentSecond,currentYear,currentMonth,currentDay,currentWeekday)){
    lastTick=millis(); time_synchronized=true; ntpSyncedThisBoot=true; ntp_ever_ok=true;
    ntp_y=currentYear;ntp_mo=currentMonth;ntp_d=currentDay;ntp_h=currentHour;ntp_mi=currentMinute;ntp_s=currentSecond;
    ntp_last_valid=true;
    saveTime(); saveNtpInfo();
  }
}

// ====================== SCENARIO EXECUTION ======================
void checkScenarios(){
  static int lastKnown=-1;
  bool desired;
  if(manual_override==1){
    desired=true;
    if(lastKnown!=1){if(time_synchronized)saveTime();lastKnown=1;}
  } else if(!time_synchronized){
    desired=false;
    if(lastKnown!=0) lastKnown=0;
  } else {
    desired=false;
    int cur=currentHour*60+currentMinute;
    for(int i=0;i<MAX_SCENARIOS;i++){
      if(!scenarios[i].active||!scenarios[i].enabled) continue;
      int s=scenarios[i].startHour*60+scenarios[i].startMinute;
      int e=scenarios[i].endHour*60+scenarios[i].endMinute;
      uint8_t w=scenarios[i].weekdays;
      if(s==e||w==0) continue;
      int sd=currentWeekday;
      if(s>e && cur<e) sd=(currentWeekday+6)%7;
      if((w & (1<<sd))==0) continue;
      bool in = (s<e) ? (cur>=s&&cur<e) : (cur>=s||cur<e);
      if(in){desired=true;break;}
    }
    int n=desired?1:0;
    if(lastKnown!=n){saveTime();lastKnown=n;}
  }
  if(desired && antiShortCycleMinutes>0 && lastStatState==0){
    unsigned long req=(unsigned long)antiShortCycleMinutes*60000UL;
    if(millis()-lastRelayOffMillis<req) desired=false;
  }
  int nrs=desired?1:0;
  if(lastStatState!=nrs){
    if(nrs==1){switchCount++;onSince=millis();onForStats=true;}
    else {if(onForStats)totalOnSec+=(millis()-onSince)/1000UL; onForStats=false;lastRelayOffMillis=millis();}
    lastStatState=nrs; saveStats();
  }
  setRelay(desired);
}

// ====================== HTTP HANDLERS ======================
void handleSync(){
  if(!allow(lastSync,1000)) return; cors();
  if(!(server.hasArg("h")&&server.hasArg("m")&&server.hasArg("s")&&server.hasArg("y")&&server.hasArg("mon")&&server.hasArg("d")&&server.hasArg("wd"))){server.send(400,"text/plain","Bad Request");return;}
  int h=server.arg("h").toInt(),m=server.arg("m").toInt(),s=server.arg("s").toInt();
  int y=server.arg("y").toInt(),mo=server.arg("mon").toInt(),d=server.arg("d").toInt(),wd=server.arg("wd").toInt();
  if(h<0||h>23||m<0||m>59||s<0||s>59||y<2024||mo<1||mo>12||d<1||d>31||wd<0||wd>6){server.send(400,"text/plain","Invalid Time");return;}
  currentHour=h;currentMinute=m;currentSecond=s;currentYear=y;currentMonth=mo;currentDay=d;currentWeekday=wd;
  lastTick=millis();time_synchronized=true;saveTime();server.send(200,"text/plain","OK");
}
void handleToggleMan(){
  if(!allow(lastToggleMan,1500)) return; cors();
  if(manual_override==0){manual_override=1;saveOverride();if(time_synchronized)saveTime();delay(100);checkScenarios();}
  else {manual_override=0;saveOverride();if(time_synchronized)saveTime();delay(100);checkScenarios();}
  server.send(200,"text/plain","OK");
}
void handleSaveAp(){
  if(!allow(lastSaveAp,3000)) return; cors();
  if(!(server.hasArg("ssid")&&server.hasArg("pass"))){server.send(400,"text/plain","Bad Request");return;}
  String ns=server.arg("ssid"),np=server.arg("pass");
  if(ns.length()>0&&np.length()>=8){
    ns.toCharArray(custom_ssid,32);np.toCharArray(custom_password,32);
    saveWifi();saveTime();server.send(200,"text/plain","OK");pendingReset=true;resetMillis=millis();return;
  }
  server.send(400,"text/plain","Bad Request");
}
void handleSaveSta(){
  if(!allow(lastSaveSta,1500)) return; cors();
  String ns=server.hasArg("sta_ssid")?server.arg("sta_ssid"):"";
  String np=server.hasArg("sta_pass")?server.arg("sta_pass"):"";
  String ni=server.hasArg("internet")?server.arg("internet"):"";
  String non=server.hasArg("sta_on_minutes")?server.arg("sta_on_minutes"):"";
  String noff=server.hasArg("sta_off_minutes")?server.arg("sta_off_minutes"):"";
  if(np.length()>0&&np.length()<8){server.send(400,"text/plain","Bad Request");return;}
  if(non.length()==0||noff.length()==0){server.send(400,"text/plain","Bad Request");return;}
  for(size_t i=0;i<non.length();i++)if(!isDigit(non[i])){server.send(400,"text/plain","Bad Request");return;}
  for(size_t i=0;i<noff.length();i++)if(!isDigit(noff[i])){server.send(400,"text/plain","Bad Request");return;}
  int ov=non.toInt(),fv=noff.toInt();
  if(ov<MIN_STA_ON_MIN||ov>MAX_STA_CYCLE_MIN||fv<MIN_STA_OFF_MIN||fv>MAX_STA_CYCLE_MIN){server.send(400,"text/plain","Bad Request");return;}
  internet_enabled=(ni=="1");staOnMinutes=ov;staOffMinutes=fv;
  ns.toCharArray(sta_ssid,32);np.toCharArray(sta_password,64);
  saveWifi();
  ntpSyncedThisBoot=false;ntpFirstPending=true;lastNtpCheck=0;staEverOk=false;
  WiFi.setAutoReconnect(false);WiFi.disconnect(false);
  staCurrentlyOn=true;staCycleLastToggleMillis=millis();staConnect();
  server.send(200,"text/plain","OK");
}
void handleSaveProt(){
  if(!allow(lastSaveProt,1000)) return; cors();
  if(!server.hasArg("min_off")){server.send(400,"text/plain","Bad Request");return;}
  String v=server.arg("min_off"); if(v.length()==0){server.send(400,"text/plain","Bad Request");return;}
  for(size_t i=0;i<v.length();i++)if(!isDigit(v[i])){server.send(400,"text/plain","Bad Request");return;}
  int x=v.toInt();
  if(x<0||x>MAX_ANTI_SHORT_CYCLE_MIN){server.send(400,"text/plain","Bad Request");return;}
  antiShortCycleMinutes=x;saveProt();server.send(200,"text/plain","OK");
}
void handleSaveApCycle(){
  if(!allow(lastSaveApCycle,1000)) return; cors();
  if(!(server.hasArg("on_minutes")&&server.hasArg("off_minutes")&&server.hasArg("tx_power"))){server.send(400,"text/plain","Bad Request");return;}
  String oR=server.arg("on_minutes"),fR=server.arg("off_minutes"),pR=server.arg("tx_power"),eR=server.hasArg("cycle_enabled")?server.arg("cycle_enabled"):"";
  for(size_t i=0;i<oR.length();i++)if(!isDigit(oR[i])){server.send(400,"text/plain","Bad Request");return;}
  for(size_t i=0;i<fR.length();i++)if(!isDigit(fR[i])){server.send(400,"text/plain","Bad Request");return;}
  for(size_t i=0;i<pR.length();i++)if(!isDigit(pR[i])){server.send(400,"text/plain","Bad Request");return;}
  int on=oR.toInt(),of=fR.toInt(),pv=pR.toInt();
  if(on<MIN_AP_CYCLE_MIN||on>MAX_AP_CYCLE_MIN||of<MIN_AP_CYCLE_MIN||of>MAX_AP_CYCLE_MIN||pv<0||pv>MAX_TX_POWER){server.send(400,"text/plain","Bad Request");return;}
  apCycleEnabled=(eR=="1");apOnMinutes=on;apOffMinutes=of;apTxPowerLevel=pv;boardApplyTxPower(pv);
  apCycleLastToggleMillis=millis();if(!apCurrentlyOn)setAp(true);
  saveWifi();server.send(200,"text/plain","OK");
}
void handleSaveSc(){
  if(!allow(lastSaveSc,1200)) return; cors();
  if(!server.hasArg("plain")){server.send(400,"text/plain","Bad Request");return;}
  String b=server.arg("plain");
  DynamicJsonDocument doc(SCEN_JSON_CAP);
  DeserializationError e=deserializeJson(doc,b);
  if(e||!doc.is<JsonArray>()){server.send(400,"text/plain","Invalid JSON");return;}
  JsonArray arr=doc.as<JsonArray>();
  Scenario tmp[MAX_SCENARIOS];
  for(int j=0;j<MAX_SCENARIOS;j++){tmp[j].active=false;tmp[j].enabled=false;tmp[j].startHour=0;tmp[j].startMinute=0;tmp[j].endHour=0;tmp[j].endMinute=0;tmp[j].weekdays=0x7F;}
  int i=0;
  for(JsonObject v:arr){
    if(i>=MAX_SCENARIOS) break;
    tmp[i].active=true;tmp[i].enabled=v["en"]|true;
    tmp[i].startHour=v["sh"]|0;tmp[i].startMinute=v["sm"]|0;
    tmp[i].endHour=v["eh"]|0;tmp[i].endMinute=v["em"]|0;
    tmp[i].weekdays=v.containsKey("wd")?(uint8_t)(v["wd"]|0x7F):0x7F;
    i++;
  }
  bool ch=false;
  for(int j=0;j<MAX_SCENARIOS;j++){
    if(scenarios[j].active!=tmp[j].active||scenarios[j].enabled!=tmp[j].enabled||
       scenarios[j].startHour!=tmp[j].startHour||scenarios[j].startMinute!=tmp[j].startMinute||
       scenarios[j].endHour!=tmp[j].endHour||scenarios[j].endMinute!=tmp[j].endMinute||
       scenarios[j].weekdays!=tmp[j].weekdays){ch=true;break;}
  }
  if(ch){for(int j=0;j<MAX_SCENARIOS;j++)scenarios[j]=tmp[j];saveScenarios();}
  server.send(200,"text/plain","Updated");
}
void handleStatus(){
  if(!allow(lastStatus,150)) return; cors();
  char ts[9]; snprintf(ts,sizeof(ts),"%02d:%02d:%02d",currentHour,currentMinute,currentSecond);
  int logSt=digitalRead(RELAY_PIN)==RELAY_ACTIVE_LEVEL?1:0;
  int stac=WiFi.status()==WL_CONNECTED?1:0,stacfg=strlen(sta_ssid)>0?1:0;
  char sip[16]="";
  if(stac){IPAddress ip=WiFi.localIP();snprintf(sip,sizeof(sip),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);}
  int stState;
  if(!internet_enabled||!stacfg) stState=0;
  else if(staOffMinutes>0&&!staCurrentlyOn) stState=4;
  else if(stac){staEverOk=true;stState=2;}
  else if(staEverOk) stState=3; else stState=1;
  unsigned long liveOn=totalOnSec;
  if(onForStats) liveOn += (millis()-onSince)/1000UL;
  long pRem=0;
  if(antiShortCycleMinutes>0&&lastStatState==0){
    unsigned long req=(unsigned long)antiShortCycleMinutes*60000UL;
    unsigned long el=millis()-lastRelayOffMillis;
    if(el<req) pRem=(long)((req-el+999UL)/1000UL);
  }
  int apClients=apCurrentlyOn?WiFi.softAPgetStationNum():0;
  long apRem=0;
  if(apCycleEnabled){
    unsigned long ph=apCurrentlyOn?(unsigned long)apOnMinutes*60000UL:(unsigned long)apOffMinutes*60000UL;
    unsigned long el=millis()-apCycleLastToggleMillis;
    if(el<ph) apRem=(long)((ph-el+999UL)/1000UL);
  }
  long staRem=0;
  if(internet_enabled&&stacfg&&staOffMinutes>0){
    unsigned long ph=staCurrentlyOn?(unsigned long)staOnMinutes*60000UL:(unsigned long)staOffMinutes*60000UL;
    unsigned long el=millis()-staCycleLastToggleMillis;
    if(el<ph) staRem=(long)((ph-el+999UL)/1000UL);
  }
  char json[1400];
  snprintf(json,sizeof(json),
    "{\"time\":\"%s\",\"relay\":%d,\"override\":%d,\"sync\":%d,\"sta\":%d,\"internetEnabled\":%d,\"staConfigured\":%d,"
    "\"staState\":%d,\"staIp\":\"%s\",\"staOnMinutes\":%d,\"staOffMinutes\":%d,\"staPhaseOn\":%d,\"staRemaining\":%ld,"
    "\"ntpOk\":%d,\"ntpLastValid\":%d,\"ntpYear\":%d,\"ntpMonth\":%d,\"ntpDay\":%d,\"ntpHour\":%d,\"ntpMinute\":%d,\"ntpSecond\":%d,"
    "\"switchCount\":%lu,\"onSeconds\":%lu,\"weekday\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"protectionMinutes\":%d,\"protectionRemaining\":%ld,"
    "\"apCycleEnabled\":%d,\"apOn\":%d,\"apOnMinutes\":%d,\"apOffMinutes\":%d,\"apTxPowerLevel\":%d,\"apRemaining\":%ld,\"apClientConnected\":%d}",
    ts,logSt,manual_override,time_synchronized?1:0,stac,internet_enabled?1:0,stacfg,stState,sip,
    staOnMinutes,staOffMinutes,staCurrentlyOn?1:0,staRem,
    ntp_ever_ok?1:0,ntp_last_valid?1:0,ntp_y,ntp_mo,ntp_d,ntp_h,ntp_mi,ntp_s,
    switchCount,liveOn,currentWeekday,currentYear,currentMonth,currentDay,antiShortCycleMinutes,pRem,
    apCycleEnabled?1:0,apCurrentlyOn?1:0,apOnMinutes,apOffMinutes,apTxPowerLevel,apRem,(apClients>0)?1:0);
  server.send(200,"application/json",json);
}
void handleConfig(){
  if(!allow(lastConfig,1000)) return; cors();
  StaticJsonDocument<512> doc;
  doc["apSsid"]=custom_ssid; doc["staSsid"]=sta_ssid;
  doc["internetEnabled"]=internet_enabled;
  doc["staOnMinutes"]=staOnMinutes; doc["staOffMinutes"]=staOffMinutes;
  doc["apCycleEnabled"]=apCycleEnabled; doc["apOnMinutes"]=apOnMinutes;
  doc["apOffMinutes"]=apOffMinutes; doc["apTxPowerLevel"]=apTxPowerLevel;
  doc["minOffMinutes"]=antiShortCycleMinutes;
  doc["deviceName"]=PROGRAM_NAME; doc["deviceTag"]=BOARD_TAGLINE_STR; doc["board"]=BOARD_NAME_STR;
  String s; serializeJson(doc,s); server.send(200,"application/json",s);
}
void handleScList(){
  if(!allow(lastScList,1000)) return; cors();
  DynamicJsonDocument doc(SCEN_JSON_CAP);
  JsonArray arr=doc.to<JsonArray>();
  for(int i=0;i<MAX_SCENARIOS;i++){
    if(!scenarios[i].active) continue;
    JsonObject o=arr.createNestedObject();
    o["sh"]=scenarios[i].startHour;o["sm"]=scenarios[i].startMinute;
    o["eh"]=scenarios[i].endHour;o["em"]=scenarios[i].endMinute;
    o["en"]=scenarios[i].enabled;o["wd"]=scenarios[i].weekdays;
  }
  String s; serializeJson(doc,s); server.send(200,"application/json",s);
}
