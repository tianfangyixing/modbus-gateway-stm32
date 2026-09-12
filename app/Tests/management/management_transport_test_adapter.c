#include "management_transport_test_adapter.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/netif.h"
#include "mqtt_publisher.h"
#include "sntp_service.h"
#include "boot_control.h"
#include "watchdog.h"
#include "unity.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include "debug_log.h"
#include <stddef.h>
#include <string.h>

management_test_platform_t management_test_platform;
struct netif *netif_default;
static jmp_buf task_pause;
static void (*task_entry)(void *);
static void *task_argument;
static uint32_t notify_waits;
static uint32_t critical_depth;
static uint32_t tcpip_depth;

void management_test_platform_reset(void)
{
    memset(&management_test_platform, 0, sizeof(management_test_platform));
    management_test_platform.ready = true;
    management_test_platform.boot_success = true;
    management_test_platform.time_available = true;
    management_test_platform.send_result = MANAGEMENT_TRANSPORT_CDC_OK;
    netif_default = NULL;
    task_entry = NULL;
    task_argument = NULL;
    notify_waits = 0U;
    critical_depth = 0U;
    tcpip_depth = 0U;
}

TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t depth, void *argument,
                              uint32_t priority, StackType_t *stack, StaticTask_t *buffer)
{
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("mgmt", name);
    TEST_ASSERT_EQUAL_UINT32(1024U, depth);
    TEST_ASSERT_NULL(argument);
    TEST_ASSERT_EQUAL_UINT32(20U, priority);
    TEST_ASSERT_NOT_NULL(stack);
    TEST_ASSERT_NOT_NULL(buffer);
    task_entry = entry;
    task_argument = argument;
    return buffer;
}

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    TEST_ASSERT_EQUAL_INT(pdTRUE, clear);
    TEST_ASSERT_TRUE(ticks == 500U || ticks == 100U);
    TEST_ASSERT_EQUAL_UINT32(0U, critical_depth);
    if (notify_waits++ != 0U)
    {
        longjmp(task_pause, 1);
    }
    return 1U;
}

void management_transport_test_run_task(void)
{
    TEST_ASSERT_NOT_NULL(task_entry);
    notify_waits = 0U;
    if (setjmp(task_pause) == 0)
    {
        task_entry(task_argument);
    }
    TEST_ASSERT_EQUAL_UINT32(0U, critical_depth);
    TEST_ASSERT_EQUAL_UINT32(0U, tcpip_depth);
}

void vTaskNotifyGiveFromISR(TaskHandle_t handle, BaseType_t *woken)
{
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_NOT_NULL(woken);
    *woken = pdFALSE;
}

void management_test_yield(BaseType_t woken)
{
    TEST_ASSERT_EQUAL_INT(pdFALSE, woken);
}

void management_test_enter_critical(void)
{
    critical_depth++;
}

void management_test_exit_critical(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(0U, critical_depth);
    critical_depth--;
}

void management_test_lock_tcpip(void)
{
    tcpip_depth++;
}

void management_test_unlock_tcpip(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(0U, tcpip_depth);
    tcpip_depth--;
}

void vTaskDelay(TickType_t ticks)
{
    TEST_ASSERT_EQUAL_UINT32(500U, ticks);
    TEST_ASSERT_EQUAL_UINT32(0U, critical_depth);
    if (management_test_platform.close_during_reset_delay)
    {
        management_transport_session_close_from_isr();
    }
}

void watchdog_report(EventBits_t task_bit)
{
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_EVENT_MANAGEMENT, task_bit);
}

management_transport_cdc_result_t management_transport_enable_receive(void)
{
    management_test_platform.receive_rearms++;
    return MANAGEMENT_TRANSPORT_CDC_OK;
}

management_transport_cdc_result_t management_transport_cdc_send(uint8_t *data, uint16_t length)
{
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, length);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(sizeof(management_test_platform.sent), length);
    if (management_test_platform.send_result == MANAGEMENT_TRANSPORT_CDC_OK)
    {
        memcpy(management_test_platform.sent, data, length);
        management_test_platform.tx_pointer = data;
        management_test_platform.tx_length = length;
        management_test_platform.tx_calls++;
    }
    return management_test_platform.send_result;
}

void management_test_complete_tx(void)
{
    TEST_ASSERT_NOT_NULL(management_test_platform.tx_pointer);
    management_transport_transmit_complete_from_isr(management_test_platform.tx_pointer,
                                                    management_test_platform.tx_length);
    management_transport_test_process();
}

bool management_transport_configuration_is_ready(void)
{
    return management_test_platform.ready;
}

void management_transport_test_system_reset(void)
{
    management_test_platform.resets++;
}

bool boot_request_set(void)
{
    management_test_platform.boot_requests++;
    return management_test_platform.boot_success;
}

bool sntp_service_is_synchronized(void)
{
    return management_test_platform.synchronized;
}

bool sntp_service_get_time(uint32_t *unix_seconds, uint32_t *microseconds)
{
    TEST_ASSERT_NOT_NULL(unix_seconds);
    TEST_ASSERT_NOT_NULL(microseconds);
    management_test_platform.time_reads++;
    *unix_seconds = UINT32_C(0x12345678);
    *microseconds = 654321U;
    return management_test_platform.time_available;
}

mqtt_publisher_state_t mqtt_publisher_get_state(void)
{
    return MQTT_PUBLISHER_STATE_DISABLED;
}

int debug_log_printf(const char *format, ...)
{
    char scratch[64];
    va_list arguments;
    int length;
    TEST_ASSERT_NOT_NULL(format);
    va_start(arguments, format);
    length = vsnprintf(scratch, sizeof(scratch), format, arguments);
    va_end(arguments);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
    return length;
}
