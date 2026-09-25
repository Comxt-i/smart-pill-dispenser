#pragma once
#include <TimeLib.h>
class DS1307RTC {
 public:
  bool readOk = false, writeOk = false;
  tmElements_t value = {};
  int reads = 0, writes = 0;
  bool read(tmElements_t &out) { ++reads; if (readOk) out=value; return readOk; }
  bool write(const tmElements_t &in) { ++writes; if (writeOk) value=in; return writeOk; }
};
extern DS1307RTC RTC;
