#include "dispenser_control.h"
#include "config.h"

#include <ESP32Servo.h>
#include <Wire.h>

namespace {

/**
 * ลำดับหนึ่งรอบ (จากที่ทดสอบกับกลไกจริงแล้วได้ผลดีที่สุด):
 *
 *   Releasing : หมุนไปตำแหน่งจ่าย พร้อมสั่น      -> กวาดยาให้ไหลลงรู
 *   Returning : หมุนกลับตำแหน่งพัก โดยเบรกมอเตอร์ -> ล็อกแกนไม่ให้ยาตกเกิน
 *   Shaking   : อยู่นิ่งแล้วสั่นค้าง               -> สะบัดเม็ดที่ค้างอยู่ปากรูให้หลุดลง
 *
 * แล้ววนใหม่จนเซ็นเซอร์ IR นับเม็ดได้ครบ
 *
 * ช่องปล่อยยา: เริ่มที่ช่องตามขนาดยาที่ผู้ใช้กรอกบนเว็บ ถ้าหมุนครบ ATTEMPTS_PER_HOLE รอบ
 * ติดกันโดยไม่มีเม็ดตก จะย้ายไปช่องที่ใหญ่กว่าทีละช่อง ครบแล้วยังไม่ได้ = จบ
 * แบบจ่ายไม่ครบ ไม่ได้กรอกขนาด = ไล่จากช่องเล็กสุด
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

  // ช่องปล่อยยาที่จะลองตามลำดับ และตำแหน่งปัจจุบันในลำดับนั้น
  uint8_t holeOrder[PILL_HOLE_COUNT] = {};
  uint8_t holeCount = 0;
  uint8_t holePos = 0;
  // หมุนที่ช่องปัจจุบันมาแล้วกี่รอบติดกันโดยไม่มีเม็ดตก
  uint8_t holeMisses = 0;
  // จำนวนเม็ดตอนเริ่มรอบหมุนนี้ ใช้ตัดสินว่ารอบนี้มีเม็ดตกไหม
  uint8_t pillsAtAttemptStart = 0;


  // จังหวะกระตุกมอเตอร์สั่นตอนออกตัว (ไม่ใช้ delay)
  bool kicking = false;
  unsigned long kickStartedMs = 0;

  // ตัวแยกเม็ดยาจากขอบสัญญาณที่ interrupt จดไว้ (ดู pollSensor)
  bool beamBlocked = false;     // ลำแสงถูกบังอยู่ไหม ตามขอบล่าสุดที่ประมวลผลแล้ว
  bool inPass = false;          // อยู่ระหว่างการผ่านของเม็ดหนึ่งเม็ด (รวมช่วงกระเด้ง/หมุนตัว)
  bool passCounted = false;     // การผ่านนี้นับไปแล้ว
  uint32_t blockStartUs = 0;    // ลำแสงเริ่มถูกบังรอบล่าสุดเมื่อไร
  uint32_t lastClearUs = 0;     // ลำแสงโล่งครั้งล่าสุดเมื่อไร
  uint32_t blockedUsInPass = 0; // เวลาที่ถูกบังรวมทั้งการผ่านนี้ (ไม่นับช่วงที่กำลังบังอยู่)
  bool jammed = false;          // ลำแสงถูกบังค้าง = มีของติดขวาง
  StopReason stopReason = StopReason::Done;
  uint8_t uncountedBlocks = 0;      // การผ่านที่จบโดยบังรวมไม่ถึงเกณฑ์
  uint32_t longestUncountedUs = 0;  // นานสุดในบรรดาการผ่านที่ไม่นับ
};

// ทุกช่องไม่มีเม็ดตกเลย + รอบที่ได้เม็ด: เพดานรวมที่ตรรกะข้างล่างไม่มีทางเกิน
constexpr unsigned MAX_TOTAL_ATTEMPTS = PILL_HOLE_COUNT * ATTEMPTS_PER_HOLE + MAX_PILLS_PER_DOSE;
static_assert(MAX_TOTAL_ATTEMPTS <= 255, "attempt counter is uint8_t");

uint8_t currentHole(const Run &run) { return run.holeOrder[run.holePos]; }

Servo dispenserServos[DISPENSER_COUNT];
Run runs[DISPENSER_COUNT];

// ---------------------------------------------------------------------------
// ขอบสัญญาณเซ็นเซอร์จาก interrupt
// ---------------------------------------------------------------------------
//
// ลำแสงเลเซอร์เล็กมาก เม็ดยาตัดผ่านแค่ไม่กี่มิลลิวินาที ถ้าอ่านระดับใน loop
// รอบไหนช้า (เขียนจอ I2C, ขยับ servo) เม็ดนั้นหายไปเลย แล้วเครื่องหมุนจ่ายเพิ่มเกินจำนวน
// interrupt จดทุกขอบพร้อมเวลาไว้ในคิว ส่วนการตัดสินว่าเป็นเม็ดหรือไม่ทำใน loop (ดู pollSensor)
//
// คิวหนึ่งต่อจาน: ผู้เขียนมีคนเดียวคือ interrupt ผู้อ่านมีคนเดียวคือ loop หลัก จึงไม่ต้องใช้ lock
struct SensorEdge {
  uint32_t atUs;
  bool blocked;
};
// ระดับสัญญาณที่แปลว่า "ลำแสงถูกบัง" ของแต่ละจาน
// เริ่มจาก PILL_SENSOR_ACTIVE_LOW แล้วหาจริงเองทุกครั้งที่เปิดเลเซอร์ (ดู laserEnsureOn)
// ตัวรับต่างรุ่นให้ขั้วต่างกันได้ ผู้ใช้จึงไม่ต้องวัดเองแล้วมาแก้ config
volatile uint8_t blockedLevel[DISPENSER_COUNT];
// ตัวรับจานนั้นใช้ไม่ได้ (ค่าไม่เปลี่ยนตอนเปิด/ปิดเลเซอร์) ห้ามจ่ายจานนั้น
bool sensorFault[DISPENSER_COUNT] = {};
// ตรวจตัวรับจานนั้นแล้วหรือยัง (ตรวจทุกครั้งที่เปิดเลเซอร์จากดับ) ใช้แยก "ยังไม่รู้" ออกจาก "ใช้ได้"
bool sensorTested[DISPENSER_COUNT] = {};
// PCF8574 ตอบครั้งล่าสุดที่สั่งไหม
bool laserSwitchOk = true;

constexpr uint8_t EDGE_QUEUE_SIZE = 32;
struct EdgeQueue {
  SensorEdge items[EDGE_QUEUE_SIZE];
  volatile uint8_t head = 0;  // interrupt เขียน
  volatile uint8_t tail = 0;  // loop อ่าน
  volatile bool overflow = false;
};
EdgeQueue edgeQueues[DISPENSER_COUNT];

void IRAM_ATTR onSensorEdge(void *arg)
{
  const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
  EdgeQueue &queue = edgeQueues[index];
  const uint8_t next = static_cast<uint8_t>((queue.head + 1) % EDGE_QUEUE_SIZE);
  if (next == queue.tail)
  {
    queue.overflow = true;  // สัญญาณรบกวนถี่จนคิวเต็ม loop จะตั้งสถานะใหม่จากระดับจริง
    return;
  }
  const int level = digitalRead(PILL_SENSOR_PINS[index]);
  queue.items[queue.head] = {static_cast<uint32_t>(micros()), level == blockedLevel[index]};
  queue.head = next;
}

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

/**
 * เขียนความแรงมอเตอร์สั่นผ่านช่อง LEDC ที่จองไว้เอง (ดู VIB_LEDC_CHANNELS ใน config.h)
 * ห้ามกลับไปใช้ analogWrite: จะไปแย่งช่องของ servo แล้ว servo ไม่หมุน
 */
bool vibAttached[DISPENSER_COUNT] = {};

void vibWrite(uint8_t index, uint8_t duty)
{
  if (!vibAttached[index])
  {
    vibAttached[index] = ledcAttachChannel(VIB_PWM_PINS[index], VIB_PWM_FREQ_HZ, VIB_PWM_RESOLUTION_BITS,
                                           VIB_LEDC_CHANNELS[index]);
    if (!vibAttached[index])
    {
      Serial.printf("[มอเตอร์สั่น] จาน %u ผูกช่อง PWM %u ไม่สำเร็จ\n",
                    static_cast<unsigned>(index + 1), static_cast<unsigned>(VIB_LEDC_CHANNELS[index]));
      return;
    }
  }
  ledcWrite(VIB_PWM_PINS[index], duty);
}

void vibrateOn(uint8_t index, unsigned long now)
{
  digitalWrite(VIB_DIR_PINS[index], LOW);
  vibWrite(index, VIB_KICK_SPEED);
  runs[index].kicking = true;
  runs[index].kickStartedMs = now;
}

/** IN1 = IN2 = HIGH คือเบรกแบบล็อกแกน กันตุ้มถ่วงเหวี่ยงต่อ */
void vibrateBrake(uint8_t index)
{
  runs[index].kicking = false;
  digitalWrite(VIB_DIR_PINS[index], HIGH);
  vibWrite(index, 255);
}

void vibrateOff(uint8_t index)
{
  runs[index].kicking = false;
  digitalWrite(VIB_DIR_PINS[index], LOW);
  vibWrite(index, 0);
}

/** ผ่อนกำลังจากจังหวะกระตุกลงมาที่ความแรงปกติ */
void tickKick(uint8_t index, unsigned long now)
{
  Run &run = runs[index];
  if (!run.kicking)
    return;
  if (now - run.kickStartedMs < VIB_KICK_MS)
    return;

  vibWrite(index, VIB_SPEED);
  run.kicking = false;
}

// ---------------------------------------------------------------------------
// เซ็นเซอร์ IR
// ---------------------------------------------------------------------------

bool readSensor(uint8_t index)
{
  return digitalRead(PILL_SENSOR_PINS[index]) == blockedLevel[index];
}

// ---------------------------------------------------------------------------
// สวิตช์เลเซอร์: ติดเฉพาะตอนมีจานกำลังจ่าย
// ---------------------------------------------------------------------------
//
// เลเซอร์ดับ = ตัวรับไม่เห็นแสง = อ่านได้ว่า "ถูกบัง" ตลอด จึงมีกติกาสองข้อ:
//   1. ห้ามอ่านเซ็นเซอร์ก่อนเลเซอร์ติดและนิ่ง ไม่อย่างนั้นทุกการจ่ายจะถูกปฏิเสธว่าเซ็นเซอร์ถูกบัง
//   2. ดับเฉพาะตอนไม่มีจานไหนจ่ายอยู่ (ไม่มีใครอ่านเซ็นเซอร์แล้ว) ไม่อย่างนั้นจังหวะแสงดับ
//      จะถูกนับเป็นเม็ดยาหนึ่งเม็ด
bool laserLit = false;

/** สั่ง relay ผ่าน PCF8574: ขาอื่นปล่อยเป็น HIGH (ค่าปกติของชิป) คืน false ถ้าชิปไม่ตอบ */
bool laserWrite(bool on)
{
  const uint8_t mask = static_cast<uint8_t>(1U << LASER_PCF_BIT);
  const bool pinHigh = on != LASER_SWITCH_ACTIVE_LOW;
  Wire.beginTransmission(LASER_PCF8574_ADDRESS);
  Wire.write(pinHigh ? 0xFF : static_cast<uint8_t>(0xFF & ~mask));
  laserSwitchOk = Wire.endTransmission() == 0;
  if (laserSwitchOk)
    return true;
  Serial.printf("[เลเซอร์] ไม่พบ PCF8574 ที่ 0x%02X เปิด-ปิดเลเซอร์ไม่ได้ ตรวจสายและจั๊มเปอร์ A0-A2\n",
                LASER_PCF8574_ADDRESS);
  return false;
}

/** เปิดเลเซอร์แล้วรอให้นิ่ง ถ้าติดอยู่แล้ว (มีจานอื่นจ่ายอยู่) ไม่ต้องรอซ้ำ */
void laserEnsureOn()
{
  if (!ENABLE_LASER_SWITCH || !ENABLE_PILL_SENSOR || laserLit)
    return;

  // เลเซอร์ดับอยู่ = ตัวรับเห็นมืด = ระดับเดียวกับตอนลำแสงถูกบัง อ่านเก็บไว้ก่อนเปิด
  int dark[DISPENSER_COUNT];
  for (uint8_t i = 0; i < DISPENSER_COUNT; ++i)
    dark[i] = digitalRead(PILL_SENSOR_PINS[i]);

  // ชิปไม่ตอบ: เลเซอร์ไม่ติด ตัวรับจะเห็นมืด แล้วการจ่ายถูกปฏิเสธว่าเซ็นเซอร์ถูกบัง ซึ่งปลอดภัยกว่าจ่ายแบบนับไม่ได้
  if (!laserWrite(true))
    return;
  laserLit = true;

  // รอจนตัวรับทุกตัวเห็นแสง (ค่าเปลี่ยนจากตอนมืด) หรือหมดเวลา แล้วรอให้นิ่งอีกช่วงสั้นๆ
  // block ได้: ตอนเลเซอร์ดับไม่มีจานไหนทำงานอยู่ จึงไม่มีเซ็นเซอร์หรือ servo ต้องดูแลระหว่างรอ
  const unsigned long startedMs = millis();
  bool waiting = true;
  while (waiting && millis() - startedMs < LASER_RESPONSE_TIMEOUT_MS)
  {
    delay(5);
    waiting = false;
    for (uint8_t i = 0; i < DISPENSER_COUNT; ++i)
      if (digitalRead(PILL_SENSOR_PINS[i]) == dark[i])
        waiting = true;
  }
  const unsigned long respondedMs = millis() - startedMs;
  delay(LASER_SETTLE_MS);
  Serial.printf("[เลเซอร์] เปิดแล้ว ตัวรับตอบใน %lu ms%s\n", respondedMs,
                waiting ? " (บางจานไม่เห็นแสงจนหมดเวลา)" : "");

  // เทียบตอนมืดกับตอนสว่าง: ต่างกัน = ตัวรับใช้ได้ และระดับตอนมืดคือระดับ "ถูกบัง"
  // เหมือนกัน = เล็งไม่โดน ตัวรับไม่มีไฟ สายหลุด หรือแสงรอบข้างแรงจนกลบเลเซอร์ นับเม็ดไม่ได้แน่นอน
  for (uint8_t i = 0; i < DISPENSER_COUNT; ++i)
  {
    const int lit = digitalRead(PILL_SENSOR_PINS[i]);
    const bool fault = lit == dark[i];
    if (fault && !sensorFault[i])
      Serial.printf("[เซ็นเซอร์] จาน %u ค่าไม่เปลี่ยนตอนเปิด/ปิดเลเซอร์ (เล็งไม่โดน ตัวรับไม่มีไฟ สายหลุด "
                    "หรือแสงรอบข้างแรงเกิน) ไม่จ่ายจานนี้จนกว่าจะแก้\n",
                    static_cast<unsigned>(i + 1));
    else if (!fault && (sensorFault[i] || blockedLevel[i] != dark[i]))
      Serial.printf("[เซ็นเซอร์] จาน %u ใช้งานได้ ลำแสงถูกบัง = %s\n",
                    static_cast<unsigned>(i + 1), dark[i] == LOW ? "LOW" : "HIGH");
    sensorFault[i] = fault;
    sensorTested[i] = true;
    if (!fault)
      blockedLevel[i] = static_cast<uint8_t>(dark[i]);
  }
}

/** ดับเลเซอร์เมื่อไม่มีจานไหนจ่ายอยู่แล้ว */
void laserOffIfIdle()
{
  if (!ENABLE_LASER_SWITCH || !laserLit || activeCount() > 0)
    return;
  laserWrite(false);
  laserLit = false;
}

// ---------------------------------------------------------------------------
// แยกเม็ดยาออกจากสัญญาณรบกวน
// ---------------------------------------------------------------------------
//
// "การผ่าน" หนึ่งครั้ง = ช่วงที่ลำแสงถูกบัง รวมช่วงโล่งสั้นๆ ที่ตามมา
//   - โล่งไม่ถึง PILL_DETECT_LOCKOUT_MS แล้วถูกบังอีก = เม็ดเดิมที่กระเด้ง หมุนตัว หรือขอบเม็ดสั่น
//     นับรวมเป็นการผ่านเดียว
//   - นับเป็นเม็ดเมื่อเวลาที่ถูกบังรวมทั้งการผ่านถึง PILL_MIN_BLOCK_US
//     สัญญาณแวบสั้นกว่านั้น (ไฟกระชาก ฝุ่น แสงสะท้อน) ไม่นับ
//     ใช้เวลารวม ไม่ใช่ช่วงยาวสุด เพราะเม็ดที่ขอบสั่นจะตัดลำแสงเป็นหลายท่อนสั้นๆ
//   - ถูกบังค้างนานถึง PILL_JAM_MS = มีเม็ดติดขวางลำแสง หยุดจ่ายทันที
//     ไม่อย่างนั้นเครื่องมองไม่เห็นเม็ดถัดไป แล้วหมุนปล่อยยาเกินจำนวน
constexpr uint32_t MERGE_GAP_US = PILL_DETECT_LOCKOUT_MS * 1000UL;

/** เริ่มนับใหม่: ทิ้งขอบเก่า (ตอนเลเซอร์ติด/ดับ หรือระหว่างรอออกตัว) แล้วตั้งสถานะจากระดับจริง */
void resetDetector(uint8_t index)
{
  EdgeQueue &queue = edgeQueues[index];
  queue.tail = queue.head;
  queue.overflow = false;

  Run &run = runs[index];
  run.beamBlocked = readSensor(index);
  run.inPass = false;
  run.passCounted = false;
  run.blockedUsInPass = 0;
  run.jammed = false;
  run.blockStartUs = run.lastClearUs = static_cast<uint32_t>(micros());
}

void countPill(uint8_t index)
{
  Run &run = runs[index];
  run.passCounted = true;
  if (run.countedPills < 255)
    ++run.countedPills;

  Serial.printf("[เซ็นเซอร์] จาน %u ตรวจพบเม็ดที่ %u/%u (บังลำแสงรวม %lu us)\n",
                static_cast<unsigned>(index + 1),
                static_cast<unsigned>(run.countedPills),
                static_cast<unsigned>(run.requestedPills),
                static_cast<unsigned long>(run.blockedUsInPass));

  if (run.countedPills >= run.requestedPills)
    run.targetReached = true;
}

/** การผ่านนี้จบแล้ว ถ้าไม่ถูกนับเป็นเม็ด จดไว้ให้เห็นบนเว็บว่าเซ็นเซอร์เห็นอะไรบางอย่าง แต่สั้นกว่าเกณฑ์ */
void endPass(Run &run)
{
  if (run.inPass && !run.passCounted && run.blockedUsInPass > 0)
  {
    if (run.uncountedBlocks < 255)
      ++run.uncountedBlocks;
    if (run.blockedUsInPass > run.longestUncountedUs)
      run.longestUncountedUs = run.blockedUsInPass;
  }
  run.inPass = false;
}

void handleEdge(uint8_t index, const SensorEdge &edge)
{
  Run &run = runs[index];
  if (edge.blocked)
  {
    if (run.beamBlocked)
      return;  // ขอบซ้ำ (ระดับเดิม) ไม่มีอะไรเปลี่ยน
    // โล่งนานพอแล้ว = เม็ดใหม่ ถ้าโล่งแค่แวบเดียว = ยังเป็นเม็ดเดิม
    if (!run.inPass || edge.atUs - run.lastClearUs >= MERGE_GAP_US)
    {
      endPass(run);
      run.inPass = true;
      run.passCounted = false;
      run.blockedUsInPass = 0;
    }
    run.beamBlocked = true;
    run.blockStartUs = edge.atUs;
    return;
  }

  if (!run.beamBlocked)
    return;
  run.beamBlocked = false;
  run.blockedUsInPass += edge.atUs - run.blockStartUs;
  run.lastClearUs = edge.atUs;
  if (!run.passCounted && run.blockedUsInPass >= PILL_MIN_BLOCK_US)
    countPill(index);
}

/** อ่านขอบที่ interrupt จดไว้ แล้วตัดสินว่ามีเม็ดยาผ่านไหม ทุกจานแยกกันจึงไม่กวนกัน */
void pollSensor(uint8_t index)
{
  if (!ENABLE_PILL_SENSOR)
    return;

  EdgeQueue &queue = edgeQueues[index];
  while (queue.tail != queue.head)
  {
    const SensorEdge edge = queue.items[queue.tail];
    queue.tail = static_cast<uint8_t>((queue.tail + 1) % EDGE_QUEUE_SIZE);
    handleEdge(index, edge);
  }

  Run &run = runs[index];
  if (queue.overflow)
  {
    // ขอบหายไปบางส่วน: บอกไม่ได้แล้วว่าช่วงที่หายไปมีเม็ดผ่านกี่เม็ด
    // เดาเอาไม่ได้ทั้งสองทาง: เติมระดับจริงลงไปจะยืดสัญญาณรบกวนจนนับเป็นเม็ด
    // ทิ้งไปเฉยๆ อาจทำเม็ดจริงหาย แล้วเครื่องหมุนปล่อยยาเกินจำนวน จึงหยุดจานนี้ไว้ก่อน
    // (ได้ไม่ครบจะถูกรายงานให้ผู้ใช้เห็น ซึ่งปลอดภัยกว่าให้ยาเกิน)
    queue.overflow = false;
    Serial.printf("[เซ็นเซอร์] จาน %u สัญญาณรบกวนถี่จนคิวเต็ม หยุดจ่าย ตรวจสายและการเล็งเลเซอร์\n",
                  static_cast<unsigned>(index + 1));
    run.beamBlocked = readSensor(index);
    run.inPass = false;
    run.passCounted = false;
    run.blockedUsInPass = 0;
    run.blockStartUs = run.lastClearUs = static_cast<uint32_t>(micros());
    run.targetReached = true;
    run.stopReason = StopReason::Noise;
    return;
  }

  const uint32_t nowUs = static_cast<uint32_t>(micros());
  if (run.beamBlocked)
  {
    const uint32_t blockedFor = nowUs - run.blockStartUs;
    // ยังบังอยู่แต่นานพอแล้ว: นับเลยไม่ต้องรอให้โล่ง จานจะได้หยุดหมุนเร็วขึ้น
    if (!run.passCounted && run.blockedUsInPass + blockedFor >= PILL_MIN_BLOCK_US)
      countPill(index);
    if (!run.jammed && blockedFor >= PILL_JAM_MS * 1000UL)
    {
      run.jammed = true;
      run.targetReached = true;  // พาจานกลับตำแหน่งพักแล้วจบ ห้ามปล่อยยาเพิ่มขณะมองไม่เห็น
      run.stopReason = StopReason::Jam;
      Serial.printf("[เซ็นเซอร์] จาน %u ลำแสงถูกบังค้างเกิน %lu ms มีเม็ดยาติด หยุดจ่าย\n",
                    static_cast<unsigned>(index + 1), static_cast<unsigned long>(PILL_JAM_MS));
    }
  }
  else if (run.inPass && nowUs - run.lastClearUs >= MERGE_GAP_US)
  {
    endPass(run);  // โล่งนานพอแล้ว การผ่านนี้จบ
  }
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
  laserOffIfIdle();

  DispenseOutcome outcome;
  outcome.dispenser = static_cast<uint8_t>(index + 1);
  outcome.requestedPills = run.requestedPills;
  outcome.attempts = run.attempts;
  outcome.cancelled = cancelled;
  outcome.sensorVerified = ENABLE_PILL_SENSOR;
  outcome.stopReason = cancelled ? StopReason::Cancelled : run.stopReason;
  if (!run.beamBlocked)
    endPass(run);  // การผ่านที่ค้างอยู่ตอนจบรอบ นับรวมในสถิติด้วย
  outcome.uncountedBlocks = run.uncountedBlocks;
  outcome.longestUncountedUs = run.longestUncountedUs;

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
      run.pillsAtAttemptStart = run.countedPills;
      servo.writeMicroseconds(HOLE_PULSE_US[index][currentHole(run)]);
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

  // No pill may be counted before this channel actually starts.
  if (run.phase == Phase::WaitingStart) {
    if (now - run.phaseStartedMs >= run.startDelayMs) {
      if (ENABLE_PILL_SENSOR && readSensor(index)) finishRun(index, false);
      else {
        if (ENABLE_PILL_SENSOR) resetDetector(index);
        beginPhase(index, Phase::Releasing, now);
      }
    }
    return;
  }
  tickKick(index, now);
  pollSensor(index);

  const unsigned long elapsed = now - run.phaseStartedMs;

  switch (run.phase)
  {
    case Phase::WaitingStart:
      if (elapsed >= run.startDelayMs)
        beginPhase(index, Phase::Releasing, now);
      return;

    case Phase::Releasing:
      if (!run.targetReached && elapsed < MOVE_TIME_MS)
        return;
      // ถึงปลายทางแล้ว ต้องพาจานกลับตำแหน่งพักเสมอ แม้จะได้เม็ดครบแล้วก็ตาม
      beginPhase(index, Phase::Returning, now);
      return;

    case Phase::Returning:
      if (elapsed < MOVE_TIME_MS)
        return;

      // ไม่มีเซ็นเซอร์: นับหนึ่งเม็ดต่อหนึ่งรอบหมุน เพราะไม่มีอะไรยืนยันได้ดีกว่านี้
      //
      // ถ้าไม่นับตรงนี้ targetReached จะไม่มีวันเป็นจริง จานจะวนไปทุกช่อง
      // ทุกครั้งที่จ่าย (ราว 16 วินาทีต่อหนึ่งเม็ด) แล้วค่อยจบแบบรายงานว่าสำเร็จ
      if (!ENABLE_PILL_SENSOR)
      {
        if (run.countedPills < 255)
          ++run.countedPills;
        if (run.countedPills >= run.requestedPills)
          run.targetReached = true;
      }

      if (run.targetReached)
      {
        finishRun(index, false);
        return;
      }
      beginPhase(index, Phase::Shaking, now);
      return;

    case Phase::Shaking:
    {
      if (elapsed < SHAKE_TIME_MS)
        return;

      // เม็ดอาจหลุดตอนเขย่านี่เอง จึงต้องเช็คอีกครั้งก่อนหมุนรอบใหม่
      if (run.targetReached)
      {
        finishRun(index, false);
        return;
      }

      // ไม่มีเซ็นเซอร์: รู้ไม่ได้ว่ายาตกหรือยัง จึงห้ามย้ายช่องหรือวนซ้ำเกินจำนวนเม็ด
      // (นับหนึ่งเม็ดต่อรอบไปแล้วตอน Returning จึงจบเองเมื่อครบ)
      if (ENABLE_PILL_SENSOR)
      {
        // ตัดสินผลของรอบนี้หลังเขย่าเสร็จ เพราะเม็ดมักหลุดตอนเขย่า ไม่ใช่ตอนหมุน
        const bool dropped = run.countedPills > run.pillsAtAttemptStart;
        if (dropped)
        {
          run.holeMisses = 0;  // ช่องนี้ใช้ได้ อยู่ช่องเดิมต่อสำหรับเม็ดถัดไป
        }
        else if (++run.holeMisses >= ATTEMPTS_PER_HOLE)
        {
          Serial.printf("[จ่ายยา] จาน %u ช่อง %u ลอง %u รอบแล้วไม่มีเม็ดตก\n",
                        static_cast<unsigned>(index + 1),
                        static_cast<unsigned>(currentHole(run)),
                        static_cast<unsigned>(ATTEMPTS_PER_HOLE));
          run.holeMisses = 0;
          if (++run.holePos >= run.holeCount)
          {
            Serial.printf("[จ่ายยา] จาน %u ลองครบทุกช่องแล้ว ได้ %u/%u เม็ด\n",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(run.countedPills),
                          static_cast<unsigned>(run.requestedPills));
            run.stopReason = StopReason::AllHolesTried;
            finishRun(index, false);
            return;
          }
          Serial.printf("[จ่ายยา] จาน %u ย้ายไปช่อง %u\n",
                        static_cast<unsigned>(index + 1),
                        static_cast<unsigned>(currentHole(run)));
        }
      }

      // กันหลุดซ้อนอีกชั้น ตามตรรกะข้างบนไม่ควรมาถึงได้
      if (run.attempts >= MAX_TOTAL_ATTEMPTS)
      {
        run.stopReason = StopReason::AttemptLimit;
        finishRun(index, false);
        return;
      }
      beginPhase(index, Phase::Releasing, now);
      return;
    }

    case Phase::Idle:
      return;
  }
}

}  // namespace

