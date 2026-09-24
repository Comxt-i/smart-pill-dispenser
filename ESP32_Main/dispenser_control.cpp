#include "dispenser_control.h"
#include "config.h"

#include <ESP32Servo.h>

namespace {

/**
 * ลำดับหนึ่งรอบ (จากที่ทดสอบกับกลไกจริงแล้วได้ผลดีที่สุด):
 *
 *   Releasing : หมุนไปตำแหน่งจ่าย พร้อมสั่น      -> กวาดยาให้ไหลลงรู
 *   Returning : หมุนกลับตำแหน่งพัก โดยเบรกมอเตอร์ -> ล็อกแกนไม่ให้ยาตกเกิน
 *   Shaking   : อยู่นิ่งแล้วสั่นค้าง               -> สะบัดเม็ดที่ค้างอยู่ปากรูให้หลุดลง
 *
 * แล้ววนใหม่จนเซ็นเซอร์ IR นับเม็ดได้ครบ หรือจนครบ MAX_ATTEMPTS_PER_DOSE
 *
 * จังหวะ Shaking จำเป็น เพราะบางครั้งเม็ดยาหลุดจากจานแล้วแต่ยังค้างอยู่ด้านล่าง
 * การหมุนอย่างเดียวไม่ทำให้มันตกลงไป
 *
 * WaitingStart ใช้เหลื่อมจังหวะออกตัวเมื่อมีจานอื่นกำลังทำงานอยู่แล้ว
 */
enum class Phase : uint8_t { Idle, WaitingStart, Releasing, Returning, Shaking };

/** สถานะของจานหนึ่งใบ แยกกันครบทุกตัวเพื่อให้หลายจานทำงานทับเวลากันได้ */
struct Run {
  Phase phase = Phase::Idle;
  uint8_t requestedPills = 0;
  uint8_t countedPills = 0;
  uint8_t attempts = 0;
  unsigned long phaseStartedMs = 0;
  unsigned long startDelayMs = 0;
  bool targetReached = false;

  // จังหวะกระตุกมอเตอร์สั่นตอนออกตัว (ไม่ใช้ delay)
  bool kicking = false;
  unsigned long kickStartedMs = 0;

  // สถานะเซ็นเซอร์รอบก่อน ใช้จับ "ขอบ" ไม่ใช่ระดับ เม็ดเดียวจะได้ไม่ถูกนับซ้ำ
  bool sensorWasBlocked = false;
  unsigned long lastDetectMs = 0;
};

Servo dispenserServos[DISPENSER_COUNT];
Run runs[DISPENSER_COUNT];

/**
 * คิวผลลัพธ์แบบวงแหวน
 *
 * ต้องเป็นคิวไม่ใช่ตัวแปรเดี่ยว เพราะหลายจานจบพร้อมกันได้
 * ขนาดเท่าจำนวนจานจึงพอเสมอ ต่อให้ทุกจานจบในรอบ loop เดียวกัน
 */
DispenseOutcome outcomes[DISPENSER_COUNT];
uint8_t outcomeHead = 0;
uint8_t outcomeCount = 0;

void pushOutcome(const DispenseOutcome &outcome)
{
  if (outcomeCount >= DISPENSER_COUNT)
    return;  // เป็นไปไม่ได้ในทางปฏิบัติ แต่กันไว้ไม่ให้เขียนทับของที่ยังไม่ถูกอ่าน

  const uint8_t tail = static_cast<uint8_t>((outcomeHead + outcomeCount) % DISPENSER_COUNT);
  outcomes[tail] = outcome;
  ++outcomeCount;
}

/** จำนวนจานที่กำลังทำงานอยู่ รวมถึงจานที่รอคิวออกตัว */
uint8_t activeCount()
{
  uint8_t count = 0;
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    if (runs[index].phase != Phase::Idle)
      ++count;
  }
  return count;
}

