#include "FreeRTOS.h"
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t depth, void *arg,
                               UBaseType_t priority, StackType_t *stack, StaticTask_t *buffer);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
