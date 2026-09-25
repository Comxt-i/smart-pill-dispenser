#include "net_sync.h"
#include "command_journal.h"
#include "certs.h"
#include "rtc_lcd.h"
#include "schedule_store.h"
#include "secrets.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>
#include <time.h>

namespace {
unsigned long nextSyncAtMs = 0;
unsigned long lastOkMs = 0;
unsigned long pollIntervalMs = SYNC_INTERVAL_MS;
uint32_t lastServerEpoch = 0;
int tzOffsetMinutes = 420;
bool lastCallOk = false;
char lastError[64] = "ยังไม่ได้เชื่อมต่อ";
char configVersion[12] = "";

RemoteCommand commands[MAX_PENDING_COMMANDS];
uint8_t commandCount = 0;
uint8_t commandHead = 0;

// ประกาศล่วงหน้า เพราะตัวช่วยของ TLS ด้านล่างเรียกใช้ก่อนถึงนิยามจริง
void setError(const char *message);

WiFiClient plainClient;
WiFiClientSecure secureClient;
bool tlsClientReady = false;
bool ntpStarted = false;

/** true เมื่อ SERVER_BASE_URL เป็น https */
bool usesTls()
{
  return strncmp(SERVER_BASE_URL, "https://", 8) == 0;
}

/**
 * TLS ตรวจอายุใบรับรองจากนาฬิกาของระบบ ถ้าเวลาไม่ถูกต้องจะเชื่อมต่อไม่ผ่านทุกครั้ง
 *
 * ลำดับการหาเวลา: นาฬิกาของระบบ -> DS1307 (มีถ่านสำรองจึงตรงอยู่แล้วหลังไฟดับ) -> NTP
 * คืน false เมื่อยังไม่ได้เวลา ผู้เรียกต้องรอรอบถัดไป
 */
bool systemClockReadyForTls()
{
  // 1 ม.ค. 2024 ใช้เป็นเส้นแบ่งว่านาฬิกาถูกตั้งแล้วหรือยัง
  constexpr time_t MIN_VALID_EPOCH = 1704067200;

  if (time(nullptr) > MIN_VALID_EPOCH)
    return true;

  // ตั้งจาก RTC ก่อน เพราะได้ทันทีโดยไม่ต้องรอเครือข่าย
  const uint32_t localEpoch = rtcLocalEpoch();
  if (localEpoch > 0)
  {
    // นาฬิกาของระบบเก็บเป็น UTC ส่วน RTC เก็บเวลาท้องถิ่น จึงต้องถอย offset ออก
    timeval now = {};
    now.tv_sec = static_cast<time_t>(localEpoch) - tzOffsetMinutes * 60;
    settimeofday(&now, nullptr);
    Serial.println("[TLS] ตั้งนาฬิการะบบจาก DS1307 เพื่อใช้ตรวจใบรับรอง");
    return true;
  }

  if (!ntpStarted)
  {
    // ยังไม่เคยตั้ง RTC (เครื่องใหม่หรือถ่านหมด) จึงต้องพึ่ง NTP ก่อนหนึ่งครั้ง
    Serial.println("[TLS] RTC ยังไม่ได้ตั้งเวลา กำลังขอเวลาจาก NTP...");
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    ntpStarted = true;
  }

  return false;
}

/**
 * เลือก client ให้ตรงกับ scheme ของ SERVER_BASE_URL
 * คืน nullptr เมื่อใช้ https แต่ยังตั้งนาฬิกาไม่ได้
 */
WiFiClient *networkClient()
{
  if (!usesTls())
    return &plainClient;

  if (!systemClockReadyForTls())
  {
    setError("รอตั้งนาฬิกาก่อนเชื่อมต่อ HTTPS");
    return nullptr;
  }

  if (!tlsClientReady)
  {
    if (TLS_VERIFY_CERTIFICATE)
    {
      secureClient.setCACert(SERVER_ROOT_CA_PEM);
    }
    else
    {
      // ไม่ตรวจใบรับรอง = ใครดักกลางทางก็อ่าน API Key ได้ ใช้ได้เฉพาะตอนทดสอบ
      secureClient.setInsecure();
      Serial.println("[TLS] คำเตือน: ปิดการตรวจใบรับรองอยู่ (TLS_VERIFY_CERTIFICATE=false)");
    }
    secureClient.setTimeout(HTTP_TIMEOUT_MS / 1000);
    tlsClientReady = true;
  }

  return &secureClient;
}

void setError(const char *message)
{
  strncpy(lastError, message, sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
}

void copyText(char *dest, size_t size, const char *source)
{
  if (!source)
  {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, source, size - 1);
  dest[size - 1] = '\0';
}

void buildUrl(char *out, size_t size, const char *path)
{
  snprintf(out, size, "%s%s", SERVER_BASE_URL, path);
}

/** ตั้งค่า header ที่ทุก request ต้องมี */
void prepare(HTTPClient &http)
{
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("X-API-Key", DEVICE_API_KEY);
  http.addHeader("Content-Type", "application/json");
}

/** แปลง "HH:MM" เป็นนาทีนับจากเที่ยงคืน คืน -1 เมื่อรูปแบบไม่ถูกต้อง */
int parseTimeToMinutes(const char *time)
{
  if (!time || strlen(time) < 4)
    return -1;

  const int hour = atoi(time);
  const char *colon = strchr(time, ':');
  if (!colon)
    return -1;
  const int minute = atoi(colon + 1);

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
    return -1;
  return hour * 60 + minute;
}

void pushCommand(const RemoteCommand &command)
{
  if (commandJournalContains(command.id)) return;
  for (uint8_t i = 0; i < commandCount; ++i)
    if (strcmp(commands[(commandHead + i) % MAX_PENDING_COMMANDS].id, command.id) == 0) return;
  if (commandCount >= MAX_PENDING_COMMANDS)
    return;

  const uint8_t slot = (commandHead + commandCount) % MAX_PENDING_COMMANDS;
  commands[slot] = command;
  ++commandCount;
}

/** เวลาท้องถิ่นในรูปแบบ ISO 8601 พร้อม offset เช่น 2026-09-17T08:00:00+07:00 */
void formatTimestamp(uint32_t localEpoch, char *out, size_t size)
{
  if (localEpoch == 0)
  {
    out[0] = '\0';
    return;
  }

  // ใช้ breakTime ของ TimeLib แทน gmtime_r เพราะมีอยู่แล้วในโปรเจกต์
  // และ localEpoch ถูกบวก offset มาจาก server แล้ว จึงไม่ต้องแปลง timezone ซ้ำ
  tmElements_t parts;
  breakTime(static_cast<time_t>(localEpoch), parts);

  const int offset = tzOffsetMinutes;
  const char sign = offset < 0 ? '-' : '+';
  const int absOffset = offset < 0 ? -offset : offset;

  snprintf(out,
           size,
           "%04d-%02u-%02uT%02u:%02u:%02u%c%02d:%02d",
           tmYearToCalendar(parts.Year),
           parts.Month,
           parts.Day,
           parts.Hour,
           parts.Minute,
           parts.Second,
           sign,
           absOffset / 60,
           absOffset % 60);
}
}

void netSyncBegin()
{
  commandJournalBegin();
  nextSyncAtMs = millis();
  commandCount = 0;
  commandHead = 0;
  lastCallOk = false;

  Serial.printf("[net] เชื่อมต่อไปที่ %s (%s)\n",
                SERVER_BASE_URL,
                usesTls() ? (TLS_VERIFY_CERTIFICATE ? "HTTPS ตรวจใบรับรอง"
                                                    : "HTTPS ไม่ตรวจใบรับรอง")
                          : "HTTP ไม่เข้ารหัส");
}

bool netSyncDue()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  return static_cast<long>(millis() - nextSyncAtMs) >= 0;
}

