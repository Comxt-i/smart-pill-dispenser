// ทดสอบ passive buzzer บน ESP32 ด้วยวิธีเดียวกับเฟิร์มแวร์จริงเป๊ะ (PWM ช่อง 7, 10 bit, ขา GPIO15)
//
// ใช้ไฟ USB อย่างเดียวได้ ไม่ต้องต่อ adapter และไม่ขยับ servo หรือมอเตอร์สั่นเลย
// อัปโหลดด้วยบอร์ด "ESP32 Dev Module" แล้วเปิด Serial Monitor ที่ 115200
//
// วนทีละขั้น ขั้นละ 1.5 วินาที แล้วพัก:
//   1. DC   เปิด-ปิดไฟค้าง แบบเฟิร์มแวร์เก่า = passive buzzer ได้ยินแค่ "แต็ก" (ถูกต้อง)
//   2. 1000 Hz   3. 2000 Hz (ค่าที่เฟิร์มแวร์ใช้)   4. 3000 Hz   5. 4000 Hz
//
// อ่านผล:
//   ขั้น 2-5 ดัง "ปี๊บ" = สายและโมดูลปกติ ถ้ากล่องจริงยังดังแค่แต็ก แปลว่ากล่องยังเป็นเฟิร์มแวร์เก่า
//   ขั้น 2-5 ก็ยังแค่แต็ก/เงียบ = ปัญหาที่สายหรือการต่อ (เช่นต่อผ่าน shifter) ไม่ใช่เฟิร์มแวร์
//   ขั้นไหนดังที่สุด ใช้ความถี่นั้นเป็น BUZZER_TONE_HZ ใน config.h
//   Serial ขึ้น "ผูก PWM ไม่สำเร็จ" = ปัญหาที่ core/บอร์ด บอกผลนี้มา

constexpr uint8_t BUZZER_PIN = 15;          // ตรงกับ config.h
constexpr uint8_t BUZZER_CHANNEL = 7;       // ตรงกับ BUZZER_LEDC_CHANNEL
constexpr uint8_t RESOLUTION_BITS = 10;     // ตรงกับ BUZZER_PWM_RESOLUTION_BITS
constexpr uint32_t TONE_DUTY = 1UL << (RESOLUTION_BITS - 1);  // 50% = ดังที่สุด
constexpr unsigned long STEP_MS = 1500;
constexpr uint32_t FREQUENCIES[] = {1000, 2000, 3000, 4000};

bool attached = false;

void playDc()
{
  Serial.println("[1] DC เปิด-ปิดไฟค้าง (แบบเฟิร์มแวร์เก่า) passive buzzer ควรได้ยินแค่แต็ก");
  if (attached)
  {
    ledcDetach(BUZZER_PIN);
    attached = false;
  }
  pinMode(BUZZER_PIN, OUTPUT);
  for (int i = 0; i < 3; ++i)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(250);
    digitalWrite(BUZZER_PIN, LOW);
    delay(250);
  }
}

void playTone(uint8_t step, uint32_t freq)
{
  Serial.printf("[%u] %lu Hz ควรได้ยินเสียงปี๊บยาว\n", step, static_cast<unsigned long>(freq));
  if (attached)
  {
    ledcDetach(BUZZER_PIN);
    attached = false;
  }
  attached = ledcAttachChannel(BUZZER_PIN, freq, RESOLUTION_BITS, BUZZER_CHANNEL);
  if (!attached)
  {
    Serial.println("    ผูก PWM ไม่สำเร็จ ช่องหรือ timer ไม่ว่าง");
    delay(STEP_MS);
    return;
  }
  ledcWrite(BUZZER_PIN, TONE_DUTY);
  delay(STEP_MS);
  ledcWrite(BUZZER_PIN, 0);
}

void setup()
{
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== ทดสอบ passive buzzer ที่ GPIO15 (PWM ช่อง 7) ===");
}

void loop()
{
  playDc();
  delay(700);
  uint8_t step = 2;
  for (uint32_t freq : FREQUENCIES)
  {
    playTone(step++, freq);
    delay(500);
  }
  Serial.println("--- พัก 3 วินาที แล้ววนใหม่ ---");
  delay(3000);
}
