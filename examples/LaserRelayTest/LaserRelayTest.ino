// ทดสอบชุดเลเซอร์ก่อนใช้กับเฟิร์มแวร์จริง: PCF8574 -> relay -> เลเซอร์ 3 ตัว, ตัวรับ 3 ตัว, ไฟจราจร
//
// ใช้ได้ทันทีโดยไม่ต้องตั้งค่า secrets/Wi-Fi และ **ไม่ขยับ servo หรือมอเตอร์สั่นเลย**
// อัปโหลดด้วยบอร์ด "ESP32 Dev Module" แล้วเปิด Serial Monitor ที่ 115200 (Newline หรือ Both NL & CR)
//
// พิมพ์คำสั่งทีละตัวอักษรแล้วกด Enter:
//   s  สแกนบัส I2C ว่าเจอ PCF8574 (0x20), จอ LCD (0x25, 0x27), RTC (0x68) ไหม
//   1  เปิดเลเซอร์ (relay ติด)       0  ปิดเลเซอร์ (relay ดับ)
//   t  เปิด-ปิด relay ทุก 1 วินาที 5 รอบ (ฟังเสียงคลิก ดูเลเซอร์ติด-ดับ)
//   p  หาขั้วสัญญาณตัวรับเอง -> บอกว่าต้องตั้ง PILL_SENSOR_ACTIVE_LOW เป็นอะไร
//   r  อ่านระดับตัวรับทั้งสามตอนนี้
//   w  โหมดดูเม็ดยา: เปิดเลเซอร์แล้วรายงานทุกครั้งที่ลำแสงถูกบัง พร้อมเวลาที่บัง (พิมพ์ w อีกครั้งเพื่อหยุด)
//   l  ไล่ไฟจราจร แดง เหลือง เขียว
//   v  เปิด/ปิดมอเตอร์สั่นทุกจาน (ความแรงเท่าตอนจ่ายจริง) ใช้คู่กับ w ดูว่าแรงสั่นทำให้พลาดเม็ดไหม
//
// ค่าข้างล่างตรงกับ ESP32_Main/config.h ถ้าแก้ที่นั่นต้องแก้ตรงนี้ด้วย

#include <Arduino.h>
#include <Wire.h>

// ---- ขาและ address ----
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t LASER_PCF8574_ADDRESS = 0x20;  // A0 A1 A2 ต่อ GND
constexpr uint8_t LASER_PCF_BIT = 0;             // P0 -> IN1 ของ relay
constexpr bool LASER_SWITCH_ACTIVE_LOW = true;   // relay แบบ opto ติดเมื่อ IN เป็น LOW
constexpr unsigned long LASER_SETTLE_MS = 60;

constexpr uint8_t SENSOR_COUNT = 3;
constexpr uint8_t SENSOR_PINS[SENSOR_COUNT] = {34, 35, 36};

constexpr uint8_t LED_PINS[3] = {25, 2, 5};  // แดง เหลือง เขียว (common cathode: HIGH = ติด)
constexpr uint8_t BUZZER_PIN = 15;
constexpr bool BUZZER_ACTIVE_HIGH = true;

// ขามอเตอร์สั่น DRV8833 ต้องเป็น LOW ทันที ไม่งั้น GPIO14 ที่ปล่อยสัญญาณตอนบูตจะทำให้มอเตอร์จาน 3 หมุนค้าง
constexpr uint8_t MOTOR_PINS[] = {13, 26, 4, 16, 17, 14};
// มอเตอร์สั่น: ขา PWM ของแต่ละจาน ช่อง PWM และความแรง ตรงกับ config.h (VIB_PWM_PINS, VIB_LEDC_CHANNELS, VIB_SPEED)
constexpr uint8_t VIB_PWM_PINS[3] = {13, 26, 4};
constexpr uint8_t VIB_LEDC_CHANNELS[3] = {4, 5, 6};
constexpr uint8_t VIB_SPEED = 70;
bool vibrating = false;

