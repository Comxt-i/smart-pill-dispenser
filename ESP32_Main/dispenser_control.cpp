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
 */
enum class Phase { Idle, Releasing, Returning, Shaking };

Servo dispenserServos[DISPENSER_COUNT];
Phase phase = Phase::Idle;

uint8_t activeIndex = 0;
uint8_t requestedPills = 0;
uint8_t countedPills = 0;
uint8_t attempts = 0;
unsigned long phaseStartedMs = 0;

// true เมื่อได้เม็ดครบแล้ว เหลือแค่พาจานกลับตำแหน่งพักให้เรียบร้อยก่อนจบ
bool targetReached = false;

// จังหวะกระตุกมอเตอร์สั่นตอนออกตัว (ไม่ใช้ delay)
bool kicking = false;
unsigned long kickStartedMs = 0;

// สถานะเซ็นเซอร์รอบก่อน ใช้จับ "ขอบ" ไม่ใช่ระดับ เม็ดเดียวจะได้ไม่ถูกนับซ้ำ
bool sensorWasBlocked[DISPENSER_COUNT] = {false};
unsigned long lastDetectMs = 0;

DispenseOutcome lastOutcome = {0, 0, 0, 0, false, false};
bool outcomePending = false;

// ---------------------------------------------------------------------------
// มอเตอร์สั่น (DRV8833)
// ---------------------------------------------------------------------------
//
// ทุกสถานะสั่งผ่าน analogWrite ที่ขา IN1 เสมอ ไม่สลับไปใช้ digitalWrite ที่ขาเดิม
// เพราะบน ESP32 การ analogWrite จะผูกขานั้นเข้ากับ LEDC แล้ว digitalWrite ภายหลัง
// อาจไม่มีผล ทำให้มอเตอร์ไม่ดับตามสั่ง

void vibrateOn(uint8_t index)
{
  digitalWrite(VIB_DIR_PINS[index], LOW);
  analogWrite(VIB_PWM_PINS[index], VIB_KICK_SPEED);
  kicking = true;
  kickStartedMs = millis();
}

/** IN1 = IN2 = HIGH คือเบรกแบบล็อกแกน กันตุ้มถ่วงเหวี่ยงต่อ */
void vibrateBrake(uint8_t index)
{
  kicking = false;
  digitalWrite(VIB_DIR_PINS[index], HIGH);
  analogWrite(VIB_PWM_PINS[index], 255);
}

void vibrateOff(uint8_t index)
{
  kicking = false;
  digitalWrite(VIB_DIR_PINS[index], LOW);
  analogWrite(VIB_PWM_PINS[index], 0);
}

/** ผ่อนกำลังจากจังหวะกระตุกลงมาที่ความแรงปกติ */
void tickKick()
{
  if (!kicking)
    return;
  if (millis() - kickStartedMs < VIB_KICK_MS)
    return;

  analogWrite(VIB_PWM_PINS[activeIndex], VIB_SPEED);
  kicking = false;
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
 * นับเม็ดที่เพิ่งตกผ่านลำแสง
 *
 * จับเฉพาะขอบขาลง (ว่าง -> ถูกบัง) และล็อกไว้ PILL_DETECT_LOCKOUT_MS
 * เพื่อไม่ให้เม็ดเดียวที่กระเด้งหรือหมุนตัวถูกนับหลายครั้ง
 */
void pollSensor()
{
  if (!ENABLE_PILL_SENSOR)
    return;

  const bool blocked = readSensor(activeIndex);
  const bool wasBlocked = sensorWasBlocked[activeIndex];
  sensorWasBlocked[activeIndex] = blocked;

  if (!blocked || wasBlocked)
    return;
  if (millis() - lastDetectMs < PILL_DETECT_LOCKOUT_MS)
    return;

  lastDetectMs = millis();
  if (countedPills < 255)
    ++countedPills;

  Serial.printf("[IR] จาน %u ตรวจพบเม็ดที่ %u/%u\n",
                static_cast<unsigned>(activeIndex + 1),
                static_cast<unsigned>(countedPills),
                static_cast<unsigned>(requestedPills));

  if (countedPills >= requestedPills)
    targetReached = true;
}

// ---------------------------------------------------------------------------

void detachAll()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    if (dispenserServos[index].attached())
      dispenserServos[index].detach();
  }
}

/** ปิดรอบการทำงานปัจจุบันและเก็บผลไว้ให้ loop หลักมาอ่าน */
void finishRun(bool cancelled)
{
  const bool wasRunning = phase != Phase::Idle;

  if (wasRunning)
    vibrateOff(activeIndex);
  detachAll();
  phase = Phase::Idle;

  if (!wasRunning)
    return;

  lastOutcome.dispenser = static_cast<uint8_t>(activeIndex + 1);
  lastOutcome.requestedPills = requestedPills;
  lastOutcome.attempts = attempts;
  lastOutcome.cancelled = cancelled;
  lastOutcome.sensorVerified = ENABLE_PILL_SENSOR;

  // ไม่มีเซ็นเซอร์ = เชื่อว่าหมุนครบรอบแล้วยาออกครบ ซึ่งยืนยันไม่ได้
  // sensorVerified บอก pill_app ให้ติดป้ายไว้ในบันทึกว่าไม่ได้ตรวจจริง
  lastOutcome.dispensedPills =
      ENABLE_PILL_SENSOR ? countedPills : (cancelled ? 0 : requestedPills);

  outcomePending = true;
}