// ---------------------------------------------------------------------------
// มอเตอร์สั่น (DRV8833)
// ---------------------------------------------------------------------------
//
// ทุกสถานะสั่งผ่าน analogWrite ที่ขา IN1 เสมอ ไม่สลับไปใช้ digitalWrite ที่ขาเดิม
// เพราะบน ESP32 การ analogWrite จะผูกขานั้นเข้ากับ LEDC แล้ว digitalWrite ภายหลัง
// อาจไม่มีผล ทำให้มอเตอร์ไม่ดับตามสั่ง

void vibrateOn(uint8_t index, unsigned long now)
{
  digitalWrite(VIB_DIR_PINS[index], LOW);
  analogWrite(VIB_PWM_PINS[index], VIB_KICK_SPEED);
  runs[index].kicking = true;
  runs[index].kickStartedMs = now;
}

/** IN1 = IN2 = HIGH คือเบรกแบบล็อกแกน กันตุ้มถ่วงเหวี่ยงต่อ */
void vibrateBrake(uint8_t index)
{
  runs[index].kicking = false;
  digitalWrite(VIB_DIR_PINS[index], HIGH);
  analogWrite(VIB_PWM_PINS[index], 255);
}

void vibrateOff(uint8_t index)
{
  runs[index].kicking = false;
  digitalWrite(VIB_DIR_PINS[index], LOW);
  analogWrite(VIB_PWM_PINS[index], 0);
}

/** ผ่อนกำลังจากจังหวะกระตุกลงมาที่ความแรงปกติ */
void tickKick(uint8_t index, unsigned long now)
{
  Run &run = runs[index];
  if (!run.kicking)
    return;
  if (now - run.kickStartedMs < VIB_KICK_MS)
    return;

  analogWrite(VIB_PWM_PINS[index], VIB_SPEED);
  run.kicking = false;
}

// ---------------------------------------------------------------------------
// เซ็นเซอร์ IR
// ---------------------------------------------------------------------------

