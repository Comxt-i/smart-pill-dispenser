// สอบเทียบตำแหน่งพัก และช่องปล่อยยา 4 ช่องของจานทั้งสาม ให้แม่นที่สุด
//
// ใช้ครั้งเดียวจบ แล้วเอาค่าที่ได้ไปใส่ ESP32_Main/hardware.local.h
//
// ต่อสายเหมือนเครื่องจริงทุกอย่าง (servo จาน 1/2/3 ที่ GPIO 18/19/23)
// ต้องจ่ายไฟ servo จาก adapter ไม่ใช่จากขา 5V ของ ESP32 และห้ามต่อ adapter เข้า VIN ตอนเสียบ USB
//
// มุมบนจอเป็นมุมจริงของ servo 0-270 องศา กึ่งกลาง = 135 องศา = ตำแหน่งพัก
// ช่องปล่อยยาเรียงจากเล็กไปใหญ่ ตรงกับตัวเลือกขนาดยาบนเว็บ:
//   ช่อง 1 = กลม ไม่เกิน 8 mm         หมุนขวา  86 องศา  -> ราว  49 องศา (ไม่ถึง 90 เพราะกลไกเลยช่อง)
//   ช่อง 2 = กลม ไม่เกิน 13 mm        หมุนขวา 135 องศา  -> ราว   5 องศา (เว้น 5 กันชนปลาย)
//   ช่อง 3 = กลม ไม่เกิน 15 mm        หมุนซ้าย  90 องศา  -> ราว 225 องศา
//   ช่อง 4 = รี/แคปซูล ไม่เกิน 25 mm  หมุนซ้าย 135 องศา  -> ราว 265 องศา (เว้น 5 กันชนปลาย)
//   (ซ้าย/ขวา มองจากหน้าเครื่อง ช่องเล็กอยู่ทางขวาเสมอ servo ตัวนี้หมุนไปทางขวาเมื่อพัลส์น้อยลง
//    ปุ่ม [ { = หมุนไปทางขวา (ด้านช่องเล็ก)  ปุ่ม ] } = หมุนไปทางซ้าย (ด้านช่องใหญ่))
//
// ทำไมแม่นกว่าการกะด้วยตา:
//   - หากึ่งกลางจาก "ขอบสองข้าง": กะว่ารูอยู่กลางรางด้วยตาคลาดง่าย แต่จุดที่รูเริ่มโผล่
//     กับจุดที่รูเริ่มหายเห็นชัด ค่ากลางของสองจุดนี้คือกึ่งกลางจริง
//   - เข้าหาตำแหน่งจากทิศเดียวกับตอนใช้งาน: เฟือง servo มีระยะคลอน เข้าหาจุดเดียวกันจากคนละทิศ
//     จะหยุดไม่ตรงกัน เครื่องจริงหมุนจาก "ตำแหน่งพัก -> ช่อง" เสมอ เครื่องมือนี้จึงพาจานกลับไปพัก
//     แล้ววิ่งเข้าช่องให้ดูทุกครั้งหลังบันทึก สิ่งที่เห็นคือสิ่งที่จะเกิดตอนจ่ายยาจริง
//   - ค่าที่บันทึกจำไว้ใน flash ไฟตก/รีเซ็ตกลางคันไม่ต้องเริ่มใหม่
//
// เปิด Serial Monitor ที่ 115200 ตั้ง Line ending เป็น No line ending (หรือ Newline ก็ได้)
// แล้วพิมพ์ตัวอักษรทีละตัวแล้วกด Enter:
//
//   1 2 3     เลือกจาน
//   [ ]       ขยับ -10us / +10us
//   { }       ขยับ -1us / +1us  (ละเอียด ใช้ตอนใกล้ตำแหน่งแล้ว)
//   c         คลายแรง servo (หมุนจานด้วยมือได้)  กดปุ่มขยับเพื่อจับแรงอีกครั้ง
//   m         ไปกึ่งกลางพอดี 135 องศา (1500us)
//   r         บันทึกตำแหน่งนี้เป็น "ตำแหน่งพัก"
//   e         จดขอบที่ 1 ของรู (จุดที่รูเริ่มเปิดเข้าราง)
//   E         จดขอบที่ 2 ของรู (จุดที่รูเริ่มปิด) -> คำนวณกึ่งกลาง แล้ววิ่งพัก->กึ่งกลางให้ดู
//   5 6 7 8   บันทึกตำแหน่งนี้เป็นช่อง 1 / 2 / 3 / 4 (แล้ววิ่งพัก->ช่องให้ตรวจทันที)
//   h         ไปตำแหน่งพักที่บันทึกไว้
//   a s d f   ไปช่อง 1 / 2 / 3 / 4 ที่บันทึกไว้ โดยออกจากตำแหน่งพักเหมือนตอนใช้งานจริง
//   t         ทดสอบวิ่ง พัก -> ช่อง 1 -> พัก -> ช่อง 2 -> ... -> ช่อง 4 -> พัก
//   p         พิมพ์ค่าทั้งหมดในรูปแบบที่ก๊อปไปวางได้เลย พร้อมตรวจความผิดปกติ
//   X         ลบค่าที่จำไว้ทั้งหมด เริ่มใหม่
//
// วิธีทำทีละจาน:
//   1. กด m ให้จานไปกึ่งกลาง ปรับด้วย [ ] { } จนจานอยู่ตำแหน่งพักที่ต้องการ (รูทุกช่องปิด) แล้วกด r
//   2. ช่อง 1: กด [ ไล่ไปทางขวาจนขอบรูเริ่มโผล่ในราง กด e
//              กด ] ต่อจนรูเลยราง ขอบอีกด้านเริ่มปิด กด E -> เครื่องไปกึ่งกลางให้
//              ถ้าตรงดีแล้วกด 5 (ถ้าอยากเลื่อน ใช้ { } ปรับก่อนกด 5)
//   3. ช่อง 2 (6), ช่อง 3 (7), ช่อง 4 (8) ทำแบบเดียวกัน ช่อง 1-2 ไล่ด้วย [ ช่อง 3-4 ไล่ด้วย ]
//   4. กด t ดูว่าทุกช่องตรงราง ถ้าไม่ตรง กด a s d f ไปช่องนั้น ปรับ แล้วบันทึกซ้ำ
//   5. ทำครบสามจานแล้วกด p ก๊อปผลไปวางใน hardware.local.h

