// สอบเทียบตำแหน่งพัก และช่องปล่อยยา 4 ช่องของจานทั้งสาม
//
// ใช้ครั้งเดียวจบ แล้วเอาค่าที่ได้ไปใส่ ESP32_Main/hardware.local.h
//
// ต่อสายเหมือนเครื่องจริงทุกอย่าง (servo จาน 1/2/3 ที่ GPIO 18/19/23)
// ต้องจ่ายไฟ servo จาก adapter ไม่ใช่จากขา 5V ของ ESP32
//
// มุมบนจอเป็นมุมจริงของ servo 0-270 องศา กึ่งกลาง = 135 องศา = ตำแหน่งพัก
// ช่องปล่อยยาเรียงจากเล็กไปใหญ่ ตรงกับตัวเลือกขนาดยาบนเว็บ:
//   ช่อง 1 = กลม ไม่เกิน 8 mm         หมุนขวา  90 องศา  -> ราว 225 องศา
//   ช่อง 2 = กลม ไม่เกิน 13 mm        หมุนขวา 135 องศา  -> ราว 265 องศา (เว้น 5 กันชนปลาย)
//   ช่อง 3 = กลม ไม่เกิน 15 mm        หมุนซ้าย  90 องศา  -> ราว  45 องศา
//   ช่อง 4 = รี/แคปซูล ไม่เกิน 25 mm  หมุนซ้าย 135 องศา  -> ราว   5 องศา (เว้น 5 กันชนปลาย)
//
// เปิด Serial Monitor ที่ 115200 ตั้ง Line ending เป็น No line ending
// แล้วพิมพ์ตัวอักษรทีละตัว:
//
//   1 2 3     เลือกจาน
//   [ ]       ขยับ -10us / +10us
//   { }       ขยับ -1us / +1us  (ละเอียด ใช้ตอนใกล้ตำแหน่งแล้ว)
//   r         บันทึกตำแหน่งนี้เป็น "ตำแหน่งพัก"
//   5 6 7 8   บันทึกตำแหน่งนี้เป็นช่อง 1 / 2 / 3 / 4
//   h         ไปตำแหน่งพักที่บันทึกไว้
//   m         ไปกึ่งกลางพอดี 135 องศา (1500us) จุดเริ่มต้นที่ควรเป็นตำแหน่งพัก
//   a s d f   ไปช่อง 1 / 2 / 3 / 4 ที่บันทึกไว้ (เพื่อเช็คหรือปรับต่อ)
//   t         ทดสอบวิ่ง พัก -> ช่อง 1 -> พัก -> ช่อง 2 -> ... -> ช่อง 4 -> พัก
//   p         พิมพ์ค่าทั้งหมดในรูปแบบที่ก๊อปไปวางได้เลย
//   c         ให้ servo คลายแรง (detach) จะได้หมุนจานด้วยมือได้
//
// วิธีทำทีละจาน:
//   1. กด c แล้วหมุนจานด้วยมือไปตำแหน่งพัก กด [ ] จนจานกลับมาตรงนั้น แล้วกด r
//   2. กด ] ไล่ไปจนรูช่อง 1 ตรงปากรางพอดี กด 5
//   3. ทำต่อจนครบช่อง 2 (6), ช่อง 3 (7), ช่อง 4 (8)
//   4. กด t ดูว่าทุกช่องตรงราง ถ้าไม่ตรง กด a s d f ไปช่องนั้น ปรับ แล้วบันทึกซ้ำ
//   5. ทำครบสามจานแล้วกด p

#include <ESP32Servo.h>

constexpr uint8_t COUNT = 3;
constexpr uint8_t HOLES = 4;
constexpr uint8_t SERVO_PINS[COUNT] = {18, 19, 23};

// ต้องตรงกับ SERVO_MIN_PULSE_US / SERVO_MAX_PULSE_US ใน config.h
// SG92R 270 องศาใช้ 500-2500us เต็มพิสัย
constexpr int MIN_US = 500;
constexpr int MAX_US = 2500;
constexpr int REST_DEFAULT = 1500;

