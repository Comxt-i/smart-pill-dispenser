#pragma once

#include <Arduino.h>

/** รูปแบบเสียงเตือน ทั้งหมดทำงานแบบไม่ block loop (ทำนองอยู่ใน alert.cpp) */
enum class AlertPattern : uint8_t {
  None,      // เงียบ
  Reminder,  // กริ่งโด-มี-ซอล ซ้ำทุก ALERT_REMINDER_PERIOD_MS ระหว่างรอผู้ใช้กดปุ่ม (นานไปแล้วถี่ขึ้น)
  Success,   // สองโน้ตไล่ขึ้นเมื่อจ่ายยาสำเร็จ
  Warning,   // สองโน้ตไล่ลงเมื่อจ่ายไม่สำเร็จหรือถูกปฏิเสธ
  Click,     // ติ๊กสั้นตอบรับการกดปุ่ม
};

void alertBegin();

/** ตั้งรูปแบบเสียงปัจจุบัน เรียกซ้ำด้วยค่าเดิมได้โดยไม่รีสตาร์ทจังหวะ */
void alertSet(AlertPattern pattern);

/** เล่นเสียงสั้นหนึ่งชุดแล้วกลับไปใช้รูปแบบเดิม ใช้ตอบรับการกดปุ่ม */
void alertOneShot(AlertPattern pattern);

void alertUpdate();
