#include "pill_hole.h"
#include <math.h>
#include "net_sync.h"
#include "command_journal.h"
#include "certs.h"
#include "rtc_lcd.h"
#include "schedule_store.h"
#include "schedule_cache.h"
#include "secrets.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace {
unsigned long nextSyncAtMs = 0;
unsigned long lastOkMs = 0;
unsigned long pollIntervalMs = SYNC_INTERVAL_MS;
uint32_t lastServerEpoch = 0;
int tzOffsetMinutes = 420;
bool lastCallOk = false;
// ภาษาไทยใช้ 3 ไบต์ต่อตัวอักษรใน UTF-8 ข้อความไม่ยาวก็เต็ม 64 ไบต์แล้ว รหัส HTTP ท้ายข้อความจะถูกตัดทิ้ง
char lastError[128] = "ยังไม่ได้เชื่อมต่อ";
char configVersion[12] = "";
// จาก sync ล่าสุด ส่งกลับไปที่ /wait ให้ server รู้ว่ากล่องเห็นข้อมูลรุ่นไหนอยู่
char stateVersion[12] = "";
// server รุ่นนี้มี /wait (ส่ง state_version มา) = รู้การเปลี่ยนทันที sync เต็มเป็นแค่ตาข่ายรองรับ
bool waitSupported = false;
// sync ครั้งแรกหลังเปิดเครื่องจบแล้ว (สำเร็จหรือไม่ก็ตาม) ใช้ตัดสินว่าจะหันไปใช้ตารางในเครื่องไหม
bool firstSyncFinished = false;
unsigned long nextWaitAtMs = 0;
unsigned long waitBlockedUntilMs = 0;

RemoteCommand commands[MAX_PENDING_COMMANDS];
uint8_t commandCount = 0;
uint8_t commandHead = 0;

// ประกาศล่วงหน้า เพราะตัวช่วยของ TLS ด้านล่างเรียกใช้ก่อนถึงนิยามจริง
void setError(const char *message);

WiFiClient plainClient;
WiFiClientSecure secureClient;
bool ntpStarted = false;

// 1 ม.ค. 2024 ใช้เป็นเส้นแบ่งว่านาฬิกาของระบบถูกตั้งแล้วหรือยัง (TLS ต้องใช้ตรวจอายุใบรับรอง)
constexpr time_t MIN_VALID_EPOCH = 1704067200;

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

/** ตั้งค่า TLS ครั้งเดียวตอนเริ่ม ก่อนมี task เบื้องหลัง จะได้ไม่ต้องแชร์สถานะนี้ข้ามคอร์ */
void initTlsClient()
{
  if (!usesTls())
    return;
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
}

/**
 * client สำหรับ task เบื้องหลัง
 *
 * ห้ามเรียก systemClockReadyForTls() ตรงนี้ เพราะมันอ่าน rtcLocalEpoch() ซึ่ง "แก้ค่า" นาฬิกาซอฟต์แวร์
 * ทุกครั้งที่อ่าน ถ้าสองคอร์เรียกพร้อมกันนาฬิกาจะเพี้ยน loop หลักเตรียมเวลาไว้ให้ก่อนส่ง job แล้ว
 * ตรงนี้แค่เช็คซ้ำแบบอ่านอย่างเดียว
 */
