#include <cassert>
#include "../ESP32_Main/wifi_web.cpp"
unsigned long millis() { return 0; }
int digitalRead(uint8_t) { return HIGH; }
void delay(unsigned long) {}
SerialClass Serial;
EspClass ESP;
WiFiClass WiFi;
void appStatusJson(String &) {}
int netSyncCompleteSetup(const char *) { return 200; }
void netSyncRequestNow() {}

int main() {
  wifiWebBegin();
  assert(!wifiSetupActive() && WiFi.beginCount == 1);
  assert(saved.magic == WIFI_MAGIC && strcmp(saved.ssid, WIFI_SSID) == 0);
  assert(strcmp(saved.password, WIFI_PASSWORD) == 0);
  assert(storage.getBytesLength("config") == sizeof(saved));

  // A later network selected through the portal survives reboot.
  strcpy(saved.ssid, "OtherNetwork");
  strcpy(saved.password, "other-password");
  storage.putBytes("config", &saved, sizeof(saved));
  saved = {};
  wifiWebBegin();
  assert(strcmp(saved.ssid, "OtherNetwork") == 0);
  assert(!wifiSetupActive() && WiFi.beginCount == 2);

  // Failed persistence leaves setup available instead of pretending it saved.
  saved = {};
  storage.bytes.clear();
  storage.writeOk = false;
  wifiWebBegin();
  assert(saved.magic == 0 && wifiSetupActive());
  assert(WiFi.beginCount == 2);
}
