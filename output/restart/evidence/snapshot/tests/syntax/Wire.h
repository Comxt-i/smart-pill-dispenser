// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

class TwoWire {
 public:
  void begin(uint8_t, uint8_t) {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission() { return 0; }
  size_t write(uint8_t) { return 1; }
  uint8_t requestFrom(int, int) { return 1; }
  int read() { return 0; }
  int available() { return 1; }
};

extern TwoWire Wire;
