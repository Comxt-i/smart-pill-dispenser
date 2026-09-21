#include "wifi_web.h"
#include "config.h"
#include "pill_app.h"
#include "rtc_lcd.h"
#include "secrets.h"

#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
bool serverStarted = false;
bool wasConnected = false;
unsigned long lastReconnectMs = 0;

/** เว้นระยะก่อนสั่งเชื่อมต่อใหม่ การเรียก WiFi.begin() รัวๆ ทำให้เชื่อมต่อช้าลง */
constexpr unsigned long RECONNECT_INTERVAL_MS = 10000;

void handleStatus()
{
  String body;
  appStatusJson(body);
  server.send(200, "application/json", body);
}

void handleHome()
{
  // หน้านี้มีไว้ตรวจสอบหน้างานเท่านั้น การตั้งตารางยาทั้งหมดทำบนเว็บหลัก
  String page =
    "<!doctype html><html lang='th'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Smart Pill Dispenser</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:16px;max-width:520px}"
    "h1{font-size:1.1rem}table{border-collapse:collapse;width:100%;font-size:.9rem}"
    "td,th{border:1px solid #ccc;padding:6px;text-align:left}"
    "form{margin-top:16px;padding:12px;border:1px solid #ccc;border-radius:8px}"
    "input,button{padding:6px;font-size:1rem}</style></head><body>"
    "<h1>Smart Pill Dispenser</h1>"
    "<p>ตารางยามาจาก server อัตโนมัติ หน้านี้ใช้ตรวจสอบและทดสอบหน้างานเท่านั้น</p>"
    "<table id='s'></table>"
    "<p><button onclick='rescan()'>สแกน I2C ใหม่</button> "
    "<span id='msg'></span></p>"
    "<form action='/dispense' method='get'>"
    "<b>ทดสอบจ่ายยา</b><br>ช่อง <input name='slot' type='number' min='1' max='3' value='1'> "
    "จำนวนเม็ด <input name='amount' type='number' min='1' max='9' step='0.5' value='1'> "
    "<button type='submit'>จ่าย</button></form>"
    "<script>fetch('/status').then(r=>r.json()).then(d=>{"
    "let rows=[['Firmware',d.firmware],['Wi-Fi',d.wifi_connected?d.ip+' ('+d.rssi+' dBm)':'ไม่ได้เชื่อมต่อ'],"
    "['นาฬิกา',d.clock_valid?'ตรงกับ server':'ยังไม่ได้ตั้งเวลา'],"
    "['Sync',d.sync_ok?('สำเร็จ ('+d.config_version+')'):('ล้มเหลว: '+d.sync_error)],"
    "['ผลรอส่ง',d.pending_events],"
    "['Servo',d.servo_movement_enabled?'เปิดใช้งาน':'ปิดอยู่ (ENABLE_SERVO_MOVEMENT=false)'],"
    "['I2C ที่เจอ',(d.i2c_found&&d.i2c_found.length)?d.i2c_found.join(', '):'ไม่เจออุปกรณ์เลย'],"
    "['I2C ที่ต้องเจอ',(d.i2c_expected||[]).join(', ')],"
    "['เส้น SDA/SCL',d.i2c_lines?((!d.i2c_lines.sda_ok||!d.i2c_lines.scl_ok)"
    "?'สายลัดลง GND หรือต่อผิดขา':((!d.i2c_lines.sda_pullup_ext||!d.i2c_lines.scl_pullup_ext)"
    "?'ไม่มี pull-up ภายนอก (shifter ไม่ได้รับไฟ?)':'ปกติ มี pull-up ครบ')):'-']];"
    "d.slots.forEach(s=>rows.push(['ช่อง '+s.slot,(s.name||'ว่าง')+' '+(s.active?'':'(ปิดอยู่) ')+"
    "s.doses.map(x=>x.time+' '+x.state).join(', ')]));"
    "document.getElementById('s').innerHTML=rows.map(r=>'<tr><th>'+r[0]+'</th><td>'+r[1]+'</td></tr>').join('');"
    "});"
    "function rescan(){document.getElementById('msg').textContent='กำลังสแกน...';"
    "fetch('/rescan').then(r=>r.json()).then(d=>{"
    "document.getElementById('msg').textContent='เจอ: '+((d.i2c_found&&d.i2c_found.length)?d.i2c_found.join(', '):'ไม่เจออุปกรณ์เลย');"
    "});}"
    "</script></body></html>";

  server.send(200, "text/html", page);
}

/**
 * สแกนบัส I2C ใหม่ตามคำสั่งจากหน้าเว็บ
 *
 * ใช้ตอนไล่ปัญหาสายหลวมหรือ address ไม่ตรง โดยไม่ต้องรีบูตบอร์ดหรือเสียบ USB
 * ขยับสายแล้วกดปุ่มบนหน้าเว็บซ้ำได้เรื่อยๆ
 */
void handleRescan()
{
  i2cScanAndReport();
  String body;
  appStatusJson(body);
  server.send(200, "application/json", body);
}

void handleDispense()
{
  const int slot = server.arg("slot").toInt();
  const float amount = server.arg("amount").toFloat();

  if (slot < 1 || slot > DISPENSER_COUNT)
  {
    server.send(400, "text/plain; charset=utf-8", "หมายเลขช่องต้องอยู่ระหว่าง 1 ถึง 3");
    return;
  }

  int status = 500;
  const char *message = appManualDispense(static_cast<uint8_t>(slot), amount, status);
  server.send(status, "text/plain; charset=utf-8", message);
}

void startServer()
{
  if (serverStarted)
    return;

  server.on("/", handleHome);
  server.on("/status", handleStatus);
  server.on("/rescan", handleRescan);
  server.on("/dispense", handleDispense);
  server.begin();
  serverStarted = true;
}
}

void wifiWebBegin()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastReconnectMs = millis();

  // ไม่รอจนเชื่อมต่อเสร็จ: กล่องยาต้องเดินนาฬิกาและรับปุ่มได้แม้ Wi-Fi ล่ม
  Serial.println("[Wi-Fi] กำลังเชื่อมต่อแบบไม่ block...");
}

void wifiWebLoop()
{
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !wasConnected)
  {
    Serial.print("[Wi-Fi] เชื่อมต่อแล้ว เปิดหน้าสถานะที่ http://");
    Serial.println(WiFi.localIP());
    startServer();
  }
  else if (!connected && wasConnected)
  {
    Serial.println("[Wi-Fi] หลุดการเชื่อมต่อ จะลองใหม่อัตโนมัติ");
  }
  wasConnected = connected;

  if (!connected && millis() - lastReconnectMs >= RECONNECT_INTERVAL_MS)
  {
    lastReconnectMs = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (serverStarted)
    server.handleClient();
}

bool wifiIsConnected()
{
  return WiFi.status() == WL_CONNECTED;
}
