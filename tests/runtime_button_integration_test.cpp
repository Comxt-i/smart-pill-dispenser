#include <cassert>
#include "../ESP32_Main/buttons.cpp"
#include "../ESP32_Main/pill_app.cpp"
uint32_t nowMs = 0;
int levels[64];
bool busy = false, inSetup = false;
int dispenseCalls = 0, setupCalls = 0, stopCalls = 0;
Dose testDose = {};
Slot testSlot = {};
PendingEvent lastEvent = {};
int queuedEvents = 0;
DispenseResult simulatedResult = DispenseResult::Started;
SerialClass Serial;
WiFiClass WiFi;
unsigned long millis() { return nowMs; }
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return levels[pin]; }
bool dispenserIsBusy() { return busy; }
bool dispenserHasCapacity() { return !busy; }
bool wifiSetupActive() { return inSetup; }
bool wifiStartSetup() { ++setupCalls; inSetup = true; return true; }
void alertOneShot(AlertPattern) {}
void alertSet(AlertPattern) {}
void lcdShowMessage(const char*, const char*) {}
void netSyncRequestNow() {}
void stopDispenser() { ++stopCalls; busy = false; }
DispenseResult dispenseMedicine(uint8_t, uint8_t pills) { ++dispenseCalls; if (!pills) return DispenseResult::Invalid; busy = simulatedResult == DispenseResult::Started; return simulatedResult; }
Dose* scheduleDoseAt(const DoseRef&) { return &testDose; }
const Slot* scheduleSlotOf(const DoseRef&) { return &testSlot; }
int scheduleFindSlot(uint8_t) { return 0; }
const Slot &scheduleSlot(uint8_t) { return testSlot; }
void scheduleSetState(const DoseRef&, DoseState state) { testDose.state = state; }
bool scheduleSnooze(const DoseRef&, int) { return true; }
bool scheduleCanSnooze(const DoseRef&, int) { return true; }
uint8_t scheduleAlertingDoses(DoseRef *out, uint8_t) {
  if (testDose.state != DoseState::Alerting) return 0;
  out[0] = {0,0,true}; return 1;
}
uint32_t rtcLocalEpoch() { return 0; }
int rtcMinutesOfDay() { return 0; }
void eventQueueMakeId(char*, size_t) {}
bool eventQueuePush(const PendingEvent &event) { lastEvent = event; ++queuedEvents; return true; }
void tick(uint32_t ms) { nowMs += ms; buttonsUpdate(); handleButtons(); }
void press(ButtonId id) {
  levels[BUTTON_PINS[static_cast<uint8_t>(id)]] = LOW;
  tick(1); tick(BUTTON_DEBOUNCE_MS);
}
void release(ButtonId id) {
  levels[BUTTON_PINS[static_cast<uint8_t>(id)]] = HIGH;
  tick(1); tick(BUTTON_DEBOUNCE_MS);
}
void resetCase() {
  for (int &level : levels) level = HIGH;
  buttonsBegin(); setupButton = {};
  for (auto &owner : runOwner) owner = RunOwner::None;
  pendingDoseCount = deferredEventCount = 0; cancelPressActive = false;
  busy = inSetup = false;
  dispenseCalls = setupCalls = stopCalls = 0;
  testDose.state = DoseState::Alerting;
  strcpy(testDose.scheduleId, "dose-1");
  testSlot.number = 1; testSlot.amountPerDose = 1;
}
int networkCalls = 0, clockCalls = 0, motionTicks = 0;
bool outcomeReady = false;
DispenseOutcome suppliedOutcome = {};
void commandJournalDoseKey(char*, size_t, const char*, uint32_t, bool) {}
bool commandJournalReserve(const char*, uint32_t) { return true; }
void dispenserControlUpdate() { ++motionTicks; }
void wifiWebLoop() { ++networkCalls; }
void rtcLcdUpdate() { ++clockCalls; }
void alertUpdate() {}
void lcdMedicineTick() {}
void lcdSetMedicineScreen(const char *const *, uint8_t, const char *const *, uint8_t) {}
bool rtcIsValid() { return false; }
uint32_t rtcDayKey() { return 20260925; }
bool scheduleConsumeDayRollover() { return false; }
bool scheduleHasData() { return false; }
uint8_t scheduleSnoozeCount(const DoseRef&) { return 0; }
uint8_t scheduleSlotCount() { return 0; }
DoseRef scheduleTick(int, uint32_t, bool) { return {}; }
DoseRef scheduleFindByScheduleId(const char*) { return {0,0,true}; }
DoseRef scheduleFirstSnoozed() { return {}; }
bool doseIsOpen(const Dose&) { return false; }
int doseEffectiveMinutes(const Dose&) { return 0; }
bool netSyncDue() { return true; }
bool netSyncFetch() { ++networkCalls; return true; }
bool netSyncFlushEvents() { ++networkCalls; return true; }
bool netSyncTakeCommand(RemoteCommand&) { return false; }
uint8_t eventQueueSize() { return 1; }
const char *wifiSetupSsid() { return "test"; }
const char *wifiSetupPassword() { return "test-only"; }
bool takeDispenseOutcome(DispenseOutcome &out) {
  if (!outcomeReady) return false;
  out = suppliedOutcome; outcomeReady = false; return true;
}
int main() {
  resetCase();
  press(ButtonId::Dispense); tick(500);
  assert(dispenseCalls == 0 && setupCalls == 0);
  release(ButtonId::Dispense);
  assert(dispenseCalls == 1 && setupCalls == 0);
  tick(500); assert(dispenseCalls == 1);

  resetCase();
  press(ButtonId::Dispense); tick(3000);
  assert(setupCalls == 1 && dispenseCalls == 0);
  release(ButtonId::Dispense);
  assert(dispenseCalls == 0 && setupCalls == 1);
  // Subsequent short presses during setup must not dispense either.
  press(ButtonId::Dispense); release(ButtonId::Dispense);
  assert(dispenseCalls == 0);

  resetCase(); busy = true;
  press(ButtonId::Dispense); tick(1000); busy = false; tick(2000);
  release(ButtonId::Dispense);
  assert(setupCalls == 0 && dispenseCalls == 0);
  press(ButtonId::Dispense); tick(3000);
  assert(setupCalls == 1 && dispenseCalls == 0);

  resetCase(); busy = true;
  press(ButtonId::Dispense);
  press(ButtonId::Cancel); tick(CANCEL_HOLD_MS);
  assert(stopCalls == 1);
  tick(3000); release(ButtonId::Dispense);
  assert(setupCalls == 0 && dispenseCalls == 0);
  // The real application reports explicit simulation markers for button and web commands.
  resetCase(); simulatedResult=DispenseResult::Disabled;
  queuedEvents=0;
  press(ButtonId::Dispense); release(ButtonId::Dispense);
  assert(queuedEvents == 1 && strcmp(lastEvent.status, "DISPENSED") == 0);
  assert(strstr(lastEvent.note, "dry run") != nullptr);
  RemoteCommand command = {}; strcpy(command.id, "test-command"); command.slot=1; command.amount=2;
  startCommandDispense(command);
  assert(queuedEvents == 2 && strcmp(lastEvent.commandId, "test-command") == 0);
  assert(strcmp(lastEvent.status, "DISPENSED") == 0 && strstr(lastEvent.note, "dry run"));
  PendingEvent skipped = {}; strcpy(skipped.status, "SKIPPED"); strcpy(skipped.note, "cancelled by user");
  queueEvent(skipped);
  assert(strstr(lastEvent.note, "dry run") && strstr(lastEvent.note, "cancelled by user"));
  assert(pillsFor(0.5f) == 0 && pillsFor(1.5f) == 0);
  assert(pillsFor(0) == 0 && pillsFor(NAN) == 0 && pillsFor(INFINITY) == 0);
  assert(pillsFor(MAX_PILLS_PER_DOSE + 1) == 0 && pillsFor(2) == 2);
  resetCase(); busy = true; networkCalls = clockCalls = motionTicks = 0;
  appLoop();
  assert(motionTicks == 1 && networkCalls == 0 && clockCalls == 0);
  busy = false; inSetup = true;
  appLoop();
  assert(networkCalls == 1 && clockCalls == 1); // Idle networking resumes.
  resetCase(); simulatedResult = DispenseResult::Started;
  press(ButtonId::Dispense);
  levels[DISPENSE_BUTTON_PIN] = HIGH; tick(1); nowMs += BUTTON_DEBOUNCE_MS;
  networkCalls = 0;
  appLoop(); // This release starts motion before the HTTP sync phase.
  assert(busy && networkCalls == 1); // Only pre-gesture local web handling, no HTTP sync.
  resetCase(); runOwner[0] = RunOwner::Command;
  suppliedOutcome = {1, 1, 2, 1, false, true}; outcomeReady = true;
  handleDispenseOutcome();
  assert(strcmp(lastEvent.status, "FAILED") == 0 && strstr(lastEvent.note, "2/1"));
  resetCase(); runOwner[0] = RunOwner::Dose;
  testDose.failureReported = false;
  suppliedOutcome = {1, 2, 1, 1, false, true}; outcomeReady = true;
  handleDispenseOutcome();
  assert(testDose.state == DoseState::Failed && strstr(lastEvent.note, "1/2"));
}
