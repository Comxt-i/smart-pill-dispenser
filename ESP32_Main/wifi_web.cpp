#include "wifi_web.h"
#include "config.h"
#include "pill_app.h"
#include "net_sync.h"
#include "secrets.h"
#include "setup_portal.h"
#include "wifi_setup_validation.h"

#include <WebServer.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <mbedtls/md.h>

namespace {
WebServer server(80);
DNSServer dns;
Preferences storage;
bool storageReady = false, setupActive = false, wasConnected = false;
bool connecting = false, startPending = false, completed = false;
// รหัส Wi-Fi ถูกเขียนลง NVS แล้วหรือยังในรอบตั้งค่านี้
// แยกจากการจับคู่บัญชี เพราะต่อเน็ตติดกับจับคู่สำเร็จเป็นคนละเรื่องกัน
bool candidateSaved = false;
// ปิดโหมดตั้งค่าเองหลังจากนี้ แม้การจับคู่ไม่ผ่าน (Wi-Fi บันทึกแล้ว กล่องทำงานต่อได้)
bool closePending = false;
bool bootSetupRequested = false;
unsigned long lastPortalActivityMs = 0;
// ข้อความสั้นภาษาอังกฤษสำหรับจอ LCD ซึ่งแสดงภาษาไทยไม่ได้ (message ยาวไว้ให้มือถืออ่าน)
const char *shortStatus = "OPEN";
uint32_t reconnectAttempts = 0;
unsigned long attemptAt = 0, nextClaimAt = 0, closeAt = 0, lastReconnectMs = 0;
constexpr unsigned long CONNECT_TIMEOUT_MS = 30000, RECONNECT_MS = 10000;
constexpr uint32_t WIFI_MAGIC = 0x50425731;
struct WifiSettings { uint32_t magic; char ssid[33]; char password[64]; };
WifiSettings saved = {}, candidate = {};
// setupPassword ต้องรองรับ WPA2 เต็มความยาว 63 ตัวอักษร เผื่อค่าที่กำหนดเองใน config.h
// (โหมดคำนวณจาก DEVICE_API_KEY ใช้แค่ 16 ตัวอักษร)
char setupSsid[32] = "", setupPassword[64] = "", nonce[33] = "", setupToken[21] = "";
const char *message = "กรอกรหัสตั้งค่าจากเว็บไซต์และ Wi-Fi บ้าน";

bool setupIdentity()
{
  if (SETUP_AP_FIXED_CREDENTIALS)
  {
    snprintf(setupSsid, sizeof(setupSsid), "%s", SETUP_AP_SSID);
    snprintf(setupPassword, sizeof(setupPassword), "%s", SETUP_AP_PASSWORD);
    return true;
  }

  unsigned char digest[32];
  const char *purpose = "pillbox-setup-ap-v1";
  const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info || mbedtls_md_hmac(info, reinterpret_cast<const unsigned char *>(DEVICE_API_KEY), strlen(DEVICE_API_KEY),
      reinterpret_cast<const unsigned char *>(purpose), strlen(purpose), digest) != 0) return false;
  // ย่อให้พอดีจอ 20 คอลัมน์ (PillBox_ + 6 = 14 ตัวอักษร) ผู้ใช้จะได้อ่านนิ่งๆ ไม่ต้องรอข้อความเลื่อน
  snprintf(setupSsid, sizeof(setupSsid), "PillBox_%02X%02X%02X", digest[0], digest[1], digest[2]);
  for (uint8_t i = 0; i < SETUP_AP_PASSWORD_BYTES; i++)
    snprintf(setupPassword + i * 2, 3, "%02x", digest[3 + i]);
  return true;
}

