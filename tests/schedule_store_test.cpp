// ทดสอบตรรกะตารางยาด้วยเวลาและข้อมูลจำลอง โดยไม่ต้องใช้บอร์ดจริง
#include "config.h"
#include "schedule_store.h"

#include <cassert>
#include <string>
#include <vector>

unsigned long fakeMillis = 0;
int cancelLevel = HIGH;
FakeSerial Serial;

namespace {

struct Transition {
  std::string scheduleId;
  DoseState previous;
  DoseState next;
};

std::vector<Transition> transitions;

void recordTransition(const DoseRef &ref, DoseState previous, DoseState next)
{
  const Dose *dose = scheduleDoseAt(ref);
  transitions.push_back({dose ? dose->scheduleId : "?", previous, next});
}

int countTo(DoseState state)
{
  int total = 0;
  for (const Transition &t : transitions)
    if (t.next == state)
      ++total;
  return total;
}

DoseState stateOf(const char *scheduleId)
{
  const DoseRef ref = scheduleFindByScheduleId(scheduleId);
  const Dose *dose = scheduleDoseAt(ref);
  assert(dose && "ไม่พบมื้อยาที่ระบุ");
  return dose->state;
}

/** ตารางมาตรฐานที่ใช้ในเกือบทุกเคส: ช่อง 1 มีมื้อ 08:00 และ 12:00, ช่อง 2 มีมื้อ 18:00 */
void stageStandardSchedule(bool slot1Active = true, bool doseDoneOnServer = false)
{
  scheduleBeginSync();

  const int slot1 = scheduleStageSlot(1, slot1Active, "med-a", "Paracetamol", 2.0f);
  scheduleStageDose(slot1, "sch-morning", "morning", 8 * 60, doseDoneOnServer);
  scheduleStageDose(slot1, "sch-noon", "noon", 12 * 60, false);

  const int slot2 = scheduleStageSlot(2, true, "med-b", "Amoxicillin", 1.0f);
  scheduleStageDose(slot2, "sch-evening", "evening", 18 * 60, false);

  // ช่อง 3 ว่าง ต้องไม่ถูกนำมาคิดเป็นมื้อยา
  scheduleStageSlot(3, false, "", "", 0.0f);

  scheduleCommitSync();
}

constexpr uint32_t DAY_ONE = 20260917;
constexpr uint32_t DAY_TWO = 20260918;

void resetStore()
{
  transitions.clear();
  scheduleBegin(recordTransition);
}

// ---------------------------------------------------------------------------

void testStagingAndLookup()
{
  resetStore();
  assert(!scheduleHasData());

  stageStandardSchedule();
  assert(scheduleHasData());
  assert(scheduleSlotCount() == 3);
  assert(scheduleFindSlot(2) == 1);
  assert(scheduleFindSlot(9) == -1);

  const DoseRef ref = scheduleFindByScheduleId("sch-evening");
  assert(ref.valid);
  assert(scheduleSlotOf(ref)->number == 2);
  assert(scheduleDoseAt(ref)->minutes == 18 * 60);

  assert(!scheduleFindByScheduleId("ไม่มีอยู่จริง").valid);
  assert(!scheduleFindByScheduleId("").valid);
  assert(!scheduleFindByScheduleId(nullptr).valid);

  // ช่องที่ว่างต้องไม่มีมื้อยา และต้องไม่ถูกเลือกมาเตือน
  const DoseRef empty = {2, 0, true};
  assert(scheduleDoseAt(empty) == nullptr);
}

void testAlertAndMissed()
{
  resetStore();
  stageStandardSchedule();

  // ก่อนถึงเวลา: ยังไม่มีอะไรเกิดขึ้น
  DoseRef alerting = scheduleTick(7 * 60 + 59, DAY_ONE, false);
  assert(!alerting.valid);
  assert(stateOf("sch-morning") == DoseState::Pending);

  // ถึงเวลาพอดี: เริ่มเตือน
  alerting = scheduleTick(8 * 60, DAY_ONE, false);
  assert(alerting.valid);
  assert(strcmp(scheduleDoseAt(alerting)->scheduleId, "sch-morning") == 0);
  assert(stateOf("sch-morning") == DoseState::Alerting);
  assert(countTo(DoseState::Alerting) == 1);

  // ยังอยู่ในเวลาผ่อนผัน: เตือนต่อโดยไม่แจ้งสถานะซ้ำ
  alerting = scheduleTick(8 * 60 + ALERT_TIMEOUT_MINUTES, DAY_ONE, false);
  assert(alerting.valid);
  assert(countTo(DoseState::Alerting) == 1);

  // เลยเวลาผ่อนผัน: กลายเป็นขาดยา และต้องแจ้งเพียงครั้งเดียว
  alerting = scheduleTick(8 * 60 + ALERT_TIMEOUT_MINUTES + 1, DAY_ONE, false);
  assert(!alerting.valid);
  assert(stateOf("sch-morning") == DoseState::Missed);
  assert(countTo(DoseState::Missed) == 1);

  scheduleTick(9 * 60, DAY_ONE, false);
  assert(countTo(DoseState::Missed) == 1);
}

void testOnlyEarliestDoseAlerts()
{
  resetStore();
  stageStandardSchedule();

  // 18:00 ถึงเวลาแล้ว แต่ 12:00 ก็ยังค้างอยู่ ต้องเตือนมื้อที่ถึงเวลาก่อน
  DoseRef alerting = scheduleTick(12 * 60, DAY_ONE, false);
  assert(strcmp(scheduleDoseAt(alerting)->scheduleId, "sch-noon") == 0);

  scheduleSetState(alerting, DoseState::Done);
  alerting = scheduleTick(18 * 60, DAY_ONE, false);
  assert(strcmp(scheduleDoseAt(alerting)->scheduleId, "sch-evening") == 0);
}

void testStaleDoseAfterBootIsSilent()
{
  resetStore();
  stageStandardSchedule();

  // เปิดเครื่องตอน 14:00 มื้อ 08:00 เลยมานานแล้ว ต้องข้ามเงียบๆ ไม่ปลุกและไม่รายงาน
  // (server ถือว่ามื้อที่ไม่มี log คือ MISSED อยู่แล้ว การรายงานซ้ำจะทำให้ log เพี้ยน)
  const DoseRef alerting = scheduleTick(14 * 60, DAY_ONE, true);
  assert(stateOf("sch-morning") == DoseState::Missed);
  assert(stateOf("sch-noon") == DoseState::Missed);
  assert(transitions.empty());
  assert(!alerting.valid);

  // แต่มื้อ 18:00 ที่ยังไม่ถึงเวลา ต้องทำงานตามปกติ
  scheduleTick(18 * 60, DAY_ONE, false);
  assert(stateOf("sch-evening") == DoseState::Alerting);
}

void testRecentDoseAfterBootStillAlerts()
{
  resetStore();
  stageStandardSchedule();

  // เปิดเครื่องหลังเวลามื้อยาไม่นาน ยังอยู่ในวิสัยที่ให้ผู้ใช้กดรับยาได้
  const DoseRef alerting = scheduleTick(8 * 60 + STALE_DOSE_MINUTES, DAY_ONE, true);
  assert(alerting.valid);
  assert(stateOf("sch-morning") == DoseState::Alerting);
}

void testSyncPreservesProgress()
{
  resetStore();
  stageStandardSchedule();

  scheduleTick(8 * 60, DAY_ONE, false);
  assert(stateOf("sch-morning") == DoseState::Alerting);
  scheduleSetState(scheduleFindByScheduleId("sch-noon"), DoseState::Skipped);

  // sync รอบใหม่ระหว่างวันต้องไม่ดึงสถานะที่คืบหน้าไปแล้วกลับเป็น Pending
  stageStandardSchedule();
  assert(stateOf("sch-morning") == DoseState::Alerting);
  assert(stateOf("sch-noon") == DoseState::Skipped);

  // มื้อที่ server บอกว่าจบแล้ว (เช่น ผู้ดูแลกดยืนยันบนเว็บ) ต้องถูกปิดโดยไม่ต้องรอปุ่ม
  stageStandardSchedule(true, true);
  assert(stateOf("sch-morning") == DoseState::Done);
  // การปิดจาก server ต้องไม่ยิง callback เพราะจะกลายเป็นรายงานผลย้อนกลับไปหา server
  assert(countTo(DoseState::Done) == 0);
}

void testInactiveSlotNeverAlerts()
{
  resetStore();
  stageStandardSchedule(false);

  const DoseRef alerting = scheduleTick(8 * 60, DAY_ONE, false);
  assert(!alerting.valid);
  assert(stateOf("sch-morning") == DoseState::Pending);
  assert(transitions.empty());

  // ช่องอื่นที่ยังเปิดอยู่ต้องทำงานตามปกติ
  scheduleTick(18 * 60, DAY_ONE, false);
  assert(stateOf("sch-evening") == DoseState::Alerting);
}

void testDayRollover()
{
  resetStore();
  stageStandardSchedule();

  scheduleTick(8 * 60, DAY_ONE, false);
  scheduleSetState(scheduleFindByScheduleId("sch-morning"), DoseState::Done);
  assert(!scheduleConsumeDayRollover());

  // ข้ามเที่ยงคืน: สถานะของเมื่อวานต้องถูกล้าง และต้องบอกให้ไปดึงตารางของวันใหม่
  scheduleTick(0, DAY_TWO, false);
  assert(scheduleConsumeDayRollover());
  assert(!scheduleConsumeDayRollover());  // แจ้งครั้งเดียว
  assert(stateOf("sch-morning") == DoseState::Pending);
  assert(stateOf("sch-noon") == DoseState::Pending);

  scheduleTick(8 * 60, DAY_TWO, false);
  assert(stateOf("sch-morning") == DoseState::Alerting);
}

void testNextUpcoming()
{
  resetStore();
  stageStandardSchedule();

  DoseRef next = scheduleNextUpcoming(0);
  assert(next.valid && scheduleDoseAt(next)->minutes == 8 * 60);

  next = scheduleNextUpcoming(9 * 60);
  assert(next.valid && scheduleDoseAt(next)->minutes == 12 * 60);

  // มื้อที่จัดการไปแล้วต้องไม่ถูกเสนอเป็นมื้อถัดไป
  scheduleSetState(scheduleFindByScheduleId("sch-noon"), DoseState::Done);
  next = scheduleNextUpcoming(9 * 60);
  assert(next.valid && scheduleDoseAt(next)->minutes == 18 * 60);

  next = scheduleNextUpcoming(20 * 60);
  assert(!next.valid);
}

void testLimitsAreRespected()
{
  resetStore();
  scheduleBeginSync();

  // ใส่มื้อยาเกินขีดจำกัดต้องไม่ล้นหน่วยความจำ
  const int slot = scheduleStageSlot(1, true, "med-a", "Overflow", 1.0f);
  for (int i = 0; i < MAX_DOSES_PER_SLOT + 5; ++i)
  {
    const std::string id = "sch-" + std::to_string(i);
    scheduleStageDose(slot, id.c_str(), "x", i * 60, false);
  }

  // หมายเลขช่องนอกช่วงและ schedule id ว่างต้องถูกปฏิเสธ
  assert(scheduleStageSlot(0, true, "x", "x", 1.0f) == -1);
  assert(scheduleStageSlot(DISPENSER_COUNT + 1, true, "x", "x", 1.0f) == -1);
  scheduleStageDose(slot, "", "x", 60, false);
  scheduleStageDose(-1, "sch-bad", "x", 60, false);

  scheduleCommitSync();
  assert(scheduleSlot(0).doseCount == MAX_DOSES_PER_SLOT);

  // ช่องเกินจำนวนที่รองรับต้องถูกปฏิเสธเช่นกัน
  scheduleBeginSync();
  for (int i = 0; i < DISPENSER_COUNT; ++i)
    assert(scheduleStageSlot(static_cast<uint8_t>(i + 1), true, "m", "n", 1.0f) == i);
  assert(scheduleStageSlot(1, true, "m", "n", 1.0f) == -1);
  scheduleCommitSync();
  assert(scheduleSlotCount() == DISPENSER_COUNT);
}

}  // namespace

int main()
{
  testStagingAndLookup();
  testAlertAndMissed();
  testOnlyEarliestDoseAlerts();
  testStaleDoseAfterBootIsSilent();
  testRecentDoseAfterBootStillAlerts();
  testSyncPreservesProgress();
  testInactiveSlotNeverAlerts();
  testDayRollover();
  testNextUpcoming();
  testLimitsAreRespected();
  return 0;
}
