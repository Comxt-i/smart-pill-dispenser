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
uint32_t fakeClock = 0;  // 0 = กล่องยังไม่มีเวลา
bool rtcIsValid() { return fakeClock != 0; }
uint32_t rtcLocalEpoch() { return fakeClock; }
void rtcSyncFromEpoch(uint32_t epoch) { if (SoftwareClock::validEpoch(epoch)) fakeClock = epoch; }
void postTime(const char *value, const char *withNonce = nullptr, bool automatic = false) {
  server.args["nonce"] = withNonce ? withNonce : nonce;
  server.args["local_epoch"] = value;
  server.args["auto"] = automatic ? "1" : "";
  handleSetTime();
}

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

  // ---- ตั้งเวลาตามมือถือจากหน้าตั้งค่า (ไม่ต้องมีเน็ต ไม่ต้องต่อคอม) ----
  handleSetupStatus();
  assert(server.lastCode == 200);
  const int syncBeforeTime = requestedSync;
  postTime("1790449393", "stale-nonce"); assert(server.lastCode == 403 && fakeClock == 0);
  postTime(""); assert(server.lastCode == 400);
  postTime("12abc"); assert(server.lastCode == 400);
  postTime("-5"); assert(server.lastCode == 400 && fakeClock == 0);
  postTime("946684800"); assert(server.lastCode == 400 && fakeClock == 0);  // ปี 2000: มือถือตั้งเวลาผิด
  postTime("99999999999"); assert(server.lastCode == 400 && fakeClock == 0);
  postTime("1790449393"); assert(server.lastCode == 200 && fakeClock == 1790449393);
  assert(requestedSync == syncBeforeTime + 1);
  // มี Wi-Fi บันทึกไว้แล้ว: ตั้งเวลาเสร็จกลับไปแสดงตารางยาเอง แม้มือถือยังเปิดหน้าค้างไว้ (โพลอยู่)
  assert(saved.magic == WIFI_MAGIC && wifiSetupActive());
  fakeTime += SETUP_RESULT_GRACE_MS / 2; handleSetupStatus(); wifiWebLoop(); assert(wifiSetupActive());
  fakeTime += SETUP_RESULT_GRACE_MS / 2 + 1; handleSetupStatus(); wifiWebLoop(); assert(!wifiSetupActive());
  // หน้าเว็บตั้งเวลาให้เองตอนเปิดหน้า: ต้องไม่ปิดโหมดตั้งค่า ผู้ใช้อาจเปิดมาเพื่อตั้ง Wi-Fi
  assert(wifiStartSetup());
  postTime("1790449450", nullptr, true); assert(server.lastCode == 200 && fakeClock == 1790449450);
  fakeTime += SETUP_RESULT_GRACE_MS + 1; wifiWebLoop(); assert(wifiSetupActive());
  closeSetup();
  // ตั้งเวลาแล้วต่อด้วยตั้ง Wi-Fi ใหม่: ห้ามปิดทับระหว่างกรอก
  assert(wifiStartSetup());
  postTime("1790449400"); assert(server.lastCode == 200);
  submit("Other", "password", "0123456789ABCDEF0123"); wifiWebLoop();
  fakeTime += SETUP_RESULT_GRACE_MS + 1; wifiWebLoop(); assert(wifiSetupActive());
  startPending = connecting = false;
  // ยังไม่มี Wi-Fi บันทึกไว้: ผู้ใช้ยังต้องตั้ง Wi-Fi ต่อ ห้ามปิดเอง
  const auto keepSaved = saved; saved = {};
  postTime("1790449500"); assert(server.lastCode == 200);
  fakeTime += SETUP_RESULT_GRACE_MS + 1; wifiWebLoop(); assert(wifiSetupActive());
  saved = keepSaved;
  // นอกโหมดตั้งค่า (เช่นคนในเครือข่ายบ้าน) เปลี่ยนเวลาไม่ได้ เพราะเวลาคุมการจ่ายยา
  closeSetup(); fakeClock = 0;
  postTime("1790449393"); assert(server.lastCode == 403 && fakeClock == 0);

  // ---- ปุ่ม "เสร็จแล้ว": กลับไปทำงานปกติเลย ไม่ต้องรอหมดเวลา ----
  assert(wifiStartSetup());
  server.args["nonce"] = "stale"; handleClose(); assert(server.lastCode == 403);
  submit("Home", "password", "0123456789ABCDEF0123");
  server.args["nonce"] = nonce; handleClose(); assert(server.lastCode == 409);  // กำลังเชื่อม Wi-Fi อยู่
  startPending = connecting = false;
  handleClose(); assert(server.lastCode == 200 && wifiSetupActive());  // ให้คำตอบถึงมือถือก่อน
  const int syncBeforeClose = requestedSync;
  fakeTime += 1501; wifiWebLoop();
  assert(!wifiSetupActive() && requestedSync == syncBeforeClose + 1);
  return 0;
}
