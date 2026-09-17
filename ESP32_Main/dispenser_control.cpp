#include "dispenser_control.h"
#include "config.h"

#include <ESP32Servo.h>

namespace {
enum class Phase { Idle, Releasing, Resting };
Servo dispenserServos[DISPENSER_COUNT];
Phase phase = Phase::Idle;
uint8_t activeIndex = 0;
uint8_t remainingCycles = 0;
unsigned long phaseStartedMs = 0;
}

void dispenserControlBegin()
{
  stopDispenser();
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
    dispenserServos[index].setPeriodHertz(50);
}

DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t amount)
{
  if (dispenser < 1 || dispenser > DISPENSER_COUNT || amount < 1 || amount > 9)
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
  servo.writeMicroseconds(RELEASE_PULSE_US[activeIndex]);
  phaseStartedMs = millis();
  phase = Phase::Releasing;
  return DispenseResult::Started;
}

void dispenserControlUpdate()
{
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW)
  {
    stopDispenser();
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
  else if (--remainingCycles == 0)
  {
    stopDispenser();
    Serial.println("Servo cycles complete (pill count not verified)");
    return;
  }
  else
  {
    servo.writeMicroseconds(RELEASE_PULSE_US[activeIndex]);
    phase = Phase::Releasing;
  }
  phaseStartedMs = millis();
}

void stopDispenser()
{
  for (uint8_t index = 0; index < DISPENSER_COUNT; ++index)
  {
    if (dispenserServos[index].attached())
      dispenserServos[index].detach();
  }
  remainingCycles = 0;
  phase = Phase::Idle;
}
