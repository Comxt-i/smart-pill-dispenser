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
  dispenserControlBegin();
  assert(attachedCount == 0 && pulses.empty());
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
    return 0;
  }

  // All three pins, one cycle, and the maximum nine cycles.
  for (uint8_t unit = 1; unit <= DISPENSER_COUNT; ++unit)
  {
    pulses.clear();
    const uint8_t amount = unit == 3 ? 9 : 1;
    assert(dispenseMedicine(unit, amount) == DispenseResult::Started);
    assert(attachedCount == 1);
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
    assert(dispenseMedicine(3, 1) == DispenseResult::Cancelled);
    cancelLevel = HIGH;
    tick();
    assert(pulses.size() == beforeCancel);
  }

  failAttach = true;
  assert(dispenseMedicine(1, 1) == DispenseResult::ServoError);
  assert(attachedCount == 0);
  failAttach = false;
  assert(dispenseMedicine(1, 1) == DispenseResult::Started);
  stopDispenser();
  stopDispenser(); // Stopping an idle controller is harmless.
  assert(attachedCount == 0);

  // Unsigned elapsed-time calculation must survive clock rollover.
  pulses.clear();
  fakeMillis = ULONG_MAX - MOVE_TIME_MS / 2;
  assert(dispenseMedicine(3, 1) == DispenseResult::Started);
  tick();
  assert(pulses.size() == 2 && attachedCount == 1);
  tick();
  assert(attachedCount == 0);
}