// ตัวกรองเดียวกับเฟิร์มแวร์จริง: บังรวมต่ำกว่านี้ = สัญญาณรบกวน
constexpr uint32_t PILL_MIN_BLOCK_US = 150;  // ตรงกับ config.h

// ---- สถานะ ----
bool laserOn = false;
int blockedLevel = -1;  // ระดับที่แปลว่า "ถูกบัง" หาได้จากคำสั่ง p (-1 = ยังไม่รู้)
bool watching = false;

// interrupt จดขอบพร้อมเวลา เหมือนเฟิร์มแวร์จริง
struct Edge { uint32_t atUs; uint8_t level; };
constexpr uint8_t QUEUE_SIZE = 64;
volatile Edge edges[SENSOR_COUNT][QUEUE_SIZE];
volatile uint8_t head[SENSOR_COUNT] = {};
volatile uint8_t tail[SENSOR_COUNT] = {};
volatile bool overflow[SENSOR_COUNT] = {};

void IRAM_ATTR onEdge(void *arg)
{
  const uint8_t i = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
  const uint8_t next = static_cast<uint8_t>((head[i] + 1) % QUEUE_SIZE);
  if (next == tail[i]) { overflow[i] = true; return; }
  edges[i][head[i]].atUs = micros();
  edges[i][head[i]].level = static_cast<uint8_t>(digitalRead(SENSOR_PINS[i]));
  head[i] = next;
}

// ---- relay ผ่าน PCF8574 ----
bool setLaser(bool on)
{
  const bool pinHigh = on != LASER_SWITCH_ACTIVE_LOW;
  const uint8_t value = pinHigh ? 0xFF : static_cast<uint8_t>(0xFF & ~(1U << LASER_PCF_BIT));
  Wire.beginTransmission(LASER_PCF8574_ADDRESS);
  Wire.write(value);
  const uint8_t err = Wire.endTransmission();
  if (err != 0)
  {
    Serial.printf("  !! PCF8574 ที่ 0x%02X ไม่ตอบ (error %u) ตรวจ SDA/SCL, VCC และจั๊มเปอร์ A0-A2\n",
                  LASER_PCF8574_ADDRESS, err);
    return false;
  }
  laserOn = on;
  Serial.printf("  เลเซอร์ %s (ส่ง 0x%02X ไป PCF8574)\n", on ? "ติด" : "ดับ", value);
  return true;
}

void scanI2C()
{
  Serial.println("สแกน I2C:");
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address)
  {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) continue;
    ++found;
    const char *what = address == LASER_PCF8574_ADDRESS ? "PCF8574 คุม relay เลเซอร์"
                     : address == 0x25                   ? "จอ LCD เวลา"
                     : address == 0x27                   ? "จอ LCD ยา"
                     : address == 0x68                   ? "RTC DS1307"
                                                         : "ไม่รู้จัก";
    Serial.printf("  0x%02X  %s\n", address, what);
  }
  if (found == 0) Serial.println("  ไม่เจออะไรเลย: ตรวจสาย SDA=21 SCL=22 และ GND ร่วม");
  Wire.beginTransmission(LASER_PCF8574_ADDRESS);
  if (Wire.endTransmission() != 0)
    Serial.println("  !! ไม่เจอ PCF8574 ที่ 0x20 (ถ้าเป็นชิป PCF8574A จะอยู่ที่ 0x38-0x3F ต้องแก้ address)");
}

void readSensors(const char *label)
{
  Serial.printf("  %-14s", label);
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    const int level = digitalRead(SENSOR_PINS[i]);
    Serial.printf("  จาน%u(GPIO%u)=%s", i + 1, SENSOR_PINS[i], level ? "HIGH" : "LOW ");
  }
  Serial.println();
}

/**
 * เลเซอร์ดับ = ตัวรับมืด = เหมือนถูกบัง
 * เทียบระดับตอนติดกับตอนดับ ก็รู้ได้เลยว่าระดับไหนแปลว่า "ถูกบัง" ไม่ต้องใช้มือบัง
 */
