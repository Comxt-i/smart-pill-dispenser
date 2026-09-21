#include "config.h"
#include "dispenser_control.h"
#include <ESP32Servo.h>
#include <climits>

unsigned long fakeMillis = 0;
int pinLevel[64];
std::vector<PinWrite> digitalWrites;
std::vector<PinWrite> analogWrites;
FakeSerial Serial;
std::vector<Pulse> pulses;
int attachedCount = 0;
bool failAttach = false;

namespace {

void setCancel(bool pressed) { pinLevel[CANCEL_BUTTON_PIN] = pressed ? LOW : HIGH; }

/** เดินเวลาไปข้างหน้าแล้วให้ตัวคุมทำงานหนึ่งครั้ง */
void advance(unsigned long ms)
{
  fakeMillis += ms;
  dispenserControlUpdate();
}

/**
 * จำลองเม็ดยาหนึ่งเม็ดตกผ่านลำแสง
 *
 * ต้องขยับเวลาให้พ้น PILL_DETECT_LOCKOUT_MS ก่อน ไม่อย่างนั้นจะถูกกลไกกันนับซ้ำกลืนไป
 * ซึ่งเป็นพฤติกรรมที่ถูกต้อง และมีเทสต์แยกครอบไว้ด้านล่าง
 */
void dropPill(uint8_t unit)
{
  const uint8_t pin = PILL_SENSOR_PINS[unit - 1];
  pinLevel[pin] = PILL_SENSOR_ACTIVE_LOW ? LOW : HIGH;
  dispenserControlUpdate();
  pinLevel[pin] = PILL_SENSOR_ACTIVE_LOW ? HIGH : LOW;
  dispenserControlUpdate();
}

/** ค่าที่ถูกเขียนล่าสุดลงขา PWM ของมอเตอร์สั่นของจานนั้น */
int lastVibrationPwm(uint8_t unit)
{
  const uint8_t pin = VIB_PWM_PINS[unit - 1];
  for (size_t i = analogWrites.size(); i > 0; --i)
  {
    if (analogWrites[i - 1].pin == pin)
      return analogWrites[i - 1].value;
  }
  return -1;
}

/** ค่าที่ถูกเขียนล่าสุดลงขาทิศทางของมอเตอร์สั่นของจานนั้น */
int lastVibrationDir(uint8_t unit)
{
  const uint8_t pin = VIB_DIR_PINS[unit - 1];
  for (size_t i = digitalWrites.size(); i > 0; --i)
  {
    if (digitalWrites[i - 1].pin == pin)
      return digitalWrites[i - 1].value;
  }
  return -1;
}

}  // namespace

