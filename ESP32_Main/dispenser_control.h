#pragma once

#include <Arduino.h>

enum class DispenseResult { Started, Invalid, Disabled, Busy, Cancelled, ServoError };

/** ผลของการจ่ายยาหนึ่งครั้งที่ "จบแล้ว" ไม่ว่าจะจบครบหรือถูกยกเลิกกลางคัน */
struct DispenseOutcome {
  uint8_t dispenser;        // จาน 1..DISPENSER_COUNT
  uint8_t requestedCycles;  // จำนวนรอบที่สั่ง
  uint8_t completedCycles;  // จำนวนรอบที่หมุนจบจริง
  bool cancelled;           // true = ถูกสั่งหยุดก่อนครบรอบ
};

void dispenserControlBegin();
void dispenserControlUpdate();
DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t amount);
void stopDispenser();

/** true ขณะกำลังหมุนจานอยู่ */
bool dispenserIsBusy();

/**
 * ดึงผลของรอบที่เพิ่งจบออกมาหนึ่งครั้ง (คืน false ถ้ายังไม่มีผลใหม่)
 * ใช้ให้ loop หลักรู้ว่าจ่ายเสร็จเมื่อไรโดยไม่ต้องรอแบบ blocking
 */
bool takeDispenseOutcome(DispenseOutcome &outcome);
