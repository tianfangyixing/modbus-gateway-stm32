#include "management_transport_test_adapter.h"

#include "lwip/netif.h"
#include "task.h"
#include "watchdog.h"

#include <stdarg.h>
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

management_transport_test_state_t management_transport_test_state;

static configuration_t management_transport_test_active_configuration;
static struct netif management_transport_test_netif;
static uint32_t management_transport_test_task_token;
static TaskFunction_t task_entry;
static void *task_argument;
static pthread_t task_thread;
static pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t task_condition = PTHREAD_COND_INITIALIZER;
static bool task_started;
static bool task_paused;
static bool task_resume;
static bool task_stop;
static bool task_exited;

struct netif *netif_default;

static void check_thread_result(int result)
{
    if (result != 0)
    {
        fprintf(stderr, "Management test scheduler failed: %d\n", result);
        abort();
    }
}

static void finish_task(void)
{
    check_thread_result(pthread_mutex_lock(&task_mutex));
    task_exited = true;
    check_thread_result(pthread_cond_broadcast(&task_condition));
    check_thread_result(pthread_mutex_unlock(&task_mutex));
    pthread_exit(NULL);
}

static void pause_task(void)
{
    bool stop_requested;

    management_transport_test_assert(management_transport_test_state.critical_depth == 0U);
    check_thread_result(pthread_mutex_lock(&task_mutex));
    task_paused = true;
    check_thread_result(pthread_cond_broadcast(&task_condition));
    while (!task_resume && !task_stop)
    {
        check_thread_result(pthread_cond_wait(&task_condition, &task_mutex));
    }
    task_paused = false;
    task_resume = false;
    stop_requested = task_stop;
    check_thread_result(pthread_mutex_unlock(&task_mutex));
    if (stop_requested)
    {
        finish_task();
    }
}

static void *run_task_thread(void *argument)
{
    check_thread_result(pthread_mutex_lock(&task_mutex));
    check_thread_result(pthread_mutex_unlock(&task_mutex));
    management_transport_test_assert(argument == NULL);
    task_entry(task_argument);
    management_transport_test_assert(false);
    finish_task();
    return NULL;
}

void management_transport_test_run_task(void)
{
    management_transport_test_assert(task_entry != NULL);
    if (task_entry == NULL)
    {
        return;
    }

    check_thread_result(pthread_mutex_lock(&task_mutex));
    if (!task_started)
    {
        task_started = true;
        check_thread_result(pthread_create(&task_thread, NULL, run_task_thread, NULL));
    }
    else if (!task_exited)
    {
        task_paused = false;
        task_resume = true;
        check_thread_result(pthread_cond_broadcast(&task_condition));
    }
    while (!task_paused && !task_exited)
    {
        check_thread_result(pthread_cond_wait(&task_condition, &task_mutex));
    }
    check_thread_result(pthread_mutex_unlock(&task_mutex));
}

void management_transport_test_stop_task(void)
{
    void *thread_result = NULL;

    if (!task_started)
    {
        return;
    }
    check_thread_result(pthread_mutex_lock(&task_mutex));
    task_stop = true;
    check_thread_result(pthread_cond_broadcast(&task_condition));
    check_thread_result(pthread_mutex_unlock(&task_mutex));
    check_thread_result(pthread_join(task_thread, &thread_result));
    management_transport_test_assert(thread_result == NULL);
    task_started = false;
}

void management_transport_test_adapter_reset(void)
{
    management_transport_test_stop_task();
    task_entry = NULL;
    task_argument = NULL;
    task_paused = false;
    task_resume = false;
    task_stop = false;
    task_exited = false;
    memset(&management_transport_test_state, 0, sizeof(management_transport_test_state));
    memset(&management_transport_test_active_configuration, 0, sizeof(management_transport_test_active_configuration));
    memset(&management_transport_test_netif, 0, sizeof(management_transport_test_netif));
    management_transport_test_task_token = 0U;
    netif_default = NULL;

    management_transport_test_state.cdc_enable_result = MANAGEMENT_TRANSPORT_CDC_OK;
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_OK;
    management_transport_test_state.configuration_ready = true;
    management_transport_test_state.active_configuration_available = true;
    management_transport_test_state.encode_result = CONFIGURATION_BINARY_CODEC_OK;
    management_transport_test_state.encoded_configuration[0] = CONFIGURATION_SCHEMA_VERSION;
    management_transport_test_state.encoded_configuration_length = 1U;
    management_transport_test_state.write_result = CONFIGURATION_SERVICE_OK;
    management_transport_test_state.sntp_time_available = true;
    management_transport_test_state.mqtt_state = MQTT_PUBLISHER_STATE_DISABLED;
}

