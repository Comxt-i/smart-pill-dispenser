#pragma once
#include <Arduino.h>
struct tmElements_t { uint8_t Second, Minute, Hour, Wday, Day, Month, Year; };
inline int tmYearToCalendar(uint8_t year) { return year + 1970; }
inline time_t makeTime(const tmElements_t &t) {
  tm value = {}; value.tm_sec=t.Second; value.tm_min=t.Minute; value.tm_hour=t.Hour;
  value.tm_mday=t.Day; value.tm_mon=t.Month-1; value.tm_year=t.Year+70;
  return timegm(&value);
}
inline void breakTime(time_t value, tmElements_t &t) {
  tm result = {}; gmtime_r(&value, &result);
  t.Second=result.tm_sec; t.Minute=result.tm_min; t.Hour=result.tm_hour;
  t.Wday=result.tm_wday+1; t.Day=result.tm_mday; t.Month=result.tm_mon+1; t.Year=result.tm_year-70;
}
