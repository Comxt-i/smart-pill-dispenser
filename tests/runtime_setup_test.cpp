#include <cassert>
#include "../ESP32_Main/setup_button.h"
using Action = SetupButtonAction;
int main() {
  SetupButtonGesture button;
  assert(button.update(false, 0, true) == Action::None);
  assert(button.update(true, 100, true) == Action::None);
  assert(button.update(true, 3099, true) == Action::None);
  assert(button.update(false, 3099, true) == Action::ShortPress);
  assert(button.update(false, 3100, true) == Action::None);
  // Long press opens once; release cannot dispense.
  assert(button.update(true, 4000, true) == Action::None);
  assert(button.update(true, 7000, true) == Action::OpenSetup);
  assert(button.update(true, 8000, true) == Action::None);
  assert(button.update(false, 8001, true) == Action::None);
  // Busy at any point invalidates the entire press, even after the motor stops.
  assert(button.update(true, 9000, false) == Action::None);
  assert(button.update(true, 12000, true) == Action::Blocked);
  assert(button.update(false, 12001, true) == Action::None);
  assert(button.update(true, 13000, true) == Action::None);
  assert(button.update(true, 14000, false) == Action::None);
  assert(button.update(true, 16000, true) == Action::Blocked);
  assert(button.update(false, 16001, true) == Action::None);
  // After release a fresh hold works; presses inside setup stay consumed.
  assert(button.update(true, 17000, true) == Action::None);
  assert(button.update(true, 20000, true) == Action::OpenSetup);
  assert(button.update(true, 21000, false) == Action::None);
  assert(button.update(false, 21001, true) == Action::None);
  // A delayed sample at release never turns a long hold into a dose.
  assert(button.update(true, 22000, true) == Action::None);
  assert(button.update(false, 26000, true) == Action::None);
  // millis wraparound.
  assert(button.update(true, UINT32_MAX - 1000, true) == Action::None);
  assert(button.update(true, 1999, true) == Action::OpenSetup);
  assert(button.update(false, 2000, true) == Action::None);
}
