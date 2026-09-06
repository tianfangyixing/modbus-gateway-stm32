#include "management_transport.h"

#include "FreeRTOS.h"
#include "configuration_binary_codec.h"
#include "configuration_service.h"
#include "debug_log.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "management_frame.h"
#include "memory_sections.h"
#include "mqtt_publisher.h"
#include "sntp_service.h"
#include "task.h"
#include "usbd_cdc.h"

#if defined(MANAGEMENT_TRANSPORT_TEST)
#include "management_transport_test_adapter.h"
#else
#include "main.h"
#endif

#include "watchdog.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define MANAGEMENT_TRANSPORT_TASK_STACK_DEPTH 1024U
#define MANAGEMENT_TRANSPORT_TASK_PRIORITY 20U
#define MANAGEMENT_TRANSPORT_USB_PACKET_SIZE 64U
#define MANAGEMENT_TRANSPORT_CDC_RETRY_MS 10U
#define MANAGEMENT_TRANSPORT_CDC_ERROR_LOG_INTERVAL_MS 1000U

#define MANAGEMENT_MESSAGE_ERROR_RESPONSE UINT8_C(0xFF)
#define MANAGEMENT_MESSAGE_GET_ACTIVE_CONFIGURATION UINT8_C(0x01)
#define MANAGEMENT_MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE UINT8_C(0x81)
#define MANAGEMENT_MESSAGE_PUT_CONFIGURATION UINT8_C(0x02)
#define MANAGEMENT_MESSAGE_PUT_CONFIGURATION_RESPONSE UINT8_C(0x82)
#define MANAGEMENT_MESSAGE_GET_STATUS UINT8_C(0x03)
#define MANAGEMENT_MESSAGE_GET_STATUS_RESPONSE UINT8_C(0x83)
#define MANAGEMENT_MESSAGE_RESTART UINT8_C(0x04)
#define MANAGEMENT_MESSAGE_RESTART_RESPONSE UINT8_C(0x84)

#define MANAGEMENT_RESULT_INVALID_REQUEST UINT16_C(1)
#define MANAGEMENT_RESULT_UNSUPPORTED_MESSAGE UINT16_C(2)
#define MANAGEMENT_RESULT_CONFIGURATION_INVALID UINT16_C(3)
#define MANAGEMENT_RESULT_RESOURCE_UNAVAILABLE UINT16_C(4)
#define MANAGEMENT_RESULT_STORAGE_IO_ERROR UINT16_C(5)
#define MANAGEMENT_RESULT_NOT_READY UINT16_C(6)
#define MANAGEMENT_RESULT_INTERNAL_ERROR UINT16_C(7)

#if CONFIGURATION_V1_MAX_PAYLOAD_LENGTH > (MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH - UINT32_C(2))
#error "Configuration payload does not fit in a Management response"
#endif

#define MANAGEMENT_TRANSPORT_MAX_RESPONSE_PAYLOAD_LENGTH \
    (UINT32_C(2) + CONFIGURATION_V1_MAX_PAYLOAD_LENGTH)

#define RX_BUF_MAX_LEN 128
#define TX_BUF_MAX_LEN (MANAGEMENT_FRAME_HEADER_LENGTH + MANAGEMENT_TRANSPORT_MAX_RESPONSE_PAYLOAD_LENGTH + MANAGEMENT_FRAME_CRC_LENGTH)

// 被isr和任务访问
static volatile uint32_t session_id;
static volatile bool session_is_open;

static CCM_SRAM uint8_t session_rx_buf[RX_BUF_MAX_LEN];
static volatile uint8_t session_rx_len;
static volatile bool session_rx_in_progress = false;
static volatile bool session_rx_completed = false;


static volatile bool session_tx_in_progress = false;
static volatile bool session_tx_completed = false;

// 仅在任务中访问
static uint8_t tx_buf[TX_BUF_MAX_LEN];
static uint32_t tx_len;
static uint32_t last_session_id = 0U;
static uint8_t rx_buf_copy[RX_BUF_MAX_LEN];
static uint8_t rx_buf_len = 0U;
static bool tx_successful = false;
static volatile uint8_t reset_pending = 0U;
enum
{
    MANAGEMENT_STATE_IDLE,
    MANAGEMENT_STATE_RECVIVING,
    MANAGEMENT_STATE_SENDING,
    MANAGEMENT_STATE_FAULT
} volatile management_state;



static management_frame_parser_t management_parser;
static CCM_SRAM_ALIGNED(4) uint8_t management_received_frame[MANAGEMENT_FRAME_MAX_LENGTH];

static TaskHandle_t management_task_handle;
static CCM_SRAM_ALIGNED(8) StackType_t management_task_stack[MANAGEMENT_TRANSPORT_TASK_STACK_DEPTH];
static CCM_SRAM StaticTask_t management_task_buffer;