WiFiClient *workerClient()
{
  if (!usesTls())
    return &plainClient;
  if (time(nullptr) <= MIN_VALID_EPOCH)
    return nullptr;
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

// ---------------------------------------------------------------------------
// งานเครือข่ายเบื้องหลัง
// ---------------------------------------------------------------------------
//
// คุยกับ server ผ่าน HTTPS ครั้งหนึ่งใช้เวลาเป็นวินาที ถ้าทำใน loop หลักทั้งเครื่องจะค้างระหว่างนั้น:
// ปุ่มไม่ตอบ จอไม่ขยับ เสียงเตือนสะดุด จึงย้ายเฉพาะ "การรับส่ง" ไป task แยกบนคอร์ 0
//
// กติกาที่ทำให้ปลอดภัย: task เบื้องหลังแตะได้แค่ `job` กับตัว HTTP เท่านั้น
// ห้ามแตะตารางยา นาฬิกา คิวผลการจ่ายยา หรือตัวแปรอื่นของ loop หลัก
// การสร้าง request และการนำผลไปใช้ทำใน loop หลักทั้งหมด
//
// ส่งต่อ job ด้วยสถานะเดียว เจ้าของ job มีทีละฝั่งเสมอ จึงไม่ต้องใช้ mutex:
//   JOB_IDLE              loop หลักเป็นเจ้าของ เขียน request ได้
//   JOB_QUEUED/RUNNING    task เบื้องหลังเป็นเจ้าของ
//   JOB_DONE              loop หลักเป็นเจ้าของ อ่านผลแล้วคืนเป็น JOB_IDLE
enum class JobKind : uint8_t { Sync, Events, Claim, Wait };
enum JobState : uint8_t { JOB_IDLE, JOB_QUEUED, JOB_RUNNING, JOB_DONE };

// ผลที่ไม่ใช่รหัส HTTP (HTTPClient เองใช้ -1 ถึง -11)
constexpr int JOB_ERR_NO_WIFI = -1001;
constexpr int JOB_ERR_CLOCK = -1002;
constexpr int JOB_ERR_BEGIN = -1003;

struct NetJob {
  JobKind kind = JobKind::Sync;
  bool post = false;
  char url[256] = "";
  String body;
  int code = 0;
  String reply;
  // /wait ถือสายนานกว่า request ทั่วไปมาก timeout ของ HTTP ต้องยาวกว่าที่ server ถือไว้
  unsigned long timeoutMs = HTTP_TIMEOUT_MS;
};

NetJob job;
std::atomic<uint8_t> jobState(JOB_IDLE);

#if defined(ARDUINO_ARCH_ESP32)
TaskHandle_t workerTask = nullptr;
#endif

/** ทำ job ที่รออยู่หนึ่งงาน รันบน task เบื้องหลัง (หรือทันทีในที่เดียวกันถ้าไม่มี task) */
void runJob()
{
  jobState.store(JOB_RUNNING);
  job.code = 0;
  job.reply = String();

  if (WiFi.status() != WL_CONNECTED)
  {
    job.code = JOB_ERR_NO_WIFI;
  }
  else
  {
    WiFiClient *client = workerClient();
    HTTPClient http;
    if (!client)
    {
      job.code = JOB_ERR_CLOCK;
    }
    else if (!http.begin(*client, job.url))
    {
      job.code = JOB_ERR_BEGIN;
    }
    else
    {
      prepare(http);
      http.setTimeout(static_cast<uint16_t>(job.timeoutMs));
      if (usesTls())
        secureClient.setTimeout((job.timeoutMs + 999) / 1000);  // task นี้เป็นเจ้าของ client คนเดียว
      job.code = job.post ? http.POST(job.body) : http.GET();
      // อ่านให้ครบเป็น String ก่อน parse (ดูเหตุผลใน applySync)
      if (job.code > 0)
        job.reply = http.getString();
      http.end();
    }
  }

  jobState.store(JOB_DONE);
}

#if defined(ARDUINO_ARCH_ESP32)
void workerLoop(void *)
{
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (jobState.load() == JOB_QUEUED)
      runJob();
  }
}
#endif

/** ส่งงานให้ task เบื้องหลัง คืน false ถ้ามีงานค้างอยู่ (ทำได้ทีละงาน) */
bool submitJob(JobKind kind, bool post, const char *url, const String &body,
               unsigned long timeoutMs = HTTP_TIMEOUT_MS)
{
  if (jobState.load() != JOB_IDLE)
    return false;

  job.kind = kind;
  job.post = post;
  copyText(job.url, sizeof(job.url), url);
  job.body = body;
  job.timeoutMs = timeoutMs;
  jobState.store(JOB_QUEUED);

#if defined(ARDUINO_ARCH_ESP32)
  if (workerTask)
  {
    xTaskNotifyGive(workerTask);
    return true;
  }
#endif
  // ไม่มี task (สร้างไม่สำเร็จ หรือคอมไพล์บนคอมเพื่อทดสอบ): ทำทันทีแบบเดิม
  runJob();
  return true;
}

/**
 * server ขอให้ถามถี่ขึ้นได้ (ตอนมีคำสั่งค้าง) แต่ห่างกว่าเพดานไม่ได้
 * แก้ตารางบนเว็บแล้วจอต้องเปลี่ยนภายในไม่กี่วินาที ไม่ว่า server รุ่นที่ deploy อยู่จะตั้งไว้เท่าไร
 * และไม่ถี่กว่า 2 วินาที กัน server ที่ตั้งค่าผิดสั่งให้ยิงรัว
 */
