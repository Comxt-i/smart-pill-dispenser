#pragma once

#include <Arduino.h>

constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

// ---------------------------------------------------------------------------
// จอ LCD สองจอ
// ---------------------------------------------------------------------------
//
// จอเล็ก = นาฬิกา (วันที่ + เวลาปัจจุบัน)
// จอใหญ่ = ตารางยา (บรรทัดละ 1 ช่อง + เวลามื้อถัดไป)
//
// ถ้าจอสลับหน้าที่กัน ให้สลับ **เฉพาะค่า ADDRESS** ของสองชุดนี้
// ส่วน COLS/ROWS ไม่ต้องแตะ เพราะมันบอกขนาดของจอที่ทำหน้าที่นั้น ไม่ได้ผูกกับ address

// จอเล็ก 16x2 = นาฬิกา
constexpr uint8_t LCD_TIME_ADDRESS = 0x25;
constexpr uint8_t LCD_TIME_COLS = 16;
constexpr uint8_t LCD_TIME_ROWS = 2;

// จอใหญ่ 20x4 = ตารางยา
constexpr uint8_t LCD_MEDICINE_ADDRESS = 0x27;
constexpr uint8_t LCD_MEDICINE_COLS = 20;
constexpr uint8_t LCD_MEDICINE_ROWS = 4;

// ขนาด buffer ต้องรองรับจอที่กว้างที่สุด
constexpr uint8_t LCD_MAX_COLS = 20;

// ข้อความที่ยาวเกินจอจะเลื่อนวน: ขยับทีละตัวอักษรทุก STEP และหยุดพักที่ต้นข้อความทุก HOLD
constexpr unsigned long LCD_MARQUEE_STEP_MS = 320;
constexpr unsigned long LCD_MARQUEE_HOLD_MS = 1600;
// ความยาวข้อความสูงสุดที่เก็บได้ (ชื่อยาหลายตัวต่อกัน)
constexpr uint8_t LCD_MARQUEE_MAX_TEXT = 80;

// บรรทัดสุดท้ายสลับระหว่างข้อมูลมื้อยากับคำแนะนำปุ่ม ทุกกี่มิลลิวินาที
constexpr unsigned long LCD_HINT_INTERVAL_MS = 2200;
// จำนวนข้อความที่สลับได้บนบรรทัดสุดท้าย
constexpr uint8_t LCD_MAX_HINTS = 4;

// Pin map for a classic ESP32 DevKit / ESP32-WROOM-32.
constexpr uint8_t DISPENSER_COUNT = 3;
constexpr uint8_t SERVO_PINS[DISPENSER_COUNT] = {18, 19, 23};

// Keep motion disabled until calibrated without pills.
constexpr bool ENABLE_SERVO_MOVEMENT = false;

/**
 * โหมดทดสอบระบบทั้งวงจรโดยที่ยังไม่ได้ต่อ Servo
 *
 * เมื่อ ENABLE_SERVO_MOVEMENT เป็น false ปกติการกดปุ่มรับยาจะถูกปฏิเสธและรายงานเป็น FAILED
 * ทำให้ทดสอบเส้นทาง "กดปุ่ม -> บันทึกขึ้น server" ไม่ได้
 *
 * ตั้งค่านี้เป็น true จะถือว่าการกดปุ่มสำเร็จทันทีโดยไม่ขยับอะไร และ**ระบุไว้ในบันทึกว่า
 * เป็นการทดสอบ** (note = "dry run") เพื่อไม่ให้ประวัติการจ่ายยาหลอกว่าเม็ดยาออกมาจริง
 *
 * ต้องตั้งกลับเป็น false เมื่อต่อ Servo แล้ว
 */
constexpr bool DISPENSE_DRY_RUN = true;
constexpr int SERVO_MIN_PULSE_US = 1000;
constexpr int SERVO_MAX_PULSE_US = 2000;
constexpr int REST_PULSE_US[DISPENSER_COUNT] = {1500, 1500, 1500};
constexpr int RELEASE_PULSE_US[DISPENSER_COUNT] = {1750, 1750, 1750};
constexpr unsigned long MOVE_TIME_MS = 700;

