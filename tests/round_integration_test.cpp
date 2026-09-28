#include <cassert>
#include <vector>
#include <algorithm>
#include <string>
#include <ESP32Servo.h>
#include <Wire.h>
#include "../ESP32_Main/buttons.cpp"
#include "../ESP32_Main/pill_app.cpp"
#include "../ESP32_Main/alert.cpp"
#include "../ESP32_Main/pill_hole.h"
SerialClass Serial;
WiFiClass WiFi;
TwoWire Wire;  // ตัวคุมจานสั่ง relay เลเซอร์ผ่าน PCF8574 บนบัสนี้
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
unsigned long extraUs = 0;
unsigned long micros() { return nowMs * 1000UL + extraUs; }
void (*sensorIsr[64])(void *) = {};
void *sensorIsrArg[64] = {};
void attachInterruptArg(uint8_t pin, void (*handler)(void *), void *arg, int) { sensorIsr[pin] = handler; sensorIsrArg[pin] = arg; }
/** เปลี่ยนระดับขาแบบฮาร์ดแวร์จริง: interrupt ที่ผูกไว้ทำงานทันที */
void drive(uint8_t pin, int level) { if (levels[pin] == level) return; levels[pin] = level; if (sensorIsr[pin]) sensorIsr[pin](sensorIsrArg[pin]); }
void pinMode(uint8_t, uint8_t) {}
// ตัวรับเลเซอร์จริง: relay ดับ (P0 ของ PCF8574 เป็น HIGH) = มืด = อ่านได้เหมือนถูกบัง
int digitalRead(uint8_t pin) {
  for (uint8_t i = 0; i < DISPENSER_COUNT; ++i) {
    if (pin != PILL_SENSOR_PINS[i] || !ENABLE_LASER_SWITCH) continue;
    const int pcf = Wire.lastValue[LASER_PCF8574_ADDRESS];
    const bool lit = pcf >= 0 && !((pcf >> LASER_PCF_BIT) & 1);
    if (!lit) return LOW;
  }
  return levels[pin];
}
// ขา buzzer (ใช้ alert.cpp ตัวจริง ไม่ใช่ตัวปลอม): passive buzzer มีเสียงเฉพาะตอนได้คลื่นความถี่
// = PWM duty 50% ไฟ HIGH ค้าง (digitalWrite) เงียบ นับว่าถูกสั่ง "ดัง" กี่ครั้ง และ duty ล่าสุด
const uint32_t BUZZER_TONE = 1u << (BUZZER_PWM_RESOLUTION_BITS - 1);
int buzzerSoundWrites = 0, buzzerDcWrites = 0, buzzerChannel = -1;
long buzzerDuty = -1;
uint32_t buzzerFreq = 0;
// จังหวะเสียง: นับ "ชุดเสียง" จากช่วงเงียบระหว่างชุด (ไม่ผูกกับโน้ตที่เลือก) และเก็บระดับเสียงที่ใช้
extern uint32_t nowMs;
int chimes = 0;
bool buzzerSounding = false;
uint32_t buzzerQuietSince = 0;
std::vector<uint32_t> pitches;
constexpr uint32_t CHIME_GAP_MS = 300;  // เงียบนานกว่านี้ = จบชุด (ช่วงหยุดในชุดสั้นกว่านี้มาก)
void digitalWrite(uint8_t pin, int value) { (void)value; if (pin == BUZZER_PIN) ++buzzerDcWrites; }
void analogWrite(uint8_t pin, int value) { (void)pin; (void)value; }
bool ledcAttachChannel(uint8_t pin, uint32_t freq, uint8_t, uint8_t channel) {
  if (pin == BUZZER_PIN) { buzzerChannel = channel; buzzerFreq = freq; }
  return true;
}
uint32_t ledcChangeFrequency(uint8_t pin, uint32_t freq, uint8_t) {
  if (pin == BUZZER_PIN) { buzzerFreq = freq; pitches.push_back(freq); }
  return freq;
}
bool ledcWrite(uint8_t pin, uint32_t duty) {
  if (pin != BUZZER_PIN) return true;
  buzzerDuty = duty;
  const bool sounding = duty == BUZZER_TONE;
  if (sounding) {
    ++buzzerSoundWrites;
    if (!buzzerSounding && nowMs - buzzerQuietSince > CHIME_GAP_MS) ++chimes;
  } else if (buzzerSounding) {
    buzzerQuietSince = nowMs;
  }
  buzzerSounding = sounding;
  return true;
}
// ไฟสถานะไม่มีผลต่อพฤติกรรมที่เทสต์นี้ตรวจ จึงกลืนทิ้ง
// แต่ต้องมี ไม่อย่างนั้นลิงก์ไม่ผ่านเพราะ pill_app.cpp เรียกใช้จริง
void statusLedBegin() {}
void statusLedFlash(LedColor) {}
void statusLedSet(LedPattern) {}
void statusLedUpdate() {}
void lcdShowMessage(const char*, const char*) { assert(!dispenserIsBusy()); }
void lcdMedicineTick() { assert(!dispenserIsBusy()); }
std::string lcdTitle;  // บรรทัดแรกของจอยาครั้งล่าสุด
void lcdSetMedicineScreen(const char *const *lines, uint8_t count, const char *const*, uint8_t) {
  assert(!dispenserIsBusy());  // ห้ามแตะบัส I2C ระหว่างมอเตอร์ทำงาน
  lcdTitle = count ? lines[0] : "";
}
bool rtcIsValid() { return true; }
// หน้าจอรายงานผลสแกน I2C ตอนบูตเรียกสามตัวนี้
// Reached only from appBegin()/appStatusJson(), which this test never calls.
// --gc-sections drops them on ELF, but MinGW/PE keeps every function, so without
// these the test cannot link on Windows at all.
void eventQueueBegin() {}
void netSyncBegin() {}
void netSyncService() {}
void netSyncPump() {}
bool netSyncFirstSyncFinished() { return false; }
bool flashStoreBegin() { return true; }
bool netSyncApplyCachedSchedule() { return false; }
void scheduleCacheBegin() {}
// มื้อที่ถูกบันทึกลง NVS ว่า "จบแล้ววันนี้" (กันเตือน/จ่ายซ้ำหลังไฟดับ)
std::vector<std::string> closedIds;
void scheduleCacheMarkClosed(const char *id, uint32_t) { closedIds.push_back(id); }
bool isClosed(const char *id) { for (const auto &c : closedIds) if (c == id) return true; return false; }
bool scheduleCacheIsClosed(const char *id, uint32_t) { return isClosed(id); }
bool netSyncLastCallOk() { return true; }
unsigned long netSyncLastOkMs() { return millis(); }
const char *netSyncLastErrorShort() { return ""; }
bool netSyncJobBusy() { return false; }
const char *netSyncLastError() { return ""; }
const char *netSyncConfigVersion() { return ""; }
void rtcLcdBegin() {}
I2cLineState i2cLastLineState() { return I2cLineState{}; }
const char *rtcClockSource() { return "RTC"; }
void wifiWebBegin() {}
void delay(unsigned long ms) { nowMs += ms; }
void delayMicroseconds(unsigned int us) { extraUs += us; }
// หน้าจอตั้งค่าและหน้าวินิจฉัยตอนบูตเรียกสามตัวนี้
const char *wifiSetupStatusShort() { return "OPEN"; }
bool wifiHasSavedNetwork() { return true; }
const char *wifiSavedSsid() { return "Home"; }
bool rtcIsPresent() { return true; }
bool rtcLostTimeDetected() { return false; }
bool rtcWasOffAtLastBoot() { return false; }
bool rtcHasTime() { return true; }
void rtcSetNetworkOnline(bool) {}
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
int releases = 0;
bool commandJournalRelease(const char *key) {
  for (auto it = reservedKeys.begin(); it != reservedKeys.end(); ++it)
    if (*it == key) { reservedKeys.erase(it); ++releases; return true; }
  return false;
}
const char *commandJournalLastError() { return "already started"; }
void tick(uint32_t ms = 1) { nowMs += ms; appLoop(); }
void press(uint8_t pin) { levels[pin]=LOW; tick(); tick(BUTTON_DEBOUNCE_MS); }
void release(uint8_t pin) { levels[pin]=HIGH; tick(); tick(BUTTON_DEBOUNCE_MS); }
void drop(uint8_t slot) {
  // เม็ดบังลำแสงไม่กี่มิลลิวินาที จับได้ด้วย interrupt แม้ loop ไม่ได้วนระหว่างนั้น
  drive(PILL_SENSOR_PINS[slot-1], LOW); extraUs += 3000;
  drive(PILL_SENSOR_PINS[slot-1], HIGH); tick();
}
size_t pulseCount(uint8_t slot) {
  size_t n=0; for (const auto &p:pulses) if (p.pin==SERVO_PINS[slot-1]) ++n; return n;
}
Dose &dose(int index) { return *scheduleDoseAt({static_cast<uint8_t>(index),0,true}); }
int8_t slot1PillHole = PILL_HOLE_ANY;  // hole chosen from the pill size set on the web
void resetCase() {
  for (int &v:levels) v=HIGH;
  dispenserControlBegin(); buttonsBegin(); scheduleBegin(onDoseStateChanged);
  pulses.clear(); savedEvents.clear(); reservedKeys.clear();
  reservations=networkCalls=0; rejectKey="";
  setupButton={}; inSetup=false; pendingDoseCount=deferredEventCount=0; closedIds.clear();
  cancelPressActive=false; clockWasValid=true; firstTickAfterClock=false;
  for (auto &owner:runOwner) owner=RunOwner::None;
  scheduleBeginSync();
  for (int i=0;i<3;++i) {
    char id[20]; snprintf(id,sizeof(id),"dose-%d",i+1);
    int slot=scheduleStageSlot(i+1,true,"test-med","Test",1);
    if (i==0) scheduleStageSlotPillHole(slot, slot1PillHole);
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

  // The physical yellow and red buttons, through the real button driver and appLoop (not just
  // snoozeRound/skipRound): a short press during the alert snoozes / skips the whole round.
  resetCase(); press(SNOOZE_BUTTON_PIN); release(SNOOZE_BUTTON_PIN);
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Snoozed);
  assert(pulses.empty() && reservations==0);
  resetCase(); press(CANCEL_BUTTON_PIN); release(CANCEL_BUTTON_PIN);
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Skipped);
  assert(pulses.empty() && reservations==0 && savedEvents.size()==3);
  for (const auto &e:savedEvents) assert(strcmp(e.status,"SKIPPED")==0);

  // The buzzer really sounds while a dose is due (real alert.cpp driving GPIO15, not a stub),
  // keeps reminding, and falls silent once the round is handled.
  // The box has a PASSIVE buzzer: it only sounds when fed a tone. The firmware used to switch the
  // pin HIGH/LOW like an active buzzer, so the box never made a sound although the logic was right.
  resetCase(); alertBegin();
  assert(buzzerChannel==BUZZER_LEDC_CHANNEL);  // its own PWM channel
  buzzerSoundWrites=0; buzzerDcWrites=0; chimes=0; pitches.clear(); buzzerQuietSince=0;  // forget the boot chirp
  for (int i=0;i<3;++i) tick(100);
  assert(chimes==1);  // the first reminder starts at once, not after a period
  for (int i=0;i<7;++i) tick(100);  // let the first chime finish (well before the next one)
  assert(chimes==1);
  // A gentle chime of several rising notes, not one flat beep ("don't make it frightening").
  { std::vector<uint32_t> firstChime=pitches;
    assert(firstChime.size()>=2);
    for (size_t i=1;i<firstChime.size();++i) assert(firstChime[i]>firstChime[i-1]);
    for (uint32_t hz:firstChime) assert(hz>=1500 && hz<=3500); }  // where a passive buzzer is loud
  // Every ALERT_REMINDER_PERIOD_MS (2 s). It used to be every 5 s: "too far apart" to notice.
  chimes=0;
  for (int i=0;i<500;++i) tick(20);  // 10 s, fine steps so chime timing is as on the box
  assert(chimes>=4 && chimes<=6);
  assert(buzzerDcWrites==0);     // a plain HIGH would be silent on a passive buzzer
  // Nobody has responded for ALERT_ESCALATE_AFTER_MS: a little more often, same chime.
  for (uint32_t t=0;t<ALERT_ESCALATE_AFTER_MS;t+=500) tick(500);
  chimes=0;
  for (int i=0;i<500;++i) tick(20);  // 10 s
  assert(chimes>=7 && chimes<=10);
  press(CANCEL_BUTTON_PIN); release(CANCEL_BUTTON_PIN);
  for (int i=0;i<20;++i) tick(100);  // the skip click finishes
  { const int afterSkip=buzzerSoundWrites;
    for (int i=0;i<100;++i) tick(100);
    assert(buzzerSoundWrites==afterSkip);
    assert(buzzerDuty==(BUZZER_ACTIVE_HIGH ? 0 : long((1u<<BUZZER_PWM_RESOLUTION_BITS)-1))); }  // held at the module's idle level
  // The next alert starts gentle again, not at the escalated pace the previous one reached.
  resetCase(); chimes=0; buzzerQuietSince=0;
  for (int i=0;i<500;++i) tick(20);
  assert(chimes>=4 && chimes<=6);
  press(CANCEL_BUTTON_PIN); release(CANCEL_BUTTON_PIN);

  // One physical short press accepts all three; reservations precede motion.
  resetCase(); press(DISPENSE_BUTTON_PIN); release(DISPENSE_BUTTON_PIN);
  if (!ENABLE_SERVO_MOVEMENT) {
    assert(savedEvents.size()==3 && pulses.empty());
    for (const auto &e:savedEvents) assert(strstr(e.note,"dry run") && strcmp(e.status,"DISPENSED")==0);
    assert(isClosed("dose-1") && isClosed("dose-2") && isClosed("dose-3"));
    return 0;
  }
  assert(reservations==3 && pendingDoseCount==1);
  assert(dose(0).state==DoseState::Dispensing && dose(1).state==DoseState::Dispensing && dose(2).state==DoseState::Queued);
  // จานที่เริ่มหมุนแล้วต้องถูกบันทึกทันที: ไฟดับตอนนี้ไม่รู้ว่ายาออกไปแล้วกี่เม็ด ห้ามเตือนซ้ำ
  // ส่วนมื้อที่ยังรอคิว ยังไม่มียาออกมา ต้องเตือนใหม่ได้หลังเปิดเครื่อง
  assert(isClosed("dose-1") && isClosed("dose-2") && !isClosed("dose-3"));
  assert(pulseCount(1)==1 && pulseCount(2)==0 && pulseCount(3)==0);
  const int beforeNetwork=networkCalls;
  tick(DISPENSE_STAGGER_MS);
  assert(pulseCount(2)==1 && networkCalls==beforeNetwork);
  drop(1);
  tick(MOVE_TIME_MS);
  if (ENABLE_PILL_SENSOR) tick(PILL_SETTLE_MS);  // plate 1 waits for late pills before freeing its slot
  assert(pendingDoseCount==0 && dose(2).state==DoseState::Dispensing);
  assert(dispenserIsBusy() && savedEvents.empty() && deferredEventCount==1);
  tick(DISPENSE_STAGGER_MS);
  assert(pulseCount(3)==1);
  drop(2); drop(3); tick(MOVE_TIME_MS);
  if (ENABLE_PILL_SENSOR) tick(PILL_SETTLE_MS);
  assert(!dispenserIsBusy() && savedEvents.size()==3 && deferredEventCount==0);
  for (const auto &e:savedEvents) assert(strcmp(e.status,"DISPENSED")==0);
  // Logged at the moment green was pressed, not when the last queued plate finished, so a
  // round accepted in time is never judged "taken late" by the server.
  { const uint32_t acceptedAt=dose(0).acceptedEpoch;
    assert(acceptedAt!=0 && rtcLocalEpoch()>acceptedAt);  // plates really finished later
    for (const auto &e:savedEvents) assert(e.localEpoch==acceptedAt); }
  for (int i=0;i<3;++i) assert(dose(i).state==DoseState::Done);
  assert(reservations==3); // No repeat reservation when third channel gets capacity.

  // A raw brief cancel stops every motor and cancels the waiting third channel.
  resetCase(); lcdTitle.clear(); acceptRound(); const size_t beforeCancel=pulses.size();
  assert(lcdTitle=="DISPENSING PILLS");  // the big LCD says so before any motor moves
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

  // Pill size 15 mm on the web: the very first servo move must be the <=15 mm hole, not the
  // smallest one. (It once went to the smallest hole first because the hole table was mirrored.)
  if (ENABLE_SERVO_MOVEMENT) {
    slot1PillHole=pillHoleFromMillimetres(15);
    resetCase(); acceptRound();
    int first=-1;
    for (const auto &p:pulses) if (p.pin==SERVO_PINS[0] && p.value!=REST_PULSE_US[0]) { first=p.value; break; }
    assert(slot1PillHole==2 && first==HOLE_PULSE_US[0][2]);
    levels[CANCEL_BUTTON_PIN]=LOW; tick(); levels[CANCEL_BUTTON_PIN]=HIGH;
    slot1PillHole=PILL_HOLE_ANY;
  }

  // Refused before any motion (plate 1 beam blocked at start): no pill can have come out, so
  // the day lock is released and the dose can be accepted again once fixed. Before this fix
  // the dose stayed "dose locked" all day although nothing was ever dispensed.
  if (ENABLE_SERVO_MOVEMENT && ENABLE_PILL_SENSOR) {
    resetCase(); releases=0;
    levels[PILL_SENSOR_PINS[0]]=LOW;  // something sits in the beam
    acceptRound();
    assert(dose(0).state==DoseState::Failed && pulseCount(1)==0);
    assert(releases==1);
    for (const auto &k:reservedKeys) assert(k!="dose-1");
    // Plates that did start keep their lock: pills may already be out.
    assert(std::find(reservedKeys.begin(),reservedKeys.end(),"dose-2")!=reservedKeys.end());
    levels[CANCEL_BUTTON_PIN]=LOW; tick(); levels[CANCEL_BUTTON_PIN]=HIGH;
    levels[PILL_SENSOR_PINS[0]]=HIGH;
  }
}