void dispenserSafePinsEarly()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    // ใช้ digitalWrite ไม่ใช่ analogWrite ตรงนี้ เพราะยังไม่ได้ผูกขาเข้ากับ LEDC
    // และต้องการให้ขาเป็น LOW ภายในไม่กี่ไมโครวินาที ไม่ต้องรอ LEDC ตั้งค่าเสร็จ
    //
    // IN1 = IN2 = LOW คือสถานะ coast ของ DRV8833 มอเตอร์หยุดหมุนอิสระ
    pinMode(VIB_DIR_PINS[index], OUTPUT);
    digitalWrite(VIB_DIR_PINS[index], LOW);
    pinMode(VIB_PWM_PINS[index], OUTPUT);
    digitalWrite(VIB_PWM_PINS[index], LOW);
  }
}

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
    blockedLevel[index] = PILL_SENSOR_ACTIVE_LOW ? LOW : HIGH;
    sensorFault[index] = false;
    sensorTested[index] = false;
    if (ENABLE_PILL_SENSOR)
      attachInterruptArg(digitalPinToInterrupt(PILL_SENSOR_PINS[index]), onSensorEdge,
                         reinterpret_cast<void *>(static_cast<uintptr_t>(index)), CHANGE);
  }

  // PCF8574 เริ่มทำงานด้วยทุกขาเป็น HIGH (relay ดับ) อยู่แล้ว สั่งซ้ำไว้เผื่อชิปค้างสถานะจากก่อนรีเซ็ต
  // (รีเซ็ต ESP32 อย่างเดียวไม่ได้ตัดไฟชิป) ต้องทำหลัง Wire.begin() ซึ่งอยู่ใน setup()
  if (ENABLE_LASER_SWITCH)
  {
    laserWrite(false);
    laserLit = false;
  }

  outcomeHead = 0;
  outcomeCount = 0;
}

