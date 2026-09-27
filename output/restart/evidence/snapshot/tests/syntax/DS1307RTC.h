// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <TimeLib.h>

class DS1307RTC {
 public:
  bool read(tmElements_t &) { return true; }
  bool write(const tmElements_t &) { return true; }
};

extern DS1307RTC RTC;
