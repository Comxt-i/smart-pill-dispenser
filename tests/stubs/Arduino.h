#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT = 0;
constexpr int OUTPUT = 1;
constexpr int INPUT_PULLUP = 2;

extern unsigned long fakeMillis;
inline unsigned long millis() { return fakeMillis; }
inline void delay(unsigned long ms) { fakeMillis += ms; }

/**
 * ระดับสัญญาณของแต่ละขา ตั้งค่าได้รายขาจากฝั่งเทสต์
 *
 * ต้องแยกรายขาจริงๆ เพราะตัวคุมอ่านทั้งปุ่ม Cancel และเซ็นเซอร์ IR ผ่าน digitalRead
 * ถ้าคืนค่าเดียวกันทุกขาเหมือนสตับรุ่นก่อน จะแยกสองเรื่องนี้ออกจากกันไม่ได้
 *
 * ค่าเริ่มต้นต้องเป็น HIGH ทุกขา = ปุ่มไม่ถูกกด และลำแสงไม่ถูกบัง
 */
extern int pinLevel[64];
inline int digitalRead(uint8_t pin) { return pinLevel[pin]; }

struct PinWrite { uint8_t pin; int value; };
extern std::vector<PinWrite> digitalWrites;
extern std::vector<PinWrite> analogWrites;

inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t pin, int value) { digitalWrites.push_back({pin, value}); }
inline void analogWrite(uint8_t pin, int value) { analogWrites.push_back({pin, value}); }

/** ตั้งทุกขาเป็น HIGH ให้เรียกก่อน dispenserControlBegin() */
inline void resetPins()
{
  for (auto &level : pinLevel)
    level = HIGH;
  digitalWrites.clear();
  analogWrites.clear();
}

struct FakeSerial {
  void println(const char *) {}

  // schedule_store บันทึกการเปลี่ยนสถานะผ่าน printf; กลืนทิ้งเพื่อไม่ให้ผลทดสอบรก
  void printf(const char *, ...) {}
};
extern FakeSerial Serial;
