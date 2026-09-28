#pragma once
// Local calibration stays out of Git. Defaults are safe for a bare ESP32.
// Tests probe the gates with -D flags, so they must be able to opt out of
// whatever calibration this particular machine happens to have on disk.
#if !defined(PILLBOX_IGNORE_LOCAL_PROFILE) && __has_include("hardware.local.h")
#include "hardware.local.h"
#endif
#ifndef PILLBOX_REAL_HARDWARE
#define PILLBOX_REAL_HARDWARE false
#endif
#ifndef PILLBOX_CALIBRATED
#define PILLBOX_CALIBRATED false
#endif
#ifndef PILLBOX_PILL_SENSOR
#define PILLBOX_PILL_SENSOR true
#endif
// Dispensing with no IR sensor cannot verify that a pill actually fell.
// Opting out is deliberate and local: history gets marked unverified.
#ifndef PILLBOX_ALLOW_UNVERIFIED_DISPENSE
#define PILLBOX_ALLOW_UNVERIFIED_DISPENSE false
#endif
#ifndef PILLBOX_REST_PULSES
#define PILLBOX_REST_PULSES {1500, 1500, 1500}
#endif
// Per plate, ordered by hole size: {<=8, <=13, <=15, <=25 mm}.
// Left/right as seen from the front: the small holes are always on the right, the large ones
// on the left. <=8 right 86 deg (the mechanism overshoots it at 90), <=13 right 130,
// <=15 left 90, <=25 left 130. This servo turns right for pulses BELOW rest and left above.
// Uncalibrated guesses (7.4us/deg around 1500us; the 135 deg holes stop at 130 deg
// to stay clear of the servo end stop). Real values come from ServoCalibrate.
#ifndef PILLBOX_HOLE_PULSES
#define PILLBOX_HOLE_PULSES {{863, 537, 2167, 2463}, {863, 537, 2167, 2463}, {863, 537, 2167, 2463}}
#endif
#ifndef PILLBOX_MOVE_TIME_MS
#define PILLBOX_MOVE_TIME_MS 700
#endif
static_assert(!PILLBOX_REAL_HARDWARE || PILLBOX_CALIBRATED,
              "Calibrate the mechanism before enabling real hardware");