unsigned long pollIntervalFromServer(unsigned long serverSec, bool pushAvailable)
{
  // มี /wait = server บอกเองทันทีที่มีอะไรเปลี่ยน sync เต็มจึงห่างได้ (แค่ตาข่ายรองรับ)
  // ไม่มี = ต้องถามถี่เองเหมือนเดิม
  const unsigned long ceilingMs = pushAvailable ? FULL_SYNC_MAX_MS : SYNC_INTERVAL_MS;
  // เทียบเป็นวินาทีก่อนคูณ ค่าใหญ่ผิดปกติจาก server คูณ 1000 แล้วจะล้นวนกลับเป็นค่าเล็ก
  if (serverSec >= ceilingMs / 1000UL)
    return ceilingMs;
  const unsigned long ms = serverSec * 1000UL;
  return ms < 2000UL ? 2000UL : ms;
}

void describeFailure(const char *what, int code);  // นิยามอยู่ด้านล่าง

/** วันในสัปดาห์ของ epoch ท้องถิ่น แบบเดียวกับที่ server ใช้ (1 ม.ค. 1970 เป็นวันพฤหัส) */
const char *dayCodeForEpoch(uint32_t localEpoch)
{
  static const char *const CODES[7] = {"THU", "FRI", "SAT", "SUN", "MON", "TUE", "WED"};
  return CODES[(localEpoch / 86400UL) % 7];
}

/**
 * มื้อนี้ต้องกินวันนี้ไหม
 * days ว่าง = ทุกวัน (รวมถึง server รุ่นเก่าที่ส่งเฉพาะมื้อของวันนี้มาโดยไม่มี days)
 */
bool doseRunsToday(const char *days, const char *today)
{
  if (!days || days[0] == '\0')
    return true;
  for (const char *p = days; *p;)
  {
    while (*p == ' ')
      ++p;
    if (strncmp(p, today, 3) == 0 && (p[3] == ',' || p[3] == ' ' || p[3] == '\0'))
      return true;
    const char *comma = strchr(p, ',');
    if (!comma)
      break;
    p = comma + 1;
  }
  return false;
}

/**
 * มื้อนี้ถือว่าจบแล้วตั้งแต่ตอนจัดตารางไหม (จะไม่เตือนอีก)
 *   - server บอกว่ากินแล้ว: เชื่อได้เฉพาะเมื่อข้อมูลได้มาในวันนี้
 *   - เครื่องบันทึกไว้เองว่าจบแล้ววันนี้: เชื่อเสมอ แม้ server ยังไม่รู้ (ส่งผลขึ้นไม่ทันก่อนไฟดับ)
 */
bool doseAlreadyHandled(bool serverSaysDone, bool serverDoneValidToday, const char *scheduleId, uint32_t dayKey)
{
  return (serverDoneValidToday && serverSaysDone) || scheduleCacheIsClosed(scheduleId, dayKey);
}

/** server ตอบ /wait แล้ว: มีอะไรเปลี่ยนก็ sync ทันที ไม่มีก็เปิดสายรอใหม่ทันที */
void onWaitAnswer(bool changed)
{
  if (changed)
    nextSyncAtMs = millis();
  nextWaitAtMs = millis();
}

/** /wait ล้มเหลว: ถอยไปก่อนแล้วค่อยลองใหม่ ไม่ยิงรัวใส่ server ที่มีปัญหา */
void onWaitFailed(int code)
{
  if (code == 404)
  {
    // server รุ่นที่ยังไม่มี /wait: กลับไปถามถี่ตามรอบปกติ แล้วค่อยลองใหม่ภายหลัง
    waitSupported = false;
    waitBlockedUntilMs = millis() + WAIT_UNSUPPORTED_RETRY_MS;
    nextSyncAtMs = millis();
    Serial.println("[wait] server ยังไม่รองรับการแจ้งทันที กลับไปถามเป็นรอบ");
    return;
  }
  describeFailure("wait", code);
  nextWaitAtMs = millis() + SYNC_RETRY_MS;
}