void sendJson(const JsonDocument &doc, int code = 200)
{
  String body; serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

void reply(int code, const char *text)
{
  JsonDocument doc; doc["message"] = text; sendJson(doc, code);
}

bool requirePortal()
{
  // Restrict setup endpoints to clients reaching the softAP interface, not the home LAN.
  if (setupActive && server.client().localIP() == WiFi.softAPIP())
  {
    // หน้าตั้งค่าบนมือถือโพล /setup/status อยู่ตลอด ตราบใดที่ยังเปิดหน้าอยู่จะไม่หมดเวลา
    lastPortalActivityMs = millis();
    return true;
  }
  server.send(403, "text/plain", "Setup mode required");
  return false;
}

/** แปลรหัสสถานะของ Wi-Fi เป็นเหตุผลที่คนอ่านแล้วรู้ว่าต้องไปแก้อะไร */
const char *wifiStatusText(int status)
{
  if (status == WL_NO_SSID_AVAIL) return "หาชื่อเครือข่ายไม่เจอ (สะกดผิด หรือเป็น 5GHz)";
  if (status == WL_CONNECT_FAILED) return "เจอเครือข่ายแต่เข้าไม่ได้ (รหัสผ่านผิด)";
  if (status == WL_DISCONNECTED) return "ยังเชื่อมไม่สำเร็จ";
  if (status == WL_CONNECTED) return "เชื่อมแล้ว";
  return "กำลังพยายามอยู่";
}

void startSetup(const char *reason)
{
  if (setupActive) return;
  if (!setupIdentity()) { Serial.println("[Setup] Cannot generate AP identity"); return; }
  for (int i = 0; i < 4; i++) snprintf(nonce + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  if (!WiFi.softAP(setupSsid, setupPassword)) { Serial.println("[Setup] softAP failed"); message = "เปิด Wi-Fi Setup ไม่สำเร็จ กรุณาเปิดเครื่องใหม่"; return; }
  dns.start(53, "*", WiFi.softAPIP());
  setupActive = true;
  closePending = completed = false;
  lastPortalActivityMs = millis();
  shortStatus = reason;
  Serial.printf("[Setup] เปิดโหมดตั้งค่า สาเหตุ: %s\n", reason);
  // พิมพ์รหัสผ่านออกมาด้วย เพื่อให้มีทางดูสำรองตอนจอ LCD เสียหรือต่อ I2C ไม่ติด
  // ไม่ถือว่าเพิ่มความเสี่ยงมาก เพราะคนที่เสียบ USB ถึงเครื่องได้ก็แฟลชเฟิร์มแวร์
  // อ่านค่าทุกอย่างออกมาได้อยู่แล้ว
  Serial.printf("[Setup] SSID %s  pass %s  -> http://192.168.4.1\n", setupSsid, setupPassword);
}

/** ปิดจุดปล่อย Wi-Fi ตั้งค่าแล้วกลับไปเป็นอุปกรณ์ในเครือข่ายบ้านอย่างเดียว */
void closeSetup()
{
  dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  setupActive = completed = closePending = false;
}

/** จับคู่ไม่ผ่าน: ถ้า Wi-Fi ใช้ได้และบันทึกแล้ว ให้ผู้ใช้อ่านผลบนมือถือก่อนแล้วปิดเอง */
void scheduleCloseAfterFailure()
{
  // Wi-Fi ยังไม่ได้บันทึก = ผู้ใช้ยังต้องใช้หน้านี้แก้ต่อ ห้ามปิด
  if (!candidateSaved) return;
  closePending = true;
  closeAt = millis() + SETUP_RESULT_GRACE_MS;
}

void handleHome()
{
  if (!setupActive) {
    // Status-only page. No unauthenticated motor controls on the setup/LAN web server.
    server.send(200, "text/html; charset=utf-8", "<meta charset='utf-8'><h1>Smart Pill Box</h1><p>ตั้งค่า Wi-Fi: กดปุ่มเขียว GPIO33 ค้าง 3 วินาทีขณะกลไกว่าง แล้วเชื่อม Wi-Fi Setup ตามป้ายบนกล่อง</p>");
    return;
  }
  if (!requirePortal()) return;
  String page(SETUP_PAGE); page.replace("__NONCE__", nonce);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleConnect()
{
  if (!requirePortal()) return;
  if (strcmp(server.arg("nonce").c_str(), nonce) != 0) { reply(403, "หน้า Setup หมดอายุ กรุณาเปิดหน้าใหม่"); return; }
  if (connecting || completed || startPending) { reply(409, "กำลังตั้งค่า กรุณารอ"); return; }
  const String ssid = server.arg("ssid"), password = server.arg("password");
  if (!validSetupWifi(ssid.c_str(), password.c_str())) { reply(400, "ชื่อ Wi-Fi ต้องยาว 1–32 ไบต์ รหัสผ่าน 8–63 ตัว หรือว่างสำหรับเครือข่ายเปิด"); return; }
  if (!normalizeSetupToken(server.arg("token").c_str(), setupToken)) { reply(400, "กรอกรหัสตั้งค่า 20 ตัวจากเว็บไซต์"); return; }
  if (!storageReady) { reply(503, "พื้นที่บันทึกตั้งค่าไม่พร้อม กรุณาเปิดเครื่องใหม่"); return; }
  candidate = {}; candidate.magic = WIFI_MAGIC;
  strcpy(candidate.ssid, ssid.c_str()); strcpy(candidate.password, password.c_str());
  startPending = true;
  closePending = false;  // ผู้ใช้กำลังลองใหม่ ห้ามปิดหน้าทับ
  shortStatus = "CONNECTING";
  message = "กำลังเชื่อม Wi-Fi บ้าน...";
  reply(202, message);
}

void handleScan()
{
  if (!requirePortal()) return;
  JsonDocument doc;
  const int count = WiFi.scanComplete();
  if (count < 0) {
    if (count != WIFI_SCAN_RUNNING && !connecting && !startPending) WiFi.scanNetworks(true);
    doc["scanning"] = true;
  } else {
    doc["scanning"] = false;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < count && i < 24; i++) networks.add(WiFi.SSID(i));
  }
  sendJson(doc);
}

void handleSetupStatus()
{
  if (!requirePortal()) return;
  JsonDocument doc; doc["message"] = message; doc["busy"] = connecting || startPending; doc["done"] = completed;
  sendJson(doc);
}
}

void wifiCheckSetupButtonAtBoot()
{
  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);
  delay(5);
  bootSetupRequested = false;
  const bool pressed = digitalRead(CONFIRM_BUTTON_PIN) == LOW;
  Serial.printf("[Setup] GPIO%u at boot: %s\n", CONFIRM_BUTTON_PIN, pressed ? "LOW (pressed)" : "HIGH (released)");
  if (!pressed) return;
  const unsigned long started = millis();
  while (digitalRead(CONFIRM_BUTTON_PIN) == LOW) {
    if (millis() - started >= 3000) {
      bootSetupRequested = true;
      Serial.println("[Setup] Button held 3 seconds; opening setup after initialization");
      return;
    }
    delay(10);
  }
  Serial.println("[Setup] Button released before 3 seconds");
}

