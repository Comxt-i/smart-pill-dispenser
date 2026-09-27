#include <esp_system.h>
#include "schedule_cache.h"
#include "pill_app.h"

#include "alert.h"
#include "buttons.h"
#include "setup_button.h"
#include "config.h"
#include "command_journal.h"
#include "dispenser_control.h"
#include "event_queue.h"
#include "net_sync.h"
#include "rtc_lcd.h"
#include "schedule_store.h"
#include "status_led.h"
#include "wifi_web.h"

#include <WiFi.h>
#include <math.h>

namespace {

/** เจ้าของรอบการจ่ายที่กำลังทำงานอยู่ ใช้ตัดสินว่าผลที่ได้ต้องรายงานแบบไหน */
enum class RunOwner : uint8_t { None, Dose, Command };

DoseRef activeAlert = {0, 0, false};

// สถานะแยกรายจาน เพราะจ่ายพร้อมกันได้ถึง MAX_CONCURRENT_DISPENSERS จาน
RunOwner runOwner[DISPENSER_COUNT] = {};
RemoteCommand runningCommand[DISPENSER_COUNT];

// อ้างอิงมื้อยาที่กำลังจ่ายด้วย schedule_id ไม่ใช่ index เพราะอาจมี sync คั่นระหว่างที่จานหมุน
// แล้วทำให้ลำดับของมื้อยาเปลี่ยนไป
char runningScheduleId[DISPENSER_COUNT][40] = {};

// มื้อที่ผู้ใช้กดรับแล้วแต่ยังไม่มีจานว่าง รอจนกว่าจะมีที่
// เก็บเป็น schedule_id ด้วยเหตุผลเดียวกับด้านบน
char pendingDoseId[DISPENSER_COUNT][40] = {};
uint8_t pendingDoseCount = 0;
PendingEvent deferredEvents[MAX_PENDING_EVENTS];
uint8_t deferredEventCount = 0;
bool hasRunOwners() {
  for (const auto owner : runOwner) if (owner != RunOwner::None) return true;
  return false;
}
void persistEvent(PendingEvent &event) {
  eventQueueMakeId(event.eventId, sizeof(event.eventId));
  if (!eventQueuePush(event)) Serial.println("[events] queue full; oldest result dropped");
}
void persistDeferredEvents() {
  if (dispenserIsBusy()) return;
  for (uint8_t i = 0; i < deferredEventCount; ++i) persistEvent(deferredEvents[i]);
  deferredEventCount = 0;
}

bool clockWasValid = false;
bool firstTickAfterClock = true;
unsigned long lastEventFlushMs = 0;
// ลองใช้ตารางยาในเครื่องแล้วหรือยังตั้งแต่เปิดเครื่อง (ลองครั้งเดียว ไม่อ่าน NVS ทุกรอบ loop)
bool cachedScheduleTried = false;
bool flushRequested = false;  // มีผลใหม่เข้าคิว ให้ส่งทันทีที่จบรอบ loop
unsigned long cancelPressedAtMs = 0;
bool cancelPressActive = false;
SetupButtonGesture setupButton;
char lastNotice[24] = "";
unsigned long noticeUntilMs = 0;

// ---------------------------------------------------------------------------
// การรายงานผลขึ้น server
// ---------------------------------------------------------------------------

void fillEvent(PendingEvent &event, const char *status)
{
  memset(&event, 0, sizeof(event));
  strncpy(event.status, status, sizeof(event.status) - 1);
  event.localEpoch = rtcLocalEpoch();
}

void queueEvent(PendingEvent &event)
{
  // Every event in simulation mode must stay distinguishable after upload.
  if (!ENABLE_SERVO_MOVEMENT && DISPENSE_DRY_RUN && !strstr(event.note, "dry run")) {
    char previous[sizeof(event.note)];
    snprintf(previous, sizeof(previous), "%s", event.note);
    snprintf(event.note, sizeof(event.note), "dry run%s%.22s", previous[0] ? ", " : "", previous);
  }
  if (deferredEventCount >= MAX_PENDING_EVENTS) stopDispenser();
  if (dispenserIsBusy()) deferredEvents[deferredEventCount++] = event;
  else {
    persistDeferredEvents();
    persistEvent(event);
  }

  // ตั้งธงไว้ให้ appLoop ส่งในจังหวะของมันเอง
  //
  // ห้ามยิง HTTP ตรงนี้: queueEvent ถูกเรียกจาก callback ที่อยู่ระหว่าง scheduleTick
  // กำลังไล่มื้อยาอยู่ การรอ HTTP นานหลายวินาทีกลางลูปเสี่ยงโดน task watchdog
  flushRequested = true;
}

void reportDose(const DoseRef &ref, const char *status, const char *note)
{
  const Slot *slot = scheduleSlotOf(ref);
  const Dose *dose = scheduleDoseAt(ref);
  if (!slot || !dose)
    return;

  PendingEvent event;
  fillEvent(event, status);
  strncpy(event.scheduleId, dose->scheduleId, sizeof(event.scheduleId) - 1);
  strncpy(event.medicationId, slot->medicationId, sizeof(event.medicationId) - 1);

  // แนบจำนวนครั้งที่ผู้ใช้กดเลื่อนไปกับผลสุดท้าย เพื่อให้ผู้ดูแลเห็นว่ามื้อนี้ถูกผัดไปกี่รอบ
  // โดยไม่ต้องสร้างประวัติแยกสำหรับการเลื่อนแต่ละครั้ง
  // ต้องเป็น ASCII และไม่เกิน 31 ไบต์ ตามข้อจำกัดของ PendingEvent::note
  const uint8_t snoozes = dose->snoozeCount;
  if (snoozes > 0 && note)
    snprintf(event.note, sizeof(event.note), "%.18s, snoozed %ux", note, snoozes);
  else if (snoozes > 0)
    snprintf(event.note, sizeof(event.note), "snoozed %ux", snoozes);
  else if (note)
    strncpy(event.note, note, sizeof(event.note) - 1);

  event.slot = slot->number;
  event.amount = slot->amountPerDose;

  queueEvent(event);
}

void reportCommand(const RemoteCommand &command, const char *status, const char *note)
{
  PendingEvent event;
  fillEvent(event, status);
  strncpy(event.commandId, command.id, sizeof(event.commandId) - 1);
  strncpy(event.scheduleId, command.scheduleId, sizeof(event.scheduleId) - 1);
  if (note)
    strncpy(event.note, note, sizeof(event.note) - 1);
  event.slot = command.slot;
  event.amount = command.amount;

  const int slotIndex = scheduleFindSlot(command.slot);
  if (slotIndex >= 0)
    strncpy(event.medicationId, scheduleSlot(slotIndex).medicationId, sizeof(event.medicationId) - 1);

  queueEvent(event);
}

/** schedule_store เรียกทุกครั้งที่มื้อยาเปลี่ยนสถานะ */
/**
 * สถานะที่ถือว่ามื้อนี้จบแล้ววันนี้ ห้ามเตือนซ้ำหลังเปิดเครื่องใหม่
 * รวม Dispensing: ไฟดับกลางการจ่ายไม่รู้ว่ายาออกไปแล้วกี่เม็ด ไม่เตือนซ้ำคือทางที่ปลอดภัย
 * ไม่รวม Queued: รับรอบแล้วแต่จานยังไม่หมุน ยังไม่มียาออกมา เตือนใหม่หลังเปิดเครื่องจึงถูกต้อง
 */
bool isClosedState(DoseState state)
{
  return state == DoseState::Dispensing || state == DoseState::Done || state == DoseState::Missed ||
         state == DoseState::Skipped || state == DoseState::Failed;
}

/**
 * บันทึกลง NVS ทุกมื้อที่จบแล้วแต่ยังไม่ได้บันทึก
 *
 * บางเส้นทางตั้งสถานะตรงๆ ไม่ผ่าน callback (โหมดทดสอบ จ่ายไม่สำเร็จ เลยเวลา) เพื่อไม่ให้ส่งผลซ้ำ
 * สแกนรวมที่เดียวจึงไม่พลาดเส้นทางไหน รวมถึงเส้นทางที่จะเพิ่มในอนาคต
 * ถูกมาก: ไม่กี่สิบมื้อ เขียน NVS เฉพาะมื้อที่เพิ่งจบ
 *
 * เรียกที่ไหนก็ได้ เพราะใช้ scheduleDayKey() ซึ่งเปลี่ยนพร้อมกับตอนล้างสถานะเมื่อขึ้นวันใหม่เสมอ
 * (ห้ามเปลี่ยนเป็นวันที่ของนาฬิกา ช่วงข้ามเที่ยงคืนสองค่านี้ไม่ตรงกัน)
 */
void persistClosedDoses()
{
  const uint32_t day = scheduleDayKey();
  if (day == 0)
    return;
  for (uint8_t s = 0; s < scheduleSlotCount(); ++s)
  {
    const Slot &slot = scheduleSlot(s);
    for (uint8_t d = 0; d < slot.doseCount; ++d)
    {
      const Dose &dose = slot.doses[d];
      if (isClosedState(dose.state) && !scheduleCacheIsClosed(dose.scheduleId, day))
        scheduleCacheMarkClosed(dose.scheduleId, day);
    }
  }
}

void onDoseStateChanged(const DoseRef &ref, DoseState previous, DoseState next)
{
  (void)previous;

  // บันทึกลง NVS ทันทีว่ามื้อนี้จบแล้ววันนี้ ก่อนจะส่งผลขึ้น server สำเร็จ
  // ไฟดับแล้วเปิดใหม่ตอนไม่มีเน็ต ตารางในเครื่องยังบอกว่ามื้อนี้ยังไม่กิน รายการนี้กันเตือนซ้ำ/จ่ายซ้ำ
  // (เส้นทางที่ตั้งสถานะตรงๆ โดยไม่ผ่านตรงนี้ persistClosedDoses() เก็บให้)
  if (isClosedState(next) && scheduleDayKey() != 0)
  {
    if (const Dose *dose = scheduleDoseAt(ref))
      scheduleCacheMarkClosed(dose->scheduleId, scheduleDayKey());
  }

  switch (next)
  {
    case DoseState::Done:
      reportDose(ref, "DISPENSED", nullptr);
      break;
    case DoseState::Missed:
      reportDose(ref, "MISSED", "no response in time");
      break;
    case DoseState::Skipped:
      reportDose(ref, "SKIPPED", "cancelled by user");
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// การจ่ายยา
// ---------------------------------------------------------------------------

/**
 * แปลงจำนวนเม็ดจากตารางยา (เป็นทศนิยมได้ เช่น 0.5 เม็ด) เป็นจำนวนเม็ดที่สั่งจ่ายจริง
 *
 * กลไกจ่ายได้ทีละเม็ดเต็มเท่านั้น จำนวนที่ไม่รองรับต้องปฏิเสธ ห้ามปัดขึ้น
 */
uint8_t pillsFor(float amount)
{
  if (!isfinite(amount) || amount < 1 || amount > MAX_PILLS_PER_DOSE || floorf(amount) != amount)
    return 0; // Controller rejects unsupported quantities before any motion.
  return static_cast<uint8_t>(amount);
}

void showNotice(const char *text)
{
  strncpy(lastNotice, text, sizeof(lastNotice) - 1);
  lastNotice[sizeof(lastNotice) - 1] = '\0';
  noticeUntilMs = millis() + 3000;
}

/** ข้อความอธิบายผลสำหรับผู้ใช้ (Serial และหน้าเว็บในเครื่อง) */
const char *describe(DispenseResult result)
{
  switch (result)
  {
    case DispenseResult::Started: return "เริ่มจ่ายยาแล้ว";
    case DispenseResult::Invalid: return "พารามิเตอร์ไม่ถูกต้อง";
    case DispenseResult::Disabled: return "ปิดการขยับ Servo อยู่ (ENABLE_SERVO_MOVEMENT=false)";
    case DispenseResult::Busy: return "กำลังจ่ายยาอยู่";
    case DispenseResult::Cancelled: return "ปุ่ม Cancel ถูกกดค้างอยู่";
    case DispenseResult::ServoError: return "ต่อ Servo ไม่สำเร็จ";
    case DispenseResult::SensorBlocked: return "เซ็นเซอร์ถูกบังอยู่ ตรวจช่องจ่ายยาก่อน";
  }
  return "ไม่ทราบผล";
}

/**
 * รหัสเหตุผลสำหรับส่งขึ้น server
 *
 * ต้องเป็น ASCII สั้นเสมอ เพราะ PendingEvent::note มีแค่ 32 ไบต์
 * ถ้าใส่ข้อความไทยแล้วถูกตัดกลางตัวอักษร UTF-8 จะทำให้ JSON ทั้งชุดเสียและ server ปฏิเสธ
 * ทำให้คิววนส่งไม่จบ ฝั่งเว็บแปลรหัสเหล่านี้เป็นภาษาไทยเพื่อแสดงผลได้
 */
const char *reasonCode(DispenseResult result)
{
  switch (result)
  {
    case DispenseResult::Started: return "started";
    case DispenseResult::Invalid: return "invalid request";
    case DispenseResult::Disabled: return "servo disabled";
    case DispenseResult::Busy: return "dispenser busy";
    case DispenseResult::Cancelled: return "cancel held";
    case DispenseResult::SensorBlocked: return "sensor blocked";
    case DispenseResult::ServoError: return "servo attach failed";
  }
  return "unknown";
}

/**
 * เริ่มจ่ายมื้อนี้
 *
 * คืน true เมื่อจัดการมื้อนี้เรียบร้อยแล้ว ไม่ว่าจะเริ่มหมุนได้หรือรายงานความล้มเหลวไปแล้ว
 * คืน false เฉพาะตอนที่ยังไม่มีจานว่าง ผู้เรียกต้องเก็บไว้ลองใหม่รอบถัดไป
 */
bool startDoseDispense(const DoseRef &ref)
{
  const Slot *slot = scheduleSlotOf(ref);
  Dose *dose = scheduleDoseAt(ref);
  if (!slot || !dose)
    return true;

  const DispenseResult result =
      dispenseMedicine(slot->number, pillsFor(slot->amountPerDose), slot->pillHole);

  // โหมดทดสอบตอนที่ยังไม่ได้ต่อ Servo: ปิดมื้อนี้ให้เรียบร้อยเหมือนจ่ายสำเร็จ
  // แต่ติดป้าย "dry run" ไว้ในบันทึก เพื่อไม่ให้ประวัติหลอกว่าเม็ดยาออกมาจริง
  if (result == DispenseResult::Disabled && DISPENSE_DRY_RUN)
  {
    Serial.printf("[จ่ายยา] ช่อง %u โหมดทดสอบ ไม่ได้ขยับ Servo\n", slot->number);
    alertOneShot(AlertPattern::Success);
    showNotice("DRY RUN OK");
    reportDose(ref, "DISPENSED", "dry run");
    // ตั้งสถานะตรงๆ เพื่อไม่ให้ callback ส่ง DISPENSED ซ้ำอีกใบ
    dose->state = DoseState::Done;
    return true;
  }

  // จานยังไม่ว่าง (จานนี้กำลังทำงาน หรือเต็มเพดานพร้อมกัน)
  // ถือเป็นการรอ ไม่ใช่ความล้มเหลว จึงไม่รายงานอะไรและให้ผู้เรียกลองใหม่
  if (result == DispenseResult::Busy)
    return false;

  if (result != DispenseResult::Started)
  {
    Serial.printf("[จ่ายยา] ช่อง %u ไม่สำเร็จ: %s\n", slot->number, describe(result));
    alertOneShot(AlertPattern::Warning);
    showNotice(result == DispenseResult::Disabled ? "SERVO DISABLED" : "DISPENSE FAILED");

    dose->state = DoseState::Failed;
    // Accepted doses require inspection before a new request, including start errors.
    if (!dose->failureReported)
    {
      dose->failureReported = true;
      reportDose(ref, "FAILED", reasonCode(result));
    }
    return true;
  }

  const uint8_t index = static_cast<uint8_t>(slot->number - 1);
  strncpy(runningScheduleId[index], dose->scheduleId, sizeof(runningScheduleId[0]) - 1);
  runningScheduleId[index][sizeof(runningScheduleId[0]) - 1] = '\0';
  runOwner[index] = RunOwner::Dose;
  scheduleSetState(ref, DoseState::Dispensing);
  alertSet(AlertPattern::None);
  // No I2C operations while a motor is active.
  return true;
}

/** ลบรายการที่ index ออกจากคิว โดยเลื่อนของที่เหลือขึ้นมา */
void removePendingAt(uint8_t index)
{
  for (uint8_t i = index; i + 1 < pendingDoseCount; ++i)
    memcpy(pendingDoseId[i], pendingDoseId[i + 1], sizeof(pendingDoseId[0]));
  --pendingDoseCount;
}

/**
 * เริ่มจ่ายมื้อที่ค้างคิวอยู่เท่าที่จานว่าง — เรียกทุกลูป
 *
 * ทำให้ช่องที่สามเริ่มเองทันทีที่ช่องแรกจ่ายเสร็จ โดยผู้ใช้ไม่ต้องกดปุ่มซ้ำ
 */
void startPendingDoses()
{
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW || wifiSetupActive()) return;
  for (uint8_t i = 0; i < pendingDoseCount;)
  {
    if (!dispenserHasCapacity())
      return;

    const DoseRef ref = scheduleFindByScheduleId(pendingDoseId[i]);
    const Dose *dose = scheduleDoseAt(ref);

    // มื้อหายไปหลัง sync หรือเปลี่ยนสถานะไปแล้ว (เช่นผู้ใช้กดข้าม) -> ทิ้งจากคิว
    const bool stillWaiting = dose && dose->state == DoseState::Queued;
    if (stillWaiting && !startDoseDispense(ref))
      return;  // จานที่ต้องใช้ยังไม่ว่าง เก็บไว้ลองใหม่รอบหน้า

    removePendingAt(i);
  }
}

void cancelPendingDoses() {
  while (pendingDoseCount) {
    const DoseRef ref = scheduleFindByScheduleId(pendingDoseId[0]);
    Dose *dose = scheduleDoseAt(ref);
    if (dose && dose->state == DoseState::Queued) scheduleSetState(ref, DoseState::Skipped);
    removePendingAt(0);
  }
}

/**
 * ผู้ใช้กดปุ่มเขียว: รับยา **ทั้งรอบ** ในครั้งเดียว
 *
 * รอบหนึ่งมีได้หลายช่อง ถ้าให้กดทีละช่อง ผู้สูงอายุมีโอกาสลืมกดช่องท้ายๆ แล้วยาขาดไปเลย
 */
void acceptRound()
{
  if (dispenserIsBusy() || pendingDoseCount || hasRunOwners()) return;
  DoseRef alerting[DISPENSER_COUNT];
  const uint8_t count = scheduleAlertingDoses(alerting, DISPENSER_COUNT);
  if (count == 0)
    return;

  // เงียบเสียงเตือนทันทีที่ผู้ใช้ตอบรับ ไม่ต้องรอให้จานแรกเริ่มหมุน
  alertSet(AlertPattern::None);

  pendingDoseCount = 0;
  for (uint8_t i = 0; i < count; ++i)
  {
    Dose *dose = scheduleDoseAt(alerting[i]);
    const Slot *slot = scheduleSlotOf(alerting[i]);
    if (!dose || !slot) continue;
    if (!pillsFor(slot->amountPerDose)) {
      dose->state = DoseState::Failed;
      reportDose(alerting[i], "FAILED", "invalid parameters");
      continue;
    }
    if (ENABLE_SERVO_MOVEMENT) {
      char key[72];
      commandJournalDoseKey(key, sizeof(key), dose->scheduleId, rtcDayKey(), false);
      if (!commandJournalReserve(key, rtcLocalEpoch())) {
        dose->state = DoseState::Failed;
        reportDose(alerting[i], "FAILED", "dose locked; check pills");
        continue;
      }
    }
    dose->state = DoseState::Queued;
    strncpy(pendingDoseId[pendingDoseCount], dose->scheduleId, sizeof(pendingDoseId[0]) - 1);
    pendingDoseId[pendingDoseCount][sizeof(pendingDoseId[0]) - 1] = '\0';
    ++pendingDoseCount;
  }

  startPendingDoses();
}

/**
 * เลื่อนทั้งรอบ
 *
 * ถ้ามีมื้อใดเลื่อนไม่ได้ จะไม่เลื่อนสักมื้อ เพราะเลื่อนได้บางช่องแล้วทิ้งช่องอื่นไว้
 * จะทำให้รอบเดียวแตกออกเป็นสองเวลา ซึ่งขัดกับที่ปุ่มเขียวรับทั้งรอบในครั้งเดียว
 *
 * ทุกมื้อในรอบเดียวกันมีเวลาเท่ากันและถูกเลื่อนพร้อมกันเสมอ ปกติจึงตอบเหมือนกันทั้งรอบอยู่แล้ว
 */
bool snoozeRound()
{
  DoseRef alerting[DISPENSER_COUNT];
  const uint8_t count = scheduleAlertingDoses(alerting, DISPENSER_COUNT);
  if (count == 0)
    return false;

  const int now = rtcMinutesOfDay();
  for (uint8_t i = 0; i < count; ++i)
  {
    if (!scheduleCanSnooze(alerting[i], now))
      return false;
  }

  for (uint8_t i = 0; i < count; ++i)
    scheduleSnooze(alerting[i], now);

  // มื้อที่ค้างคิวอยู่ถูกเลื่อนไปแล้ว ไม่ต้องรอจานว่างอีก
  pendingDoseCount = 0;
  return true;
}

/** ข้ามทั้งรอบ */
uint8_t skipRound()
{
  DoseRef alerting[DISPENSER_COUNT];
  const uint8_t count = scheduleAlertingDoses(alerting, DISPENSER_COUNT);

  // callback ของ scheduleSetState จะส่ง SKIPPED ขึ้น server ให้เองทีละมื้อ
  for (uint8_t i = 0; i < count; ++i)
    scheduleSetState(alerting[i], DoseState::Skipped);

  pendingDoseCount = 0;
  return count;
}

void startCommandDispense(const RemoteCommand &command)
{
  const DispenseResult result =
      dispenseMedicine(command.slot, pillsFor(command.amount), scheduleSlotPillHole(command.slot));
  if (result == DispenseResult::Disabled && DISPENSE_DRY_RUN) {
    alertOneShot(AlertPattern::Success);
    showNotice("DRY RUN OK");
    reportCommand(command, "DISPENSED", "dry run");
    return;
  }
  if (result != DispenseResult::Started)
  {
    Serial.printf("[คำสั่ง] ช่อง %u ไม่สำเร็จ: %s\n", command.slot, describe(result));
    alertOneShot(AlertPattern::Warning);
    reportCommand(command, "FAILED", reasonCode(result));
    return;
  }

  const uint8_t index = static_cast<uint8_t>(command.slot - 1);
  runningCommand[index] = command;
  runOwner[index] = RunOwner::Command;
  // No I2C operations while a motor is active.
}

/**
 * ตรวจว่ารอบที่กำลังหมุนจบหรือยัง แล้วรายงานผลตามเจ้าของรอบนั้น
 *
 * อ่านจากคิวทีละใบ เพราะหลายจานจบพร้อมกันได้
 */
void handleDispenseOutcome()
{
  DispenseOutcome outcome;
  if (!takeDispenseOutcome(outcome))
    return;

  const uint8_t index = static_cast<uint8_t>(outcome.dispenser - 1);
  if (index >= DISPENSER_COUNT) return;
  const bool complete = !outcome.cancelled && outcome.dispensedPills == outcome.requestedPills;
  char failureNote[32];
  snprintf(failureNote, sizeof(failureNote), "%s %u/%u; check pills",
           outcome.cancelled ? "cancelled" : "count", outcome.dispensedPills, outcome.requestedPills);
  const RunOwner owner = runOwner[index];
  runOwner[index] = RunOwner::None;

  // ไม่ได้ตรวจด้วย IR = รู้แค่ว่าสั่งหมุนไปแล้ว ยืนยันไม่ได้ว่าเม็ดยาออกมาจริง
  // ต้องติดป้ายไว้ในบันทึก เพื่อไม่ให้ประวัติหลอกว่ายืนยันแล้ว
  const char *const unverified = outcome.sensorVerified ? nullptr : "unverified";

  Serial.printf("[จ่ายยา] ช่อง %u ได้ %u/%u เม็ด (หมุน %u รอบ)%s%s\n",
                outcome.dispenser,
                outcome.dispensedPills,
                outcome.requestedPills,
                outcome.attempts,
                outcome.cancelled ? " (ถูกยกเลิก)" : "",
                outcome.sensorVerified ? "" : " (ไม่ได้ตรวจด้วย IR)");

  if (owner == RunOwner::Dose)
  {
    // ค้นใหม่จาก schedule_id เผื่อมี sync เปลี่ยนตารางระหว่างที่จานกำลังหมุน
    const DoseRef ref = scheduleFindByScheduleId(runningScheduleId[index]);
    runningScheduleId[index][0] = '\0';

    Dose *dose = scheduleDoseAt(ref);
    if (!dose)
    {
      Serial.println("[จ่ายยา] มื้อยาที่กำลังจ่ายหายไปจากตารางหลัง sync จึงไม่บันทึกผล");
      return;
    }

    if (complete)
    {
      alertOneShot(AlertPattern::Success);
      statusLedFlash(LedColor::Green);
      showNotice("TAKE YOUR PILLS");
      if (unverified)
      {
        // ต้องแนบหมายเหตุ แต่ onDoseStateChanged ส่ง DISPENSED แบบไม่มีหมายเหตุ
        // จึงรายงานเองแล้วตั้งสถานะตรงๆ เพื่อไม่ให้ callback ส่งซ้ำอีกใบ
        // (แพตเทิร์นเดียวกับเส้นทาง dry run ด้านบน)
        reportDose(ref, "DISPENSED", unverified);
        dose->state = DoseState::Done;
      }
      else
      {
        scheduleSetState(ref, DoseState::Done);  // callback จะส่ง DISPENSED ให้เอง
      }
    }
    else
    {
      alertOneShot(AlertPattern::Warning);
      statusLedFlash(LedColor::Red);
      showNotice("DISPENSE STOPPED");
      // A partial/uncertain dose must not be retried as another full dose.
      dose->state = DoseState::Failed;
      if (!dose->failureReported)
      {
        dose->failureReported = true;
        reportDose(ref, "FAILED", failureNote);
      }
    }
    return;
  }

  if (owner == RunOwner::Command)
  {
    alertOneShot(complete ? AlertPattern::Success : AlertPattern::Warning);
    reportCommand(runningCommand[index],
                  complete ? "DISPENSED" : "FAILED",
                  complete ? unverified : failureNote);
  }
}

// ---------------------------------------------------------------------------
// ปุ่มกด
// ---------------------------------------------------------------------------

void handleButtons()
{
  Dose *alertDose = scheduleDoseAt(activeAlert);
  const bool alerting = !wifiSetupActive() && alertDose && alertDose->state == DoseState::Alerting;

  // Consume the down edge, but never dispense until a short press is released.
  if (buttonPressed(ButtonId::Dispense))
    Serial.println("[Button] GPIO33 pressed; hold 3 seconds for Wi-Fi Setup");
  const auto action = setupButton.update(buttonHeld(ButtonId::Dispense), millis(),
      !wifiSetupActive() && !dispenserIsBusy() && !hasRunOwners() && pendingDoseCount == 0);
  if (action == SetupButtonAction::OpenSetup && alerting) {
    // ระหว่างเตือน กดค้างครบ 3 วินาที = รับยา ไม่ใช่เปิดโหมดตั้งค่า
    //
    // คนที่ถึงเวลากินยามักกดปุ่มเขียวค้างรอให้เครื่องตอบสนอง เดิมพอครบ 3 วินาที
    // กลับเปิดโหมดตั้งค่า ซึ่งปิดการเตือนและการจ่ายยาทั้งหมด ยาไม่ออกและจอค้างหน้า Wi-Fi
    //
    // ยังได้ผลนี้เฉพาะตอนที่ gesture อนุญาต (ไม่ busy) เหมือนกดสั้น ถ้า busy จะได้ Blocked แทน
    // นอกช่วงเตือน กดค้าง 3 วินาทียังเปิดโหมดตั้งค่าได้ตามปกติ
    Serial.println("[Button] GPIO33 held 3 seconds during alert; accepting round");
    alertOneShot(AlertPattern::Click);
    statusLedFlash(LedColor::Green);
    acceptRound();
  } else if (action == SetupButtonAction::OpenSetup) {
    Serial.println("[Setup] GPIO33 held 3 seconds; requesting Wi-Fi Setup");
    if (!wifiStartSetup()) {
      alertOneShot(AlertPattern::Warning);
      showNotice("SETUP FAILED");
    }
  } else if (action == SetupButtonAction::Blocked && !wifiSetupActive()) {
    Serial.println("[Setup] Busy during button hold; release and try again");
    alertOneShot(AlertPattern::Warning);
    showNotice("BUSY");
  }

  if (action == SetupButtonAction::ShortPress)
  {
    if (alerting)
    {
      alertOneShot(AlertPattern::Click);
      statusLedFlash(LedColor::Green);
      acceptRound();
    }
    else
    {
      alertOneShot(AlertPattern::Warning);
      statusLedFlash(LedColor::Red);
      showNotice(dispenserIsBusy() ? "BUSY" : "NO DOSE DUE");
    }
  }

  if (buttonPressed(ButtonId::Snooze) && !wifiSetupActive())
  {
    if (alerting)
    {
      // ปุ่มเหลือง: ยังไม่สะดวกตอนนี้ ขอเลื่อนทั้งรอบไปอีก SNOOZE_MINUTES นาที
      statusLedFlash(LedColor::Yellow);
      if (snoozeRound())
      {
        alertOneShot(AlertPattern::Click);
        char notice[24];
        snprintf(notice, sizeof(notice), "SNOOZE %d MIN", SNOOZE_MINUTES);
        showNotice(notice);
      }
      else
      {
        // เลื่อนครบโควตาแล้ว หรือเลื่อนต่อจะเลยเวลาผ่อนผัน
        alertOneShot(AlertPattern::Warning);
        statusLedFlash(LedColor::Red);
        showNotice("CANNOT SNOOZE");
      }
    }
    else
    {
      alertOneShot(AlertPattern::Click);
      showNotice("SYNCING...");
      netSyncRequestNow();
    }
  }

  // Cancel: กดสั้น = ข้ามมื้อนี้, กดค้าง = หยุดกลไกทันที
  if (buttonPressed(ButtonId::Cancel))
  {
    cancelPressActive = true;
    cancelPressedAtMs = millis();
  }

  if (buttonHeldFor(ButtonId::Cancel, CANCEL_HOLD_MS))
  {
    cancelPressActive = false;
    stopDispenser();

    cancelPendingDoses();

    alertOneShot(AlertPattern::Warning);
    statusLedFlash(LedColor::Red);
    showNotice("STOPPED");
  }

  if (cancelPressActive && !buttonHeld(ButtonId::Cancel))
  {
    cancelPressActive = false;
    if (millis() - cancelPressedAtMs < CANCEL_HOLD_MS && alerting && !wifiSetupActive())
    {
      // ปุ่มแดงกดสั้น: ข้ามทั้งรอบ ให้สอดคล้องกับปุ่มเขียวที่รับทั้งรอบ
      alertOneShot(AlertPattern::Click);
      statusLedFlash(LedColor::Red);
      showNotice("SKIPPED");
      skipRound();
    }
  }
}

// ---------------------------------------------------------------------------
// จอและเสียง
// ---------------------------------------------------------------------------

/** จำนวนเม็ดแบบอ่านง่าย: 2 ไม่ใช่ 2.0 */
void formatAmount(float amount, char *out, size_t size)
{
  if (amount == static_cast<int>(amount))
    snprintf(out, size, "%d", static_cast<int>(amount));
  else
    snprintf(out, size, "%.1f", static_cast<double>(amount));
}

/**
 * หาเวลาของ "รอบถัดไป" คือมื้อที่ใกล้ที่สุดที่ยังไม่ได้จัดการ
 *
 * ใช้เวลาที่คิดการเลื่อนแล้ว (doseEffectiveMinutes) ไม่ใช่เวลาตามตาราง
 * ถ้าใช้เวลาตามตาราง มื้อที่ถูกเลื่อนไปทับรอบถัดไปจะถูกจัดลำดับผิด
 *
 * คืน -1 เมื่อจัดการครบทุกมื้อแล้ววันนี้
 */
int nextRoundMinutes()
{
  int soonest = -1;

  for (uint8_t s = 0; s < scheduleSlotCount(); ++s)
  {
    const Slot &slot = scheduleSlot(s);
    if (!slot.active || slot.medicationId[0] == '\0')
      continue;

    for (uint8_t d = 0; d < slot.doseCount; ++d)
    {
      const Dose &dose = slot.doses[d];
      if (!doseIsOpen(dose))
        continue;

      const int when = doseEffectiveMinutes(dose);
      if (soonest < 0 || when < soonest)
        soonest = when;
    }
  }

  return soonest;
}

/**
 * สร้างบรรทัดของยาที่ต้องกิน โดยแสดงทีละรอบ
 *
 * ปกติจะแสดงเฉพาะยาที่มีเวลาตรงกับ roundMinutes
 * แต่ถ้ามีมื้อที่ถึงเวลาแล้ว (เช่นผู้ใช้กดเลื่อนจนไปทับรอบถัดไป) จะแสดง
 * **ทุกมื้อที่ถึงเวลาแล้วพร้อมกัน** เพราะผู้ใช้ต้องจัดการทั้งหมด ไม่ใช่เห็นทีละตัว
 *
 * คืนจำนวนบรรทัดที่สร้างได้ และตั้ง anyDueNow ว่ามีมื้อที่ถึงเวลาแล้วหรือไม่
 */
uint8_t buildRoundLines(int roundMinutes,
                        int nowMinutes,
                        char buffer[][LCD_MARQUEE_MAX_TEXT],
                        uint8_t maxLines,
                        bool &anyDueNow)
{
  uint8_t used = 0;
  anyDueNow = false;
  if (roundMinutes < 0)
    return 0;

  // รอบแรก: ดูว่ามีมื้อที่ถึงเวลาแล้วไหม
  for (uint8_t s = 0; s < scheduleSlotCount() && !anyDueNow; ++s)
  {
    const Slot &slot = scheduleSlot(s);
    if (!slot.active || slot.medicationId[0] == '\0')
      continue;
    for (uint8_t d = 0; d < slot.doseCount; ++d)
    {
      const Dose &dose = slot.doses[d];
      if (doseIsOpen(dose) && doseEffectiveMinutes(dose) <= nowMinutes)
      {
        anyDueNow = true;
        break;
      }
    }
  }

  for (uint8_t s = 0; s < scheduleSlotCount() && used < maxLines; ++s)
  {
    const Slot &slot = scheduleSlot(s);
    if (!slot.active || slot.medicationId[0] == '\0')
      continue;

    for (uint8_t d = 0; d < slot.doseCount && used < maxLines; ++d)
    {
      const Dose &dose = slot.doses[d];
      if (!doseIsOpen(dose))
        continue;

      const int when = doseEffectiveMinutes(dose);

      // มีของถึงเวลาแล้ว -> โชว์ทุกอันที่ถึงเวลา (อาจข้ามรอบได้)
      // ยังไม่ถึงเวลา -> โชว์เฉพาะรอบที่ใกล้ที่สุด
      if (anyDueNow ? when > nowMinutes : when != roundMinutes)
        continue;

      const char *mark = "";
      if (dose.state == DoseState::Alerting)
        mark = " <<";
      else if (dose.state == DoseState::Snoozed)
        mark = " ZZZ";

      char amount[8];
      formatAmount(slot.amountPerDose, amount, sizeof(amount));
      snprintf(buffer[used],
               LCD_MARQUEE_MAX_TEXT,
               "%u %s x%s%s",
               slot.number,
               slot.name,
               amount,
               mark);
      ++used;
    }
  }

  return used;
}

/**
 * เลือกรูปแบบไฟสถานะจากสิ่งที่เครื่องกำลังทำอยู่
 *
 * กำลังจ่าย (รวมช่องที่ยังรอคิว) สำคัญกว่าการเตือน เพราะผู้ใช้กดรับไปแล้ว
 * และต้องเห็นว่าเครื่องยังทำงานอยู่ อย่าเพิ่งเดินหนี
 */
void updateStatusLed()
{
  if (dispenserIsBusy() || pendingDoseCount > 0)
    statusLedSet(LedPattern::DispenseChase);
  else if (scheduleDoseAt(activeAlert) &&
           scheduleDoseAt(activeAlert)->state == DoseState::Alerting)
    statusLedSet(LedPattern::AlertBlink);
  else
    statusLedSet(LedPattern::Off);

  statusLedUpdate();
}

/**
 * หน้าจอโหมดตั้งค่า Wi-Fi
 *
 * แสดงชื่อและรหัสของ AP ที่เครื่องปล่อยเอง เพื่อให้ผู้ใช้จริงเชื่อมต่อได้
 * โดยไม่ต้องต่อคอมพิวเตอร์ดู Serial — จอ 20x4 ใส่ได้ครบทั้งสี่บรรทัดพอดี
 *
 * บรรทัดไหนยาวเกินจอจะเลื่อนวนเองผ่าน lcdMedicineTick()
 */
void showSetupScreen()
{
  static char passLine[LCD_MARQUEE_MAX_TEXT];
  snprintf(passLine, sizeof(passLine), "pw: %s", wifiSetupPassword());

  // บรรทัดแรกบอกว่าเปิดเพราะอะไร แล้วเปลี่ยนตามขั้นตอน (CONNECTING, WIFI SAVED, PAIR FAILED ...)
  // เดิมเป็นหัวข้อนิ่งๆ ผู้ใช้จึงไม่รู้เลยว่ากล่องเข้าโหมดนี้เองทำไม หรือตั้งค่าไปถึงไหนแล้ว
  static char titleLine[LCD_MARQUEE_MAX_TEXT];
  snprintf(titleLine, sizeof(titleLine), "SETUP:%s", wifiSetupStatusShort());

  const char *lines[LCD_MEDICINE_ROWS] = {
      titleLine,
      wifiSetupSsid(),
      passLine,
      "http://192.168.4.1",
  };

  // ส่ง 0 hint เพื่อให้บรรทัดสุดท้ายอยู่นิ่ง ผู้ใช้กำลังพิมพ์ตามอยู่ ไม่ควรให้ข้อความสลับไปมา
  lcdSetMedicineScreen(lines, LCD_MEDICINE_ROWS, nullptr, 0);
}

void updateDisplay()
{
  const bool online = WiFi.status() == WL_CONNECTED;

  // ข้อความตอบรับการกดปุ่ม แสดงสั้นๆ แล้วกลับไปหน้าปกติ
  if (static_cast<long>(noticeUntilMs - millis()) > 0 && lastNotice[0] != '\0')
  {
    lcdShowMessage(lastNotice, "");
    return;
  }

  if (dispenserIsBusy())
  {
    lcdShowMessage("Dispensing...", "Please wait");
    return;
  }

  if (!rtcIsValid())
  {
    lcdShowMessage("Clock not set", online ? "Syncing..." : "No Wi-Fi");
    return;
  }

  if (!scheduleHasData())
  {
    lcdShowMessage("No schedule yet", online ? "Contacting server" : "No Wi-Fi");
    return;
  }

  // ---- บรรทัดบน: ยาของ "รอบถัดไป" รอบเดียว บรรทัดละ 1 ช่อง ----
  // บรรทัดสุดท้ายสงวนไว้ให้เวลา/คำแนะนำปุ่ม
  constexpr uint8_t SLOT_LINES = LCD_MEDICINE_ROWS - 1;
  static char slotText[SLOT_LINES][LCD_MARQUEE_MAX_TEXT];
  const int nowMinutes = rtcMinutesOfDay();
  const int roundMinutes = nextRoundMinutes();
  bool anyDueNow = false;
  const uint8_t slotLines =
      buildRoundLines(roundMinutes, nowMinutes, slotText, SLOT_LINES, anyDueNow);

  const char *lines[LCD_MEDICINE_ROWS] = {};
  for (uint8_t i = 0; i < slotLines; ++i)
    lines[i] = slotText[i];
  for (uint8_t i = slotLines; i < LCD_MEDICINE_ROWS; ++i)
    lines[i] = "";

  // ---- บรรทัดสุดท้าย ----
  const Dose *alertDose = scheduleDoseAt(activeAlert);
  const bool alerting = alertDose && alertDose->state == DoseState::Alerting;

  char hintA[LCD_MAX_COLS + 1];
  char hintB[LCD_MAX_COLS + 1];
  char hintC[LCD_MAX_COLS + 1];

  if (alerting)
  {
    // กำลังเตือน: บอกว่าถึงเวลาของช่องไหน แล้วสลับบอกว่าปุ่มไหนทำอะไร
    const Slot *slot = scheduleSlotOf(activeAlert);
    snprintf(hintA, sizeof(hintA), "NOW %02d:%02d  slot %u",
             alertDose->minutes / 60, alertDose->minutes % 60,
             slot ? slot->number : 0);
    snprintf(hintB, sizeof(hintB), "GREEN = TAKE NOW");

    if (scheduleSnoozeCount(activeAlert) >= MAX_SNOOZE_PER_DOSE)
      snprintf(hintC, sizeof(hintC), "RED = SKIP DOSE");
    else
      snprintf(hintC, sizeof(hintC), "YEL +%dmin  RED skip", SNOOZE_MINUTES);

    const char *hints[3] = {hintA, hintB, hintC};
    lcdSetMedicineScreen(lines, LCD_MEDICINE_ROWS, hints, 3);
    return;
  }

  const DoseRef snoozed = scheduleFirstSnoozed();
  const Dose *snoozedDose = scheduleDoseAt(snoozed);
  if (snoozedDose)
  {
    snprintf(hintA, sizeof(hintA), "Snooze back %02d:%02d",
             snoozedDose->snoozedUntil / 60, snoozedDose->snoozedUntil % 60);
    snprintf(hintB, sizeof(hintB), "Snoozed %u of %u",
             static_cast<unsigned>(snoozedDose->snoozeCount),
             static_cast<unsigned>(MAX_SNOOZE_PER_DOSE));
    const char *hints[2] = {hintA, hintB};
    lcdSetMedicineScreen(lines, LCD_MEDICINE_ROWS, hints, 2);
    return;
  }

  // ---- ว่าง: บรรทัดสุดท้ายบอกเวลาของรอบถัดไป ----
  //
  // ตอนทุกอย่างปกติให้แสดงบรรทัดเดียวนิ่งๆ ไม่ต้องสลับ
  // การสลับข้อความที่ไม่ได้บอกอะไรใหม่ทำให้จอกวนตาเปล่าๆ
  // จะเพิ่มบรรทัดสลับเฉพาะตอนมีเรื่องที่ผู้ใช้ควรรู้จริงๆ เช่นเน็ตหลุด
  if (roundMinutes >= 0)
    snprintf(hintA, sizeof(hintA), "Next %02d:%02d  %u item%s",
             roundMinutes / 60,
             roundMinutes % 60,
             static_cast<unsigned>(slotLines),
             slotLines == 1 ? "" : "s");
  else
    snprintf(hintA, sizeof(hintA), "All done today");

  if (online)
  {
    const char *hints[1] = {hintA};
    lcdSetMedicineScreen(lines, LCD_MEDICINE_ROWS, hints, 1);
  }
  else
  {
    // ออฟไลน์ = ตารางอาจไม่ตรงกับที่ตั้งไว้บนเว็บ ผู้ใช้ควรรู้
    snprintf(hintB, sizeof(hintB), "OFFLINE - no sync");
    const char *hints[2] = {hintA, hintB};
    lcdSetMedicineScreen(lines, LCD_MEDICINE_ROWS, hints, 2);
  }
}

void updateBuzzer()
{
  const Dose *alertDose = scheduleDoseAt(activeAlert);
  const bool shouldRemind = alertDose && alertDose->state == DoseState::Alerting;
  alertSet(shouldRemind ? AlertPattern::Reminder : AlertPattern::None);
}

}  // namespace

// ---------------------------------------------------------------------------

/** สาเหตุที่เครื่องรีเซ็ตรอบล่าสุด แบบสั้นพอดีจอ */
const char *resetReasonText()
{
  switch (esp_reset_reason())
  {
    case ESP_RST_POWERON:   return "POWER ON";
    case ESP_RST_EXT:       return "EXT PIN";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "CRASH";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
    default:                return "UNKNOWN";
  }
}

/**
 * โชว์สถานะเครื่องบนจอสักพักตอนบูต
 *
 * จำเป็นเพราะจอทั้งสองใช้ไฟ 5V จาก adapter การจะดู Serial ต้องถอด adapter
 * ซึ่งทำให้จอดับไปด้วย ข้อมูลวินิจฉัยจึงต้องมาโผล่บนจอเอง
 *
 * ต้องเรียกหลัง wifiWebBegin() เพราะต้องรู้ว่ามี Wi-Fi บันทึกไว้ไหม
 *
 *   I2C:25 27 68      เจอที่อยู่อะไรบนบัสบ้าง (68 = DS1307)
 *   RTC RUNNING       หรือ RTC HALTED-BATT (ถ่านหมด) / RTC MISSING (ชิปไม่ตอบ)
 *   WIFI:embedded     หรือ WIFI:NOT SAVED = ไม่ได้จำไว้
 *   RST:POWER ON      ถ้าเป็น BROWNOUT/CRASH/WATCHDOG แปลว่าเครื่องรีบูตเอง
 */
void showBootReport()
{
  if (BOOT_REPORT_SCREEN_MS == 0)
    return;

  char found[21] = "";
  uint8_t used = 0;
  for (uint8_t i = 0; i < i2cFoundCount() && used < sizeof(found) - 5; ++i)
    used += snprintf(found + used, sizeof(found) - used, "%02X ", i2cFoundAddress(i));
  if (used == 0) strcpy(found, "NONE");

  static char i2cLine[21], wifiLine[LCD_MARQUEE_MAX_TEXT], resetLine[21];
  snprintf(i2cLine, sizeof(i2cLine), "I2C:%s", found);
  // สามสถานะนี้แก้คนละวิธี จึงต้องแยกให้ออกบนจอ ไม่ใช่รวมเป็น "error" เดียว
  const char *rtcState = !rtcIsPresent()      ? "RTC MISSING"
                       : rtcOscillatorHalted() ? "RTC HALTED-BATT"
                                               : "RTC RUNNING";
  snprintf(wifiLine, sizeof(wifiLine), "WIFI:%s",
           wifiHasSavedNetwork() ? wifiSavedSsid() : "NOT SAVED");
  snprintf(resetLine, sizeof(resetLine), "RST:%s", resetReasonText());

  Serial.printf("[Boot] %s | %s | %s | %s\n", i2cLine, rtcState, wifiLine, resetLine);

  const char *lines[3] = {i2cLine, rtcState, wifiLine};
  const char *hints[1] = {resetLine};
  lcdSetMedicineScreen(lines, 3, hints, 1);

  // lcdSetMedicineScreen แค่เก็บข้อความลง buffer ตัวที่เขียนลงจอจริงคือ lcdMedicineTick()
  // ซึ่งปกติถูกเรียกจาก appLoop() ที่ยังไม่เริ่มทำงานตอนนี้
  // ถ้า delay() เฉยๆ จอจะว่างตลอดแล้วถูก appLoop เขียนทับทันที
  const unsigned long until = millis() + BOOT_REPORT_SCREEN_MS;
  while (static_cast<long>(millis() - until) < 0)
  {
    lcdMedicineTick();
    delay(50);
  }
}

void appBegin()
{
  buttonsBegin();
  alertBegin();
  statusLedBegin();
  eventQueueBegin();
  scheduleBegin(onDoseStateChanged);
  scheduleCacheBegin();

  rtcLcdBegin();

  dispenserControlBegin();
  dispenserHomeAll();

  wifiWebBegin();
  netSyncBegin();

  showBootReport();
}

void appLoop()
{
  buttonsUpdate();
  dispenserControlUpdate();
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW) cancelPendingDoses();
  // Drain all simultaneously completed channels. Their events remain in RAM
  // while another motor is active, keeping NVS/HTTP/I2C out of the sensing loop.
  if (!dispenserIsBusy()) rtcLcdUpdate();
  for (uint8_t i = 0; i < DISPENSER_COUNT; ++i) handleDispenseOutcome();
  if (dispenserIsBusy() || pendingDoseCount) {
    handleButtons();
    startPendingDoses();
    if (dispenserIsBusy() || pendingDoseCount) { alertUpdate(); return; }
  }
  persistDeferredEvents();
  wifiWebLoop();

