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

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define MANAGEMENT_TRANSPORT_TASK_STACK_DEPTH 768U
#define MANAGEMENT_TRANSPORT_TASK_PRIORITY 20U
#define MANAGEMENT_TRANSPORT_USB_PACKET_SIZE 64U
#define MANAGEMENT_TRANSPORT_INTER_BYTE_TIMEOUT_MS 2000U
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
#define MANAGEMENT_TRANSPORT_TX_BUFFER_LENGTH \
    (MANAGEMENT_FRAME_HEADER_LENGTH + MANAGEMENT_TRANSPORT_MAX_RESPONSE_PAYLOAD_LENGTH + MANAGEMENT_FRAME_CRC_LENGTH)

typedef enum
{
    MANAGEMENT_LIFECYCLE_UNINITIALIZED = 0,
    MANAGEMENT_LIFECYCLE_INACTIVE,
    MANAGEMENT_LIFECYCLE_ACTIVE,
    MANAGEMENT_LIFECYCLE_FAILED
} management_lifecycle_t;

typedef enum
{
    MANAGEMENT_RESPONSE_EMPTY = 0,
    MANAGEMENT_RESPONSE_READY,
    MANAGEMENT_RESPONSE_IN_FLIGHT,
    MANAGEMENT_RESPONSE_CACHED
} management_response_state_t;

typedef struct
{
    volatile bool open;
    volatile uint32_t generation;
} management_session_observation_t;

typedef struct
{
    volatile bool enabled;
    volatile bool occupied;
    volatile bool faulted;
    volatile bool fault_log_pending;
    volatile uint16_t length;
    volatile uint32_t generation;
    volatile TickType_t arrival_tick;
} management_receive_mailbox_t;

typedef struct
{
    volatile bool active;
    const uint8_t *volatile data;
    volatile uint16_t length;
    volatile uint8_t endpoint;
    volatile uint32_t generation;
} management_send_token_t;

typedef struct
{
    management_send_token_t token;
    volatile bool completion_pending;
    volatile uint32_t completion_generation;
} management_send_observation_t;

typedef struct
{
    management_response_state_t state;
    uint32_t transaction_id;
    uint32_t generation;
    uint16_t length;
    bool retry_waiting;
    TickType_t retry_deadline;
    bool reset_after_send;
} management_response_slot_t;

typedef struct
{
    bool initialized;
    TickType_t deadline;
} management_cdc_error_log_t;

typedef struct
{
    uint8_t message_type;
    uint16_t result_code;
    uint32_t payload_length;
    bool requests_restart;
} management_response_description_t;

typedef struct
{
    bool packet_pending;
    uint16_t packet_length;
    uint32_t packet_generation;
    TickType_t packet_arrival_tick;
    bool completion_pending;
    uint32_t completion_generation;
    bool receive_fault_log_pending;
} management_task_events_t;

typedef struct
{
    TaskHandle_t task_handle;
    volatile management_lifecycle_t lifecycle;
    management_session_observation_t observed_session;
    management_receive_mailbox_t receive_mailbox;
    management_send_observation_t send_observation;
    management_frame_parser_t parser;
    uint32_t task_generation;
    bool task_session_open;
    bool parser_tick_valid;
    TickType_t parser_last_byte_tick;
    management_response_slot_t response;
    bool reset_pending;
    management_cdc_error_log_t cdc_error_log;
} management_transport_context_t;

static CCM_SRAM_ALIGNED(8) StackType_t management_task_stack[MANAGEMENT_TRANSPORT_TASK_STACK_DEPTH];
static CCM_SRAM StaticTask_t management_task_control;
static CCM_SRAM_ALIGNED(4) uint8_t management_rx_packet[MANAGEMENT_TRANSPORT_USB_PACKET_SIZE];
static uint8_t management_received_frame[MANAGEMENT_FRAME_MAX_LENGTH] __attribute__((aligned(4)));
static CCM_SRAM_ALIGNED(4) uint8_t management_tx_buffer[MANAGEMENT_TRANSPORT_TX_BUFFER_LENGTH];
static CCM_SRAM management_transport_context_t management_context;

