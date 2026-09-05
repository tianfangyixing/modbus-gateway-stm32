#ifndef MANAGEMENT_TEST_TASK_H
#define MANAGEMENT_TEST_TASK_H

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *argument);

TaskHandle_t xTaskCreateStatic(TaskFunction_t task_function, const char *name, uint32_t stack_depth, void *argument,
                              UBaseType_t priority, StackType_t *stack_buffer, StaticTask_t *task_buffer);
void vTaskNotifyGiveFromISR(TaskHandle_t task_handle, BaseType_t *higher_priority_task_woken);
BaseType_t xTaskNotifyGive(TaskHandle_t task_handle);
uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks_to_wait);
void vTaskDelay(TickType_t ticks_to_delay);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);

#endif
