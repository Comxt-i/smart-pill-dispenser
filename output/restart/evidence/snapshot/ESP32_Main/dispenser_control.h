#pragma once

#include "config.h"
#include <Arduino.h>

enum class DispenseResult { Started, Invalid, Disabled, Busy, Cancelled, ServoError, SensorBlocked };

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

/**
 * ดับมอเตอร์สั่นทั้งสามตัวทันที ต้องเป็นคำสั่งแรกสุดใน setup()
 *
 * GPIO14 (ขา DIR ของจาน 3) ปล่อยสัญญาณคล็อกออกมาเองตอนบูตตามปกติของชิป
 * กับ DRV8833 นั่นแปลว่า IN2=HIGH ขณะที่ IN1 ยังลอยอยู่ = มอเตอร์วิ่งเต็มกำลัง
 * และจะวิ่งไปเรื่อยๆ จนกว่าจะมีคนเขียนขานั้นให้เป็น LOW
 *
 * แยกออกมาจาก dispenserControlBegin() เพราะตัวนั้นอยู่ท้าย setup()
 * กว่าจะถึงก็ผ่าน Serial, ปุ่มบูต และการสแกน I2C ไปแล้วเป็นวินาที
 */
void dispenserSafePinsEarly();

void dispenserControlBegin();

/**
 * พาทุกจานกลับตำแหน่งพัก (135 องศา กึ่งกลางของ servo 270 องศา) ตอนเปิดเครื่อง
 *
 * ปกติ servo ถูกขับเฉพาะตอนจ่ายยาแล้วปล่อยแรงหลังจบ เพื่อไม่ให้ครางและกินไฟ
 * ผลคือหลังไฟดับหรือหยุดฉุกเฉิน จานค้างอยู่ตำแหน่งไหนก็ตำแหน่งนั้น ไม่ได้เริ่มที่กึ่งกลาง
 *
 * ขยับทีละจาน กระแสกระชากของ servo จะได้ไม่ซ้อนกันสามตัว เรียกใน setup() เท่านั้น (block ราว 2 วินาที)
 */
void dispenserHomeAll();
void dispenserControlUpdate();

/**
 * สั่งจ่ายยา `pills` เม็ดจากจาน `dispenser`
 *
 * จะวนหมุน-เขย่าเป็นรอบๆ จนเซ็นเซอร์นับครบ
 *
 * `preferredHole` คือช่องปล่อยยาตามขนาดที่ผู้ใช้กรอกบนเว็บ (0-3 จากเล็กไปใหญ่)
 * ลองช่องนั้นก่อน ATTEMPTS_PER_HOLE รอบ ไม่ตกก็ไล่ไปช่องที่ใหญ่กว่า (ไม่ย้อนไปช่องที่เล็กกว่า)
 * PILL_HOLE_ANY = ไม่ได้กรอก ไล่จากช่องเล็กสุด
 * ไม่มีเซ็นเซอร์: หมุนหนึ่งรอบต่อเม็ดที่ช่องแรกเท่านั้น ไม่วนหาช่อง
 * ไม่ block — ต้องเรียก dispenserControlUpdate() ทุกลูป
 *
 * หลายจานทำงานพร้อมกันได้ถึง MAX_CONCURRENT_DISPENSERS จาน
 * เกินกว่านั้นจะคืน Busy ผู้เรียกต้องลองใหม่เมื่อ dispenserHasCapacity() เป็น true
 */
DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t pills,
                                int8_t preferredHole = PILL_HOLE_ANY);

/** หยุดทุกจานที่กำลังทำงานอยู่ */
void stopDispenser();

/** true ขณะมีจานใดจานหนึ่งกำลังทำงานอยู่ */
bool dispenserIsBusy();

/** true เมื่อยังรับคำสั่งจ่ายเพิ่มได้อีกอย่างน้อยหนึ่งจาน */
bool dispenserHasCapacity();

/**
 * ดึงผลของรอบที่เพิ่งจบออกมาหนึ่งครั้ง (คืน false ถ้ายังไม่มีผลใหม่)
 * ใช้ให้ loop หลักรู้ว่าจ่ายเสร็จเมื่อไรโดยไม่ต้องรอแบบ blocking
 */
bool takeDispenseOutcome(DispenseOutcome &outcome);

/** อ่านสถานะดิบของเซ็นเซอร์ IR ของจานหนึ่ง สำหรับหน้าเว็บวินิจฉัย */
bool pillSensorBlocked(uint8_t dispenser);