static uint16_t management_transport_map_configuration_result(configuration_service_result_t result)
{
    switch (result)
    {
        case CONFIGURATION_SERVICE_OK:
            return 0U;

        case CONFIGURATION_SERVICE_INVALID_ARGUMENT:
        case CONFIGURATION_SERVICE_INVALID_PAYLOAD:
            return MANAGEMENT_RESULT_CONFIGURATION_INVALID;

        case CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE:
            return MANAGEMENT_RESULT_RESOURCE_UNAVAILABLE;

        case CONFIGURATION_SERVICE_IO_ERROR:
            return MANAGEMENT_RESULT_STORAGE_IO_ERROR;

        case CONFIGURATION_SERVICE_NOT_INITIALIZED:
            return MANAGEMENT_RESULT_NOT_READY;

        default:
            return MANAGEMENT_RESULT_INTERNAL_ERROR;
    }
}

static void management_transport_notify_task_from_isr(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (management_task_handle == NULL)
    {
        return;
    }

    vTaskNotifyGiveFromISR(management_task_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void management_transport_dispatch_get_active_configuration(const management_frame_view_t *frame)
{
    const configuration_t *active_configuration;
    uint32_t configuration_length = 0U;
    uint32_t payload_length;

    tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH] = MANAGEMENT_MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE;
    tx_len = 2U;
    if (frame->payload_length != 0U)
    {
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
                                      MANAGEMENT_RESULT_INVALID_REQUEST);
    }
    else if (!management_transport_configuration_is_ready())
    {
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], MANAGEMENT_RESULT_NOT_READY);
    }
    else
    {
        active_configuration = configuration_service_active();
        if (active_configuration == NULL)
        {
            management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
                                          MANAGEMENT_RESULT_INTERNAL_ERROR);
        }
        else if (configuration_binary_encode(active_configuration,
                                             &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH + 2U],
                                             CONFIGURATION_V1_MAX_PAYLOAD_LENGTH, &configuration_length) !=
                 CONFIGURATION_BINARY_CODEC_OK)
        {
            management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
                                          MANAGEMENT_RESULT_INTERNAL_ERROR);
        }
        else
        {
            management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], 0U);
            tx_len += configuration_length;
        }
    }

    payload_length = tx_len;
    tx_len = 0U;
    if (management_frame_encode(tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH], frame->transaction_id,
                                &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], payload_length,
                                tx_buf, sizeof(tx_buf), &tx_len) != MANAGEMENT_FRAME_OK)
    {
        tx_len = 0U;
    }
}

static void management_transport_dispatch_put_configuration(const management_frame_view_t *frame)
{
    uint32_t payload_length;

    tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH] = MANAGEMENT_MESSAGE_PUT_CONFIGURATION_RESPONSE;
    tx_len = 2U;
    if (!management_transport_configuration_is_ready())
    {
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], MANAGEMENT_RESULT_NOT_READY);
    }
    else
    {
        management_frame_write_u16_le(
            &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
            management_transport_map_configuration_result(
                configuration_service_write(frame->payload, frame->payload_length)));
    }

    payload_length = tx_len;
    tx_len = 0U;
    if (management_frame_encode(tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH], frame->transaction_id,
                                &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], payload_length,
                                tx_buf, sizeof(tx_buf), &tx_len) != MANAGEMENT_FRAME_OK)
    {
        tx_len = 0U;
    }
}

static void management_transport_dispatch_get_status(const management_frame_view_t *frame)
{
    uint8_t *status = &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH + 2U];
    uint32_t microseconds = 0U;
    uint32_t payload_length;
    uint32_t unix_seconds = 0U;

    tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH] = MANAGEMENT_MESSAGE_GET_STATUS_RESPONSE;
    tx_len = 2U;
    if (frame->payload_length != 0U)
    {
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
                                      MANAGEMENT_RESULT_INVALID_REQUEST);
    }
    else
    {
        memset(status, 0, 15U);
#if !NO_SYS
        LOCK_TCPIP_CORE();
#endif
        if (netif_default != NULL)
        {
            const ip4_addr_t *ipv4 = netif_ip4_addr(netif_default);

            status[0] = netif_is_link_up(netif_default) ? 1U : 0U;
            status[1] = ip4_addr1(ipv4);
            status[2] = ip4_addr2(ipv4);
            status[3] = ip4_addr3(ipv4);
            status[4] = ip4_addr4(ipv4);
        }
#if !NO_SYS
        UNLOCK_TCPIP_CORE();
#endif

        status[5] = sntp_service_is_synchronized() ? 1U : 0U;
        if (status[5] != 0U && !sntp_service_get_time(&unix_seconds, &microseconds))
        {
            unix_seconds = 0U;
            microseconds = 0U;
        }
        management_frame_write_u32_le(&status[6], unix_seconds);
        management_frame_write_u32_le(&status[10], microseconds);
        status[14] = (uint8_t)mqtt_publisher_get_state();
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], 0U);
        tx_len += 15U;
    }

    payload_length = tx_len;
    tx_len = 0U;
    if (management_frame_encode(tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH], frame->transaction_id,
                                &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], payload_length,
                                tx_buf, sizeof(tx_buf), &tx_len) != MANAGEMENT_FRAME_OK)
    {
        tx_len = 0U;
    }
}