// ---------------------------------------------------------------------------
// มอเตอร์สั่น (DRV8833)
// ---------------------------------------------------------------------------
//
// IN1 = ขา PWM คุมความแรงสั่น, IN2 = ขาทิศทาง/เบรก
// DRV8833 หนึ่งบอร์ดมี 2 ช่อง จึงต้องใช้ **สองบอร์ด** สำหรับสามจาน
// ขา SLP/nSLEEP ของทุกบอร์ดต้องเป็น HIGH และ GND ต้องต่อร่วมกับ ESP32
//
// GPIO14 ปล่อยพัลส์สั้นๆ ออกมาตอนบูตตามปกติของชิป มอเตอร์จาน 3 จึงอาจกระตุกหนึ่งครั้ง
// ตอนเปิดเครื่อง ไม่เป็นอันตราย ถ้ารำคาญให้ย้ายไปขาอื่นที่ว่าง
constexpr uint8_t VIB_PWM_PINS[DISPENSER_COUNT] = {13, 26, 4};
constexpr uint8_t VIB_DIR_PINS[DISPENSER_COUNT] = {16, 17, 14};

// ความแรงสั่น 0-255 (ช่วงที่ใช้ได้จริงราว 60-130)
// มอเตอร์สั่นส่วนใหญ่เป็นรุ่น 3V ถ้าจ่ายไฟมอเตอร์ 5V ต้องไม่เกินราว 150
constexpr uint8_t VIB_SPEED = 70;
// กระตุกเต็มกำลังสั้นๆ ให้ตุ้มถ่วงออกตัว แล้วผ่อนลงมาที่ VIB_SPEED
constexpr uint8_t VIB_KICK_SPEED = 255;
constexpr unsigned long VIB_KICK_MS = 40;

// เขย่าอยู่กับที่หลังหมุนกลับถึงตำแหน่งพักนานเท่าไร
// จำเป็นเพราะบางครั้งเม็ดยาหลุดจากจานแล้วแต่ยังค้างอยู่ด้านล่าง การหมุนอย่างเดียวไม่ทำให้มันตก
constexpr unsigned long SHAKE_TIME_MS = 600;

// ---------------------------------------------------------------------------
// เซ็นเซอร์ IR ตรวจเม็ดยาที่ตกลงมา (จานละหนึ่งตัว)
// ---------------------------------------------------------------------------
//
// ตั้ง false ถ้ายังไม่ได้ต่อเซ็นเซอร์ ระบบจะกลับไปหมุนตามจำนวนเม็ดที่สั่งแบบไม่ตรวจสอบ
// และติดป้าย "unverified" ไว้ในบันทึก เพื่อไม่ให้ประวัติหลอกว่ายืนยันเม็ดจริง
constexpr bool ENABLE_PILL_SENSOR = true;

// GPIO34-39 เป็นขาอินพุตอย่างเดียวและ **ไม่มี pull-up ในตัวชิป**
// โมดูล IR ต้องขับสัญญาณเองแบบ push-pull ไม่อย่างนั้นต้องใส่ pull-up ภายนอก 10k
constexpr uint8_t PILL_SENSOR_PINS[DISPENSER_COUNT] = {34, 35, 36};

// โมดูล IR ส่วนใหญ่ให้เอาต์พุต LOW เมื่อลำแสงถูกบัง
constexpr bool PILL_SENSOR_ACTIVE_LOW = true;

// หลังนับหนึ่งเม็ดแล้วไม่รับสัญญาณใหม่นานเท่านี้
// กันเม็ดเดียวที่กระเด้งหรือหมุนตัวผ่านลำแสงถูกนับหลายครั้ง
// ถ้าตั้งยาวเกินไปจะนับตกเมื่อเม็ดตกติดกันเร็วๆ
constexpr unsigned long PILL_DETECT_LOCKOUT_MS = 60;

// วนหมุน-เขย่าได้สูงสุดกี่รอบต่อการจ่ายหนึ่งครั้งก่อนยอมแพ้
// ถ้าครบแล้วยังได้ไม่ครบ จะบันทึกเป็นจ่ายไม่ครบและให้ผู้ใช้กดลองใหม่
constexpr uint8_t MAX_ATTEMPTS_PER_DOSE = 8;

constexpr uint8_t BUZZER_PIN = 25;
// โมดูลปุ่ม 3 ตัว: จ่ายไฟ VCC ด้วย 3.3V เท่านั้น (5V จะทำให้ GPIO เสียหาย)
// ทุกปุ่มเป็น active-LOW คือกดแล้วดึงขาลง GND
constexpr uint8_t DISPENSE_BUTTON_PIN = 33;  // K3 ปุ่มเขียว = รับยา
constexpr uint8_t SNOOZE_BUTTON_PIN = 32;    // K2 ปุ่มเหลือง = เลื่อนไปอีก 5 นาที
constexpr uint8_t CANCEL_BUTTON_PIN = 27;    // K1 ปุ่มแดง  = ข้ามมื้อนี้

