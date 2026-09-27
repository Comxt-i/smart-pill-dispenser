#pragma once

#include <Arduino.h>

#include "config.h"

/**
 * ข้อความเลื่อนวนบนจอ LCD กว้างคงที่ แบบไม่ block loop
 *
 * ข้อความที่สั้นกว่าความกว้างจอจะอยู่นิ่ง ไม่เลื่อนโดยไม่จำเป็น
 * ข้อความที่ยาวกว่าจะเลื่อนวนโดยมีช่องว่างคั่นหัวท้าย และหยุดพักตอนเริ่มต้นรอบ
 * เพื่อให้ผู้ใช้อ่านต้นข้อความทัน
 */
struct Marquee {
  char text[LCD_MARQUEE_MAX_TEXT];
  uint8_t length;
  uint8_t width;            // ความกว้างของจอที่ข้อความนี้จะไปแสดง
  uint8_t offset;           // ตำแหน่งเริ่มต้นของหน้าต่างที่กำลังแสดง
  unsigned long lastStepMs;
  bool timerStarted;        // เริ่มจับเวลาแล้วหรือยัง (ห้ามใช้ lastStepMs==0 แทน เพราะเวลา 0 ก็ถูกต้อง)
  bool holdingAtStart;      // กำลังหยุดพักที่ต้นข้อความ
};

/**
 * ตั้งข้อความและความกว้างของจอ
 * ถ้าเป็นข้อความเดิมและความกว้างเดิมจะไม่รีเซ็ตตำแหน่งเลื่อน
 */
void marqueeSet(Marquee &marquee, const char *text, uint8_t width);

/** true เมื่อข้อความยาวเกินจอจนต้องเลื่อน */
bool marqueeScrolls(const Marquee &marquee);

/**
 * เขียนหน้าต่างที่ต้องแสดง ณ เวลานี้ลง out (ต้องมีที่อย่างน้อย width + 1)
 * เรียกถี่แค่ไหนก็ได้ จังหวะการเลื่อนคุมด้วย nowMs ภายใน
 */
void marqueeRender(Marquee &marquee, unsigned long nowMs, char *out);
