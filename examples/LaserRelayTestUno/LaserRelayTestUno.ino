// ทดสอบชุดเลเซอร์ด้วย Arduino Uno: PCF8574 -> relay -> เลเซอร์ 3 ตัว, ตัวรับ 3 ตัว, ไฟจราจร
//
// Uno เป็นไฟ 5V ทั้งบอร์ด ต่อทุกอย่างตรงได้ ไม่ต้องใช้ shifter
// อัปโหลดด้วยบอร์ด "Arduino Uno" แล้วเปิด Serial Monitor ที่ 115200 (Newline)
//
// การต่อสาย:
//   PCF8574  VCC->5V  GND->GND  SDA->A4  SCL->A5  A0 A1 A2->GND (address 0x20)  P0->IN1 ของ relay
//   relay    VCC->5V  GND->GND  IN1->P0 ของ PCF8574 (จั๊มเปอร์ JD-VCC เสียบไว้)  COM->5V  NO->ขา S ของเลเซอร์ทั้ง 3
//   เลเซอร์  S->NO ของ relay  ขากลางไม่ต่อ  (-)->GND
//   ตัวรับ   VCC->5V  GND->GND  OUT จาน1->D8  จาน2->D9  จาน3->D10
//   ไฟจราจร  R->D5  Y->D6  G->D7  GND->GND
//
// คำสั่ง (พิมพ์ทีละตัวแล้ว Enter):
//   s สแกน I2C   1 เลเซอร์ติด   0 เลเซอร์ดับ   t เปิด-ปิด relay 5 รอบ
//   p หาขั้วสัญญาณตัวรับ   r อ่านตัวรับ   w ดูเม็ดยา (พิมพ์ w อีกครั้งเพื่อหยุด)   l ไล่ไฟจราจร

#include <Arduino.h>
#include <Wire.h>

// ใช้รีจิสเตอร์ของชิป ATmega328P โดยตรง (pin-change interrupt) คอมไพล์กับบอร์ดอื่นไม่ได้
#if !defined(__AVR_ATmega328P__)
#error "เลือกบอร์ดผิด: ไปที่ Tools > Board > Arduino AVR Boards > Arduino Uno (ถ้าใช้ ESP32 ให้เปิด examples/LaserRelayTest แทน)"
#endif

const uint8_t LASER_PCF8574_ADDRESS = 0x20;
const uint8_t LASER_PCF_BIT = 0;
const bool LASER_SWITCH_ACTIVE_LOW = true;  // relay แบบ opto ติดเมื่อ IN เป็น LOW
const unsigned long LASER_SETTLE_MS = 60;

// D8 D9 D10 = PB0 PB1 PB2 อยู่กลุ่ม pin-change interrupt เดียวกัน (PCINT0)
// Uno มี interrupt ภายนอกแค่ D2 D3 ไม่พอสามตัว จึงใช้ pin-change แทน จับขอบได้ทุกขาเหมือนกัน
const uint8_t SENSOR_COUNT = 3;
const uint8_t SENSOR_PINS[SENSOR_COUNT] = {8, 9, 10};
const uint8_t SENSOR_MASK = 0b00000111;  // บิต 0-2 ของ PORTB

const uint8_t LED_PINS[3] = {5, 6, 7};  // แดง เหลือง เขียว (HIGH = ติด)

// ตัวกรองเดียวกับเฟิร์มแวร์จริง: บังรวมต่ำกว่านี้ = สัญญาณรบกวน
const unsigned long PILL_MIN_BLOCK_US = 500;

bool laserOn = false;
int blockedLevel = -1;  // ระดับที่แปลว่า "ถูกบัง" หาได้จากคำสั่ง p
bool watching = false;

// interrupt จดขอบพร้อมเวลา (RAM ของ Uno มีน้อย คิวจึงสั้นกว่าบน ESP32)
struct Edge { unsigned long atUs; uint8_t level; };
const uint8_t QUEUE_SIZE = 16;
volatile Edge edges[SENSOR_COUNT][QUEUE_SIZE];
volatile uint8_t head[SENSOR_COUNT];
volatile uint8_t tail[SENSOR_COUNT];
volatile bool overflow[SENSOR_COUNT];
volatile uint8_t lastPortB = 0;

ISR(PCINT0_vect)
{
  const unsigned long now = micros();
  const uint8_t port = PINB & SENSOR_MASK;
  const uint8_t changed = port ^ lastPortB;
  lastPortB = port;
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    if (!(changed & (1 << i))) continue;
    const uint8_t next = (head[i] + 1) % QUEUE_SIZE;
    if (next == tail[i]) { overflow[i] = true; continue; }
    edges[i][head[i]].atUs = now;
    edges[i][head[i]].level = (port >> i) & 1;
    head[i] = next;
  }
}

