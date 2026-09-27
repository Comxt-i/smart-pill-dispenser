# ภาพการต่อ ESP32 — Smart Pill Dispenser

ตารางการต่อสายเชิงตรรกะสำหรับ ESP32 DevKit / ESP32-WROOM-32 แบบดั้งเดิม อ้างอิง `ESP32_Main/config.h` และ firmware ปัจจุบัน ไม่ใช่ผังตำแหน่งขาบนบอร์ด: ให้ต่อโดยดูชื่อ GPIO ที่สกรีนบนบอร์ดจริง

## ตารางต่อสาย

| ขา ESP32 | ต่อไปยัง |
|---|---|
| GPIO18 | สายสัญญาณ Servo จาน 1 |
| GPIO19 | สายสัญญาณ Servo จาน 2 |
| GPIO23 | สายสัญญาณ Servo จาน 3 |
| GPIO15 | IN ของวงจรขับ/โมดูล Active buzzer ที่รองรับสัญญาณ 3.3V และ active HIGH |
| GPIO13 / GPIO16 | DRV8833 ช่อง 1: IN1 (PWM) / IN2 (DIR) ของมอเตอร์สั่น |
| GPIO26 / GPIO17 | DRV8833 ช่อง 2: IN1 (PWM) / IN2 (DIR) ของมอเตอร์สั่น |
| GPIO4 / GPIO14 | DRV8833 ช่อง 3: IN1 (PWM) / IN2 (DIR) ของมอเตอร์สั่น |
| GPIO25 / GPIO2 / GPIO5 | LED แดง / เหลือง / เขียว (ถ้าเปิดใช้) |
| GPIO32 | ปุ่ม Snooze (เหลือง) อีกขั้วต่อ GND |
| GPIO33 | ปุ่ม Dispense อีกขั้วต่อ GND |
| GPIO27 | ปุ่ม Cancel อีกขั้วต่อ GND |
| GPIO21 | LV1 ของ bidirectional I²C level shifter; HV1 ต่อ SDA ของ LCD ทั้งสองและ DS1307 |
| GPIO22 | LV2 ของ bidirectional I²C level shifter; HV2 ต่อ SCL ของ LCD ทั้งสองและ DS1307 |
| 3V3 | LV ของ level shifter |
| GND | GND ร่วมของทุกอุปกรณ์และแหล่งจ่าย |
| 5V/VIN | บัส +5V เฉพาะเมื่อสเปกบอร์ดรองรับขานี้เป็นอินพุต 5V |

ปุ่มเป็นแบบกดติดปล่อยดับ (normally open) ใช้ `INPUT_PULLUP` ในโค้ด ไม่ต่อปุ่มเข้าบัส +5V
มอเตอร์สั่นสามตัวใช้ DRV8833 สองบอร์ด; ต่อ GND ร่วมกันและตั้งขา `SLP`/`nSLEEP` เป็น HIGH ตามรุ่นบอร์ด
GPIO14 อาจมีสัญญาณระหว่างบูต เฟิร์มแวร์ตั้งขา driver ให้อยู่ในสถานะปลอดภัยตั้งแต่ต้น `setup()`; อย่าย้ายการเรียกนี้ไปหลังการเริ่มอุปกรณ์อื่น

## I²C และไฟเลี้ยง

- LCD 1 แสดงเวลา: `0x25`; LCD 2 แสดงยา: `0x27`; RTC DS1307: `0x68` ใช้บัสร่วม 100 kHz
- ภาพสมมติ LCD backpack และ RTC เป็นโมดูล 5V ตามเอกสารโปรเจกต์: VCC ต่อบัส +5V และ GND ต่อกราวด์ร่วม
- Level shifter: LV = 3.3V จาก ESP32, HV = บัส +5V, GND = กราวด์ร่วม ต้องเป็นชนิดรองรับ I²C สองทิศทาง พร้อม pull-up ไปแรงดันที่ถูกต้องในแต่ละฝั่ง ตรวจตัวต้านทานที่ติดมากับโมดูลด้วย
- Adapter 5V regulated → Fuse → สวิตช์ → บัส +5V; ขั้วลบ Adapter → บัส GND
- Servo ทั้งสามรับไฟตรงจากบัส ไม่ผ่านขาไฟของ ESP32 ตรวจว่า Servo รองรับไฟ 5V และ PWM 3.3V; หากไม่รองรับสัญญาณ 3.3V ต้องเพิ่มวงจรแปลงระดับที่เหมาะสม
- Capacitor 1000 µF ต่อคร่อมบัสใกล้จุดจ่าย Servo: ขั้ว + ต่อ +5V และขั้ว − ต่อ GND เลือกพิกัดแรงดันสูงกว่าแรงดันบัส
- เลือกกระแส Adapter และ Fuse ตามกระแสจริงของ Servo รวมถึงขณะติดขัด และพิกัดสาย/ขั้วต่อ เพราะโปรเจกต์ยังไม่ได้ระบุรุ่น Servo
- ตรวจวงจรไฟของบอร์ดก่อนเสียบ USB พร้อมไฟภายนอก ห้ามต่อ 5V เข้าขา GPIO หรือ 3V3

## ส่วนที่ต้องยืนยันกับอุปกรณ์จริง

ภาพ Buzzer เป็นตัวอย่างโมดูล Active buzzer พร้อมวงจรขับ ใช้ไฟ 5V และรับ IN 3.3V แบบ active HIGH ต้องตรวจสเปกโมดูลก่อนเลือกใช้ โค้ดใช้ `digitalWrite()` เปิด/ปิด ไม่ได้สร้างเสียงความถี่สำหรับ passive buzzer และ `BUZZER_ACTIVE_HIGH` ใช้เลือกขั้วลอจิก

firmware มีโค้ดอ่านเซ็นเซอร์ดิจิทัลแบบ active-low ที่ GPIO34/35/36 แต่การทำงานร่วมกับชุด **Laser Head Transmitter & Receiver Module KY-008** ยังไม่ผ่านการยืนยันกับอุปกรณ์จริง ขาเหล่านี้เป็นเพียงข้อเสนอสำหรับสัญญาณจากตัวรับแสง ไม่ใช่ขาควบคุมตัวส่งเลเซอร์ ต้องตรวจผังขา แรงดันและชนิดเอาต์พุตก่อนต่อ และ GPIO เหล่านี้ไม่มี pull-up/pull-down ภายใน

ค่า `ENABLE_SERVO_MOVEMENT = false` ทำให้ Servo ยังไม่ขยับ แม้ต่อสายครบแล้ว ต้องสอบเทียบกลไกแบบไม่ใส่ยาก่อนเปิดเป็น `true`

## อ้างอิง

- [ผังระบบของโปรเจกต์](ARCHITECTURE.md)
- [ค่าขาใน firmware](../ESP32_Main/config.h)
- [Espressif: ข้อจำกัดแรงดัน GPIO](https://docs.espressif.com/projects/esp-faq/en/latest/hardware-related/hardware-design.html)
- [Analog Devices: DS1307 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS1307.pdf)

[คำอธิบายสำหรับสร้างภาพผังสาย](esp32-wiring-image-prompt.txt) เป็นเอกสารประกอบเพิ่มเติม; ตารางข้างต้นคือ pin map สำหรับ firmware ปัจจุบัน