#include <ESP32Servo.h>
#include <Preferences.h>

constexpr uint8_t COUNT = 3;
constexpr uint8_t HOLES = 4;
constexpr uint8_t SERVO_PINS[COUNT] = {18, 19, 23};

// ขามอเตอร์สั่น DRV8833 ต้องเป็น LOW ทันที ไม่งั้น GPIO14 ที่ปล่อยสัญญาณตอนบูตจะทำให้มอเตอร์จาน 3 หมุนค้าง
constexpr uint8_t MOTOR_PINS[] = {13, 26, 4, 16, 17, 14};
constexpr uint8_t BUZZER_PIN = 15;
constexpr bool BUZZER_ACTIVE_HIGH = true;

// ต้องตรงกับ SERVO_MIN_PULSE_US / SERVO_MAX_PULSE_US ใน config.h
// SG92R 270 องศาใช้ 500-2500us เต็มพิสัย
constexpr int MIN_US = 500;
constexpr int MAX_US = 2500;
constexpr int REST_DEFAULT = 1500;

// เวลาให้ servo วิ่งถึงที่ ต้องไม่น้อยกว่า PILLBOX_MOVE_TIME_MS ของเครื่องจริง
constexpr unsigned long MOVE_MS = 550;

// ช่องที่ห่างจากตำแหน่งพักน้อยกว่านี้ = ผิดแน่ (รูจะเปิดค้างตอนพัก หรือบันทึกผิดปุ่ม)
constexpr int MIN_HOLE_FROM_REST_US = 150;
// ชิดปลายพิสัยเกินนี้ = servo อาจดันตัวกั้นปลาย ร้อนและกินไฟ
constexpr int END_MARGIN_US = 15;