bool readSensor(uint8_t index)
{
  const int level = digitalRead(PILL_SENSOR_PINS[index]);
  return PILL_SENSOR_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

/**
 * นับเม็ดที่เพิ่งตกผ่านลำแสงของจานนี้
 *
 * จับเฉพาะขอบขาลง (ว่าง -> ถูกบัง) และล็อกไว้ PILL_DETECT_LOCKOUT_MS
 * เพื่อไม่ให้เม็ดเดียวที่กระเด้งหรือหมุนตัวถูกนับหลายครั้ง
 *
 * ทุกตัวนับแยกรายจาน จานที่ทำงานพร้อมกันจึงไม่กวนกัน
 */
void pollSensor(uint8_t index, unsigned long now)
{
  if (!ENABLE_PILL_SENSOR)
    return;

  Run &run = runs[index];
  const bool blocked = readSensor(index);
  const bool wasBlocked = run.sensorWasBlocked;
  run.sensorWasBlocked = blocked;

  if (!blocked || wasBlocked)
    return;
  if (now - run.lastDetectMs < PILL_DETECT_LOCKOUT_MS)
    return;

  run.lastDetectMs = now;
  if (run.countedPills < 255)
    ++run.countedPills;

  Serial.printf("[IR] จาน %u ตรวจพบเม็ดที่ %u/%u\n",
                static_cast<unsigned>(index + 1),
                static_cast<unsigned>(run.countedPills),
                static_cast<unsigned>(run.requestedPills));

  if (run.countedPills >= run.requestedPills)
    run.targetReached = true;
}

// ---------------------------------------------------------------------------

/** ปิดรอบการทำงานของจานหนึ่งและเก็บผลเข้าคิวให้ loop หลักมาอ่าน */
void finishRun(uint8_t index, bool cancelled)
{
  Run &run = runs[index];
  if (run.phase == Phase::Idle)
    return;

  vibrateOff(index);
  if (dispenserServos[index].attached())
    dispenserServos[index].detach();
  run.phase = Phase::Idle;

  DispenseOutcome outcome;
  outcome.dispenser = static_cast<uint8_t>(index + 1);
  outcome.requestedPills = run.requestedPills;
  outcome.attempts = run.attempts;
  outcome.cancelled = cancelled;
  outcome.sensorVerified = ENABLE_PILL_SENSOR;

  // ไม่มีเซ็นเซอร์ = เชื่อว่าหมุนครบรอบแล้วยาออกครบ ซึ่งยืนยันไม่ได้
  // sensorVerified บอก pill_app ให้ติดป้ายไว้ในบันทึกว่าไม่ได้ตรวจจริง
  outcome.dispensedPills =
      ENABLE_PILL_SENSOR ? run.countedPills : (cancelled ? 0 : run.requestedPills);

  pushOutcome(outcome);
}

void beginPhase(uint8_t index, Phase next, unsigned long now)
{
  Servo &servo = dispenserServos[index];
  Run &run = runs[index];

  switch (next)
  {
    case Phase::Releasing:
      ++run.attempts;
      vibrateOn(index, now);
      servo.writeMicroseconds(RELEASE_PULSE_US[index]);
      break;

    case Phase::Returning:
      vibrateBrake(index);
      servo.writeMicroseconds(REST_PULSE_US[index]);
      break;

    case Phase::Shaking:
      // จานอยู่ที่ตำแหน่งพักแล้ว ไม่สั่ง servo ซ้ำ ให้สั่นอย่างเดียว
      vibrateOn(index, now);
      break;

    case Phase::WaitingStart:
    case Phase::Idle:
      break;
  }

  run.phase = next;
  run.phaseStartedMs = now;
}

/** เดินสถานะของจานหนึ่งใบ */
void updateRun(uint8_t index, unsigned long now)
{
  Run &run = runs[index];
  if (run.phase == Phase::Idle)
    return;

  tickKick(index, now);
  pollSensor(index, now);

  const unsigned long elapsed = now - run.phaseStartedMs;

  switch (run.phase)
  {
    case Phase::WaitingStart:
      if (elapsed >= run.startDelayMs)
        beginPhase(index, Phase::Releasing, now);
      return;

    case Phase::Releasing:
      if (elapsed < MOVE_TIME_MS)
        return;
      // ถึงปลายทางแล้ว ต้องพาจานกลับตำแหน่งพักเสมอ แม้จะได้เม็ดครบแล้วก็ตาม
      beginPhase(index, Phase::Returning, now);
      return;

    case Phase::Returning:
      if (elapsed < MOVE_TIME_MS)
        return;

      if (run.targetReached)
      {
        finishRun(index, false);
        return;
      }
      if (run.attempts >= MAX_ATTEMPTS_PER_DOSE)
      {
        Serial.printf("[จ่ายยา] จาน %u หมุนครบ %u รอบแล้วยังได้ไม่ครบ (%u/%u เม็ด)\n",
                      static_cast<unsigned>(index + 1),
                      static_cast<unsigned>(run.attempts),
                      static_cast<unsigned>(run.countedPills),
                      static_cast<unsigned>(run.requestedPills));
        finishRun(index, false);
        return;
      }

      beginPhase(index, Phase::Shaking, now);
      return;

    case Phase::Shaking:
      if (elapsed < SHAKE_TIME_MS)
        return;

      // เม็ดอาจหลุดตอนเขย่านี่เอง จึงต้องเช็คอีกครั้งก่อนหมุนรอบใหม่
      if (run.targetReached)
      {
        finishRun(index, false);
        return;
      }
      beginPhase(index, Phase::Releasing, now);
      return;

    case Phase::Idle:
      return;
  }
}

}  // namespace