// ค่าเริ่มต้นประมาณจากสูตร (7.4us ต่อองศา รอบ 1500) ช่อง 135 องศาหยุดที่ 130
// เพื่อไม่ชนตัวกั้นปลายของ servo ต้องปรับให้ตรงรูจริงทุกช่อง
constexpr int HOLE_DEFAULT[HOLES] = {2167, 2463, 833, 537};
const char *const HOLE_NAME[HOLES] = {"กลม<=8mm", "กลม<=13mm", "กลม<=15mm", "แคปซูล<=25mm"};

Servo servos[COUNT];
int current[COUNT] = {REST_DEFAULT, REST_DEFAULT, REST_DEFAULT};
int restUs[COUNT] = {REST_DEFAULT, REST_DEFAULT, REST_DEFAULT};
int holeUs[COUNT][HOLES];
bool restSet[COUNT] = {false, false, false};
bool holeSet[COUNT][HOLES] = {};
uint8_t selected = 0;

void attachIfNeeded(uint8_t i)
{
  if (!servos[i].attached())
    servos[i].attach(SERVO_PINS[i], MIN_US, MAX_US);
}

void moveTo(uint8_t i, int us)
{
  if (us < MIN_US) us = MIN_US;
  if (us > MAX_US) us = MAX_US;
  current[i] = us;
  attachIfNeeded(i);
  servos[i].writeMicroseconds(us);
}

/** มุมจริงของ servo 0-270 องศา (500us = 0, 1500us = 135 กึ่งกลาง, 2500us = 270) */
float servoDegrees(int us)
{
  return (us - MIN_US) * 270.0f / (MAX_US - MIN_US);
}

void report()
{
  const uint8_t i = selected;
  Serial.printf("จาน %u  ตอนนี้ %d us = %.0f องศา   พัก=%d us = %.0f องศา%s\n",
                static_cast<unsigned>(i + 1), current[i], servoDegrees(current[i]),
                restUs[i], servoDegrees(restUs[i]), restSet[i] ? "" : " (ยังไม่ตั้ง)");
  for (uint8_t h = 0; h < HOLES; ++h)
    Serial.printf("   ช่อง %u %-14s %d us = %.0f องศา%s\n",
                  static_cast<unsigned>(h + 1), HOLE_NAME[h], holeUs[i][h],
                  servoDegrees(holeUs[i][h]), holeSet[i][h] ? "" : "  (ยังไม่ตั้ง)");
}

void printResult()
{
  Serial.println();
  Serial.println("---- ก๊อปบรรทัดเหล่านี้ไปวางทับใน ESP32_Main/hardware.local.h ----");
  Serial.printf("#define PILLBOX_REST_PULSES {%d, %d, %d}\n", restUs[0], restUs[1], restUs[2]);
  Serial.print("#define PILLBOX_HOLE_PULSES {");
  for (uint8_t i = 0; i < COUNT; ++i)
    Serial.printf("{%d, %d, %d, %d}%s", holeUs[i][0], holeUs[i][1], holeUs[i][2], holeUs[i][3],
                  i + 1 < COUNT ? ", " : "");
  Serial.println("}");
  Serial.println("#define PILLBOX_CALIBRATED true");
  Serial.println("------------------------------------------------------------------");

  // เงื่อนไขเดียวกับ static_assert ใน config.h บอกตรงนี้เลยจะได้ไม่ไปงงตอนกด Upload
  for (uint8_t i = 0; i < COUNT; ++i)
  {
    if (!restSet[i])
      Serial.printf("เตือน: จาน %u ยังไม่ได้ตั้งตำแหน่งพัก ค่าที่พิมพ์เป็นค่าตั้งต้น\n",
                    static_cast<unsigned>(i + 1));
    for (uint8_t h = 0; h < HOLES; ++h)
    {
      if (!holeSet[i][h])
        Serial.printf("เตือน: จาน %u ช่อง %u ยังไม่ได้ตั้ง ค่าที่พิมพ์เป็นค่าประมาณ ไม่ใช่ค่าที่วัด\n",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(h + 1));
      if (holeUs[i][h] == restUs[i])
        Serial.printf("ผิด: จาน %u ช่อง %u อยู่ตำแหน่งเดียวกับที่พัก จานจะไม่ขยับ คอมไพล์จะไม่ผ่าน\n",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(h + 1));
    }
  }
  Serial.println();
}

