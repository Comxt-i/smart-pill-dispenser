/**
 * UnoPillCount — ทดสอบลำดับจ่ายยาแบบวนจนเซ็นเซอร์ IR นับเม็ดได้ครบ
 *
 * ลำดับหนึ่งรอบ (ตามที่ทดสอบกับกลไกจริงแล้วได้ผลดีที่สุด):
 *
 *   1. หมุนไปตำแหน่งจ่าย  + สั่น      -> กวาดยาให้ไหลลงรู
 *   2. หมุนกลับตำแหน่งพัก + เบรกมอเตอร์ -> ล็อกแกนไม่ให้ยาตกเกิน
 *   3. อยู่นิ่ง            + สั่นค้าง    -> สะบัดเม็ดที่ค้างอยู่ด้านล่างให้หลุดลง
 *
 * แล้ววนใหม่จนกว่า IR จะนับเม็ดได้ครบ หรือจนครบ MAX_ATTEMPTS
 *
 * จังหวะที่ 3 จำเป็น เพราะบางครั้งเม็ดยาหลุดจากจานแล้วแต่ยังค้างอยู่ด้านล่าง
 * การหมุนอย่างเดียวไม่ทำให้มันตกลงไป
 *
 * สั่งงานด้วยปุ่มบนตัวเครื่อง จึงทดสอบได้โดยไม่ต้องต่อคอมพิวเตอร์:
 *   K1 / K2 = จ่ายยาจากจานนั้น PILLS_PER_RUN เม็ด
 *   K3      = จ่ายทั้งสองจานพร้อมกัน
 * ไฟบนบอร์ด (ขา 13) ติดขณะยังทำงานอยู่
 *
 * ถ้าต่อ Serial Monitor (115200) ไว้ จะเห็นการนับเม็ดแบบเรียลไทม์
 * และสั่งด้วย 1 / 2 / b / s ได้ด้วย
 */

#include <Servo.h>

// ---------------------------------------------------------------------------
// ผังขา — เลือกมาให้เลี่ยงการแย่ง timer กันแล้ว
// ---------------------------------------------------------------------------
//
// ไลบรารี Servo ยึด Timer1 ไว้ ทำให้ analogWrite ที่ขา 9 และ 10 ใช้ไม่ได้
// จึงให้ 9/10 เป็นสายสัญญาณ servo และย้าย PWM ของมอเตอร์ไปขา 3/11 (Timer2)
//
// ไม่ใช้ขา 5/6 เพราะอยู่บน Timer0 ตัวเดียวกับ millis()
// ทำให้ analogWrite(pin, 0) ยังมีพัลส์เล็ดลอดออกมา มอเตอร์จะไม่ดับสนิท

const uint8_t PLATE_COUNT = 2;

const uint8_t SERVO_PIN[PLATE_COUNT]   = {9, 10};   // สายสัญญาณ servo
const uint8_t VIB_PWM_PIN[PLATE_COUNT] = {3, 11};   // DRV8833 AIN1 / BIN1 (ต้องเป็นขา PWM)
const uint8_t VIB_DIR_PIN[PLATE_COUNT] = {7, 8};    // DRV8833 AIN2 / BIN2 (digital ธรรมดา)

// เซ็นเซอร์ IR ตรวจเม็ดยาที่ตกลงมา จานละหนึ่งตัว (A0/A1 ใช้เป็น digital input ได้)
const uint8_t IR_PIN[PLATE_COUNT] = {A0, A1};

// โมดูล IR ส่วนใหญ่ให้เอาต์พุต LOW เมื่อลำแสงถูกบัง
const bool IR_ACTIVE_LOW = true;

// โมดูลที่เอาต์พุตเป็น open-collector ต้องใช้ pull-up ในตัวชิป
// ถ้าโมดูลขับสัญญาณเองแบบ push-pull อยู่แล้ว ตั้งเป็น false ได้
const bool IR_USE_PULLUP = true;

const uint8_t BUTTON_COUNT = 3;
const uint8_t BUTTON_PIN[BUTTON_COUNT] = {2, 4, 12};  // K1 = จาน 1, K2 = จาน 2, K3 = ทั้งคู่
const unsigned long BUTTON_DEBOUNCE_MS = 40;

const uint8_t STATUS_LED_PIN = LED_BUILTIN;

// ---------------------------------------------------------------------------
// ค่าการเคลื่อนไหว
// ---------------------------------------------------------------------------

const int SERVO_MIN_US = 500;
const int SERVO_MAX_US = 2500;
const int SERVO_SPAN_DEG = 270;

// เผื่อจากปลายสุดเข้ามาข้างละ 5 องศา
// ถ้าได้ยินเสียงเค้นตอนถึงปลายทาง ให้ดึงเข้ามาเป็น 574/2426 แล้วลด SWEEP_DEG
const int SAFE_MIN_US = 537;
const int SAFE_MAX_US = 2463;

