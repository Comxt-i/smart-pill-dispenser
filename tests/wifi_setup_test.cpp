#include <cassert>
#include "../ESP32_Main/wifi_web.cpp"
unsigned long fakeTime = 0;
unsigned long millis() { return fakeTime; }
int digitalRead(uint8_t) { return HIGH; }
void delay(unsigned long) {}
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
  return 0;
}
