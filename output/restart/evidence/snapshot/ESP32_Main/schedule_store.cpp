#include "schedule_store.h"

namespace {
Slot slots[DISPENSER_COUNT];
uint8_t slotCount = 0;

// buffer พักระหว่างรับข้อมูลใหม่ ตารางที่ใช้งานอยู่จะยังไม่ถูกแตะจนกว่าจะ commit
Slot staging[DISPENSER_COUNT];
uint8_t stagingCount = 0;

DoseStateChangedFn stateChanged = nullptr;
uint32_t currentDayKey = 0;
bool dayRollover = false;
bool hasData = false;

void copyText(char *dest, size_t size, const char *source)
{
  if (!source)
  {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, source, size - 1);
  dest[size - 1] = '\0';
}

/** หาสถานะเดิมของมื้อยาที่ id ตรงกัน เพื่อไม่ให้ sync ระหว่างวันลบความคืบหน้าทิ้ง */
bool findPreviousDose(const char *scheduleId, Dose &out)
{
  for (uint8_t s = 0; s < slotCount; ++s)
  {
    for (uint8_t d = 0; d < slots[s].doseCount; ++d)
    {
      if (strcmp(slots[s].doses[d].scheduleId, scheduleId) == 0)
      {
        out = slots[s].doses[d];
        return true;
      }
    }
  }
  return false;
}
}

void scheduleBegin(DoseStateChangedFn onStateChanged)
{
  stateChanged = onStateChanged;
  slotCount = 0;
  stagingCount = 0;
  hasData = false;
  currentDayKey = 0;
  dayRollover = false;
}

// ---------------------------------------------------------------------------
// การรับข้อมูลจาก server
// ---------------------------------------------------------------------------

void scheduleBeginSync()
{
  stagingCount = 0;
  memset(staging, 0, sizeof(staging));
}

int scheduleStageSlot(uint8_t number,
                      bool active,
                      const char *medicationId,
                      const char *name,
                      float amountPerDose)
{
  if (stagingCount >= DISPENSER_COUNT || number < 1 || number > DISPENSER_COUNT)
    return -1;

  Slot &slot = staging[stagingCount];
  slot.number = number;
  slot.active = active;
  slot.amountPerDose = amountPerDose > 0 ? amountPerDose : 1.0f;
  // ต้องรีเซ็ตทุกครั้ง: staging ถูกใช้ซ้ำ ถ้าไม่ล้าง ขนาดยาของยาตัวก่อนจะติดมากับยาตัวใหม่
  slot.pillHole = PILL_HOLE_ANY;
  slot.doseCount = 0;
  copyText(slot.medicationId, sizeof(slot.medicationId), medicationId);
  copyText(slot.name, sizeof(slot.name), name);

  return stagingCount++;
}

void scheduleStageSlotPillHole(int slotIndex, int8_t hole)
{
  if (slotIndex < 0 || slotIndex >= stagingCount)
    return;
  staging[slotIndex].pillHole =
      hole >= 0 && hole < static_cast<int8_t>(PILL_HOLE_COUNT) ? hole : PILL_HOLE_ANY;
}

int8_t scheduleSlotPillHole(uint8_t number)
{
  for (uint8_t i = 0; i < slotCount; ++i)
  {
    if (slots[i].number == number)
      return slots[i].pillHole;
  }
  return PILL_HOLE_ANY;
}