static void management_transport_dispatch_restart(const management_frame_view_t *frame)
{
    uint32_t payload_length;

    tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH] = MANAGEMENT_MESSAGE_RESTART_RESPONSE;
    tx_len = 2U;
    if (frame->payload_length == 0U)
    {
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], 0U);
        reset_pending = true;
    }
    else
    {
        management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
                                      MANAGEMENT_RESULT_INVALID_REQUEST);
    }

    payload_length = tx_len;
    tx_len = 0U;
    if (management_frame_encode(tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH], frame->transaction_id,
                                &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], payload_length,
                                tx_buf, sizeof(tx_buf), &tx_len) != MANAGEMENT_FRAME_OK)
    {
        tx_len = 0U;
        reset_pending = false;
    }
}

static void management_transport_dispatch_request(const management_frame_view_t *frame)
{
    reset_pending = false;

    switch (frame->message_type)
    {
        case MANAGEMENT_MESSAGE_GET_ACTIVE_CONFIGURATION:
            management_transport_dispatch_get_active_configuration(frame);
            break;

        case MANAGEMENT_MESSAGE_PUT_CONFIGURATION:
            management_transport_dispatch_put_configuration(frame);
            break;

        case MANAGEMENT_MESSAGE_GET_STATUS:
            management_transport_dispatch_get_status(frame);
            break;

        case MANAGEMENT_MESSAGE_RESTART:
            management_transport_dispatch_restart(frame);
            break;

        default:
        {
            uint32_t payload_length;

            tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH] = MANAGEMENT_MESSAGE_ERROR_RESPONSE;
            tx_len = 2U;
            management_frame_write_u16_le(&tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH],
                                          MANAGEMENT_RESULT_UNSUPPORTED_MESSAGE);
            payload_length = tx_len;
            tx_len = 0U;
            if (management_frame_encode(tx_buf[MANAGEMENT_FRAME_MAGIC_LENGTH], frame->transaction_id,
                                        &tx_buf[MANAGEMENT_FRAME_HEADER_LENGTH], payload_length,
                                        tx_buf, sizeof(tx_buf), &tx_len) !=
                MANAGEMENT_FRAME_OK)
            {
                tx_len = 0U;
            }
            break;
        }
    }
}

static void management_transport_handle_frame(const management_frame_view_t *frame, void *context)
{
    configASSERT(context == NULL);

    management_transport_dispatch_request(frame);

    management_state = MANAGEMENT_STATE_SENDING;
}


