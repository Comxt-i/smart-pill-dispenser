/*
 * TestDispenseEvent — ทดสอบการยิงผลการกดรับยาขึ้น server
 *
 * sketch นี้แยกจาก firmware หลัก ใช้ตรวจว่าเส้นทาง ESP32 -> backend ใช้งานได้จริง
 * โดยไม่ต้องต่อ LCD, RTC, Servo หรือกลไกใดๆ ต่อแค่ ESP32 กับสาย USB ก็ทดสอบได้
 *
 * สิ่งที่ทดสอบ:
 *   1. เชื่อม Wi-Fi และตั้งนาฬิกาจาก NTP (จำเป็นสำหรับ HTTPS)
 *   2. GET  /api/device/sync   -> ดึงตารางยาของวันนี้มาแสดง
 *   3. POST /api/device/events -> ส่งผลการจ่ายยา แล้วพิมพ์คำตอบของ server ให้ดูทั้งก้อน
 *   4. ส่ง event เดิมซ้ำ เพื่อพิสูจน์ว่า event_id กันบันทึกซ้ำได้จริง
 *
 * วิธีใช้:
 *   - คัดลอก secrets.example.h เป็น secrets.h แล้วกรอกค่าให้ครบ
 *   - ต่อปุ่มจาก GPIO33 ลง GND (ไม่มีปุ่มก็สั่งผ่าน Serial Monitor ได้)
 *   - เปิด Serial Monitor ที่ 115200 baud แล้วพิมพ์ h เพื่อดูคำสั่งทั้งหมด
 *
 * ต้องติดตั้งไลบรารี ArduinoJson เวอร์ชัน 7 ขึ้นไป
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "certs.h"
#include "secrets.h"

// ---------------------------------------------------------------------------
// ตั้งค่า
// ---------------------------------------------------------------------------

constexpr uint8_t DISPENSE_BUTTON_PIN = 33;  // ปุ่มต่อลง GND
constexpr unsigned long DEBOUNCE_MS = 40;
constexpr unsigned long HTTP_TIMEOUT_MS = 8000;
constexpr int TZ_OFFSET_MINUTES = 420;  // ไทย = UTC+7 ใช้แค่ตอนแสดงผล

// ตรวจใบรับรองเมื่อใช้ https; ตั้ง false เฉพาะตอนไล่ปัญหาเท่านั้น
constexpr bool TLS_VERIFY_CERTIFICATE = true;

// ---------------------------------------------------------------------------
// สถานะภายใน
// ---------------------------------------------------------------------------

WiFiClient plainClient;
WiFiClientSecure secureClient;
bool tlsReady = false;

// ข้อมูลมื้อยาที่ได้จาก sync ล่าสุด ใช้แนบไปกับ event ให้ผูกกับมื้อยาจริง
char targetScheduleId[40] = "";
char targetMedicationName[32] = "";
uint8_t targetSlot = 0;
float targetAmount = 1.0f;

// event_id ที่ส่งไปล่าสุด เก็บไว้ทดสอบส่งซ้ำ
char lastEventId[24] = "";
uint32_t eventCounter = 0;
char idPrefix[8] = "test";

bool buttonStable = false;
bool buttonLastRaw = false;
unsigned long buttonChangedAtMs = 0;

// ---------------------------------------------------------------------------
// ตัวช่วย
// ---------------------------------------------------------------------------

bool usesTls()
{
  return strncmp(SERVER_BASE_URL, "https://", 8) == 0;
}

/** แปลง epoch เป็น ISO 8601 แบบ UTC โดยไม่พึ่ง gmtime_r หรือ TimeLib */
void formatIso8601Utc(uint32_t epoch, char *out, size_t size)
{
  const uint32_t secondsOfDay = epoch % 86400UL;
  int32_t days = static_cast<int32_t>(epoch / 86400UL);

  // civil_from_days: แปลงจำนวนวันนับจาก 1970-01-01 เป็นปี/เดือน/วัน
  days += 719468;
  const int32_t era = (days >= 0 ? days : days - 146096) / 146097;
  const uint32_t dayOfEra = static_cast<uint32_t>(days - era * 146097);
  const uint32_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const int32_t year = static_cast<int32_t>(yearOfEra) + era * 400;
  const uint32_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const uint32_t mp = (5 * dayOfYear + 2) / 153;
  const uint32_t day = dayOfYear - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp < 10 ? mp + 3 : mp - 9;

  snprintf(out,
           size,
           "%04ld-%02lu-%02luT%02lu:%02lu:%02luZ",
           static_cast<long>(month <= 2 ? year + 1 : year),
           static_cast<unsigned long>(month),
           static_cast<unsigned long>(day),
           static_cast<unsigned long>(secondsOfDay / 3600),
           static_cast<unsigned long>((secondsOfDay % 3600) / 60),
           static_cast<unsigned long>(secondsOfDay % 60));
}

