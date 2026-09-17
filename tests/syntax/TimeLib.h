// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

typedef struct {
  uint8_t Second;
  uint8_t Minute;
  uint8_t Hour;
  uint8_t Wday;
  uint8_t Day;
  uint8_t Month;
  uint8_t Year;  // offset จากปี 1970
} tmElements_t;

inline int tmYearToCalendar(uint8_t year) { return year + 1970; }
inline time_t makeTime(const tmElements_t &) { return 0; }
inline void breakTime(time_t, tmElements_t &) {}
