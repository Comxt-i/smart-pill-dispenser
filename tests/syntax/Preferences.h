// NVS ปลอมสำหรับทดสอบบนคอม
//
// blob: หนึ่งก้อนต่อหนึ่ง instance (`bytes`) เพราะโค้ดจริงใช้ blob key เดียวต่อ namespace
//       เทสต์เข้าถึง `bytes` ตรงๆ เพื่อจำลองข้อมูลในแฟลชเสียหาย
// ค่าเดี่ยว (uchar/ulong): เก็บจริงตาม key เพื่อให้จำลองการรีบูต (เรียก begin ซ้ำ) ได้ถูกต้อง
#pragma once

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

class Preferences {
 public:
  bool begin(const char *, bool) { return true; }
  uint32_t getULong(const char *key, uint32_t fallback)
  {
    auto it = scalars.find(key);
    return it == scalars.end() ? fallback : it->second;
  }
  void putULong(const char *key, uint32_t value) { scalars[key] = value; }
  uint8_t getUChar(const char *key, uint8_t fallback)
  {
    auto it = scalars.find(key);
    return it == scalars.end() ? fallback : static_cast<uint8_t>(it->second);
  }
  void putUChar(const char *key, uint8_t value) { scalars[key] = value; }
  std::vector<unsigned char> bytes;
  std::map<std::string, uint32_t> scalars;
  bool writeOk = true;
  size_t getBytesLength(const char *) { return bytes.size(); }
  size_t getBytes(const char *, void *out, size_t size) { if (bytes.size() != size) return 0; memcpy(out, bytes.data(), size); return size; }
  size_t putBytes(const char *, const void *data, size_t size) { if (!writeOk) return 0; const auto *p = static_cast<const unsigned char *>(data); bytes.assign(p, p + size); return size; }
  void remove(const char *key)
  {
    // ค่าเดี่ยวลบตาม key ส่วน key อื่นคือ blob ก้อนเดียวของ namespace นี้
    if (scalars.erase(key) == 0)
      bytes.clear();
  }
};
