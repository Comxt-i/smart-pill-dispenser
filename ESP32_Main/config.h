#pragma once

#include <Arduino.h>

constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

constexpr uint8_t LCD_TIME_ADDRESS = 0x27;
constexpr uint8_t LCD_MEDICINE_ADDRESS = 0x25;

// Pin map for a classic ESP32 DevKit / ESP32-WROOM-32.
constexpr uint8_t DISPENSER_COUNT = 3;
constexpr uint8_t SERVO_PINS[DISPENSER_COUNT] = {18, 19, 23};

// Keep motion disabled until calibrated without pills.
constexpr bool ENABLE_SERVO_MOVEMENT = false;
constexpr int SERVO_MIN_PULSE_US = 1000;
constexpr int SERVO_MAX_PULSE_US = 2000;
constexpr int REST_PULSE_US[DISPENSER_COUNT] = {1500, 1500, 1500};
constexpr int RELEASE_PULSE_US[DISPENSER_COUNT] = {1750, 1750, 1750};
constexpr unsigned long MOVE_TIME_MS = 700;

constexpr uint8_t BUZZER_PIN = 25;
constexpr uint8_t CONFIRM_BUTTON_PIN = 32;
constexpr uint8_t DISPENSE_BUTTON_PIN = 33;
constexpr uint8_t CANCEL_BUTTON_PIN = 27;

// ---------------------------------------------------------------------------
// การเชื่อมต่อกับ server (ตั้งค่า SERVER_BASE_URL และ DEVICE_API_KEY ใน secrets.h)
// ---------------------------------------------------------------------------

constexpr char FIRMWARE_VERSION[] = "1.0.0";

// รอบการดึงตารางยาเมื่อไม่มีคำสั่งค้าง (server อาจสั่งให้ถี่ขึ้นผ่าน next_poll_sec)
constexpr unsigned long SYNC_INTERVAL_MS = 60UL * 1000UL;
// เว้นระยะก่อน sync ใหม่หลังเรียกไม่สำเร็จ กันยิงรัวตอน server ล่ม
constexpr unsigned long SYNC_RETRY_MS = 15UL * 1000UL;
// รอบการพยายามส่งผลการจ่ายยาที่ค้างอยู่ในคิว
constexpr unsigned long EVENT_FLUSH_INTERVAL_MS = 10UL * 1000UL;
constexpr unsigned long HTTP_TIMEOUT_MS = 8000UL;

// ตรวจใบรับรองของเซิร์ฟเวอร์เมื่อ SERVER_BASE_URL เป็น https (ใช้ค่าใน certs.h)
// ตั้งเป็น false เฉพาะตอนไล่ปัญหาเท่านั้น เพราะการไม่ตรวจ = ใครดักกลางทางก็อ่าน API Key ได้
// ค่านี้ไม่มีผลเมื่อใช้ http ธรรมดาในวง LAN
constexpr bool TLS_VERIFY_CERTIFICATE = true;

// ---------------------------------------------------------------------------
// พฤติกรรมการเตือนและการจ่ายยา
// ---------------------------------------------------------------------------

// ปลุกก่อนถึงเวลามื้อยากี่นาที (0 = ปลุกตรงเวลา)
constexpr int ALERT_LEAD_MINUTES = 0;
// เตือนนานสุดกี่นาทีก่อนบันทึกว่า "ขาดยา" — ต้องไม่เกิน grace_minutes ของ server (30 นาที)
constexpr int ALERT_TIMEOUT_MINUTES = 30;
// ถ้าเครื่องเพิ่งบูตแล้วพบว่ามื้อยาเลยมาเกินเท่านี้ ให้ข้ามไปเลยโดยไม่ปลุกย้อนหลัง
constexpr int STALE_DOSE_MINUTES = 30;

// จำนวนเม็ดต่อหนึ่งรอบหมุนจาน (ยังไม่ผ่านการสอบเทียบและยังไม่มี IR ยืนยัน)
constexpr float PILLS_PER_CYCLE = 1.0f;
// เพดานจำนวนรอบต่อการจ่ายหนึ่งครั้ง ตรงกับขีดจำกัดของ dispenseMedicine()
constexpr uint8_t MAX_CYCLES_PER_DOSE = 9;

// ความยาวสูงสุดของข้อมูลที่เก็บในหน่วยความจำ
constexpr uint8_t MAX_DOSES_PER_SLOT = 6;   // มื้อยาต่อช่องต่อวัน
constexpr uint8_t MAX_PENDING_EVENTS = 12;  // ผลการจ่ายยาที่รอส่งเมื่อเน็ตหลุด (เก็บลง NVS)
constexpr uint8_t MAX_PENDING_COMMANDS = 5; // คำสั่งจากเว็บที่รอทำ

// ---------------------------------------------------------------------------
// ปุ่มกดและเสียงเตือน
// ---------------------------------------------------------------------------

constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;
// กดปุ่ม Cancel ค้างเกินเท่านี้ = สั่งหยุดกลไกทันที (กดสั้น = ข้ามมื้อยา)
constexpr unsigned long CANCEL_HOLD_MS = 1200;

// รูปแบบเสียง: ดัง BEEP_ON_MS ดับ BEEP_OFF_MS ซ้ำทุก ALERT_BEEP_PERIOD_MS
constexpr unsigned long BEEP_ON_MS = 150;
constexpr unsigned long BEEP_OFF_MS = 150;
constexpr unsigned long ALERT_BEEP_PERIOD_MS = 5000;
// ตั้งเป็น false ถ้าใช้ buzzer แบบ active (ต่อไฟแล้วดังเอง) และไม่ต้องการ PWM
constexpr bool BUZZER_ACTIVE_HIGH = true;