// ปุ่มที่กดค้าง 3 วินาที "ตอนเปิดเครื่อง" เพื่อเข้าโหมดตั้งค่า Wi-Fi
//
// ใช้ปุ่มเขียวตัวเดียวกับการรับยา ไม่ชนกันเพราะท่านี้อ่านเฉพาะตอนบูต
// ซึ่งเป็นช่วงที่ยังไม่มีมื้อยาใดกำลังเตือน
// อีกสองปุ่มมีท่าของตัวเองอยู่แล้ว (แดงกดค้าง = หยุดฉุกเฉิน, เหลือง = สั่ง sync ทันที)
constexpr uint8_t CONFIRM_BUTTON_PIN = DISPENSE_BUTTON_PIN;

// ---------------------------------------------------------------------------
// Wi-Fi ที่เครื่องปล่อยเองตอนเข้าโหมดตั้งค่า (กดปุ่มเขียวค้าง 3 วินาทีตอนเปิดเครื่อง)
// ---------------------------------------------------------------------------
//
// true  = ใช้ชื่อและรหัสที่กำหนดไว้ด้านล่าง พิมพ์แปะข้างกล่องได้เลย
// false = คำนวณจาก DEVICE_API_KEY ให้ไม่ซ้ำกันรายเครื่อง (ต้องเปิด Serial ดูชื่อ)
//
// ข้อแลกเปลี่ยนของการกำหนดเอง: ทุกกล่องใช้รหัสเดียวกัน ใครรู้จากกล่องหนึ่งก็เข้าได้ทุกกล่อง
// และคนที่อยู่บน AP เดียวกันดักรหัส Wi-Fi บ้านที่ผู้ใช้กำลังกรอกได้ เพราะหน้า setup เป็น HTTP
// ยังมีรหัสตั้งค่าจากเว็บกั้นอีกชั้น การจับคู่จึงยังทำไม่ได้ถ้าไม่มีรหัสนั้น
constexpr bool SETUP_AP_FIXED_CREDENTIALS = true;

// ต้องไม่เกิน 31 ตัวอักษรตามข้อกำหนดของ SSID
constexpr char SETUP_AP_SSID[] = "SmartPill_Setup";

// WPA2 บังคับ 8-63 ตัวอักษร ถ้าสั้นกว่านี้ softAP จะเปิดไม่ผ่านหรือกลายเป็นเครือข่ายเปิด
constexpr char SETUP_AP_PASSWORD[] = "pillbox1234";

static_assert(sizeof(SETUP_AP_SSID) - 1 >= 1 && sizeof(SETUP_AP_SSID) - 1 <= 31,
              "SETUP_AP_SSID ต้องยาว 1-31 ตัวอักษร");
static_assert(sizeof(SETUP_AP_PASSWORD) - 1 >= 8 && sizeof(SETUP_AP_PASSWORD) - 1 <= 63,
              "SETUP_AP_PASSWORD ต้องยาว 8-63 ตัวอักษรตามข้อกำหนดของ WPA2");

// ---------------------------------------------------------------------------
// การเชื่อมต่อกับ server (ตั้งค่า SERVER_BASE_URL และ DEVICE_API_KEY ใน secrets.h)
// ---------------------------------------------------------------------------

constexpr char FIRMWARE_VERSION[] = "1.1.0";

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

// กดปุ่มเหลืองแล้วเลื่อนการเตือนออกไปกี่นาที
constexpr int SNOOZE_MINUTES = 5;
// เลื่อนได้สูงสุดกี่ครั้งต่อมื้อ
//
// SNOOZE_MINUTES * MAX_SNOOZE_PER_DOSE ต้องน้อยกว่า ALERT_TIMEOUT_MINUTES
// ไม่อย่างนั้นผู้ใช้จะเลื่อนจนเลยเวลาผ่อนผันแล้วกลายเป็นขาดยาโดยไม่ทันรู้ตัว
constexpr uint8_t MAX_SNOOZE_PER_DOSE = 3;

// เพดานจำนวนเม็ดต่อการจ่ายหนึ่งครั้ง ตรงกับขีดจำกัดของ dispenseMedicine()
// ไม่ใช่จำนวนรอบหมุนอีกต่อไป เพราะรอบหมุนถูกกำหนดโดยเซ็นเซอร์ว่าได้ครบหรือยัง
constexpr uint8_t MAX_PILLS_PER_DOSE = 9;

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