void findPolarity()
{
  Serial.println("หาขั้วสัญญาณ (อย่าบังลำแสงระหว่างนี้):");
  if (!setLaser(false)) return;
  delay(200);
  int dark[SENSOR_COUNT];
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) dark[i] = digitalRead(SENSOR_PINS[i]);
  readSensors("เลเซอร์ดับ:");

  setLaser(true);
  delay(LASER_SETTLE_MS + 200);
  int lit[SENSOR_COUNT];
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) lit[i] = digitalRead(SENSOR_PINS[i]);
  readSensors("เลเซอร์ติด:");
  setLaser(false);

  int agreed = -1;
  bool consistent = true;
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    if (dark[i] == lit[i])
    {
      Serial.printf("  !! จาน %u ค่าไม่เปลี่ยนเลย: เลเซอร์ไม่ส่องโดนตัวรับ ตัวรับไม่ได้ต่อไฟ หรือสาย OUT/shifter หลุด\n", i + 1);
      consistent = false;
      continue;
    }
    if (agreed < 0) agreed = dark[i];
    else if (agreed != dark[i]) consistent = false;
  }

  if (agreed < 0)
  {
    Serial.println("  ยังสรุปไม่ได้ แก้ตามข้อความด้านบนแล้วลองใหม่");
    return;
  }
  blockedLevel = agreed;
  Serial.printf("  ลำแสงถูกบัง = %s  ->  ตั้งใน config.h: PILL_SENSOR_ACTIVE_LOW = %s\n",
                agreed == LOW ? "LOW" : "HIGH", agreed == LOW ? "true" : "false");
  if (!consistent)
    Serial.println("  !! ตัวรับแต่ละตัวให้ขั้วไม่ตรงกัน หรือบางตัวไม่ทำงาน ต้องแก้ก่อนใช้จริง");
}

void relayToggleTest()
{
  Serial.println("เปิด-ปิด relay 5 รอบ (ต้องได้ยินคลิก และเลเซอร์ติด-ดับตาม):");
  for (int n = 0; n < 5; ++n)
  {
    if (!setLaser(true)) return;
    delay(1000);
    setLaser(false);
    delay(1000);
  }
}

void ledTest()
{
  const char *names[3] = {"แดง", "เหลือง", "เขียว"};
  for (uint8_t i = 0; i < 3; ++i)
  {
    Serial.printf("  ไฟ%s (GPIO%u)\n", names[i], LED_PINS[i]);
    digitalWrite(LED_PINS[i], HIGH);
    delay(700);
    digitalWrite(LED_PINS[i], LOW);
  }
}

void startWatch()
{
  if (blockedLevel < 0)
  {
    Serial.println("ยังไม่รู้ขั้วสัญญาณ หาให้ก่อน:");
    findPolarity();
    if (blockedLevel < 0) return;
  }
  if (!setLaser(true)) return;
  delay(LASER_SETTLE_MS);
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) { tail[i] = head[i]; overflow[i] = false; }
  watching = true;
  Serial.printf("โหมดดูเม็ดยา: หยอดเม็ดยาหรือเอาอะไรผ่านลำแสง (นับเมื่อบังรวม >= %lu us) พิมพ์ w เพื่อหยุด\n",
                static_cast<unsigned long>(PILL_MIN_BLOCK_US));
}

void stopWatch()
{
  watching = false;
  setLaser(false);
  Serial.println("หยุดโหมดดูเม็ดยา");
}

// รายงานทุกช่วงที่ลำแสงถูกบัง ใช้ตั้งค่า PILL_MIN_BLOCK_US จากเม็ดยาจริง
uint32_t blockStart[SENSOR_COUNT] = {};
bool isBlocked[SENSOR_COUNT] = {};
uint32_t count[SENSOR_COUNT] = {};

