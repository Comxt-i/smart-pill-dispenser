// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

class Preferences {
 public:
  bool begin(const char *, bool) { return true; }
  uint32_t getULong(const char *, uint32_t fallback) { return fallback; }
  void putULong(const char *, uint32_t) {}
  uint8_t getUChar(const char *, uint8_t fallback) { return fallback; }
  void putUChar(const char *, uint8_t) {}
  size_t getBytesLength(const char *) { return 0; }
  size_t getBytes(const char *, void *, size_t) { return 0; }
  void putBytes(const char *, const void *, size_t) {}
  void remove(const char *) {}
};