  if (!wifiSetupActive())
  {
    if (scheduleConsumeDayRollover()) {
      // วันใหม่: เลือกมื้อของวันนี้จากตารางทั้งสัปดาห์ในเครื่อง ไม่ใช่เอามื้อของเมื่อวานมาใช้ซ้ำ
      // (ยาบางตัวกินเฉพาะบางวัน) แล้วค่อยถาม server เผื่อมีอะไรเปลี่ยน
      netSyncApplyCachedSchedule();
      netSyncRequestNow();
    }
    const bool clockValid = rtcIsValid();
    // เปิดเครื่องมาแล้วนาฬิกาพร้อม แต่ยังไม่มีตารางจาก server: ใช้ของที่เก็บไว้ เตือนได้แม้เน็ตยังไม่มา
    if (clockValid && !scheduleHasData() && !cachedScheduleTried) {
      cachedScheduleTried = true;
      netSyncApplyCachedSchedule();
    }
    if (clockValid && !clockWasValid) firstTickAfterClock = true;
    clockWasValid = clockValid;
    if (clockValid && scheduleHasData()) {
      activeAlert = scheduleTick(rtcMinutesOfDay(), rtcDayKey(), firstTickAfterClock);
      firstTickAfterClock = false;
      persistClosedDoses();
    }
  }

