#include "alert.h"
#include "config.h"

namespace {
AlertPattern current = AlertPattern::None;
AlertPattern oneShot = AlertPattern::None;
AlertPattern activePattern = AlertPattern::None;

uint8_t beepsRemaining = 0;
bool buzzerOn = false;
unsigned long phaseStartedMs = 0;
unsigned long cycleStartedMs = 0;

void writeBuzzer(bool on)
{
  buzzerOn = on;
  digitalWrite(BUZZER_PIN, (on == BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
}

uint8_t beepsFor(AlertPattern pattern)
{
  switch (pattern)
  {
    case AlertPattern::Reminder: return 2;
    case AlertPattern::Success: return 1;
    case AlertPattern::Warning: return 3;
    case AlertPattern::Click: return 1;
    case AlertPattern::None: return 0;
  }
  return 0;
}

unsigned long onTimeFor(AlertPattern pattern)
{
  switch (pattern)
  {
    case AlertPattern::Success: return BEEP_ON_MS * 3;
    case AlertPattern::Click: return BEEP_ON_MS / 2;
    default: return BEEP_ON_MS;
  }
}

/** เริ่มชุดเสียงใหม่หนึ่งชุด */
void startBurst(AlertPattern pattern)
{
  activePattern = pattern;
  beepsRemaining = beepsFor(pattern);
  cycleStartedMs = millis();
  phaseStartedMs = millis();
  writeBuzzer(beepsRemaining > 0);
}
}

void alertBegin()
{
  pinMode(BUZZER_PIN, OUTPUT);
  writeBuzzer(false);

  // ตรวจสายตอนบูต: ใช้ delay ได้เพราะอยู่ใน setup() ยังไม่มีอะไรต้องเดินพร้อมกัน
  if (BUZZER_BOOT_CHIRP_MS > 0)
  {
    writeBuzzer(true);
    delay(BUZZER_BOOT_CHIRP_MS);
    writeBuzzer(false);
  }
  current = AlertPattern::None;
  oneShot = AlertPattern::None;
  activePattern = AlertPattern::None;
  beepsRemaining = 0;
}

void alertSet(AlertPattern pattern)
{
  if (current == pattern)
    return;

  current = pattern;

  // เสียงชุดที่กำลังเล่นอยู่ให้เล่นจนจบ แล้วค่อยเปลี่ยนไปใช้รูปแบบใหม่
  if (beepsRemaining == 0 && oneShot == AlertPattern::None)
  {
    if (pattern == AlertPattern::None)
    {
      writeBuzzer(false);
      activePattern = AlertPattern::None;
    }
    else
    {
      startBurst(pattern);
    }
  }
}

void alertOneShot(AlertPattern pattern)
{
  oneShot = pattern;
  startBurst(pattern);
}

void alertUpdate()
{
  const unsigned long now = millis();

  if (beepsRemaining > 0)
  {
    const unsigned long limit = buzzerOn ? onTimeFor(activePattern) : BEEP_OFF_MS;
    if (now - phaseStartedMs < limit)
      return;

    if (buzzerOn)
    {
      writeBuzzer(false);
      --beepsRemaining;
    }
    else
    {
      writeBuzzer(true);
    }
    phaseStartedMs = now;
    return;
  }

  writeBuzzer(false);

  if (oneShot != AlertPattern::None)
  {
    // เล่นเสียงตอบรับจบแล้ว กลับไปใช้รูปแบบเดิม
    oneShot = AlertPattern::None;
    if (current != AlertPattern::None)
      startBurst(current);
    else
      activePattern = AlertPattern::None;
    return;
  }

  if (current == AlertPattern::Reminder && now - cycleStartedMs >= ALERT_BEEP_PERIOD_MS)
  {
    startBurst(AlertPattern::Reminder);
    return;
  }

  // Success / Warning ดังครั้งเดียวแล้วเงียบ
  if (current == AlertPattern::Success || current == AlertPattern::Warning)
    current = AlertPattern::None;
}
