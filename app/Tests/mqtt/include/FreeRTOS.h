#ifndef MQTT_TEST_FREERTOS_H
#define MQTT_TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t StackType_t;
typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;
typedef int StaticTask_t;
typedef int StaticSemaphore_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
#endif
