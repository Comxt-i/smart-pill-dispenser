# Smart Pill Dispenser

โปรเจกต์กล่องยาสำหรับผู้สูงอายุแบบจานหมุน 3 ชุด :

- ESP32 จำนวน 1 ตัวเป็นตัวควบคุมหลัก: Wi-Fi, หน้าเว็บ, ตารางยา, RTC, LCD และปุ่ม
- Arduino Nano จำนวน 1 ตัวควบคุม Servo 270° จำนวน 3 ตัว
- มีเซนเซอร์แสงอินฟราเรด (IR) จำนวน 3 ตัว ตรวจเม็ดยาที่ตกจากจานยา จานละ 1 ตัว โดย Nano รับผิดชอบอ่านค่า; รอยืนยันรุ่นเพื่อกำหนดขาและเพิ่มโค้ดอ่านค่า
- Servo แต่ละตัวหมุนจานของยาคนละชนิด ไม่ใช่ระบบรางยา 3 ชุด
- ESP32 ส่งคำสั่ง `ชนิดยา + จำนวนเม็ด` ไปยัง Nano ผ่าน I²C และ Logic Level Shifter

## โครงสร้างโฟลเดอร์

```text
smart-pill-dispenser/
├── ESP32_Main/
│   ├── ESP32_Main.ino
│   ├── config.h
│   ├── nano_control.h
│   ├── nano_control.cpp
│   ├── rtc_lcd.h
│   ├── rtc_lcd.cpp
│   ├── wifi_web.h
│   ├── wifi_web.cpp
│   └── secrets.example.h
├── Nano_Dispenser/
│   └── Nano_Dispenser.ino
├── docs/
│   └── ARCHITECTURE.md
├── .gitignore
└── README.md
```

เปิดไฟล์ `.ino` ภายในโฟลเดอร์ของบอร์ดที่ต้องการอัปโหลดด้วย Arduino IDE

## ไลบรารีที่ต้องใช้

ESP32 Main:

- `LiquidCrystal_I2C`
- `TimeLib`
- `DS1307RTC`

Arduino Nano:

- `Servo` และ `Wire` ที่มากับ Arduino AVR Boards

## ตั้งค่าก่อนใช้งาน

1. คัดลอก `ESP32_Main/secrets.example.h` เป็น `ESP32_Main/secrets.h` แล้วใส่ Wi-Fi ของตนเอง
2. ตรวจ I²C address ของ LCD ทั้งสองจอด้วย I²C scanner; ค่าเริ่มต้นในโค้ดคือ `0x27` และ `0x25`
3. Nano ใช้ I²C address `0x10` และรับคำสั่งสำหรับจานยา 1–3 จาก ESP32
4. ปรับ pulse width ของ Servo แต่ละตัวใน `Nano_Dispenser.ino` โดยทดสอบแบบไม่ใส่ยาก่อน
5. เปลี่ยน `ENABLE_SERVO_MOVEMENT` เป็น `true` หลังจากตรวจว่ากลไกไม่ติดขัดเท่านั้น

## ความปลอดภัยด้านไฟเลี้ยง

- ใช้ Adapter 5V regulated ต่อผ่าน Fuse และสวิตช์ก่อนเข้าบอร์ดกระจายไฟ
- ต่อ Capacitor 1000µF คร่อม `+5V` กับ `GND` ใกล้จุดจ่ายไฟ Servo
- จ่ายไฟ Servo จากบอร์ดกระจายไฟโดยตรง ห้ามจ่ายผ่านขา 5V ของ Nano
- ESP32, Nano, Servo และโมดูลที่สื่อสารกันต้องใช้ GND ร่วมกัน
- ตรวจว่า Servo รองรับ 5V และเลือกพิกัด Fuse ตามสาย ขั้วต่อ และกระแสจริงของระบบ

รายละเอียดการเชื่อมต่อและ protocol อยู่ใน [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
