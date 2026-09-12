#ifndef MANAGEMENT_TEST_TASK_H
#define MANAGEMENT_TEST_TASK_H
#include "FreeRTOS.h"
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t depth, void *argument,
                              uint32_t priority, StackType_t *stack, StaticTask_t *buffer);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
void vTaskNotifyGiveFromISR(TaskHandle_t handle, BaseType_t *woken);
void vTaskDelay(TickType_t ticks);
#endif