void wifiWebBegin()
{
  storageReady = storage.begin("pillwifi", false);
  if (storageReady && storage.getBytesLength("config") == sizeof(saved)) {
    storage.getBytes("config", &saved, sizeof(saved));
    saved.ssid[32] = '\0'; saved.password[63] = '\0';
    if (saved.magic != WIFI_MAGIC || !validSetupWifi(saved.ssid, saved.password)) saved = {};
  }
  // Optional administrator-provisioned network. Keep any later portal settings.
#if defined(WIFI_PRESET_ENABLED) && WIFI_PRESET_ENABLED
  if (saved.magic != WIFI_MAGIC && storageReady && validSetupWifi(WIFI_SSID, WIFI_PASSWORD)) {
    WifiSettings preset = {};
    preset.magic = WIFI_MAGIC;
    strcpy(preset.ssid, WIFI_SSID);
    strcpy(preset.password, WIFI_PASSWORD);
    if (storage.putBytes("config", &preset, sizeof(preset)) == sizeof(preset)) {
      saved = preset;
      Serial.println("[Wi-Fi] Preset saved; connecting automatically");
    } else {
      Serial.println("[Wi-Fi] Could not save preset; opening setup");
    }
  }
#endif
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  server.on("/", HTTP_GET, handleHome);
  server.on("/setup/connect", HTTP_POST, handleConnect);
  server.on("/setup/scan", HTTP_GET, handleScan);
  server.on("/setup/status", HTTP_GET, handleSetupStatus);
  server.on("/status", HTTP_GET, []() {
    if (setupActive) { handleSetupStatus(); return; }
    String body; appStatusJson(body); server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", body);
  });
  server.onNotFound([]() {
    if (setupActive && server.client().localIP() == WiFi.softAPIP()) {
      server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", "Open setup");
    } else server.send(404, "text/plain", "Not found");
  });
  server.begin();
  if (saved.magic == WIFI_MAGIC && !bootSetupRequested)
  {
    Serial.printf("[Wi-Fi] กำลังเชื่อมกับ \"%s\" ที่บันทึกไว้\n", saved.ssid);
    WiFi.begin(saved.ssid, saved.password);
  }
  if (bootSetupRequested || saved.magic != WIFI_MAGIC)
  {
    if (!bootSetupRequested)
      Serial.println("[Wi-Fi] ยังไม่เคยตั้งค่า Wi-Fi จึงเปิดโหมดตั้งค่าแทนการเชื่อมต่อ");
    startSetup(bootSetupRequested ? "BOOT BUTTON" : "NO WIFI SAVED");
  }
  lastReconnectMs = millis();
}