void netSyncRequestNow()
{
  nextSyncAtMs = millis();
}

bool netSyncFetch()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    setError("Wi-Fi ยังไม่เชื่อมต่อ");
    lastCallOk = false;
    return false;
  }

  char url[224];
  char query[128];
  snprintf(query,
           sizeof(query),
           "/api/device/sync?firmware_version=%s&ip_address=%s&rssi=%d&dry_run=%s",
           FIRMWARE_VERSION,
           WiFi.localIP().toString().c_str(),
           static_cast<int>(WiFi.RSSI()),
           !ENABLE_SERVO_MOVEMENT && DISPENSE_DRY_RUN ? "true" : "false");
  buildUrl(url, sizeof(url), query);

  WiFiClient *client = networkClient();
  if (!client)
  {
    lastCallOk = false;
    nextSyncAtMs = millis() + SYNC_RETRY_MS;
    return false;
  }

  HTTPClient http;
  if (!http.begin(*client, url))
  {
    setError("เปิดการเชื่อมต่อ HTTP ไม่สำเร็จ");
    lastCallOk = false;
    nextSyncAtMs = millis() + SYNC_RETRY_MS;
    return false;
  }
  prepare(http);

  const int code = http.GET();
  if (code != 200)
  {
    snprintf(lastError, sizeof(lastError), "sync ล้มเหลว (HTTP %d)", code);
    Serial.printf("[sync] %s\n", lastError);
    http.end();
    lastCallOk = false;
    nextSyncAtMs = millis() + SYNC_RETRY_MS;
    return false;
  }

  // อ่าน body ให้ครบเป็น String ก่อนแล้วค่อย parse
  //
  // ห้าม parse จาก http.getStream() ตรงๆ เพราะเมื่อ payload โตขึ้น (มียาหลายช่อง)
  // ข้อมูลจะมาเป็นหลายก้อนผ่าน TLS แล้ว ArduinoJson จะเจอสตรีมขาดกลางคัน
  // แล้วคืน IncompleteInput ทั้งที่ server ส่งมาครบ
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);

  if (error)
  {
    snprintf(lastError, sizeof(lastError), "อ่าน JSON ไม่สำเร็จ (%s)", error.c_str());
    Serial.printf("[sync] %s\n", lastError);
    lastCallOk = false;
    nextSyncAtMs = millis() + SYNC_RETRY_MS;
    return false;
  }

  // ---- เวลา: ตั้ง RTC ให้ตรงกับ server ----
  tzOffsetMinutes = doc["tz_offset_minutes"] | 420;
  const uint32_t localEpoch = doc["local_epoch"] | 0UL;
  if (localEpoch > 0)
  {
    lastServerEpoch = localEpoch;
    rtcSyncFromEpoch(localEpoch);
  }

  copyText(configVersion, sizeof(configVersion), doc["config_version"] | "");

  // ---- ตารางยาของวันนี้ ----
  scheduleBeginSync();
  for (JsonObject slotJson : doc["slots"].as<JsonArray>())
  {
    const uint8_t number = slotJson["slot"] | 0;
    const char *medicationId = slotJson["medication_id"] | "";
    const int slotIndex = scheduleStageSlot(number,
                                            slotJson["active"] | false,
                                            medicationId,
                                            slotJson["name"] | "",
                                            slotJson["amount_per_dose"] | 1.0f);
    if (slotIndex < 0)
      continue;

    for (JsonObject doseJson : slotJson["doses"].as<JsonArray>())
    {
      const int minutes = doseJson["minutes"] | parseTimeToMinutes(doseJson["time"] | "");
      if (minutes < 0)
        continue;

      scheduleStageDose(slotIndex,
                        doseJson["schedule_id"] | "",
                        doseJson["label"] | "",
                        minutes,
                        doseJson["done"] | false);
    }
  }
  scheduleCommitSync();

  // ---- คำสั่งที่เว็บฝากไว้ ----
  for (JsonObject commandJson : doc["commands"].as<JsonArray>())
  {
    RemoteCommand command = {};
    command.receivedAtMs = millis();
    copyText(command.id, sizeof(command.id), commandJson["id"] | "");
    copyText(command.type, sizeof(command.type), commandJson["type"] | "DISPENSE");
    copyText(command.scheduleId, sizeof(command.scheduleId), commandJson["schedule_id"] | "");
    command.slot = commandJson["slot"] | 0;
    command.amount = commandJson["amount"] | 1.0f;

    if (command.id[0] != '\0')
      pushCommand(command);
  }

  const unsigned long nextPollSec = doc["next_poll_sec"] | (SYNC_INTERVAL_MS / 1000);
  pollIntervalMs = nextPollSec * 1000UL;
  if (pollIntervalMs < 2000UL)
    pollIntervalMs = 2000UL;

  nextSyncAtMs = millis() + pollIntervalMs;
  lastOkMs = millis();
  lastCallOk = true;
  setError("");

  Serial.printf("[sync] สำเร็จ ตาราง %s, คำสั่งค้าง %u, รอบถัดไปใน %lu วินาที\n",
                configVersion,
                commandCount,
                nextPollSec);
  return true;
}