const int REST_DEG[PLATE_COUNT] = {135, 135};

// ทิศกวาดของแต่ละจาน: -1 = ซ้าย, +1 = ขวา (มองจากด้านบนของจาน)
const int SWEEP_DIR[PLATE_COUNT] = {-1, +1};
const int SWEEP_DEG[PLATE_COUNT] = {130, 130};

// เวลาที่ servo ใช้หมุนจนถึงตำแหน่ง — ต้องวัดจากของจริง ไม่ใช่เดา
const unsigned long TRAVEL_MS = 700;

// เขย่าอยู่กับที่หลังหมุนกลับถึงตำแหน่งพักนานเท่าไร
const unsigned long SHAKE_MS = 600;

// วนได้สูงสุดกี่รอบก่อนยอมแพ้ ถ้าไม่มีเพดานแล้วช่องยาว่าง จะวนไม่จบ
const uint8_t MAX_ATTEMPTS = 8;

// จำนวนเม็ดที่สั่งจ่ายต่อการกดหนึ่งครั้ง
const uint8_t PILLS_PER_RUN = 1;

// หลังนับหนึ่งเม็ดแล้วไม่รับสัญญาณใหม่นานเท่านี้
// กันเม็ดเดียวที่กระเด้งหรือหมุนตัวผ่านลำแสงถูกนับหลายครั้ง
const unsigned long DETECT_LOCKOUT_MS = 60;

const uint8_t VIB_SPEED = 70;
const uint8_t VIB_KICK_SPEED = 255;
const unsigned long VIB_KICK_MS = 40;

// หน่วงจานที่สองก่อนออกตัว ไม่ให้กระแสพุ่งของ servo สองตัวซ้อนกันพอดี
const unsigned long START_STAGGER_MS = 250;

// ---------------------------------------------------------------------------

/**
 * แปลงองศาเป็นความกว้าง pulse
 *
 * int ของ Uno กว้างแค่ 16 บิต และ 270 x 2000 = 540000 ล้นทันที
 * จึงต้องบังคับให้คูณกันในชนิด long ก่อน
 */
int angleToUs(int deg) {
  const long span = (long)SERVO_MAX_US - SERVO_MIN_US;
  return (int)(SERVO_MIN_US + (deg * span + SERVO_SPAN_DEG / 2) / SERVO_SPAN_DEG);
}

int targetDeg(uint8_t plate) {
  return REST_DEG[plate] + SWEEP_DIR[plate] * SWEEP_DEG[plate];
}

/**
 * จานหนึ่งใบ: servo + มอเตอร์สั่น + เซ็นเซอร์ IR
 *
 * วนหมุน-เขย่าจนเซ็นเซอร์นับเม็ดได้ครบ แล้วจึงหยุด
 * ไม่มี delay() ในลูป ทั้งสองจานจึงทำงานทับเวลากันได้
 */
class Plate {
 public:
  void begin(uint8_t index) {
    idx_ = index;
    pinMode(VIB_PWM_PIN[idx_], OUTPUT);
    pinMode(VIB_DIR_PIN[idx_], OUTPUT);
    pinMode(IR_PIN[idx_], IR_USE_PULLUP ? INPUT_PULLUP : INPUT);
    coast();
    servo_.attach(SERVO_PIN[idx_], SERVO_MIN_US, SERVO_MAX_US);
    writeUs(angleToUs(REST_DEG[idx_]));
    sensorWasBlocked_ = beamBlocked();
  }

  /** true เมื่อลำแสงถูกบังอยู่ตอนนี้ (อ่านดิบ ใช้ตรวจการต่อสาย) */
  bool beamBlocked() const {
    const int level = digitalRead(IR_PIN[idx_]);
    return IR_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
  }

  void start(uint8_t pills, unsigned long now, unsigned long delayMs = 0) {
    if (pills == 0 || busy()) return;

    requested_ = pills;
    counted_ = 0;
    attempts_ = 0;
    targetReached_ = false;

    // ช่วงกันนับซ้ำต้องนับจากเม็ดก่อนหน้าในรอบนี้เท่านั้น
    // ถ้าปล่อยให้ค้างจากรอบก่อน เม็ดแรกของรอบใหม่จะถูกกลืนหายไป
    lastDetectMs_ = now - DETECT_LOCKOUT_MS - 1;

    // อ่านสถานะเริ่มต้นไว้ ไม่งั้นถ้าลำแสงถูกบังค้างอยู่ตั้งแต่ต้น
    // ขอบขาลงแรกจะถูกนับทั้งที่ไม่มีเม็ดยาตกใหม่
    sensorWasBlocked_ = beamBlocked();

    Serial.print(F("plate "));
    Serial.print(idx_ + 1);
    Serial.print(F(": dispensing "));
    Serial.print(requested_);
    Serial.println(F(" pill(s)"));

    if (delayMs == 0) {
      beginPhase(Releasing, now);
      return;
    }
    startDelayMs_ = delayMs;
    phase_ = WaitingStart;
    phaseStartedMs_ = now;
  }