void wifiWebLoop()
{
  const unsigned long now = millis();
  server.handleClient();
  if (setupActive) dns.processNextRequest();
  if (startPending) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    startPending = false; connecting = true; candidateSaved = false; attemptAt = now; nextClaimAt = now;
    WiFi.scanDelete(); WiFi.disconnect(); WiFi.begin(candidate.ssid, candidate.password);
    return;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connecting) {
    // บันทึกรหัส Wi-Fi ทันทีที่เชื่อมต่อติด ไม่รอผลการจับคู่บัญชี และเขียนทับค่าเดิมได้
    //
    // หน้าตั้งค่าเปิดได้สองทางเท่านั้น: เครื่องใหม่ที่ยังไม่มี Wi-Fi หรือกดปุ่มเขียวค้าง 3 วินาที
    // ทั้งสองทางต้องอยู่หน้าเครื่องจริง การกันไม่ให้เขียนทับจึงไม่ได้กันใครเพิ่ม
    // แต่ทำให้ "ตั้ง Wi-Fi ใหม่เสร็จแล้วไม่จำ" ทุกครั้งที่รหัสจับคู่หมดอายุหรือเว็บล่ม
    if (connected && !candidateSaved && storageReady) {
      if (storage.putBytes("config", &candidate, sizeof(candidate)) == sizeof(candidate)) {
        saved = candidate; candidateSaved = true;
        shortStatus = "WIFI SAVED";
        Serial.printf("[Wi-Fi] บันทึก \"%s\" ลงหน่วยความจำแล้ว\n", saved.ssid);
      } else {
        Serial.println("[Wi-Fi] เขียนค่าลง NVS ไม่สำเร็จ จะลืมค่านี้เมื่อรีเซ็ต");
      }
    }

    if (!connected && now - attemptAt >= CONNECT_TIMEOUT_MS) {
      connecting = false; WiFi.disconnect();
      memset(candidate.password, 0, sizeof(candidate.password)); memset(setupToken, 0, sizeof(setupToken));
      shortStatus = "WIFI FAILED";
      message = "เชื่อม Wi-Fi ไม่สำเร็จ ตรวจชื่อและรหัสผ่านแล้วลองใหม่ (ค่าเดิมยังไม่ถูกลบ)";
      Serial.println("[Setup] เชื่อม Wi-Fi ที่กรอกมาไม่สำเร็จภายในเวลาที่กำหนด");
    } else if (connected && static_cast<long>(now - nextClaimAt) >= 0) {
      shortStatus = "PAIRING";
      message = "เชื่อม Wi-Fi แล้ว กำลังยืนยันรหัสตั้งค่ากับเว็บไซต์...";
      const int code = netSyncCompleteSetup(setupToken);
      nextClaimAt = millis() + 10000;
      Serial.printf("[Setup] ผลการจับคู่บัญชี: HTTP %d\n", code);
      if (code == 200) {
        // เขียนซ้ำเผื่อครั้งแรกตอนเชื่อมต่อติดเขียนไม่สำเร็จ
        if (storage.putBytes("config", &candidate, sizeof(candidate)) != sizeof(candidate)) {
          shortStatus = "SAVE FAILED";
          message = "บันทึก Wi-Fi ไม่สำเร็จ กรุณาลองใหม่"; connecting = false;
        } else {
          saved = candidate; memset(candidate.password, 0, sizeof(candidate.password));
          memset(setupToken, 0, sizeof(setupToken)); completed = true; connecting = false;
          shortStatus = "PAIRED, DONE";
          message = "ตั้งค่าและจับคู่สำเร็จ กำลังปิด Wi-Fi Setup"; closeAt = millis() + 10000;
        }
      } else if (code == 400 || code == 401 || code == 403 || code == 404 || code == 409) {
        connecting = false; memset(setupToken, 0, sizeof(setupToken));
        shortStatus = code == 401 ? "BAD API KEY" : "PAIR FAILED";
        message = code == 401 ? "API Key ของกล่องไม่ถูกต้อง ติดต่อผู้ดูแลเครื่อง" : "รหัสตั้งค่าไม่ถูกต้อง หมดอายุ หรือกล่องอยู่ในบัญชีอื่น สร้างรหัสใหม่แล้วลองอีกครั้ง";
        scheduleCloseAfterFailure();
      } else {
        shortStatus = "NO SERVER";
        message = "เชื่อม Wi-Fi แล้ว แต่ยังติดต่อเว็บไซต์ไม่ได้ จะลองใหม่อัตโนมัติ";
        if (millis() - attemptAt > 120000) {
          connecting = false;
          message = "ติดต่อเว็บไซต์ไม่ได้ ตรวจอินเทอร์เน็ต แล้วกดเชื่อมต่อเพื่อลองใหม่";
          scheduleCloseAfterFailure();
        }
      }
    }
  }

  if ((completed || closePending) && static_cast<long>(millis() - closeAt) >= 0) {
    Serial.println(completed ? "[Setup] จับคู่สำเร็จ ปิดโหมดตั้งค่า"
                             : "[Setup] Wi-Fi บันทึกแล้วแต่จับคู่ไม่ผ่าน ปิดโหมดตั้งค่าเพื่อให้กล่องทำงานต่อ");
    closeSetup();
    netSyncRequestNow();
  }

  // ไม่มีมือถือเปิดหน้าตั้งค่าอยู่เลยนานเกินไป ขณะที่มี Wi-Fi บันทึกไว้แล้ว
  // เช่นเผลอกดปุ่มเขียวค้าง ห้ามปล่อยให้กล่องเงียบทั้งวันเพราะโหมดนี้ปิดการเตือนยาทั้งหมด
  // เครื่องใหม่ที่ยังไม่มี Wi-Fi ไม่นับ เพราะไม่มีอะไรให้กลับไปทำ
  if (setupActive && !connecting && !startPending && !completed && !closePending &&
      saved.magic == WIFI_MAGIC && millis() - lastPortalActivityMs >= SETUP_IDLE_TIMEOUT_MS) {
    Serial.println("[Setup] ไม่มีการใช้งานหน้าตั้งค่า กลับไปทำงานปกติด้วย Wi-Fi ที่บันทึกไว้");
    closeSetup();
    lastReconnectMs = millis() - RECONNECT_MS;  // เชื่อมต่อใหม่ทันทีในรอบถัดไป
  }
  if (!setupActive && !connected && saved.magic == WIFI_MAGIC && now - lastReconnectMs >= RECONNECT_MS) {
    lastReconnectMs = now;
    ++reconnectAttempts;
    // ต้องรายงานทุกครั้ง ไม่อย่างนั้นเวลาเชื่อมไม่ได้จะเงียบสนิทจนดูเหมือนเครื่องไม่ได้พยายาม
    Serial.printf("[Wi-Fi] เชื่อม %s ไม่สำเร็จ: %s (ลองใหม่ครั้งที่ %lu)\n",
                  saved.ssid,
                  wifiStatusText(WiFi.status()),
                  static_cast<unsigned long>(reconnectAttempts));
    WiFi.disconnect();
    WiFi.begin(saved.ssid, saved.password);
  }
  if (connected && !wasConnected)
  {
    reconnectAttempts = 0;
    Serial.print("[Wi-Fi] เชื่อมสำเร็จ IP = ");
    Serial.println(WiFi.localIP());
  }
  else if (!connected && wasConnected)
  {
    Serial.println("[Wi-Fi] หลุดการเชื่อมต่อ กำลังลองใหม่");
  }
  wasConnected = connected;
}

bool wifiStartSetup() { startSetup("BUTTON HOLD"); return setupActive; }

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
bool wifiSetupActive() { return setupActive; }
const char *wifiSetupSsid() { return setupSsid; }
const char *wifiSetupPassword() { return setupPassword; }
const char *wifiSetupStatusShort() { return shortStatus; }
bool wifiHasSavedNetwork() { return saved.magic == WIFI_MAGIC; }
const char *wifiSavedSsid() { return saved.magic == WIFI_MAGIC ? saved.ssid : ""; }
