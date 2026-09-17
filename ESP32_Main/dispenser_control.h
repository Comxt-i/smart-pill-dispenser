#pragma once

#include <Arduino.h>

enum class DispenseResult { Started, Invalid, Disabled, Busy, Cancelled, ServoError };

void dispenserControlBegin();
void dispenserControlUpdate();
DispenseResult dispenseMedicine(uint8_t dispenser, uint8_t amount);
void stopDispenser();
