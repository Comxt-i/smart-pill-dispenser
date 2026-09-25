# สัญญาการเชื่อมต่อระหว่าง ESP32 กับ Backend

เอกสารนี้อธิบายข้อตกลงที่ firmware (`ESP32_Main/`) กับ backend (`embedded_project/backend`) ใช้คุยกัน
ถ้าแก้ฝั่งใดฝั่งหนึ่ง ต้องแก้อีกฝั่งให้ตรงกันด้วย

## ภาพรวม

```text
เว็บ (React + JWT)                         ESP32 (X-API-Key)
  |                                           |
  | ตั้งตารางยา / จัดยาลงช่อง                  |
  v                                           |
Backend (NestJS + Prisma)  <--- GET /api/device/sync ------+  ดึงเวลา + ตารางยาของวันนี้ + คำสั่งค้าง
       ^                   <--- POST /api/device/events ---+  ส่งผลการจ่ายยากลับ
       |                                                      (กันซ้ำด้วย event_id)
       +--- POST /api/devices/:id/dispense (สั่งจ่ายทันทีจากเว็บ)
```

ESP32 อยู่หลัง NAT และไม่มี public IP ฝั่ง server จึง **push ไม่ได้**
ทุกอย่างจึงเป็น polling: เครื่องถามเองทุก `next_poll_sec` วินาที (ปกติ 60 วินาที, เหลือ 5 วินาทีเมื่อมีคำสั่งค้าง)

## การเชื่อมต่อ: HTTP ในวง LAN หรือ HTTPS ผ่านอินเทอร์เน็ต

firmware ดูจาก scheme ของ `SERVER_BASE_URL` เองว่าจะใช้แบบไหน

| | LAN (`http://`) | Deploy จริง (`https://`) |
|---|---|---|
| ใบรับรอง | ไม่มี | ตรวจด้วย ISRG Root X1 ใน `certs.h` |
| นาฬิกา | ไม่เกี่ยว | ต้องตรงก่อน ไม่งั้นเชื่อมต่อไม่ผ่าน |
| API Key | ส่งแบบไม่เข้ารหัส | เข้ารหัสด้วย TLS |

ถ้าใช้ HTTPS เครื่องจะตั้งนาฬิการะบบก่อนเสมอ โดยไล่จากนาฬิการะบบ → DS1307 → NTP
ปิดการตรวจใบรับรองได้ที่ `TLS_VERIFY_CERTIFICATE` ใน `config.h` แต่ควรใช้เฉพาะตอนไล่ปัญหา

วิธี deploy ดู [embedded_project/DEPLOY.md](../../embedded_project/DEPLOY.md)

## การยืนยันตัวตน

| ผู้เรียก | วิธี | ใช้กับ |
|---|---|---|
| ESP32 | header `X-API-Key: <Device.api_key>` | `/api/device/*`, `/api/medications/device-sync`, `/api/intake-logs` |
| เว็บ | header `Authorization: Bearer <JWT>` | endpoint อื่นทั้งหมด |

`/api/device/*` ใช้ `DeviceAuthGuard` ที่ **ปฏิเสธคำขอที่ไม่มี key เสมอ**
API Key ของเครื่องอยู่ในตาราง `Device.api_key`
ค่า `esp32_sec_key_246f28abc199` เป็นค่าสำหรับ **dev เท่านั้น** (อยู่ใน git จึงถือว่าสาธารณะ)
ตอน seed ด้วย `NODE_ENV=production` ระบบจะสุ่ม API Key ใหม่ให้เสมอ

---

## GET /api/device/time

ใช้ตั้ง RTC ตอนบูตโดยไม่ต้องดึงตารางทั้งชุด

```json
{
  "ok": true,
  "server_time": "2026-09-17T06:47:44.831Z",
  "utc_epoch": 1789627664,
  "local_epoch": 1789652864,
  "tz_offset_minutes": 420
}
```

`local_epoch` = `utc_epoch + tz_offset_minutes * 60` เขียนลง DS1307 ได้ตรงๆ โดยไม่ต้องคำนวณ timezone บนเครื่อง

## GET /api/device/sync

Query (ไม่บังคับ): `firmware_version`, `ip_address`, `rssi` — server เก็บเป็น telemetry และอัปเดต `last_seen_at`

```json
{
  "ok": true,
  "local_epoch": 1789652864,
  "tz_offset_minutes": 420,
  "day_of_week": "THU",
  "device": { "id": "...", "name": "...", "grace_minutes": 30 },
  "next_poll_sec": 60,
  "config_version": "28f8864f",
  "slots": [
    {
      "slot": 1,
      "active": true,
      "medication_id": "3ee842f7-...",
      "name": "Paracetamol",
      "full_name": "พาราเซตามอล (Paracetamol)",
      "amount_per_dose": 1,
      "pill_size_mm": 13,
      "meal_timing": "AFTER_MEAL",
      "doses": [
        { "schedule_id": "b841e121-...", "time": "08:00", "minutes": 480, "label": null, "done": true }
      ]
    }
  ],
  "commands": [
    { "id": "e392e90d-...", "type": "DISPENSE", "slot": 1, "amount": 1, "schedule_id": null }
  ]
}
```

