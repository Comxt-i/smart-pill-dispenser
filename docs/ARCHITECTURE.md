# Architecture and wiring

## ภาพรวมระบบ

ใช้ ESP32 บอร์ดเดียวควบคุมอุปกรณ์ทั้งหมด ตัด Arduino Nano และคำสั่ง I²C ระหว่างบอร์ดออกแล้ว
ผังขานี้อิง ESP32 DevKit / ESP32-WROOM-32 แบบดั้งเดิมที่มี GPIO ตามตาราง
หากใช้ ESP32 รุ่นอื่น ต้องตรวจและปรับ `ESP32_Main/config.h` ให้ตรงกับบอร์ด

```text
Backend NestJS (ตารางยา + ประวัติการจ่ายยา)
        ^  GET /api/device/sync   (ดึงเวลา + ตารางของวันนี้ + คำสั่งค้าง ทุก 60 วินาที)
        |  POST /api/device/events (ส่งผลการจ่ายยา กันซ้ำด้วย event_id)
        v
ESP32
    |-------- GPIO18 -> Servo จานยา 1
    |-------- GPIO19 -> Servo จานยา 2
    |-------- GPIO23 -> Servo จานยา 3
    |-------- GPIO25 -> Buzzer แจ้งเตือนมื้อยา
    |-------- GPIO33 <- Dispense button -> GND
    |-------- GPIO32 <- Confirm button -> GND
    |-------- GPIO27 <- Cancel button -> GND
    |-------- IR 1–3 (reserved inputs; firmware not implemented)
    |
    | I2C: GPIO21 SDA, GPIO22 SCL (3.3V side)
    v
Bidirectional I2C Logic Level Shifter (LV=3.3V, HV=5V)
    |-------- RTC DS1307 (0x68)
    |-------- LCD 1: เวลาปัจจุบัน (0x27)
    `-------- LCD 2: ยาและรอบถัดไป (0x25)
