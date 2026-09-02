#pragma once

#include <Arduino.h>

void nanoControlBegin();
bool dispenseMedicine(uint8_t lane, uint8_t amount);
bool stopLane(uint8_t lane);

