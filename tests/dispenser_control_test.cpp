#include "config.h"
#include "dispenser_control.h"
#include <ESP32Servo.h>
#include <climits>

#ifdef EXPECT_SERVO_MOVEMENT
// ถ้าไฟล์ตั้งค่าเครื่องไหนสักที่แอบเปลี่ยนโหมด สองรอบของเทสต์จะทดสอบโหมดเดียวกันซ้ำโดยไม่มีใครรู้
static_assert(ENABLE_SERVO_MOVEMENT == (EXPECT_SERVO_MOVEMENT != 0),
              "test is not running in the mode the driver asked for");
static_assert(ENABLE_PILL_SENSOR == (EXPECT_PILL_SENSOR != 0),
              "test is not running in the sensor mode the driver asked for");
#endif

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
 * จำลองเม็ดยาหนึ่งเม็ดตกผ่านลำแสงของจานนั้น
 *
 * ต้องขยับเวลาให้พ้น PILL_DETECT_LOCKOUT_MS ก่อนหยดเม็ดถัดไปของจานเดียวกัน
 * ไม่อย่างนั้นจะถูกกลไกกันนับซ้ำกลืนไป ซึ่งเป็นพฤติกรรมที่ถูกต้อง
 * และมีเทสต์แยกครอบไว้ด้านล่าง
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

/** จำนวนพัลส์ที่ถูกส่งไปยัง servo ของจานนั้น */
size_t pulseCountFor(uint8_t unit)
{
  size_t count = 0;
  for (size_t i = 0; i < pulses.size(); ++i)
  {
    if (pulses[i].pin == SERVO_PINS[unit - 1])
      ++count;
  }
  return count;
}

/** ตำแหน่งปล่อยยาทุกครั้งที่ servo ของจานนั้นถูกสั่ง (ตัดตำแหน่งพักออก) ตามลำดับเวลา */
std::vector<int> releasePulses(uint8_t unit)
{
  std::vector<int> out;
  for (size_t i = 0; i < pulses.size(); ++i)
  {
    if (pulses[i].pin == SERVO_PINS[unit - 1] && pulses[i].value != REST_PULSE_US[unit - 1])
      out.push_back(pulses[i].value);
  }
  return out;
}

/**
 * เดินครบรอบ ออก-กลับ-เขย่า `n` รอบโดยไม่มีเม็ดตก
 * คืนจำนวนรอบที่เดินได้จริงก่อนจานหยุด (หยุดก่อนกำหนด = ได้ค่าน้อยกว่า n)
 */
unsigned runAttemptsWithoutDrop(unsigned n)
{
  unsigned done = 0;
  for (; done < n && dispenserIsBusy(); ++done)
  {
    advance(MOVE_TIME_MS);   // Releasing -> Returning
    advance(MOVE_TIME_MS);   // Returning -> Shaking
    advance(SHAKE_TIME_MS);  // Shaking -> ตัดสินผลแล้วหมุนรอบใหม่ หรือจบ
  }
  return done;
}

void clearLog()
{
  pulses.clear();
  analogWrites.clear();
  digitalWrites.clear();
}

}  // namespace

