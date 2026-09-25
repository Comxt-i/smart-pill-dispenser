#include <cassert>
#include <vector>
#include <string>
#include <ESP32Servo.h>
#include "../ESP32_Main/buttons.cpp"
#include "../ESP32_Main/pill_app.cpp"
SerialClass Serial;
WiFiClass WiFi;
std::vector<Pulse> pulses;
int attachedCount = 0;
bool failAttach = false;
uint32_t nowMs = 1000;
int levels[64];
bool inSetup = false;
int networkCalls = 0, reservations = 0;
std::vector<PendingEvent> savedEvents;
std::vector<std::string> reservedKeys;
const char *rejectKey = "";
unsigned long millis() { return nowMs; }
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return levels[pin]; }
void digitalWrite(uint8_t pin, int value) { (void)pin; (void)value; }
void analogWrite(uint8_t pin, int value) { (void)pin; (void)value; }
// ไฟสถานะไม่มีผลต่อพฤติกรรมที่เทสต์นี้ตรวจ จึงกลืนทิ้ง
// แต่ต้องมี ไม่อย่างนั้นลิงก์ไม่ผ่านเพราะ pill_app.cpp เรียกใช้จริง
void statusLedBegin() {}
void statusLedFlash(LedColor) {}
void statusLedSet(LedPattern) {}
void statusLedUpdate() {}
void alertSet(AlertPattern) {}
void alertOneShot(AlertPattern) {}
void alertUpdate() {}
void lcdShowMessage(const char*, const char*) { assert(!dispenserIsBusy()); }
void lcdMedicineTick() { assert(!dispenserIsBusy()); }
void lcdSetMedicineScreen(const char *const*, uint8_t, const char *const*, uint8_t) { assert(!dispenserIsBusy()); }
bool rtcIsValid() { return true; }
// หน้าจอรายงานผลสแกน I2C ตอนบูตเรียกสามตัวนี้
// Reached only from appBegin()/appStatusJson(), which this test never calls.
// --gc-sections drops them on ELF, but MinGW/PE keeps every function, so without
// these the test cannot link on Windows at all.
void alertBegin() {}
void eventQueueBegin() {}
void netSyncBegin() {}
bool netSyncLastCallOk() { return true; }
const char *netSyncLastError() { return ""; }
const char *netSyncConfigVersion() { return ""; }
void rtcLcdBegin() {}
I2cLineState i2cLastLineState() { return I2cLineState{}; }
const char *rtcClockSource() { return "RTC"; }
void wifiWebBegin() {}
void delay(unsigned long ms) { nowMs += ms; }
// หน้าจอตั้งค่าและหน้าวินิจฉัยตอนบูตเรียกสามตัวนี้
const char *wifiSetupStatusShort() { return "OPEN"; }
bool wifiHasSavedNetwork() { return true; }
const char *wifiSavedSsid() { return "Home"; }
bool rtcIsPresent() { return true; }
bool rtcOscillatorHalted() { return false; }
uint8_t i2cFoundCount() { return 0; }
uint8_t i2cFoundAddress(uint8_t) { return 0; }
uint32_t rtcLocalEpoch() { return 1800000000 + nowMs/1000; }
uint32_t rtcDayKey() { return 20260925; }
int rtcMinutesOfDay() { return 600; }
void rtcLcdUpdate() { assert(!dispenserIsBusy()); }
bool wifiSetupActive() { return inSetup; }
bool wifiStartSetup() { inSetup = true; return true; }
void wifiWebLoop() { assert(!dispenserIsBusy()); ++networkCalls; }
const char *wifiSetupSsid() { return "test"; }
const char *wifiSetupPassword() { return "test-only"; }
void netSyncRequestNow() {}
bool netSyncDue() { return true; }
bool netSyncFetch() { assert(!dispenserIsBusy()); ++networkCalls; return true; }
bool netSyncFlushEvents() { assert(!dispenserIsBusy()); ++networkCalls; return true; }
bool netSyncTakeCommand(RemoteCommand&) { return false; }
uint8_t eventQueueSize() { return static_cast<uint8_t>(savedEvents.size()); }
void eventQueueMakeId(char *out, size_t size) {
  assert(!dispenserIsBusy()); snprintf(out, size, "evt-%zu", savedEvents.size());
}
bool eventQueuePush(const PendingEvent &event) {
  assert(!dispenserIsBusy()); savedEvents.push_back(event); return true;
}
void commandJournalDoseKey(char *out, size_t size, const char *id, uint32_t, bool) { snprintf(out,size,"%s",id); }
bool commandJournalReserve(const char *key, uint32_t) {
  assert(!dispenserIsBusy()); ++reservations;
  if (strcmp(key, rejectKey) == 0) return false;
  for (const auto &id : reservedKeys) if (id == key) return false;
  reservedKeys.emplace_back(key); return true;
}
void tick(uint32_t ms = 1) { nowMs += ms; appLoop(); }
void press(uint8_t pin) { levels[pin]=LOW; tick(); tick(BUTTON_DEBOUNCE_MS); }
void release(uint8_t pin) { levels[pin]=HIGH; tick(); tick(BUTTON_DEBOUNCE_MS); }
void drop(uint8_t slot) {
  levels[PILL_SENSOR_PINS[slot-1]]=LOW; tick();
  levels[PILL_SENSOR_PINS[slot-1]]=HIGH; tick();
}
size_t pulseCount(uint8_t slot) {
  size_t n=0; for (const auto &p:pulses) if (p.pin==SERVO_PINS[slot-1]) ++n; return n;
}
Dose &dose(int index) { return *scheduleDoseAt({static_cast<uint8_t>(index),0,true}); }
void resetCase() {
  for (int &v:levels) v=HIGH;
  dispenserControlBegin(); buttonsBegin(); scheduleBegin(onDoseStateChanged);
  pulses.clear(); savedEvents.clear(); reservedKeys.clear();
  reservations=networkCalls=0; rejectKey="";
  setupButton={}; inSetup=false; pendingDoseCount=deferredEventCount=0;
  cancelPressActive=false; clockWasValid=true; firstTickAfterClock=false;
  for (auto &owner:runOwner) owner=RunOwner::None;
  scheduleBeginSync();
  for (int i=0;i<3;++i) {
    char id[20]; snprintf(id,sizeof(id),"dose-%d",i+1);
    int slot=scheduleStageSlot(i+1,true,"test-med","Test",1);
    scheduleStageDose(slot,id,"Morning",600,false);
  }
  scheduleCommitSync();
  activeAlert=scheduleTick(600,rtcDayKey(),false);
}
int main() {
  // What users actually do at dose time: press green and HOLD it, waiting for the box.
  // After 3 s that used to open Wi-Fi setup, which silenced the alert, skipped the dose
  // and left the LCD on the setup screen. During an alert the 3 s hold must accept the
  // round instead, and the release afterwards must not act a second time.
  resetCase();
  levels[DISPENSE_BUTTON_PIN]=LOW; tick(); tick(BUTTON_DEBOUNCE_MS);
  tick(500);
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Alerting);  // short so far: unchanged
  for (int i=0;i<30;++i) tick(100);  // keep holding past 3 s
  for (int i=0;i<3;++i) assert(dose(i).state!=DoseState::Alerting);
  assert(!inSetup);
  const int acceptedReservations=reservations;
  release(DISPENSE_BUTTON_PIN);
  assert(!inSetup && reservations==acceptedReservations);

  // Control: with no alert the same 3 s hold must still open setup, or the case above
  // would pass even if "alerting" were stuck true. (Dry run finishes the round at once;
  // with real motors the channels stay busy and the hold is correctly refused.)
  if (!ENABLE_SERVO_MOVEMENT) {
    resetCase(); press(DISPENSE_BUTTON_PIN); release(DISPENSE_BUTTON_PIN);
    for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Done);
    levels[DISPENSE_BUTTON_PIN]=LOW; tick(); tick(BUTTON_DEBOUNCE_MS);
    for (int i=0;i<32;++i) tick(100);
    assert(inSetup);
    release(DISPENSE_BUTTON_PIN);
  }

  // One physical short press accepts all three; reservations precede motion.
  resetCase(); press(DISPENSE_BUTTON_PIN); release(DISPENSE_BUTTON_PIN);
  if (!ENABLE_SERVO_MOVEMENT) {
    assert(savedEvents.size()==3 && pulses.empty());
    for (const auto &e:savedEvents) assert(strstr(e.note,"dry run") && strcmp(e.status,"DISPENSED")==0);
    return 0;
  }
  assert(reservations==3 && pendingDoseCount==1);
  assert(dose(0).state==DoseState::Dispensing && dose(1).state==DoseState::Dispensing && dose(2).state==DoseState::Queued);
  assert(pulseCount(1)==1 && pulseCount(2)==0 && pulseCount(3)==0);
  const int beforeNetwork=networkCalls;
  tick(DISPENSE_STAGGER_MS);
  assert(pulseCount(2)==1 && networkCalls==beforeNetwork);
  drop(1);
  tick(MOVE_TIME_MS);
  assert(pendingDoseCount==0 && dose(2).state==DoseState::Dispensing);
  assert(dispenserIsBusy() && savedEvents.empty() && deferredEventCount==1);
  tick(DISPENSE_STAGGER_MS);
  assert(pulseCount(3)==1);
  drop(2); drop(3); tick(MOVE_TIME_MS);
  assert(!dispenserIsBusy() && savedEvents.size()==3 && deferredEventCount==0);
  for (const auto &e:savedEvents) assert(strcmp(e.status,"DISPENSED")==0);
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Done);
  assert(reservations==3); // No repeat reservation when third channel gets capacity.

  // A raw brief cancel stops every motor and cancels the waiting third channel.
  resetCase(); acceptRound(); const size_t beforeCancel=pulses.size();
  levels[CANCEL_BUTTON_PIN]=LOW; tick();
  assert(!dispenserIsBusy() && pendingDoseCount==0 && savedEvents.size()==3);
  assert(dose(0).state==DoseState::Failed && dose(1).state==DoseState::Failed && dose(2).state==DoseState::Skipped);
  levels[CANCEL_BUTTON_PIN]=HIGH; tick(DISPENSE_STAGGER_MS+MOVE_TIME_MS);
  assert(pulses.size()==beforeCancel);

  // Failed reservation affects only that dose and never writes NVS during motion.
  resetCase(); rejectKey="dose-2"; acceptRound();
  assert(reservations==3 && dose(1).state==DoseState::Failed);
  assert(pulseCount(2)==0 && pendingDoseCount==0);
  levels[CANCEL_BUTTON_PIN]=LOW; tick();

  // The IR becomes blocked while channel 2 is waiting: no release pulse or false success.
  resetCase(); acceptRound();
  levels[PILL_SENSOR_PINS[1]]=LOW; tick(DISPENSE_STAGGER_MS);
  assert(pulseCount(2)==0 && dose(1).state==DoseState::Failed);
  assert(deferredEventCount==1 && savedEvents.empty());
  levels[CANCEL_BUTTON_PIN]=LOW; tick();
  assert(savedEvents.size()==3);

  // Snooze/skip remain whole-round actions while idle.
  resetCase(); assert(snoozeRound());
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Snoozed);
  resetCase(); dose(1).snoozeCount=MAX_SNOOZE_PER_DOSE;
  assert(!snoozeRound());
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Alerting);
  assert(skipRound()==3 && savedEvents.size()==3);
}