// ค่าเริ่มต้นประมาณจากสูตร (7.4us ต่อองศา รอบ 1500) ช่อง 135 องศาหยุดที่ 130
// เพื่อไม่ชนตัวกั้นปลายของ servo ต้องปรับให้ตรงรูจริงทุกช่อง
constexpr int HOLE_DEFAULT[HOLES] = {863, 537, 2167, 2463};  // ช่อง 1 = ขวา 86 องศา
const char *const HOLE_NAME[HOLES] = {"กลม<=8mm", "กลม<=13mm", "กลม<=15mm", "แคปซูล<=25mm"};

Servo servos[COUNT];
Preferences store;
int current[COUNT] = {REST_DEFAULT, REST_DEFAULT, REST_DEFAULT};
int restUs[COUNT] = {REST_DEFAULT, REST_DEFAULT, REST_DEFAULT};
int holeUs[COUNT][HOLES];
bool restSet[COUNT] = {false, false, false};
bool holeSet[COUNT][HOLES] = {};
uint8_t selected = 0;

// ขอบรูที่จดไว้ของจานที่เลือก (-1 = ยังไม่จด)
int edgeA = -1;

// ---- จำค่าใน flash ----

void save()
{
  store.putBytes("rest", restUs, sizeof(restUs));
  store.putBytes("holes", holeUs, sizeof(holeUs));
  store.putBytes("restSet", restSet, sizeof(restSet));
  store.putBytes("holeSet", holeSet, sizeof(holeSet));
}

bool load()
{
  if (store.getBytesLength("holes") != sizeof(holeUs) || store.getBytesLength("rest") != sizeof(restUs))
    return false;
  store.getBytes("rest", restUs, sizeof(restUs));
  store.getBytes("holes", holeUs, sizeof(holeUs));
  store.getBytes("restSet", restSet, sizeof(restSet));
  store.getBytes("holeSet", holeSet, sizeof(holeSet));
  return true;
}

void resetAll()
{
  for (uint8_t i = 0; i < COUNT; ++i)
  {
    restUs[i] = REST_DEFAULT;
    restSet[i] = false;
    for (uint8_t h = 0; h < HOLES; ++h)
    {
      holeUs[i][h] = HOLE_DEFAULT[h];
      holeSet[i][h] = false;
    }
  }
}

// ---- ขยับ servo ----

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

/** ไปตำแหน่งนั้นแบบเดียวกับเครื่องจริง: กลับไปพักก่อน แล้ววิ่งจากพักเข้าหาเป้า */
void approachFromRest(uint8_t i, int us)
{
  moveTo(i, restUs[i]);
  delay(MOVE_MS);
  moveTo(i, us);
  delay(MOVE_MS);
}

/** มุมจริงของ servo 0-270 องศา (500us = 0, 1500us = 135 กึ่งกลาง, 2500us = 270) */
float servoDegrees(int us)
{
  return (us - MIN_US) * 270.0f / (MAX_US - MIN_US);
}

void report()
{
  const uint8_t i = selected;
  Serial.printf("จาน %u  ตอนนี้ %d us = %.1f องศา   พัก=%d us = %.1f องศา%s\n",
                static_cast<unsigned>(i + 1), current[i], servoDegrees(current[i]),
                restUs[i], servoDegrees(restUs[i]), restSet[i] ? "" : " (ยังไม่ตั้ง)");
  for (uint8_t h = 0; h < HOLES; ++h)
    Serial.printf("   ช่อง %u %-14s %d us = %.1f องศา%s\n",
                  static_cast<unsigned>(h + 1), HOLE_NAME[h], holeUs[i][h],
                  servoDegrees(holeUs[i][h]), holeSet[i][h] ? "" : "  (ยังไม่ตั้ง)");
  if (edgeA >= 0)
    Serial.printf("   จดขอบที่ 1 ไว้ที่ %d us แล้ว หาขอบที่ 2 แล้วกด E\n", edgeA);
}