  // Process physical gestures before any blocking network work or queued commands.
  handleButtons();
  persistClosedDoses();  // มื้อที่เพิ่งรับหรือข้ามด้วยปุ่ม บันทึกทันที ไม่รอรอบถัดไป
  if (wifiSetupActive())
  {
    alertSet(AlertPattern::None);
    alertUpdate();
    statusLedFlash(LedColor::Green);  // ค้างเขียวไว้ตลอดที่อยู่ในโหมดตั้งค่า
    statusLedUpdate();
    showSetupScreen();
    lcdMedicineTick();
    return;
  }

  // A short button press above may have started motion in this same iteration.
  if (dispenserIsBusy()) { alertUpdate(); return; }
  // Keep reading the hold gesture promptly; defer network work until release.
  // ทุกอย่างในนี้ไม่ block แล้ว: การรับส่งจริงอยู่ใน task เบื้องหลัง ตรงนี้แค่ส่งงานกับรับผล
  if (!buttonHeld(ButtonId::Dispense))
  {
    netSyncService();  // ผลที่เสร็จแล้ว (ตารางใหม่ เวลา คำสั่ง) นำไปใช้ตรงนี้

    // ส่งผลการจ่ายยาก่อนขอตารางใหม่ เพราะ server ใช้ผลนี้ตัดสินว่ามื้อไหนเสร็จแล้ว
    // ทำได้ทีละงาน ถ้าส่งไม่ได้ (มีงานค้าง) ต้องไม่ล้างธง ไม่อย่างนั้นผลจะรอไปอีก 10 วินาที
    const bool flushDue = millis() - lastEventFlushMs >= EVENT_FLUSH_INTERVAL_MS;
    if ((flushRequested || flushDue) && eventQueueSize() > 0) {
      if (netSyncFlushEvents()) {
        lastEventFlushMs = millis();
        flushRequested = false;
      }
    } else if (flushDue) {
      lastEventFlushMs = millis();
      flushRequested = false;
    }
    // sync ถ้าถึงรอบ ไม่อย่างนั้นเปิดสายรอให้ server บอกทันทีเมื่อมีอะไรเปลี่ยน
    netSyncPump();
  }

