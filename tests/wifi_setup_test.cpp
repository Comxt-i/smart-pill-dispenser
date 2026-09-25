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

  // จับคู่บัญชีไม่ผ่าน แต่ Wi-Fi ต่อติดแล้ว: ต้องจำรหัสไว้
  // ไม่อย่างนั้นรีเซ็ตแล้วลืมทุกครั้งที่รหัสตั้งค่าหมดอายุหรือเซิร์ฟเวอร์ล่ม
  submit("Home", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  claimCode=403; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(!connecting && !completed && wifiSetupActive());
  assert(saved.magic==WIFI_MAGIC && strcmp(saved.ssid,"Home")==0);

  // ตั้ง Wi-Fi ใหม่ทับค่าเดิม แม้จับคู่ไม่ผ่านก็ต้องจำค่าใหม่
  // หน้าตั้งค่าเปิดได้เฉพาะคนที่อยู่หน้าเครื่อง (กดปุ่มค้าง) การกันเขียนทับจึงไม่ได้กันใคร
  // แต่ทำให้ "ตั้ง Wi-Fi เสร็จแล้วไม่จำ"
  submit("NewHome", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  claimCode=404; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(saved.magic==WIFI_MAGIC && strcmp(saved.ssid,"NewHome")==0);

  // จับคู่ไม่ผ่านต้องไม่ขังกล่องไว้ในโหมดตั้งค่า เพราะโหมดนี้ปิดการเตือนและการจ่ายยาทั้งหมด
  // เปิดค้างไว้ให้อ่านผลบนมือถือก่อน แล้วปิดเองและกลับไป sync ต่อ
  const int syncBeforeFailure = requestedSync;
  fakeTime += SETUP_RESULT_GRACE_MS - 1; wifiWebLoop(); assert(wifiSetupActive());
  fakeTime += 2; wifiWebLoop();
  assert(!wifiSetupActive() && requestedSync == syncBeforeFailure + 1);

  // ผู้ใช้กดลองใหม่ระหว่างรอปิด: ห้ามปิดหน้าทับขณะกำลังเชื่อมต่อ
  assert(wifiStartSetup());
  submit("NewHome", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  claimCode=409; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  submit("NewHome", "password", "0123456789ABCDEF0123");
  fakeTime += SETUP_RESULT_GRACE_MS + 1; wifiWebLoop();
  assert(wifiSetupActive() && connecting);
  fakeTime += 30001; wifiWebLoop();  // รอบนี้ต่อไม่ติด
  assert(!connecting && wifiSetupActive());

  // ไม่มีมือถือเปิดหน้าตั้งค่าอยู่ (เช่นเผลอกดปุ่มค้าง) และมี Wi-Fi บันทึกไว้แล้ว
  // ต้องกลับไปทำงานปกติเอง ไม่ใช่เงียบทั้งวัน
  handleSetupStatus();  // มือถือโพลสถานะครั้งสุดท้าย
  fakeTime += SETUP_IDLE_TIMEOUT_MS - 1; wifiWebLoop(); assert(wifiSetupActive());
  fakeTime += 2; wifiWebLoop(); assert(!wifiSetupActive());
  assert(saved.magic==WIFI_MAGIC && strcmp(saved.ssid,"NewHome")==0);

  // เครื่องใหม่ที่ยังไม่มี Wi-Fi ห้ามหมดเวลา เพราะไม่มีอะไรให้กลับไปทำ
  saved={};
  assert(wifiStartSetup());
  handleSetupStatus();
  fakeTime += SETUP_IDLE_TIMEOUT_MS * 2; wifiWebLoop();
  assert(wifiSetupActive());

  submit("Home", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  claimCode=200; storage.writeOk=false; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(!completed && saved.magic==0 && wifiSetupActive());

  submit("Home", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  storage.writeOk=true; WiFi.connectionStatus=WL_CONNECTED; wifiWebLoop();
  assert(completed && saved.magic==WIFI_MAGIC && wifiSetupActive());
  assert(strcmp(saved.ssid,"Home")==0 && setupToken[0]=='\0');
  const int syncBeforeDone = requestedSync;
  fakeTime+=10001; wifiWebLoop(); assert(!wifiSetupActive() && requestedSync == syncBeforeDone + 1);
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
