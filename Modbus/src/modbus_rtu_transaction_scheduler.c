#include "modbus_rtu_transaction_scheduler.h"

#include <stddef.h>
#include <string.h>

#define MODBUS_RTU_TRANSACTION_SCHEDULER_ADAPTER_RECOVERY_DELAY_MS 100U
#define MODBUS_RTU_TRANSACTION_SCHEDULER_MAX_CONSECUTIVE_HIGH_REQUESTS 4U

static void select_available_request_queues(
    modbus_rtu_transaction_scheduler_t *scheduler,
    UBaseType_t *selected_high_request_count,
    UBaseType_t *selected_low_request_count)
{
    QueueSetMemberHandle_t selected_queue;
    TickType_t ticks_to_wait = 0;

    if (*selected_high_request_count == 0U && *selected_low_request_count == 0U)
    {
        ticks_to_wait = portMAX_DELAY;
    }

    selected_queue = xQueueSelectFromSet(scheduler->request_queue_set, ticks_to_wait);
    while (selected_queue != NULL)
    {
        if (selected_queue == scheduler->hp_request_queue)
        {
            (*selected_high_request_count)++;
        }
        else
        {
            (*selected_low_request_count)++;
        }

        selected_queue = xQueueSelectFromSet(scheduler->request_queue_set, 0);
    }
}

static QueueHandle_t receive_next_transaction(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_transaction_scheduler_request_t *request,
    UBaseType_t *selected_high_request_count,
    UBaseType_t *selected_low_request_count)
{
    QueueHandle_t request_queue;
    QueueHandle_t response_queue;

    select_available_request_queues(scheduler, selected_high_request_count, selected_low_request_count);

    if (*selected_high_request_count > 0U &&
        (scheduler->consecutive_high_request_count < MODBUS_RTU_TRANSACTION_SCHEDULER_MAX_CONSECUTIVE_HIGH_REQUESTS ||
         *selected_low_request_count == 0U))
    {
        request_queue = scheduler->hp_request_queue;
        response_queue = scheduler->hp_response_queue;
        (*selected_high_request_count)--;

        if (scheduler->consecutive_high_request_count < UINT8_MAX)
        {
            scheduler->consecutive_high_request_count++;
        }
    }
    else
    {
        request_queue = scheduler->lp_request_queue;
        response_queue = scheduler->lp_response_queue;
        (*selected_low_request_count)--;
        scheduler->consecutive_high_request_count = 0U;
    }

    xQueueReceive(request_queue, request, 0);
    return response_queue;
}

static void transaction_scheduler_task(void *argument)
{
    modbus_rtu_transaction_scheduler_t *scheduler = argument;
    UBaseType_t selected_high_request_count = 0U;
    UBaseType_t selected_low_request_count = 0U;

    for (;;)
    {
        modbus_rtu_transaction_scheduler_request_t request;
        QueueHandle_t response_queue;
        modbus_rtu_adu_t temporary_response;
        modbus_rtu_transaction_scheduler_response_t response;

        response_queue = receive_next_transaction(
            scheduler, &request, &selected_high_request_count, &selected_low_request_count);
        response.token = request.token;
        response.result = modbus_rtu_transact(
            scheduler->rtu_channel, request.rtu_adu, request.response_timeout_ms, &temporary_response);
        response.rtu_adu = request.rtu_adu;

        if (response.result == MODBUS_RTU_TRANSACTION_OK ||
            response.result == MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE)
        {
            response.rtu_adu->length = temporary_response.length;
            memcpy(response.rtu_adu->data, temporary_response.data, temporary_response.length);
        }

        xQueueSend(response_queue, &response, portMAX_DELAY);

        if (response.result == MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR)
        {
            vTaskDelay(pdMS_TO_TICKS(MODBUS_RTU_TRANSACTION_SCHEDULER_ADAPTER_RECOVERY_DELAY_MS));
        }
    }
}

void modbus_rtu_transaction_scheduler_init(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_channel_t *rtu_channel)
{
    scheduler->rtu_channel = rtu_channel;
    scheduler->hp_request_queue = NULL;
    scheduler->hp_response_queue = NULL;
    scheduler->lp_request_queue = NULL;
    scheduler->lp_response_queue = NULL;
    scheduler->request_queue_set = NULL;
    scheduler->task_handle = NULL;
    scheduler->consecutive_high_request_count = 0U;
}

void modbus_rtu_transaction_scheduler_bind_queue(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_transaction_scheduler_priority_t priority,
    QueueHandle_t request_queue,
    QueueHandle_t response_queue)
{
    if (priority == MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_HIGH)
    {
        scheduler->hp_request_queue = request_queue;
        scheduler->hp_response_queue = response_queue;
    }
    else if (priority == MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_LOW)
    {
        scheduler->lp_request_queue = request_queue;
        scheduler->lp_response_queue = response_queue;
    }
}

void modbus_rtu_transaction_scheduler_start(
    modbus_rtu_transaction_scheduler_t *scheduler,
    const char *task_name,
    UBaseType_t task_priority,
    StackType_t *task_stack,
    uint32_t task_stack_depth,
    StaticTask_t *task_buffer)
{
    UBaseType_t request_queue_set_length;
    BaseType_t queue_set_result;

    request_queue_set_length = uxQueueSpacesAvailable(scheduler->hp_request_queue) +
                               uxQueueSpacesAvailable(scheduler->lp_request_queue);
    scheduler->request_queue_set = xQueueCreateSet(request_queue_set_length);
    if (scheduler->request_queue_set == NULL)
    {
        return;
    }

    queue_set_result = xQueueAddToSet(scheduler->hp_request_queue, scheduler->request_queue_set);
    if (queue_set_result != pdPASS)
    {
        vQueueDelete(scheduler->request_queue_set);
        scheduler->request_queue_set = NULL;
        return;
    }

    queue_set_result = xQueueAddToSet(scheduler->lp_request_queue, scheduler->request_queue_set);
    if (queue_set_result != pdPASS)
    {
        xQueueRemoveFromSet(scheduler->hp_request_queue, scheduler->request_queue_set);
        vQueueDelete(scheduler->request_queue_set);
        scheduler->request_queue_set = NULL;
        return;
    }

    scheduler->task_handle = xTaskCreateStatic(
        transaction_scheduler_task,
        task_name,
        task_stack_depth,
        scheduler,
        task_priority,
        task_stack,
        task_buffer);

    if (scheduler->task_handle == NULL)
    {
        xQueueRemoveFromSet(scheduler->hp_request_queue, scheduler->request_queue_set);
        xQueueRemoveFromSet(scheduler->lp_request_queue, scheduler->request_queue_set);
        vQueueDelete(scheduler->request_queue_set);
        scheduler->request_queue_set = NULL;
    }
}