static void management_transport_notify_task_from_isr(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (management_context.task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(management_context.task_handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

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

static void management_transport_reset_response(void)
{
    management_context.response.state = MANAGEMENT_RESPONSE_EMPTY;
    management_context.response.transaction_id = 0U;
    management_context.response.generation = 0U;
    management_context.response.length = 0U;
    management_context.response.retry_waiting = false;
    management_context.response.retry_deadline = 0U;
    management_context.response.reset_after_send = false;
    management_context.reset_pending = false;
    management_context.cdc_error_log.initialized = false;
    management_context.cdc_error_log.deadline = 0U;
}

static void management_transport_reset_parser(void)
{
    management_frame_parser_reset(&management_context.parser);
    management_context.parser_tick_valid = false;
    management_context.parser_last_byte_tick = 0U;
}

static bool management_transport_session_is_current(uint32_t generation)
{
    bool current;

    taskENTER_CRITICAL();
    current = management_context.observed_session.open &&
              management_context.observed_session.generation == generation;
    taskEXIT_CRITICAL();

    return current;
}

static void management_transport_synchronize_session(void)
{
    uint32_t generation;
    bool open;

    taskENTER_CRITICAL();
    generation = management_context.observed_session.generation;
    open = management_context.observed_session.open;
    taskEXIT_CRITICAL();

    if (management_context.task_generation != generation || management_context.task_session_open != open)
    {
        management_transport_reset_parser();
        management_transport_reset_response();
        management_context.task_generation = generation;
        management_context.task_session_open = open;
    }
}

static void management_transport_mark_receive_fault(void)
{
    taskENTER_CRITICAL();
    management_context.receive_mailbox.enabled = false;
    management_context.receive_mailbox.faulted = true;
    taskEXIT_CRITICAL();
}

static void management_transport_take_events(management_task_events_t *events, uint8_t *packet)
{
    memset(events, 0, sizeof(*events));

    taskENTER_CRITICAL();
    if (management_context.receive_mailbox.occupied)
    {
        events->packet_pending = true;
        events->packet_length = management_context.receive_mailbox.length;
        events->packet_generation = management_context.receive_mailbox.generation;
        events->packet_arrival_tick = management_context.receive_mailbox.arrival_tick;
        if (events->packet_length != 0U)
        {
            memcpy(packet, management_rx_packet, events->packet_length);
        }
        management_context.receive_mailbox.occupied = false;
        management_context.receive_mailbox.length = 0U;
    }

    events->completion_pending = management_context.send_observation.completion_pending;
    events->completion_generation = management_context.send_observation.completion_generation;
    management_context.send_observation.completion_pending = false;

    events->receive_fault_log_pending = management_context.receive_mailbox.fault_log_pending;
    management_context.receive_mailbox.fault_log_pending = false;
    taskEXIT_CRITICAL();
}

static void management_transport_dispatch_get_active_configuration(const management_frame_view_t *frame,
                                                                   management_response_description_t *response)
{
    const configuration_t *active_configuration;
    uint32_t configuration_length = 0U;

    response->message_type = MANAGEMENT_MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE;
    if (frame->payload_length != 0U)
    {
        response->result_code = MANAGEMENT_RESULT_INVALID_REQUEST;
        return;
    }
    if (!management_transport_configuration_is_ready())
    {
        response->result_code = MANAGEMENT_RESULT_NOT_READY;
        return;
    }

    active_configuration = configuration_service_active();
    if (active_configuration == NULL)
    {
        response->result_code = MANAGEMENT_RESULT_INTERNAL_ERROR;
        return;
    }
    if (configuration_binary_encode(active_configuration,
                                    &management_tx_buffer[MANAGEMENT_FRAME_HEADER_LENGTH + 2U],
                                    CONFIGURATION_V1_MAX_PAYLOAD_LENGTH, &configuration_length) !=
        CONFIGURATION_BINARY_CODEC_OK)
    {
        response->result_code = MANAGEMENT_RESULT_INTERNAL_ERROR;
        return;
    }

    response->result_code = 0U;
    response->payload_length += configuration_length;
}

static void management_transport_dispatch_put_configuration(const management_frame_view_t *frame,
                                                             management_response_description_t *response)
{
    response->message_type = MANAGEMENT_MESSAGE_PUT_CONFIGURATION_RESPONSE;
    if (!management_transport_configuration_is_ready())
    {
        response->result_code = MANAGEMENT_RESULT_NOT_READY;
        return;
    }

    response->result_code = management_transport_map_configuration_result(
        configuration_service_write(frame->payload, frame->payload_length));
}

static void management_transport_dispatch_get_status(const management_frame_view_t *frame,
                                                      management_response_description_t *response)
{
    uint8_t *status = &management_tx_buffer[MANAGEMENT_FRAME_HEADER_LENGTH + 2U];
    uint32_t microseconds = 0U;
    uint32_t unix_seconds = 0U;

    response->message_type = MANAGEMENT_MESSAGE_GET_STATUS_RESPONSE;
    if (frame->payload_length != 0U)
    {
        response->result_code = MANAGEMENT_RESULT_INVALID_REQUEST;
        return;
    }

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
    if (!sntp_service_get_time(&unix_seconds, &microseconds))
    {
        unix_seconds = 0U;
        microseconds = 0U;
    }
    management_frame_write_u32_le(&status[6], unix_seconds);
    management_frame_write_u32_le(&status[10], microseconds);
    status[14] = (uint8_t)mqtt_publisher_get_state();
    response->result_code = 0U;
    response->payload_length = 17U;
}

static void management_transport_dispatch_restart(const management_frame_view_t *frame,
                                                   management_response_description_t *response)
{
    response->message_type = MANAGEMENT_MESSAGE_RESTART_RESPONSE;
    if (frame->payload_length == 0U)
    {
        response->result_code = 0U;
        response->requests_restart = true;
    }
    else
    {
        response->result_code = MANAGEMENT_RESULT_INVALID_REQUEST;
    }
}

static void management_transport_dispatch_request(const management_frame_view_t *frame,
                                                  management_response_description_t *response)
{
    response->message_type = MANAGEMENT_MESSAGE_ERROR_RESPONSE;
    response->result_code = MANAGEMENT_RESULT_UNSUPPORTED_MESSAGE;
    response->payload_length = 2U;
    response->requests_restart = false;

    switch (frame->message_type)
    {
        case MANAGEMENT_MESSAGE_GET_ACTIVE_CONFIGURATION:
            management_transport_dispatch_get_active_configuration(frame, response);
            break;

        case MANAGEMENT_MESSAGE_PUT_CONFIGURATION:
            management_transport_dispatch_put_configuration(frame, response);
            break;

        case MANAGEMENT_MESSAGE_GET_STATUS:
            management_transport_dispatch_get_status(frame, response);
            break;

        case MANAGEMENT_MESSAGE_RESTART:
            management_transport_dispatch_restart(frame, response);
            break;

        default:
            break;
    }
}

static bool management_transport_encode_response(const management_frame_view_t *frame,
                                                 const management_response_description_t *response,
                                                 uint32_t *frame_length)
{
    management_frame_write_u16_le(&management_tx_buffer[MANAGEMENT_FRAME_HEADER_LENGTH], response->result_code);
    return management_frame_encode(response->message_type, frame->transaction_id,
                                   &management_tx_buffer[MANAGEMENT_FRAME_HEADER_LENGTH], response->payload_length,
                                   management_tx_buffer, sizeof(management_tx_buffer), frame_length) ==
           MANAGEMENT_FRAME_OK;
}

static void management_transport_handle_frame(const management_frame_view_t *frame, void *context)
{
    const uint32_t *frame_generation = context;
    management_response_description_t response;
    uint32_t frame_length = 0U;

    if (frame == NULL || frame_generation == NULL)
    {
        management_transport_mark_receive_fault();
        return;
    }
    if (!management_transport_session_is_current(*frame_generation))
    {
        return;
    }

    if (management_context.response.state == MANAGEMENT_RESPONSE_CACHED &&
        management_context.response.transaction_id == frame->transaction_id)
    {
        management_context.response.state = MANAGEMENT_RESPONSE_READY;
        management_context.response.retry_waiting = false;
        return;
    }
    if (management_context.response.state != MANAGEMENT_RESPONSE_EMPTY &&
        management_context.response.state != MANAGEMENT_RESPONSE_CACHED)
    {
        return;
    }

    management_context.response.state = MANAGEMENT_RESPONSE_EMPTY;
    management_transport_dispatch_request(frame, &response);
    if (!management_transport_encode_response(frame, &response, &frame_length))
    {
        management_transport_mark_receive_fault();
        return;
    }
    if (!management_transport_session_is_current(*frame_generation))
    {
        return;
    }

    management_context.response.transaction_id = frame->transaction_id;
    management_context.response.generation = *frame_generation;
    management_context.response.length = (uint16_t)frame_length;
    management_context.response.retry_waiting = false;
    management_context.response.reset_after_send = response.requests_restart;
    management_context.response.state = MANAGEMENT_RESPONSE_READY;
}

static void management_transport_handle_completion(const management_task_events_t *events)
{
    if (!events->completion_pending ||
        !management_transport_session_is_current(events->completion_generation))
    {
        return;
    }
    if (management_context.response.state != MANAGEMENT_RESPONSE_IN_FLIGHT ||
        management_context.response.generation != events->completion_generation)
    {
        return;
    }

    management_context.response.state = MANAGEMENT_RESPONSE_CACHED;
    if (management_context.response.reset_after_send)
    {
        management_context.reset_pending = true;
    }
}

static management_transport_result_t management_transport_enable_receive_from_task(void)
{
    management_transport_cdc_result_t result = MANAGEMENT_TRANSPORT_CDC_FAILED;
    bool attempted = false;
    bool log_failure = false;

    taskENTER_CRITICAL();
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_ACTIVE && management_context.task_session_open &&
        management_context.observed_session.open &&
        management_context.task_generation == management_context.observed_session.generation &&
        !management_context.receive_mailbox.enabled && !management_context.receive_mailbox.occupied &&
        !management_context.receive_mailbox.faulted)
    {
        attempted = true;
        result = management_transport_cdc_enable_receive();
        if (result == MANAGEMENT_TRANSPORT_CDC_OK)
        {
            management_context.receive_mailbox.enabled = true;
        }
        else
        {
            management_context.receive_mailbox.faulted = true;
            log_failure = true;
        }
    }
    taskEXIT_CRITICAL();

    if (log_failure)
    {
        debug_log_printf("Management CDC receive enable failed\n");
    }
    if (!attempted || result == MANAGEMENT_TRANSPORT_CDC_OK)
    {
        return MANAGEMENT_TRANSPORT_OK;
    }
    return MANAGEMENT_TRANSPORT_FAILED;
}

static void management_transport_try_send(void)
{
    TickType_t current_tick = xTaskGetTickCount();
    bool log_error = false;

    taskENTER_CRITICAL();
    if (management_context.task_session_open && management_context.observed_session.open &&
        management_context.task_generation == management_context.observed_session.generation &&
        management_context.response.state == MANAGEMENT_RESPONSE_READY &&
        management_context.response.generation == management_context.task_generation &&
        (!management_context.response.retry_waiting ||
         (int32_t)(current_tick - management_context.response.retry_deadline) >= 0))
    {
        management_transport_cdc_result_t result =
            management_transport_cdc_send(management_tx_buffer, management_context.response.length);

        if (result == MANAGEMENT_TRANSPORT_CDC_OK)
        {
            management_context.send_observation.token.data = management_tx_buffer;
            management_context.send_observation.token.length = management_context.response.length;
            management_context.send_observation.token.endpoint = (uint8_t)(CDC_IN_EP & 0x0FU);
            management_context.send_observation.token.generation = management_context.response.generation;
            management_context.send_observation.token.active = true;
            management_context.response.retry_waiting = false;
            management_context.response.state = MANAGEMENT_RESPONSE_IN_FLIGHT;
        }
        else
        {
            management_context.response.retry_waiting = true;
            management_context.response.retry_deadline =
                current_tick + pdMS_TO_TICKS(MANAGEMENT_TRANSPORT_CDC_RETRY_MS);
            if (result == MANAGEMENT_TRANSPORT_CDC_FAILED &&
                (!management_context.cdc_error_log.initialized ||
                 (int32_t)(current_tick - management_context.cdc_error_log.deadline) >= 0))
            {
                management_context.cdc_error_log.initialized = true;
                management_context.cdc_error_log.deadline =
                    current_tick + pdMS_TO_TICKS(MANAGEMENT_TRANSPORT_CDC_ERROR_LOG_INTERVAL_MS);
                log_error = true;
            }
        }
    }
    taskEXIT_CRITICAL();

    if (log_error)
    {
        debug_log_printf("Management CDC transmit failed; retrying\n");
    }
}

static TickType_t management_transport_wait_ticks(void)
{
    TickType_t current_tick = xTaskGetTickCount();
    TickType_t wait_ticks = portMAX_DELAY;

    if (management_context.task_session_open && management_context.response.state == MANAGEMENT_RESPONSE_READY &&
        management_context.response.retry_waiting)
    {
        wait_ticks = (int32_t)(current_tick - management_context.response.retry_deadline) >= 0
                         ? 0U
                         : management_context.response.retry_deadline - current_tick;
    }

    return wait_ticks;
}

static bool management_transport_restart_if_ready(void)
{
    bool restart = false;

    taskENTER_CRITICAL();
    if (management_context.reset_pending && management_context.task_session_open &&
        management_context.observed_session.open &&
        management_context.task_generation == management_context.observed_session.generation)
    {
        management_context.reset_pending = false;
        restart = true;
    }
    taskEXIT_CRITICAL();

    if (restart)
    {
#if defined(MANAGEMENT_TRANSPORT_TEST)
        management_transport_test_system_reset();
#else
        NVIC_SystemReset();
#endif
    }

    return restart;
}

static void management_transport_feed_packet(const management_task_events_t *events, const uint8_t *packet)
{
    uint32_t packet_generation = events->packet_generation;
    TickType_t timeout_ticks = pdMS_TO_TICKS(MANAGEMENT_TRANSPORT_INTER_BYTE_TIMEOUT_MS);

    if (management_context.parser.length != 0U && management_context.parser_tick_valid &&
        (TickType_t)(events->packet_arrival_tick - management_context.parser_last_byte_tick) >= timeout_ticks)
    {
        management_transport_reset_parser();
    }

    if (management_frame_parser_feed(&management_context.parser, packet, events->packet_length,
                                     management_transport_handle_frame, &packet_generation) !=
        MANAGEMENT_FRAME_OK)
    {
        management_transport_mark_receive_fault();
        return;
    }

    if (management_context.parser.length != 0U)
    {
        management_context.parser_tick_valid = true;
        management_context.parser_last_byte_tick = events->packet_arrival_tick;
    }
    else
    {
        management_context.parser_tick_valid = false;
        management_context.parser_last_byte_tick = 0U;
    }
}

static void management_transport_process(void)
{
    uint8_t packet[MANAGEMENT_TRANSPORT_USB_PACKET_SIZE];
    management_task_events_t events;

    management_transport_take_events(&events, packet);
    management_transport_synchronize_session();

    if (events.receive_fault_log_pending)
    {
        debug_log_printf("Management CDC receive invariant failed\n");
    }

    management_transport_handle_completion(&events);
    if (management_transport_restart_if_ready())
    {
        return;
    }

    if (events.packet_pending && events.packet_length != 0U &&
        management_transport_session_is_current(events.packet_generation) &&
        management_context.task_generation == events.packet_generation)
    {
        management_transport_feed_packet(&events, packet);
    }

    management_transport_synchronize_session();
    management_transport_enable_receive_from_task();
    management_transport_try_send();
    if (management_transport_restart_if_ready())
    {
        return;
    }
}

static void management_transport_task(void *argument)
{
    configASSERT(argument == NULL);

    for (;;)
    {
        management_transport_process();
        ulTaskNotifyTake(pdTRUE, management_transport_wait_ticks());
    }
}

management_transport_result_t management_transport_init(void)
{
    management_frame_result_t parser_result;

    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_INACTIVE ||
        management_context.lifecycle == MANAGEMENT_LIFECYCLE_ACTIVE)
    {
        return MANAGEMENT_TRANSPORT_OK;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_FAILED)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    parser_result = management_frame_parser_init(&management_context.parser, management_received_frame,
                                                 sizeof(management_received_frame));
    if (parser_result != MANAGEMENT_FRAME_OK)
    {
        management_context.lifecycle = MANAGEMENT_LIFECYCLE_FAILED;
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    management_context.task_handle = xTaskCreateStatic(
        management_transport_task, "mgmt", MANAGEMENT_TRANSPORT_TASK_STACK_DEPTH, NULL,
        MANAGEMENT_TRANSPORT_TASK_PRIORITY, management_task_stack, &management_task_control);
    if (management_context.task_handle == NULL)
    {
        management_context.lifecycle = MANAGEMENT_LIFECYCLE_FAILED;
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    management_context.lifecycle = MANAGEMENT_LIFECYCLE_INACTIVE;
    return MANAGEMENT_TRANSPORT_OK;
}

management_transport_result_t management_transport_activate(void)
{
    management_transport_result_t result;

    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_UNINITIALIZED)
    {
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_FAILED)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    taskENTER_CRITICAL();
    management_context.lifecycle = MANAGEMENT_LIFECYCLE_ACTIVE;
    taskEXIT_CRITICAL();

    management_transport_synchronize_session();
    result = management_transport_enable_receive_from_task();
    if (result == MANAGEMENT_TRANSPORT_OK)
    {
        xTaskNotifyGive(management_context.task_handle);
    }
    return result;
}

management_transport_result_t management_transport_session_open_from_isr(void)
{
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_UNINITIALIZED)
    {
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_FAILED)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    management_context.observed_session.generation++;
    management_context.observed_session.open = true;
    management_context.receive_mailbox.enabled = true;
    management_context.receive_mailbox.occupied = false;
    management_context.receive_mailbox.faulted = false;
    management_context.receive_mailbox.fault_log_pending = false;
    management_context.receive_mailbox.length = 0U;
    management_context.receive_mailbox.arrival_tick = 0U;
    management_context.send_observation.token.active = false;
    management_context.send_observation.completion_pending = false;
    management_transport_notify_task_from_isr();

    return MANAGEMENT_TRANSPORT_OK;
}

management_transport_result_t management_transport_session_close_from_isr(void)
{
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_UNINITIALIZED)
    {
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_FAILED)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    management_context.observed_session.open = false;
    management_context.receive_mailbox.enabled = false;
    management_context.receive_mailbox.occupied = false;
    management_context.receive_mailbox.faulted = false;
    management_context.receive_mailbox.fault_log_pending = false;
    management_context.receive_mailbox.length = 0U;
    management_context.receive_mailbox.arrival_tick = 0U;
    management_context.send_observation.token.active = false;
    management_context.send_observation.completion_pending = false;
    management_transport_notify_task_from_isr();

    return MANAGEMENT_TRANSPORT_OK;
}

