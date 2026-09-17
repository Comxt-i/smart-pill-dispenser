// Stub สำหรับตรวจไวยากรณ์เท่านั้น
//
// MinGW ไม่มี settimeofday แต่ newlib ของ ESP32 มี จึงเติมให้เฉพาะตอนตรวจบนคอมพิวเตอร์
// include_next คือการดึงไฟล์จริงของระบบมาก่อน แล้วค่อยเติมส่วนที่ขาด
#pragma once

#include_next <sys/time.h>

#ifndef ESP32
int settimeofday(const struct timeval *tv, const struct timezone *tz);
#endif