void dispenserHomeAll()
{
  if (!ENABLE_SERVO_MOVEMENT)
    return;

  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    Servo &servo = dispenserServos[index];
    servo.attach(SERVO_PINS[index], SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    if (!servo.attached())
    {
      Serial.printf("[servo] จาน %u ต่อ servo ไม่ได้ ข้ามการกลับกึ่งกลาง\n", static_cast<unsigned>(index + 1));
      continue;
    }
    servo.writeMicroseconds(REST_PULSE_US[index]);
    delay(MOVE_TIME_MS);
    servo.detach();
  }
}

DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t pills, int8_t preferredHole)
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

  // ต้องเปิดเลเซอร์ก่อนอ่าน ไม่อย่างนั้นตัวรับไม่เห็นแสงและรายงานว่าถูกบังทุกครั้ง
  laserEnsureOn();
  // สั่งเปิดไม่ได้ = ตัวรับมืดทุกตัว ต้องบอกให้ตรงว่าเป็นที่สาย I2C ไม่ใช่ที่ช่องจ่ายยา
  if (ENABLE_LASER_SWITCH && ENABLE_PILL_SENSOR && !laserLit)
    return DispenseResult::LaserOff;
  if (ENABLE_PILL_SENSOR && (sensorFault[index] || readSensor(index)))
  {
    laserOffIfIdle();
    return DispenseResult::SensorBlocked;
  }

  Servo &servo = dispenserServos[index];
  servo.attach(SERVO_PINS[index], SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
  if (!servo.attached())
  {
    laserOffIfIdle();
    return DispenseResult::ServoError;
  }

  Run &run = runs[index];
  run.requestedPills = pills;
  run.countedPills = 0;
  run.attempts = 0;
  run.targetReached = false;
  run.stopReason = StopReason::Done;
  run.uncountedBlocks = 0;
  run.longestUncountedUs = 0;

  // ลำดับช่องที่จะลอง: ช่องตามขนาดที่ผู้ใช้กรอกก่อน แล้วเฉพาะช่องที่ใหญ่กว่าไล่ขึ้นไป
  // ไม่ย้อนไปช่องที่เล็กกว่า เพราะเม็ดที่ไม่ผ่านช่องขนาดตัวเองย่อมไม่ผ่านช่องที่เล็กกว่าแน่นอน
  // ไม่ได้กรอกขนาด (หรือค่าผิดช่วง) = ไล่จากช่องเล็กสุดไปใหญ่สุด
  const bool hasPreferred = preferredHole >= 0 && preferredHole < static_cast<int8_t>(PILL_HOLE_COUNT);
  run.holeCount = 0;
  const uint8_t firstHole = hasPreferred ? static_cast<uint8_t>(preferredHole) : 0;
  for (uint8_t hole = firstHole; hole < PILL_HOLE_COUNT; ++hole)
    run.holeOrder[run.holeCount++] = hole;
  run.holePos = 0;
  run.holeMisses = 0;

  Serial.printf("[จ่ายยา] จาน %u ขอ %u เม็ด เริ่มที่ช่อง %u%s\n",
                static_cast<unsigned>(dispenser),
                static_cast<unsigned>(pills),
                static_cast<unsigned>(currentHole(run)),
                hasPreferred ? " (ตามขนาดยาที่กรอก)" : " (ไม่ได้กรอกขนาด เริ่มช่องเล็กสุด)");

  const unsigned long now = millis();

  // เริ่มตัวนับของรอบนี้ใหม่: ทิ้งขอบค้างจากรอบก่อนและจากตอนเลเซอร์เพิ่งติด
  // ไม่อย่างนั้นแสงดับตอนจบรอบก่อน หรือแสงติดตอนนี้ จะถูกนับเป็นเม็ดยา
  if (ENABLE_PILL_SENSOR)
    resetDetector(index);

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

void dispenserSensorSelfTest()
{
  // เปิดเลเซอร์แป๊บเดียวให้ laserEnsureOn() เทียบตอนมืดกับตอนสว่างของทุกจาน แล้วดับ
  // ห้ามทำตอนมีจานทำงาน: เลเซอร์ต้องติดค้างให้จานนั้น และการเทียบต้องเริ่มจากตอนดับ
  if (!ENABLE_LASER_SWITCH || !ENABLE_PILL_SENSOR || activeCount() > 0 || laserLit)
    return;
  laserEnsureOn();
  laserOffIfIdle();
}

bool laserSwitchResponding()
{
  return !ENABLE_LASER_SWITCH || laserSwitchOk;
}

int8_t pillSensorStatus(uint8_t dispenser)
{
  if (!ENABLE_PILL_SENSOR || dispenser < 1 || dispenser > DISPENSER_COUNT)
    return -1;
  const uint8_t index = static_cast<uint8_t>(dispenser - 1);
  if (ENABLE_LASER_SWITCH && !sensorTested[index])
    return -1;
  return sensorFault[index] ? 0 : 1;
}

bool pillSensorBlocked(uint8_t dispenser)
{
  // ปิดเซ็นเซอร์อยู่ = ขายังลอย การอ่านจะได้ค่าสุ่ม จึงตอบว่าไม่ถูกบังไปเลย
  // ไม่อย่างนั้นหน้าเว็บวินิจฉัยจะโชว์ค่ามั่วให้เข้าใจผิดว่าเซ็นเซอร์ทำงานอยู่
  if (!ENABLE_PILL_SENSOR)
    return false;
  if (dispenser < 1 || dispenser > DISPENSER_COUNT)
    return false;
  // เลเซอร์ดับอยู่ (ไม่ได้จ่ายยา) ตัวรับไม่เห็นแสงจึงอ่านได้ว่าถูกบังเสมอ ค่านั้นไม่มีความหมาย
  if (ENABLE_LASER_SWITCH && !laserLit)
    return false;
  return readSensor(static_cast<uint8_t>(dispenser - 1));
}
