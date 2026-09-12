#include "FreeRTOS.h"
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer);
int xSemaphoreTake(SemaphoreHandle_t handle, TickType_t timeout);
int xSemaphoreGive(SemaphoreHandle_t handle);
