#ifndef MODBUS_COLLECTOR_H
#define MODBUS_COLLECTOR_H

#include "configuration.h"
#include "modbus_rtu_transaction_scheduler.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdint.h>

typedef enum
{
    MODBUS_COLLECTOR_STATE_UNINITIALIZED = 0,
    MODBUS_COLLECTOR_STATE_DISABLED,
    MODBUS_COLLECTOR_STATE_RUNNING,
    MODBUS_COLLECTOR_STATE_FAILED
} modbus_collector_state_t;

typedef enum
{
    MODBUS_COLLECTOR_OK = 0,
    MODBUS_COLLECTOR_DISABLED,
    MODBUS_COLLECTOR_INVALID_ARGUMENT,
    MODBUS_COLLECTOR_INVALID_STATE,
    MODBUS_COLLECTOR_TASK_CREATE_FAILED
} modbus_collector_result_t;

typedef struct
{
    const configuration_collection_t *collection;
    QueueHandle_t request_queue;
    QueueHandle_t response_queue;
    const char *task_name;
    UBaseType_t task_priority;
    StackType_t *task_stack;
    uint32_t task_stack_depth;
    StaticTask_t *task_buffer;
} modbus_collector_config_t;

typedef struct
{
    const configuration_collection_t *collection;
    QueueHandle_t request_queue;
    QueueHandle_t response_queue;
    TickType_t next_deadline[CONFIGURATION_COLLECTION_POINT_MAX_COUNT];
    uint8_t publish_context_indices[CONFIGURATION_COLLECTION_POINT_MAX_COUNT];
    uint32_t next_token;
    TaskHandle_t task_handle;
    modbus_collector_state_t state;
} modbus_collector_t;

/**
  * @brief Initialize and start a collector using the scheduler's dedicated low-priority queues.
  * @pre collector is zero-initialized and config->collection points into the stable validated active configuration.
  * @retval MODBUS_COLLECTOR_DISABLED No task is created when point_count is zero.
  */
modbus_collector_result_t modbus_collector_init(modbus_collector_t *collector, const modbus_collector_config_t *config);

#endif
