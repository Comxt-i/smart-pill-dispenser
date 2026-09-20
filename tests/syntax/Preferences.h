// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>
#include <vector>

class Preferences {
 public:
  bool begin(const char *, bool) { return true; }
  uint32_t getULong(const char *, uint32_t fallback) { return fallback; }
  void putULong(const char *, uint32_t) {}
  uint8_t getUChar(const char *, uint8_t fallback) { return fallback; }
  void putUChar(const char *, uint8_t) {}
  std::vector<unsigned char> bytes;
  bool writeOk = true;
  size_t getBytesLength(const char *) { return bytes.size(); }
  size_t getBytes(const char *, void *out, size_t size) { if (bytes.size() != size) return 0; memcpy(out, bytes.data(), size); return size; }
  size_t putBytes(const char *, const void *data, size_t size) { if (!writeOk) return 0; const auto *p = static_cast<const unsigned char *>(data); bytes.assign(p, p + size); return size; }
  void remove(const char *) {}
};