void scheduleStageDose(int slotIndex,
                       const char *scheduleId,
                       const char *label,
                       int minutes,
                       bool doneOnServer)
{
  if (slotIndex < 0 || slotIndex >= stagingCount)
    return;

  Slot &slot = staging[slotIndex];
  if (slot.doseCount >= MAX_DOSES_PER_SLOT || !scheduleId || scheduleId[0] == '\0')
    return;

  Dose &dose = slot.doses[slot.doseCount];
  copyText(dose.scheduleId, sizeof(dose.scheduleId), scheduleId);
  copyText(dose.label, sizeof(dose.label), label);
  dose.minutes = minutes;
  dose.state = doneOnServer ? DoseState::Done : DoseState::Pending;
  dose.failureReported = false;
  dose.snoozedUntil = -1;
  dose.snoozeCount = 0;

  // มื้อที่เครื่องกำลังจัดการอยู่ต้องไม่ถูก sync ดึงกลับไปเป็น Pending
  // รวมถึงการเลื่อนที่ผู้ใช้กดไว้ ต้องไม่หายไปเพราะ sync รอบใหม่
  Dose previous;
  if (!doneOnServer && findPreviousDose(dose.scheduleId, previous))
  {
    dose.state = previous.state;
    dose.failureReported = previous.failureReported;
    dose.snoozedUntil = previous.snoozedUntil;
    dose.snoozeCount = previous.snoozeCount;
  }

  ++slot.doseCount;
}

void scheduleCommitSync()
{
  memcpy(slots, staging, sizeof(slots));
  slotCount = stagingCount;
  hasData = true;
}

// ---------------------------------------------------------------------------
// การใช้งาน
// ---------------------------------------------------------------------------

uint8_t scheduleSlotCount()
{
  return slotCount;
}

const Slot &scheduleSlot(uint8_t index)
{
  return slots[index];
}

int scheduleFindSlot(uint8_t slotNumber)
{
  for (uint8_t i = 0; i < slotCount; ++i)
  {
    if (slots[i].number == slotNumber)
      return i;
  }
  return -1;
}

DoseRef scheduleFindByScheduleId(const char *scheduleId)
{
  DoseRef ref = {0, 0, false};
  if (!scheduleId || scheduleId[0] == '\0')
    return ref;

  for (uint8_t s = 0; s < slotCount; ++s)
  {
    for (uint8_t d = 0; d < slots[s].doseCount; ++d)
    {
      if (strcmp(slots[s].doses[d].scheduleId, scheduleId) == 0)
        return DoseRef{s, d, true};
    }
  }
  return ref;
}

Dose *scheduleDoseAt(const DoseRef &ref)
{
  if (!ref.valid || ref.slotIndex >= slotCount)
    return nullptr;
  if (ref.doseIndex >= slots[ref.slotIndex].doseCount)
    return nullptr;
  return &slots[ref.slotIndex].doses[ref.doseIndex];
}

const Slot *scheduleSlotOf(const DoseRef &ref)
{
  if (!ref.valid || ref.slotIndex >= slotCount)
    return nullptr;
  return &slots[ref.slotIndex];
}

void scheduleSetState(const DoseRef &ref, DoseState state)
{
  Dose *dose = scheduleDoseAt(ref);
  if (!dose || dose->state == state)
    return;

  const DoseState previous = dose->state;
  dose->state = state;

  const Slot *slot = scheduleSlotOf(ref);
  Serial.printf("[มื้อยา] ช่อง %u %02d:%02d %s -> %s\n",
                slot ? slot->number : 0,
                dose->minutes / 60,
                dose->minutes % 60,
                doseStateName(previous),
                doseStateName(state));

  if (stateChanged)
    stateChanged(ref, previous, state);
}

