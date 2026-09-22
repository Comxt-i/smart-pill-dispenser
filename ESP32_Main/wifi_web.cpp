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
bool bootHold = false;
unsigned long bootAt = 0, attemptAt = 0, nextClaimAt = 0, closeAt = 0, lastReconnectMs = 0;
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
  for (int i = 0; i < 8; i++) snprintf(setupPassword + i * 2, 3, "%02x", digest[3 + i]);
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
  if (setupActive && server.client().localIP() == WiFi.softAPIP()) return true;
  server.send(403, "text/plain", "Setup mode required");
  return false;
}

void startSetup()
{
  if (setupActive || !setupIdentity()) return;
  for (int i = 0; i < 4; i++) snprintf(nonce + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  if (!WiFi.softAP(setupSsid, setupPassword)) { message = "เปิด Wi-Fi Setup ไม่สำเร็จ กรุณาเปิดเครื่องใหม่"; return; }
  dns.start(53, "*", WiFi.softAPIP());
  setupActive = true;
  // พิมพ์รหัสผ่านออกมาด้วย เพื่อให้มีทางดูสำรองตอนจอ LCD เสียหรือต่อ I2C ไม่ติด
  // ไม่ถือว่าเพิ่มความเสี่ยงมาก เพราะคนที่เสียบ USB ถึงเครื่องได้ก็แฟลชเฟิร์มแวร์
  // อ่านค่าทุกอย่างออกมาได้อยู่แล้ว
  Serial.printf("[Setup] SSID %s  pass %s  -> http://192.168.4.1\n", setupSsid, setupPassword);
}

void handleHome()
{
  if (!setupActive) {
    // Status-only page. No unauthenticated motor controls on the setup/LAN web server.
    server.send(200, "text/html; charset=utf-8", "<meta charset='utf-8'><h1>Smart Pill Box</h1><p>ตั้งค่า Wi-Fi: กด CONFIRM ค้างขณะเปิดเครื่อง 3 วินาที แล้วเชื่อม Wi-Fi Setup ตามป้ายบนกล่อง</p>");
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

void wifiWebBegin()
{
  bootAt = millis(); bootHold = digitalRead(CONFIRM_BUTTON_PIN) == LOW;
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
  if (saved.magic == WIFI_MAGIC) WiFi.begin(saved.ssid, saved.password);
  else startSetup();
  lastReconnectMs = millis();
}

void wifiWebLoop()
{
  const unsigned long now = millis();
  if (bootHold) {
    if (digitalRead(CONFIRM_BUTTON_PIN) != LOW) bootHold = false;
    else if (now - bootAt >= 3000) { bootHold = false; startSetup(); }
  }
  server.handleClient();
  if (setupActive) dns.processNextRequest();
  if (startPending) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    startPending = false; connecting = true; attemptAt = now; nextClaimAt = now;
    WiFi.scanDelete(); WiFi.disconnect(); WiFi.begin(candidate.ssid, candidate.password);
    return;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connecting) {
    if (!connected && now - attemptAt >= CONNECT_TIMEOUT_MS) {
      connecting = false; WiFi.disconnect();
      memset(candidate.password, 0, sizeof(candidate.password)); memset(setupToken, 0, sizeof(setupToken));
      message = "เชื่อม Wi-Fi ไม่สำเร็จ ตรวจชื่อและรหัสผ่านแล้วลองใหม่ (ค่าเดิมยังไม่ถูกลบ)";
    } else if (connected && static_cast<long>(now - nextClaimAt) >= 0) {
      message = "เชื่อม Wi-Fi แล้ว กำลังยืนยันรหัสตั้งค่ากับเว็บไซต์...";
      const int code = netSyncCompleteSetup(setupToken);
      nextClaimAt = millis() + 10000;
      if (code == 200) {
        // Persist only after both Wi-Fi association and account authorization succeed.
        if (storage.putBytes("config", &candidate, sizeof(candidate)) != sizeof(candidate)) {
          message = "บันทึก Wi-Fi ไม่สำเร็จ กรุณาลองใหม่"; connecting = false;
        } else {
          saved = candidate; memset(candidate.password, 0, sizeof(candidate.password));
          memset(setupToken, 0, sizeof(setupToken)); completed = true; connecting = false;
          message = "ตั้งค่าและจับคู่สำเร็จ กำลังปิด Wi-Fi Setup"; closeAt = millis() + 10000;
        }
      } else if (code == 400 || code == 401 || code == 403 || code == 404 || code == 409) {
        connecting = false; memset(setupToken, 0, sizeof(setupToken));
        message = code == 401 ? "API Key ของกล่องไม่ถูกต้อง ติดต่อผู้ดูแลเครื่อง" : "รหัสตั้งค่าไม่ถูกต้อง หมดอายุ หรือกล่องอยู่ในบัญชีอื่น สร้างรหัสใหม่แล้วลองอีกครั้ง";
      } else {
        message = "เชื่อม Wi-Fi แล้ว แต่ยังติดต่อเว็บไซต์ไม่ได้ จะลองใหม่อัตโนมัติ";
        if (millis() - attemptAt > 120000) { connecting = false; message = "ติดต่อเว็บไซต์ไม่ได้ ตรวจอินเทอร์เน็ต แล้วกดเชื่อมต่อเพื่อลองใหม่"; }
      }
    }
  }
  if (completed && static_cast<long>(millis() - closeAt) >= 0) {
    dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); setupActive = false;
    netSyncRequestNow(); completed = false;
  }
  if (!setupActive && !connected && saved.magic == WIFI_MAGIC && now - lastReconnectMs >= RECONNECT_MS) {
    lastReconnectMs = now; WiFi.disconnect(); WiFi.begin(saved.ssid, saved.password);
  }
  if (connected && !wasConnected) Serial.println("[Wi-Fi] Connected");
  wasConnected = connected;
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
bool wifiSetupActive() { return setupActive; }
const char *wifiSetupSsid() { return setupSsid; }
const char *wifiSetupPassword() { return setupPassword; }
