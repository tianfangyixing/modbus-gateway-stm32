#ifndef MANAGEMENT_TEST_FREERTOS_H
#define MANAGEMENT_TEST_FREERTOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef void *TaskHandle_t;

typedef struct
{
    uint32_t storage[24];
} StaticTask_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
#define portMAX_DELAY UINT32_MAX

void management_transport_test_assert(bool condition);
void management_transport_test_enter_critical(void);
void management_transport_test_exit_critical(void);
void management_transport_test_yield_from_isr(BaseType_t higher_priority_task_woken);

#define configASSERT(condition) management_transport_test_assert((condition) != 0)
#define taskENTER_CRITICAL() management_transport_test_enter_critical()
#define taskEXIT_CRITICAL() management_transport_test_exit_critical()
#define portYIELD_FROM_ISR(higher_priority_task_woken) \
    management_transport_test_yield_from_isr(higher_priority_task_woken)

#endif
