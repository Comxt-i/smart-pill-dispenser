// Stub สำหรับตรวจไวยากรณ์บนคอมพิวเตอร์เท่านั้น ไม่ใช่การจำลองพฤติกรรมของบอร์ด
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>

#define PROGMEM

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int OUTPUT = 1;
constexpr int INPUT = 0;
constexpr int INPUT_PULLUP = 2;

unsigned long millis();
void delay(unsigned long ms);
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, int value);

/** พอสำหรับสิ่งที่ firmware ใช้: ต่อสตริง, แปลงตัวเลข, และอ่านค่ากลับเป็น C string */
class String {
 public:
  String() {}
  String(const char *value) : text(value ? value : "") {}
  String(const std::string &value) : text(value) {}
  String(int value) : text(std::to_string(value)) {}
  String(long value) : text(std::to_string(value)) {}
  String(unsigned value) : text(std::to_string(value)) {}
  String(unsigned long value) : text(std::to_string(value)) {}
  String(float value, int = 2) : text(std::to_string(value)) {}
  String(double value, int = 2) : text(std::to_string(value)) {}

  const char *c_str() const { return text.c_str(); }
  int toInt() const { return atoi(text.c_str()); }
  float toFloat() const { return static_cast<float>(atof(text.c_str())); }
  size_t length() const { return text.size(); }

  String &operator+=(const String &other) {
    text += other.text;
    return *this;
  }
  String operator+(const String &other) const { return String(text + other.text); }
  String &operator=(const char *value) {
    text = value ? value : "";
    return *this;
  }

  std::string text;
};

inline String operator+(const char *left, const String &right) { return String(left) + right; }

class Stream {
 public:
  int available() { return 0; }
  int read() { return -1; }
};

class SerialClass : public Stream {
 public:
  void begin(unsigned long) {}
  void println() {}
  void printf(const char *, ...) {}

  // Print ของ Arduino จริงรับได้ทั้ง Printable และชนิดตัวเลข จึงเปิดกว้างไว้แบบเดียวกัน
  template <typename T>
  void print(const T &) {}
  template <typename T>
  void println(const T &) {}
};

extern SerialClass Serial;

class EspClass {
 public:
  uint64_t getEfuseMac() { return 0; }
  uint32_t getFreeHeap() { return 0; }
};

extern EspClass ESP;
