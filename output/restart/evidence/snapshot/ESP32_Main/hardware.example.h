#pragma once
// Copy to hardware.local.h ONLY when preparing your particular mechanism.
// Leave both false until each of the three mechanisms has been calibrated
// without medicine. The example pulse values are NOT calibrated values.
#define PILLBOX_REAL_HARDWARE false
#define PILLBOX_CALIBRATED false
#define PILLBOX_PILL_SENSOR true
// Set true ONLY if you accept dispensing that nothing verifies.
// Required when PILLBOX_PILL_SENSOR is false and real hardware is on.
#define PILLBOX_ALLOW_UNVERIFIED_DISPENSE false
#define PILLBOX_REST_PULSES {1500, 1500, 1500}
// Per plate: {right 90 (<=8mm), right 135 (<=13mm), left 90 (<=15mm), left 135 (<=25mm)}
#define PILLBOX_HOLE_PULSES {{2167, 2463, 833, 537}, {2167, 2463, 833, 537}, {2167, 2463, 833, 537}}
#define PILLBOX_MOVE_TIME_MS 700