void management_transport_test_set_network(bool present, bool link_up, uint8_t first, uint8_t second, uint8_t third,
                                           uint8_t fourth)
{
    ip4_addr_t address;

    memset(&management_transport_test_netif, 0, sizeof(management_transport_test_netif));
    if (!present)
    {
        netif_default = NULL;
        return;
    }

    netif_default = &management_transport_test_netif;
    if (link_up)
    {
        management_transport_test_netif.flags |= NETIF_FLAG_LINK_UP;
    }
    IP4_ADDR(&address, first, second, third, fourth);
    ip_addr_copy_from_ip4(management_transport_test_netif.ip_addr, address);
}

void management_transport_test_assert(bool condition)
{
    if (!condition)
    {
        management_transport_test_state.assert_failed = true;
    }
}

void management_transport_test_enter_critical(void)
{
    management_transport_test_state.critical_depth++;
}

void management_transport_test_exit_critical(void)
{
    if (management_transport_test_state.critical_depth == 0U)
    {
        management_transport_test_state.assert_failed = true;
        return;
    }
    management_transport_test_state.critical_depth--;
}

void management_transport_test_yield_from_isr(BaseType_t higher_priority_task_woken)
{
    if (higher_priority_task_woken != pdFALSE)
    {
        management_transport_test_state.yield_count++;
    }
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t task_function, const char *name, uint32_t stack_depth, void *argument,
                              UBaseType_t priority, StackType_t *stack_buffer, StaticTask_t *task_buffer)
{
    management_transport_test_state.task_create_count++;
    management_transport_test_assert(task_function != NULL);
    management_transport_test_assert(name != NULL);
    management_transport_test_assert(stack_depth != 0U);
    management_transport_test_assert(argument == NULL);
    management_transport_test_assert(priority != 0U);
    management_transport_test_assert(stack_buffer != NULL);
    management_transport_test_assert(task_buffer != NULL);

    if (management_transport_test_state.task_creation_fails)
    {
        return NULL;
    }
    task_entry = task_function;
    task_argument = argument;
    return &management_transport_test_task_token;
}

void vTaskNotifyGiveFromISR(TaskHandle_t task_handle, BaseType_t *higher_priority_task_woken)
{
    management_transport_test_assert(task_handle != NULL);
    management_transport_test_state.isr_notify_count++;
    management_transport_test_state.notification_count++;
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = pdTRUE;
    }
}

BaseType_t xTaskNotifyGive(TaskHandle_t task_handle)
{
    management_transport_test_assert(task_handle != NULL);
    management_transport_test_state.task_notify_count++;
    management_transport_test_state.notification_count++;
    return pdTRUE;
}

uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks_to_wait)
{
    TickType_t start_tick = management_transport_test_state.tick;
    uint32_t count;

    management_transport_test_assert(clear_count_on_exit == pdTRUE);
    management_transport_test_state.last_wait_ticks = ticks_to_wait;
    while (management_transport_test_state.notification_count == 0U &&
           (ticks_to_wait == portMAX_DELAY ||
            (TickType_t)(management_transport_test_state.tick - start_tick) < ticks_to_wait))
    {
        pause_task();
    }
    count = management_transport_test_state.notification_count;
    management_transport_test_state.notification_count = 0U;
    return count;
}

void vTaskDelay(TickType_t ticks_to_delay)
{
    management_transport_test_assert(management_transport_test_state.critical_depth == 0U);
    management_transport_test_state.delay_count++;
    management_transport_test_state.last_delay_ticks = ticks_to_delay;
    management_transport_test_state.tick += ticks_to_delay;
    if (management_transport_test_state.delay_hook != NULL)
    {
        management_transport_test_state.delay_hook();
    }
}

TickType_t xTaskGetTickCount(void)
{
    return management_transport_test_state.tick;
}

