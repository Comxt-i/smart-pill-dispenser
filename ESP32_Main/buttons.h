#pragma once

#include <Arduino.h>

enum class ButtonId : uint8_t { Dispense, Snooze, Cancel, Count };

/** ตั้ง pinMode ให้ทุกปุ่มเป็น INPUT_PULLUP (ปุ่มต่อลง GND) */
void buttonsBegin();

/** อ่านและกรองสัญญาณเด้ง ต้องเรียกทุกรอบ loop */
void buttonsUpdate();

/** true หนึ่งครั้งต่อการกดหนึ่งครั้ง (ขอบขาลงที่ผ่าน debounce แล้ว) */
bool buttonPressed(ButtonId id);

/** true ขณะที่ปุ่มยังถูกกดค้างอยู่ */
bool buttonHeld(ButtonId id);

/** true เมื่อกดค้างครบเวลาที่กำหนด แจ้งครั้งเดียวต่อการกดค้างหนึ่งครั้ง */
bool buttonHeldFor(ButtonId id, unsigned long durationMs);