static void management_transport_process(void)
{
    uint32_t wait_ticks = pdMS_TO_TICKS(500);


    while(1)
    {
        watchdog_report(WATCHDOG_EVENT_MANAGEMENT);

        ulTaskNotifyTake(pdTRUE, wait_ticks);
        wait_ticks = pdMS_TO_TICKS(500);

        

        // 在临界区处理各个事件，session一切值即被任务读写，也被isr读写

        taskENTER_CRITICAL();

        take_events:

        // 被插拔一次或多次，此时处于开状态
        if (last_session_id != session_id && session_is_open)
        {
            management_state = MANAGEMENT_STATE_RECVIVING;
            last_session_id = session_id;
            tx_len = 0;
            tx_successful = false;
            reset_pending = 0U;
            management_frame_parser_reset(&management_parser);
        }
        else if (!session_is_open) // 拔出一次或多次，此时处于关状态
        {
            management_state = MANAGEMENT_STATE_IDLE;
            last_session_id = session_id;
            reset_pending = 0;
            taskEXIT_CRITICAL();
            continue;
        }

        if (management_state == MANAGEMENT_STATE_RECVIVING && session_rx_completed)
        {
            rx_buf_len = session_rx_len;
            if (rx_buf_len != 0U)
            {
                memcpy(rx_buf_copy, session_rx_buf, rx_buf_len);
            }
            session_rx_len = 0U;  
            session_rx_completed = false;
        }
        else if (management_state == MANAGEMENT_STATE_SENDING && session_tx_completed)
        {
            session_tx_completed = false;
            tx_successful = true;
        }

        taskEXIT_CRITICAL();

        // 处理接收或发送事件，与重置处理

        if (reset_pending)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
#if defined(MANAGEMENT_TRANSPORT_TEST)
            management_transport_test_system_reset();
#else
            NVIC_SystemReset();
#endif
        }

        // 接收消息的处理

        if (rx_buf_len != 0)
        {
            // 帧错误，直接进入FAULT，必须重新插拔后才能使用
            if (management_frame_parser_feed(&management_parser, rx_buf_copy, rx_buf_len, management_transport_handle_frame, NULL) != MANAGEMENT_FRAME_OK)
            {
                management_state = MANAGEMENT_STATE_FAULT;
            }

            rx_buf_len = 0;
        }
        
        // 发送消息成功处理
        if(tx_successful)
        {
            management_state = MANAGEMENT_STATE_RECVIVING;
            tx_successful = false;
        }

        // 进入临界区启动发送，启动接收
        taskENTER_CRITICAL();
        // 必须先检查是否已重新插拔，如果是，则放弃发送和接收
        if (last_session_id != session_id || !session_is_open)
        {
            goto take_events;
        }

        if(management_state == MANAGEMENT_STATE_RECVIVING && !session_rx_in_progress)
        {
            if(management_transport_enable_receive() == MANAGEMENT_TRANSPORT_CDC_OK)
            {
                session_rx_in_progress = true;
            }
            else
            {
                wait_ticks = pdMS_TO_TICKS(100);
            }
        }
        else if(management_state == MANAGEMENT_STATE_SENDING && !session_tx_in_progress)
        {
            if(management_transport_cdc_send(tx_buf, tx_len) == MANAGEMENT_TRANSPORT_CDC_OK)
            {
                session_tx_in_progress = true;
            }
            else
            {
                wait_ticks = pdMS_TO_TICKS(100);
            }
        }

        taskEXIT_CRITICAL();
    }


}

static void management_transport_task(void *argument)
{
    configASSERT(argument == NULL);

    management_transport_process();
}

management_transport_result_t management_transport_init(void)
{
    management_frame_result_t parser_result;

    parser_result = management_frame_parser_init(&management_parser, management_received_frame,
                                                 sizeof(management_received_frame));
    if (parser_result != MANAGEMENT_FRAME_OK)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    session_id = 0;
    session_is_open = false;
    session_rx_len = 0;
    management_state = MANAGEMENT_STATE_IDLE;

    management_task_handle = xTaskCreateStatic(
        management_transport_task, "mgmt", MANAGEMENT_TRANSPORT_TASK_STACK_DEPTH, NULL,
        MANAGEMENT_TRANSPORT_TASK_PRIORITY, management_task_stack, &management_task_buffer);


    return MANAGEMENT_TRANSPORT_OK;
}

void management_transport_session_open_from_isr(void)
{
    session_rx_len = 0;
    session_rx_in_progress = true;
    session_rx_completed = false;
    session_tx_in_progress = false;
    session_tx_completed = false;
    session_id++;
    session_is_open = true;
    management_transport_notify_task_from_isr();
}

void management_transport_session_close_from_isr(void)
{
    session_is_open = false;
    management_transport_notify_task_from_isr();
}

void management_transport_receive_from_isr(const uint8_t *data, uint32_t length)
{
    if (length != 0U)
    {
        memcpy(session_rx_buf, data, length);
    }
    session_rx_len = (uint8_t)length;
    session_rx_in_progress = false;
    session_rx_completed = true;
    management_transport_notify_task_from_isr();
}

void management_transport_transmit_complete_from_isr(const uint8_t *data, uint32_t length)
{
    configASSERT(data != NULL);
    configASSERT(length != 0U);
    session_tx_in_progress = false;
    session_tx_completed = true;
    management_transport_notify_task_from_isr();
}

#if defined(MANAGEMENT_TRANSPORT_TEST)
void management_transport_test_reset(void)
{
    session_id = 0U;
    session_is_open = false;
    session_rx_len = 0U;
    session_rx_in_progress = false;
    session_rx_completed = false;
    session_tx_in_progress = false;
    session_tx_completed = false;
    tx_len = 0U;
    last_session_id = 0U;
    rx_buf_len = 0U;
    tx_successful = false;
    reset_pending = 0U;
    management_state = MANAGEMENT_STATE_IDLE;
    management_task_handle = NULL;
    memset(management_task_stack, 0, sizeof(management_task_stack));
    memset(&management_task_buffer, 0, sizeof(management_task_buffer));
    memset(session_rx_buf, 0, sizeof(session_rx_buf));
    memset(management_received_frame, 0, sizeof(management_received_frame));
    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf_copy, 0, sizeof(rx_buf_copy));
    memset(&management_parser, 0, sizeof(management_parser));
}

void management_transport_test_process(void)
{
    management_transport_test_run_task();
}
#endif