/**
 * ตารางจาก JSON (ทั้ง sync สดและที่เก็บไว้ในเครื่อง) -> มื้อของวันนี้ในตารางที่ใช้เตือน
 *
 * `honorServerDone`: สถานะ "กินแล้ว" ของ server ใช้ได้เฉพาะวันที่ได้ข้อมูลมา
 * มื้อที่เครื่องบันทึกว่าจบแล้ววันนี้ ถือว่าจบเสมอ ไม่ว่าข้อมูลจะมาจากไหน (กันจ่ายซ้ำ)
 */
void stageSchedule(JsonDocument &doc, const char *today, uint32_t dayKey, bool honorServerDone)
{
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

    // ขนาดเม็ดยาที่ผู้ใช้เลือกบนเว็บ -> ช่องปล่อยยาที่จะลองก่อน
    // เว็บส่ง pill_size_mm เป็น 8/13/15 (ทรงกลม) หรือ 25 (ทรงรี/แคปซูล)
    // null / ไม่ส่งมา = ไม่ระบุ ไล่ลองจากช่องเล็กสุด
    const float pillSizeMm = slotJson["pill_size_mm"] | 0.0f;
    scheduleStageSlotPillHole(slotIndex, pillHoleFromMillimetres(static_cast<long>(ceilf(pillSizeMm))));

    for (JsonObject doseJson : slotJson["doses"].as<JsonArray>())
    {
      const int minutes = doseJson["minutes"] | parseTimeToMinutes(doseJson["time"] | "");
      if (minutes < 0)
        continue;
      if (!doseRunsToday(doseJson["days"] | "", today))
        continue;

      const char *scheduleId = doseJson["schedule_id"] | "";
      const bool done = doseAlreadyHandled(doseJson["done"] | false, honorServerDone, scheduleId, dayKey);
      scheduleStageDose(slotIndex, scheduleId, doseJson["label"] | "", minutes, done);
    }
  }
  scheduleCommitSync();
}

/** คืน job ให้ loop หลักใช้ต่อ ปล่อยหน่วยความจำของ request/response */
void finishJob()
{
  job.body = String();
  job.reply = String();
  jobState.store(JOB_IDLE);
}

/** เงื่อนไขก่อนส่ง: รันใน loop หลักเท่านั้น เพราะอาจตั้งนาฬิการะบบจาก RTC */
bool readyToSend()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    setError("Wi-Fi ยังไม่เชื่อมต่อ");
    return false;
  }
  if (usesTls() && !systemClockReadyForTls())
  {
    setError("รอตั้งนาฬิกาก่อนเชื่อมต่อ HTTPS");
    return false;
  }
  return true;
}

/** อธิบายผลที่ไม่ใช่ 200 ให้คนอ่านรู้ว่าต้องแก้อะไร */
void describeFailure(const char *what, int code)
{
  if (code == JOB_ERR_NO_WIFI)
    setError("Wi-Fi หลุดระหว่างส่ง");
  else if (code == JOB_ERR_CLOCK)
    setError("รอตั้งนาฬิกาก่อนเชื่อมต่อ HTTPS");
  else if (code == JOB_ERR_BEGIN)
    setError("เปิดการเชื่อมต่อ HTTP ไม่สำเร็จ");
  else
    // รหัสขึ้นก่อนเสมอ ต่อให้ข้อความถูกตัดก็ยังเห็นสาเหตุ
    snprintf(lastError, sizeof(lastError), "HTTP %d: %s ล้มเหลว", code, what);
  Serial.printf("[net] %s\n", lastError);
}
}

void netSyncBegin()
{
  commandJournalBegin();

  // job ที่ค้างจากก่อนหน้า (เช่นเรียกซ้ำตอนทดสอบ) ห้ามถูกนำไปใช้ต่อ
  if (jobState.load() == JOB_DONE)
    finishJob();

  initTlsClient();
#if defined(ARDUINO_ARCH_ESP32)
  if (!workerTask)
  {
    // คอร์ 0 เป็นคอร์เดียวกับ Wi-Fi stack ส่วน loop หลักอยู่คอร์ 1 จึงไม่แย่งเวลากัน
    // stack 12 KB เผื่อ TLS handshake ซึ่งกินสแตกมาก
    if (xTaskCreatePinnedToCore(workerLoop, "net", 12288, nullptr, 1, &workerTask, 0) != pdPASS)
    {
      workerTask = nullptr;
      Serial.println("[net] สร้าง task เบื้องหลังไม่สำเร็จ จะคุยกับ server ใน loop หลักแทน (เครื่องจะค้างระหว่างส่ง)");
    }
  }
#endif
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
  if (jobState.load() != JOB_IDLE)
    return false;  // มีงานค้างอยู่ รอผลก่อน
  return static_cast<long>(millis() - nextSyncAtMs) >= 0;
}

