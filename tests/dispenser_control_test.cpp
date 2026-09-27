#include "config.h"
#include "dispenser_control.h"
#include <ESP32Servo.h>
#include <Wire.h>
#include <climits>

#ifdef EXPECT_SERVO_MOVEMENT
// ถ้าไฟล์ตั้งค่าเครื่องไหนสักที่แอบเปลี่ยนโหมด สองรอบของเทสต์จะทดสอบโหมดเดียวกันซ้ำโดยไม่มีใครรู้
static_assert(ENABLE_SERVO_MOVEMENT == (EXPECT_SERVO_MOVEMENT != 0),
              "test is not running in the mode the driver asked for");
static_assert(ENABLE_PILL_SENSOR == (EXPECT_PILL_SENSOR != 0),
              "test is not running in the sensor mode the driver asked for");
#endif

unsigned long fakeMillis = 0;
unsigned long fakeExtraUs = 0;
int pinLevel[64];
void (*pinIsr[64])(void *) = {};
void *pinIsrArg[64] = {};
int writtenLevel[64];
unsigned long writtenAt[64];
TwoWire Wire;
int (*digitalReadHook)(uint8_t pin) = nullptr;
std::vector<PinWrite> digitalWrites;
std::vector<PinWrite> analogWrites;
int analogWriteCalls = 0;
int ledcChannelOf[64];
FakeSerial Serial;
std::vector<Pulse> pulses;
int attachedCount = 0;
bool failAttach = false;