void dispenserControlBegin()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    finishRun(index, false);
    dispenserServos[index].setPeriodHertz(50);

    pinMode(VIB_PWM_PINS[index], OUTPUT);
    pinMode(VIB_DIR_PINS[index], OUTPUT);
    vibrateOff(index);

    // GPIO34-39 เป็นขาอินพุตอย่างเดียวและ **ไม่มี pull-up ในตัวชิป**
    // โมดูล IR ต้องขับสัญญาณเองแบบ push-pull ไม่อย่างนั้นต้องใส่ตัวต้านทาน pull-up ภายนอก
    pinMode(PILL_SENSOR_PINS[index], INPUT);
    runs[index].sensorWasBlocked = ENABLE_PILL_SENSOR ? readSensor(index) : false;
  }

  outcomeHead = 0;
  outcomeCount = 0;
}

DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t pills)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT || pills < 1 || pills > MAX_PILLS_PER_DOSE)
    return DispenseResult::Invalid;
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
    return DispenseResult::Cancelled;
  if (!ENABLE_SERVO_MOVEMENT)
    return DispenseResult::Disabled;

  const uint8_t index = static_cast<uint8_t>(dispenser - 1);
  if (runs[index].phase != Phase::Idle)
    return DispenseResult::Busy;

  // เต็มเพดานแล้ว ผู้เรียกต้องลองใหม่เมื่อมีจานว่าง
  const uint8_t running = activeCount();
  if (running >= MAX_CONCURRENT_DISPENSERS)
    return DispenseResult::Busy;

  Servo &servo = dispenserServos[index];
  servo.attach(SERVO_PINS[index], SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
  if (!servo.attached())
    return DispenseResult::ServoError;

  Run &run = runs[index];
  run.requestedPills = pills;
  run.countedPills = 0;
  run.attempts = 0;
  run.targetReached = false;

  const unsigned long now = millis();

  // ช่วงกันนับซ้ำมีไว้กันเม็ดเดียวถูกนับหลายครั้ง จึงต้องนับจากเม็ดก่อนหน้า "ในรอบนี้"
  // ถ้าปล่อยให้ค้างจากรอบก่อน (หรือค้างที่ 0 ตอนเพิ่งบูต) เม็ดแรกของรอบจะถูกกลืนหายไป
  run.lastDetectMs = now - PILL_DETECT_LOCKOUT_MS - 1;

  // อ่านสถานะเริ่มต้นไว้ ไม่งั้นถ้าลำแสงถูกบังค้างอยู่ตั้งแต่ต้น
  // ขอบขาลงแรกจะถูกนับทั้งที่ไม่มีเม็ดยาตกใหม่
  if (ENABLE_PILL_SENSOR)
    run.sensorWasBlocked = readSensor(index);

  if (running == 0)
  {
    beginPhase(index, Phase::Releasing, now);
  }
  else
  {
    // มีจานอื่นออกตัวไปแล้ว เหลื่อมจังหวะไม่ให้กระแสพุ่งซ้อนกัน
    run.startDelayMs = DISPENSE_STAGGER_MS;
    beginPhase(index, Phase::WaitingStart, now);
  }

  return DispenseResult::Started;
}

void dispenserControlUpdate()
{
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
  {
    stopDispenser();
    return;
  }

  const unsigned long now = millis();
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
    updateRun(index, now);
}

void stopDispenser()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
    finishRun(index, true);
}

bool dispenserIsBusy()
{
  return activeCount() > 0;
}

bool dispenserHasCapacity()
{
  return activeCount() < MAX_CONCURRENT_DISPENSERS;
}

bool takeDispenseOutcome(DispenseOutcome &outcome)
{
  if (outcomeCount == 0)
    return false;

  outcome = outcomes[outcomeHead];
  outcomeHead = static_cast<uint8_t>((outcomeHead + 1) % DISPENSER_COUNT);
  --outcomeCount;
  return true;
}

bool pillSensorBlocked(uint8_t dispenser)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT)
    return false;
  return readSensor(static_cast<uint8_t>(dispenser - 1));
}
