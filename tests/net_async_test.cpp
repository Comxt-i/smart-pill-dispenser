// ทดสอบการส่งงานเครือข่ายแบบไม่ block ระหว่าง loop หลักกับ task เบื้องหลัง
//
// คอมไพล์บนคอมไม่มี FreeRTOS งานจึงถูกทำทันทีตอนส่ง แต่ผลต้องรออยู่จนกว่า loop หลัก
// จะเรียก netSyncService() เหมือนบนเครื่องจริงทุกประการ ซึ่งคือสิ่งที่เทสต์นี้ตรวจ
#include <cassert>
#include <cstring>
#include "../ESP32_Main/net_sync.cpp"
#include "../ESP32_Main/command_journal.cpp"

FakeHttp fakeHttp;
SerialClass Serial;
WiFiClass WiFi;
unsigned long testNow = 1000;
unsigned long millis() { return testNow; }
void delay(unsigned long ms) { testNow += ms; }
#ifdef _WIN32
int settimeofday(const struct timeval *, const struct timezone *) { return 0; }
#endif

// โมดูลอื่นที่ net_sync เรียก: นับไว้พอให้รู้ว่าผลถูกนำไปใช้ "เมื่อไร"
int syncCommits = 0, queueRemovals = 0, queuePersists = 0;
uint8_t queued = 0;
PendingEvent pendingEvent = {};
uint32_t rtcLocalEpoch() { return 1800000000; }
void rtcSyncFromEpoch(uint32_t) {}
void scheduleBeginSync() {}
void scheduleCommitSync() { ++syncCommits; }
int scheduleStageSlot(uint8_t, bool, const char *, const char *, float) { return 0; }
void scheduleStageDose(int, const char *, const char *, int, bool) {}
void scheduleStageSlotPillHole(int, int8_t) {}
uint8_t eventQueueSize() { return queued; }
const PendingEvent &eventQueueAt(uint8_t) { return pendingEvent; }
void eventQueueRemove(const char *) { ++queueRemovals; }
void eventQueuePersist() { ++queuePersists; }

static bool lastUrlHas(const char *part)
{
  return !fakeHttp.urls.empty() && strstr(fakeHttp.urls.back().c_str(), part) != nullptr;
}