int main()
{
  DispenseOutcome outcome = {0, 0, 0, 0, false, false};

  resetPins();
  dispenserControlBegin();

  assert(attachedCount == 0 && pulses.empty());
  assert(!dispenserIsBusy());
  assert(!takeDispenseOutcome(outcome));  // ยังไม่เคยจ่าย จึงยังไม่มีผลให้อ่าน

  assert(dispenseMedicine(0, 1) == DispenseResult::Invalid);
  assert(dispenseMedicine(DISPENSER_COUNT + 1, 1) == DispenseResult::Invalid);
  assert(dispenseMedicine(1, 0) == DispenseResult::Invalid);
  assert(dispenseMedicine(1, MAX_PILLS_PER_DOSE + 1) == DispenseResult::Invalid);

  setCancel(true);
  assert(dispenseMedicine(1, 1) == DispenseResult::Cancelled);
  setCancel(false);

  if (!ENABLE_SERVO_MOVEMENT)
  {
    assert(dispenseMedicine(1, 1) == DispenseResult::Disabled);
    advance(MOVE_TIME_MS);
    assert(attachedCount == 0 && pulses.empty());
    assert(!dispenserIsBusy());
    assert(!takeDispenseOutcome(outcome));  // คำสั่งที่ถูกปฏิเสธต้องไม่ถูกรายงานว่าจ่ายแล้ว
    return 0;
  }

  // -------------------------------------------------------------------------
  // เม็ดยาตกตั้งแต่รอบแรก: ต้องจบทันทีที่จานกลับถึงตำแหน่งพัก ไม่ต้องเขย่าต่อ
  // -------------------------------------------------------------------------
  for (uint8_t unit = 1; unit <= DISPENSER_COUNT; ++unit)
  {
    pulses.clear();
    analogWrites.clear();
    digitalWrites.clear();

    assert(dispenseMedicine(unit, 1) == DispenseResult::Started);
    assert(attachedCount == 1 && dispenserIsBusy());
    assert(dispenseMedicine(1, 1) == DispenseResult::Busy);

    // ช่วงหมุนออกต้องสั่นอยู่ (กระตุกเต็มกำลังก่อน)
    assert(pulses.size() == 1 && pulses[0].value == RELEASE_PULSE_US[unit - 1]);
    assert(lastVibrationPwm(unit) == VIB_KICK_SPEED);
    assert(lastVibrationDir(unit) == LOW);

    // ผ่อนจากจังหวะกระตุกลงมาที่ความแรงปกติโดยไม่ต้องรอครบ MOVE_TIME_MS
    advance(VIB_KICK_MS);
    assert(lastVibrationPwm(unit) == VIB_SPEED);

    if (ENABLE_PILL_SENSOR)
      dropPill(unit);

    // ยังไม่ครบเวลาเดินทาง ห้ามเปลี่ยนช่วง แม้จะนับเม็ดครบแล้วก็ตาม
    assert(pulses.size() == 1);
    assert(dispenserIsBusy());

    advance(MOVE_TIME_MS);  // จบ Releasing -> Returning
    assert(pulses.size() == 2 && pulses[1].value == REST_PULSE_US[unit - 1]);
    // ขากลับต้องเบรกล็อกแกน: IN1 = PWM เต็ม และ IN2 = HIGH
    assert(lastVibrationPwm(unit) == 255 && lastVibrationDir(unit) == HIGH);

    advance(MOVE_TIME_MS);  // จบ Returning -> ครบแล้วจึงจบงาน ไม่เข้า Shaking

    if (ENABLE_PILL_SENSOR)
    {
      assert(!dispenserIsBusy());
      assert(attachedCount == 0);
      assert(lastVibrationPwm(unit) == 0);  // ดับมอเตอร์เมื่อจบงาน

      assert(takeDispenseOutcome(outcome));
      assert(outcome.dispenser == unit);
      assert(outcome.requestedPills == 1 && outcome.dispensedPills == 1);
      assert(outcome.attempts == 1);
      assert(!outcome.cancelled && outcome.sensorVerified);
      assert(!takeDispenseOutcome(outcome));  // ผลหนึ่งรอบอ่านได้ครั้งเดียว

      assert(pulses.size() == 2);  // หนึ่งรอบ = ออกหนึ่งครั้ง กลับหนึ่งครั้ง
      assert(pulses[0].pin == SERVO_PINS[unit - 1]);
      assert(pulses[1].pin == SERVO_PINS[unit - 1]);
    }
    else
    {
      stopDispenser();
      (void)takeDispenseOutcome(outcome);
    }
  }

  if (ENABLE_PILL_SENSOR)
  {
    // -----------------------------------------------------------------------
    // ไม่มีเม็ดตกเลย: ต้องวนหมุน-เขย่าจนครบเพดาน แล้วรายงานว่าได้ไม่ครบ
    // -----------------------------------------------------------------------
    pulses.clear();
    assert(dispenseMedicine(1, 2) == DispenseResult::Started);

    for (uint8_t attempt = 0; attempt < MAX_ATTEMPTS_PER_DOSE; ++attempt)
    {
      assert(dispenserIsBusy());
      const size_t before = pulses.size();

      advance(MOVE_TIME_MS);  // Releasing -> Returning
      assert(pulses.size() == before + 1);

      // ช่วงเขย่าต้องไม่สั่ง servo เพิ่ม แต่ต้องสั่นอยู่
      if (attempt + 1 < MAX_ATTEMPTS_PER_DOSE)
      {
        advance(MOVE_TIME_MS);  // Returning -> Shaking
        assert(lastVibrationPwm(1) == VIB_KICK_SPEED && lastVibrationDir(1) == LOW);
        const size_t duringShake = pulses.size();
        advance(SHAKE_TIME_MS);  // Shaking -> Releasing รอบใหม่
        assert(pulses.size() == duringShake + 1);
      }
      else
      {
        advance(MOVE_TIME_MS);  // ครบเพดานแล้ว ต้องจบตรงนี้ ไม่เข้า Shaking อีก
      }
    }

    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome));
    assert(outcome.attempts == MAX_ATTEMPTS_PER_DOSE);
    assert(outcome.requestedPills == 2 && outcome.dispensedPills == 0);
    assert(!outcome.cancelled && outcome.sensorVerified);

    // -----------------------------------------------------------------------
    // กลไกกันนับซ้ำ: เม็ดเดียวที่กระเด้งผ่านลำแสงสองครั้งติดต้องนับเป็นหนึ่ง
    // -----------------------------------------------------------------------
    assert(dispenseMedicine(1, 2) == DispenseResult::Started);
    dropPill(1);
    dropPill(1);  // อยู่ในช่วงล็อก จึงต้องไม่ถูกนับ
    advance(MOVE_TIME_MS);
    advance(MOVE_TIME_MS);
    assert(dispenserIsBusy());  // ยังได้ไม่ครบ 2 เม็ด จึงต้องวนต่อ

    // พ้นช่วงล็อกแล้วค่อยนับเม็ดที่สอง
    advance(PILL_DETECT_LOCKOUT_MS + 1);
    dropPill(1);
    advance(SHAKE_TIME_MS);
    advance(MOVE_TIME_MS);
    advance(MOVE_TIME_MS);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispensedPills == 2 && outcome.requestedPills == 2);

    // -----------------------------------------------------------------------
    // ลำแสงที่ถูกบังค้างไว้ตั้งแต่ก่อนเริ่ม ต้องไม่ถูกนับเป็นเม็ดใหม่
    // -----------------------------------------------------------------------
    pinLevel[PILL_SENSOR_PINS[0]] = PILL_SENSOR_ACTIVE_LOW ? LOW : HIGH;
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    advance(MOVE_TIME_MS);
    advance(MOVE_TIME_MS);
    assert(dispenserIsBusy());  // ไม่มีขอบขาลงใหม่ จึงยังนับไม่ได้
    stopDispenser();
    (void)takeDispenseOutcome(outcome);
    pinLevel[PILL_SENSOR_PINS[0]] = PILL_SENSOR_ACTIVE_LOW ? HIGH : LOW;
  }

  // -------------------------------------------------------------------------
  // ยกเลิกระหว่างทาง: หยุดทันที ปฏิเสธคำสั่งใหม่ขณะกดค้าง และไม่ทำต่อเมื่อปล่อย
  // -------------------------------------------------------------------------
  for (int phase = 0; phase < 3; ++phase)
  {
    assert(dispenseMedicine(2, 3) == DispenseResult::Started);
    for (int step = 0; step < phase; ++step)
      advance(MOVE_TIME_MS);

    const auto beforeCancel = pulses.size();
    setCancel(true);
    dispenserControlUpdate();

    assert(attachedCount == 0);
    assert(lastVibrationPwm(2) == 0);  // ยกเลิกแล้วต้องดับมอเตอร์ด้วย
    assert(takeDispenseOutcome(outcome));
    assert(outcome.cancelled && outcome.dispenser == 2);
    assert(outcome.dispensedPills < outcome.requestedPills);
    assert(dispenseMedicine(3, 1) == DispenseResult::Cancelled);
    assert(!takeDispenseOutcome(outcome));  // คำสั่งที่ถูกปฏิเสธไม่นับเป็นหนึ่งรอบ

    setCancel(false);
    advance(MOVE_TIME_MS);
    assert(pulses.size() == beforeCancel);
  }

  failAttach = true;
  assert(dispenseMedicine(1, 1) == DispenseResult::ServoError);
  assert(attachedCount == 0);
  failAttach = false;
  assert(!takeDispenseOutcome(outcome));  // attach ไม่สำเร็จ ไม่ใช่รอบที่เริ่มแล้ว

  assert(dispenseMedicine(1, 1) == DispenseResult::Started);
  stopDispenser();
  assert(takeDispenseOutcome(outcome) && outcome.cancelled);
  stopDispenser();                        // หยุดตอนที่ว่างอยู่ต้องไม่เป็นอันตราย
  assert(!takeDispenseOutcome(outcome));  // และต้องไม่สร้างผลหลอก
  assert(attachedCount == 0);

  // -------------------------------------------------------------------------
  // การคำนวณเวลาต้องทนการวนกลับของ millis()
  // -------------------------------------------------------------------------
  pulses.clear();
  fakeMillis = ULONG_MAX - MOVE_TIME_MS / 2;
  assert(dispenseMedicine(3, 1) == DispenseResult::Started);
  if (ENABLE_PILL_SENSOR)
    dropPill(3);
  advance(MOVE_TIME_MS);
  assert(pulses.size() == 2 && attachedCount == 1);
  advance(MOVE_TIME_MS);

  if (ENABLE_PILL_SENSOR)
  {
    assert(attachedCount == 0);
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 3 && outcome.dispensedPills == 1 && !outcome.cancelled);
  }

  return 0;
}
