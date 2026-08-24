#include "modbus_gateway_app.h"

#include "main.h"
#include "modbus_rtu.h"
#include "modbus_rtu_adu_pool.h"
#include "modbus_rtu_rs485_port.h"
#include "modbus_rtu_transaction_scheduler.h"
#include "modbus_tcp_server.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "memory_sections.h"

#include <stdint.h>

#define MODBUS_GATEWAY_RTU_BAUD_RATE UINT32_C(9600)
#define MODBUS_GATEWAY_RTU_RESPONSE_TIMEOUT_MS UINT32_C(1000)
#define MODBUS_GATEWAY_TCP_LISTEN_PORT UINT16_C(502)
#define MODBUS_GATEWAY_LOW_QUEUE_LENGTH 1U
#define MODBUS_GATEWAY_SCHEDULER_TASK_STACK_DEPTH 256U
#define MODBUS_GATEWAY_SCHEDULER_TASK_PRIORITY 23U
#define MODBUS_GATEWAY_TCP_TASK_STACK_DEPTH 512U
#define MODBUS_GATEWAY_TCP_TASK_PRIORITY 22U

static modbus_rtu_channel_t modbus_gateway_rtu_channel;
static modbus_rtu_transaction_scheduler_t modbus_gateway_scheduler;
static modbus_tcp_server_t modbus_gateway_tcp_server;

static CCM_SRAM_ALIGNED(8) StackType_t modbus_gateway_scheduler_task_stack[MODBUS_GATEWAY_SCHEDULER_TASK_STACK_DEPTH];
static StaticTask_t modbus_gateway_scheduler_task_buffer;
static CCM_SRAM_ALIGNED(8) StackType_t modbus_gateway_tcp_task_stack[MODBUS_GATEWAY_TCP_TASK_STACK_DEPTH];
static StaticTask_t modbus_gateway_tcp_task_buffer;

static StaticQueue_t modbus_gateway_low_request_queue_buffer;
static StaticQueue_t modbus_gateway_low_response_queue_buffer;
static uint8_t modbus_gateway_low_request_queue_storage[
    MODBUS_GATEWAY_LOW_QUEUE_LENGTH * sizeof(modbus_rtu_transaction_scheduler_request_t)];
static uint8_t modbus_gateway_low_response_queue_storage[
    MODBUS_GATEWAY_LOW_QUEUE_LENGTH * sizeof(modbus_rtu_transaction_scheduler_response_t)];
static QueueHandle_t modbus_gateway_low_request_queue;
static QueueHandle_t modbus_gateway_low_response_queue;

void modbus_gateway_app_init(void)
{
    modbus_rtu_channel_result_t channel_result;
    modbus_tcp_server_config_t tcp_config;
    modbus_tcp_server_result_t tcp_result;

    modbus_rtu_adu_pool_init();
    modbus_rtu_rs485_port_init();

    channel_result = modbus_rtu_channel_init(&modbus_gateway_rtu_channel, NULL, modbus_rtu_rs485_port_read,
                                             modbus_rtu_rs485_port_write, MODBUS_GATEWAY_RTU_BAUD_RATE);
    if (channel_result != MODBUS_RTU_CHANNEL_OK)
    {
        Error_Handler();
        return;
    }

    modbus_rtu_transaction_scheduler_init(&modbus_gateway_scheduler, &modbus_gateway_rtu_channel);

    tcp_config.transaction_scheduler = &modbus_gateway_scheduler;
    tcp_config.listen_port = MODBUS_GATEWAY_TCP_LISTEN_PORT;
    tcp_config.response_timeout_ms = MODBUS_GATEWAY_RTU_RESPONSE_TIMEOUT_MS;
    tcp_config.task_name = "modbus_tcp";
    tcp_config.task_priority = MODBUS_GATEWAY_TCP_TASK_PRIORITY;
    tcp_config.task_stack = modbus_gateway_tcp_task_stack;
    tcp_config.task_stack_depth = MODBUS_GATEWAY_TCP_TASK_STACK_DEPTH;
    tcp_config.task_buffer = &modbus_gateway_tcp_task_buffer;

    tcp_result = modbus_tcp_server_init(&modbus_gateway_tcp_server, &tcp_config);
    if (tcp_result != MODBUS_TCP_SERVER_OK)
    {
        Error_Handler();
        return;
    }

    modbus_gateway_low_request_queue = xQueueCreateStatic(
        MODBUS_GATEWAY_LOW_QUEUE_LENGTH, sizeof(modbus_rtu_transaction_scheduler_request_t),
        modbus_gateway_low_request_queue_storage, &modbus_gateway_low_request_queue_buffer);
    modbus_gateway_low_response_queue = xQueueCreateStatic(
        MODBUS_GATEWAY_LOW_QUEUE_LENGTH, sizeof(modbus_rtu_transaction_scheduler_response_t),
        modbus_gateway_low_response_queue_storage, &modbus_gateway_low_response_queue_buffer);
    if (modbus_gateway_low_request_queue == NULL || modbus_gateway_low_response_queue == NULL)
    {
        Error_Handler();
        return;
    }

    modbus_rtu_transaction_scheduler_bind_queue(
        &modbus_gateway_scheduler, MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_LOW,
        modbus_gateway_low_request_queue, modbus_gateway_low_response_queue);


    modbus_rtu_transaction_scheduler_start(
        &modbus_gateway_scheduler, "modbus_sched", MODBUS_GATEWAY_SCHEDULER_TASK_PRIORITY,
        modbus_gateway_scheduler_task_stack, MODBUS_GATEWAY_SCHEDULER_TASK_STACK_DEPTH,
        &modbus_gateway_scheduler_task_buffer);


    tcp_result = modbus_tcp_server_start(&modbus_gateway_tcp_server);
    if (tcp_result != MODBUS_TCP_SERVER_OK)
    {
        Error_Handler();
        return;
    }
}
