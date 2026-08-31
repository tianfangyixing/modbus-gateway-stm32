#include "modbus_collector.h"

#include "debug_log.h"
#include "modbus_collector_codec.h"
#include "modbus_rtu_adu_pool.h"
#include "mqtt_publisher.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODBUS_COLLECTOR_TICK_HALF_RANGE ((TickType_t)0x80000000UL)

typedef enum
{
    MODBUS_COLLECTOR_FAILURE_ADU_UNAVAILABLE = 1,
    MODBUS_COLLECTOR_FAILURE_ENCODE,
    MODBUS_COLLECTOR_FAILURE_REQUEST_QUEUE,
    MODBUS_COLLECTOR_FAILURE_RESPONSE_TOKEN,
    MODBUS_COLLECTOR_FAILURE_TRANSACTION,
    MODBUS_COLLECTOR_FAILURE_DECODE,
    MODBUS_COLLECTOR_FAILURE_PUBLISH
} modbus_collector_failure_t;

static void log_point_failure(uint8_t point_index, const configuration_collection_point_t *point,
                              modbus_collector_failure_t failure, int result)
{
    debug_log_printf("[collector] point=%u slave=%u source=%u address=%u failure=%u result=%d\r\n",
                     (unsigned int)point_index, (unsigned int)point->slave_address, (unsigned int)point->source,
                     (unsigned int)point->address, (unsigned int)failure, result);
}

static void publish_complete(void *context, mqtt_publisher_publish_result_t result)
{
    const uint8_t *point_index = context;

    if (result != MQTT_PUBLISHER_PUBLISH_OK)
    {
        debug_log_printf("[collector] point=%u failure=%u result=%d\r\n", (unsigned int)*point_index,
                         (unsigned int)MODBUS_COLLECTOR_FAILURE_PUBLISH, (int)result);
    }
}

static TickType_t interval_to_ticks(uint32_t interval_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(interval_ms);

    if (ticks == 0U)
    {
        ticks = 1U;
    }
    return ticks;
}

static bool deadline_is_reached(TickType_t now, TickType_t deadline)
{
    return (TickType_t)(now - deadline) < MODBUS_COLLECTOR_TICK_HALF_RANGE;
}

static bool deadline_is_before(TickType_t left, TickType_t right)
{
    return left != right && (TickType_t)(right - left) < MODBUS_COLLECTOR_TICK_HALF_RANGE;
}

static uint8_t select_earliest_point(const modbus_collector_t *collector)
{
    uint8_t point_index;
    uint8_t selected_index = 0U;

    for (point_index = 1U; point_index < collector->collection->point_count; point_index++)
    {
        if (deadline_is_before(collector->next_deadline[point_index], collector->next_deadline[selected_index]))
        {
            selected_index = point_index;
        }
    }
    return selected_index;
}

static void advance_deadline(modbus_collector_t *collector, uint8_t point_index, TickType_t now)
{
    TickType_t deadline = collector->next_deadline[point_index];
    TickType_t interval = interval_to_ticks(collector->collection->points[point_index].poll_interval_ms);
    TickType_t elapsed = (TickType_t)(now - deadline);
    TickType_t periods = (TickType_t)(elapsed / interval) + (TickType_t)1U;

    collector->next_deadline[point_index] = (TickType_t)(deadline + periods * interval);
}

static void process_response(modbus_collector_t *collector, uint8_t point_index, uint32_t expected_token,
                             modbus_rtu_transaction_scheduler_response_t *response)
{
    const configuration_collection_point_t *point = &collector->collection->points[point_index];

    if (response->token != expected_token)
    {
        log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_RESPONSE_TOKEN, 0);
    }
    else if (response->result != MODBUS_RTU_TRANSACTION_OK)
    {
        log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_TRANSACTION, (int)response->result);
    }
    else
    {
        char payload[MODBUS_COLLECTOR_PAYLOAD_CAPACITY];
        uint16_t payload_length;
        modbus_collector_codec_result_t codec_result;

        codec_result = modbus_collector_codec_decode_response(point, response->rtu_adu, payload, sizeof(payload),
                                                              &payload_length);
        if (codec_result != MODBUS_COLLECTOR_CODEC_OK)
        {
            log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_DECODE, (int)codec_result);
        }
        else
        {
            mqtt_publisher_publish_result_t publish_result;

            publish_result = mqtt_publisher_publish((const char *)point->topic.bytes, point->topic.length, payload,
                                                     payload_length, point->qos, 0U, publish_complete,
                                                     &collector->publish_context_indices[point_index]);
            if (publish_result != MQTT_PUBLISHER_PUBLISH_OK)
            {
                log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_PUBLISH, (int)publish_result);
            }
        }
    }

    modbus_rtu_adu_pool_release(response->rtu_adu);
}