void netSyncRequestNow()
{
  nextSyncAtMs = millis();
}

/** นำผล sync ไปใช้ รันใน loop หลักเท่านั้น (แก้ตารางยา นาฬิกา และคิวคำสั่ง) */
static bool applySync(int code, const String &body)
{
  firstSyncFinished = true;
  if (code != 200)
  {
    describeFailure("sync", code);
    lastCallOk = false;
    nextSyncAtMs = millis() + SYNC_RETRY_MS;
    return false;
  }

  // body ถูกอ่านเป็น String จนครบแล้วใน runJob()
  //
  // ห้าม parse จาก http.getStream() ตรงๆ เพราะเมื่อ payload โตขึ้น (มียาหลายช่อง)
  // ข้อมูลจะมาเป็นหลายก้อนผ่าน TLS แล้ว ArduinoJson จะเจอสตรีมขาดกลางคัน
  // แล้วคืน IncompleteInput ทั้งที่ server ส่งมาครบ
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

  // ---- ตารางยา: server ส่งทั้งสัปดาห์ เครื่องเลือกมื้อของวันนี้เอง แล้วเก็บสำเนาไว้ใช้ตอนออฟไลน์ ----
  copyText(stateVersion, sizeof(stateVersion), doc["state_version"] | "");
  waitSupported = stateVersion[0] != '\0';
  const uint32_t nowLocal = localEpoch > 0 ? localEpoch : rtcLocalEpoch();
  const uint32_t today = rtcDayKey();
  stageSchedule(doc, nowLocal > 0 ? dayCodeForEpoch(nowLocal) : "", today, true);
  if (scheduleCacheStore(body, stateVersion, today))
    Serial.printf("[cache] เก็บตารางยารุ่น %s ไว้ในเครื่องแล้ว\n", stateVersion);

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
  pollIntervalMs = pollIntervalFromServer(nextPollSec, waitSupported);

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

bool netSyncFetch()
{
  if (jobState.load() != JOB_IDLE)
    return false;
  if (!readyToSend())
  {
    lastCallOk = false;
    nextSyncAtMs = millis() + SYNC_RETRY_MS;
    return false;
  }

  char url[224];
  char query[128];
  snprintf(query,
           sizeof(query),
           "/api/device/sync?schema=2&firmware_version=%s&ip_address=%s&rssi=%d&dry_run=%s",
           FIRMWARE_VERSION,
           WiFi.localIP().toString().c_str(),
           static_cast<int>(WiFi.RSSI()),
           !ENABLE_SERVO_MOVEMENT && DISPENSE_DRY_RUN ? "true" : "false");
  buildUrl(url, sizeof(url), query);

  // กันส่งซ้ำระหว่างรอผล รอบถัดไปจริงถูกตั้งตอนนำผลไปใช้
  nextSyncAtMs = millis() + SYNC_RETRY_MS;
  return submitJob(JobKind::Sync, false, url, String());
}

/** นำคำตอบของ /events ไปใช้ รันใน loop หลักเท่านั้น (แก้คิวผลการจ่ายยาใน NVS) */
static bool applyEvents(int code, const String &reply)
{
  if (code != 200)
  {
    describeFailure("ส่งผลการจ่ายยา", code);
    Serial.println("[events] เก็บไว้ในคิวเพื่อส่งใหม่");
    return false;
  }

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

bool netSyncFlushEvents()
{
  const uint8_t pending = eventQueueSize();
  if (pending == 0)
    return true;

  if (jobState.load() != JOB_IDLE || !readyToSend())
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

  String body;
  serializeJson(doc, body);
  return submitJob(JobKind::Events, true, url, body);
}

/** เปิดสายรอให้ server บอกว่ามีอะไรเปลี่ยน (ไม่ block) */
static bool startWait()
{
  char url[224];
  char query[128];
  snprintf(query, sizeof(query), "/api/device/wait?state=%s&timeout_sec=%lu&dry_run=%s",
           stateVersion, static_cast<unsigned long>(WAIT_TIMEOUT_SEC),
           !ENABLE_SERVO_MOVEMENT && DISPENSE_DRY_RUN ? "true" : "false");
  buildUrl(url, sizeof(url), query);
  // เผื่อเวลาเครือข่ายเกินที่ server ถือสายไว้ ไม่งั้นกล่องตัดสายก่อน server ตอบ
  return submitJob(JobKind::Wait, false, url, String(), (WAIT_TIMEOUT_SEC + 10UL) * 1000UL);
}

static void applyWait(int code, const String &reply)
{
  if (code != 200)
  {
    onWaitFailed(code);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, reply))
  {
    onWaitFailed(code);
    return;
  }
  // อ่านไม่ออกว่าเปลี่ยนไหม ให้ถือว่าเปลี่ยน sync เกินหนึ่งรอบดีกว่าพลาดการแก้ไข
  onWaitAnswer(doc["changed"] | true);
}

bool netSyncFirstSyncFinished()
{
  return firstSyncFinished;
}

bool netSyncApplyCachedSchedule()
{
  const uint32_t nowLocal = rtcLocalEpoch();
  if (nowLocal == 0)
    return false;  // ยังไม่รู้วันที่ เลือกมื้อของวันนี้ไม่ได้

  String body;
  uint32_t fetchedDay = 0;
  if (!scheduleCacheLoad(body, fetchedDay))
    return false;

  JsonDocument doc;
  if (deserializeJson(doc, body))
  {
    Serial.println("[cache] อ่านตารางยาในเครื่องไม่ออก รอ sync จาก server");
    return false;
  }

  const uint32_t today = rtcDayKey();
  stageSchedule(doc, dayCodeForEpoch(nowLocal), today, fetchedDay == today);
  Serial.printf("[cache] ใช้ตารางยาในเครื่อง (รุ่น %s) เตือนได้แม้ไม่มีเน็ต\n",
                scheduleCacheStateVersion());
  return true;
}

void netSyncPump()
{
  if (jobState.load() != JOB_IDLE)
    return;
  if (netSyncDue())
  {
    netSyncFetch();
    return;
  }
  // server รุ่นเก่าที่ไม่มี /wait: ถามเป็นรอบตาม netSyncDue() อย่างเดียว
  if (!waitSupported || stateVersion[0] == '\0')
    return;
  if (static_cast<long>(millis() - waitBlockedUntilMs) < 0 ||
      static_cast<long>(millis() - nextWaitAtMs) < 0)
    return;
  if (!readyToSend())
    return;
  startWait();
}

void netSyncService()
{
  if (jobState.load() != JOB_DONE)
    return;

  switch (job.kind)
  {
    case JobKind::Sync:
      applySync(job.code, job.reply);
      break;
    case JobKind::Events:
      applyEvents(job.code, job.reply);
      break;
    case JobKind::Wait:
      applyWait(job.code, job.reply);
      break;
    case JobKind::Claim:
      break;  // คนรอเลิกรอไปแล้วเพราะหมดเวลา ทิ้งผลได้
  }
  finishJob();
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
  // ใช้ task เบื้องหลังตัวเดียวกัน ไม่เปิด TLS ซ้อนสองชุด (หน่วยความจำไม่พอ)
  // รอได้เพราะเรียกระหว่างโหมดตั้งค่า ซึ่งเดิมก็ block ระหว่างยืนยันรหัสอยู่แล้ว
  if (!readyToSend())
    return 0;

  const unsigned long limitMs = HTTP_TIMEOUT_MS * 3;
  const unsigned long startedMs = millis();
  while (jobState.load() != JOB_IDLE)
  {
    if (jobState.load() == JOB_DONE)
      netSyncService();  // งานก่อนหน้าเสร็จแล้ว นำผลไปใช้ให้จบก่อน
    else if (millis() - startedMs > limitMs)
      return 0;
    else
      delay(10);
  }

  char url[224];
  buildUrl(url, sizeof(url), "/api/device/setup/complete");
  JsonDocument doc;
  doc["token"] = token;
  String body;
  serializeJson(doc, body);
  if (!submitJob(JobKind::Claim, true, url, body))
    return 0;

  while (jobState.load() != JOB_DONE)
  {
    if (millis() - startedMs > limitMs)
      return 0;  // ผลที่มาทีหลังจะถูก netSyncService() ทิ้ง
    delay(10);
  }
  const int code = job.code;
  finishJob();
  return code > 0 ? code : 0;
}
