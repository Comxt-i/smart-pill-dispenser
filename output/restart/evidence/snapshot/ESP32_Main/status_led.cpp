#include "status_led.h"
#include "config.h"

namespace {

constexpr uint8_t LED_COUNT = 3;

LedPattern pattern = LedPattern::Off;
unsigned long patternStartedMs = 0;

// การกดปุ่มทับรูปแบบพื้นหลังชั่วคราว
bool flashing = false;
LedColor flashColor = LedColor::Green;
unsigned long flashStartedMs = 0;

/** ขับหนึ่งดวงตามชนิดของโมดูล (common cathode หรือ common anode) */
void writeLed(uint8_t index, bool on)
{
  const int level = STATUS_LED_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
  digitalWrite(STATUS_LED_PINS[index], level);
}

/** เขียนสถานะของทั้งสามดวงพร้อมกัน */
void write(bool red, bool yellow, bool green)
{
  writeLed(0, red);
  writeLed(1, yellow);
  writeLed(2, green);
}

void writeOnly(LedColor color)
{
  const uint8_t index = static_cast<uint8_t>(color);
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    writeLed(i, i == index);
}

}  // namespace

void statusLedBegin()
{
  // ปิดอยู่ = ห้ามแตะขาเลย ไม่ต้อง pinMode ด้วย
  // ไม่อย่างนั้น GPIO2 จะถูกยึดไปจากไฟบนบอร์ด และขาอื่นจะถูกจองไว้เปล่าๆ
  if (!ENABLE_STATUS_LED)
    return;

  for (uint8_t i = 0; i < LED_COUNT; ++i)
  {
    pinMode(STATUS_LED_PINS[i], OUTPUT);
    writeLed(i, false);
  }

  // ไล่ไฟทีละดวงตอนเปิดเครื่อง เพื่อให้เห็นทันทีว่าทั้งสามดวงต่อถูกและไม่ขาด
  // ถ้าดวงไหนไม่ติดตอนนี้ก็รู้เลยว่าสายเส้นนั้นมีปัญหา ไม่ต้องรอให้ถึงเวลากินยา
  //
  // ใช้ delay() ได้เพราะอยู่ใน setup() ยังไม่มีอะไรต้องเดินเวลาพร้อมกัน
  for (uint8_t i = 0; i < LED_COUNT; ++i)
  {
    writeLed(i, true);
    delay(STATUS_LED_SELFTEST_MS);
    writeLed(i, false);
  }

  pattern = LedPattern::Off;
  patternStartedMs = millis();
  flashing = false;
}

void statusLedSet(LedPattern next)
{
  if (!ENABLE_STATUS_LED)
    return;

  // ตั้งค่าเดิมซ้ำต้องไม่รีเซ็ตจังหวะ ไม่อย่างนั้นไฟจะค้างที่เฟรมแรกตลอด
  // เพราะ pill_app เรียกฟังก์ชันนี้ทุกรอบ loop
  if (next == pattern)
    return;

  pattern = next;
  patternStartedMs = millis();
}

void statusLedFlash(LedColor color)
{
  if (!ENABLE_STATUS_LED)
    return;

  flashColor = color;
  flashStartedMs = millis();
  flashing = true;
}

void statusLedUpdate()
{
  if (!ENABLE_STATUS_LED)
    return;

  const unsigned long now = millis();

  // การตอบรับปุ่มสำคัญกว่าสถานะพื้นหลัง เพราะผู้ใช้เพิ่งกดและกำลังรอคำตอบ
  if (flashing)
  {
    if (now - flashStartedMs < STATUS_LED_FLASH_MS)
    {
      writeOnly(flashColor);
      return;
    }
    flashing = false;
  }

  switch (pattern)
  {
    case LedPattern::Off:
      write(false, false, false);
      return;

    case LedPattern::AlertBlink:
    {
      // ครึ่งแรกของคาบติด ครึ่งหลังดับ
      const bool on = ((now - patternStartedMs) / STATUS_LED_BLINK_MS) % 2 == 0;
      write(on, on, on);
      return;
    }

    case LedPattern::DispenseChase:
    {
      const uint8_t step =
          static_cast<uint8_t>(((now - patternStartedMs) / STATUS_LED_CHASE_MS) % LED_COUNT);
      for (uint8_t i = 0; i < LED_COUNT; ++i)
        writeLed(i, i == step);
      return;
    }
  }
}
