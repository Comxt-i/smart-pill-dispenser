#pragma once

#include <cstdint>

constexpr int LOW = 0;
constexpr int HIGH = 1;
extern unsigned long fakeMillis;
extern int cancelLevel;
inline unsigned long millis() { return fakeMillis; }
inline int digitalRead(uint8_t) { return cancelLevel; }
struct FakeSerial {
  void println(const char *) {}
};
extern FakeSerial Serial;
