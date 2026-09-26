#include "event_queue.h"
#include "flash_store.h"

#include <Preferences.h>

namespace {
// จองเต็มความจุของแบบไฟล์ ใช้จริงเท่า eventQueueCapacity()
PendingEvent queue[MAX_PENDING_EVENTS_FLASH];
uint8_t queueSize = 0;

Preferences storage;
bool storageReady = false;
uint32_t eventSeq = 0;
char idPrefix[8] = "esp";

constexpr char NVS_NAMESPACE[] = "pilldisp";
constexpr char NVS_QUEUE_KEY[] = "evq";
constexpr char NVS_COUNT_KEY[] = "evn";
constexpr char NVS_SEQ_KEY[] = "evseq";

// ไฟล์เดียวทั้งหัวและรายการ เขียนครั้งเดียวจบ
// (แบบ NVS เดิมเขียนจำนวนกับข้อมูลแยกกันสองครั้ง ไฟดับตรงกลาง = ขนาดไม่ตรง = ทิ้งทั้งคิว)
constexpr char QUEUE_FILE[] = "/events.bin";
constexpr uint32_t QUEUE_MAGIC = 0x50455131;  // "PEQ1"
struct QueueHeader {
  uint32_t magic;
  uint32_t eventSize;  // sizeof(PendingEvent) ตอนเขียน struct เปลี่ยนหลังอัปเดต = อ่านไม่ได้
  uint32_t count;
};

bool contains(const char *eventId)
{
  for (uint8_t i = 0; i < queueSize; ++i)
    if (strcmp(queue[i].eventId, eventId) == 0)
      return true;
  return false;
}

/** โหลดคิวแบบเดิมจาก NVS (firmware รุ่นก่อน) ใช้ตอนย้ายมาเก็บในไฟล์ และตอนไม่มีพาร์ทิชันไฟล์ */
uint8_t loadFromNvs(PendingEvent *out, uint8_t capacity)
{
  if (!storageReady)
    return 0;
  const uint8_t stored = storage.getUChar(NVS_COUNT_KEY, 0);
  if (stored == 0 || stored > MAX_PENDING_EVENTS || stored > capacity)
    return 0;
  const size_t expected = static_cast<size_t>(stored) * sizeof(PendingEvent);
  if (storage.getBytesLength(NVS_QUEUE_KEY) != expected)
  {
    // ขนาดไม่ตรง แปลว่า struct เปลี่ยนไปหลังอัปเดต firmware จึงทิ้งของเก่า
    storage.remove(NVS_QUEUE_KEY);
    storage.putUChar(NVS_COUNT_KEY, 0);
    return 0;
  }
  return storage.getBytes(NVS_QUEUE_KEY, out, expected) == expected ? stored : 0;
}

void clearNvsQueue()
{
  if (!storageReady)
    return;
  storage.remove(NVS_QUEUE_KEY);
  storage.putUChar(NVS_COUNT_KEY, 0);
}

bool loadFromFlash()
{
  const size_t size = flashStoreSize(QUEUE_FILE);
  if (size < sizeof(QueueHeader))
    return false;
  // อ่านทั้งไฟล์ลง buffer ชั่วคราวก่อนตรวจ ไม่เขียนทับคิวในหน่วยความจำจนกว่าจะแน่ใจว่าไฟล์ถูกต้อง
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer)
    return false;
  bool ok = flashStoreRead(QUEUE_FILE, buffer, size);
  QueueHeader header = {};
  if (ok)
  {
    memcpy(&header, buffer, sizeof(header));
    ok = header.magic == QUEUE_MAGIC && header.eventSize == sizeof(PendingEvent) &&
         header.count <= MAX_PENDING_EVENTS_FLASH &&
         size == sizeof(header) + header.count * sizeof(PendingEvent);
  }
  if (ok)
  {
    memcpy(queue, buffer + sizeof(header), header.count * sizeof(PendingEvent));
    queueSize = static_cast<uint8_t>(header.count);
  }
  free(buffer);
  return ok;
}

bool saveToFlash()
{
  const size_t size = sizeof(QueueHeader) + static_cast<size_t>(queueSize) * sizeof(PendingEvent);
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer)
    return false;
  QueueHeader header = {QUEUE_MAGIC, sizeof(PendingEvent), queueSize};
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), queue, static_cast<size_t>(queueSize) * sizeof(PendingEvent));
  const bool ok = flashStoreWrite(QUEUE_FILE, buffer, size);
  free(buffer);
  return ok;
}
}  // namespace

uint8_t eventQueueCapacity()
{
  return flashStoreReady() ? MAX_PENDING_EVENTS_FLASH : MAX_PENDING_EVENTS;
}

