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


void testSnoozeDelaysAlert()
{
  resetStore();
  stageStandardSchedule();

  // 08:00 เริ่มเตือน
  DoseRef alerting = scheduleTick(8 * 60, DAY_ONE, false);
  assert(alerting.valid);
  assert(stateOf("sch-morning") == DoseState::Alerting);

  // กดเลื่อน -> หยุดเตือนทันที
  assert(scheduleSnooze(alerting, 8 * 60));
  assert(stateOf("sch-morning") == DoseState::Snoozed);
  assert(scheduleSnoozeCount(scheduleFindByScheduleId("sch-morning")) == 1);

  // ระหว่างที่เลื่อนอยู่ ต้องไม่ถูกเลือกมาเตือน (ไม่มีเสียง)
  alerting = scheduleTick(8 * 60 + SNOOZE_MINUTES - 1, DAY_ONE, false);
  assert(!alerting.valid);
  assert(stateOf("sch-morning") == DoseState::Snoozed);

  // ครบเวลาแล้วกลับมาเตือนเอง
  alerting = scheduleTick(8 * 60 + SNOOZE_MINUTES, DAY_ONE, false);
  assert(alerting.valid);
  assert(strcmp(scheduleDoseAt(alerting)->scheduleId, "sch-morning") == 0);
  assert(stateOf("sch-morning") == DoseState::Alerting);
}

void testSnoozeHasQuota()
{
  resetStore();
  stageStandardSchedule();

  int now = 8 * 60;
  scheduleTick(now, DAY_ONE, false);

  // เลื่อนได้จนครบโควตา
  for (int i = 0; i < MAX_SNOOZE_PER_DOSE; ++i)
  {
    const DoseRef ref = scheduleFindByScheduleId("sch-morning");
    assert(scheduleSnooze(ref, now));
    now += SNOOZE_MINUTES;
    scheduleTick(now, DAY_ONE, false);  // ตื่นกลับมาเตือนอีกครั้ง
  }

  // ครั้งถัดไปต้องถูกปฏิเสธ และยังคงเตือนอยู่เหมือนเดิม
  const DoseRef ref = scheduleFindByScheduleId("sch-morning");
  assert(scheduleSnoozeCount(ref) == MAX_SNOOZE_PER_DOSE);
  assert(!scheduleSnooze(ref, now));
  assert(stateOf("sch-morning") == DoseState::Alerting);
}

void testSnoozeCannotCrossGracePeriod()
{
  resetStore();
  stageStandardSchedule();

  // เข้าใกล้เส้นตายมากจนเลื่อนไปก็จะตื่นหลังหมดเวลาผ่อนผัน
  const int nearDeadline = 8 * 60 + ALERT_TIMEOUT_MINUTES - (SNOOZE_MINUTES - 1);
  scheduleTick(nearDeadline, DAY_ONE, false);
  assert(stateOf("sch-morning") == DoseState::Alerting);

  const DoseRef ref = scheduleFindByScheduleId("sch-morning");
  assert(!scheduleSnooze(ref, nearDeadline));
  assert(stateOf("sch-morning") == DoseState::Alerting);
}

void testSnoozedDoseStillBecomesMissed()
{
  resetStore();
  stageStandardSchedule();

  scheduleTick(8 * 60, DAY_ONE, false);
  const DoseRef ref = scheduleFindByScheduleId("sch-morning");
  assert(scheduleSnooze(ref, 8 * 60));
  assert(stateOf("sch-morning") == DoseState::Snoozed);

  // ถ้าเครื่องดับหรือผู้ใช้หายไปจนเลยเวลาผ่อนผัน มื้อที่เลื่อนไว้ต้องกลายเป็นขาดยา
  // ไม่ใช่ค้างอยู่ในสถานะเลื่อนตลอดไป
  scheduleTick(8 * 60 + ALERT_TIMEOUT_MINUTES + 1, DAY_ONE, false);
  assert(stateOf("sch-morning") == DoseState::Missed);
  assert(countTo(DoseState::Missed) == 1);
}

void testSnoozeSurvivesSync()
{
  resetStore();
  stageStandardSchedule();

  scheduleTick(8 * 60, DAY_ONE, false);
  assert(scheduleSnooze(scheduleFindByScheduleId("sch-morning"), 8 * 60));

  // sync รอบใหม่ระหว่างวันต้องไม่ล้างการเลื่อนที่ผู้ใช้กดไว้
  stageStandardSchedule();
  assert(stateOf("sch-morning") == DoseState::Snoozed);
  assert(scheduleSnoozeCount(scheduleFindByScheduleId("sch-morning")) == 1);

  const DoseRef first = scheduleFirstSnoozed();
  assert(first.valid);
  assert(scheduleDoseAt(first)->snoozedUntil == 8 * 60 + SNOOZE_MINUTES);
}

void testSnoozeOnlyWhileAlerting()
{
  resetStore();
  stageStandardSchedule();

  // ยังไม่ถึงเวลา กดเลื่อนไม่ได้
  const DoseRef ref = scheduleFindByScheduleId("sch-morning");
  assert(stateOf("sch-morning") == DoseState::Pending);
  assert(!scheduleSnooze(ref, 7 * 60));

  // จัดการไปแล้วก็เลื่อนไม่ได้
  scheduleTick(8 * 60, DAY_ONE, false);
  scheduleSetState(ref, DoseState::Done);
  assert(!scheduleSnooze(ref, 8 * 60));

  // อ้างอิงที่ไม่ถูกต้องต้องไม่ทำให้พัง
  const DoseRef bad = {0, 0, false};
  assert(!scheduleSnooze(bad, 8 * 60));
  assert(scheduleSnoozeCount(bad) == 0);
}