static void process_point(modbus_collector_t *collector, uint8_t point_index)
{
    const configuration_collection_point_t *point = &collector->collection->points[point_index];
    modbus_rtu_transaction_scheduler_request_t request;
    modbus_rtu_transaction_scheduler_response_t response;
    modbus_collector_codec_result_t codec_result;
    BaseType_t queue_result;
    modbus_rtu_adu_t *request_adu;
    uint32_t token;

    request_adu = modbus_rtu_adu_pool_allocate();
    if (request_adu == NULL)
    {
        log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_ADU_UNAVAILABLE, 0);
        return;
    }

    codec_result = modbus_collector_codec_encode_request(point, request_adu);
    if (codec_result != MODBUS_COLLECTOR_CODEC_OK)
    {
        log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_ENCODE, (int)codec_result);
        modbus_rtu_adu_pool_release(request_adu);
        return;
    }

    token = collector->next_token;
    collector->next_token++;
    request.token = token;
    request.response_timeout_ms = point->first_byte_timeout_ms;
    request.rtu_adu = request_adu;
    queue_result = xQueueSend(collector->request_queue, &request, 0U);
    if (queue_result != pdPASS)
    {
        log_point_failure(point_index, point, MODBUS_COLLECTOR_FAILURE_REQUEST_QUEUE, (int)queue_result);
        modbus_rtu_adu_pool_release(request_adu);
        return;
    }

    xQueueReceive(collector->response_queue, &response, portMAX_DELAY);

    process_response(collector, point_index, token, &response);
}

static void collector_task(void *argument)
{
    modbus_collector_t *collector = argument;
    TickType_t initial_deadline = xTaskGetTickCount();
    uint8_t point_index;

    for (point_index = 0U; point_index < collector->collection->point_count; point_index++)
    {
        collector->next_deadline[point_index] = initial_deadline;
    }

    for (;;)
    {
        TickType_t now;
        TickType_t deadline;

        point_index = select_earliest_point(collector);
        deadline = collector->next_deadline[point_index];
        now = xTaskGetTickCount();
        if (!deadline_is_reached(now, deadline))
        {
            vTaskDelay((TickType_t)(deadline - now));
            continue;
        }

        process_point(collector, point_index);
        now = xTaskGetTickCount();
        advance_deadline(collector, point_index, now);
    }
}

static bool config_is_valid(const modbus_collector_config_t *config)
{
    uint8_t point_index;

    if (config->request_queue == NULL || config->response_queue == NULL || config->task_name == NULL ||
        config->task_stack == NULL || config->task_stack_depth == 0U || config->task_buffer == NULL ||
        config->task_priority >= configMAX_PRIORITIES ||
        config->collection->point_count > CONFIGURATION_COLLECTION_POINT_MAX_COUNT)
    {
        return false;
    }

    for (point_index = 0U; point_index < config->collection->point_count; point_index++)
    {
        TickType_t interval = interval_to_ticks(config->collection->points[point_index].poll_interval_ms);

        if (interval >= MODBUS_COLLECTOR_TICK_HALF_RANGE)
        {
            return false;
        }
    }
    return true;
}

modbus_collector_result_t modbus_collector_init(modbus_collector_t *collector, const modbus_collector_config_t *config)
{
    uint8_t point_index;

    if (collector == NULL || config == NULL || config->collection == NULL)
    {
        return MODBUS_COLLECTOR_INVALID_ARGUMENT;
    }
    if (collector->state != MODBUS_COLLECTOR_STATE_UNINITIALIZED)
    {
        return MODBUS_COLLECTOR_INVALID_STATE;
    }

    collector->collection = config->collection;
    collector->request_queue = NULL;
    collector->response_queue = NULL;
    collector->next_token = 0U;
    collector->task_handle = NULL;

    if (config->collection->point_count == 0U)
    {
        collector->state = MODBUS_COLLECTOR_STATE_DISABLED;
        return MODBUS_COLLECTOR_DISABLED;
    }
    if (!config_is_valid(config))
    {
        collector->state = MODBUS_COLLECTOR_STATE_FAILED;
        return MODBUS_COLLECTOR_INVALID_ARGUMENT;
    }

    collector->request_queue = config->request_queue;
    collector->response_queue = config->response_queue;
    for (point_index = 0U; point_index < config->collection->point_count; point_index++)
    {
        collector->publish_context_indices[point_index] = point_index;
    }

    collector->task_handle = xTaskCreateStatic(collector_task, config->task_name, config->task_stack_depth, collector,
                                               config->task_priority, config->task_stack, config->task_buffer);
    if (collector->task_handle == NULL)
    {
        collector->state = MODBUS_COLLECTOR_STATE_FAILED;
        collector->request_queue = NULL;
        collector->response_queue = NULL;
        return MODBUS_COLLECTOR_TASK_CREATE_FAILED;
    }

    collector->state = MODBUS_COLLECTOR_STATE_RUNNING;
    return MODBUS_COLLECTOR_OK;
}
