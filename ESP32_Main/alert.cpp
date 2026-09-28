#include "alert.h"
#include "config.h"

namespace {

// ---------------------------------------------------------------------------
// ทำนอง
// ---------------------------------------------------------------------------
//
// passive buzzer เล่นได้หลายระดับเสียง จึงใช้โน้ตไล่ตามคอร์ดเมเจอร์ให้ฟังเป็นเสียงกริ่งที่คุ้นหู
// แทนเสียงปี๊บแหลมโทนเดียวแบบสัญญาณเตือนภัย ผู้สูงอายุได้ยินชัดแต่ไม่ตกใจ
// ทุกโน้ตอยู่ช่วง 1.7-3.2 kHz ที่ passive buzzer ดังชัดที่สุด

struct Note {
  uint16_t hz;  // 0 = เงียบ (ช่วงหยุดระหว่างโน้ต)
  uint16_t ms;
};

constexpr uint16_t HZ_A6 = 1760, HZ_C7 = 2093, HZ_D7 = 2349, HZ_E7 = 2637, HZ_G7 = 3136;

// ถึงเวลายา: โด-มี-ซอล ไล่ขึ้นเบาๆ ยาวราวครึ่งวินาที
constexpr Note REMINDER[] = {{HZ_C7, 120}, {0, 40}, {HZ_E7, 120}, {0, 40}, {HZ_G7, 220}};
// จ่ายยาเสร็จ: สองโน้ตไล่ขึ้น ฟังแล้วรู้ว่าเรียบร้อย
constexpr Note SUCCESS[] = {{HZ_C7, 100}, {0, 30}, {HZ_G7, 250}};
// ไม่สำเร็จหรือถูกปฏิเสธ: สองโน้ตไล่ลง ต่างจากเสียงสำเร็จชัด แต่ไม่แหลมไม่รัว
constexpr Note WARNING[] = {{HZ_D7, 180}, {0, 60}, {HZ_A6, 320}};
// ตอบรับการกดปุ่ม
constexpr Note CLICK[] = {{HZ_E7, 40}};

constexpr unsigned long lengthOf(const Note *notes, size_t count)
{
  return count == 0 ? 0 : notes[0].ms + lengthOf(notes + 1, count - 1);
}
static_assert(lengthOf(REMINDER, sizeof(REMINDER) / sizeof(REMINDER[0])) + 300 < ALERT_REMINDER_URGENT_PERIOD_MS,
              "the reminder chime needs a clear pause before the next one, even when escalated");

struct Melody {
  const Note *notes;
  uint8_t count;
};

template <size_t N>
constexpr Melody melody(const Note (&notes)[N])
{
  return {notes, static_cast<uint8_t>(N)};
}

Melody melodyFor(AlertPattern pattern)
{
  switch (pattern)
  {
    case AlertPattern::Reminder: return melody(REMINDER);
    case AlertPattern::Success: return melody(SUCCESS);
    case AlertPattern::Warning: return melody(WARNING);
    case AlertPattern::Click: return melody(CLICK);
    case AlertPattern::None: break;
  }
  return {nullptr, 0};
}

// ---------------------------------------------------------------------------
// สถานะ
// ---------------------------------------------------------------------------

AlertPattern current = AlertPattern::None;  // รูปแบบค้าง (Reminder) หรือเสียงครั้งเดียวที่สั่งผ่าน alertSet
AlertPattern oneShot = AlertPattern::None;  // เสียงตอบรับที่แทรกอยู่ เล่นจบแล้วกลับไปใช้ current

Melody playing = {nullptr, 0};
uint8_t noteIndex = 0;
unsigned long noteStartedMs = 0;
unsigned long lastReminderMs = 0;    // กริ่งเตือนชุดล่าสุดเริ่มเมื่อไร
unsigned long remindingSinceMs = 0;  // เริ่มเตือนมื้อนี้เมื่อไร ใช้ตัดสินว่าถึงเวลาถี่ขึ้นหรือยัง

// passive buzzer ต้องได้คลื่นความถี่ถึงจะมีเสียง จึงขับด้วย PWM duty 50% บนช่อง LEDC ที่จองไว้ (ดู config.h)
bool toneAttached = false;
uint32_t toneHz = 0;
constexpr uint32_t BUZZER_TONE_DUTY = 1UL << (BUZZER_PWM_RESOLUTION_BITS - 1);
constexpr uint32_t BUZZER_FULL_DUTY = (1UL << BUZZER_PWM_RESOLUTION_BITS) - 1;  // ledcWrite ตีความเป็นเปิดค้าง

/** ดังที่ความถี่ hz หรือเงียบ (hz = 0) active buzzer ไม่สนความถี่ แค่เปิด-ปิด */
void writeBuzzer(uint32_t hz)
{
  const bool on = hz != 0;
  if (toneAttached)
  {
    // เปลี่ยนความถี่เฉพาะ timer ของ buzzer เอง (ความละเอียดไม่ซ้ำใคร จึงไม่มีช่องอื่นใช้ timer ร่วม)
    if (on && hz != toneHz && ledcChangeFrequency(BUZZER_PIN, hz, BUZZER_PWM_RESOLUTION_BITS) != 0)
      toneHz = hz;
    // เงียบ = ค้างที่ระดับ "ไม่ดัง" ของโมดูล ไม่ปล่อยให้ทรานซิสเตอร์ในโมดูลนำไฟค้างระหว่างรอ
    const uint32_t silent = BUZZER_ACTIVE_HIGH ? 0 : BUZZER_FULL_DUTY;
    ledcWrite(BUZZER_PIN, on ? BUZZER_TONE_DUTY : silent);
    return;
  }
  digitalWrite(BUZZER_PIN, (on == BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
}

bool isPlaying()
{
  return playing.notes != nullptr;
}

void stopPlaying()
{
  playing = {nullptr, 0};
  writeBuzzer(0);
}

void playCurrentNote(unsigned long now)
{
  noteStartedMs = now;
  writeBuzzer(playing.notes[noteIndex].hz);
}

/** เริ่มเล่นทำนองของรูปแบบนั้นตั้งแต่โน้ตแรก */
void startMelody(AlertPattern pattern)
{
  const unsigned long now = millis();
  playing = melodyFor(pattern);
  noteIndex = 0;
  if (pattern == AlertPattern::Reminder)
    lastReminderMs = now;
  if (playing.count == 0)
  {
    stopPlaying();
    return;
  }
  playCurrentNote(now);
}

unsigned long reminderPeriod(unsigned long now)
{
  return now - remindingSinceMs >= ALERT_ESCALATE_AFTER_MS ? ALERT_REMINDER_URGENT_PERIOD_MS
                                                           : ALERT_REMINDER_PERIOD_MS;
}
}  // namespace

void alertBegin()
{
  // ผูก PWM ครั้งเดียว: pinMode() บนขาที่ผูก PWM แล้วจะถอดขาออกจาก PWM (core 3.x) เสียงจะหายเงียบ
  if (BUZZER_PASSIVE && !toneAttached)
  {
    toneAttached =
        ledcAttachChannel(BUZZER_PIN, BUZZER_TONE_HZ, BUZZER_PWM_RESOLUTION_BITS, BUZZER_LEDC_CHANNEL);
    toneHz = BUZZER_TONE_HZ;
    if (!toneAttached)
      Serial.printf("[buzzer] ผูกช่อง PWM %u ไม่สำเร็จ passive buzzer จะไม่มีเสียงเตือน\n",
                    static_cast<unsigned>(BUZZER_LEDC_CHANNEL));
  }
  if (!toneAttached)
    pinMode(BUZZER_PIN, OUTPUT);
  writeBuzzer(0);

  // ตรวจสายตอนบูต: ใช้ delay ได้เพราะอยู่ใน setup() ยังไม่มีอะไรต้องเดินพร้อมกัน
  if (BUZZER_BOOT_CHIRP_MS > 0)
  {
    writeBuzzer(BUZZER_TONE_HZ);
    delay(BUZZER_BOOT_CHIRP_MS);
    writeBuzzer(0);
  }
  current = AlertPattern::None;
  oneShot = AlertPattern::None;
  playing = {nullptr, 0};
}

void alertSet(AlertPattern pattern)
{
  if (current == pattern)
    return;

  current = pattern;
  if (pattern == AlertPattern::Reminder)
    remindingSinceMs = millis();

  // เสียงตอบรับที่แทรกอยู่ให้เล่นจนจบ แล้ว alertUpdate จะเปลี่ยนไปใช้รูปแบบใหม่เอง
  if (oneShot != AlertPattern::None)
    return;
  // ผู้ใช้ตอบรับแล้ว (หรือเลิกเตือน): เงียบทันที ไม่ต้องรอกริ่งชุดที่กำลังเล่นจบ
  if (pattern == AlertPattern::None)
  {
    stopPlaying();
    return;
  }
  if (!isPlaying())
    startMelody(pattern);
}

void alertOneShot(AlertPattern pattern)
{
  oneShot = pattern;
  startMelody(pattern);
}

void alertUpdate()
{
  const unsigned long now = millis();

  if (isPlaying())
  {
    if (now - noteStartedMs < playing.notes[noteIndex].ms)
      return;
    if (++noteIndex < playing.count)
    {
      playCurrentNote(now);
      return;
    }
    stopPlaying();
  }

  if (oneShot != AlertPattern::None)
  {
    // เล่นเสียงตอบรับจบแล้ว กลับไปใช้รูปแบบเดิม
    oneShot = AlertPattern::None;
    if (current != AlertPattern::None)
      startMelody(current);
    return;
  }

  if (current == AlertPattern::Reminder && now - lastReminderMs >= reminderPeriod(now))
  {
    startMelody(AlertPattern::Reminder);
    return;
  }

  // Success / Warning ดังครั้งเดียวแล้วเงียบ
  if (current == AlertPattern::Success || current == AlertPattern::Warning)
    current = AlertPattern::None;
}
