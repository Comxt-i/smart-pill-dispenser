// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <WiFi.h>

class WiFiClientSecure : public WiFiClient {
 public:
  void setCACert(const char *) {}
  void setInsecure() {}
  void setTimeout(uint32_t) {}
};
