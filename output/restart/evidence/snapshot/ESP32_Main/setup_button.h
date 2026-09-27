#pragma once
#include <stdint.h>

// Input is already debounced by buttonsUpdate(). Short press fires on release.
enum class SetupButtonAction { None, ShortPress, OpenSetup, Blocked };
class SetupButtonGesture {
 public:
  SetupButtonAction update(bool held, uint32_t now, bool allowed) {
    if (!held) {
      const bool shortPress = active && !consumed && !blocked && allowed &&
                              uint32_t(now - started) < HOLD_MS;
      active = consumed = blocked = false;
      return shortPress ? SetupButtonAction::ShortPress : SetupButtonAction::None;
    }
    if (!active) {
      active = true;
      started = now;
      blocked = !allowed;
    }
    blocked = blocked || !allowed;
    if (!consumed && uint32_t(now - started) >= HOLD_MS) {
      consumed = true;
      return blocked ? SetupButtonAction::Blocked : SetupButtonAction::OpenSetup;
    }
    return SetupButtonAction::None;
  }
 private:
  static constexpr uint32_t HOLD_MS = 3000;
  bool active = false, consumed = false, blocked = false;
  uint32_t started = 0;
};