TickType_t xTaskGetTickCountFromISR(void)
{
    return management_transport_test_state.tick;
}

void watchdog_report(EventBits_t task_bit)
{
    management_transport_test_assert(task_started && pthread_equal(pthread_self(), task_thread) != 0);
    management_transport_test_assert(management_transport_test_state.critical_depth == 0U);
    management_transport_test_assert(task_bit == WATCHDOG_EVENT_MANAGEMENT);
    management_transport_test_state.watchdog_report_count++;
    management_transport_test_state.last_watchdog_report_tick = management_transport_test_state.tick;
}

int management_transport_test_log(const char *format, ...)
{
    va_list arguments;

    if (format == NULL)
    {
        return -1;
    }
    va_start(arguments, format);
    va_end(arguments);
    management_transport_test_state.log_count++;
    return 0;
}

management_transport_cdc_result_t management_transport_enable_receive(void)
{
    management_transport_test_state.cdc_enable_count++;
    return management_transport_test_state.cdc_enable_result;
}

management_transport_cdc_result_t management_transport_cdc_send(uint8_t *data, uint16_t length)
{
    management_transport_test_state.cdc_send_count++;
    if (management_transport_test_state.cdc_send_result != MANAGEMENT_TRANSPORT_CDC_OK)
    {
        return management_transport_test_state.cdc_send_result;
    }
    if (data == NULL || length == 0U || length > sizeof(management_transport_test_state.last_send_copy))
    {
        return MANAGEMENT_TRANSPORT_CDC_FAILED;
    }

    management_transport_test_state.last_send_data = data;
    management_transport_test_state.last_send_length = length;
    memcpy(management_transport_test_state.last_send_copy, data, length);
    return MANAGEMENT_TRANSPORT_CDC_OK;
}

bool management_transport_configuration_is_ready(void)
{
    return management_transport_test_state.configuration_ready;
}

const configuration_t *configuration_service_active(void)
{
    return management_transport_test_state.active_configuration_available
               ? &management_transport_test_active_configuration
               : NULL;
}

configuration_service_result_t configuration_service_write(const uint8_t *payload, uint32_t payload_length)
{
    management_transport_test_assert(management_transport_test_state.critical_depth == 0U);
    management_transport_test_state.write_count++;
    management_transport_test_state.write_payload_length = payload_length;
    if (payload != NULL && payload_length <= sizeof(management_transport_test_state.write_payload))
    {
        memcpy(management_transport_test_state.write_payload, payload, payload_length);
    }
    return management_transport_test_state.write_result;
}

configuration_binary_codec_result_t configuration_binary_encode(const configuration_t *configuration,
                                                                 uint8_t *payload, uint32_t payload_capacity,
                                                                 uint32_t *payload_length)
{
    management_transport_test_assert(management_transport_test_state.critical_depth == 0U);
    if (management_transport_test_state.encode_result != CONFIGURATION_BINARY_CODEC_OK)
    {
        return management_transport_test_state.encode_result;
    }
    if (configuration == NULL || payload == NULL || payload_length == NULL ||
        payload_capacity < management_transport_test_state.encoded_configuration_length)
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    memcpy(payload, management_transport_test_state.encoded_configuration,
           management_transport_test_state.encoded_configuration_length);
    *payload_length = management_transport_test_state.encoded_configuration_length;
    return CONFIGURATION_BINARY_CODEC_OK;
}

bool sntp_service_is_synchronized(void)
{
    return management_transport_test_state.sntp_synchronized;
}

bool sntp_service_get_time(uint32_t *unix_seconds, uint32_t *microseconds)
{
    management_transport_test_state.time_read_count++;
    if (!management_transport_test_state.sntp_time_available || unix_seconds == NULL || microseconds == NULL)
    {
        return false;
    }
    *unix_seconds = management_transport_test_state.unix_seconds;
    *microseconds = management_transport_test_state.microseconds;
    return true;
}

mqtt_publisher_state_t mqtt_publisher_get_state(void)
{
    return management_transport_test_state.mqtt_state;
}

void management_transport_test_system_reset(void)
{
    management_transport_test_assert(management_transport_test_state.critical_depth == 0U);
    management_transport_test_state.reset_count++;
    finish_task();
}
