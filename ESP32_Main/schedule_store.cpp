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
  slot.doseCount = 0;
  copyText(slot.medicationId, sizeof(slot.medicationId), medicationId);
  copyText(slot.name, sizeof(slot.name), name);

  return stagingCount++;
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

  // มื้อที่เครื่องกำลังจัดการอยู่ต้องไม่ถูก sync ดึงกลับไปเป็น Pending
  Dose previous;
  if (!doneOnServer && findPreviousDose(dose.scheduleId, previous))
  {
    dose.state = previous.state;
    dose.failureReported = previous.failureReported;
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

      if (dose.state == DoseState::Alerting && nowMinutes > deadline)
      {
        scheduleSetState(ref, DoseState::Missed);
        continue;
      }

      // เตือนทีละมื้อ โดยให้มื้อที่ถึงเวลาก่อนได้สิทธิ์ก่อน
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

const char *doseStateName(DoseState state)
{
  switch (state)
  {
    case DoseState::Pending: return "Pending";
    case DoseState::Alerting: return "Alerting";
    case DoseState::Dispensing: return "Dispensing";
    case DoseState::Done: return "Done";
    case DoseState::Missed: return "Missed";
    case DoseState::Skipped: return "Skipped";
  }
  return "Unknown";
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