int main()
{
  DispenseOutcome outcome = {0, 0, 0, 0, false, false};

  resetPins();
  dispenserControlBegin();

  assert(attachedCount == 0 && pulses.empty());
  assert(!dispenserIsBusy() && dispenserHasCapacity());
  assert(!takeDispenseOutcome(outcome));  // ยังไม่เคยจ่าย จึงยังไม่มีผลให้อ่าน

  // เปิดเครื่อง: ทุกจานกลับกึ่งกลาง (135 องศา) ทีละจาน แล้วปล่อยแรง
  {
    const unsigned long before = fakeMillis;
    dispenserHomeAll();
    if (ENABLE_SERVO_MOVEMENT)
    {
      assert(pulses.size() == DISPENSER_COUNT);
      for (uint8_t i = 0; i < DISPENSER_COUNT; ++i)
        assert(pulses[i].pin == SERVO_PINS[i] && pulses[i].value == REST_PULSE_US[i]);
      // ทีละจาน: ใช้เวลาเท่ากับหมุนสามครั้งต่อกัน ไม่ใช่พร้อมกัน
      assert(fakeMillis - before == DISPENSER_COUNT * MOVE_TIME_MS);
      // 135 องศาของ servo 270 องศาคือกึ่งกลางพิสัยพอดี
      assert(REST_PULSE_US[0] * 2 == SERVO_MIN_PULSE_US + SERVO_MAX_PULSE_US);
    }
    else
    {
      assert(pulses.empty());  // ปิดมอเตอร์อยู่ ห้ามขยับ
    }
    assert(attachedCount == 0);  // ปล่อยแรงแล้ว ไม่ครางค้าง
    pulses.clear();
  }

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
  // จานเดียว เม็ดตกตั้งแต่รอบแรก: จบทันทีที่กลับถึงตำแหน่งพัก ไม่ต้องเขย่าต่อ
  // -------------------------------------------------------------------------
  for (uint8_t unit = 1; unit <= DISPENSER_COUNT; ++unit)
  {
    clearLog();

    assert(dispenseMedicine(unit, 1) == DispenseResult::Started);
    assert(attachedCount == 1 && dispenserIsBusy());
    assert(dispenseMedicine(unit, 1) == DispenseResult::Busy);  // จานเดิมซ้ำ

    // ช่วงหมุนออกต้องสั่นอยู่ (กระตุกเต็มกำลังก่อน)
    // ไม่ได้กรอกขนาดยา = เริ่มที่ช่องเล็กสุด
    assert(pulses.size() == 1 && pulses[0].value == HOLE_PULSE_US[unit - 1][0]);
    assert(lastVibrationPwm(unit) == VIB_KICK_SPEED);
    assert(lastVibrationDir(unit) == LOW);

    // ผ่อนจากจังหวะกระตุกลงมาที่ความแรงปกติโดยไม่ต้องรอครบ MOVE_TIME_MS
    advance(VIB_KICK_MS);
    assert(lastVibrationPwm(unit) == VIB_SPEED);

    if (!ENABLE_PILL_SENSOR)
    {
      // ไม่มีเซ็นเซอร์: หนึ่งรอบหมุน = หนึ่งเม็ด ต้องจบใน 1 รอบ ไม่ใช่วนจนครบเพดาน
      advance(MOVE_TIME_MS);  // Releasing -> Returning
      advance(MOVE_TIME_MS);  // Returning -> จบ

      assert(!dispenserIsBusy());
      assert(takeDispenseOutcome(outcome));
      assert(outcome.dispenser == unit);
      assert(outcome.attempts == 1);  // ห้ามวนซ้ำโดยไม่จำเป็น
      assert(outcome.dispensedPills == 1 && outcome.requestedPills == 1);
      assert(!outcome.cancelled);
      assert(!outcome.sensorVerified);  // ต้องบอกว่ายืนยันด้วย IR ไม่ได้
      assert(pulses.size() == 2);
      continue;
    }

    dropPill(unit);

    // Reaching the target immediately commands return and braking.
    assert(pulses.size() == (ENABLE_PILL_SENSOR ? 2u : 1u));
    assert(dispenserIsBusy());

    if (!ENABLE_PILL_SENSOR) advance(MOVE_TIME_MS);  // Releasing -> Returning
    assert(pulses.size() == 2 && pulses[1].value == REST_PULSE_US[unit - 1]);
    // ขากลับต้องเบรกล็อกแกน: IN1 = PWM เต็ม และ IN2 = HIGH
    assert(lastVibrationPwm(unit) == 255 && lastVibrationDir(unit) == HIGH);

    advance(MOVE_TIME_MS);  // จบ Returning -> ครบแล้วจึงจบงาน ไม่เข้า Shaking

    assert(!dispenserIsBusy() && attachedCount == 0);
    assert(lastVibrationPwm(unit) == 0);  // ดับมอเตอร์เมื่อจบงาน

    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == unit);
    assert(outcome.requestedPills == 1 && outcome.dispensedPills == 1);
    assert(outcome.attempts == 1);
    assert(!outcome.cancelled && outcome.sensorVerified);
    assert(!takeDispenseOutcome(outcome));  // ผลหนึ่งรอบอ่านได้ครั้งเดียว

    assert(pulses.size() == 2);  // หนึ่งรอบ = ออกหนึ่งครั้ง กลับหนึ่งครั้ง
  }

  if (ENABLE_PILL_SENSOR && MAX_CONCURRENT_DISPENSERS >= 2)
  {
    // -----------------------------------------------------------------------
    // เดินได้พร้อมกันถึงเพดาน จานที่เกินต้องรอ
    //
    // เขียนอิงค่า MAX_CONCURRENT_DISPENSERS ตรงๆ ไม่ผูกกับเลข 2
    // ไม่อย่างนั้นถ้ามีคนไปแก้เพดาน บล็อกนี้จะถูกข้ามเงียบๆ แล้วเทสต์ผ่านแบบว่างเปล่า
    // -----------------------------------------------------------------------
    clearLog();

    uint8_t startedUnits = 0;
    for (uint8_t unit = 1; unit <= DISPENSER_COUNT; ++unit)
    {
      if (dispenseMedicine(unit, 1) == DispenseResult::Started)
        ++startedUnits;
      else
        break;  // เต็มเพดานแล้ว
    }

    assert(startedUnits == MAX_CONCURRENT_DISPENSERS);
    assert(!dispenserHasCapacity());
    assert(attachedCount == MAX_CONCURRENT_DISPENSERS);

    // จานแรกออกตัวทันที ที่เหลือต้องรอเหลื่อมจังหวะก่อน
    // ไม่อย่างนั้นกระแสพุ่งตอนออกตัวของหลาย servo จะซ้อนกันพอดี
    assert(pulseCountFor(1) == 1);
    for (uint8_t unit = 2; unit <= startedUnits; ++unit)
      assert(pulseCountFor(unit) == 0);

    // จานที่เกินเพดานต้องถูกปฏิเสธ (มีให้ทดสอบเฉพาะตอนที่เพดานน้อยกว่าจำนวนจาน)
    if (startedUnits < DISPENSER_COUNT)
      assert(dispenseMedicine(static_cast<uint8_t>(startedUnits + 1), 1) == DispenseResult::Busy);

    advance(DISPENSE_STAGGER_MS);  // ครบเวลาเหลื่อม จานที่เหลือจึงออกตัว
    for (uint8_t unit = 2; unit <= startedUnits; ++unit)
      assert(pulseCountFor(unit) == 1);
    if (startedUnits < DISPENSER_COUNT)
      assert(dispenseMedicine(static_cast<uint8_t>(startedUnits + 1), 1) == DispenseResult::Busy);

    // Each target immediately returns its own channel; finish at different times.
    dropPill(1);
    assert(pulseCountFor(1) == 2 && pulseCountFor(2) == 1);
    advance(DISPENSE_STAGGER_MS);
    dropPill(2);
    assert(pulseCountFor(2) == 2);
    advance(MOVE_TIME_MS - DISPENSE_STAGGER_MS);
    assert(dispenserIsBusy() && dispenserHasCapacity());
    assert(dispenseMedicine(3, 1) == DispenseResult::Started);
    advance(DISPENSE_STAGGER_MS); // Channel 2 finishes; channel 3 starts.

    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 1 && outcome.dispensedPills == 1 && !outcome.cancelled);
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 2 && outcome.dispensedPills == 1 && !outcome.cancelled);
    assert(!takeDispenseOutcome(outcome));

    stopDispenser();  // เก็บกวาดจานสามที่ยังค้างอยู่
    assert(takeDispenseOutcome(outcome) && outcome.dispenser == 3 && outcome.cancelled);
    assert(!dispenserIsBusy());
  }

  if (ENABLE_PILL_SENSOR)
  {
    // -----------------------------------------------------------------------
    // ช่องปล่อยยาตามขนาด: ไม่ตกเลย ต้องลองช่องละ ATTEMPTS_PER_HOLE รอบตามลำดับ
    // แล้วรายงานว่าได้ไม่ครบ
    // -----------------------------------------------------------------------
    const unsigned allHoles = PILL_HOLE_COUNT * ATTEMPTS_PER_HOLE;

    // ไม่ได้กรอกขนาด: ไล่จากเล็กไปใหญ่ 0,1,2,3
    clearLog();
    assert(dispenseMedicine(1, 2) == DispenseResult::Started);
    assert(runAttemptsWithoutDrop(allHoles) == allHoles);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome));
    assert(outcome.attempts == allHoles);
    assert(outcome.requestedPills == 2 && outcome.dispensedPills == 0);
    assert(!outcome.cancelled && outcome.sensorVerified);
    {
      const std::vector<int> released = releasePulses(1);
      assert(released.size() == allHoles);
      const uint8_t order[PILL_HOLE_COUNT] = {0, 1, 2, 3};
      for (unsigned i = 0; i < allHoles; ++i)
        assert(released[i] == HOLE_PULSE_US[0][order[i / ATTEMPTS_PER_HOLE]]);
    }

    // กรอกขนาดไว้ (ช่อง 1): ลองช่องนั้นก่อน แล้วเฉพาะช่องที่ใหญ่กว่า 2,3
    // ห้ามย้อนไปช่อง 0 ที่เล็กกว่า เม็ดที่ไม่ผ่านช่องขนาดตัวเองย่อมไม่ผ่านช่องเล็กกว่า
    clearLog();
    assert(dispenseMedicine(2, 1, 1) == DispenseResult::Started);
    assert(runAttemptsWithoutDrop(allHoles) == 3 * ATTEMPTS_PER_HOLE);
    assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 0);
    assert(outcome.attempts == 3 * ATTEMPTS_PER_HOLE);
    {
      const std::vector<int> released = releasePulses(2);
      assert(released.size() == 3 * ATTEMPTS_PER_HOLE);
      const uint8_t order[3] = {1, 2, 3};
      for (unsigned i = 0; i < released.size(); ++i)
        assert(released[i] == HOLE_PULSE_US[1][order[i / ATTEMPTS_PER_HOLE]]);
    }

    // กรอกช่องใหญ่สุดไว้: ไม่มีช่องที่ใหญ่กว่าให้ไปต่อ ลองครบ 10 รอบแล้วจบ
    clearLog();
    assert(dispenseMedicine(2, 1, 3) == DispenseResult::Started);
    assert(runAttemptsWithoutDrop(allHoles) == ATTEMPTS_PER_HOLE);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome) && outcome.attempts == ATTEMPTS_PER_HOLE);
    for (int value : releasePulses(2)) assert(value == HOLE_PULSE_US[1][3]);

    // ค่านอกช่วงจากเซิร์ฟเวอร์ต้องถือว่าไม่ได้ระบุ ห้ามอ่านเลยขอบอาร์เรย์
    // ใช้จาน 2 ซึ่งรอบก่อนจบที่ช่อง 3 ถ้าลำดับช่องว่างเปล่าแล้วไปหยิบค่าค้างจากรอบก่อน
    // จะได้ช่อง 3 ไม่ใช่ช่อง 0 เทสต์จึงจับได้ (บนจาน 1 ค่าค้างบังเอิญเป็น 0 พอดี)
    clearLog();
    assert(dispenseMedicine(2, 1, 9) == DispenseResult::Started);
    assert(releasePulses(2).size() == 1 && releasePulses(2)[0] == HOLE_PULSE_US[1][0]);
    stopDispenser();
    assert(takeDispenseOutcome(outcome) && outcome.cancelled);

    // ตกที่ช่องที่กรอกไว้ในรอบที่ 3 (ระหว่างเขย่า): หยุดทันที ไม่ย้ายช่อง
    clearLog();
    assert(dispenseMedicine(3, 1, 3) == DispenseResult::Started);
    assert(runAttemptsWithoutDrop(2) == 2);
    advance(MOVE_TIME_MS);  // Releasing -> Returning
    advance(MOVE_TIME_MS);  // Returning -> Shaking
    dropPill(3);
    advance(SHAKE_TIME_MS);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 1 && outcome.attempts == 3);
    {
      const std::vector<int> released = releasePulses(3);
      assert(released.size() == 3);
      for (size_t i = 0; i < released.size(); ++i) assert(released[i] == HOLE_PULSE_US[2][3]);
    }

    // สองเม็ด: ช่องที่กรอก (1) ไม่ตกครบ 10 รอบ -> ย้ายไปช่อง 2 ที่ใหญ่กว่า
    // ช่อง 2 พลาด 4 รอบแล้วได้เม็ดแรก จากนั้นพลาดอีก 9 รอบแล้วได้เม็ดที่สอง
    // ต้องอยู่ช่อง 2 ตลอด เพราะได้เม็ดแล้วต้องนับรอบพลาดใหม่ (4+9 ไม่ใช่ 13 ครั้งติด)
    clearLog();
    assert(dispenseMedicine(1, 2, 1) == DispenseResult::Started);
    assert(runAttemptsWithoutDrop(ATTEMPTS_PER_HOLE) == ATTEMPTS_PER_HOLE);
    assert(runAttemptsWithoutDrop(4) == 4);
    advance(MOVE_TIME_MS);
    advance(MOVE_TIME_MS);
    dropPill(1);                 // เม็ดแรก ที่ช่อง 2 รอบที่ 5
    advance(SHAKE_TIME_MS);
    assert(dispenserIsBusy());
    assert(runAttemptsWithoutDrop(ATTEMPTS_PER_HOLE - 1) == ATTEMPTS_PER_HOLE - 1);
    advance(MOVE_TIME_MS);
    advance(MOVE_TIME_MS);
    advance(PILL_DETECT_LOCKOUT_MS + 1);
    dropPill(1);                 // เม็ดที่สอง ยังที่ช่อง 2 (พลาด 9 รอบยังไม่ครบ 10)
    advance(SHAKE_TIME_MS);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 2);
    {
      const std::vector<int> released = releasePulses(1);
      assert(released.size() == ATTEMPTS_PER_HOLE + 4 + 1 + (ATTEMPTS_PER_HOLE - 1) + 1);
      for (unsigned i = 0; i < ATTEMPTS_PER_HOLE; ++i) assert(released[i] == HOLE_PULSE_US[0][1]);
      for (size_t i = ATTEMPTS_PER_HOLE; i < released.size(); ++i) assert(released[i] == HOLE_PULSE_US[0][2]);
    }

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
    const auto beforeBlocked = pulses.size();
    assert(dispenseMedicine(1, 1) == DispenseResult::SensorBlocked);
    assert(!dispenserIsBusy() && pulses.size() == beforeBlocked);
    pinLevel[PILL_SENSOR_PINS[0]] = PILL_SENSOR_ACTIVE_LOW ? HIGH : LOW;
  }

  // -------------------------------------------------------------------------
  // ยกเลิกระหว่างทาง: หยุดทุกจานทันที ปฏิเสธคำสั่งใหม่ขณะกดค้าง และไม่ทำต่อเมื่อปล่อย
  // -------------------------------------------------------------------------
  for (int phase = 0; phase < 3; ++phase)
  {
    clearLog();
    assert(dispenseMedicine(2, 3) == DispenseResult::Started);
    for (int step = 0; step < phase; ++step)
      advance(MOVE_TIME_MS);

    const size_t beforeCancel = pulses.size();
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

  // ยกเลิกขณะสองจานทำงานพร้อมกัน ต้องได้ผลกลับมาครบทั้งสองใบ
  if (MAX_CONCURRENT_DISPENSERS >= 2)
  {
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    assert(dispenseMedicine(2, 1) == DispenseResult::Started);
    stopDispenser();
    assert(takeDispenseOutcome(outcome) && outcome.dispenser == 1 && outcome.cancelled);
    assert(takeDispenseOutcome(outcome) && outcome.dispenser == 2 && outcome.cancelled);
    assert(!takeDispenseOutcome(outcome));
    assert(!dispenserIsBusy());
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
  clearLog();
  fakeMillis = ULONG_MAX - MOVE_TIME_MS / 2;
  assert(dispenseMedicine(3, 1) == DispenseResult::Started);
  if (ENABLE_PILL_SENSOR)
    dropPill(3);
  advance(MOVE_TIME_MS / 2);
  // มีเซ็นเซอร์: เม็ดตกแล้วจึงหมุนกลับทันที
  // ไม่มี: ยังหมุนไม่ครบเวลาต้องยังไม่กลับ ถ้าการลบเวลาข้ามจุดวนกลับผิด
  // elapsed จะออกมามหาศาลแล้วจานจะกลับทันที
  assert(pulses.size() == (ENABLE_PILL_SENSOR ? 2u : 1u) && attachedCount == 1);
  advance(MOVE_TIME_MS);
  if (!ENABLE_PILL_SENSOR)
  {
    assert(pulses.size() == 2);  // ครบเวลาแล้วจึงกลับ
    advance(MOVE_TIME_MS);       // กลับถึงที่พัก นับหนึ่งเม็ดแล้วจบ
    assert(attachedCount == 0 && takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 3 && outcome.dispensedPills == 1 && !outcome.sensorVerified);
  }

  if (ENABLE_PILL_SENSOR)
  {
    assert(attachedCount == 0);
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 3 && outcome.dispensedPills == 1 && !outcome.cancelled);
  }

  if (ENABLE_SERVO_MOVEMENT && !ENABLE_PILL_SENSOR)
  {
    // ไม่มีเซ็นเซอร์: รู้ไม่ได้ว่ายาตกหรือยัง จึงห้ามวนหาช่องเด็ดขาด
    // (วนครบทุกช่องอาจเทยาออกมาหลายสิบเม็ด) ต้องหมุนแค่หนึ่งรอบต่อเม็ดที่ช่องแรก
    clearLog();
    assert(dispenseMedicine(1, 2, 3) == DispenseResult::Started);
    assert(runAttemptsWithoutDrop(PILL_HOLE_COUNT * ATTEMPTS_PER_HOLE) == 2);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome));
    assert(outcome.attempts == 2 && outcome.dispensedPills == 2 && !outcome.sensorVerified);
    const std::vector<int> released = releasePulses(1);
    assert(released.size() == 2);
    assert(released[0] == HOLE_PULSE_US[0][3] && released[1] == HOLE_PULSE_US[0][3]);
  }

  return 0;
}
