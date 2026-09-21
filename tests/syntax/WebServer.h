// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <map>
constexpr int HTTP_GET = 0, HTTP_POST = 1;

class WebServer {
 public:
  explicit WebServer(int) {}
  void on(const char *, void (*)()) {}
  void on(const char *, int, void (*)()) {}
  void onNotFound(void (*)()) {}
  void sendHeader(const char *, const char *) {}
  WiFiClient client() { return WiFiClient(); }
  void begin() {}
  void handleClient() {}
  int lastCode = 0;
  std::map<std::string, String> args;
  void send(int code, const char *, const String &) { lastCode = code; }
  String arg(const char *key) { return args[key]; }
};