/** สร้าง event_id ที่ไม่ซ้ำจาก MAC + ตัวนับ */
void makeEventId(char *out, size_t size)
{
  snprintf(out, size, "%s-%lu", idPrefix, static_cast<unsigned long>(++eventCounter));
}

WiFiClient *networkClient()
{
  if (!usesTls())
    return &plainClient;

  if (!tlsReady)
  {
    if (TLS_VERIFY_CERTIFICATE)
    {
      secureClient.setCACert(SERVER_ROOT_CA_PEM);
    }
    else
    {
      secureClient.setInsecure();
      Serial.println("[TLS] คำเตือน: ปิดการตรวจใบรับรองอยู่");
    }
    tlsReady = true;
  }
  return &secureClient;
}

void prepareRequest(HTTPClient &http)
{
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("X-API-Key", DEVICE_API_KEY);
  http.addHeader("Content-Type", "application/json");
}

// ---------------------------------------------------------------------------
// 1) Wi-Fi และนาฬิกา
// ---------------------------------------------------------------------------

void connectWifi()
{
  Serial.printf("[Wi-Fi] กำลังเชื่อมต่อ %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[Wi-Fi] เชื่อมต่อไม่สำเร็จ ตรวจ SSID/รหัสผ่านใน secrets.h");
    return;
  }

  Serial.print("[Wi-Fi] สำเร็จ IP = ");
  Serial.println(WiFi.localIP());
  Serial.printf("[Wi-Fi] ความแรงสัญญาณ %d dBm\n", static_cast<int>(WiFi.RSSI()));

  // ต้องใช้ MAC address ตอนลงทะเบียนอุปกรณ์ผ่าน POST /api/devices เพื่อขอ API Key
  Serial.print("[Wi-Fi] MAC address ของบอร์ดนี้ = ");
  Serial.println(WiFi.macAddress());
}