bool netSyncFlushEvents()
{
  const uint8_t pending = eventQueueSize();
  if (pending == 0)
    return true;

  if (WiFi.status() != WL_CONNECTED)
    return false;

  JsonDocument doc;
  JsonArray events = doc["events"].to<JsonArray>();

  // ส่งทีละไม่เกิน 10 รายการเพื่อไม่ให้ payload และหน่วยความจำบานปลาย
  const uint8_t batchSize = pending > 10 ? 10 : pending;
  for (uint8_t i = 0; i < batchSize; ++i)
  {
    const PendingEvent &event = eventQueueAt(i);
    JsonObject item = events.add<JsonObject>();
    item["event_id"] = event.eventId;
    item["status"] = event.status;

    if (event.scheduleId[0] != '\0')
      item["schedule_id"] = event.scheduleId;
    if (event.medicationId[0] != '\0')
      item["medication_id"] = event.medicationId;
    if (event.commandId[0] != '\0')
      item["command_id"] = event.commandId;
    if (event.note[0] != '\0')
      item["note"] = event.note;
    if (event.slot > 0)
      item["slot_number"] = event.slot;
    if (event.amount > 0)
      item["amount"] = event.amount;

    char timestamp[32];
    formatTimestamp(event.localEpoch, timestamp, sizeof(timestamp));
    if (timestamp[0] != '\0')
      item["timestamp"] = timestamp;
  }

  char url[160];
  buildUrl(url, sizeof(url), "/api/device/events");

  WiFiClient *client = networkClient();
  if (!client)
    return false;

  HTTPClient http;
  if (!http.begin(*client, url))
  {
    setError("เปิดการเชื่อมต่อ HTTP ไม่สำเร็จ");
    return false;
  }
  prepare(http);

  String body;
  serializeJson(doc, body);

  const int code = http.POST(body);
  if (code != 200)
  {
    snprintf(lastError, sizeof(lastError), "ส่งผลการจ่ายยาไม่สำเร็จ (HTTP %d)", code);
    Serial.printf("[events] %s — เก็บไว้ในคิวเพื่อส่งใหม่\n", lastError);
    http.end();
    return false;
  }

  // อ่านให้ครบก่อนแล้วค่อย parse ด้วยเหตุผลเดียวกับใน netSyncFetch()
  const String reply = http.getString();
  http.end();

  JsonDocument response;
  const DeserializationError error = deserializeJson(response, reply);

  if (error)
  {
    // server รับไปแล้วแต่เราอ่านคำตอบไม่ออก: เก็บไว้ส่งใหม่ได้ เพราะกันซ้ำด้วย event_id
    setError("อ่านคำตอบของ /events ไม่สำเร็จ");
    return false;
  }

  uint8_t removed = 0;
  for (JsonVariant id : response["accepted"].as<JsonArray>())
  {
    eventQueueRemove(id.as<const char *>());
    ++removed;
  }

  // รายการที่ถูกปฏิเสธถือเป็นคำตอบสุดท้าย ต้องเอาออกไม่งั้นจะวนส่งไม่จบ
  for (JsonObject item : response["rejected"].as<JsonArray>())
  {
    const char *id = item["event_id"] | "";
    Serial.printf("[events] server ปฏิเสธ %s: %s\n", id, item["reason"] | "ไม่ระบุเหตุผล");
    eventQueueRemove(id);
    ++removed;
  }

  if (removed > 0)
    eventQueuePersist();

  Serial.printf("[events] ส่งสำเร็จ %u รายการ เหลือค้าง %u\n", removed, eventQueueSize());
  return true;
}

