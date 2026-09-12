#pragma once

#include <Arduino.h>

void wifiWebBegin();
void wifiWebLoop();
bool takeDispenseRequest(uint8_t &dispenser, uint8_t &amount);
