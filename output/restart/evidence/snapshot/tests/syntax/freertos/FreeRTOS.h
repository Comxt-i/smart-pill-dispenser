// Stub สำหรับตรวจไวยากรณ์เท่านั้น: ให้โค้ดส่วน task เบื้องหลังของ ESP32 ถูกคอมไพล์ตรวจบนคอมด้วย
#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffUL
