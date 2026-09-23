#include <cassert>
#include "../ESP32_Main/wifi_web.cpp"
unsigned long fakeTime = 0;
unsigned long millis() { return fakeTime; }
unsigned long releaseAt = 0;
int digitalRead(uint8_t) { return fakeTime < releaseAt ? LOW : HIGH; }
void delay(unsigned long ms) { fakeTime += ms; }
void pinMode(uint8_t, uint8_t) {}
SerialClass Serial;
EspClass ESP;
WiFiClass WiFi;
int claimCode = 200, requestedSync = 0;
void appStatusJson(String &) {}
int netSyncCompleteSetup(const char *) { return claimCode; }
void netSyncRequestNow() { ++requestedSync; }

void submit(const char *ssid, const char *password, const char *token) {
  server.args["nonce"] = nonce;
  server.args["ssid"] = ssid;
  server.args["password"] = password;
  server.args["token"] = token;
  handleConnect();
}
int main() {
  char normalized[21];
  assert(normalizeSetupToken("01234567-89ab-cdef-0123", normalized));
  assert(strcmp(normalized,"0123456789ABCDEF0123")==0);
  assert(!normalizeSetupToken("short",normalized));
  assert(!normalizeSetupToken("0123456789ABCDEG01234",normalized));
  assert(validSetupWifi("OpenNetwork", ""));
  assert(!validSetupWifi("", "password"));
  assert(!validSetupWifi("Home", "short"));

  wifiWebBegin();
  assert(wifiSetupActive());
  submit("Home", "short", "0123456789ABCDEF0123"); assert(server.lastCode==400);
  server.args["nonce"]="wrong"; handleConnect(); assert(server.lastCode==403);
  submit("Home", "password", "0123456789ABCDEF0123"); assert(server.lastCode==202);
  wifiWebLoop(); assert(connecting && WiFi.beginCount==1);
  fakeTime+=30001; wifiWebLoop(); assert(!connecting && wifiSetupActive() && saved.magic==0);

  submit("Home", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  claimCode=403; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(!connecting && !completed && saved.magic==0 && wifiSetupActive());

  submit("Home", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  claimCode=200; storage.writeOk=false; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(!completed && saved.magic==0 && wifiSetupActive());

  submit("Home", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  storage.writeOk=true; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(completed && saved.magic==WIFI_MAGIC && wifiSetupActive());
  assert(strcmp(saved.ssid,"Home")==0 && setupToken[0]=='\0');
  fakeTime+=10001; wifiWebLoop(); assert(!wifiSetupActive() && requestedSync==1);
  saved={}; wifiWebBegin(); assert(saved.magic==WIFI_MAGIC && !wifiSetupActive());
  // Boot gesture is captured before peripheral initialization and stays latched.
  releaseAt = fakeTime + 4000;
  wifiCheckSetupButtonAtBoot();
  assert(bootSetupRequested);
  fakeTime += 15000;
  wifiWebBegin();
  assert(wifiSetupActive());
  setupActive = false;
  releaseAt = fakeTime + 1000;
  wifiCheckSetupButtonAtBoot();
  assert(!bootSetupRequested);
  wifiWebBegin();
  assert(!wifiSetupActive());
  releaseAt = 0;
  wifiCheckSetupButtonAtBoot();
  assert(!bootSetupRequested);
  // Runtime entry also works with saved Wi-Fi and no reboot gesture.
  const auto previousSettings = storage.bytes;
  assert(wifiStartSetup() && wifiSetupActive());
  assert(storage.bytes == previousSettings);
  assert(wifiStartSetup());
  assert(storage.bytes == previousSettings);
  return 0;
}