bool setLaser(bool on)
{
  const bool pinHigh = on != LASER_SWITCH_ACTIVE_LOW;
  const uint8_t value = pinHigh ? 0xFF : (uint8_t)(0xFF & ~(1 << LASER_PCF_BIT));
  Wire.beginTransmission(LASER_PCF8574_ADDRESS);
  Wire.write(value);
  const uint8_t err = Wire.endTransmission();
  if (err != 0)
  {
    Serial.print(F("  !! PCF8574 ที่ 0x20 ไม่ตอบ (error "));
    Serial.print(err);
    Serial.println(F(") ตรวจ SDA=A4 SCL=A5, VCC และจั๊มเปอร์ A0-A2"));
    return false;
  }
  laserOn = on;
  Serial.println(on ? F("  เลเซอร์ ติด") : F("  เลเซอร์ ดับ"));
  return true;
}

void printHex(uint8_t value)
{
  Serial.print(F("0x"));
  if (value < 16) Serial.print('0');
  Serial.print(value, HEX);
}

void scanI2C()
{
  Serial.println(F("สแกน I2C:"));
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address)
  {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) continue;
    ++found;
    Serial.print(F("  "));
    printHex(address);
    if (address == LASER_PCF8574_ADDRESS) Serial.println(F("  PCF8574 คุม relay เลเซอร์"));
    else Serial.println(F("  อุปกรณ์อื่น"));
  }
  if (found == 0) Serial.println(F("  ไม่เจออะไรเลย: ตรวจสาย SDA=A4 SCL=A5 และ GND ร่วม"));
  Wire.beginTransmission(LASER_PCF8574_ADDRESS);
  if (Wire.endTransmission() != 0)
    Serial.println(F("  !! ไม่เจอ PCF8574 ที่ 0x20 (ถ้าเป็นชิป PCF8574A จะอยู่ที่ 0x38-0x3F)"));
}

void readSensors(const __FlashStringHelper *label)
{
  Serial.print(F("  "));
  Serial.print(label);
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    Serial.print(F("  จาน"));
    Serial.print(i + 1);
    Serial.print(F("(D"));
    Serial.print(SENSOR_PINS[i]);
    Serial.print(F(")="));
    Serial.print(digitalRead(SENSOR_PINS[i]) ? F("HIGH") : F("LOW "));
  }
  Serial.println();
}

// เลเซอร์ดับ = ตัวรับมืด = เหมือนถูกบัง เทียบตอนติดกับตอนดับก็รู้ว่าระดับไหนแปลว่า "ถูกบัง"
void findPolarity()
{
  Serial.println(F("หาขั้วสัญญาณ (อย่าบังลำแสงระหว่างนี้):"));
  if (!setLaser(false)) return;
  delay(200);
  int dark[SENSOR_COUNT];
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) dark[i] = digitalRead(SENSOR_PINS[i]);
  readSensors(F("เลเซอร์ดับ:"));

  setLaser(true);
  delay(LASER_SETTLE_MS + 200);
  int lit[SENSOR_COUNT];
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) lit[i] = digitalRead(SENSOR_PINS[i]);
  readSensors(F("เลเซอร์ติด:"));
  setLaser(false);

  int agreed = -1;
  bool consistent = true;
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    if (dark[i] == lit[i])
    {
      Serial.print(F("  !! จาน "));
      Serial.print(i + 1);
      Serial.println(F(" ค่าไม่เปลี่ยน: เลเซอร์ไม่ส่องโดนตัวรับ ตัวรับไม่ได้ต่อไฟ หรือสาย OUT หลุด"));
      consistent = false;
      continue;
    }
    if (agreed < 0) agreed = dark[i];
    else if (agreed != dark[i]) consistent = false;
  }

  if (agreed < 0)
  {
    Serial.println(F("  ยังสรุปไม่ได้ แก้ตามข้อความด้านบนแล้วลองใหม่"));
    return;
  }
  blockedLevel = agreed;
  Serial.print(F("  ลำแสงถูกบัง = "));
  Serial.println(agreed == LOW ? F("LOW") : F("HIGH"));
  Serial.println(F("  (ต่อกับ ESP32 ผ่าน shifter ขั้วยังเหมือนเดิม)"));
  Serial.print(F("  ตั้งใน config.h: PILL_SENSOR_ACTIVE_LOW = "));
  Serial.println(agreed == LOW ? F("true") : F("false"));
  if (!consistent)
    Serial.println(F("  !! ตัวรับแต่ละตัวให้ขั้วไม่ตรงกัน หรือบางตัวไม่ทำงาน ต้องแก้ก่อนใช้จริง"));
}

void relayToggleTest()
{
  Serial.println(F("เปิด-ปิด relay 5 รอบ (ต้องได้ยินคลิก และเลเซอร์ติด-ดับตาม):"));
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
  for (uint8_t i = 0; i < 3; ++i)
  {
    Serial.println(i == 0 ? F("  ไฟแดง (D5)") : i == 1 ? F("  ไฟเหลือง (D6)") : F("  ไฟเขียว (D7)"));
    digitalWrite(LED_PINS[i], HIGH);
    delay(700);
    digitalWrite(LED_PINS[i], LOW);
  }
}

