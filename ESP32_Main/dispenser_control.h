#pragma once

#include <Arduino.h>

enum class DispenseResult { Started, Invalid, Disabled, Busy, Cancelled, ServoError };

/**
 * ผลของการจ่ายยาหนึ่งครั้งที่ "จบแล้ว" ไม่ว่าจะจบครบหรือถูกยกเลิกกลางคัน
 *
 * นับเป็นเม็ด ไม่ใช่รอบหมุน เพราะเซ็นเซอร์ IR ยืนยันเม็ดที่ตกจริง
 * จำนวนรอบที่หมุนไปกี่รอบเป็นเพียงผลพลอยได้ เก็บไว้ดูว่ากลไกฝืดแค่ไหน
 */
struct DispenseOutcome {
  uint8_t dispenser;       // จาน 1..DISPENSER_COUNT
  uint8_t requestedPills;  // จำนวนเม็ดที่สั่ง
  uint8_t dispensedPills;  // จำนวนเม็ดที่ตกจริง (ถ้า sensorVerified เป็น false คือค่าที่อนุมาน)
  uint8_t attempts;        // หมุนไปกี่รอบกว่าจะได้ครบ
  bool cancelled;          // true = ถูกสั่งหยุดก่อนครบ
  bool sensorVerified;     // true = นับจากเซ็นเซอร์ IR จริง
};

void dispenserControlBegin();
void dispenserControlUpdate();

/**
 * สั่งจ่ายยา `pills` เม็ดจากจาน `dispenser`
 *
 * จะวนหมุน-เขย่าเป็นรอบๆ จนเซ็นเซอร์นับครบ หรือจนครบ MAX_ATTEMPTS_PER_DOSE
 * ไม่ block — ต้องเรียก dispenserControlUpdate() ทุกลูป
 */
DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t pills);
void stopDispenser();

/** true ขณะกำลังหมุนจานอยู่ */
bool dispenserIsBusy();

/**
 * ดึงผลของรอบที่เพิ่งจบออกมาหนึ่งครั้ง (คืน false ถ้ายังไม่มีผลใหม่)
 * ใช้ให้ loop หลักรู้ว่าจ่ายเสร็จเมื่อไรโดยไม่ต้องรอแบบ blocking
 */
bool takeDispenseOutcome(DispenseOutcome &outcome);

/** อ่านสถานะดิบของเซ็นเซอร์ IR ของจานหนึ่ง สำหรับหน้าเว็บวินิจฉัย */
bool pillSensorBlocked(uint8_t dispenser);
