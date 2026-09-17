#include "config.h"
#include "dispenser_control.h"
#include <ESP32Servo.h>
#include <climits>

unsigned long fakeMillis = 0;
int cancelLevel = HIGH;
FakeSerial Serial;
std::vector<Pulse> pulses;
int attachedCount = 0;
bool failAttach = false;

void tick()
{
  fakeMillis += MOVE_TIME_MS;
  dispenserControlUpdate();
}

int main()
{
  DispenseOutcome outcome = {0, 0, 0, false};

  dispenserControlBegin();
  assert(attachedCount == 0 && pulses.empty());
  assert(!dispenserIsBusy());
  assert(!takeDispenseOutcome(outcome)); // ยังไม่เคยจ่าย จึงยังไม่มีผลให้อ่าน
  assert(dispenseMedicine(0, 1) == DispenseResult::Invalid);
  assert(dispenseMedicine(4, 1) == DispenseResult::Invalid);
  assert(dispenseMedicine(1, 0) == DispenseResult::Invalid);
  assert(dispenseMedicine(1, 10) == DispenseResult::Invalid);
  cancelLevel = LOW;
  assert(dispenseMedicine(1, 1) == DispenseResult::Cancelled);
  cancelLevel = HIGH;

  if (!ENABLE_SERVO_MOVEMENT)
  {
    assert(dispenseMedicine(1, 1) == DispenseResult::Disabled);
    tick();
    assert(attachedCount == 0 && pulses.empty());
    assert(!dispenserIsBusy());
    assert(!takeDispenseOutcome(outcome)); // คำสั่งที่ถูกปฏิเสธต้องไม่ถูกรายงานว่าจ่ายแล้ว
    return 0;
  }

  // All three pins, one cycle, and the maximum nine cycles.
  for (uint8_t unit = 1; unit <= DISPENSER_COUNT; ++unit)
  {
    pulses.clear();
    const uint8_t amount = unit == 3 ? 9 : 1;
    assert(dispenseMedicine(unit, amount) == DispenseResult::Started);
    assert(attachedCount == 1);
    assert(dispenserIsBusy());
    const unsigned long started = fakeMillis;
    fakeMillis += MOVE_TIME_MS - 1;
    dispenserControlUpdate();
    assert(pulses.size() == 1); // No early transition.
    fakeMillis = started;
    assert(dispenseMedicine(1, 1) == DispenseResult::Busy);
    for (uint8_t cycle = 0; cycle < amount; ++cycle)
    {
      tick();
      assert(attachedCount == 1); // Hold rest pulse for a full interval.
      tick();
    }
    assert(attachedCount == 0);
    assert(!dispenserIsBusy());
    assert(takeDispenseOutcome(outcome));
    assert(outcome.dispenser == unit);
    assert(outcome.requestedCycles == amount && outcome.completedCycles == amount);
    assert(!outcome.cancelled);
    assert(!takeDispenseOutcome(outcome)); // ผลหนึ่งรอบอ่านได้ครั้งเดียว
    assert(pulses.size() == 2u * amount);
    for (unsigned i = 0; i < pulses.size(); ++i)
    {
      assert(pulses[i].pin == SERVO_PINS[unit - 1]);
      assert(pulses[i].value == (i % 2 == 0 ? RELEASE_PULSE_US[unit - 1]
                                                          : REST_PULSE_US[unit - 1]));
    }
    tick();
    assert(pulses.size() == 2u * amount); // No queued duplicate.
  }

  // Cancel during either phase, reject while held, and never resume on release.
  for (int resting = 0; resting < 2; ++resting)
  {
    assert(dispenseMedicine(2, 3) == DispenseResult::Started);
    if (resting) tick();
    const auto beforeCancel = pulses.size();
    cancelLevel = LOW;
    dispenserControlUpdate();
    assert(attachedCount == 0);
    assert(takeDispenseOutcome(outcome));
    assert(outcome.cancelled && outcome.dispenser == 2);
    assert(outcome.completedCycles < outcome.requestedCycles);
    assert(dispenseMedicine(3, 1) == DispenseResult::Cancelled);
    assert(!takeDispenseOutcome(outcome)); // คำสั่งที่ถูกปฏิเสธไม่นับเป็นหนึ่งรอบ
    cancelLevel = HIGH;
    tick();
    assert(pulses.size() == beforeCancel);
  }

  failAttach = true;
  assert(dispenseMedicine(1, 1) == DispenseResult::ServoError);
  assert(attachedCount == 0);
  failAttach = false;
  assert(!takeDispenseOutcome(outcome)); // attach ไม่สำเร็จ ไม่ใช่รอบที่เริ่มแล้ว
  assert(dispenseMedicine(1, 1) == DispenseResult::Started);
  stopDispenser();
  assert(takeDispenseOutcome(outcome) && outcome.cancelled);
  stopDispenser(); // Stopping an idle controller is harmless.
  assert(!takeDispenseOutcome(outcome)); // หยุดตอนที่ว่างอยู่ต้องไม่สร้างผลหลอก
  assert(attachedCount == 0);

  // Unsigned elapsed-time calculation must survive clock rollover.
  pulses.clear();
  fakeMillis = ULONG_MAX - MOVE_TIME_MS / 2;
  assert(dispenseMedicine(3, 1) == DispenseResult::Started);
  tick();
  assert(pulses.size() == 2 && attachedCount == 1);
  tick();
  assert(attachedCount == 0);
  assert(takeDispenseOutcome(outcome));
  assert(outcome.dispenser == 3 && outcome.completedCycles == 1 && !outcome.cancelled);
}