uint32_t netSyncLastServerEpoch()
{
  return lastServerEpoch;
}

int netSyncTimezoneOffsetMinutes()
{
  return tzOffsetMinutes;
}

bool netSyncTakeCommand(RemoteCommand &command)
{
  while (commandCount > 0 && millis() - commands[commandHead].receivedAtMs >= 15UL * 60UL * 1000UL) {
    commandHead = (commandHead + 1) % MAX_PENDING_COMMANDS;
    --commandCount;
  }
  if (commandCount == 0)
    return false;

  const RemoteCommand &next = commands[commandHead];
  if (strcmp(next.type, "DISPENSE") == 0 && !commandJournalReserve(next.id, rtcLocalEpoch())) {
    setError("Command journal unavailable; motion blocked");
    return false;
  }
  command = next;
  commandHead = (commandHead + 1) % MAX_PENDING_COMMANDS;
  --commandCount;
  return true;
}

bool netSyncLastCallOk()
{
  return lastCallOk;
}

unsigned long netSyncLastOkMs()
{
  return lastOkMs;
}

const char *netSyncLastError()
{
  return lastError;
}

const char *netSyncConfigVersion()
{
  return configVersion;
}

int netSyncCompleteSetup(const char *token)
{
  if (WiFi.status() != WL_CONNECTED) return 0;
  WiFiClient *client = networkClient();
  if (!client) return 0;
  char url[224]; buildUrl(url, sizeof(url), "/api/device/setup/complete");
  HTTPClient http;
  if (!http.begin(*client, url)) return 0;
  prepare(http);
  JsonDocument doc; doc["token"] = token;
  String body; serializeJson(doc, body);
  const int code = http.POST(body);
  http.end();
  return code;
}
