// นาฬิกาที่ใช้ตรวจใบรับรอง HTTPS ต้องตามเวลาเครื่องเสมอ
//
// เดิมตั้งครั้งเดียวตอนบูต: ถ้าบูตมาด้วยเวลา RTC ผิด แล้วผู้ใช้แก้เวลาจากมือถือทีหลัง
// จอบอกเวลาถูก แต่ HTTPS ยังใช้เวลาผิด ต่อ server ไม่ได้เลย เว็บเห็นกล่อง OFFLINE ตลอด
//
// บนคอม time() คือเวลาจริงของคอม จึงคุมด้วยเวลา RTC ปลอมแทน แล้วดูว่า settimeofday ถูกเรียกด้วยค่าอะไร
#include <cassert>
#include <cstring>
#include "../ESP32_Main/net_sync.cpp"
#include "../ESP32_Main/command_journal.cpp"
#include "../ESP32_Main/flash_store.cpp"
#include "../ESP32_Main/schedule_cache.cpp"

FakeHttp fakeHttp;
SerialClass Serial;
WiFiClass WiFi;
unsigned long testNow = 1000;
unsigned long millis() { return testNow; }
void delay(unsigned long ms) { testNow += ms; }

int clockSets = 0;
time_t lastSet = 0;
int settimeofday(const struct timeval *tv, const struct timezone *)
{
  ++clockSets;
  lastSet = tv->tv_sec;
  return 0;
}

uint32_t fakeRtc = 0;
uint32_t rtcLocalEpoch() { return fakeRtc; }
uint32_t rtcDayKey() { return 20260927; }
void rtcSyncFromEpoch(uint32_t) {}
void scheduleBeginSync() {}
void scheduleCommitSync() {}
int scheduleStageSlot(uint8_t, bool, const char *, const char *, float) { return 0; }
void scheduleStageDose(int, const char *, const char *, int, bool) {}
void scheduleStageSlotPillHole(int, int8_t) {}
uint8_t eventQueueSize() { return 0; }
PendingEvent pendingEvent = {};
const PendingEvent &eventQueueAt(uint8_t) { return pendingEvent; }
void eventQueueRemove(const char *) {}
void eventQueuePersist() {}

int main()
{
  assert(usesTls());
  const time_t pcUtc = time(nullptr);
  assert(pcUtc > MIN_VALID_EPOCH);
  const uint32_t localNow = static_cast<uint32_t>(pcUtc + tzOffsetMinutes * 60);

  // เวลาเครื่องตรงกับนาฬิการะบบ: ไม่ต้องแตะ
  fakeRtc = localNow;
  assert(systemClockReadyForTls() && clockSets == 0);
  fakeRtc = localNow + 60;  // คลาดไม่กี่นาที ใบรับรองไม่สนใจ ไม่ต้องตั้งซ้ำทุกรอบ
  assert(systemClockReadyForTls() && clockSets == 0);

  // นาฬิการะบบผิดจากเวลาเครื่องมาก (บูตด้วย RTC ผิดแล้วแก้เวลาทีหลัง): ต้องตามเวลาเครื่อง
  fakeRtc = localNow + 3 * 86400;
  assert(systemClockReadyForTls() && clockSets == 1);
  assert(lastSet == static_cast<time_t>(fakeRtc) - tzOffsetMinutes * 60);  // ระบบเก็บ UTC
  fakeRtc = localNow - 30 * 86400;
  assert(systemClockReadyForTls() && clockSets == 2);
  assert(lastSet == static_cast<time_t>(fakeRtc) - tzOffsetMinutes * 60);

  // เครื่องยังไม่มีเวลาเลย แต่นาฬิการะบบมีแล้ว (ได้จาก NTP): ใช้ต่อได้ ไม่ทับด้วยค่าว่าง
  fakeRtc = 0;
  assert(systemClockReadyForTls() && clockSets == 2);

  // หน้าจอได้รหัสสั้นภาษาอังกฤษของเหตุที่ล้มเหลว
  assert(netSyncLastErrorShort()[0] == '\0');
  describeFailure("sync", -1);
  assert(strcmp(netSyncLastErrorShort(), "CONN -1") == 0);
  describeFailure("sync", 401);
  assert(strcmp(netSyncLastErrorShort(), "HTTP 401") == 0);
  describeFailure("sync", JOB_ERR_CLOCK);
  assert(strcmp(netSyncLastErrorShort(), "NO CLOCK") == 0);
  describeFailure("sync", JOB_ERR_NO_WIFI);
  assert(strcmp(netSyncLastErrorShort(), "WIFI DROP") == 0);
}
