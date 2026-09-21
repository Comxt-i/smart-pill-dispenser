#include "marquee.h"

namespace {
/** ช่องว่างคั่นระหว่างท้ายข้อความกับต้นข้อความรอบถัดไป */
constexpr uint8_t GAP = 3;

/** ความยาวของวงข้อความทั้งรอบ (ข้อความ + ช่องว่างคั่น) */
uint8_t cycleLength(const Marquee &marquee)
{
  return static_cast<uint8_t>(marquee.length + GAP);
}
}

void marqueeSet(Marquee &marquee, const char *text, uint8_t width)
{
  const char *source = text ? text : "";
  if (width == 0 || width > LCD_MAX_COLS)
    width = LCD_MAX_COLS;

  // ข้อความเดิมต้องไม่ถูกรีเซ็ตตำแหน่ง ไม่อย่างนั้นจะกระตุกทุกครั้งที่ loop เรียกซ้ำ
  if (marquee.width == width && strncmp(marquee.text, source, sizeof(marquee.text)) == 0)
    return;

  marquee.width = width;

  strncpy(marquee.text, source, sizeof(marquee.text) - 1);
  marquee.text[sizeof(marquee.text) - 1] = '\0';

  const size_t actual = strlen(marquee.text);
  marquee.length = static_cast<uint8_t>(actual > 255 ? 255 : actual);
  marquee.offset = 0;
  marquee.lastStepMs = 0;
  marquee.timerStarted = false;
  marquee.holdingAtStart = true;
}

bool marqueeScrolls(const Marquee &marquee)
{
  return marquee.length > marquee.width;
}

void marqueeRender(Marquee &marquee, unsigned long nowMs, char *out)
{
  if (!marqueeScrolls(marquee))
  {
    // สั้นพอที่จะอยู่นิ่ง เติมช่องว่างให้เต็มจอเพื่อลบข้อความเดิมที่ยาวกว่า
    snprintf(out, marquee.width + 1, "%-*.*s", marquee.width, marquee.width, marquee.text);
    marquee.offset = 0;
    return;
  }

  if (!marquee.timerStarted)
  {
    marquee.timerStarted = true;
    marquee.lastStepMs = nowMs;
  }

  const unsigned long interval =
      marquee.holdingAtStart ? LCD_MARQUEE_HOLD_MS : LCD_MARQUEE_STEP_MS;

  if (nowMs - marquee.lastStepMs >= interval)
  {
    marquee.lastStepMs = nowMs;
    marquee.holdingAtStart = false;
    marquee.offset = static_cast<uint8_t>((marquee.offset + 1) % cycleLength(marquee));

    // กลับมาครบรอบแล้ว หยุดพักให้อ่านต้นข้อความอีกครั้ง
    if (marquee.offset == 0)
      marquee.holdingAtStart = true;
  }

  const uint8_t cycle = cycleLength(marquee);
  for (uint8_t i = 0; i < marquee.width; ++i)
  {
    const uint8_t index = static_cast<uint8_t>((marquee.offset + i) % cycle);
    out[i] = index < marquee.length ? marquee.text[index] : ' ';
  }
  out[marquee.width] = '\0';
}