  // ช่องที่รอคิวอยู่เริ่มเองทันทีที่มีจานว่าง ผู้ใช้จึงกดปุ่มครั้งเดียวพอ
  startPendingDoses();

  // คำสั่งจากเว็บทำได้เฉพาะตอนที่กลไกว่างและไม่มีมื้อยาค้างคิวอยู่
  if (!buttonHeld(ButtonId::Dispense) && !dispenserIsBusy() && pendingDoseCount == 0)
  {
    RemoteCommand command;
    if (netSyncTakeCommand(command))
    {
      if (strcmp(command.type, "DISPENSE") == 0)
      {
        startCommandDispense(command);
      }
      else if (strcmp(command.type, "BUZZ") == 0)
      {
        alertOneShot(AlertPattern::Warning);
        showNotice("CALL FROM WEB");
        reportCommand(command, "ACK", "buzz");
      }
      else if (strcmp(command.type, "CANCEL") == 0)
      {
        stopDispenser();
        cancelPendingDoses();
        reportCommand(command, "ACK", "cancel");
      }
      else
      {
        // คำสั่งที่ firmware รุ่นนี้ยังไม่รู้จัก ต้องปิดให้จบไม่งั้นจะค้างจนหมดอายุ
        Serial.printf("[คำสั่ง] ไม่รู้จักชนิด %s\n", command.type);
        reportCommand(command, "ACK", "unsupported");
      }
    }
  }

