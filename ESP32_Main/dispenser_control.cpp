#include "dispenser_control.h"
#include "config.h"

#include <ESP32Servo.h>

namespace {
enum class Phase { Idle, Releasing, Resting };
Servo dispenserServos[DISPENSER_COUNT];
Phase phase = Phase::Idle;
uint8_t activeIndex = 0;
uint8_t remainingCycles = 0;
uint8_t requestedCycles = 0;
uint8_t completedCycles = 0;
unsigned long phaseStartedMs = 0;

DispenseOutcome lastOutcome = {0, 0, 0, false};
bool outcomePending = false;

void detachAll()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    if (dispenserServos[index].attached())
      dispenserServos[index].detach();
  }
}

/** ปิดรอบการทำงานปัจจุบันและเก็บผลไว้ให้ loop หลักมาอ่าน */
void finishRun(bool cancelled)
{
  const bool wasRunning = phase != Phase::Idle;

  detachAll();
  remainingCycles = 0;
  phase = Phase::Idle;

  if (!wasRunning)
    return;

  lastOutcome.dispenser = static_cast<uint8_t>(activeIndex + 1);
  lastOutcome.requestedCycles = requestedCycles;
  lastOutcome.completedCycles = completedCycles;
  lastOutcome.cancelled = cancelled;
  outcomePending = true;
}
}

void dispenserControlBegin()
{
  finishRun(false);
  outcomePending = false;
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
    dispenserServos[index].setPeriodHertz(50);
}

DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t amount)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT || amount < 1 || amount > MAX_CYCLES_PER_DOSE)
    return DispenseResult::Invalid;
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
    return DispenseResult::Cancelled;
  if (!ENABLE_SERVO_MOVEMENT)
    return DispenseResult::Disabled;
  if (phase != Phase::Idle)
    return DispenseResult::Busy;

  activeIndex = dispenser - 1;
  Servo &servo = dispenserServos[activeIndex];
  servo.attach(SERVO_PINS[activeIndex], SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
  if (!servo.attached())
    return DispenseResult::ServoError;

  remainingCycles = amount;
  requestedCycles = amount;
  completedCycles = 0;
  servo.writeMicroseconds(RELEASE_PULSE_US[activeIndex]);
  phaseStartedMs = millis();
  phase = Phase::Releasing;
  return DispenseResult::Started;
}

void dispenserControlUpdate()
{
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
  {
    finishRun(true);
    return;
  }
  if (phase == Phase::Idle || millis() - phaseStartedMs < MOVE_TIME_MS)
    return;

  Servo &servo = dispenserServos[activeIndex];
  if (phase == Phase::Releasing)
  {
    servo.writeMicroseconds(REST_PULSE_US[activeIndex]);
    phase = Phase::Resting;
  }
  else
  {
    ++completedCycles;
    if (--remainingCycles == 0)
    {
      finishRun(false);
      Serial.println("Servo cycles complete (pill count not verified)");
      return;
    }
    servo.writeMicroseconds(RELEASE_PULSE_US[activeIndex]);
    phase = Phase::Releasing;
  }
  phaseStartedMs = millis();
}

void stopDispenser()
{
  finishRun(phase != Phase::Idle);
}

bool dispenserIsBusy()
{
  return phase != Phase::Idle;
}

bool takeDispenseOutcome(DispenseOutcome &outcome)
{
  if (!outcomePending)
    return false;

  outcome = lastOutcome;
  outcomePending = false;
  return true;
}