### `pill_size_mm` — ขนาดเม็ดยา (ไม่บังคับ)

มาจากช่อง "รูปทรงและขนาดเม็ดยา" บนเว็บ (`Medication.pill_size_mm`) บอกเครื่องว่าควรเริ่มปล่อยยาจากช่องไหนบนจาน
backend ยอมรับเฉพาะ 4 ค่านี้

| ค่า | ความหมาย | ช่องบนจาน |
|---|---|---|
| `8` | ทรงกลม ขนาดไม่เกิน 8 mm | หมุนขวา 90° |
| `13` | ทรงกลม ขนาดไม่เกิน 13 mm | หมุนขวา 135° |
| `15` | ทรงกลม ขนาดไม่เกิน 15 mm | หมุนซ้าย 90° |
| `25` | ทรงรี/แคปซูล ขนาดไม่เกิน 25 mm | หมุนซ้าย 135° |
| `null` | ไม่ระบุ | ไล่ลองจากช่องเล็กสุด |

ช่องที่ไม่ใช่ของบัญชีที่ผูกกับกล่อง ส่ง `null` เสมอ

การจ่าย (ต้องมีเซ็นเซอร์ IR): ลองช่องตามขนาดก่อน 10 รอบ ไม่มีเม็ดตกก็ไปช่องที่ใหญ่กว่า
ช่องละ 10 รอบ (ไม่ย้อนไปช่องที่เล็กกว่า) ได้เม็ดแล้วอยู่ช่องเดิมต่อ ครบแล้วยังไม่ได้ = บันทึกว่าจ่ายไม่ครบ
ไม่ระบุขนาด = ไล่จากช่องเล็กสุดไปใหญ่สุด
ถ้าไม่มีเซ็นเซอร์ เครื่องหมุนแค่หนึ่งรอบต่อเม็ดที่ช่องแรก เพราะวนหาช่องโดยไม่รู้ว่ายาตกหรือยังอาจเทยาออกมาหลายเม็ด

ข้อตกลงที่สำคัญ:

- ส่งเฉพาะมื้อยา **ของวันนี้** เท่านั้น (กรองด้วย `days_of_week` มาให้แล้ว) firmware ไม่ต้องตีความรหัสวัน
- `name` เป็น **ASCII เท่านั้น** เพราะ LCD HD44780 แสดงภาษาไทยไม่ได้
  server ดึงส่วนอักษรละตินจากชื่อยาให้ (เช่น `พาราเซตามอล (Paracetamol)` → `Paracetamol`)
  ถ้าชื่อยาไม่มีอักษรละตินเลยจะได้ `Slot 1` แทน ส่วนชื่อเต็มอยู่ใน `full_name`
- `active: false` = ช่องว่างหรือถูกปิดใช้งานจากเว็บ เครื่องต้องไม่ปลุกและไม่จ่ายช่องนั้น
- `done: true` = มื้อนั้นมี IntakeLog แล้ว ใช้กันไม่ให้เครื่องปลุกซ้ำหลังรีบูตกลางวัน
- `config_version` เปลี่ยนเมื่อตารางหรือการจัดช่องเปลี่ยน ใช้ไล่ปัญหาว่าเครื่องได้ตารางรุ่นไหน
- คำสั่งที่ถูกส่งออกไปแล้วจะเปลี่ยนเป็น `SENT` และหมดอายุอัตโนมัติใน 15 นาทีถ้าไม่มีผลกลับ

## GET /api/device/wait — server บอกกล่องทันทีเมื่อมีอะไรเปลี่ยน

กล่องอยู่หลังเราเตอร์บ้าน server เปิดการเชื่อมต่อไปหาเองไม่ได้ กล่องจึงเปิดสายนี้ค้างไว้
แล้ว server ตอบทันทีที่มีการเปลี่ยนแปลง (long polling) กล่องจึงรู้การแก้ไขบนเว็บในไม่ถึงวินาที
โดยไม่ต้องถามรัวทุกไม่กี่วินาที

```
GET /api/device/wait?state=ab12cd34&timeout_sec=15
```

| query | ความหมาย |
|---|---|
| `state` | `state_version` ล่าสุดที่กล่องได้จาก sync |
| `timeout_sec` | ถือสายไว้นานสุด 1–25 วินาที (ค่าเริ่มต้น 20) |
| `dry_run` | ต้องตรงกับที่ใช้ตอน sync |

