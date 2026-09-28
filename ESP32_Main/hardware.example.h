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
// Per plate, ordered by hole size (seen from the front, small holes on the right):
// {<=8 right 86, <=13 right 130, <=15 left 90, <=25 left 130}; right = pulse below rest
#define PILLBOX_HOLE_PULSES {{863, 537, 2167, 2463}, {863, 537, 2167, 2463}, {863, 537, 2167, 2463}}
#define PILLBOX_MOVE_TIME_MS 700
