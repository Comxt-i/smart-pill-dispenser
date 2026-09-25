#pragma once

#include <Arduino.h>

/** ดวงไฟบนโมดูล เรียงตามตำแหน่งจริงบนบอร์ดจากซ้ายไปขวา */
enum class LedColor : uint8_t { Red = 0, Yellow = 1, Green = 2 };

/** รูปแบบไฟที่บอกสถานะของเครื่อง */
enum class LedPattern : uint8_t {
  Off,            // ว่าง ไม่มีอะไรเกิดขึ้น
  AlertBlink,     // ถึงเวลากินยา กระพริบพร้อมกันทุกดวง
  DispenseChase,  // กำลังจ่ายยา วิ่งไล่จากแดงไปเขียว
};

void statusLedBegin();

/**
 * ตั้งรูปแบบพื้นหลังที่สะท้อนสถานะของเครื่อง
 *
 * เรียกซ้ำด้วยค่าเดิมได้ทุกลูป จังหวะกระพริบจะไม่ถูกรีเซ็ต
 */
void statusLedSet(LedPattern pattern);

/**
 * โชว์สีเดียวค้างไว้ชั่วคราวเพื่อตอบรับการกดปุ่ม แล้วกลับไปรูปแบบพื้นหลังเอง
 *
 * ทับรูปแบบพื้นหลังชั่วคราว ไม่ได้ลบทิ้ง จึงไม่ต้องตั้งกลับหลังจากนี้
 */
void statusLedFlash(LedColor color);

/** ขยับจังหวะไฟ ต้องเรียกทุกรอบ loop และห้าม block */
void statusLedUpdate();
