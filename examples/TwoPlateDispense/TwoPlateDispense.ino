/**
 * TwoPlateDispense — ทดสอบจานหมุน 2 จานพร้อมกัน
 *
 * แต่ละจาน = Servo 270° 1 ตัว + มอเตอร์สั่น 1 ตัว (ช่องหนึ่งของ DRV8833)
 * DRV8833 หนึ่งตัวมี 2 ช่อง จึงคุมมอเตอร์สั่นได้ครบ 2 ตัวโดยไม่ต้องเพิ่มบอร์ด
 *
 * ต่างจาก sketch ทดสอบเดิมตรงที่ **ไม่มี delay() ในลูปเลย**
 * ทุกจังหวะคุมด้วย millis() ทั้งสองจานจึงหมุนทับเวลากันได้จริง
 * ถ้าใช้ delay() จานที่สองจะต้องรอจานแรกหมุนจบก่อนเสมอ
 *
 * สั่งงานผ่าน Serial Monitor (115200):
 *   1 / 2  = หมุนจานนั้น 1 รอบ
 *   b      = หมุนทั้งสองจานพร้อมกัน 1 รอบ
 *   r      = หมุนทั้งสองจานพร้อมกัน CYCLES_PER_RUN รอบ
 *   s      = หยุดทันทีและกลับตำแหน่งพัก
 */

#if defined(ESP32)
#include <ESP32Servo.h>
#else
#include <Servo.h>
#endif

// ---------------------------------------------------------------------------
// ผังขา
// ---------------------------------------------------------------------------
//
// IN1 ของแต่ละช่องคือขาที่รับ PWM (คุมความแรงสั่น) จึงต้องเป็นขาที่ทำ PWM ได้
// IN2 เป็นขาทิศทาง/เบรก ใช้ digital ธรรมดา
//
// DRV8833 ต้องต่อ GND ร่วมกับบอร์ด และขา SLP/nSLEEP ต้องเป็น HIGH
// (บอร์ดสำเร็จรูปหลายรุ่นต่อค้างไว้ให้แล้ว ถ้ารุ่นของคุณแยกขาออกมาให้ต่อเข้า 3.3V/5V)

constexpr uint8_t PLATE_COUNT = 2;

#if defined(ESP32)
constexpr uint8_t SERVO_PIN[PLATE_COUNT] = {18, 19};
constexpr uint8_t VIB_PWM_PIN[PLATE_COUNT] = {13, 26};  // AIN1, BIN1
constexpr uint8_t VIB_DIR_PIN[PLATE_COUNT] = {4, 16};   // AIN2, BIN2
#else
// Arduino Uno/Nano: ไลบรารี Servo ยึด Timer1 ไว้ ทำให้ analogWrite ขา 9/10 ใช้ไม่ได้
// จึงให้ขา 9/10 เป็นสาย servo และย้าย PWM ของมอเตอร์ไปขา 5/6 (Timer0)
constexpr uint8_t SERVO_PIN[PLATE_COUNT] = {9, 10};
constexpr uint8_t VIB_PWM_PIN[PLATE_COUNT] = {5, 6};
constexpr uint8_t VIB_DIR_PIN[PLATE_COUNT] = {7, 8};
#endif

// ---------------------------------------------------------------------------
// ค่าการเคลื่อนไหว — ปรับทีละจานได้ เพราะกลไกแต่ละจานไม่เหมือนกัน
// ---------------------------------------------------------------------------

constexpr int SERVO_MIN_US = 500;
constexpr int SERVO_MAX_US = 2500;
constexpr int SERVO_MAX_ANGLE = 270;

constexpr int REST_ANGLE[PLATE_COUNT] = {135, 135};       // ตำแหน่งพัก
constexpr int DISPENSE_ANGLE[PLATE_COUNT] = {270, 0};     // จานซ้ายกวาดไปทางหนึ่ง จานขวาอีกทางหนึ่ง