```

LCD ทั้งสองจอและ DS1307 อยู่บน I²C bus เดียวกัน ความถี่ 100 kHz และต้องมี address ไม่ซ้ำกัน
ภาพนี้สมมติใช้โมดูล LCD/RTC แบบ 5V: ต่อ pull-up ของ SDA/SCL ให้ตรงแรงดันแต่ละฝั่ง
ตรวจ pull-up ที่ติดอยู่บนโมดูลด้วย ห้ามให้ GPIO21/22 ถูกดึงขึ้น 5V
การตัด Nano ไม่ได้ทำให้ Logic Level Shifter ของโมดูล I²C แบบ 5V หมดความจำเป็น

## ผังขา ESP32

| อุปกรณ์ | GPIO | หมายเหตุ |
|---|---:|---|
| I²C SDA | 21 | ฝั่ง 3.3V ของ Level Shifter |
| I²C SCL | 22 | ฝั่ง 3.3V ของ Level Shifter |
| Servo จาน 1 | 18 | PWM 50 Hz |
| Servo จาน 2 | 19 | PWM 50 Hz |
| Servo จาน 3 | 23 | PWM 50 Hz |
| Buzzer | 25 | ตั้งเป็น OUTPUT; เตือนซ้ำทุก 5 วินาทีขณะถึงเวลามื้อยา |
| Dispense button (K3 เขียว) | 33 | INPUT_PULLUP; สั่งจ่ายยามื้อที่กำลังเตือน |
| Snooze button (K2 เหลือง) | 32 | INPUT_PULLUP; เลื่อนการเตือน 5 นาที / สั่ง sync ใหม่ |
| Cancel button (K1 แดง) | 27 | INPUT_PULLUP; กดสั้น = ข้ามมื้อ, กดค้าง ≥1.2 วินาที = หยุดกลไก |

โมดูลปุ่ม 3 ตัวจ่ายไฟ **VCC = 3.3V เท่านั้น** ห้ามใช้ 5V เพราะ pull-up บนโมดูลจะดัน
สัญญาณเป็น 5V เข้า GPIO ที่รับได้แค่ 3.3V; ถ้าจำเป็นต้องใช้ 5V ต้องผ่าน Level Shifter
โดยฝั่ง HV ต่อปุ่มและฝั่ง LV ต่อ GPIO (แต่ shifter 4 ช่องจะเหลือไม่พอเพราะ I²C ใช้ไปแล้ว 2 ช่อง)
| IR 1 | 34 (สำรอง) | ยังไม่ตั้งค่า/อ่านใน firmware |
| IR 2 | 35 (สำรอง) | ยังไม่ตั้งค่า/อ่านใน firmware |
| IR 3 | 36 (สำรอง) | ยังไม่ตั้งค่า/อ่านใน firmware |

GPIO34/35/36 เป็น input-only และไม่มี pull-up/pull-down ภายใน
ขา IR เป็นเพียงข้อเสนอจนกว่าจะยืนยันรุ่น: ตรวจแรงดันและชนิดเอาต์พุตก่อนต่อ
ถ้าเป็น open-collector ต้องเลือก pull-up ภายนอกไป 3.3V ให้เหมาะสม
ถ้าเอาต์พุตเป็น 5V ต้องลดระดับสัญญาณก่อนเข้า ESP32
ตรวจด้วยว่า Servo รับ PWM ลอจิก 3.3V ได้หรือจำเป็นต้องมีวงจรแปลงระดับสัญญาณ

## การควบคุมการจ่าย

`dispenser_control.cpp` ใช้ `ESP32Servo` จ่าย PWM โดยตรง ไม่มี I²C address หรือ protocol ของบอร์ดจ่ายยา

- รับจาน 1–3 และจำนวนรอบ 1–9; ทำงานครั้งละจานและปฏิเสธคำสั่งซ้อน
- เมื่อรอบจบหรือถูกยกเลิก จะเก็บผลไว้ให้ `takeDispenseOutcome()` อ่านได้ครั้งเดียว
  `pill_app` ใช้ผลนี้ตัดสินว่าจะรายงาน `DISPENSED` หรือ `FAILED` ขึ้น server
- ค่าเริ่มต้น `ENABLE_SERVO_MOVEMENT = false` ใน `config.h` จึงยังไม่ attach Servo
- เมื่อเปิดการเคลื่อนไหว จะส่ง release pulse รอ `MOVE_TIME_MS` แล้วส่ง rest pulse และรออีกครั้ง นับเป็นหนึ่งรอบ
- ใช้ `millis()` เปลี่ยนสถานะโดยไม่มี `delay()` ในการขยับ Servo ทำให้ loop ยังบริการเว็บ ปุ่ม Cancel และ LCD ได้
- จบรอบหรือกด Cancel จะ detach Servo ทั้งหมดและล้างรอบที่เหลือ ปล่อยปุ่มแล้วจะไม่ทำต่อเอง
- ขณะกด Cancel ค้างไว้จะไม่รับคำสั่งเริ่มใหม่ การ detach หยุด PWM แต่ไม่ได้ตัดไฟหรือเบรกกลไก

หน้าเว็บในเครื่อง (`/dispense`) เรียกผ่าน `appManualDispense()` และตอบ `202` เมื่อเริ่มรอบ,
`400` เมื่อพารามิเตอร์ไม่ถูกต้อง, `409` เมื่อกำลังทำงานหรือกด Cancel ค้าง
และ `503` เมื่อปิดการเคลื่อนไหวหรือ attach Servo ไม่สำเร็จ
ทุกเส้นทาง (ปุ่มกด, คำสั่งจากเว็บหลัก, หน้าเว็บในเครื่อง) รายงานผลขึ้น server เหมือนกัน
การตอบว่าเริ่มรอบไม่ใช่การยืนยันว่าเม็ดยาตกครบ

## ระบบจ่ายไฟ

```text
Adapter +5V -> Fuse -> Main switch -> +5V bus
Adapter GND --------------------------> GND bus

+5V/GND bus --+--> Servo 1
              +--> Servo 2
              +--> Servo 3
              +--> ESP32 dev board (ขา 5V/VIN ตามสเปกบอร์ด)
              +--> โมดูล LCD/RTC 5V ที่ยืนยันสเปกแล้ว

ESP32 3.3V -------> Level Shifter LV
+5V bus ----------> Level Shifter HV
GND bus ----------> ESP32 / Servo / modules / Level Shifter GND

