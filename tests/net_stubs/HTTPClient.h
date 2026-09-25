// HTTP ปลอมที่ตั้งผลล่วงหน้าได้ และจดทุก request ไว้ตรวจ (ใช้กับ net_async_test เท่านั้น)
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <string>
#include <vector>

struct FakeHttp {
  int nextCode = 200;
  std::string nextBody = "{}";
  int requests = 0;
  std::vector<std::string> urls;
  std::vector<std::string> posted;
};
extern FakeHttp fakeHttp;

class HTTPClient {
 public:
  bool begin(WiFiClient &, const String &url) { url_ = url.c_str(); return true; }
  bool begin(const String &url) { url_ = url.c_str(); return true; }
  void setTimeout(uint16_t) {}
  void setConnectTimeout(int32_t) {}
  void addHeader(const char *, const char *) {}
  int GET() { ++fakeHttp.requests; fakeHttp.urls.push_back(url_); return fakeHttp.nextCode; }
  int POST(const String &body)
  {
    ++fakeHttp.requests;
    fakeHttp.urls.push_back(url_);
    fakeHttp.posted.push_back(body.c_str());
    return fakeHttp.nextCode;
  }
  String getString() { return String(fakeHttp.nextBody.c_str()); }
  void end() {}

 private:
  std::string url_;
};
