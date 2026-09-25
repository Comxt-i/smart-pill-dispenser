#pragma once
// Copy to hardware.local.h ONLY when preparing your particular mechanism.
// Leave both false until each of the three mechanisms has been calibrated
// without medicine. The example pulse values are NOT calibrated values.
#define PILLBOX_REAL_HARDWARE false
#define PILLBOX_CALIBRATED false
#define PILLBOX_REST_PULSES {1500, 1500, 1500}
#define PILLBOX_RELEASE_PULSES {1750, 1750, 1750}
#define PILLBOX_MOVE_TIME_MS 700