management_transport_result_t management_transport_receive_from_isr(const uint8_t *data, uint32_t length)
{
    if (data == NULL && length != 0U)
    {
        return MANAGEMENT_TRANSPORT_INVALID_ARGUMENT;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_UNINITIALIZED ||
        !management_context.observed_session.open)
    {
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_FAILED)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }
    if (management_context.lifecycle != MANAGEMENT_LIFECYCLE_ACTIVE)
    {
        management_context.receive_mailbox.enabled = false;
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }

    if (!management_context.receive_mailbox.enabled || management_context.receive_mailbox.occupied ||
        length > MANAGEMENT_TRANSPORT_USB_PACKET_SIZE)
    {
        management_context.receive_mailbox.enabled = false;
        management_context.receive_mailbox.faulted = true;
        management_context.receive_mailbox.fault_log_pending = true;
        management_transport_notify_task_from_isr();
        return MANAGEMENT_TRANSPORT_FAILED;
    }

    management_context.receive_mailbox.enabled = false;
    if (length != 0U)
    {
        memcpy(management_rx_packet, data, length);
    }
    management_context.receive_mailbox.length = (uint16_t)length;
    management_context.receive_mailbox.generation = management_context.observed_session.generation;
    management_context.receive_mailbox.arrival_tick = xTaskGetTickCountFromISR();
    management_context.receive_mailbox.occupied = true;
    management_transport_notify_task_from_isr();

    return MANAGEMENT_TRANSPORT_OK;
}

