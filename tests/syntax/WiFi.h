// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

constexpr int WL_CONNECTED = 3;
constexpr int WIFI_STA = 1;

class IPAddress {
 public:
  String toString() const { return String("0.0.0.0"); }
};

class WiFiClient {
 public:
  virtual ~WiFiClient() {}
};

class WiFiClass {
 public:
  void mode(int) {}
  void begin(const char *, const char *) {}
  void disconnect() {}
  void setAutoReconnect(bool) {}
  int status() { return WL_CONNECTED; }
  IPAddress localIP() { return IPAddress(); }
  int RSSI() { return -50; }
  String macAddress() { return String("00:00:00:00:00:00"); }
};

extern WiFiClass WiFi;

inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
