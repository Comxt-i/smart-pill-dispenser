#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr int LOW = 0;
constexpr int HIGH = 1;
extern unsigned long fakeMillis;
extern int cancelLevel;
inline unsigned long millis() { return fakeMillis; }
inline int digitalRead(uint8_t) { return cancelLevel; }
struct FakeSerial {
  void println(const char *) {}

  // schedule_store บันทึกการเปลี่ยนสถานะผ่าน printf; กลืนทิ้งเพื่อไม่ให้ผลทดสอบรก
  void printf(const char *, ...) {}
};
extern FakeSerial Serial;