// เวลาที่ servo ใช้หมุนจนถึงตำแหน่ง — ต้องวัดจากของจริง ไม่ใช่เดา
// ถ้าตั้งสั้นไปจะเบรกมอเตอร์ก่อนจานหมุนถึง ยาจะไม่ตก
constexpr unsigned long TRAVEL_MS = 500;

// ความแรงสั่น 0-255 (ช่วงที่ใช้ได้จริงประมาณ 60-130)
constexpr uint8_t VIB_SPEED = 70;
// กระตุกเต็มกำลังสั้นๆ ให้ตุ้มถ่วงออกตัว แล้วค่อยผ่อนลงมาที่ VIB_SPEED
constexpr uint8_t VIB_KICK_SPEED = 255;
constexpr unsigned long VIB_KICK_MS = 40;

constexpr uint8_t CYCLES_PER_RUN = 10;

// ---------------------------------------------------------------------------

/**
 * จานหนึ่งใบ: servo + มอเตอร์สั่น พร้อมลำดับการทำงานของตัวเอง
 *
 * หนึ่งรอบ = สั่น + หมุนไปตำแหน่งจ่าย -> เบรกมอเตอร์ + หมุนกลับตำแหน่งพัก
 * เบรกตอนหมุนกลับเพื่อล็อกแกนไม่ให้ตุ้มถ่วงเหวี่ยงจนยาตกเพิ่ม
 */
class Plate {
 public:
  void begin(uint8_t index) {
    idx_ = index;
    pinMode(VIB_PWM_PIN[idx_], OUTPUT);
    pinMode(VIB_DIR_PIN[idx_], OUTPUT);
    coast();
    servo_.attach(SERVO_PIN[idx_], SERVO_MIN_US, SERVO_MAX_US);
    writeAngle(REST_ANGLE[idx_]);
  }

  /** เริ่มหมุนจำนวนรอบที่ขอ; เรียกซ้ำระหว่างที่ยังหมุนอยู่จะถูกเมิน */
  void start(uint8_t cycles, unsigned long now) {
    if (cycles == 0 || busy()) return;
    remaining_ = cycles;
    beginOutStroke(now);
  }

  bool busy() const { return state_ != State::Idle; }

  /** เรียกทุกลูป ห้าม block */
  void update(unsigned long now) {
    tickKick(now);
    if (state_ == State::Idle) return;
    if (now - phaseStartedMs_ < TRAVEL_MS) return;

    if (state_ == State::MovingOut) {
      brake();
      writeAngle(REST_ANGLE[idx_]);
      state_ = State::MovingBack;
      phaseStartedMs_ = now;
      return;
    }

    // หมุนกลับถึงตำแหน่งพักแล้ว = จบหนึ่งรอบ
    remaining_--;
    if (remaining_ > 0) {
      beginOutStroke(now);
    } else {
      coast();
      state_ = State::Idle;
    }
  }

  /** หยุดทันที: ดับมอเตอร์และสั่ง servo กลับตำแหน่งพัก */
  void abort() {
    remaining_ = 0;
    state_ = State::Idle;
    coast();
    writeAngle(REST_ANGLE[idx_]);
  }

 private:
  enum class State : uint8_t { Idle, MovingOut, MovingBack };

  void beginOutStroke(unsigned long now) {
    vibrate(now);
    writeAngle(DISPENSE_ANGLE[idx_]);
    state_ = State::MovingOut;
    phaseStartedMs_ = now;
  }

  void writeAngle(int angle) {
    const int us = map(constrain(angle, 0, SERVO_MAX_ANGLE), 0, SERVO_MAX_ANGLE,
                       SERVO_MIN_US, SERVO_MAX_US);
    servo_.writeMicroseconds(us);
  }

  // ทุกสถานะของมอเตอร์สั่งผ่าน analogWrite ที่ขา IN1 เสมอ
  // ถ้าสลับไปใช้ digitalWrite ที่ขาเดิม ตัว PWM ของ ESP32 อาจยังคาอยู่ที่ขานั้น
  void vibrate(unsigned long now) {
    digitalWrite(VIB_DIR_PIN[idx_], LOW);
    analogWrite(VIB_PWM_PIN[idx_], VIB_KICK_SPEED);
    kicking_ = true;
    kickStartedMs_ = now;
  }

