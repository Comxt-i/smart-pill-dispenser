#pragma once

#include <Arduino.h>

constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

constexpr uint8_t LCD_TIME_ADDRESS = 0x27;
constexpr uint8_t LCD_MEDICINE_ADDRESS = 0x25;

// The current hardware uses one Nano for all three rotating dispensers.
constexpr uint8_t NANO_DISPENSER_ADDRESS = 0x10;

constexpr uint8_t BUZZER_PIN = 25;
constexpr uint8_t CONFIRM_BUTTON_PIN = 32;
constexpr uint8_t DISPENSE_BUTTON_PIN = 33;
constexpr uint8_t CANCEL_BUTTON_PIN = 27;

constexpr uint8_t CMD_DISPENSE = 0x01;
constexpr uint8_t CMD_STOP = 0x02;