void eventQueueBegin()
{
  // MAC 3 ไบต์ท้ายทำให้ eventId ของเครื่องแต่ละตัวไม่ชนกัน
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(idPrefix,
           sizeof(idPrefix),
           "%02X%02X%02X",
           static_cast<uint8_t>(mac >> 16),
           static_cast<uint8_t>(mac >> 8),
           static_cast<uint8_t>(mac));

  queueSize = 0;
  storageReady = storage.begin(NVS_NAMESPACE, false);
  if (storageReady)
    eventSeq = storage.getULong(NVS_SEQ_KEY, 0);
  else
    Serial.println("NVS: เปิดไม่สำเร็จ");

  if (!flashStoreReady())
  {
    // ไม่มีพาร์ทิชันไฟล์: เก็บใน NVS แบบเดิมแต่จำกัดจำนวน กัน NVS เต็มจนส่วนอื่นเขียนไม่ได้
    queueSize = loadFromNvs(queue, MAX_PENDING_EVENTS);
  }
  else
  {
    loadFromFlash();

    // ย้ายผลที่ค้างอยู่ใน NVS จาก firmware รุ่นก่อนมาไว้ในไฟล์ ห้ามหายระหว่างอัปเดต
    PendingEvent legacy[MAX_PENDING_EVENTS];
    const uint8_t moved = loadFromNvs(legacy, MAX_PENDING_EVENTS);
    uint8_t added = 0;
    for (uint8_t i = 0; i < moved && queueSize < MAX_PENDING_EVENTS_FLASH; ++i)
    {
      if (contains(legacy[i].eventId))
        continue;  // ไฟดับกลางการย้ายรอบก่อน: มีในไฟล์แล้ว
      queue[queueSize++] = legacy[i];
      ++added;
    }
    // ลบของเดิมหลังเขียนไฟล์สำเร็จเท่านั้น ไฟดับระหว่างนี้ = ย้ายซ้ำรอบหน้า (กันซ้ำด้วย eventId)
    if (moved > 0 && (added == 0 || saveToFlash()))
      clearNvsQueue();
  }

  if (queueSize > 0)
    Serial.printf("[events] กู้ผลการจ่ายยาที่ยังไม่ได้ส่ง %u รายการ จะส่งเมื่อมีเน็ต\n", queueSize);
}

void eventQueueMakeId(char *out, size_t size)
{
  snprintf(out, size, "%s-%lu", idPrefix, static_cast<unsigned long>(++eventSeq));
  if (storageReady)
    storage.putULong(NVS_SEQ_KEY, eventSeq);
}

bool eventQueuePush(const PendingEvent &event)
{
  bool droppedOldest = false;
  const uint8_t capacity = eventQueueCapacity();

  if (queueSize >= capacity)
  {
    // คิวเต็ม (ออฟไลน์นานมาก): ทิ้งรายการเก่าสุดเพื่อให้ผลล่าสุดยังถูกเก็บไว้
    Serial.printf("[events] คิวผลที่ยังไม่ได้ส่งเต็ม %u รายการ ทิ้งรายการเก่าสุด (%s)\n",
                  capacity, queue[0].eventId);
    memmove(&queue[0], &queue[1], sizeof(PendingEvent) * (capacity - 1));
    queueSize = capacity - 1;
    droppedOldest = true;
  }

  queue[queueSize++] = event;
  eventQueuePersist();
  return !droppedOldest;
}

uint8_t eventQueueSize()
{
  return queueSize;
}

const PendingEvent &eventQueueAt(uint8_t index)
{
  return queue[index];
}

void eventQueueRemove(const char *eventId)
{
  for (uint8_t i = 0; i < queueSize; ++i)
  {
    if (strcmp(queue[i].eventId, eventId) != 0)
      continue;

    if (i + 1 < queueSize)
      memmove(&queue[i], &queue[i + 1], sizeof(PendingEvent) * (queueSize - i - 1));
    --queueSize;
    return;
  }
}

void eventQueuePersist()
{
  if (flashStoreReady())
  {
    if (!saveToFlash())
      Serial.println("[events] เขียนคิวลงแฟลชไม่สำเร็จ ถ้าไฟดับตอนนี้ผลล่าสุดอาจหาย");
    return;
  }

  if (!storageReady)
    return;
  storage.putUChar(NVS_COUNT_KEY, queueSize);
  if (queueSize == 0)
    storage.remove(NVS_QUEUE_KEY);
  else
    storage.putBytes(NVS_QUEUE_KEY, queue, static_cast<size_t>(queueSize) * sizeof(PendingEvent));
}