  bool busy() const { return phase_ != Idle; }
  uint8_t counted() const { return counted_; }

  /** เรียกทุกลูป ห้าม block */
  void update(unsigned long now) {
    tickKick(now);
    if (phase_ == Idle) return;

    pollSensor(now);

    if (phase_ == WaitingStart) {
      if (now - phaseStartedMs_ >= startDelayMs_) beginPhase(Releasing, now);
      return;
    }

    const unsigned long elapsed = now - phaseStartedMs_;

    if (phase_ == Releasing) {
      if (elapsed < TRAVEL_MS) return;
      // ต้องพาจานกลับตำแหน่งพักเสมอ แม้จะได้เม็ดครบแล้วก็ตาม
      beginPhase(Returning, now);
      return;
    }

    if (phase_ == Returning) {
      if (elapsed < TRAVEL_MS) return;
      if (targetReached_) { finish(now, true); return; }
      if (attempts_ >= MAX_ATTEMPTS) { finish(now, false); return; }
      beginPhase(Shaking, now);
      return;
    }

    // Shaking
    if (elapsed < SHAKE_MS) return;
    // เม็ดอาจหลุดตอนเขย่านี่เอง จึงต้องเช็คอีกครั้งก่อนหมุนรอบใหม่
    if (targetReached_) { finish(now, true); return; }
    beginPhase(Releasing, now);
  }

  void abort(unsigned long now) {
    if (phase_ == Idle) return;
    finish(now, false);
  }

 private:
  enum Phase { Idle, WaitingStart, Releasing, Returning, Shaking };

  void beginPhase(Phase next, unsigned long now) {
    switch (next) {
      case Releasing:
        ++attempts_;
        vibrateOn(now);
        writeUs(angleToUs(targetDeg(idx_)));
        break;
      case Returning:
        brake();
        writeUs(angleToUs(REST_DEG[idx_]));
        break;
      case Shaking:
        // จานอยู่ที่ตำแหน่งพักแล้ว ไม่สั่ง servo ซ้ำ ให้สั่นอย่างเดียว
        vibrateOn(now);
        break;
      default:
        break;
    }
    phase_ = next;
    phaseStartedMs_ = now;
  }

  void finish(unsigned long now, bool success) {
    (void)now;
    coast();
    writeUs(angleToUs(REST_DEG[idx_]));
    phase_ = Idle;

    Serial.print(F("plate "));
    Serial.print(idx_ + 1);
    Serial.print(success ? F(": OK ") : F(": INCOMPLETE "));
    Serial.print(counted_);
    Serial.print('/');
    Serial.print(requested_);
    Serial.print(F(" pills in "));
    Serial.print(attempts_);
    Serial.println(F(" attempt(s)"));
  }

  /**
   * นับเม็ดที่เพิ่งตกผ่านลำแสง
   *
   * จับเฉพาะขอบขาลง (ว่าง -> ถูกบัง) และล็อกไว้ DETECT_LOCKOUT_MS
   * เพื่อไม่ให้เม็ดเดียวที่กระเด้งหรือหมุนตัวถูกนับหลายครั้ง
   */
  void pollSensor(unsigned long now) {
    const bool blocked = beamBlocked();
    const bool wasBlocked = sensorWasBlocked_;
    sensorWasBlocked_ = blocked;

    if (!blocked || wasBlocked) return;
    if (now - lastDetectMs_ < DETECT_LOCKOUT_MS) return;

    lastDetectMs_ = now;
    if (counted_ < 255) ++counted_;

    Serial.print(F("  [IR] plate "));
    Serial.print(idx_ + 1);
    Serial.print(F(" pill "));
    Serial.print(counted_);
    Serial.print('/');
    Serial.println(requested_);

    if (counted_ >= requested_) targetReached_ = true;
  }

  // ทุกสถานะของมอเตอร์สั่งผ่าน analogWrite ที่ขา IN1 เสมอ
  void writeUs(int us) {
    servo_.writeMicroseconds(constrain(us, SAFE_MIN_US, SAFE_MAX_US));
  }

