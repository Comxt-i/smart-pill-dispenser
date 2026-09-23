#include <cassert>
#include "../ESP32_Main/buttons.cpp"
#include "../ESP32_Main/pill_app.cpp"
uint32_t nowMs = 0;
int levels[64];
bool busy = false, inSetup = false;
int dispenseCalls = 0, setupCalls = 0, stopCalls = 0;
Dose testDose = {};
Slot testSlot = {};
SerialClass Serial;
WiFiClass WiFi;
unsigned long millis() { return nowMs; }
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return levels[pin]; }
bool dispenserIsBusy() { return busy; }
bool wifiSetupActive() { return inSetup; }
bool wifiStartSetup() { ++setupCalls; inSetup = true; return true; }
void alertOneShot(AlertPattern) {}
void alertSet(AlertPattern) {}
void lcdShowMessage(const char*, const char*) {}
void netSyncRequestNow() {}
void stopDispenser() { ++stopCalls; busy = false; }
DispenseResult dispenseMedicine(uint8_t, uint8_t) { ++dispenseCalls; return DispenseResult::Started; }
Dose* scheduleDoseAt(const DoseRef&) { return &testDose; }
const Slot* scheduleSlotOf(const DoseRef&) { return &testSlot; }
void scheduleSetState(const DoseRef&, DoseState state) { testDose.state = state; }
bool scheduleSnooze(const DoseRef&, int) { return true; }
uint32_t rtcLocalEpoch() { return 0; }
int rtcMinutesOfDay() { return 0; }
void eventQueueMakeId(char*, size_t) {}
bool eventQueuePush(const PendingEvent&) { return true; }
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
  runOwner = RunOwner::None; cancelPressActive = false;
  busy = inSetup = false;
  dispenseCalls = setupCalls = stopCalls = 0;
  testDose.state = DoseState::Alerting;
  testSlot.number = 1; testSlot.amountPerDose = 1;
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
}