/** HTTPS ตรวจอายุใบรับรองจากนาฬิกา ถ้าเวลาไม่ถูกต้องจะเชื่อมต่อไม่ผ่านทุกครั้ง */
bool syncClockFromNtp()
{
  Serial.print("[NTP] กำลังขอเวลา");
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  for (int i = 0; i < 40; ++i)
  {
    if (time(nullptr) > 1704067200)  // หลัง 1 ม.ค. 2024 = ตั้งเวลาแล้ว
    {
      char stamp[32];
      formatIso8601Utc(static_cast<uint32_t>(time(nullptr)), stamp, sizeof(stamp));
      Serial.printf("\n[NTP] สำเร็จ เวลา UTC = %s\n", stamp);
      return true;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[NTP] ขอเวลาไม่สำเร็จ — ถ้าใช้ https จะเชื่อมต่อไม่ผ่าน");
  return false;
}

// ---------------------------------------------------------------------------
// 2) GET /api/device/sync
// ---------------------------------------------------------------------------

bool fetchSchedule()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[sync] Wi-Fi ยังไม่เชื่อมต่อ");
    return false;
  }

  char url[224];
  snprintf(url, sizeof(url), "%s/api/device/sync?firmware_version=test-sketch", SERVER_BASE_URL);
  Serial.printf("\n[sync] GET %s\n", url);

  HTTPClient http;
  if (!http.begin(*networkClient(), url))
  {
    Serial.println("[sync] เปิดการเชื่อมต่อไม่สำเร็จ");
    return false;
  }
  prepareRequest(http);

  const int code = http.GET();
  Serial.printf("[sync] HTTP %d\n", code);

  if (code != 200)
  {
    Serial.println("[sync] เนื้อหาที่ตอบกลับ:");
    Serial.println(http.getString());
    http.end();
    if (code == 401)
      Serial.println("       -> DEVICE_API_KEY ไม่ตรงกับในฐานข้อมูล");
    else if (code < 0)
      Serial.println("       -> ต่อไม่ถึง server หรือใบรับรองไม่ตรงกับ certs.h");
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  if (error)
  {
    Serial.printf("[sync] อ่าน JSON ไม่สำเร็จ: %s\n", error.c_str());
    return false;
  }

  Serial.printf("[sync] วันนี้คือ %s, ตารางเวอร์ชัน %s\n",
                doc["day_of_week"] | "?",
                doc["config_version"] | "?");

  targetScheduleId[0] = '\0';
  targetSlot = 0;

  Serial.println("---- ตารางยาของวันนี้ ----");
  for (JsonObject slot : doc["slots"].as<JsonArray>())
  {
    const uint8_t number = slot["slot"] | 0;
    const bool active = slot["active"] | false;
    const char *name = slot["name"] | "";

    if (!active || strlen(name) == 0)
    {
      Serial.printf("  ช่อง %u: ว่างหรือปิดใช้งาน\n", number);
      continue;
    }

    Serial.printf("  ช่อง %u: %s (ครั้งละ %.1f เม็ด)\n",
                  number,
                  name,
                  static_cast<double>(slot["amount_per_dose"] | 1.0f));

    for (JsonObject dose : slot["doses"].as<JsonArray>())
    {
      const bool done = dose["done"] | false;
      Serial.printf("      %s  %s\n", dose["time"] | "??:??", done ? "(จ่ายแล้ว)" : "(ยังไม่จ่าย)");

      // เลือกมื้อแรกที่ยังไม่ได้จ่าย ไว้ใช้เป็นเป้าหมายของ event ทดสอบ
      if (!done && targetScheduleId[0] == '\0')
      {
        strncpy(targetScheduleId, dose["schedule_id"] | "", sizeof(targetScheduleId) - 1);
        strncpy(targetMedicationName, name, sizeof(targetMedicationName) - 1);
        targetSlot = number;
        targetAmount = slot["amount_per_dose"] | 1.0f;
      }
    }
  }
  Serial.println("--------------------------");

  if (targetSlot == 0)
  {
    Serial.println("[sync] ไม่พบมื้อยาที่ยังไม่ได้จ่ายในวันนี้");
    Serial.println("       กดปุ่มได้อยู่ แต่ event จะไม่ผูกกับมื้อยาใด");
  }
  else
  {
    Serial.printf("[sync] เป้าหมายทดสอบ: ช่อง %u (%s) เวลามื้อยา schedule_id=%s\n",
                  targetSlot,
                  targetMedicationName,
                  targetScheduleId);
  }
  return true;
}

// ---------------------------------------------------------------------------
// 3) POST /api/device/events
// ---------------------------------------------------------------------------

/**
 * ส่งผลการจ่ายยาขึ้น server
 * reuseLastId = true จะส่ง event_id เดิมซ้ำ เพื่อพิสูจน์ว่า server ไม่บันทึกซ้ำ
 */
bool sendEvent(const char *status, bool reuseLastId)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[event] Wi-Fi ยังไม่เชื่อมต่อ");
    return false;
  }

  if (reuseLastId && lastEventId[0] == '\0')
  {
    Serial.println("[event] ยังไม่เคยส่ง event จึงยังไม่มี id ให้ส่งซ้ำ");
    return false;
  }

  if (!reuseLastId)
    makeEventId(lastEventId, sizeof(lastEventId));

  JsonDocument doc;
  JsonArray events = doc["events"].to<JsonArray>();
  JsonObject item = events.add<JsonObject>();

  item["event_id"] = lastEventId;
  item["status"] = status;
  if (targetScheduleId[0] != '\0')
    item["schedule_id"] = targetScheduleId;
  if (targetSlot > 0)
  {
    item["slot_number"] = targetSlot;
    item["amount"] = targetAmount;
  }
  item["note"] = "test sketch";

  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (now > 1704067200)
  {
    char stamp[32];
    formatIso8601Utc(now, stamp, sizeof(stamp));
    item["timestamp"] = stamp;
  }

  String body;
  serializeJson(doc, body);

  char url[224];
  snprintf(url, sizeof(url), "%s/api/device/events", SERVER_BASE_URL);

  Serial.printf("\n[event] POST %s\n", url);
  Serial.print("[event] ส่ง: ");
  Serial.println(body);

  HTTPClient http;
  if (!http.begin(*networkClient(), url))
  {
    Serial.println("[event] เปิดการเชื่อมต่อไม่สำเร็จ");
    return false;
  }
  prepareRequest(http);

  const unsigned long startedMs = millis();
  const int code = http.POST(body);
  const unsigned long elapsedMs = millis() - startedMs;

  const String response = http.getString();
  http.end();

  Serial.printf("[event] HTTP %d (ใช้เวลา %lu ms)\n", code, elapsedMs);
  Serial.print("[event] ตอบกลับ: ");
  Serial.println(response);

  if (code != 200)
  {
    if (code == 401)
      Serial.println("        -> DEVICE_API_KEY ไม่ถูกต้อง");
    else if (code == 400)
      Serial.println("        -> รูปแบบ JSON ไม่ผ่าน validation ของ server");
    else if (code < 0)
      Serial.println("        -> ต่อไม่ถึง server หรือใบรับรองไม่ตรงกับ certs.h");
    return false;
  }

  // แยกดูว่า server รับหรือปฏิเสธรายการไหน
  JsonDocument parsed;
  if (deserializeJson(parsed, response) == DeserializationError::Ok)
  {
    for (JsonVariant id : parsed["accepted"].as<JsonArray>())
      Serial.printf("        รับแล้ว: %s\n", id.as<const char *>());

    for (JsonObject rejected : parsed["rejected"].as<JsonArray>())
      Serial.printf("        ถูกปฏิเสธ: %s (%s)\n",
                    rejected["event_id"] | "?",
                    rejected["reason"] | "ไม่ระบุเหตุผล");
  }

  Serial.println("[event] เปิดหน้าเว็บดูประวัติการจ่ายยาได้เลย");
  return true;
}