void testDayRolloverClearsSnooze()
{
  resetStore();
  stageStandardSchedule();

  scheduleTick(8 * 60, DAY_ONE, false);
  assert(scheduleSnooze(scheduleFindByScheduleId("sch-morning"), 8 * 60));

  scheduleTick(0, DAY_TWO, false);
  assert(stateOf("sch-morning") == DoseState::Pending);
  assert(scheduleSnoozeCount(scheduleFindByScheduleId("sch-morning")) == 0);
  assert(!scheduleFirstSnoozed().valid);
}


void testEffectiveMinutesFollowsSnooze()
{
  resetStore();
  stageStandardSchedule();

  const DoseRef ref = scheduleFindByScheduleId("sch-morning");
  const Dose *dose = scheduleDoseAt(ref);

  // ยังไม่เลื่อน: ใช้เวลาตามตาราง
  assert(doseEffectiveMinutes(*dose) == 8 * 60);
  assert(doseIsOpen(*dose));

  scheduleTick(8 * 60, DAY_ONE, false);
  assert(scheduleSnooze(ref, 8 * 60));

  // เลื่อนแล้ว: ต้องใช้เวลาที่เลื่อนไป ไม่ใช่เวลาตามตาราง
  // ถ้าใช้เวลาตามตาราง มื้อที่เลื่อนไปทับรอบถัดไปจะถูกจัดลำดับผิดแล้วหายจากจอ
  assert(doseEffectiveMinutes(*dose) == 8 * 60 + SNOOZE_MINUTES);
  assert(doseIsOpen(*dose));
}

void testSnoozeCanOverlapNextRound()
{
  resetStore();

  // สองรอบห่างกันเท่ากับเวลาที่เลื่อนหนึ่งครั้งพอดี
  scheduleBeginSync();
  const int slot1 = scheduleStageSlot(1, true, "med-a", "Paracetamol", 1.0f);
  scheduleStageDose(slot1, "sch-first", "first", 8 * 60, false);
  const int slot2 = scheduleStageSlot(2, true, "med-b", "Amoxicillin", 1.0f);
  scheduleStageDose(slot2, "sch-second", "second", 8 * 60 + SNOOZE_MINUTES, false);
  scheduleStageSlot(3, false, "", "", 0.0f);
  scheduleCommitSync();

  // 08:00 มื้อแรกเตือน แล้วผู้ใช้กดเลื่อนไปชนเวลาของมื้อที่สองพอดี
  scheduleTick(8 * 60, DAY_ONE, false);
  const DoseRef first = scheduleFindByScheduleId("sch-first");
  assert(scheduleSnooze(first, 8 * 60));
  assert(doseEffectiveMinutes(*scheduleDoseAt(first)) == 8 * 60 + SNOOZE_MINUTES);

  // พอถึงเวลานั้น ทั้งสองมื้อต้องถึงกำหนดพร้อมกัน ไม่มีอันไหนถูกกลืนหาย
  const int now = 8 * 60 + SNOOZE_MINUTES;
  scheduleTick(now, DAY_ONE, false);

  const Dose *a = scheduleDoseAt(scheduleFindByScheduleId("sch-first"));
  const Dose *b = scheduleDoseAt(scheduleFindByScheduleId("sch-second"));

  assert(doseIsOpen(*a) && doseIsOpen(*b));
  assert(doseEffectiveMinutes(*a) <= now);
  assert(doseEffectiveMinutes(*b) <= now);

  // มื้อที่เลื่อนต้องตื่นกลับมาเตือน ไม่ค้างอยู่ในสถานะเลื่อน
  assert(a->state == DoseState::Alerting);
  assert(b->state == DoseState::Alerting);
}

void testDoseIsOpenCoversEveryState()
{
  resetStore();
  stageStandardSchedule();
  const DoseRef ref = scheduleFindByScheduleId("sch-morning");
  Dose *dose = scheduleDoseAt(ref);

  // สถานะที่ยังต้องให้ผู้ใช้จัดการ
  dose->state = DoseState::Pending;
  assert(doseIsOpen(*dose));
  dose->state = DoseState::Alerting;
  assert(doseIsOpen(*dose));
  dose->state = DoseState::Snoozed;
  assert(doseIsOpen(*dose));
  dose->state = DoseState::Dispensing;
  assert(doseIsOpen(*dose));

  // สถานะที่จบแล้ว ต้องไม่ถูกนำมาแสดงบนจออีก
  dose->state = DoseState::Done;
  assert(!doseIsOpen(*dose));
  dose->state = DoseState::Missed;
  assert(!doseIsOpen(*dose));
  dose->state = DoseState::Skipped;
  assert(!doseIsOpen(*dose));
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
  testSnoozeDelaysAlert();
  testSnoozeHasQuota();
  testSnoozeCannotCrossGracePeriod();
  testSnoozedDoseStillBecomesMissed();
  testSnoozeSurvivesSync();
  testSnoozeOnlyWhileAlerting();
  testDayRolloverClearsSnooze();
  testEffectiveMinutesFollowsSnooze();
  testSnoozeCanOverlapNextRound();
  testDoseIsOpenCoversEveryState();
  return 0;
}
