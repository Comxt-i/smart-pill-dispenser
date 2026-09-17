#pragma once

#include <Arduino.h>

constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

constexpr uint8_t LCD_TIME_ADDRESS = 0x27;
constexpr uint8_t LCD_MEDICINE_ADDRESS = 0x25;

// Pin map for a classic ESP32 DevKit / ESP32-WROOM-32.
constexpr uint8_t DISPENSER_COUNT = 3;
constexpr uint8_t SERVO_PINS[DISPENSER_COUNT] = {18, 19, 23};

// Keep motion disabled until calibrated without pills.
constexpr bool ENABLE_SERVO_MOVEMENT = false;
constexpr int SERVO_MIN_PULSE_US = 1000;
constexpr int SERVO_MAX_PULSE_US = 2000;
constexpr int REST_PULSE_US[DISPENSER_COUNT] = {1500, 1500, 1500};
constexpr int RELEASE_PULSE_US[DISPENSER_COUNT] = {1750, 1750, 1750};
constexpr unsigned long MOVE_TIME_MS = 700;

constexpr uint8_t BUZZER_PIN = 25;
constexpr uint8_t CONFIRM_BUTTON_PIN = 32;
constexpr uint8_t DISPENSE_BUTTON_PIN = 33;
constexpr uint8_t CANCEL_BUTTON_PIN = 27;