  if (dispenserIsBusy()) { alertUpdate(); return; }
  updateBuzzer();
  alertUpdate();
  updateStatusLed();
  updateDisplay();
  lcdMedicineTick();  // ขยับข้อความเลื่อนและสลับบรรทัดล่าง
}

const char *appManualDispense(uint8_t slotNumber, float amount, int &httpStatus)
{
  const int slotIndex = scheduleFindSlot(slotNumber);
  if (slotIndex < 0)
  {
    httpStatus = 404;
    return "ไม่พบช่องนี้ในตารางที่ sync มาจาก server";
  }

  const Slot &slot = scheduleSlot(slotIndex);
  if (slot.medicationId[0] == '\0')
  {
    httpStatus = 409;
    return "ช่องนี้ยังไม่ได้ใส่ยาในระบบ";
  }

  const float requested = amount > 0 ? amount : slot.amountPerDose;
  const DispenseResult result = dispenseMedicine(slotNumber, pillsFor(requested), slot.pillHole);
  if (result != DispenseResult::Started)
  {
    httpStatus = result == DispenseResult::Busy || result == DispenseResult::Cancelled ? 409 : 503;
    return describe(result);
  }

  // ใช้เส้นทางเดียวกับคำสั่งจากเว็บ เพื่อให้ผลถูกบันทึกขึ้น server เหมือนกัน
  const uint8_t index = static_cast<uint8_t>(slotNumber - 1);
  memset(&runningCommand[index], 0, sizeof(runningCommand[0]));
  strncpy(runningCommand[index].type, "DISPENSE", sizeof(runningCommand[0].type) - 1);
  runningCommand[index].slot = slotNumber;
  runningCommand[index].amount = requested;
  runOwner[index] = RunOwner::Command;

  httpStatus = 202;
  return "เริ่มจ่ายยาแล้ว (ยังไม่ได้ยืนยันจำนวนเม็ดด้วย IR)";
}