DoseRef scheduleTick(int nowMinutes, uint32_t dayKey, bool justBooted)
{
  DoseRef alerting = {0, 0, false};

  if (dayKey != currentDayKey)
  {
    if (currentDayKey != 0)
    {
      // ขึ้นวันใหม่: ล้างสถานะของเมื่อวานทิ้ง แล้วรอ sync ตารางของวันนี้
      for (uint8_t s = 0; s < slotCount; ++s)
      {
        for (uint8_t d = 0; d < slots[s].doseCount; ++d)
        {
          slots[s].doses[d].state = DoseState::Pending;
          slots[s].doses[d].failureReported = false;
          slots[s].doses[d].snoozedUntil = -1;
          slots[s].doses[d].snoozeCount = 0;
        }
      }
      dayRollover = true;
    }
    currentDayKey = dayKey;
  }

  for (uint8_t s = 0; s < slotCount; ++s)
  {
    Slot &slot = slots[s];
    if (!slot.active || slot.medicationId[0] == '\0')
      continue;

    for (uint8_t d = 0; d < slot.doseCount; ++d)
    {
      Dose &dose = slot.doses[d];
      const DoseRef ref = {s, d, true};
      const int dueAt = dose.minutes - ALERT_LEAD_MINUTES;
      const int deadline = dose.minutes + ALERT_TIMEOUT_MINUTES;

      if (dose.state == DoseState::Pending && nowMinutes >= dueAt)
      {
        if (justBooted && nowMinutes > dose.minutes + STALE_DOSE_MINUTES)
        {
          // เพิ่งเปิดเครื่องแล้วพบมื้อที่เลยมานาน: ไม่ปลุกย้อนหลังและไม่รายงานซ้ำ
          // เพราะฝั่ง server ถือว่ามื้อที่ไม่มี log = MISSED อยู่แล้ว
          dose.state = DoseState::Missed;
          continue;
        }
        scheduleSetState(ref, DoseState::Alerting);
      }

      // ครบเวลาที่เลื่อนไว้แล้ว กลับมาเตือนต่อ
      if (dose.state == DoseState::Snoozed && nowMinutes >= dose.snoozedUntil)
      {
        dose.snoozedUntil = -1;
        scheduleSetState(ref, DoseState::Alerting);
      }

      // เลยเวลาผ่อนผันแล้วถือว่าขาดยา
      //
      // ไม่ต้องตรวจสถานะ Snoozed ตรงนี้ เพราะ scheduleSnooze() การันตีว่า snoozedUntil
      // ไม่เกิน deadline เสมอ มื้อที่เลื่อนไว้จึงถูกปลุกกลับเป็น Alerting ในบล็อกด้านบน
      // ก่อนถึงบรรทัดนี้แล้ว
      if (dose.state == DoseState::Alerting && nowMinutes > deadline)
      {
        dose.snoozedUntil = -1;
        scheduleSetState(ref, DoseState::Missed);
        continue;
      }

      // เตือนทีละมื้อ โดยให้มื้อที่ถึงเวลาก่อนได้สิทธิ์ก่อน
      // มื้อที่กำลังเลื่อนอยู่ไม่ถูกเลือก จึงไม่มีเสียงเตือนระหว่างนั้น
      if (dose.state == DoseState::Alerting || dose.state == DoseState::Dispensing)
      {
        const Dose *current = scheduleDoseAt(alerting);
        if (!current || dose.minutes < current->minutes)
          alerting = ref;
      }
    }
  }

  return alerting;
}

uint8_t scheduleAlertingDoses(DoseRef *out, uint8_t maxCount)
{
  if (!out || maxCount == 0)
    return 0;

  uint8_t found = 0;
  for (uint8_t s = 0; s < slotCount && found < maxCount; ++s)
  {
    const Slot &slot = slots[s];
    if (!slot.active || slot.medicationId[0] == '\0')
      continue;

    for (uint8_t d = 0; d < slot.doseCount && found < maxCount; ++d)
    {
      if (slot.doses[d].state != DoseState::Alerting)
        continue;
      out[found++] = {s, d, true};
    }
  }
  return found;
}

DoseRef scheduleNextUpcoming(int nowMinutes)
{
  DoseRef best = {0, 0, false};
  int bestMinutes = 24 * 60 + 1;

  for (uint8_t s = 0; s < slotCount; ++s)
  {
    const Slot &slot = slots[s];
    if (!slot.active || slot.medicationId[0] == '\0')
      continue;

    for (uint8_t d = 0; d < slot.doseCount; ++d)
    {
      const Dose &dose = slot.doses[d];
      if (dose.state != DoseState::Pending || dose.minutes < nowMinutes)
        continue;
      if (dose.minutes < bestMinutes)
      {
        bestMinutes = dose.minutes;
        best = {s, d, true};
      }
    }
  }

  return best;
}

