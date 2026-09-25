// Stub สำหรับตรวจไวยากรณ์เท่านั้น
#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, unsigned,
                                          TaskHandle_t *handle, int)
{
  if (handle) *handle = nullptr;
  return pdPASS;
}
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