namespace {

void setCancel(bool pressed) { pinLevel[CANCEL_BUTTON_PIN] = pressed ? LOW : HIGH; }

/** relay เลเซอร์ติดอยู่ไหม ตามไบต์ล่าสุดที่ส่งไป PCF8574 (-1 = ยังไม่เคยสั่ง) */
int laserState()
{
  const int value = Wire.lastValue[LASER_PCF8574_ADDRESS];
  if (value < 0)
    return -1;
  const bool pinHigh = (value >> LASER_PCF_BIT) & 1;
  return pinHigh != LASER_SWITCH_ACTIVE_LOW ? 1 : 0;
}
bool laserIsOn() { return laserState() == 1; }
bool laserIsOff() { return laserState() == 0; }

/** เลเซอร์ติดและนิ่งแล้วหรือยัง ตามที่ตัวคุมสั่งชิปไว้จริง */
unsigned long receiverResponseMs = 10;  // ตัวรับต้องใช้เวลาเท่านี้หลังเลเซอร์ติดกว่าจะเห็นแสง

bool laserSettled()
{
  return laserIsOn() && fakeMillis - Wire.lastAt[LASER_PCF8574_ADDRESS] >= receiverResponseMs;
}

/**
 * ตัวรับเลเซอร์จริง: เลเซอร์ดับหรือยังไม่นิ่ง = ไม่เห็นแสง = อ่านได้ว่าถูกบัง
 * ใช้กับทุกเทสต์ด้านล่าง การจ่ายทุกครั้งในไฟล์นี้จึงพิสูจน์ด้วยว่าเปิดเลเซอร์ก่อนอ่านเซ็นเซอร์
 */
bool receiverInverted = false;  // ตัวรับอีกรุ่นที่ให้ขั้วกลับกัน
int deadReceiver = -1;          // ตัวรับที่เสีย/สายหลุด: ค่าไม่เปลี่ยนเลย (-1 = ไม่มี)
int deadLevel = HIGH;           // ค่าที่ตัวรับเสียค้างอยู่

int laserReceiverRead(uint8_t pin)
{
  for (uint8_t i = 0; i < DISPENSER_COUNT; ++i)
  {
    if (pin != PILL_SENSOR_PINS[i])
      continue;
    if (static_cast<int>(i) == deadReceiver)
      return deadLevel;
    int level = pinLevel[pin];
    if (ENABLE_LASER_SWITCH && !laserSettled())
      level = PILL_SENSOR_ACTIVE_LOW ? LOW : HIGH;
    return receiverInverted ? !level : level;
  }
  return pinLevel[pin];
}

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
const int BLOCKED_LEVEL = PILL_SENSOR_ACTIVE_LOW ? LOW : HIGH;
const int CLEAR_LEVEL = PILL_SENSOR_ACTIVE_LOW ? HIGH : LOW;

/** บังลำแสงนาน `us` ไมโครวินาที โดย loop ไม่ได้ทำงานระหว่างนั้นเลย (มีแค่ interrupt ที่เห็น) */
void pulseBeam(uint8_t unit, unsigned long us)
{
  const uint8_t pin = PILL_SENSOR_PINS[unit - 1];
  driveInput(pin, BLOCKED_LEVEL);
  fakeExtraUs += us;
  driveInput(pin, CLEAR_LEVEL);
}

void dropPill(uint8_t unit)
{
  // เม็ดจริงบังลำแสงเลเซอร์แค่ไม่กี่มิลลิวินาที ระหว่างนั้น loop อาจยังไม่ได้วนมาเลย
  pulseBeam(unit, 3000);
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
  DispenseOutcome outcome = {0, 0, 0, 0, false, false, StopReason::Done, 0, 0};

  resetPins();
  for (uint8_t pin = 0; pin < 64; ++pin)
    writtenLevel[pin] = -1;
  digitalReadHook = laserReceiverRead;
  for (int &channel : ledcChannelOf)
    channel = -1;
  dispenserSafePinsEarly();
  dispenserControlBegin();
  // มอเตอร์สั่นต้องอยู่บนช่อง PWM ที่จองไว้เอง ไม่ใช่ช่องที่ analogWrite เลือกให้
  // (ชนช่องของ servo แล้ว servo แค่กระตุก ไม่หมุน)
  for (uint8_t i = 0; i < DISPENSER_COUNT; ++i)
    assert(ledcChannelOf[VIB_PWM_PINS[i]] == VIB_LEDC_CHANNELS[i]);
  assert(pillSensorStatus(1) == (ENABLE_PILL_SENSOR && !ENABLE_LASER_SWITCH ? 1 : -1));  // ยังไม่ได้ตรวจ
  // ชิปอาจค้างสถานะ "ติด" จากก่อนรีเซ็ต ESP32 (ไฟชิปไม่ได้ถูกตัด): ตอนเริ่มต้องสั่งดับ
  if (ENABLE_LASER_SWITCH)
    assert(laserIsOff());

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
    assert(outcome.stopReason == StopReason::Done);
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 2 && outcome.dispensedPills == 1 && !outcome.cancelled);
    assert(!takeDispenseOutcome(outcome));

    stopDispenser();  // เก็บกวาดจานสามที่ยังค้างอยู่
    assert(takeDispenseOutcome(outcome) && outcome.dispenser == 3 && outcome.cancelled);
    assert(outcome.stopReason == StopReason::Cancelled);
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
    assert(outcome.stopReason == StopReason::AllHolesTried);  // เว็บขึ้น "no drop 0/2 a40"
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

  if (ENABLE_SERVO_MOVEMENT && ENABLE_PILL_SENSOR && ENABLE_LASER_SWITCH)
  {
    // -----------------------------------------------------------------------
    // เลเซอร์ติดเฉพาะตอนจ่าย
    // -----------------------------------------------------------------------
    while (takeDispenseOutcome(outcome)) {}
    assert(!dispenserIsBusy() && laserIsOff());
    assert(!pillSensorBlocked(1));  // เลเซอร์ดับ ค่าจากตัวรับไม่มีความหมาย ห้ามรายงานว่าถูกบัง

    // เริ่มจ่าย: เปิดเลเซอร์และรอให้นิ่งก่อนเช็คลำแสง (ตัวจำลองมืดจนกว่าจะนิ่ง)
    unsigned long before = fakeMillis;
    assert(dispenseMedicine(1, 2) == DispenseResult::Started);
    // รอจนตัวรับตอบ แล้วรอให้นิ่งอีกนิด ไม่รอเต็มเวลาสูงสุดเมื่อตัวรับตอบไวแล้ว
    assert(laserIsOn() && fakeMillis - before >= receiverResponseMs + LASER_SETTLE_MS);
    assert(fakeMillis - before < LASER_RESPONSE_TIMEOUT_MS);

    // จานที่สองเริ่มระหว่างจานแรกยังทำงาน: เลเซอร์ติดอยู่แล้ว ไม่ต้องรอซ้ำ
    before = fakeMillis;
    assert(dispenseMedicine(2, 1) == DispenseResult::Started);
    assert(fakeMillis == before);
    advance(DISPENSE_STAGGER_MS);

    // จานสองได้ครบก่อน: จานหนึ่งยังนับเม็ดอยู่ เลเซอร์ต้องยังติด
    dropPill(2);
    bool twoDone = false;
    for (int i = 0; i < 20 && !twoDone; ++i)
    {
      advance(MOVE_TIME_MS);
      if (takeDispenseOutcome(outcome))
      {
        assert(outcome.dispenser == 2 && outcome.dispensedPills == 1);
        twoDone = true;
      }
    }
    assert(twoDone && dispenserIsBusy());
    assert(laserIsOn());

    // จานหนึ่งได้ครบ: ไม่มีใครใช้แล้ว เลเซอร์ดับ และจังหวะแสงดับต้องไม่ถูกนับเป็นเม็ด
    advance(PILL_DETECT_LOCKOUT_MS + 1);
    dropPill(1);
    advance(PILL_DETECT_LOCKOUT_MS + 1);
    dropPill(1);
    for (int i = 0; i < 20 && dispenserIsBusy(); ++i)
      advance(MOVE_TIME_MS);
    assert(!dispenserIsBusy() && laserIsOff());
    advance(1000);  // ไม่มีจานไหนอ่านเซ็นเซอร์หลังเลเซอร์ดับ ผลต้องไม่เปลี่ยน
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 1 && outcome.dispensedPills == 2);
    assert(!takeDispenseOutcome(outcome));

    // ลำแสงถูกบังจริง (มีของค้าง): ปฏิเสธการจ่าย และดับเลเซอร์ ไม่ปล่อยติดค้าง
    pinLevel[PILL_SENSOR_PINS[0]] = PILL_SENSOR_ACTIVE_LOW ? LOW : HIGH;
    assert(dispenseMedicine(1, 1) == DispenseResult::SensorBlocked);
    assert(!dispenserIsBusy() && laserIsOff());
    pinLevel[PILL_SENSOR_PINS[0]] = PILL_SENSOR_ACTIVE_LOW ? HIGH : LOW;

    // PCF8574 ไม่ตอบ (สายหลุด): เลเซอร์ไม่ติด ห้ามจ่ายแบบนับเม็ดไม่ได้
    Wire.present = false;
    clearLog();
    assert(dispenseMedicine(1, 1) == DispenseResult::LaserOff);  // บอกตรงว่าเป็นที่สาย I2C
    assert(!dispenserIsBusy() && pulses.empty());
    Wire.present = true;

    // ตัวรับที่ตอบช้า (เคยรอตายตัว 60 ms: relay ติดแป๊บเดียวแล้วดับ ไม่ยอมจ่าย): ต้องรอจนตอบแล้วจ่ายได้
    receiverResponseMs = 250;
    {
      const unsigned long before = fakeMillis;
      assert(dispenseMedicine(1, 1) == DispenseResult::Started);
      assert(fakeMillis - before >= 250 + LASER_SETTLE_MS);
      dropPill(1);
      for (int i = 0; i < 10 && dispenserIsBusy(); ++i)
        advance(MOVE_TIME_MS);
      assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 1);
    }
    // ...แต่ถ้าช้าเกินเวลาสูงสุด = ถือว่าไม่เห็นแสง ไม่จ่าย
    receiverResponseMs = LASER_RESPONSE_TIMEOUT_MS + 100;
    assert(dispenseMedicine(1, 1) == DispenseResult::SensorBlocked && laserIsOff());
    receiverResponseMs = 10;

    // ตัวรับอีกรุ่นให้ขั้วกลับกัน: เครื่องหาขั้วเองตอนเปิดเลเซอร์ นับได้ถูกโดยไม่ต้องแก้ config
    receiverInverted = true;
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    dropPill(1);
    for (int i = 0; i < 10 && dispenserIsBusy(); ++i)
      advance(MOVE_TIME_MS);
    assert(!dispenserIsBusy() && takeDispenseOutcome(outcome));
    assert(outcome.dispenser == 1 && outcome.dispensedPills == 1);
    // interrupt ต้องใช้ขั้วที่หาได้ด้วย: สัญญาณแวบสั้นยังต้องไม่ถูกนับ แม้ขั้วกลับกัน
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    pulseBeam(1, 100);
    dispenserControlUpdate();
    advance(PILL_DETECT_LOCKOUT_MS + 1);
    stopDispenser();
    assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 0);
    receiverInverted = false;

    // ตัวรับจาน 2 เสีย (ค่าไม่เปลี่ยนตอนเปิด/ปิดเลเซอร์): ห้ามจ่ายจานนั้นเลย
    // ค้างได้ทั้งสองระดับ ระดับหนึ่งจะถูกอ่านว่า "โล่ง" ต้องปฏิเสธเพราะตรวจแล้วเสีย ไม่ใช่เพราะบังเอิญ
    deadReceiver = 1;
    for (int level : {LOW, HIGH})
    {
      deadLevel = level;
      clearLog();
      assert(dispenseMedicine(2, 1) == DispenseResult::SensorBlocked);
      assert(pulses.empty() && !dispenserIsBusy() && laserIsOff());
    }
    // ...แต่จานอื่นยังจ่ายได้ตามปกติ
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    dropPill(1);
    for (int i = 0; i < 10 && dispenserIsBusy(); ++i)
      advance(MOVE_TIME_MS);
    assert(takeDispenseOutcome(outcome) && outcome.dispenser == 1 && outcome.dispensedPills == 1);
    // แก้สายแล้ว: ตรวจใหม่ตอนเปิดเลเซอร์ครั้งถัดไป จานนั้นกลับมาจ่ายได้เอง ไม่ต้องรีบูต
    deadReceiver = -1;
    assert(dispenseMedicine(2, 1) == DispenseResult::Started);
    stopDispenser();
    while (takeDispenseOutcome(outcome)) {}

    // ตรวจตัวเองตอนเปิดเครื่อง (ผลขึ้นจอ ไม่ต้องต่อคอม): ครบทุกจาน แล้วดับเลเซอร์
    assert(laserIsOff());
    dispenserSensorSelfTest();
    assert(laserIsOff() && laserSwitchResponding());
    assert(pillSensorStatus(1) == 1 && pillSensorStatus(2) == 1 && pillSensorStatus(3) == 1);
    deadReceiver = 2;  // จาน 3 ยังไม่ได้เสียบตัวรับ
    dispenserSensorSelfTest();
    assert(pillSensorStatus(1) == 1 && pillSensorStatus(3) == 0);
    deadReceiver = -1;
    assert(pillSensorStatus(0) == -1 && pillSensorStatus(DISPENSER_COUNT + 1) == -1);
    // PCF8574 ไม่ตอบ: จอบอกว่าเป็นที่ชิปคุม relay
    Wire.present = false;
    dispenserSensorSelfTest();
    assert(!laserSwitchResponding());
    Wire.present = true;
    dispenserSensorSelfTest();
    assert(laserSwitchResponding());
    // มีจานทำงานอยู่: ห้ามตรวจ (จะไปดับเลเซอร์ที่จานนั้นใช้อยู่)
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    dispenserSensorSelfTest();
    assert(laserIsOn() && dispenserIsBusy());
    stopDispenser();
    while (takeDispenseOutcome(outcome)) {}

    // กดยกเลิกกลางคัน: เลเซอร์ต้องดับด้วย
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    stopDispenser();
    assert(laserIsOff());
    while (takeDispenseOutcome(outcome)) {}
  }

  if (ENABLE_SERVO_MOVEMENT && ENABLE_PILL_SENSOR)
  {
    // -----------------------------------------------------------------------
    // อ่านเซ็นเซอร์ด้วย interrupt และแยกเม็ดยาออกจากสัญญาณรบกวน
    // -----------------------------------------------------------------------
    while (takeDispenseOutcome(outcome)) {}
    auto countAfter = [&](void (*scenario)()) {
      assert(dispenseMedicine(1, 3) == DispenseResult::Started);
      scenario();
      advance(PILL_DETECT_LOCKOUT_MS + 1);
      stopDispenser();
      assert(takeDispenseOutcome(outcome));
      return outcome.dispensedPills;
    };

    // เม็ดที่บังแค่ 3 ms ระหว่างที่ loop ไม่ได้วนเลย: interrupt ต้องจับได้
    assert(countAfter([] { dropPill(1); }) == 1);

    // สัญญาณแวบสั้น (ไฟกระชาก ฝุ่น แสงสะท้อน) ไม่ใช่เม็ดยา
    assert(countAfter([] { pulseBeam(1, 100); dispenserControlUpdate(); }) == 0);
    assert(countAfter([] { pulseBeam(1, PILL_MIN_BLOCK_US - 1); dispenserControlUpdate(); }) == 0);
    // สถิติบนเว็บ: เซ็นเซอร์เห็นบางอย่างแต่สั้นกว่าเกณฑ์ ("b1 380us") ต่างจาก ไม่เห็นอะไรเลย ("b0")
    assert(countAfter([] { pulseBeam(1, 380); dispenserControlUpdate(); }) == 0);
    assert(outcome.uncountedBlocks == 1 && outcome.longestUncountedUs == 380);
    assert(countAfter([] { pulseBeam(1, 200); advance(PILL_DETECT_LOCKOUT_MS + 1); pulseBeam(1, 300); dispenserControlUpdate(); }) == 0);
    assert(outcome.uncountedBlocks == 2 && outcome.longestUncountedUs == 300);
    assert(countAfter([] {}) == 0 && outcome.uncountedBlocks == 0 && outcome.longestUncountedUs == 0);
    assert(countAfter([] { dropPill(1); }) == 1 && outcome.uncountedBlocks == 0);  // เม็ดที่นับแล้วไม่ใช่ "ไม่นับ"
    assert(countAfter([] { pulseBeam(1, PILL_MIN_BLOCK_US); dispenserControlUpdate(); }) == 1);

    // เม็ดที่ขอบสั่น ตัดลำแสงเป็นหลายท่อนสั้นๆ: รวมเวลาแล้วถึงเกณฑ์ = หนึ่งเม็ด ไม่ใช่สามเม็ด
    assert(countAfter([] {
      for (int i = 0; i < 3; ++i) { pulseBeam(1, 200); fakeExtraUs += 100; }
      dispenserControlUpdate();
    }) == 1);

    // กระเด้งกลับมาตัดลำแสงอีกก่อนพ้นช่วงรวม = เม็ดเดิม
    assert(countAfter([] { dropPill(1); fakeExtraUs += 20000; dropPill(1); }) == 1);
    // เว้นนานพอ = สองเม็ด
    assert(countAfter([] { dropPill(1); advance(PILL_DETECT_LOCKOUT_MS + 1); dropPill(1); }) == 2);

    // สัญญาณรบกวนถี่จนคิวล้น: ไม่ค้าง ไม่นับมั่ว (ท่อนละ 10 us รวมไม่ถึงเกณฑ์)
    assert(countAfter([] {
      for (int i = 0; i < 40; ++i) { pulseBeam(1, 10); fakeExtraUs += 10; }
      dispenserControlUpdate();
    }) == 0);

    // ...และหยุดจานนั้น ไม่หมุนปล่อยยาต่อทั้งที่นับไม่ได้ว่าช่วงที่หายไปมีกี่เม็ด
    clearLog();
    assert(dispenseMedicine(1, 3) == DispenseResult::Started);
    for (int i = 0; i < 40; ++i) { pulseBeam(1, 10); fakeExtraUs += 10; }
    dispenserControlUpdate();
    for (int i = 0; i < 10 && dispenserIsBusy(); ++i)
      advance(MOVE_TIME_MS);
    assert(!dispenserIsBusy() && releasePulses(1).size() == 1);
    assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 0 && !outcome.cancelled);
    assert(outcome.stopReason == StopReason::Noise);

    // ยังบังอยู่แต่นานถึงเกณฑ์แล้ว: นับทันทีไม่ต้องรอให้โล่ง
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    driveInput(PILL_SENSOR_PINS[0], BLOCKED_LEVEL);
    fakeExtraUs += 2000;
    dispenserControlUpdate();
    driveInput(PILL_SENSOR_PINS[0], CLEAR_LEVEL);
    for (int i = 0; i < 10 && dispenserIsBusy(); ++i)
      advance(MOVE_TIME_MS);
    assert(!dispenserIsBusy() && takeDispenseOutcome(outcome) && outcome.dispensedPills == 1);

    // เม็ดติดขวางลำแสง: หยุดจ่ายทันที ห้ามหมุนปล่อยยาเพิ่มขณะมองไม่เห็นเม็ดถัดไป
    clearLog();
    assert(dispenseMedicine(1, 3) == DispenseResult::Started);
    driveInput(PILL_SENSOR_PINS[0], BLOCKED_LEVEL);
    advance(PILL_JAM_MS);
    for (int i = 0; i < 10 && dispenserIsBusy(); ++i)
      advance(MOVE_TIME_MS);
    assert(!dispenserIsBusy());
    assert(releasePulses(1).size() == 1);  // หมุนปล่อยแค่ครั้งแรก แล้วกลับที่พัก
    assert(takeDispenseOutcome(outcome) && outcome.dispensedPills == 1 && !outcome.cancelled);
    assert(outcome.stopReason == StopReason::Jam);  // เว็บขึ้น "jam 1/3 a1"
    driveInput(PILL_SENSOR_PINS[0], CLEAR_LEVEL);

    // จานที่รอเหลื่อมจังหวะ: สัญญาณแวบระหว่างรอ (ยังไม่ได้หมุนเลย ยาตกไม่ได้) ต้องไม่ถูกนับ
    assert(dispenseMedicine(1, 1) == DispenseResult::Started);
    assert(dispenseMedicine(2, 1) == DispenseResult::Started);  // รอ DISPENSE_STAGGER_MS
    pulseBeam(2, 3000);
    advance(DISPENSE_STAGGER_MS);
    advance(PILL_DETECT_LOCKOUT_MS + 1);
    stopDispenser();
    while (takeDispenseOutcome(outcome))
      if (outcome.dispenser == 2) assert(outcome.dispensedPills == 0);

    // ขอบที่ค้างในคิวจากหลังจบรอบ (เช่นแสงดับตอนปิดเลเซอร์) ต้องไม่ถูกนับในรอบถัดไป
    pulseBeam(1, 5000);
    pulseBeam(1, 5000);
    assert(countAfter([] {}) == 0);
  }

  // ตลอดการทดสอบทุกกรณี ไม่มีการเรียก analogWrite เลย
  assert(analogWriteCalls == 0);
  return 0;
}
