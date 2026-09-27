#include "buttons.h"
#include "config.h"

namespace {
constexpr uint8_t BUTTON_COUNT = static_cast<uint8_t>(ButtonId::Count);

const uint8_t BUTTON_PINS[BUTTON_COUNT] = {
  DISPENSE_BUTTON_PIN,
  SNOOZE_BUTTON_PIN,
  CANCEL_BUTTON_PIN,
};

struct ButtonState {
  bool stable;          // true = กำลังถูกกด
  bool lastRaw;
  bool pressedLatch;    // รอให้ผู้เรียกมาอ่านขอบขาลง
  bool holdReported;
  unsigned long changedAtMs;
  unsigned long pressedAtMs;
};

ButtonState states[BUTTON_COUNT];

// เวลาที่อ่านปุ่มครั้งล่าสุด ใช้รู้ว่า loop ค้างไปนานแค่ไหนระหว่างสองครั้ง
unsigned long lastUpdateMs = 0;

uint8_t indexOf(ButtonId id)
{
  return static_cast<uint8_t>(id);
}
}

void buttonsBegin()
{
  const unsigned long now = millis();
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i)
  {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    states[i].stable = false;
    states[i].lastRaw = false;
    states[i].pressedLatch = false;
    states[i].holdReported = false;
    states[i].changedAtMs = now;
    states[i].pressedAtMs = 0;
  }
  lastUpdateMs = now;
}

void buttonsUpdate()
{
  const unsigned long now = millis();

  // loop ค้างนานกว่านี้ (เช่นรอ HTTPS) = ระหว่างนั้นเราไม่เห็นปุ่มเลย
  // หน้าสัมผัสหยุดเด้งไปนานแล้ว ค่าที่อ่านได้ตอนนี้จึงเชื่อได้ทันทีโดยไม่ต้องรอ debounce อีกรอบ
  //
  // ถ้ารอ debounce ตามปกติ สถานะ "กดอยู่" จากก่อนค้างจะติดไปอีกหนึ่งรอบ แล้วถูกตีความว่า
  // กดค้างมาตลอดช่วงที่ค้าง การกดสั้นๆ จึงกลายเป็นกดค้าง 3 วินาที แล้วเปิดหน้า setup เอง
  const bool stale = now - lastUpdateMs > BUTTON_STALE_GAP_MS;
  lastUpdateMs = now;

  for (uint8_t i = 0; i < BUTTON_COUNT; ++i)
  {
    ButtonState &state = states[i];
    const bool raw = digitalRead(BUTTON_PINS[i]) == LOW;  // ปุ่มต่อลง GND

    if (stale)
    {
      state.lastRaw = raw;
      state.changedAtMs = now;
      if (raw != state.stable)
      {
        state.stable = raw;
        if (raw)
        {
          state.pressedLatch = true;
          state.pressedAtMs = now;
          state.holdReported = false;
        }
      }
      continue;
    }

    if (raw != state.lastRaw)
    {
      state.lastRaw = raw;
      state.changedAtMs = now;
      continue;
    }

    if (now - state.changedAtMs < BUTTON_DEBOUNCE_MS || raw == state.stable)
      continue;

    state.stable = raw;
    if (raw)
    {
      state.pressedLatch = true;
      state.pressedAtMs = now;
      state.holdReported = false;
    }
  }
}

bool buttonPressed(ButtonId id)
{
  ButtonState &state = states[indexOf(id)];
  if (!state.pressedLatch)
    return false;

  state.pressedLatch = false;
  return true;
}

bool buttonHeld(ButtonId id)
{
  return states[indexOf(id)].stable;
}

bool buttonHeldFor(ButtonId id, unsigned long durationMs)
{
  ButtonState &state = states[indexOf(id)];
  if (!state.stable || state.holdReported)
    return false;
  if (millis() - state.pressedAtMs < durationMs)
    return false;

  state.holdReported = true;
  return true;
}