int main()
{
  netSyncBegin();

  // ---- กฎความถี่: ช้ากว่า SYNC_INTERVAL_MS ไม่ได้ ไม่ว่า server จะสั่งมาเท่าไร ----
  assert(pollIntervalFromServer(60) == SYNC_INTERVAL_MS);  // server รุ่นเก่าสั่ง 60 วินาที
  assert(pollIntervalFromServer(0) == 2000UL);             // ไม่ยิงรัวถี่กว่า 2 วินาที
  assert(pollIntervalFromServer(1) == 2000UL);
  assert(pollIntervalFromServer(4000000000UL) == SYNC_INTERVAL_MS);  // คูณ 1000 แล้วต้องไม่ล้นวนกลับ
  assert(SYNC_INTERVAL_MS <= 10000UL);  // แก้บนเว็บแล้วจอต้องเปลี่ยนในไม่กี่วินาที

  // ---- ขอตาราง: ส่งงานแล้วกลับทันที ผลยังไม่ถูกใช้จนกว่า loop จะมารับ ----
  assert(netSyncDue());
  assert(netSyncFetch());
  assert(fakeHttp.requests == 1 && lastUrlHas("/api/device/sync"));
  assert(syncCommits == 0);  // ยังไม่แตะตารางยาจากที่อื่นนอก loop หลัก

  // มีงานค้างอยู่ ห้ามส่งซ้อน ทั้ง sync และ events
  // เดินเวลาเลยรอบถัดไปก่อน ไม่อย่างนั้น "ยังไม่ถึงเวลา" จะบังว่าจริงๆ กันด้วยงานค้างหรือเปล่า
  testNow += SYNC_INTERVAL_MS + SYNC_RETRY_MS;
  assert(!netSyncDue());

  // ด่านชั้นในสุด: ส่งงานทับงานที่ยังไม่ถูกรับผล ต้องถูกปฏิเสธและห้ามแก้ของเดิม
  assert(!submitJob(JobKind::Events, true, "http://overwrite/", String("x")));
  assert(job.kind == JobKind::Sync && strstr(job.url, "/api/device/sync") != nullptr);
  assert(!netSyncFetch());
  queued = 1;
  assert(!netSyncFlushEvents());
  assert(fakeHttp.requests == 1);

  netSyncService();  // loop หลักมารับผล
  assert(syncCommits == 1 && netSyncLastCallOk());
  netSyncService();  // เรียกซ้ำต้องไม่นำผลเดิมไปใช้อีก
  assert(syncCommits == 1);

  // รอบถัดไปภายใน SYNC_INTERVAL_MS ไม่ใช่ 60 วินาที
  assert(!netSyncDue());
  testNow += SYNC_INTERVAL_MS;
  assert(netSyncDue());

  // ---- ส่งผลการจ่ายยา: ลบออกจากคิวเฉพาะตอนนำคำตอบไปใช้ ----
  queued = 1;
  strcpy(pendingEvent.eventId, "evt-1");
  strcpy(pendingEvent.status, "DISPENSED");
  fakeHttp.nextCode = 503;
  assert(netSyncFlushEvents());
  assert(lastUrlHas("/api/device/events") && fakeHttp.posted.size() == 1);
  netSyncService();
  assert(queueRemovals == 0 && queuePersists == 0);  // server ไม่รับ = ต้องเก็บไว้ส่งใหม่
  assert(strstr(netSyncLastError(), "503") != nullptr);
  queued = 0;

  // ---- sync ล้มเหลว: ไม่แตะตาราง บอกสาเหตุ แล้วลองใหม่เร็วกว่ารอบปกติ ----
  fakeHttp.nextCode = 500;
  assert(netSyncFetch());
  netSyncService();
  assert(syncCommits == 1 && !netSyncLastCallOk());
  assert(strstr(netSyncLastError(), "500") != nullptr);
  assert(!netSyncDue());
  testNow += SYNC_RETRY_MS;
  assert(netSyncDue());

  // ---- Wi-Fi หลุด: ไม่ส่งอะไรเลย ----
  const int before = fakeHttp.requests;
  WiFi.connectionStatus = 0;
  assert(!netSyncFetch() && !netSyncDue());
  assert(fakeHttp.requests == before);
  WiFi.connectionStatus = WL_CONNECTED;

  // ---- ยืนยันรหัสตั้งค่า: รอผลได้ และได้รหัส HTTP ตรงตามจริง ----
  fakeHttp.nextCode = 409;
  assert(netSyncCompleteSetup("0123456789ABCDEF0123") == 409);
  assert(lastUrlHas("/api/device/setup/complete"));
  fakeHttp.nextCode = 200;
  assert(netSyncCompleteSetup("0123456789ABCDEF0123") == 200);

  // มีผล sync รออยู่ตอนยืนยันรหัส: ต้องนำผลนั้นไปใช้ให้จบก่อน ไม่ใช่ทิ้งหรือค้าง
  testNow += SYNC_INTERVAL_MS;
  assert(netSyncFetch());
  const int commitsBefore = syncCommits;
  assert(netSyncCompleteSetup("0123456789ABCDEF0123") == 200);
  assert(syncCommits == commitsBefore + 1);
  testNow += SYNC_INTERVAL_MS;
  assert(netSyncFetch());  // ไม่มีงานค้างหลงเหลือ
  netSyncService();

  // เชื่อมต่อไม่ได้เลย (HTTPClient คืนค่าติดลบ): ยืนยันรหัสต้องคืน 0 ไม่ใช่เลขติดลบ
  fakeHttp.nextCode = -1;
  assert(netSyncCompleteSetup("0123456789ABCDEF0123") == 0);
  return 0;
}