```json
{ "ok": true, "changed": true, "state_version": "9f00e1a2", "pending_commands": 1 }
```

- ตอบ **ทันที** (`changed: true`) ถ้า `state` ไม่ตรงกับปัจจุบัน หรือมีคำสั่งใหม่ที่ยังไม่ส่ง (`PENDING`)
- ไม่อย่างนั้นถือสายไว้ มีการเขียนข้อมูลที่กระทบกล่องเมื่อไร ตรวจใหม่แล้วตอบทันทีถ้าเปลี่ยนจริง
- ครบ `timeout_sec` ยังไม่มีอะไรเปลี่ยน ตอบ `changed: false` กล่องเปิดสายใหม่ทันที
- กล่องได้ `changed: true` แล้วเรียก `GET /api/device/sync` เพื่อเอาข้อมูลจริง (`/wait` ไม่ส่งตารางมาเอง)
- นับเฉพาะคำสั่ง `PENDING` คำสั่ง `SENT` ถูกส่งซ้ำทุก sync จนกล่องรายงานผล ถ้านับด้วยจะตอบ "เปลี่ยน" ตลอด

**server รู้ได้ยังไงว่ามีอะไรเปลี่ยน:** Prisma middleware (`src/prisma/prisma.service.ts`) เคาะ "กระดิ่ง"
(`src/modules/device/change-bus.ts`) หลังเขียนตาราง `Medication`, `Schedule`, `Compartment`,
`DeviceCommand`, `IntakeLog` สำเร็จ และ `Device` เฉพาะตอนแก้ `user_id`, `timezone_offset`, `name`
(ไม่นับ `last_seen_at`/`status` ที่กล่องเขียนเองทุกครั้งที่ถาม) จุดเดียวครอบทุกหน้าของเว็บ ไม่ต้องไล่ใส่ทีละ service

**ข้อจำกัด:** กระดิ่งอยู่ในหน่วยความจำของ process เดียว ใช้ได้เพราะ deploy เป็น backend ตัวเดียวบน VPS
ถ้าขยายเป็นหลาย instance ต้องเปลี่ยนเป็น pub/sub (เช่น Postgres `LISTEN/NOTIFY`)
ระหว่างนั้นกล่องยังถูกต้องเสมอ เพราะมี sync เต็มทุก `next_poll_sec` (300 วินาที) เป็นตาข่ายรองรับ

### `schema=2` — ตารางทั้งสัปดาห์สำหรับเก็บไว้ในกล่อง

กล่องรุ่นใหม่ขอ `GET /api/device/sync?schema=2` แล้วได้ **ทุกมื้อของทุกวัน** แต่ละมื้อมี `days`:

```json
{ "schedule_id": "b841...", "time": "08:00", "minutes": 480, "days": "MON,WED,FRI", "done": false }
```

- `days` เรียงตามสัปดาห์ `""` = ทุกวัน กล่องเลือกมื้อของวันนี้เองจากนาฬิกาในเครื่อง
- `done` มีความหมายเฉพาะมื้อของวันนี้ มื้อของวันอื่นเป็น `false` เสมอ
- `state_version` เปลี่ยนเมื่อตาราง ชื่อ ขนาดยา เวลากิน วันที่กิน **หรือมื้อที่ถูกบันทึกว่ากินแล้ว** เปลี่ยน
  (ญาติกดบันทึกบนเว็บ กล่องต้องหยุดเตือนมื้อนั้น)
- `next_poll_sec` เป็น 300 เพราะ `/wait` แจ้งการเปลี่ยนอยู่แล้ว (มีคำสั่งค้าง = 2)
- ไม่ส่ง `schema` (กล่องรุ่นเก่า) = ได้ตารางของวันนี้แบบเดิม ไม่มี `days` ถามทุก 5 วินาทีเหมือนเดิม

กล่องเก็บคำตอบนี้ลง NVS เมื่อ `state_version` เปลี่ยน จึงเตือนได้ทันทีหลังเปิดเครื่องและตลอดช่วงที่เน็ตล่ม
มื้อที่จบในกล่องถูกบันทึกลง NVS แยกทันที กันเตือนหรือจ่ายซ้ำหลังไฟดับ แม้ server ยังไม่รู้

## POST /api/device/events

ส่งผลการจ่ายยาเป็นชุด (สูงสุด 20 รายการต่อครั้ง)

```json
{
  "events": [
    {
      "event_id": "AABBCC-12",
      "status": "DISPENSED",
      "schedule_id": "3a796201-...",
      "slot_number": 1,
      "amount": 1,
      "command_id": null,
      "timestamp": "2026-09-17T12:05:00+07:00",
      "note": null
    }
  ]
}
```

คำตอบ:

```json
{ "ok": true, "received": 1, "accepted": ["AABBCC-12"], "rejected": [] }
```

