#include "pill_app.h"

#include "alert.h"
#include "buttons.h"
#include "config.h"
#include "dispenser_control.h"
#include "event_queue.h"
#include "net_sync.h"
#include "rtc_lcd.h"
#include "schedule_store.h"
#include "wifi_web.h"

#include <WiFi.h>
#include <math.h>

namespace {

/** เจ้าของรอบการจ่ายที่กำลังทำงานอยู่ ใช้ตัดสินว่าผลที่ได้ต้องรายงานแบบไหน */
enum class RunOwner : uint8_t { None, Dose, Command };

DoseRef activeAlert = {0, 0, false};
RunOwner runOwner = RunOwner::None;
RemoteCommand runningCommand;

// อ้างอิงมื้อยาที่กำลังจ่ายด้วย schedule_id ไม่ใช่ index เพราะอาจมี sync คั่นระหว่างที่จานหมุน
// แล้วทำให้ลำดับของมื้อยาเปลี่ยนไป
char runningScheduleId[40] = "";

bool clockWasValid = false;
bool firstTickAfterClock = true;
unsigned long lastEventFlushMs = 0;
bool flushRequested = false;  // มีผลใหม่เข้าคิว ให้ส่งทันทีที่จบรอบ loop
unsigned long cancelPressedAtMs = 0;
bool cancelPressActive = false;
char lastNotice[24] = "";
unsigned long noticeUntilMs = 0;

// ---------------------------------------------------------------------------
// การรายงานผลขึ้น server
// ---------------------------------------------------------------------------

void fillEvent(PendingEvent &event, const char *status)
{
  memset(&event, 0, sizeof(event));
  eventQueueMakeId(event.eventId, sizeof(event.eventId));
  strncpy(event.status, status, sizeof(event.status) - 1);
  event.localEpoch = rtcLocalEpoch();
}

void queueEvent(PendingEvent &event)
{
  if (!eventQueuePush(event))
    Serial.println("[คิว] คิวผลการจ่ายยาเต็ม จำเป็นต้องทิ้งรายการเก่าสุด");

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
void onDoseStateChanged(const DoseRef &ref, DoseState previous, DoseState next)
{
  (void)previous;

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
 * กลไกจ่ายได้ทีละเม็ดเต็มเท่านั้น จึงปัดขึ้น แล้วคุมไม่ให้เกินเพดาน
 */
uint8_t pillsFor(float amount)
{
  if (amount <= 0)
    return 1;

  int pills = static_cast<int>(ceilf(amount));
  if (pills < 1)
    pills = 1;
  if (pills > MAX_PILLS_PER_DOSE)
    pills = MAX_PILLS_PER_DOSE;
  return static_cast<uint8_t>(pills);
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
    case DispenseResult::ServoError: return "servo attach failed";
  }
  return "unknown";
}

/** ผู้ใช้กดปุ่มรับยาขณะที่มื้อนั้นกำลังเตือน */
void startDoseDispense(const DoseRef &ref)
{
  const Slot *slot = scheduleSlotOf(ref);
  Dose *dose = scheduleDoseAt(ref);
  if (!slot || !dose)
    return;

  const DispenseResult result = dispenseMedicine(slot->number, pillsFor(slot->amountPerDose));

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
    return;
  }

  if (result != DispenseResult::Started)
  {
    Serial.printf("[จ่ายยา] ช่อง %u ไม่สำเร็จ: %s\n", slot->number, describe(result));
    alertOneShot(AlertPattern::Warning);
    showNotice(result == DispenseResult::Disabled ? "SERVO DISABLED" : "DISPENSE FAILED");

    // รายงาน FAILED เพียงครั้งเดียวต่อมื้อ ผู้ใช้ยังกดลองใหม่ได้จนหมดเวลาผ่อนผัน
    if (!dose->failureReported)
    {
      dose->failureReported = true;
      reportDose(ref, "FAILED", reasonCode(result));
    }
    return;
  }

  strncpy(runningScheduleId, dose->scheduleId, sizeof(runningScheduleId) - 1);
  runningScheduleId[sizeof(runningScheduleId) - 1] = '\0';
  runOwner = RunOwner::Dose;
  scheduleSetState(ref, DoseState::Dispensing);
  alertSet(AlertPattern::None);
  lcdShowMessage(slot->name, "Dispensing...");
}

void startCommandDispense(const RemoteCommand &command)
{
  const DispenseResult result = dispenseMedicine(command.slot, pillsFor(command.amount));
  if (result != DispenseResult::Started)
  {
    Serial.printf("[คำสั่ง] ช่อง %u ไม่สำเร็จ: %s\n", command.slot, describe(result));
    alertOneShot(AlertPattern::Warning);
    reportCommand(command, "FAILED", reasonCode(result));
    return;
  }

  runningCommand = command;
  runOwner = RunOwner::Command;
  lcdShowMessage("From website", "Dispensing...");
}

/** ตรวจว่ารอบที่กำลังหมุนจบหรือยัง แล้วรายงานผลตามเจ้าของรอบนั้น */
void handleDispenseOutcome()
{
  DispenseOutcome outcome;
  if (!takeDispenseOutcome(outcome))
    return;

  const bool complete = !outcome.cancelled && outcome.dispensedPills >= outcome.requestedPills;
  const RunOwner owner = runOwner;
  runOwner = RunOwner::None;

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
    const DoseRef ref = scheduleFindByScheduleId(runningScheduleId);
    runningScheduleId[0] = '\0';

    Dose *dose = scheduleDoseAt(ref);
    if (!dose)
    {
      Serial.println("[จ่ายยา] มื้อยาที่กำลังจ่ายหายไปจากตารางหลัง sync จึงไม่บันทึกผล");
      return;
    }

    if (complete)
    {
      alertOneShot(AlertPattern::Success);
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
      showNotice("DISPENSE STOPPED");
      // กลับไปเตือนต่อเพื่อให้ผู้ใช้กดลองใหม่ได้ภายในเวลาผ่อนผัน
      scheduleSetState(ref, DoseState::Alerting);
      if (!dose->failureReported)
      {
        dose->failureReported = true;
        reportDose(ref, "FAILED", "not enough pills");
      }
    }
    return;
  }