void serviceWatch()
{
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    if (overflow[i])
    {
      overflow[i] = false;
      Serial.printf("  !! จาน %u สัญญาณสั่นถี่จนคิวเต็ม (สายหลวม? แสงรอบข้าง? เล็งไม่ตรง?)\n", i + 1);
    }
    while (tail[i] != head[i])
    {
      const uint32_t at = edges[i][tail[i]].atUs;
      const bool blocked = edges[i][tail[i]].level == blockedLevel;
      tail[i] = static_cast<uint8_t>((tail[i] + 1) % QUEUE_SIZE);
      if (blocked && !isBlocked[i])
      {
        isBlocked[i] = true;
        blockStart[i] = at;
      }
      else if (!blocked && isBlocked[i])
      {
        isBlocked[i] = false;
        const uint32_t us = at - blockStart[i];
        const bool pill = us >= PILL_MIN_BLOCK_US;
        if (pill) ++count[i];
        Serial.printf("  จาน %u บังลำแสง %6lu us  %s  (นับได้ %lu)\n", i + 1, static_cast<unsigned long>(us),
                      pill ? "= เม็ดยา" : "= สัญญาณรบกวน ไม่นับ", static_cast<unsigned long>(count[i]));
      }
    }
  }
}

/** เปิด/ปิดมอเตอร์สั่นทุกจาน ใช้ดูว่าแรงสั่นหรือไฟตกจากมอเตอร์ทำให้เซ็นเซอร์พลาดหรือนับมั่วไหม */
void toggleVibration()
{
  vibrating = !vibrating;
  for (uint8_t i = 0; i < 3; ++i)
  {
    static bool attached[3] = {};
    if (!attached[i])
      attached[i] = ledcAttachChannel(VIB_PWM_PINS[i], 1000, 8, VIB_LEDC_CHANNELS[i]);
    ledcWrite(VIB_PWM_PINS[i], vibrating ? VIB_SPEED : 0);
  }
  Serial.println(vibrating ? "มอเตอร์สั่นทำงาน (ทุกจาน) พิมพ์ v อีกครั้งเพื่อหยุด" : "มอเตอร์สั่นหยุดแล้ว");
}

void printHelp()
{
  Serial.println();
  Serial.println("คำสั่ง: s=สแกน I2C  1=เลเซอร์ติด  0=เลเซอร์ดับ  t=ทดสอบ relay  p=หาขั้วสัญญาณ");
  Serial.println("        r=อ่านตัวรับ  w=ดูเม็ดยา(เปิด/ปิด)  l=ทดสอบไฟจราจร  v=มอเตอร์สั่น(เปิด/ปิด)");
}

void setup()
{
  // ขาอันตรายก่อนอย่างอื่น: มอเตอร์ต้องหยุด buzzer ต้องเงียบ
  for (uint8_t pin : MOTOR_PINS) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  for (uint8_t pin : LED_PINS) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== ทดสอบ relay เลเซอร์ + ตัวรับ + ไฟจราจร ===");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    // GPIO34-39 ไม่มี pull-up ในตัว ตัวรับ (ผ่าน shifter) ต้องขับสัญญาณเอง
    pinMode(SENSOR_PINS[i], INPUT);
    attachInterruptArg(digitalPinToInterrupt(SENSOR_PINS[i]), onEdge,
                       reinterpret_cast<void *>(static_cast<uintptr_t>(i)), CHANGE);
  }

  scanI2C();
  setLaser(false);  // เผื่อชิปค้างสถานะ "ติด" จากก่อนรีเซ็ต
  printHelp();
}

void loop()
{
  if (watching)
    serviceWatch();

  if (!Serial.available())
    return;
  const char command = static_cast<char>(Serial.read());
  if (command == '\n' || command == '\r' || command == ' ')
    return;

  // ออกจากโหมดดูก่อนทำคำสั่งอื่น
  if (watching && command != 'w' && command != 'v')
    stopWatch();

  switch (command)
  {
    case 's': scanI2C(); break;
    case '1': setLaser(true); break;
    case '0': setLaser(false); break;
    case 't': relayToggleTest(); break;
    case 'p': findPolarity(); break;
    case 'r': readSensors("ตอนนี้:"); break;
    case 'w': watching ? stopWatch() : startWatch(); break;
    case 'l': ledTest(); break;
    case 'v': toggleVibration(); break;
    default: printHelp(); break;
  }
}