bool scheduleCanSnooze(const DoseRef &ref, int nowMinutes)
{
  const Dose *dose = scheduleDoseAt(ref);
  if (!dose)
    return false;

  // เลื่อนได้เฉพาะตอนที่กำลังเตือนอยู่เท่านั้น
  if (dose->state != DoseState::Alerting)
    return false;
  if (dose->snoozeCount >= MAX_SNOOZE_PER_DOSE)
    return false;

  // เลื่อนไปก็จะตื่นหลังหมดเวลาผ่อนผันอยู่ดี จึงไม่ให้เลื่อน
  return nowMinutes + SNOOZE_MINUTES <= dose->minutes + ALERT_TIMEOUT_MINUTES;
}

bool scheduleSnooze(const DoseRef &ref, int nowMinutes)
{
  Dose *dose = scheduleDoseAt(ref);
  if (!dose)
    return false;

  if (!scheduleCanSnooze(ref, nowMinutes))
  {
    if (dose->state == DoseState::Alerting)
    {
      if (dose->snoozeCount >= MAX_SNOOZE_PER_DOSE)
        Serial.printf("[มื้อยา] เลื่อนครบ %u ครั้งแล้ว เลื่อนต่อไม่ได้\n",
                      static_cast<unsigned>(MAX_SNOOZE_PER_DOSE));
      else
        Serial.println("[มื้อยา] เลื่อนไม่ได้ เพราะจะเลยเวลาผ่อนผัน");
    }
    return false;
  }

  const int wakeAt = nowMinutes + SNOOZE_MINUTES;
  dose->snoozedUntil = wakeAt;
  ++dose->snoozeCount;
  scheduleSetState(ref, DoseState::Snoozed);

  Serial.printf("[มื้อยา] เลื่อนไปที่ %02d:%02d (ครั้งที่ %u)\n",
                wakeAt / 60,
                wakeAt % 60,
                static_cast<unsigned>(dose->snoozeCount));
  return true;
}

DoseRef scheduleFirstSnoozed()
{
  DoseRef best = {0, 0, false};
  int bestWake = 24 * 60 + 1;

  for (uint8_t s = 0; s < slotCount; ++s)
  {
    for (uint8_t d = 0; d < slots[s].doseCount; ++d)
    {
      const Dose &dose = slots[s].doses[d];
      if (dose.state != DoseState::Snoozed)
        continue;
      if (dose.snoozedUntil >= 0 && dose.snoozedUntil < bestWake)
      {
        bestWake = dose.snoozedUntil;
        best = DoseRef{s, d, true};
      }
    }
  }
  return best;
}

uint8_t scheduleSnoozeCount(const DoseRef &ref)
{
  const Dose *dose = scheduleDoseAt(ref);
  return dose ? dose->snoozeCount : 0;
}

bool doseIsOpen(const Dose &dose)
{
  return dose.state == DoseState::Pending || dose.state == DoseState::Alerting ||
         dose.state == DoseState::Snoozed || dose.state == DoseState::Dispensing || dose.state == DoseState::Queued;
}

int doseEffectiveMinutes(const Dose &dose)
{
  if (dose.state == DoseState::Snoozed && dose.snoozedUntil >= 0)
    return dose.snoozedUntil;
  return dose.minutes;
}

const char *doseStateName(DoseState state)
{
  switch (state)
  {
    case DoseState::Pending: return "Pending";
    case DoseState::Alerting: return "Alerting";
    case DoseState::Queued: return "Queued";
    case DoseState::Dispensing: return "Dispensing";
    case DoseState::Done: return "Done";
    case DoseState::Missed: return "Missed";
    case DoseState::Skipped: return "Skipped";
    case DoseState::Snoozed: return "Snoozed";
    case DoseState::Failed: return "Failed";
  }
  return "Unknown";
}

uint32_t scheduleDayKey()
{
  return currentDayKey;
}

bool scheduleConsumeDayRollover()
{
  const bool rolled = dayRollover;
  dayRollover = false;
  return rolled;
}

bool scheduleHasData()
{
  return hasData;
}
