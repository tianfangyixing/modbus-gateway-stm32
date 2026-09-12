#ifndef MANAGEMENT_TEST_FREERTOS_H
#define MANAGEMENT_TEST_FREERTOS_H
#include <stdint.h>
#include "unity.h"
typedef int BaseType_t;
typedef uint32_t StackType_t;
typedef uint32_t TickType_t;
typedef struct
{
    uint32_t value;
} StaticTask_t;
typedef StaticTask_t *TaskHandle_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
#define configASSERT(condition) TEST_ASSERT_TRUE(condition)
void management_test_yield(BaseType_t woken);
void management_test_enter_critical(void);
void management_test_exit_critical(void);
#define portYIELD_FROM_ISR(woken) management_test_yield(woken)
#define taskENTER_CRITICAL() management_test_enter_critical()
#define taskEXIT_CRITICAL() management_test_exit_critical()
#endif