1000uF capacitor: ขา + ไป +5V bus, ขา - ไป GND bus
```

Logic Level Shifter ใช้แปลงระดับสัญญาณ ไม่ได้ใช้จ่ายไฟให้ Servo หรือ ESP32
ห้ามต่อ 5V เข้าขา 3.3V หรือ GPIO ของ ESP32 และห้ามจ่ายไฟ Servo ผ่านบอร์ด ESP32
ตรวจข้อกำหนดการจ่ายไฟของบอร์ดก่อนเสียบ USB พร้อมแหล่งจ่ายภายนอก

## โครงสร้าง firmware

`ESP32_Main.ino` ทำแค่ตั้ง I²C แล้วเรียก `appBegin()` / `appLoop()` ตรรกะทั้งหมดอยู่ใน `pill_app`
เพื่อให้โมดูลต่างๆ เรียกหากันได้โดยไม่ต้องพึ่ง auto-prototype ของไฟล์ `.ino`

| โมดูล | หน้าที่ |
|---|---|
| `pill_app` | เดินสถานะรวม: ตาราง ปุ่ม เสียง จอ คำสั่ง และการรายงานผล |
| `net_sync` | HTTP + JSON คุยกับ backend และคุมจังหวะ polling |
| `schedule_store` | ตารางยาของวันนี้ สถานะรายมื้อ การข้ามวัน และการคงสถานะเมื่อ sync ระหว่างวัน |
| `event_queue` | คิวผลการจ่ายยา เก็บลง NVS จึงไม่หายเมื่อไฟดับ |
| `dispenser_control` | คุม Servo แบบไม่ block และรายงานผลหนึ่งรอบผ่าน `takeDispenseOutcome()` |
| `buttons` / `alert` | กรองสัญญาณเด้งของปุ่ม และจังหวะเสียง buzzer แบบไม่ block |
| `rtc_lcd` | อ่าน/ตั้ง DS1307 และเขียนจอทั้งสอง |
| `wifi_web` | เชื่อม Wi-Fi แบบไม่ block และหน้าเว็บสถานะในเครื่อง |

สัญญาการเชื่อมต่อกับ backend อยู่ใน [SERVER_API.md](SERVER_API.md)

### ข้อจำกัดที่ยังเหลือ

- **IR ยังไม่ถูกอ่าน**: มี IR 3 ตัวสำหรับตรวจเม็ดยาที่ตกจากจานของตนเอง
  ยังต้องยืนยันรุ่น แรงดัน ชนิดเอาต์พุต ระดับสัญญาณเมื่อตรวจพบ และระยะติดตั้งก่อนเพิ่ม firmware
  จำนวนที่สั่งจึงยังเป็นจำนวนรอบขยับ Servo ไม่มีการนับเม็ดยาจริงหรือหยุดเมื่อยาไม่ตก
- จอ LCD เป็น HD44780 จึงแสดงภาษาไทยไม่ได้ ชื่อยาที่แสดงเป็นส่วนอักษรละตินที่ server ตัดมาให้
- API Key ถูกส่งผ่าน HTTP ธรรมดา ยังไม่มี TLS จึงเหมาะกับวง LAN ที่เชื่อถือได้เท่านั้น

## ส่วนที่ต้องทดสอบกับกลไกจริง

- ช่วง pulse ที่ Servo 270° แต่ละรุ่นรองรับ รวมทั้งตำแหน่งพัก/ปล่อยและจำนวนองศาต่อหนึ่งเม็ด
- ค่า `1000–2000 µs`, พัก `1500 µs`, ปล่อย `1750 µs`, ระยะเวลา `700 ms` เป็นเพียงค่าเริ่มต้น ไม่ใช่ค่าที่สอบเทียบแล้ว
- ตำแหน่งติดตั้ง IR และการตรวจเม็ดยาตกจริงของทั้งสามจาน
- ความจำเป็นของมอเตอร์สั่น, motor driver และประตูทางออกเสริม
- ESP32-CAM ยังไม่รวมอยู่ในระบบบอร์ดเดียวชุดนี้

ทดสอบ Servo ทีละตัวแบบไม่ใส่ยาและจำกัดช่วง pulse ก่อนใช้กับกลไกจริง

## เอกสารอ้างอิงฮาร์ดแวร์

- [ESP32 GPIO — Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html)
- [ESP32 Datasheet — Espressif](https://documentation.espressif.com/esp32_datasheet_en.html)
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo)
