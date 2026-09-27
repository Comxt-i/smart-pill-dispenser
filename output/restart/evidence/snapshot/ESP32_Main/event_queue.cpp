#include "event_queue.h"

#include <Preferences.h>

namespace {
PendingEvent queue[MAX_PENDING_EVENTS];
uint8_t queueSize = 0;

Preferences storage;
bool storageReady = false;
uint32_t eventSeq = 0;
char idPrefix[8] = "esp";

constexpr char NVS_NAMESPACE[] = "pilldisp";
constexpr char NVS_QUEUE_KEY[] = "evq";
constexpr char NVS_COUNT_KEY[] = "evn";
constexpr char NVS_SEQ_KEY[] = "evseq";
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

  storageReady = storage.begin(NVS_NAMESPACE, false);
  if (!storageReady)
  {
    Serial.println("NVS: เปิดไม่สำเร็จ คิวจะทำงานในหน่วยความจำอย่างเดียว");
    return;
  }

  eventSeq = storage.getULong(NVS_SEQ_KEY, 0);

  const uint8_t stored = storage.getUChar(NVS_COUNT_KEY, 0);
  if (stored == 0 || stored > MAX_PENDING_EVENTS)
    return;

  const size_t expected = static_cast<size_t>(stored) * sizeof(PendingEvent);
  if (storage.getBytesLength(NVS_QUEUE_KEY) != expected)
  {
    // ขนาดไม่ตรง แปลว่า struct เปลี่ยนไปหลังอัปเดต firmware จึงทิ้งของเก่า
    storage.remove(NVS_QUEUE_KEY);
    storage.putUChar(NVS_COUNT_KEY, 0);
    return;
  }

  storage.getBytes(NVS_QUEUE_KEY, queue, expected);
  queueSize = stored;
  Serial.printf("NVS: กู้คิวผลการจ่ายยาที่ค้างอยู่ %u รายการ\n", queueSize);
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

  if (queueSize >= MAX_PENDING_EVENTS)
  {
    // คิวเต็ม: ทิ้งรายการเก่าสุดเพื่อให้ผลล่าสุดยังถูกเก็บไว้
    memmove(&queue[0], &queue[1], sizeof(PendingEvent) * (MAX_PENDING_EVENTS - 1));
    queueSize = MAX_PENDING_EVENTS - 1;
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
  if (!storageReady)
    return;

  storage.putUChar(NVS_COUNT_KEY, queueSize);
  if (queueSize == 0)
    storage.remove(NVS_QUEUE_KEY);
  else
    storage.putBytes(NVS_QUEUE_KEY, queue, static_cast<size_t>(queueSize) * sizeof(PendingEvent));
}
