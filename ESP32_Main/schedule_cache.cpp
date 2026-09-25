#include "schedule_cache.h"
#include "config.h"

#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

namespace {

// ---- ตารางยาชุดล่าสุด ----
//
// เก็บหัว + เนื้อหาใน blob เดียว เขียนครั้งเดียวจบ ไฟดับกลางคันหัวกับเนื้อหาจึงไม่เพี้ยนไม่ตรงกัน
constexpr uint32_t BODY_MAGIC = 0x50534331;  // "PSC1"
// คำตอบของสามช่อง มื้อละไม่กี่สิบไบต์ ปกติไม่ถึง 3 KB เผื่อไว้สองเท่า
// เกินนี้ไม่เก็บ (NVS ทั้งพาร์ทิชันมีแค่ราว 20 KB ต้องแบ่งกับ Wi-Fi คิวผล และบันทึกคำสั่ง)
constexpr size_t MAX_BODY_BYTES = 6144;

struct BodyHeader {
  uint32_t magic;
  uint32_t dayKey;
  char stateVersion[12];
  uint32_t length;  // ไม่รวม '\0' ท้าย
};

Preferences bodyStore;
bool bodyReady = false;
char storedVersion[12] = "";
uint32_t storedDay = 0;

// ---- มื้อที่จบแล้ววันนี้ ----
constexpr uint32_t CLOSED_MAGIC = 0x50434C31;  // "PCL1"
constexpr uint8_t MAX_CLOSED = DISPENSER_COUNT * MAX_DOSES_PER_SLOT;

struct ClosedToday {
  uint32_t magic;
  uint32_t dayKey;
  uint8_t count;
  char ids[MAX_CLOSED][40];
};

Preferences closedStore;
bool closedReady = false;
ClosedToday closed = {};

void copyCacheText(char *dest, size_t size, const char *source)
{
  strncpy(dest, source ? source : "", size - 1);
  dest[size - 1] = '\0';
}

/** อ่าน blob ทั้งก้อนมาไว้ใน buffer ที่จองใหม่ ผู้เรียกต้อง free() เอง */
uint8_t *readBody(size_t &size)
{
  size = bodyReady ? bodyStore.getBytesLength("payload") : 0;
  if (size < sizeof(BodyHeader) + 1 || size > sizeof(BodyHeader) + MAX_BODY_BYTES + 1)
    return nullptr;
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer)
    return nullptr;
  if (bodyStore.getBytes("payload", buffer, size) != size)
  {
    free(buffer);
    return nullptr;
  }
  BodyHeader header;
  memcpy(&header, buffer, sizeof(header));
  // ข้อมูลเสียหายหรือจากเฟิร์มแวร์รุ่นอื่น ถือว่าไม่มี ดีกว่าเอาไปใช้ผิดๆ
  if (header.magic != BODY_MAGIC || header.length + sizeof(header) + 1 != size ||
      buffer[size - 1] != '\0')
  {
    free(buffer);
    return nullptr;
  }
  return buffer;
}

}  // namespace

void scheduleCacheBegin()
{
  bodyReady = bodyStore.begin("pillsched", false);
  closedReady = closedStore.begin("pillclosed", false);

  storedVersion[0] = '\0';
  storedDay = 0;
  size_t size = 0;
  uint8_t *buffer = readBody(size);
  if (buffer)
  {
    BodyHeader header;
    memcpy(&header, buffer, sizeof(header));
    copyCacheText(storedVersion, sizeof(storedVersion), header.stateVersion);
    storedDay = header.dayKey;
    free(buffer);
    Serial.printf("[cache] มีตารางยาในเครื่อง (%s, %u ไบต์)\n", storedVersion,
                  static_cast<unsigned>(header.length));
  }

  closed = {};
  if (closedReady && closedStore.getBytesLength("today") == sizeof(closed))
    closedStore.getBytes("today", &closed, sizeof(closed));
  if (closed.magic != CLOSED_MAGIC || closed.count > MAX_CLOSED)
    closed = {};
  for (uint8_t i = 0; i < closed.count; ++i)
    closed.ids[i][sizeof(closed.ids[i]) - 1] = '\0';
}

bool scheduleCacheStore(const String &body, const char *stateVersion, uint32_t dayKey)
{
  if (!bodyReady || !stateVersion || stateVersion[0] == '\0')
    return false;
  if (strcmp(stateVersion, storedVersion) == 0 && dayKey == storedDay)
    return false;  // ไม่มีอะไรใหม่ ไม่เขียน NVS ให้สึกหรอเปล่าๆ

  const size_t length = strlen(body.c_str());
  if (length == 0 || length > MAX_BODY_BYTES)
  {
    Serial.printf("[cache] ตารางยาใหญ่เกินจะเก็บในเครื่อง (%u ไบต์)\n", static_cast<unsigned>(length));
    return false;
  }

  const size_t size = sizeof(BodyHeader) + length + 1;
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer)
    return false;
  BodyHeader header = {};
  header.magic = BODY_MAGIC;
  header.dayKey = dayKey;
  copyCacheText(header.stateVersion, sizeof(header.stateVersion), stateVersion);
  header.length = static_cast<uint32_t>(length);
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), body.c_str(), length + 1);

  const bool ok = bodyStore.putBytes("payload", buffer, size) == size;
  free(buffer);
  if (!ok)
  {
    Serial.println("[cache] เขียนตารางยาลง NVS ไม่สำเร็จ");
    return false;
  }
  copyCacheText(storedVersion, sizeof(storedVersion), stateVersion);
  storedDay = dayKey;
  return true;
}

bool scheduleCacheLoad(String &body, uint32_t &fetchedDayKey)
{
  size_t size = 0;
  uint8_t *buffer = readBody(size);
  if (!buffer)
    return false;
  BodyHeader header;
  memcpy(&header, buffer, sizeof(header));
  body = String(reinterpret_cast<const char *>(buffer + sizeof(header)));
  fetchedDayKey = header.dayKey;
  free(buffer);
  return true;
}

const char *scheduleCacheStateVersion()
{
  return storedVersion;
}

void scheduleCacheMarkClosed(const char *scheduleId, uint32_t dayKey)
{
  if (!scheduleId || scheduleId[0] == '\0' || dayKey == 0)
    return;
  if (closed.magic != CLOSED_MAGIC || closed.dayKey != dayKey)
  {
    // วันใหม่: รายการของเมื่อวานไม่เกี่ยวแล้ว
    closed = {};
    closed.magic = CLOSED_MAGIC;
    closed.dayKey = dayKey;
  }
  if (scheduleCacheIsClosed(scheduleId, dayKey))
    return;
  if (closed.count >= MAX_CLOSED)
    return;  // เกินจำนวนมื้อที่เครื่องรองรับต่อวันอยู่แล้ว เป็นไปไม่ได้ในทางปฏิบัติ

  copyCacheText(closed.ids[closed.count], sizeof(closed.ids[closed.count]), scheduleId);
  ++closed.count;
  if (!closedReady || closedStore.putBytes("today", &closed, sizeof(closed)) != sizeof(closed))
    Serial.println("[cache] บันทึกมื้อที่จบแล้วลง NVS ไม่สำเร็จ ถ้าไฟดับตอนนี้อาจเตือนมื้อนี้ซ้ำ");
}

bool scheduleCacheIsClosed(const char *scheduleId, uint32_t dayKey)
{
  if (!scheduleId || closed.magic != CLOSED_MAGIC || closed.dayKey != dayKey)
    return false;
  for (uint8_t i = 0; i < closed.count; ++i)
    if (strcmp(closed.ids[i], scheduleId) == 0)
      return true;
  return false;
}