/** ตรวจค่าที่ผิดแน่ๆ ก่อนเอาไปใช้ คืนจำนวนปัญหาที่เจอ */
int checkValues()
{
  int problems = 0;
  for (uint8_t i = 0; i < COUNT; ++i)
  {
    if (!restSet[i])
    {
      Serial.printf("เตือน: จาน %u ยังไม่ได้ตั้งตำแหน่งพัก ค่าที่พิมพ์เป็นค่าตั้งต้น\n",
                    static_cast<unsigned>(i + 1));
      ++problems;
    }
    for (uint8_t h = 0; h < HOLES; ++h)
    {
      const int us = holeUs[i][h];
      if (!holeSet[i][h])
      {
        Serial.printf("เตือน: จาน %u ช่อง %u ยังไม่ได้ตั้ง ค่าที่พิมพ์เป็นค่าประมาณ ไม่ใช่ค่าที่วัด\n",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(h + 1));
        ++problems;
      }
      if (abs(us - restUs[i]) < MIN_HOLE_FROM_REST_US)
      {
        Serial.printf("ผิด: จาน %u ช่อง %u ห่างจากตำแหน่งพักแค่ %d us รูจะเปิดค้างตอนพัก หรือบันทึกผิดปุ่ม\n",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(h + 1), abs(us - restUs[i]));
        ++problems;
      }
      if (us < MIN_US + END_MARGIN_US || us > MAX_US - END_MARGIN_US)
      {
        Serial.printf("เตือน: จาน %u ช่อง %u ชิดปลายพิสัย servo (%d us) อาจดันตัวกั้นจนร้อน\n",
                      static_cast<unsigned>(i + 1), static_cast<unsigned>(h + 1), us);
        ++problems;
      }
    }
    // ช่อง 1-2 (เล็ก) อยู่ทางขวา = พัลส์น้อยกว่าพัก, ช่อง 3-4 (ใหญ่) อยู่ทางซ้าย = มากกว่าพัก
    // และช่อง 135 ต้องไกลจากพักกว่าช่อง 90
    const int *hu = holeUs[i];
    if (!(hu[0] < restUs[i] && hu[1] < hu[0] && hu[2] > restUs[i] && hu[3] > hu[2]))
    {
      Serial.printf("เตือน: จาน %u ลำดับช่องแปลก (ควรเป็น ช่อง2 < ช่อง1 < พัก < ช่อง3 < ช่อง4) "
                    "ตรวจว่ากดปุ่มบันทึกถูกช่อง\n", static_cast<unsigned>(i + 1));
      ++problems;
    }
  }
  return problems;
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

  const int problems = checkValues();
  Serial.println(problems == 0 ? "ตรวจแล้ว: ครบทุกจานทุกช่อง ไม่พบค่าผิดปกติ" : "แก้ตามข้อความด้านบนก่อนใช้งานจริง");
  Serial.println();
}

void runTest(uint8_t i)
{
  Serial.printf("จาน %u: ทดสอบทุกช่อง (ออกจากพักทุกครั้งเหมือนตอนจ่ายจริง)\n", static_cast<unsigned>(i + 1));
  moveTo(i, restUs[i]);
  delay(MOVE_MS);
  for (uint8_t h = 0; h < HOLES; ++h)
  {
    Serial.printf("  ช่อง %u %s\n", static_cast<unsigned>(h + 1), HOLE_NAME[h]);
    moveTo(i, holeUs[i][h]);
    delay(MOVE_MS + 500);  // ค้างให้ดูว่ารูตรงราง
    moveTo(i, restUs[i]);
    delay(MOVE_MS + 500);
  }
  Serial.println("เสร็จ");
}

