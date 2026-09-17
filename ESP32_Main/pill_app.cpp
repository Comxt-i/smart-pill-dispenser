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
  if (note)
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

uint8_t cyclesFor(float amount)
{
  if (amount <= 0)
    return 1;

  const float raw = amount / PILLS_PER_CYCLE;
  int cycles = static_cast<int>(ceilf(raw));
  if (cycles < 1)
    cycles = 1;
  if (cycles > MAX_CYCLES_PER_DOSE)
    cycles = MAX_CYCLES_PER_DOSE;
  return static_cast<uint8_t>(cycles);
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

  const DispenseResult result = dispenseMedicine(slot->number, cyclesFor(slot->amountPerDose));
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
  const DispenseResult result = dispenseMedicine(command.slot, cyclesFor(command.amount));
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

  const bool complete = !outcome.cancelled && outcome.completedCycles >= outcome.requestedCycles;
  const RunOwner owner = runOwner;
  runOwner = RunOwner::None;

  Serial.printf("[จ่ายยา] ช่อง %u หมุน %u/%u รอบ%s\n",
                outcome.dispenser,
                outcome.completedCycles,
                outcome.requestedCycles,
                outcome.cancelled ? " (ถูกยกเลิก)" : "");

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
      scheduleSetState(ref, DoseState::Done);  // callback จะส่ง DISPENSED ให้เอง
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
        reportDose(ref, "FAILED", "incomplete cycles");
      }
    }
    return;
  }

  if (owner == RunOwner::Command)
  {
    alertOneShot(complete ? AlertPattern::Success : AlertPattern::Warning);
    reportCommand(runningCommand,
                  complete ? "DISPENSED" : "FAILED",
                  complete ? nullptr : "incomplete cycles");
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

  if (buttonPressed(ButtonId::Confirm))
  {
    if (alerting)
    {
      // ผู้ใช้ทานยาเองจากซองแล้ว ปิดการเตือนและบันทึกว่ามื้อนี้เรียบร้อย
      alertOneShot(AlertPattern::Success);
      showNotice("CONFIRMED");
      scheduleSetState(activeAlert, DoseState::Done);
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

void updateDisplay()
{
  if (static_cast<long>(noticeUntilMs - millis()) > 0 && lastNotice[0] != '\0')
  {
    lcdShowMessage(lastNotice, WiFi.status() == WL_CONNECTED ? "Online" : "Offline");
    return;
  }

  if (dispenserIsBusy())
  {
    lcdShowMessage("Dispensing...", "Please wait");
    return;
  }

  const Dose *alertDose = scheduleDoseAt(activeAlert);
  if (alertDose && alertDose->state == DoseState::Alerting)
  {
    const Slot *slot = scheduleSlotOf(activeAlert);
    lcdShowAlert(slot ? slot->name : "Medicine", slot ? slot->amountPerDose : 1.0f, alertDose->minutes);
    return;
  }

  if (!rtcIsValid())
  {
    lcdShowMessage("Clock not set", WiFi.status() == WL_CONNECTED ? "Syncing..." : "No Wi-Fi");
    return;
  }

  if (!scheduleHasData())
  {
    lcdShowMessage("Waiting sync", WiFi.status() == WL_CONNECTED ? "Online" : "No Wi-Fi");
    return;
  }

  const DoseRef next = scheduleNextUpcoming(rtcMinutesOfDay());
  const Dose *nextDose = scheduleDoseAt(next);
  if (nextDose)
  {
    const Slot *slot = scheduleSlotOf(next);
    lcdShowNextDose(slot ? slot->name : "Medicine", nextDose->minutes);
    return;
  }

  lcdShowIdle(WiFi.status() == WL_CONNECTED);
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
  const DispenseResult result = dispenseMedicine(slotNumber, cyclesFor(requested));
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
