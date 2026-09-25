// ทดสอบการส่งงานเครือข่ายแบบไม่ block ระหว่าง loop หลักกับ task เบื้องหลัง
//
// คอมไพล์บนคอมไม่มี FreeRTOS งานจึงถูกทำทันทีตอนส่ง แต่ผลต้องรออยู่จนกว่า loop หลัก
// จะเรียก netSyncService() เหมือนบนเครื่องจริงทุกประการ ซึ่งคือสิ่งที่เทสต์นี้ตรวจ
#include <cassert>
#include <cstring>
#include "../ESP32_Main/net_sync.cpp"
#include "../ESP32_Main/command_journal.cpp"
#include "../ESP32_Main/schedule_cache.cpp"

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
uint32_t rtcDayKey() { return 20260926; }
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
  // server รุ่นเก่า (ไม่มี /wait): ต้องถามถี่เอง ช้ากว่า SYNC_INTERVAL_MS ไม่ได้
  assert(pollIntervalFromServer(60, false) == SYNC_INTERVAL_MS);
  assert(pollIntervalFromServer(4000000000UL, false) == SYNC_INTERVAL_MS);  // คูณ 1000 แล้วต้องไม่ล้นวนกลับ
  // มี /wait: server บอกเองทันที sync เต็มจึงห่างได้ตามที่ server ขอ แต่ไม่เกินเพดาน
  assert(pollIntervalFromServer(300, true) == 300000UL);
  assert(pollIntervalFromServer(4000000000UL, true) == FULL_SYNC_MAX_MS);
  assert(pollIntervalFromServer(0, true) == 2000UL);  // ไม่ยิงรัวถี่กว่า 2 วินาที
  assert(pollIntervalFromServer(1, false) == 2000UL);
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

  // ======================= ตารางทั้งสัปดาห์ในเครื่อง =======================

  // วันในสัปดาห์จากนาฬิกาเครื่อง ต้องตรงกับที่ server ใช้
  assert(strcmp(dayCodeForEpoch(0), "THU") == 0);          // 1 ม.ค. 1970
  assert(strcmp(dayCodeForEpoch(86399), "THU") == 0);      // วินาทีสุดท้ายของวันเดียวกัน
  assert(strcmp(dayCodeForEpoch(86400), "FRI") == 0);
  assert(strcmp(dayCodeForEpoch(1704067200UL), "MON") == 0);  // 1 ม.ค. 2024 เป็นวันจันทร์
  assert(strcmp(dayCodeForEpoch(1790380800UL), "SAT") == 0);  // 26 ก.ย. 2026 เป็นวันเสาร์

  // มื้อไหนต้องกินวันนี้
  assert(doseRunsToday("", "WED"));            // ไม่ระบุวัน = ทุกวัน
  assert(doseRunsToday(nullptr, "WED"));       // server รุ่นเก่า ไม่ส่ง days มาเลย
  assert(doseRunsToday("MON,WED,FRI", "WED"));
  assert(!doseRunsToday("MON,WED,FRI", "TUE"));
  assert(doseRunsToday("MON, WED", "WED"));    // มีช่องว่างหลังจุลภาค
  assert(!doseRunsToday("MON", ""));           // ไม่รู้ว่าวันนี้วันอะไร: ไม่เดา

  // มื้อไหนถือว่าจบแล้ว (กันจ่ายซ้ำ)
  scheduleCacheBegin();
  assert(!doseAlreadyHandled(false, true, "dose-a", 20260926));
  assert(doseAlreadyHandled(true, true, "dose-a", 20260926));    // server บอกว่ากินแล้ววันนี้
  assert(!doseAlreadyHandled(true, false, "dose-a", 20260926));  // ข้อมูลของเมื่อวาน: ใช้ไม่ได้
  scheduleCacheMarkClosed("dose-a", 20260926);                  // กินแล้วแต่ยังส่งขึ้น server ไม่ทัน
  assert(doseAlreadyHandled(false, true, "dose-a", 20260926));
  assert(doseAlreadyHandled(false, false, "dose-a", 20260926));  // ออฟไลน์หลังรีบูต: ยังจำได้
  assert(!doseAlreadyHandled(false, true, "dose-a", 20260927));  // พรุ่งนี้เป็นมื้อใหม่

  // ======================= server บอกเครื่องทันที (/wait) =======================

  // server รุ่นใหม่ส่ง state_version มากับ sync (ArduinoJson ปลอมอ่านค่าไม่ได้ จึงตั้งตรงๆ)
  testNow += FULL_SYNC_MAX_MS;
  netSyncService();
  assert(jobState.load() == JOB_IDLE);
  copyText(stateVersion, sizeof(stateVersion), "abc12345");
  waitSupported = true;
  nextSyncAtMs = testNow + 300000UL;  // sync เต็มครั้งถัดไปอีกนาน
  nextWaitAtMs = testNow;

  // ไม่ถึงรอบ sync: เปิดสายรอ server แทนการถามรัว
  fakeHttp.nextCode = 200;
  int requestsBefore = fakeHttp.requests;
  netSyncPump();
  assert(fakeHttp.requests == requestsBefore + 1);
  assert(lastUrlHas("/api/device/wait?state=abc12345&timeout_sec=15"));
  // HTTP timeout ต้องยาวกว่าที่ server ถือสายไว้ ไม่งั้นกล่องตัดสายเองก่อน server ตอบ
  assert(job.timeoutMs > WAIT_TIMEOUT_SEC * 1000UL);
  netSyncPump();  // มีงานค้าง: ห้ามเปิดสายซ้อน
  assert(fakeHttp.requests == requestsBefore + 1);

  // server ตอบว่ามีอะไรเปลี่ยน (ArduinoJson ปลอมให้ค่าตั้งต้น = ถือว่าเปลี่ยน ซึ่งเป็นทางที่ปลอดภัย)
  netSyncService();
  assert(netSyncDue());  // sync ทันที ไม่รอรอบ 5 นาที
  netSyncPump();
  assert(lastUrlHas("/api/device/sync?schema=2"));  // ขอตารางทั้งสัปดาห์
  netSyncService();

  // server ตอบว่าไม่มีอะไรเปลี่ยน: เปิดสายรอใหม่ทันที โดยไม่ sync
  copyText(stateVersion, sizeof(stateVersion), "abc12345");
  waitSupported = true;
  nextSyncAtMs = testNow + 300000UL;
  onWaitAnswer(false);
  assert(!netSyncDue());
  requestsBefore = fakeHttp.requests;
  netSyncPump();
  assert(lastUrlHas("/api/device/wait") && fakeHttp.requests == requestsBefore + 1);

  // /wait ล้มเหลว: ถอยไปก่อน ไม่ยิงซ้ำรัว
  // (บนคอมงานถูกทำทันทีตอนส่ง จึงต้องตั้งผลปลอมก่อนส่ง ไม่ใช่หลัง)
  netSyncService();  // เก็บผลของสายก่อนหน้าให้จบ
  nextSyncAtMs = testNow + 300000UL;
  nextWaitAtMs = testNow;
  fakeHttp.nextCode = 502;
  netSyncPump();
  assert(lastUrlHas("/api/device/wait"));
  netSyncService();
  requestsBefore = fakeHttp.requests;
  netSyncPump();
  assert(fakeHttp.requests == requestsBefore);
  testNow += SYNC_RETRY_MS;
  fakeHttp.nextCode = 200;
  netSyncPump();
  assert(lastUrlHas("/api/device/wait") && fakeHttp.requests == requestsBefore + 1);
  netSyncService();

  // server ที่ยังไม่มี /wait (404): กลับไปถามเป็นรอบทันที และไม่ลอง /wait ซ้ำไปอีกนาน
  copyText(stateVersion, sizeof(stateVersion), "abc12345");
  waitSupported = true;
  nextSyncAtMs = testNow + 300000UL;
  nextWaitAtMs = testNow;
  fakeHttp.nextCode = 404;
  netSyncPump();
  assert(lastUrlHas("/api/device/wait"));
  netSyncService();
  assert(!waitSupported && netSyncDue());
  fakeHttp.nextCode = 200;
  netSyncPump();
  assert(lastUrlHas("/api/device/sync"));
  netSyncService();
  copyText(stateVersion, sizeof(stateVersion), "abc12345");
  waitSupported = true;  // เช่น server ถูกอัปเดตแล้ว sync ถัดไปได้ state_version มา
  nextSyncAtMs = testNow + 300000UL;
  requestsBefore = fakeHttp.requests;
  netSyncPump();
  assert(fakeHttp.requests == requestsBefore);  // ยังอยู่ในช่วงพักหลัง 404
  testNow += WAIT_UNSUPPORTED_RETRY_MS;
  nextSyncAtMs = testNow + 300000UL;
  netSyncPump();
  assert(lastUrlHas("/api/device/wait"));
  netSyncService();

  return 0;
}