// ---------------------------------------------------------------------------
// ปุ่มและเมนู Serial
// ---------------------------------------------------------------------------

void printMenu()
{
  Serial.println();
  Serial.println("==================== คำสั่งทดสอบ ====================");
  Serial.println("  กดปุ่ม GPIO33  หรือพิมพ์ d  = ส่ง DISPENSED (จ่ายยาสำเร็จ)");
  Serial.println("  m = ส่ง MISSED (ขาดยา)");
  Serial.println("  s = ส่ง SKIPPED (ผู้ใช้กดข้าม)");
  Serial.println("  f = ส่ง FAILED (จ่ายไม่สำเร็จ)");
  Serial.println("  x = ส่ง event เดิมซ้ำ (ต้องไม่เกิดประวัติซ้ำใน server)");
  Serial.println("  r = ดึงตารางยาใหม่ (GET /api/device/sync)");
  Serial.println("  h = แสดงเมนูนี้อีกครั้ง");
  Serial.println("====================================================");
}

/** true หนึ่งครั้งต่อการกดหนึ่งครั้ง */
bool buttonPressed()
{
  const bool raw = digitalRead(DISPENSE_BUTTON_PIN) == LOW;

  if (raw != buttonLastRaw)
  {
    buttonLastRaw = raw;
    buttonChangedAtMs = millis();
    return false;
  }

  if (millis() - buttonChangedAtMs < DEBOUNCE_MS || raw == buttonStable)
    return false;

  buttonStable = raw;
  return raw;  // แจ้งเฉพาะตอนกดลง ไม่แจ้งตอนปล่อย
}

void handleSerialCommand(char command)
{
  switch (command)
  {
    case 'd': sendEvent("DISPENSED", false); break;
    case 'm': sendEvent("MISSED", false); break;
    case 's': sendEvent("SKIPPED", false); break;
    case 'f': sendEvent("FAILED", false); break;
    case 'x':
      Serial.println("\n[ทดสอบ] ส่ง event_id เดิมซ้ำ — ผลที่ถูกต้องคือ server ตอบ accepted");
      Serial.println("[ทดสอบ] แต่ต้องไม่มีประวัติเพิ่มขึ้นในหน้าเว็บ");
      sendEvent("DISPENSED", true);
      break;
    case 'r': fetchSchedule(); break;
    case 'h': printMenu(); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(300);

  pinMode(DISPENSE_BUTTON_PIN, INPUT_PULLUP);

  const uint64_t mac = ESP.getEfuseMac();
  snprintf(idPrefix,
           sizeof(idPrefix),
           "%02X%02X%02X",
           static_cast<uint8_t>(mac >> 16),
           static_cast<uint8_t>(mac >> 8),
           static_cast<uint8_t>(mac));

  Serial.println();
  Serial.println("===== ทดสอบการส่งผลการจ่ายยาขึ้น server =====");
  Serial.printf("server: %s (%s)\n",
                SERVER_BASE_URL,
                usesTls() ? "HTTPS ตรวจใบรับรอง" : "HTTP ไม่เข้ารหัส");
  Serial.printf("รหัสนำหน้า event_id: %s\n", idPrefix);

  connectWifi();

  if (usesTls())
    syncClockFromNtp();

  fetchSchedule();
  printMenu();
}

void loop()
{
  if (buttonPressed())
  {
    Serial.println("\n[ปุ่ม] ตรวจพบการกดปุ่มรับยา");
    sendEvent("DISPENSED", false);
  }

  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command != '\n' && command != '\r')
      handleSerialCommand(command);
  }

  delay(10);
}
