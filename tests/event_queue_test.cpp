// รับยาตอนออฟไลน์ ผลต้องไม่หาย: รอดการรีบูต ย้ายจาก firmware รุ่นเก่าได้ ไฟดับกลางการเขียนไม่เสีย
// เรียก flashStoreBegin() + eventQueueBegin() ซ้ำ = จำลองเปิดเครื่องใหม่
#include <cassert>
#include <cstring>
#include "../ESP32_Main/flash_store.cpp"
#include "../ESP32_Main/event_queue.cpp"

SerialClass Serial;
EspClass ESP;

static PendingEvent makeEvent(unsigned n)
{
  PendingEvent e = {};
  snprintf(e.eventId, sizeof(e.eventId), "evt-%u", n);
  strcpy(e.status, "DISPENSED");
  snprintf(e.scheduleId, sizeof(e.scheduleId), "sch-%u", n);
  e.slot = 1;
  e.amount = 1;
  e.localEpoch = 1790380800UL + n * 60;  // เวลาที่รับยาจริง ไม่ใช่เวลาที่ส่งขึ้น server
  return e;
}

static void reboot()
{
  flashStoreBegin();
  eventQueueBegin();
}

static bool ids(std::initializer_list<unsigned> expected)
{
  if (eventQueueSize() != expected.size())
    return false;
  uint8_t i = 0;
  for (unsigned n : expected)
  {
    char id[24];
    snprintf(id, sizeof(id), "evt-%u", n);
    if (strcmp(eventQueueAt(i++).eventId, id) != 0)
      return false;
  }
  return true;
}

int main()
{
  // ---------------- มีพาร์ทิชันไฟล์ (ตาราง default ของ Arduino) ----------------
  reboot();
  assert(eventQueueSize() == 0 && eventQueueCapacity() == MAX_PENDING_EVENTS_FLASH);

  // รับยาสามมื้อตอนเน็ตหลุด แล้วไฟดับ
  for (unsigned n = 1; n <= 3; ++n)
    assert(eventQueuePush(makeEvent(n)));
  reboot();
  assert(ids({1, 2, 3}));
  assert(eventQueueAt(0).localEpoch == 1790380800UL + 60);  // เวลาเดิมที่รับยา ไม่เปลี่ยน

  // เน็ตกลับมา server รับไปหนึ่งรายการ -> ต้องไม่กลับมาอีกหลังรีบูต
  eventQueueRemove("evt-2");
  eventQueuePersist();
  reboot();
  assert(ids({1, 3}));

  // ไฟดับระหว่างเขียน (ก่อนเปลี่ยนชื่อไฟล์ทับ): ต้องได้ของเดิมครบ ไม่ใช่ไฟล์เสีย
  fakeFlash().renameOk = false;
  eventQueuePush(makeEvent(4));
  fakeFlash().renameOk = true;
  reboot();
  assert(ids({1, 3}));

  // แฟลชเต็ม เขียนได้ไม่ครบ: ของเดิมยังอยู่ครบเช่นกัน
  fakeFlash().writeOk = false;
  eventQueuePush(makeEvent(5));
  fakeFlash().writeOk = true;
  reboot();
  assert(ids({1, 3}));

  // ออฟไลน์นานมาก: เก็บได้ถึงความจุ เกินแล้วทิ้งเก่าสุดและบอกผู้เรียก
  eventQueueRemove("evt-1");
  eventQueueRemove("evt-3");
  for (unsigned n = 100; n < 100 + MAX_PENDING_EVENTS_FLASH; ++n)
    assert(eventQueuePush(makeEvent(n)));
  assert(!eventQueuePush(makeEvent(999)));
  assert(eventQueueSize() == MAX_PENDING_EVENTS_FLASH);
  assert(strcmp(eventQueueAt(0).eventId, "evt-101") == 0);
  assert(strcmp(eventQueueAt(MAX_PENDING_EVENTS_FLASH - 1).eventId, "evt-999") == 0);
  reboot();
  assert(eventQueueSize() == MAX_PENDING_EVENTS_FLASH);

  // ---------------- อัปเดตจาก firmware รุ่นก่อน (ผลค้างอยู่ใน NVS) ----------------
  fakeFlash().files.clear();
  PendingEvent legacy[2] = {makeEvent(7), makeEvent(8)};
  storage.putUChar("evn", 2);
  storage.putBytes("evq", legacy, sizeof(legacy));
  reboot();
  assert(ids({7, 8}));                        // ย้ายมาครบ
  assert(storage.getUChar("evn", 0) == 0);    // คืนที่ให้ NVS แล้ว
  reboot();
  assert(ids({7, 8}));                        // ไม่ซ้ำหลังรีบูต

  // ไฟดับกลางการย้ายรอบก่อน: ไฟล์มีแล้ว NVS ยังไม่ถูกลบ -> ต้องไม่ซ้ำ
  storage.putUChar("evn", 2);
  storage.putBytes("evq", legacy, sizeof(legacy));
  reboot();
  assert(ids({7, 8}) && storage.getUChar("evn", 0) == 0);

  // ย้ายไม่สำเร็จ (แฟลชเขียนไม่ได้) ห้ามลบของเดิมใน NVS
  fakeFlash().files.clear();
  storage.putUChar("evn", 2);
  storage.putBytes("evq", legacy, sizeof(legacy));
  fakeFlash().writeOk = false;
  reboot();
  fakeFlash().writeOk = true;
  assert(storage.getUChar("evn", 0) == 2);
  reboot();
  assert(ids({7, 8}) && storage.getUChar("evn", 0) == 0);

  // ---------------- ไม่มีพาร์ทิชันไฟล์: NVS แบบจำกัดจำนวน ----------------
  fakeFlash().mountOk = false;
  fakeFlash().files.clear();
  storage.remove("evq");
  storage.putUChar("evn", 0);
  reboot();
  assert(eventQueueSize() == 0 && eventQueueCapacity() == MAX_PENDING_EVENTS);
  for (unsigned n = 1; n <= 3; ++n)
    eventQueuePush(makeEvent(n));
  reboot();
  assert(ids({1, 2, 3}));
  for (unsigned n = 10; n < 10 + MAX_PENDING_EVENTS; ++n)
    eventQueuePush(makeEvent(n));
  assert(eventQueueSize() == MAX_PENDING_EVENTS);  // ไม่เกินที่ NVS รับได้
  return 0;
}
