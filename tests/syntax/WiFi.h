// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

constexpr int WL_CONNECTED = 3;
constexpr int WL_NO_SSID_AVAIL = 1;
constexpr int WL_CONNECT_FAILED = 4;
constexpr int WL_DISCONNECTED = 6;
constexpr int WIFI_STA = 1;
constexpr int WIFI_AUTH_OPEN = 0;

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
  int scanNetworks() { return 0; }
  void scanDelete() {}
  String SSID(int) { return String(""); }
  int RSSI(int) { return -50; }
  int channel(int) { return 1; }
  int encryptionType(int) { return WIFI_AUTH_OPEN; }
};

extern WiFiClass WiFi;

inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