void beginPhase(Phase next)
{
  Servo &servo = dispenserServos[activeIndex];

  switch (next)
  {
    case Phase::Releasing:
      ++attempts;
      vibrateOn(activeIndex);
      servo.writeMicroseconds(RELEASE_PULSE_US[activeIndex]);
      break;

    case Phase::Returning:
      vibrateBrake(activeIndex);
      servo.writeMicroseconds(REST_PULSE_US[activeIndex]);
      break;

    case Phase::Shaking:
      // จานอยู่ที่ตำแหน่งพักแล้ว ไม่สั่ง servo ซ้ำ ให้สั่นอย่างเดียว
      vibrateOn(activeIndex);
      break;

    case Phase::Idle:
      break;
  }

  phase = next;
  phaseStartedMs = millis();
}

}  // namespace

void dispenserControlBegin()
{
  finishRun(false);
  outcomePending = false;

  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    dispenserServos[index].setPeriodHertz(50);

    pinMode(VIB_PWM_PINS[index], OUTPUT);
    pinMode(VIB_DIR_PINS[index], OUTPUT);
    vibrateOff(index);

    // GPIO34-39 เป็นขาอินพุตอย่างเดียวและ **ไม่มี pull-up ในตัวชิป**
    // โมดูล IR ต้องขับสัญญาณเองแบบ push-pull ไม่อย่างนั้นต้องใส่ตัวต้านทาน pull-up ภายนอก
    pinMode(PILL_SENSOR_PINS[index], INPUT);
    sensorWasBlocked[index] = ENABLE_PILL_SENSOR ? readSensor(index) : false;
  }
}

DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t pills)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT || pills < 1 || pills > MAX_PILLS_PER_DOSE)
    return DispenseResult::Invalid;
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
    return DispenseResult::Cancelled;
  if (!ENABLE_SERVO_MOVEMENT)
    return DispenseResult::Disabled;
  if (phase != Phase::Idle)
    return DispenseResult::Busy;

  activeIndex = dispenser - 1;
  Servo &servo = dispenserServos[activeIndex];
  servo.attach(SERVO_PINS[activeIndex], SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
  if (!servo.attached())
    return DispenseResult::ServoError;

  requestedPills = pills;
  countedPills = 0;
  attempts = 0;
  targetReached = false;

  // ช่วงกันนับซ้ำมีไว้กันเม็ดเดียวถูกนับหลายครั้ง จึงต้องนับจากเม็ดก่อนหน้า "ในรอบนี้"
  // ถ้าปล่อยให้ค้างจากรอบก่อน (หรือค้างที่ 0 ตอนเพิ่งบูต) เม็ดแรกของรอบจะถูกกลืนหายไป
  lastDetectMs = millis() - PILL_DETECT_LOCKOUT_MS - 1;

  // อ่านสถานะเริ่มต้นไว้ ไม่งั้นถ้าลำแสงถูกบังค้างอยู่ตั้งแต่ต้น
  // ขอบขาลงแรกจะถูกนับทั้งที่ไม่มีเม็ดยาตกใหม่
  if (ENABLE_PILL_SENSOR)
    sensorWasBlocked[activeIndex] = readSensor(activeIndex);

  beginPhase(Phase::Releasing);
  return DispenseResult::Started;
}

void dispenserControlUpdate()
{
  if (phase == Phase::Idle)
    return;

  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
  {
    finishRun(true);
    return;
  }

  tickKick();
  pollSensor();

  const unsigned long elapsed = millis() - phaseStartedMs;

  switch (phase)
  {
    case Phase::Releasing:
      if (elapsed < MOVE_TIME_MS)
        return;
      // ถึงปลายทางแล้ว ต้องพาจานกลับตำแหน่งพักเสมอ แม้จะได้เม็ดครบแล้วก็ตาม
      beginPhase(Phase::Returning);
      return;

    case Phase::Returning:
      if (elapsed < MOVE_TIME_MS)
        return;

      if (targetReached)
      {
        finishRun(false);
        return;
      }
      if (attempts >= MAX_ATTEMPTS_PER_DOSE)
      {
        Serial.printf("[จ่ายยา] จาน %u หมุนครบ %u รอบแล้วยังได้ไม่ครบ (%u/%u เม็ด)\n",
                      static_cast<unsigned>(activeIndex + 1),
                      static_cast<unsigned>(attempts),
                      static_cast<unsigned>(countedPills),
                      static_cast<unsigned>(requestedPills));
        finishRun(false);
        return;
      }

      beginPhase(Phase::Shaking);
      return;

    case Phase::Shaking:
      if (elapsed < SHAKE_TIME_MS)
        return;

      // เม็ดอาจหลุดตอนเขย่านี่เอง จึงต้องเช็คอีกครั้งก่อนหมุนรอบใหม่
      if (targetReached)
      {
        finishRun(false);
        return;
      }
      beginPhase(Phase::Releasing);
      return;

    case Phase::Idle:
      return;
  }
}

void stopDispenser()
{
  finishRun(phase != Phase::Idle);
}

bool dispenserIsBusy()
{
  return phase != Phase::Idle;
}

bool takeDispenseOutcome(DispenseOutcome &outcome)
{
  if (!outcomePending)
    return false;

  outcome = lastOutcome;
  outcomePending = false;
  return true;
}

bool pillSensorBlocked(uint8_t dispenser)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT)
    return false;
  return readSensor(static_cast<uint8_t>(dispenser - 1));
}