ข้อตกลงที่สำคัญ:

- `event_id` เป็นกุญแจกันบันทึกซ้ำ (firmware สร้างจาก MAC + ตัวนับที่เก็บใน NVS)
  **ส่งซ้ำได้ไม่จำกัด** ถ้าไม่ได้รับคำตอบเพราะเน็ตหลุด จะไม่เกิด log ซ้ำ
- รายการที่อยู่ใน `accepted` **และ** `rejected` ถือเป็นคำตอบสุดท้าย firmware ต้องลบออกจากคิวทั้งคู่
  ถ้าไม่ลบรายการที่ถูกปฏิเสธ คิวจะวนส่งไม่จบ
- ถ้าทั้ง request ล้มเหลว (HTTP ไม่ใช่ 200) ให้เก็บทุกรายการไว้ส่งใหม่
- `status` ที่รับ: `DISPENSED`, `TAKEN_LATE`, `MISSED`, `SKIPPED`, `FAILED`
  และ `ACK` ที่ใช้ปิดคำสั่งซึ่งไม่ใช่การจ่ายยา (`BUZZ`, `CANCEL`) โดยไม่สร้างประวัติการจ่ายยา
- server จะ **ยกระดับ `DISPENSED` เป็น `TAKEN_LATE` เอง** ถ้า `timestamp` เลยเวลามื้อยาเกิน `grace_minutes`
  firmware ไม่ต้องตัดสินใจเรื่องนี้
- `note` ต้องเป็น **ASCII สั้นไม่เกิน 31 ไบต์** เพราะ `PendingEvent::note` บนเครื่องมีแค่ 32 ไบต์
  ข้อความไทยที่ถูกตัดกลางตัวอักษร UTF-8 จะทำให้ JSON ทั้งชุดเสียและถูกปฏิเสธทั้งหมด
  ฝั่งเว็บแปลรหัสเหล่านี้เป็นภาษาไทยผ่าน `DEVICE_NOTE_LABELS_TH`

การหายาที่ตรงกับ event ทำตามลำดับ: `medication_id` (หรือ `rfid_code`) → `schedule_id` → `slot_number`
ถ้าหาไม่เจอจะถูกปฏิเสธพร้อมเหตุผล

## POST /api/devices/:id/dispense (เว็บสั่ง)

```json
{ "slot_number": 1, "amount": 1, "schedule_id": null, "note": null }
```

สร้าง `DeviceCommand` สถานะ `PENDING` เครื่องจะมารับตอน sync รอบถัดไป
สถานะเดินทาง `PENDING → SENT → DONE/FAILED` โดย `DONE/FAILED` ถูกปิดเมื่อเครื่องส่ง event ที่มี `command_id` กลับมา
ช่องที่ยังไม่มียาหรือถูกปิดใช้งานจะถูกปฏิเสธด้วย `404`

## POST /api/medications/device-sync

ใช้ตอนสแกน RFID/Barcode ที่ซองยา

```json
{ "mac_address": "24:6F:28:AB:C1:99", "rfid_code": "RFID-AMX-8801", "name": "ชื่อยา (ไม่บังคับ)" }
```

ยาที่มี `rfid_code` นี้อยู่แล้วจะถูกผูกกับเครื่อง ส่วนยาใหม่จะถูกสร้างเป็นรายการร่าง (ยังไม่มีตารางเวลา)
ให้ผู้ใช้ไปกรอกรายละเอียดและตั้งเวลาต่อบนเว็บ

---

## สถานะออนไลน์/ออฟไลน์

ESP32 แจ้งตอน "ดับ" ไม่ได้ ฝั่ง server จึงคำนวณจาก `last_seen_at`:
ไม่ได้ยินจากเครื่องเกิน 180 วินาที (`OFFLINE_AFTER_SEC`) จะถูกปรับเป็น `OFFLINE` ตอนที่เว็บอ่านรายการอุปกรณ์

## ค่าที่ต้องตรงกันสองฝั่ง

| ความหมาย | firmware (`config.h`) | backend (`device.service.ts`) |
|---|---|---|
| ระยะผ่อนผันก่อนนับว่าขาดยา | `ALERT_TIMEOUT_MINUTES = 30` | `GRACE_MINUTES = 30` |
| รอบ polling ปกติ | `SYNC_INTERVAL_MS = 60000` | `POLL_INTERVAL_SEC = 60` |
| จำนวนช่อง | `DISPENSER_COUNT = 3` | `slot_number` 1–3 |

`ALERT_TIMEOUT_MINUTES` ต้องไม่เกิน `GRACE_MINUTES` ไม่อย่างนั้นเครื่องจะยังเตือนอยู่
ทั้งที่ server ถือว่ามื้อนั้นขาดยาไปแล้ว
