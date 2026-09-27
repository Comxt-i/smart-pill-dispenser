// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>

constexpr int WL_CONNECTED = 3;
constexpr int WL_NO_SSID_AVAIL = 1;
constexpr int WL_CONNECT_FAILED = 4;
constexpr int WL_DISCONNECTED = 6;
constexpr int WIFI_STA = 1;
constexpr int WIFI_AUTH_OPEN = 0;
constexpr int WIFI_AP_STA = 3;
constexpr int WIFI_SCAN_RUNNING = -1;

class IPAddress {
 public:
  IPAddress() {}
  IPAddress(int, int, int, int) {}
  bool operator==(const IPAddress &) const { return true; }
  String toString() const { return String("0.0.0.0"); }
};

class WiFiClient {
 public:
  virtual ~WiFiClient() {}
  IPAddress localIP() { return IPAddress(); }
};

class WiFiClass {
 public:
  void mode(int) {}
  void begin(const char *, const char *) { connectionStatus = 0; ++beginCount; }
  void disconnect() {}
  void setAutoReconnect(bool) {}
  int connectionStatus = WL_CONNECTED;
  int beginCount = 0;
  int status() { return connectionStatus; }
  IPAddress localIP() { return IPAddress(); }
  bool softAP(const char *, const char *) { return true; }
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
  bool softAPdisconnect(bool) { return true; }
  IPAddress softAPIP() { return IPAddress(); }
  int scanComplete() { return 0; }
  int scanNetworks(bool) { return 0; }
  void scanDelete() {}
  String SSID(int) { return String(""); }
  int RSSI() { return -50; }
  String macAddress() { return String("00:00:00:00:00:00"); }
  int scanNetworks() { return 0; }
  int RSSI(int) { return -50; }
  int channel(int) { return 1; }
  int encryptionType(int) { return WIFI_AUTH_OPEN; }
};

extern WiFiClass WiFi;

inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