void setup()
{
  // ขาอันตรายก่อนอย่างอื่น: มอเตอร์ต้องหยุด buzzer ต้องเงียบ
  for (uint8_t pin : MOTOR_PINS) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? LOW : HIGH);

  Serial.begin(115200);
  delay(300);

  // ESP32Servo กับ analogWrite แย่ง LEDC timer กัน ต้องจองก่อนเสมอ
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  resetAll();
  store.begin("servocal", false);
  const bool restored = load();

  for (uint8_t i = 0; i < COUNT; ++i)
  {
    servos[i].setPeriodHertz(50);
    current[i] = restUs[i];
    moveTo(i, current[i]);
  }

  Serial.println();
  Serial.println("=== สอบเทียบ servo: ตำแหน่งพัก + ช่องปล่อยยา 4 ช่อง ===");
  Serial.println(restored ? "โหลดค่าที่บันทึกไว้ครั้งก่อนแล้ว ทำต่อจากเดิมได้ (X = เริ่มใหม่)"
                          : "ยังไม่มีค่าที่บันทึกไว้ เริ่มจากค่าประมาณ");
  Serial.println("1 2 3 = เลือกจาน | [ ] = +-10us | { } = +-1us | c = คลายแรง | m = กึ่งกลาง 135");
  Serial.println("r = ตั้งพัก | e/E = จดขอบรู 1/2 -> หากึ่งกลาง | 5 6 7 8 = ตั้งช่อง 1-4");
  Serial.println("h = ไปพัก | a s d f = ไปช่อง 1-4 | t = ทดสอบทุกช่อง | p = พิมพ์ผล | X = ลบค่าทั้งหมด");
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
      edgeA = -1;
      Serial.printf("เลือกจาน %c\n", key);
      break;

    case '[': moveTo(i, current[i] - 10); break;
    case ']': moveTo(i, current[i] + 10); break;
    case '{': moveTo(i, current[i] - 1); break;
    case '}': moveTo(i, current[i] + 1); break;

    case 'r':
      restUs[i] = current[i];
      restSet[i] = true;
      save();
      Serial.printf("ตั้งตำแหน่งพักของจาน %u = %d us\n", static_cast<unsigned>(i + 1), restUs[i]);
      break;

    case 'e':
      edgeA = current[i];
      Serial.printf("จดขอบที่ 1 = %d us แล้ว ขยับต่อจนถึงขอบอีกด้านแล้วกด E\n", edgeA);
      break;

    case 'E':
    {
      if (edgeA < 0)
      {
        Serial.println("ยังไม่ได้จดขอบที่ 1 กด e ที่ขอบแรกก่อน");
        return;
      }
      const int edgeB = current[i];
      const int center = (edgeA + edgeB + 1) / 2;
      Serial.printf("ขอบ %d us ถึง %d us (กว้าง %d us) -> กึ่งกลาง %d us\n",
                    edgeA, edgeB, abs(edgeB - edgeA), center);
      if (abs(edgeB - edgeA) < 20)
        Serial.println("เตือน: สองขอบใกล้กันผิดปกติ อาจกด e กับ E ที่ขอบเดียวกัน");
      edgeA = -1;
      approachFromRest(i, center);
      Serial.println("วิ่งจากพักมากึ่งกลางแล้ว ถ้ารูตรงรางดี กด 5-8 เพื่อบันทึกเป็นช่องนั้น");
      break;
    }

    case '5': case '6': case '7': case '8':
    {
      const uint8_t h = static_cast<uint8_t>(key - '5');
      holeUs[i][h] = current[i];
      holeSet[i][h] = true;
      save();
      Serial.printf("ตั้งช่อง %u (%s) ของจาน %u = %d us  ตรวจซ้ำแบบใช้งานจริง: พัก -> ช่อง\n",
                    static_cast<unsigned>(h + 1), HOLE_NAME[h], static_cast<unsigned>(i + 1), holeUs[i][h]);
      approachFromRest(i, holeUs[i][h]);
      break;
    }

    case 'h': moveTo(i, restUs[i]); break;
    case 'm': moveTo(i, REST_DEFAULT); break;  // กึ่งกลางพอดี 135 องศา
    case 'a': approachFromRest(i, holeUs[i][0]); break;
    case 's': approachFromRest(i, holeUs[i][1]); break;
    case 'd': approachFromRest(i, holeUs[i][2]); break;
    case 'f': approachFromRest(i, holeUs[i][3]); break;

    case 't': runTest(i); return;
    case 'p': printResult(); return;

    case 'X':
      resetAll();
      save();
      edgeA = -1;
      Serial.println("ลบค่าที่บันทึกไว้ทั้งหมดแล้ว เริ่มใหม่จากค่าประมาณ");
      moveTo(i, restUs[i]);
      break;

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