void runTest(uint8_t i)
{
  Serial.printf("จาน %u: ทดสอบทุกช่อง\n", static_cast<unsigned>(i + 1));
  moveTo(i, restUs[i]);
  delay(700);
  for (uint8_t h = 0; h < HOLES; ++h)
  {
    Serial.printf("  ช่อง %u %s\n", static_cast<unsigned>(h + 1), HOLE_NAME[h]);
    moveTo(i, holeUs[i][h]);
    delay(1200);
    moveTo(i, restUs[i]);
    delay(1200);
  }
  Serial.println("เสร็จ");
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  // ESP32Servo กับ analogWrite แย่ง LEDC timer กัน ต้องจองก่อนเสมอ
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  for (uint8_t i = 0; i < COUNT; ++i)
  {
    for (uint8_t h = 0; h < HOLES; ++h)
      holeUs[i][h] = HOLE_DEFAULT[h];
    servos[i].setPeriodHertz(50);
    moveTo(i, current[i]);
  }

  Serial.println();
  Serial.println("=== สอบเทียบ servo: ตำแหน่งพัก + ช่องปล่อยยา 4 ช่อง ===");
  Serial.println("1 2 3 = เลือกจาน | [ ] = +-10us | { } = +-1us | c = คลายแรง");
  Serial.println("r = ตั้งพัก | 5 6 7 8 = ตั้งช่อง 1-4 | h = ไปพัก | m = กึ่งกลาง 135 | a s d f = ไปช่อง 1-4");
  Serial.println("t = ทดสอบทุกช่อง | p = พิมพ์ผล");
  report();
}

void loop()
{
  if (!Serial.available())
    return;

  const char key = static_cast<char>(Serial.read());
  const uint8_t i = selected;

  switch (key)
  {
    case '1': case '2': case '3':
      selected = static_cast<uint8_t>(key - '1');
      Serial.printf("เลือกจาน %c\n", key);
      break;

    case '[': moveTo(i, current[i] - 10); break;
    case ']': moveTo(i, current[i] + 10); break;
    case '{': moveTo(i, current[i] - 1); break;
    case '}': moveTo(i, current[i] + 1); break;

    case 'r':
      restUs[i] = current[i];
      restSet[i] = true;
      Serial.printf("ตั้งตำแหน่งพักของจาน %u = %d us\n", static_cast<unsigned>(i + 1), restUs[i]);
      break;

    case '5': case '6': case '7': case '8':
    {
      const uint8_t h = static_cast<uint8_t>(key - '5');
      holeUs[i][h] = current[i];
      holeSet[i][h] = true;
      Serial.printf("ตั้งช่อง %u (%s) ของจาน %u = %d us\n", static_cast<unsigned>(h + 1), HOLE_NAME[h],
                    static_cast<unsigned>(i + 1), holeUs[i][h]);
      break;
    }

    case 'h': moveTo(i, restUs[i]); break;
    case 'm': moveTo(i, REST_DEFAULT); break;  // กึ่งกลางพอดี 135 องศา
    case 'a': moveTo(i, holeUs[i][0]); break;
    case 's': moveTo(i, holeUs[i][1]); break;
    case 'd': moveTo(i, holeUs[i][2]); break;
    case 'f': moveTo(i, holeUs[i][3]); break;

    case 't': runTest(i); return;
    case 'p': printResult(); return;

    case 'c':
      // ปล่อยให้หมุนจานด้วยมือได้ เพื่อหาตำแหน่งที่รูตรงรางพอดี
      servos[i].detach();
      Serial.printf("จาน %u คลายแรงแล้ว หมุนด้วยมือได้ กดปุ่มขยับเพื่อจับแรงอีกครั้ง\n",
                    static_cast<unsigned>(i + 1));
      return;

    default:
      return;  // ข้าม newline และปุ่มที่ไม่ได้ใช้
  }

  report();
}
