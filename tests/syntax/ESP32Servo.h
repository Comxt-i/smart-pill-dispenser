// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

class Servo {
 public:
  void setPeriodHertz(int) {}
  void attach(int, int, int) {}
  bool attached() const { return true; }
  void writeMicroseconds(int) {}
  void detach() {}
};
