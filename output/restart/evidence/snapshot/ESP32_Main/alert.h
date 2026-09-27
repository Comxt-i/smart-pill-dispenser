#pragma once

#include <Arduino.h>

/** รูปแบบเสียงเตือน ทั้งหมดทำงานแบบไม่ block loop */
enum class AlertPattern : uint8_t {
  None,      // เงียบ
  Reminder,  // ปี๊บสองครั้งซ้ำทุก ALERT_BEEP_PERIOD_MS ระหว่างรอผู้ใช้กดปุ่ม
  Success,   // ปี๊บยาวหนึ่งครั้งเมื่อจ่ายยาสำเร็จ
  Warning,   // ปี๊บสั้นสามครั้งเมื่อจ่ายไม่สำเร็จหรือถูกปฏิเสธ
  Click,     // ตอบรับการกดปุ่ม
};

void alertBegin();

/** ตั้งรูปแบบเสียงปัจจุบัน เรียกซ้ำด้วยค่าเดิมได้โดยไม่รีสตาร์ทจังหวะ */
void alertSet(AlertPattern pattern);

/** เล่นเสียงสั้นหนึ่งชุดแล้วกลับไปใช้รูปแบบเดิม ใช้ตอบรับการกดปุ่ม */
void alertOneShot(AlertPattern pattern);

void alertUpdate();
