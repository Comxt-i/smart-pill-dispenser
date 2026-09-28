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
// ไมโครวินาทีที่เดินเพิ่มจาก fakeMillis ใช้จำลองเม็ดยาที่บังลำแสงแค่ไม่กี่มิลลิวินาที
extern unsigned long fakeExtraUs;
inline unsigned long millis() { return fakeMillis; }
inline unsigned long micros() { return fakeMillis * 1000UL + fakeExtraUs; }
inline void delay(unsigned long ms) { fakeMillis += ms; }
inline void delayMicroseconds(unsigned int us) { fakeExtraUs += us; }

#define IRAM_ATTR
constexpr int CHANGE = 3;
inline uint8_t digitalPinToInterrupt(uint8_t pin) { return pin; }

// interrupt ที่ตัวคุมผูกไว้ต่อขา (nullptr = ไม่ได้ผูก)
extern void (*pinIsr[64])(void *);
extern void *pinIsrArg[64];
inline void attachInterruptArg(uint8_t pin, void (*handler)(void *), void *arg, int)
{
  pinIsr[pin] = handler;
  pinIsrArg[pin] = arg;
}

/**
 * ระดับสัญญาณของแต่ละขา ตั้งค่าได้รายขาจากฝั่งเทสต์
 *
 * ต้องแยกรายขาจริงๆ เพราะตัวคุมอ่านทั้งปุ่ม Cancel และเซ็นเซอร์ IR ผ่าน digitalRead
 * ถ้าคืนค่าเดียวกันทุกขาเหมือนสตับรุ่นก่อน จะแยกสองเรื่องนี้ออกจากกันไม่ได้
 *
 * ค่าเริ่มต้นต้องเป็น HIGH ทุกขา = ปุ่มไม่ถูกกด และลำแสงไม่ถูกบัง
 */
extern int pinLevel[64];

// ระดับที่ถูกเขียนล่าสุดของแต่ละขาและเวลาที่เขียน (-1 = ยังไม่เคยเขียน)
// แยกจาก digitalWrites เพราะเทสต์ล้าง log นั้นระหว่างทาง แต่สถานะขาจริงไม่ได้หายไปด้วย
extern int writtenLevel[64];
extern unsigned long writtenAt[64];

// ให้เทสต์จำลองฮาร์ดแวร์ที่ค่าอ่านขึ้นกับขาอื่นได้ เช่นตัวรับเลเซอร์ที่มืดเมื่อเลเซอร์ดับ
extern int (*digitalReadHook)(uint8_t pin);
inline int digitalRead(uint8_t pin) { return digitalReadHook ? digitalReadHook(pin) : pinLevel[pin]; }

struct PinWrite { uint8_t pin; int value; };
extern std::vector<PinWrite> digitalWrites;
extern std::vector<PinWrite> analogWrites;

inline void pinMode(uint8_t, int) {}

/** เปลี่ยนระดับขาอินพุตแบบฮาร์ดแวร์จริง: ระดับเปลี่ยนแล้ว interrupt ที่ผูกไว้ทำงานทันที */
inline void driveInput(uint8_t pin, int level)
{
  if (pinLevel[pin] == level)
    return;
  pinLevel[pin] = level;
  if (pinIsr[pin])
    pinIsr[pin](pinIsrArg[pin]);
}
inline void digitalWrite(uint8_t pin, int value)
{
  digitalWrites.push_back({pin, value});
  writtenLevel[pin] = value;
  writtenAt[pin] = fakeMillis;
}
// analogWrite ห้ามใช้กับมอเตอร์สั่นแล้ว (แย่งช่อง PWM ของ servo) นับไว้ให้เทสต์ยืนยันว่าเป็นศูนย์
extern int analogWriteCalls;
inline void analogWrite(uint8_t pin, int value) { ++analogWriteCalls; analogWrites.push_back({pin, value}); }
// ช่อง LEDC ที่ผูกกับแต่ละขา (-1 = ยังไม่ผูก)
extern int ledcChannelOf[64];
inline bool ledcAttachChannel(uint8_t pin, uint32_t, uint8_t, uint8_t channel)
{
  ledcChannelOf[pin] = channel;
  return true;
}
inline bool ledcWrite(uint8_t pin, uint32_t duty)
{
  analogWrites.push_back({pin, static_cast<int>(duty)});
  return true;
}

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
