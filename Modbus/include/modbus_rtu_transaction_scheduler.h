#ifndef MODBUS_RTU_TRANSACTION_SCHEDULER_H
#define MODBUS_RTU_TRANSACTION_SCHEDULER_H

#include "modbus_rtu.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdint.h>

typedef struct
{
    uint32_t token;
    uint32_t response_timeout_ms;
    modbus_rtu_adu_t *rtu_adu;
} modbus_rtu_transaction_scheduler_request_t;

typedef struct
{
    uint32_t token;
    modbus_rtu_transaction_result_t result;
    modbus_rtu_adu_t *rtu_adu;
} modbus_rtu_transaction_scheduler_response_t;

typedef enum
{
    MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_HIGH = 0,
    MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_LOW
} modbus_rtu_transaction_scheduler_priority_t;

typedef struct
{
    modbus_rtu_channel_t *rtu_channel;

    /* 高优先级事务队列。 */
    QueueHandle_t hp_request_queue;
    QueueHandle_t hp_response_queue;

    /* 低优先级事务队列。 */
    QueueHandle_t lp_request_queue;
    QueueHandle_t lp_response_queue;

    QueueSetHandle_t request_queue_set;

    // 自上次低优先级请求被处理后，连续调度的高优先级请求数
    uint8_t consecutive_high_request_count;

    TaskHandle_t task_handle;
} modbus_rtu_transaction_scheduler_t;

void modbus_rtu_transaction_scheduler_init(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_channel_t *rtu_channel);

void modbus_rtu_transaction_scheduler_bind_queue(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_transaction_scheduler_priority_t priority,
    QueueHandle_t request_queue,
    QueueHandle_t response_queue);

void modbus_rtu_transaction_scheduler_start(
    modbus_rtu_transaction_scheduler_t *scheduler,
    const char *task_name,
    UBaseType_t task_priority,
    StackType_t *task_stack,
    uint32_t task_stack_depth,
    StaticTask_t *task_buffer);

#endif
