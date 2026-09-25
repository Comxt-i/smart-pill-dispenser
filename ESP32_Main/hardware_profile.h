#pragma once
// Local calibration stays out of Git. Defaults are safe for a bare ESP32.
#if __has_include("hardware.local.h")
#include "hardware.local.h"
#endif
#ifndef PILLBOX_REAL_HARDWARE
#define PILLBOX_REAL_HARDWARE false
#endif
#ifndef PILLBOX_CALIBRATED
#define PILLBOX_CALIBRATED false
#endif
#ifndef PILLBOX_REST_PULSES
#define PILLBOX_REST_PULSES {1500, 1500, 1500}
#endif
#ifndef PILLBOX_RELEASE_PULSES
#define PILLBOX_RELEASE_PULSES {1750, 1750, 1750}
#endif
#ifndef PILLBOX_MOVE_TIME_MS
#define PILLBOX_MOVE_TIME_MS 700
#endif
static_assert(!PILLBOX_REAL_HARDWARE || PILLBOX_CALIBRATED,
              "Calibrate the mechanism before enabling real hardware");