unsigned long blockStart[SENSOR_COUNT];
bool isBlocked[SENSOR_COUNT];
unsigned long pillCount[SENSOR_COUNT];

void clearQueues()
{
  noInterrupts();
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    tail[i] = head[i];
    overflow[i] = false;
    isBlocked[i] = false;
  }
  interrupts();
}

void startWatch()
{
  if (blockedLevel < 0)
  {
    Serial.println(F("ยังไม่รู้ขั้วสัญญาณ หาให้ก่อน:"));
    findPolarity();
    if (blockedLevel < 0) return;
  }
  if (!setLaser(true)) return;
  delay(LASER_SETTLE_MS);
  clearQueues();
  watching = true;
  Serial.println(F("โหมดดูเม็ดยา: หยอดเม็ดยาผ่านลำแสง (นับเมื่อบัง >= 500 us) พิมพ์ w เพื่อหยุด"));
}

void stopWatch()
{
  watching = false;
  setLaser(false);
  Serial.println(F("หยุดโหมดดูเม็ดยา"));
}

// รายงานทุกช่วงที่ลำแสงถูกบัง ใช้ตั้งค่า PILL_MIN_BLOCK_US จากเม็ดยาจริง
void serviceWatch()
{
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
  {
    if (overflow[i])
    {
      overflow[i] = false;
      Serial.print(F("  !! จาน "));
      Serial.print(i + 1);
      Serial.println(F(" สัญญาณสั่นถี่จนคิวเต็ม (สายหลวม? แสงรอบข้าง? เล็งไม่ตรง?)"));
    }
    while (true)
    {
      noInterrupts();
      if (tail[i] == head[i]) { interrupts(); break; }
      const unsigned long at = edges[i][tail[i]].atUs;
      const uint8_t level = edges[i][tail[i]].level;
      tail[i] = (tail[i] + 1) % QUEUE_SIZE;
      interrupts();

      const bool blocked = level == blockedLevel;
      if (blocked && !isBlocked[i])
      {
        isBlocked[i] = true;
        blockStart[i] = at;
      }
      else if (!blocked && isBlocked[i])
      {
        isBlocked[i] = false;
        const unsigned long us = at - blockStart[i];
        const bool pill = us >= PILL_MIN_BLOCK_US;
        if (pill) ++pillCount[i];
        Serial.print(F("  จาน "));
        Serial.print(i + 1);
        Serial.print(F(" บังลำแสง "));
        Serial.print(us);
        Serial.print(pill ? F(" us = เม็ดยา") : F(" us = สัญญาณรบกวน ไม่นับ"));
        Serial.print(F("  (นับได้ "));
        Serial.print(pillCount[i]);
        Serial.println(F(")"));
      }
    }
  }
}

void printHelp()
{
  Serial.println();
  Serial.println(F("คำสั่ง: s=สแกน I2C  1=เลเซอร์ติด  0=เลเซอร์ดับ  t=ทดสอบ relay  p=หาขั้วสัญญาณ"));
  Serial.println(F("        r=อ่านตัวรับ  w=ดูเม็ดยา(เปิด/ปิด)  l=ทดสอบไฟจราจร"));
}

void setup()
{
  for (uint8_t i = 0; i < 3; ++i) { pinMode(LED_PINS[i], OUTPUT); digitalWrite(LED_PINS[i], LOW); }

  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== ทดสอบ relay เลเซอร์ + ตัวรับ + ไฟจราจร (Arduino Uno) ==="));

  Wire.begin();
  Wire.setClock(100000);

  // ตัวรับขับสัญญาณเองอยู่แล้ว ไม่ต้องเปิด pull-up ในตัว
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) pinMode(SENSOR_PINS[i], INPUT);
  lastPortB = PINB & SENSOR_MASK;
  PCMSK0 |= SENSOR_MASK;   // เลือกขา D8 D9 D10
  PCIFR |= (1 << PCIF0);   // ล้างธงค้าง
  PCICR |= (1 << PCIE0);   // เปิด pin-change interrupt กลุ่ม PORTB

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
  const char command = Serial.read();
  if (command == '\n' || command == '\r' || command == ' ')
    return;

  if (watching && command != 'w')
    stopWatch();

  switch (command)
  {
    case 's': scanI2C(); break;
    case '1': setLaser(true); break;
    case '0': setLaser(false); break;
    case 't': relayToggleTest(); break;
    case 'p': findPolarity(); break;
    case 'r': readSensors(F("ตอนนี้:")); break;
    case 'w': if (watching) stopWatch(); else startWatch(); break;
    case 'l': ledTest(); break;
    default: printHelp(); break;
  }
}