  if (owner == RunOwner::Command)
  {
    alertOneShot(complete ? AlertPattern::Success : AlertPattern::Warning);
    reportCommand(runningCommand,
                  complete ? "DISPENSED" : "FAILED",
                  complete ? unverified : "not enough pills");
  }
}

// ---------------------------------------------------------------------------
// ปุ่มกด
// ---------------------------------------------------------------------------

void handleButtons()
{
  Dose *alertDose = scheduleDoseAt(activeAlert);
  const bool alerting = alertDose && alertDose->state == DoseState::Alerting;

  if (buttonPressed(ButtonId::Dispense))
  {
    if (alerting && !dispenserIsBusy())
    {
      alertOneShot(AlertPattern::Click);
      startDoseDispense(activeAlert);
    }
    else
    {
      alertOneShot(AlertPattern::Warning);
      showNotice(dispenserIsBusy() ? "BUSY" : "NO DOSE DUE");
    }
  }

  if (buttonPressed(ButtonId::Snooze))
  {
    if (alerting)
    {
      // ปุ่มเหลือง: ยังไม่สะดวกตอนนี้ ขอเลื่อนไปอีก SNOOZE_MINUTES นาที
      if (scheduleSnooze(activeAlert, rtcMinutesOfDay()))
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
    alertOneShot(AlertPattern::Warning);
    showNotice("STOPPED");
  }

  if (cancelPressActive && !buttonHeld(ButtonId::Cancel))
  {
    cancelPressActive = false;
    if (millis() - cancelPressedAtMs < CANCEL_HOLD_MS && alerting)
    {
      alertOneShot(AlertPattern::Click);
      showNotice("SKIPPED");
      scheduleSetState(activeAlert, DoseState::Skipped);
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

void appBegin()
{
  buttonsBegin();
  alertBegin();
  eventQueueBegin();
  scheduleBegin(onDoseStateChanged);

  rtcLcdBegin();
  dispenserControlBegin();

  wifiWebBegin();
  netSyncBegin();
}

void appLoop()
{
  buttonsUpdate();
  wifiWebLoop();
  dispenserControlUpdate();
  rtcLcdUpdate();

  // Setup is entered only at boot; preserve emergency buttons while configuring Wi-Fi.
  if (wifiSetupActive()) { handleButtons(); lcdShowMessage("Wi-Fi Setup", "192.168.4.1"); return; }

  // ตารางยาของวันใหม่ต้องดึงใหม่ทันทีที่ข้ามเที่ยงคืน
  if (scheduleConsumeDayRollover())
    netSyncRequestNow();

  // ได้เวลาจาก RTC ครั้งแรกหลังบูต: ต้องรู้ก่อนว่าจะข้ามมื้อที่เลยมานานหรือไม่
  const bool clockValid = rtcIsValid();
  if (clockValid && !clockWasValid)
    firstTickAfterClock = true;
  clockWasValid = clockValid;

  if (netSyncDue())
    netSyncFetch();

  const bool flushDue = millis() - lastEventFlushMs >= EVENT_FLUSH_INTERVAL_MS;
  if ((flushRequested || flushDue) && eventQueueSize() > 0)
  {
    lastEventFlushMs = millis();
    flushRequested = false;
    netSyncFlushEvents();
  }
  else if (flushDue)
  {
    lastEventFlushMs = millis();
    flushRequested = false;
  }

  if (clockValid && scheduleHasData())
  {
    activeAlert = scheduleTick(rtcMinutesOfDay(), rtcDayKey(), firstTickAfterClock);
    firstTickAfterClock = false;
  }

  handleDispenseOutcome();
  handleButtons();

  // คำสั่งจากเว็บทำได้เฉพาะตอนที่กลไกว่างและไม่มีมื้อยากำลังเตือนค้างอยู่
  if (!dispenserIsBusy() && runOwner == RunOwner::None)
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

  updateBuzzer();
  alertUpdate();
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
  const DispenseResult result = dispenseMedicine(slotNumber, pillsFor(requested));
  if (result != DispenseResult::Started)
  {
    httpStatus = result == DispenseResult::Busy || result == DispenseResult::Cancelled ? 409 : 503;
    return describe(result);
  }

  // ใช้เส้นทางเดียวกับคำสั่งจากเว็บ เพื่อให้ผลถูกบันทึกขึ้น server เหมือนกัน
  memset(&runningCommand, 0, sizeof(runningCommand));
  strncpy(runningCommand.type, "DISPENSE", sizeof(runningCommand.type) - 1);
  runningCommand.slot = slotNumber;
  runningCommand.amount = requested;
  runOwner = RunOwner::Command;

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
