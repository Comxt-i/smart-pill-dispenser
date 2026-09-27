// I2C ปลอมสำหรับเทสต์ตัวคุมจาน: จำไบต์ล่าสุดที่ส่งไปแต่ละ address พร้อมเวลา
// ใช้จำลอง PCF8574 ที่คุม relay เลเซอร์
#pragma once

#include "Arduino.h"

class TwoWire {
 public:
  void begin(uint8_t, uint8_t) {}
  void beginTransmission(uint8_t address) { current = address; hasByte = false; }
  size_t write(uint8_t value) { pending = value; hasByte = true; return 1; }
  uint8_t endTransmission()
  {
    if (!present)
      return 2;  // NACK = ไม่มีชิปตอบ
    if (hasByte)
    {
      lastValue[current] = value(pending);
      lastAt[current] = fakeMillis;
      ++writes;
    }
    return 0;
  }

  bool present = true;
  int lastValue[128];          // -1 = ยังไม่เคยเขียน
  unsigned long lastAt[128] = {};
  int writes = 0;

  TwoWire() { for (int &v : lastValue) v = -1; }

 private:
  static int value(uint8_t v) { return v; }
  uint8_t current = 0;
  uint8_t pending = 0;
  bool hasByte = false;
};

extern TwoWire Wire;