  /** IN1 = IN2 = HIGH คือเบรกแบบล็อกแกน */
  void brake() {
    kicking_ = false;
    digitalWrite(VIB_DIR_PIN[idx_], HIGH);
    analogWrite(VIB_PWM_PIN[idx_], 255);
  }

  /** ปล่อยให้หมุนอิสระจนหยุดเอง */
  void coast() {
    kicking_ = false;
    digitalWrite(VIB_DIR_PIN[idx_], LOW);
    analogWrite(VIB_PWM_PIN[idx_], 0);
  }

  /** ผ่อนกำลังจากจังหวะกระตุกลงมาที่ความแรงปกติ โดยไม่ต้อง delay */
  void tickKick(unsigned long now) {
    if (!kicking_) return;
    if (now - kickStartedMs_ < VIB_KICK_MS) return;
    analogWrite(VIB_PWM_PIN[idx_], VIB_SPEED);
    kicking_ = false;
  }

  Servo servo_;
  uint8_t idx_ = 0;
  State state_ = State::Idle;
  uint8_t remaining_ = 0;
  unsigned long phaseStartedMs_ = 0;
  unsigned long kickStartedMs_ = 0;
  bool kicking_ = false;
};

Plate plates[PLATE_COUNT];
bool wasBusy[PLATE_COUNT] = {false, false};

// Arduino IDE เติม prototype ให้เอง แต่ประกาศไว้เองด้วยเพื่อให้ compiler อื่นตรวจไฟล์นี้ได้
void handleSerial(unsigned long now);

void setup() {
  Serial.begin(115200);

#if defined(ESP32)
  // จอง LEDC timer ให้ ESP32Servo แค่ 2 ตัวตามจำนวน servo
  // เหลือ timer ที่เหลือไว้ให้ analogWrite ของมอเตอร์สั่น ถ้าจองครบ 4 ตัว PWM มอเตอร์จะไม่ทำงาน
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
#endif

  for (uint8_t i = 0; i < PLATE_COUNT; i++) {
    plates[i].begin(i);
  }

  Serial.println();
  Serial.println(F("TwoPlateDispense พร้อมใช้งาน"));
  Serial.println(F("  1 / 2 = หมุนจานนั้น 1 รอบ"));
  Serial.println(F("  b     = สองจานพร้อมกัน 1 รอบ"));
  Serial.println(F("  r     = สองจานพร้อมกันหลายรอบ"));
  Serial.println(F("  s     = หยุด"));
}

void loop() {
  const unsigned long now = millis();

  handleSerial(now);

  for (uint8_t i = 0; i < PLATE_COUNT; i++) {
    plates[i].update(now);

    const bool busy = plates[i].busy();
    if (wasBusy[i] && !busy) {
      Serial.print(F("จาน "));
      Serial.print(i + 1);
      Serial.println(F(" หมุนครบแล้ว"));
    }
    wasBusy[i] = busy;
  }
}

void handleSerial(unsigned long now) {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    switch (c) {
      case '1':
        plates[0].start(1, now);
        break;
      case '2':
        plates[1].start(1, now);
        break;
      case 'b':
      case 'B':
        // สั่งด้วย now ค่าเดียวกัน ทั้งสองจานจึงออกตัวพร้อมกันจริง
        plates[0].start(1, now);
        plates[1].start(1, now);
        break;
      case 'r':
      case 'R':
        plates[0].start(CYCLES_PER_RUN, now);
        plates[1].start(CYCLES_PER_RUN, now);
        break;
      case 's':
      case 'S':
        for (uint8_t i = 0; i < PLATE_COUNT; i++) plates[i].abort();
        Serial.println(F("หยุดทุกจาน"));
        break;
      default:
        break;  // ข้าม newline และตัวอักษรอื่น
    }
  }
}
