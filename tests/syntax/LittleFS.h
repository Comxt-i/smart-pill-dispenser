// LittleFS ปลอมในหน่วยความจำ ใช้ทดสอบบนคอม
// จำลองได้ทั้งเมานต์ไม่ได้ (ไม่มีพาร์ทิชัน) เขียนไม่ครบ (แฟลชเต็ม) และไฟดับก่อนเปลี่ยนชื่อไฟล์
#pragma once

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

struct FakeFlash {
  std::map<std::string, std::vector<uint8_t>> files;
  bool mountOk = true;
  bool writeOk = true;
  bool renameOk = true;  // false = ไฟดับหลังเขียนไฟล์ชั่วคราว ก่อนเปลี่ยนชื่อทับ
  int writes = 0;
};
inline FakeFlash &fakeFlash()
{
  static FakeFlash flash;
  return flash;
}

class File {
 public:
  File() = default;
  File(const std::string &path, bool writing) : path_(path), open_(true), writing_(writing)
  {
    if (writing)
      fakeFlash().files[path].clear();
  }
  explicit operator bool() const { return open_; }
  size_t write(const uint8_t *data, size_t size)
  {
    if (!open_ || !writing_)
      return 0;
    ++fakeFlash().writes;
    // แฟลชเต็ม: เขียนได้แค่ครึ่งเดียว
    const size_t n = fakeFlash().writeOk ? size : size / 2;
    auto &bytes = fakeFlash().files[path_];
    bytes.insert(bytes.end(), data, data + n);
    return n;
  }
  size_t read(uint8_t *out, size_t size)
  {
    const auto &bytes = fakeFlash().files[path_];
    const size_t n = size < bytes.size() - pos_ ? size : bytes.size() - pos_;
    memcpy(out, bytes.data() + pos_, n);
    pos_ += n;
    return n;
  }
  size_t size() const { return fakeFlash().files[path_].size(); }
  void close() { open_ = false; }

 private:
  std::string path_;
  bool open_ = false;
  bool writing_ = false;
  size_t pos_ = 0;
};

class LittleFSFS {
 public:
  bool begin(bool = false) { return fakeFlash().mountOk; }
  bool exists(const char *path) { return fakeFlash().files.count(path) != 0; }
  File open(const char *path, const char *mode)
  {
    const bool writing = mode[0] == 'w';
    if (!writing && !exists(path))
      return File();
    return File(path, writing);
  }
  bool remove(const char *path) { return fakeFlash().files.erase(path) != 0; }
  bool rename(const char *from, const char *to)
  {
    if (!fakeFlash().renameOk || !exists(from))
      return false;
    fakeFlash().files[to] = fakeFlash().files[from];
    fakeFlash().files.erase(from);
    return true;
  }
};
inline LittleFSFS LittleFS;
