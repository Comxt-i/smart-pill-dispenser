// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

class TwoWire {
 public:
  void begin(uint8_t, uint8_t) {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t address) { current = address; }
  uint8_t endTransmission() { return 0; }
  size_t write(uint8_t value) { if (current < 128) lastValue[current] = value; return 1; }
  uint8_t requestFrom(int, int) { return 1; }
  int read() { return 0; }
  int available() { return 1; }

  // ไบต์ล่าสุดที่ส่งไปแต่ละ address (-1 = ยังไม่เคยส่ง) ให้เทสต์จำลองชิปที่ถูกสั่ง เช่น relay เลเซอร์
  int lastValue[128];
  TwoWire() { for (int &v : lastValue) v = -1; }

 private:
  uint8_t current = 0;
};

extern TwoWire Wire;