void appStatusJson(String &out)
{
  const bool online = WiFi.status() == WL_CONNECTED;

  out = "{";
  out += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  out += "\"wifi_connected\":" + String(online ? "true" : "false") + ",";
  out += "\"ip\":\"" + (online ? WiFi.localIP().toString() : String("-")) + "\",";
  out += "\"rssi\":" + String(online ? WiFi.RSSI() : 0) + ",";
  out += "\"clock_valid\":" + String(rtcIsValid() ? "true" : "false") + ",";
  out += "\"clock_source\":\"" + String(rtcClockSource()) + "\",";
  out += "\"dry_run\":" + String(!ENABLE_SERVO_MOVEMENT && DISPENSE_DRY_RUN ? "true" : "false") + ",";
  out += "\"minutes_of_day\":" + String(rtcMinutesOfDay()) + ",";
  out += "\"servo_movement_enabled\":" + String(ENABLE_SERVO_MOVEMENT ? "true" : "false") + ",";
  out += "\"dispenser_busy\":" + String(dispenserIsBusy() ? "true" : "false") + ",";
  out += "\"sync_ok\":" + String(netSyncLastCallOk() ? "true" : "false") + ",";
  out += "\"sync_error\":\"" + String(netSyncLastError()) + "\",";
  out += "\"config_version\":\"" + String(netSyncConfigVersion()) + "\",";
  out += "\"pending_events\":" + String(eventQueueSize()) + ",";

  // รายการ address ที่เจอบนบัส I2C ใช้ไล่ปัญหาจอไม่ขึ้นโดยไม่ต้องเสียบ USB
  out += "\"i2c_found\":[";
  for (uint8_t i = 0; i < i2cFoundCount(); ++i)
  {
    char hex[8];
    snprintf(hex, sizeof(hex), "\"0x%02X\"", i2cFoundAddress(i));
    if (i > 0)
      out += ",";
    out += hex;
  }
  out += "],";
  {
    const I2cLineState lines = i2cLastLineState();
    char diag[160];
    snprintf(diag,
             sizeof(diag),
             "\"i2c_lines\":{\"sda_pullup_ext\":%s,\"scl_pullup_ext\":%s,"
             "\"sda_ok\":%s,\"scl_ok\":%s},",
             lines.sdaHighWithoutPullup ? "true" : "false",
             lines.sclHighWithoutPullup ? "true" : "false",
             lines.sdaHighWithPullup ? "true" : "false",
             lines.sclHighWithPullup ? "true" : "false");
    out += diag;
  }
  {
    char expected[48];
    snprintf(expected,
             sizeof(expected),
             "\"i2c_expected\":[\"0x%02X\",\"0x%02X\",\"0x68\"],",
             LCD_TIME_ADDRESS,
             LCD_MEDICINE_ADDRESS);
    out += expected;
  }
  out += "\"slots\":[";

  for (uint8_t i = 0; i < scheduleSlotCount(); ++i)
  {
    const Slot &slot = scheduleSlot(i);
    if (i > 0)
      out += ",";
    out += "{\"slot\":" + String(slot.number);
    out += ",\"active\":" + String(slot.active ? "true" : "false");
    out += ",\"name\":\"" + String(slot.name) + "\"";
    out += ",\"amount_per_dose\":" + String(slot.amountPerDose, 1);
    out += ",\"doses\":[";
    for (uint8_t d = 0; d < slot.doseCount; ++d)
    {
      const Dose &dose = slot.doses[d];
      if (d > 0)
        out += ",";
      char time[6];
      snprintf(time, sizeof(time), "%02d:%02d", dose.minutes / 60, dose.minutes % 60);
      out += "{\"time\":\"" + String(time) + "\",\"state\":\"" + String(doseStateName(dose.state)) + "\"}";
    }
    out += "]}";
  }

  out += "]}";
}
