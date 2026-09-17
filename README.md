# Smart Pill Dispenser

โปรเจกต์กล่องยาสำหรับผู้สูงอายุแบบจานหมุน 3 ชุด ใช้ **ESP32 เพียงบอร์ดเดียว**

- ESP32 ควบคุม Wi-Fi, หน้าเว็บ, RTC DS1307, LCD 2 จอ และ Servo 270° จำนวน 3 ตัวโดยตรง
- Servo แต่ละตัวหมุนจานของยาคนละชนิด ไม่ใช่ระบบรางยา 3 ชุด
- มี IR จำนวน 3 ตัวสำหรับตรวจเม็ดยาที่ตกจากจานยา จานละ 1 ตัว; ยังรอยืนยันรุ่นและเพิ่มโค้ดอ่านค่าบน ESP32
- ไม่มีบอร์ด Nano หรือ protocol ส่งคำสั่งระหว่างบอร์ด
- ตารางยาอัตโนมัติยังไม่ถูกพัฒนาใน firmware ปัจจุบัน; สั่งขยับ Servo ผ่านหน้าเว็บและยกเลิกด้วยปุ่มได้

## โครงสร้างโฟลเดอร์

```text
smart-pill-dispenser/
├── ESP32_Main/
│   ├── ESP32_Main.ino
│   ├── config.h
│   ├── dispenser_control.h
│   ├── dispenser_control.cpp
│   ├── rtc_lcd.h
│   ├── rtc_lcd.cpp
│   ├── wifi_web.h
│   ├── wifi_web.cpp
│   └── secrets.example.h
├── tests/
├── docs/
│   └── ARCHITECTURE.md
├── .gitignore
└── README.md
```

เปิด `ESP32_Main/ESP32_Main.ino` ด้วย Arduino IDE และอัปโหลดเพียงบอร์ดเดียว

## บอร์ดและไลบรารีที่ต้องใช้

ผังขาเริ่มต้นอิง ESP32 DevKit / ESP32-WROOM-32 แบบดั้งเดิม ไม่ใช่ ESP32-C3/S3
ติดตั้งแพ็กเกจบอร์ด `esp32 by Espressif Systems` และเลือกบอร์ดให้ตรงกับฮาร์ดแวร์
ติดตั้งไลบรารี:

- `ESP32Servo` สำหรับควบคุม Servo ด้วย PWM ของ ESP32
- `LiquidCrystal_I2C`
- `TimeLib` (ชื่อใน Library Manager: `Time`)
- `DS1307RTC`

`Wire`, `WiFi` และ `WebServer` มาพร้อมแพ็กเกจบอร์ด ESP32

## ตั้งค่าก่อนใช้งาน

1. คัดลอก `ESP32_Main/secrets.example.h` เป็น `ESP32_Main/secrets.h` แล้วใส่ Wi-Fi ของตนเอง
2. ต่อสายตาม [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): Servo จาน 1–3 ใช้ GPIO18, GPIO19, GPIO23
3. ตรวจ I²C address ของ LCD ทั้งสองจอ; ค่าเริ่มต้นคือ `0x27` และ `0x25` ส่วน DS1307 คือ `0x68`
4. ตรวจช่วง pulse ที่ Servo รองรับ แล้วปรับ `SERVO_MIN_PULSE_US`, `SERVO_MAX_PULSE_US`, `REST_PULSE_US`, `RELEASE_PULSE_US` และ `MOVE_TIME_MS` ใน `config.h` โดยทดสอบแบบไม่ใส่ยาก่อน ค่าเริ่มต้นเป็นเพียงตัวอย่าง ไม่ใช่ช่วงหมุนครบ 270°
5. เปลี่ยน `ENABLE_SERVO_MOVEMENT` เป็น `true` และอัปโหลดใหม่เมื่อพร้อมทดสอบการเคลื่อนไหว; ค่าเริ่มต้น `false` จะปฏิเสธคำสั่งโดยไม่จ่าย PWM
6. เปิด Serial Monitor ที่ 115200 baud และเปิด IP ที่แสดงหลังเชื่อมต่อ Wi-Fi เลือกจาน 1–3 และจำนวนรอบ 1–9

ระหว่างทำงานระบบจะปฏิเสธคำสั่งจ่ายซ้ำ หน้าเว็บและ LCD ยังได้รับการอัปเดตระหว่างขยับ Servo
กดปุ่ม Cancel ที่ GPIO27 ลง GND เพื่อหยุดส่ง PWM และยกเลิกรอบที่เหลือ
การหยุด PWM ไม่ใช่การตัดไฟ Servo หรือการรับประกันว่ากลไกหยุดทันที
จำนวนรอบยังไม่ใช่จำนวนเม็ดยาที่ตรวจยืนยันด้วย IR

## ระบบจ่ายไฟ

- ใช้ Adapter 5V regulated ต่อผ่าน Fuse และสวิตช์ก่อนเข้าบอร์ดกระจายไฟ
- ต่อ Capacitor 1000µF คร่อม `+5V` กับ `GND` ใกล้จุดจ่ายไฟ Servo
- จ่ายไฟ Servo จากบอร์ดกระจายไฟโดยตรง ห้ามจ่ายผ่านขา 3.3V/5V ของ ESP32
- ESP32, Servo และโมดูลที่สื่อสารกันต้องใช้ GND ร่วมกัน
- ตรวจว่า Servo รองรับ 5V และเลือกพิกัด Fuse ตามสาย ขั้วต่อ และกระแสจริงของระบบ
- GPIO ของ ESP32 ใช้ลอจิก 3.3V: โมดูล I²C ที่ดึงสัญญาณขึ้น 5V ยังต้องผ่าน Logic Level Shifter แม้ตัด Nano ออกแล้ว

รายละเอียดระดับสัญญาณ ผังสาย และข้อจำกัด firmware อยู่ใน [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

## ทดสอบ logic บนคอมพิวเตอร์

รัน `python3 tests/test_dispenser_control.py` (ต้องมี `c++`) เพื่อทดสอบด้วยฮาร์ดแวร์จำลอง
การทดสอบนี้ไม่แทนการ compile ด้วย ESP32 toolchain หรือการทดสอบกับบอร์ดและกลไกจริง