  void vibrateOn(unsigned long now) {
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

  void coast() {
    kicking_ = false;
    digitalWrite(VIB_DIR_PIN[idx_], LOW);
    analogWrite(VIB_PWM_PIN[idx_], 0);
  }

  void tickKick(unsigned long now) {
    if (!kicking_) return;
    if (now - kickStartedMs_ < VIB_KICK_MS) return;
    analogWrite(VIB_PWM_PIN[idx_], VIB_SPEED);
    kicking_ = false;
  }

  Servo servo_;
  uint8_t idx_ = 0;
  Phase phase_ = Idle;
  uint8_t requested_ = 0;
  uint8_t counted_ = 0;
  uint8_t attempts_ = 0;
  bool targetReached_ = false;
  bool sensorWasBlocked_ = false;
  unsigned long phaseStartedMs_ = 0;
  unsigned long kickStartedMs_ = 0;
  unsigned long startDelayMs_ = 0;
  unsigned long lastDetectMs_ = 0;
  bool kicking_ = false;
};

Plate plates[PLATE_COUNT];

/**
 * ปุ่ม active-LOW พร้อม debounce แบบไม่ block
 * คืน true เฉพาะ "จังหวะที่เพิ่งถูกกดลง" ครั้งเดียว ไม่ใช่ตลอดเวลาที่กดค้าง
 */
class Button {
 public:
  void begin(uint8_t pin) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
    stable_ = digitalRead(pin_);
    lastRaw_ = stable_;
  }

  bool pressed(unsigned long now) {
    const int raw = digitalRead(pin_);
    if (raw != lastRaw_) {
      lastRaw_ = raw;
      lastChangeMs_ = now;
      return false;
    }
    if (now - lastChangeMs_ < BUTTON_DEBOUNCE_MS) return false;
    if (raw == stable_) return false;

    stable_ = raw;
    return stable_ == LOW;
  }

 private:
  uint8_t pin_ = 0;
  int stable_ = HIGH;
  int lastRaw_ = HIGH;
  unsigned long lastChangeMs_ = 0;
};

Button buttons[BUTTON_COUNT];

void handleButtons(unsigned long now);
void handleSerial(unsigned long now);
void reportSensors();

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  for (uint8_t i = 0; i < PLATE_COUNT; i++) plates[i].begin(i);
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) buttons[i].begin(BUTTON_PIN[i]);

  Serial.println();
  Serial.println(F("UnoPillCount ready"));
  Serial.println(F("  K1 / K2 = dispense from that plate"));
  Serial.println(F("  K3      = both plates together"));
  Serial.println(F("  serial 1/2/b dispense, i = sensor check, s = stop"));
  reportSensors();
}

void loop() {
  const unsigned long now = millis();

  handleButtons(now);
  handleSerial(now);

  bool anyBusy = false;
  for (uint8_t i = 0; i < PLATE_COUNT; i++) {
    plates[i].update(now);
    if (plates[i].busy()) anyBusy = true;
  }

  digitalWrite(STATUS_LED_PIN, anyBusy ? HIGH : LOW);
}

/** ตรวจว่าเซ็นเซอร์ต่อถูกและมองเห็นอะไรอยู่ ใช้ก่อนเริ่มทดสอบจริง */
void reportSensors() {
  Serial.print(F("IR now: "));
  for (uint8_t i = 0; i < PLATE_COUNT; i++) {
    Serial.print(F("plate "));
    Serial.print(i + 1);
    Serial.print('=');
    Serial.print(plates[i].beamBlocked() ? F("BLOCKED") : F("clear"));
    Serial.print(' ');
  }
  Serial.println();
  Serial.println(F("  (เอามือบังเซ็นเซอร์แล้วกด i ซ้ำ ค่าต้องเปลี่ยนเป็น BLOCKED)"));
}

void handleButtons(unsigned long now) {
  // อ่านทุกปุ่มทุกรอบเสมอ ไม่ใช้ else-if
  // ไม่อย่างนั้นปุ่มที่อยู่ท้ายจะถูกข้าม debounce ในรอบที่ปุ่มอื่นถูกกด
  bool hit[BUTTON_COUNT];
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) hit[i] = buttons[i].pressed(now);

  if (hit[0]) plates[0].start(PILLS_PER_RUN, now);
  if (hit[1]) plates[1].start(PILLS_PER_RUN, now);
  if (hit[2]) {
    plates[0].start(PILLS_PER_RUN, now);
    plates[1].start(PILLS_PER_RUN, now, START_STAGGER_MS);
  }
}

void handleSerial(unsigned long now) {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    switch (c) {
      case '1': plates[0].start(PILLS_PER_RUN, now); break;
      case '2': plates[1].start(PILLS_PER_RUN, now); break;
      case 'b':
      case 'B':
        plates[0].start(PILLS_PER_RUN, now);
        plates[1].start(PILLS_PER_RUN, now, START_STAGGER_MS);
        break;
      case 'i':
      case 'I': reportSensors(); break;
      case 's':
      case 'S':
        for (uint8_t i = 0; i < PLATE_COUNT; i++) plates[i].abort(now);
        Serial.println(F("stopped"));
        break;
      default: break;  // ข้าม newline และตัวอักษรอื่น
    }
  }
}
