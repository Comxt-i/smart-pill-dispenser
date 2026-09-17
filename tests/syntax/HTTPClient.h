// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>
#include <WiFi.h>

class HTTPClient {
 public:
  bool begin(const String &) { return true; }
  bool begin(WiFiClient &, const String &) { return true; }
  void setTimeout(uint16_t) {}
  void setConnectTimeout(int32_t) {}
  void addHeader(const char *, const char *) {}
  int GET() { return 200; }
  int POST(const String &) { return 200; }
  Stream &getStream() { return stream; }
  void end() {}

 private:
  Stream stream;
};