management_transport_result_t management_transport_transmit_complete_from_isr(const uint8_t *data, uint32_t length,
                                                                              uint8_t endpoint)
{
    if (data == NULL || length == 0U || endpoint == 0U)
    {
        return MANAGEMENT_TRANSPORT_INVALID_ARGUMENT;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_UNINITIALIZED ||
        !management_context.observed_session.open)
    {
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }
    if (management_context.lifecycle == MANAGEMENT_LIFECYCLE_FAILED)
    {
        return MANAGEMENT_TRANSPORT_FAILED;
    }
    if (management_context.lifecycle != MANAGEMENT_LIFECYCLE_ACTIVE)
    {
        return MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
    }

    if (management_context.send_observation.token.active &&
        management_context.send_observation.token.generation == management_context.observed_session.generation &&
        data == management_context.send_observation.token.data &&
        length == management_context.send_observation.token.length &&
        endpoint == management_context.send_observation.token.endpoint)
    {
        management_context.send_observation.token.active = false;
        management_context.send_observation.completion_generation =
            management_context.send_observation.token.generation;
        management_context.send_observation.completion_pending = true;
        management_transport_notify_task_from_isr();
    }

    return MANAGEMENT_TRANSPORT_OK;
}

#if defined(MANAGEMENT_TRANSPORT_TEST)
void management_transport_test_reset(void)
{
    memset(management_task_stack, 0, sizeof(management_task_stack));
    memset(&management_task_control, 0, sizeof(management_task_control));
    memset(management_rx_packet, 0, sizeof(management_rx_packet));
    memset(management_received_frame, 0, sizeof(management_received_frame));
    memset(management_tx_buffer, 0, sizeof(management_tx_buffer));
    memset(&management_context, 0, sizeof(management_context));
}

void management_transport_test_process(void)
{
    management_transport_process();
}
#endif
