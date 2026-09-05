#include "management_frame.h"
#include "management_transport.h"
#include "management_transport_test_adapter.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

#define MESSAGE_ERROR_RESPONSE UINT8_C(0xFF)
#define MESSAGE_GET_ACTIVE_CONFIGURATION UINT8_C(0x01)
#define MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE UINT8_C(0x81)
#define MESSAGE_PUT_CONFIGURATION UINT8_C(0x02)
#define MESSAGE_PUT_CONFIGURATION_RESPONSE UINT8_C(0x82)
#define MESSAGE_GET_STATUS UINT8_C(0x03)
#define MESSAGE_GET_STATUS_RESPONSE UINT8_C(0x83)
#define MESSAGE_RESTART UINT8_C(0x04)
#define MESSAGE_RESTART_RESPONSE UINT8_C(0x84)

typedef struct
{
    uint8_t message_type;
    uint32_t transaction_id;
    const uint8_t *payload;
    uint32_t payload_length;
} response_view_t;

static uint8_t request_buffer[MANAGEMENT_FRAME_MAX_LENGTH];

void setUp(void)
{
    memset(request_buffer, 0, sizeof(request_buffer));
    management_transport_test_adapter_reset();
    management_transport_test_reset();
}

void tearDown(void)
{
    management_transport_test_stop_task();
    TEST_ASSERT_FALSE(management_transport_test_state.assert_failed);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.critical_depth);
}

static uint32_t encode_request(uint8_t message_type, uint32_t transaction_id, const uint8_t *payload,
                               uint32_t payload_length)
{
    uint32_t frame_length = 0U;

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_encode(message_type, transaction_id, payload, payload_length,
                                                  request_buffer, sizeof(request_buffer), &frame_length));
    return frame_length;
}

static void initialize_active_session(void)
{
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_TRANSPORT_OK, management_transport_init());
    management_transport_session_open_from_isr();
    management_transport_test_process();
}

static void deliver_bytes(const uint8_t *bytes, uint32_t length, TickType_t arrival_tick)
{
    uint32_t offset = 0U;

    management_transport_test_state.tick = arrival_tick;
    while (offset < length)
    {
        uint32_t chunk_length = length - offset;

        if (chunk_length > 64U)
        {
            chunk_length = 64U;
        }
        management_transport_receive_from_isr(&bytes[offset], chunk_length);
        management_transport_test_process();
        offset += chunk_length;
    }
}

static response_view_t current_response(void)
{
    const uint8_t *frame = management_transport_test_state.last_send_copy;
    uint16_t frame_length = management_transport_test_state.last_send_length;
    response_view_t response;
    uint32_t expected_crc;

    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(MANAGEMENT_FRAME_HEADER_LENGTH + MANAGEMENT_FRAME_CRC_LENGTH, frame_length);
    TEST_ASSERT_EQUAL_MEMORY("MBGW", frame, 4U);
    response.message_type = frame[4];
    response.transaction_id = management_frame_read_u32_le(&frame[5]);
    response.payload_length = management_frame_read_u32_le(&frame[9]);
    TEST_ASSERT_EQUAL_UINT32(MANAGEMENT_FRAME_HEADER_LENGTH + response.payload_length + MANAGEMENT_FRAME_CRC_LENGTH,
                             frame_length);
    expected_crc = management_frame_read_u32_le(&frame[MANAGEMENT_FRAME_HEADER_LENGTH + response.payload_length]);
    TEST_ASSERT_EQUAL_HEX32(expected_crc,
                           management_frame_crc32(frame, MANAGEMENT_FRAME_HEADER_LENGTH + response.payload_length));
    response.payload = &frame[MANAGEMENT_FRAME_HEADER_LENGTH];
    return response;
}

static void assert_response(uint8_t message_type, uint32_t transaction_id, uint16_t result_code,
                            uint32_t response_data_length)
{
    response_view_t response = current_response();

    TEST_ASSERT_EQUAL_HEX8(message_type, response.message_type);
    TEST_ASSERT_EQUAL_UINT32(transaction_id, response.transaction_id);
    TEST_ASSERT_EQUAL_UINT32(2U + response_data_length, response.payload_length);
    TEST_ASSERT_EQUAL_UINT16(result_code, management_frame_read_u16_le(response.payload));
}

static void complete_current_response(void)
{
    TEST_ASSERT_NOT_NULL(management_transport_test_state.last_send_data);
    management_transport_transmit_complete_from_isr(management_transport_test_state.last_send_data,
                                                   management_transport_test_state.last_send_length);
    management_transport_test_process();
}

static void test_lifecycle_reports_initialization_and_task_creation_failures(void)
{
    management_transport_test_state.task_creation_fails = true;
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_TRANSPORT_FAILED, management_transport_init());
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.task_create_count);
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_TRANSPORT_FAILED, management_transport_init());
}

static void test_initialization_is_idempotent(void)
{
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_TRANSPORT_OK, management_transport_init());
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_TRANSPORT_OK, management_transport_init());
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.task_create_count);
}

static void test_initial_cdc_arm_accepts_packet_before_task_runs(void)
{
    uint32_t request_length = encode_request(MESSAGE_GET_STATUS, 1U, NULL, 0U);

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_TRANSPORT_OK, management_transport_init());
    management_transport_session_open_from_isr();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.cdc_enable_count);
    management_transport_receive_from_isr(request_buffer, request_length);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.cdc_send_count);
    management_transport_test_process();
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 1U, 0U, 15U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);
    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_enable_count);
}

static void test_get_active_configuration_returns_payload_and_not_ready(void)
{
    uint32_t request_length;
    response_view_t response;

    initialize_active_session();
    management_transport_test_state.encoded_configuration[0] = 0x01U;
    management_transport_test_state.encoded_configuration[1] = 0xA5U;
    management_transport_test_state.encoded_configuration[2] = 0x5AU;
    management_transport_test_state.encoded_configuration_length = 3U;

    request_length = encode_request(MESSAGE_GET_ACTIVE_CONFIGURATION, 10U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 10U);
    assert_response(MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, 10U, 0U, 3U);
    response = current_response();
    TEST_ASSERT_EQUAL_MEMORY(management_transport_test_state.encoded_configuration, &response.payload[2], 3U);
    complete_current_response();

    management_transport_test_state.configuration_ready = false;
    request_length = encode_request(MESSAGE_GET_ACTIVE_CONFIGURATION, 11U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 11U);
    assert_response(MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, 11U, 6U, 0U);
}

static void test_get_active_configuration_maps_internal_encoding_failures(void)
{
    uint32_t request_length;

    initialize_active_session();
    management_transport_test_state.active_configuration_available = false;
    request_length = encode_request(MESSAGE_GET_ACTIVE_CONFIGURATION, 20U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 20U);
    assert_response(MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, 20U, 7U, 0U);
    complete_current_response();

    management_transport_test_state.active_configuration_available = true;
    management_transport_test_state.encode_result = CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    request_length = encode_request(MESSAGE_GET_ACTIVE_CONFIGURATION, 21U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 21U);
    assert_response(MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, 21U, 7U, 0U);
}

static void test_put_configuration_maps_service_results_and_preserves_payload(void)
{
    static const configuration_service_result_t service_results[] =
    {
        CONFIGURATION_SERVICE_OK,
        CONFIGURATION_SERVICE_INVALID_ARGUMENT,
        CONFIGURATION_SERVICE_INVALID_PAYLOAD,
        CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE,
        CONFIGURATION_SERVICE_IO_ERROR,
        CONFIGURATION_SERVICE_NOT_INITIALIZED,
        (configuration_service_result_t)99
    };
    static const uint16_t protocol_results[] = {0U, 3U, 3U, 4U, 5U, 6U, 7U};
    static const uint8_t configuration_payload[] = {0x01U, 0x10U, 0x20U, 0x30U};
    uint32_t index;

    initialize_active_session();
    for (index = 0U; index < sizeof(service_results) / sizeof(service_results[0]); index++)
    {
        uint32_t transaction_id = 100U + index;
        uint32_t request_length;

        management_transport_test_state.write_result = service_results[index];
        request_length = encode_request(MESSAGE_PUT_CONFIGURATION, transaction_id, configuration_payload,
                                        sizeof(configuration_payload));
        deliver_bytes(request_buffer, request_length, transaction_id);
        assert_response(MESSAGE_PUT_CONFIGURATION_RESPONSE, transaction_id, protocol_results[index], 0U);
        TEST_ASSERT_EQUAL_UINT32(index + 1U, management_transport_test_state.write_count);
        TEST_ASSERT_EQUAL_UINT32(sizeof(configuration_payload),
                                 management_transport_test_state.write_payload_length);
        TEST_ASSERT_EQUAL_MEMORY(configuration_payload, management_transport_test_state.write_payload,
                                 sizeof(configuration_payload));
        complete_current_response();
    }
}

static void test_configuration_unavailable_does_not_block_status_or_restart(void)
{
    static const uint8_t configuration_payload[] = {0x01U};
    uint32_t request_length;

    initialize_active_session();
    management_transport_test_state.configuration_ready = false;

    request_length = encode_request(MESSAGE_GET_ACTIVE_CONFIGURATION, 200U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 1U);
    assert_response(MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, 200U, 6U, 0U);
    complete_current_response();

    request_length = encode_request(MESSAGE_PUT_CONFIGURATION, 201U, configuration_payload,
                                    sizeof(configuration_payload));
    deliver_bytes(request_buffer, request_length, 2U);
    assert_response(MESSAGE_PUT_CONFIGURATION_RESPONSE, 201U, 6U, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
    complete_current_response();

    request_length = encode_request(MESSAGE_GET_STATUS, 202U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 3U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 202U, 0U, 15U);
    complete_current_response();

    request_length = encode_request(MESSAGE_RESTART, 203U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 4U);
    assert_response(MESSAGE_RESTART_RESPONSE, 203U, 0U, 0U);
    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.reset_count);
}

static void test_get_status_encodes_network_time_and_mqtt_state(void)
{
    uint32_t request_length;
    response_view_t response;

    TEST_ASSERT_EQUAL_UINT8(0U, MQTT_PUBLISHER_STATE_DISABLED);
    TEST_ASSERT_EQUAL_UINT8(1U, MQTT_PUBLISHER_STATE_DISCONNECTED);
    TEST_ASSERT_EQUAL_UINT8(2U, MQTT_PUBLISHER_STATE_CONNECTING);
    TEST_ASSERT_EQUAL_UINT8(3U, MQTT_PUBLISHER_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_UINT8(4U, MQTT_PUBLISHER_STATE_ERROR);

    initialize_active_session();
    management_transport_test_set_network(true, true, 192U, 168U, 1U, 50U);
    management_transport_test_state.sntp_synchronized = true;
    management_transport_test_state.sntp_time_available = true;
    management_transport_test_state.unix_seconds = UINT32_C(0x12345678);
    management_transport_test_state.microseconds = UINT32_C(0x000ABCDE);
    management_transport_test_state.mqtt_state = MQTT_PUBLISHER_STATE_CONNECTED;

    request_length = encode_request(MESSAGE_GET_STATUS, 30U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 30U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 30U, 0U, 15U);
    response = current_response();
    TEST_ASSERT_EQUAL_UINT8(1U, response.payload[2]);
    TEST_ASSERT_EQUAL_UINT8(192U, response.payload[3]);
    TEST_ASSERT_EQUAL_UINT8(168U, response.payload[4]);
    TEST_ASSERT_EQUAL_UINT8(1U, response.payload[5]);
    TEST_ASSERT_EQUAL_UINT8(50U, response.payload[6]);
    TEST_ASSERT_EQUAL_UINT8(1U, response.payload[7]);
    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0x12345678), management_frame_read_u32_le(&response.payload[8]));
    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0x000ABCDE), management_frame_read_u32_le(&response.payload[12]));
    TEST_ASSERT_EQUAL_UINT8(MQTT_PUBLISHER_STATE_CONNECTED, response.payload[16]);
}

static void test_known_invalid_payload_and_unknown_message_return_protocol_errors(void)
{
    static const uint8_t extra = 0x55U;
    uint32_t request_length;

    initialize_active_session();
    request_length = encode_request(MESSAGE_GET_STATUS, 40U, &extra, 1U);
    deliver_bytes(request_buffer, request_length, 40U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 40U, 1U, 0U);
    complete_current_response();

    request_length = encode_request(0x70U, 41U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 41U);
    assert_response(MESSAGE_ERROR_RESPONSE, 41U, 2U, 0U);
}

static void test_transactions_drop_in_flight_requests(void)
{
    uint32_t request_length;

    initialize_active_session();
    request_length = encode_request(MESSAGE_GET_STATUS, 50U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 50U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);

    request_length = encode_request(MESSAGE_GET_STATUS, 51U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 52U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);

    complete_current_response();
    request_length = encode_request(MESSAGE_GET_STATUS, 51U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 54U);
    TEST_ASSERT_EQUAL_UINT32(2U, management_transport_test_state.cdc_send_count);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 51U, 0U, 15U);
}

static void test_restart_occurs_only_after_matching_transmit_completion(void)
{
    uint16_t response_length;
    uint32_t request_length;

    initialize_active_session();
    request_length = encode_request(MESSAGE_RESTART, 60U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 60U);
    assert_response(MESSAGE_RESTART_RESPONSE, 60U, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
    response_length = management_transport_test_state.last_send_length;

    management_transport_transmit_complete_from_isr(request_buffer, response_length);
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);

    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.reset_count);
}

static void test_session_close_cancels_pending_restart(void)
{
    uint8_t *send_data;
    uint16_t send_length;
    uint32_t request_length;

    initialize_active_session();
    request_length = encode_request(MESSAGE_RESTART, 61U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 61U);
    send_data = management_transport_test_state.last_send_data;
    send_length = management_transport_test_state.last_send_length;

    management_transport_session_close_from_isr();
    management_transport_test_process();
    management_transport_transmit_complete_from_isr(send_data, send_length);
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
}

static void test_cdc_busy_retries_after_ten_milliseconds(void)
{
    uint32_t request_length;

    initialize_active_session();
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_BUSY;
    request_length = encode_request(MESSAGE_GET_STATUS, 80U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);
    TEST_ASSERT_NULL(management_transport_test_state.last_send_data);

    management_transport_test_state.tick = 9U;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);

    management_transport_test_state.tick = 10U;
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_OK;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(2U, management_transport_test_state.cdc_send_count);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 80U, 0U, 15U);
}

static void test_cdc_failure_logs_are_rate_limited(void)
{
    uint32_t request_length;

    initialize_active_session();
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_FAILED;
    request_length = encode_request(MESSAGE_GET_STATUS, 81U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.log_count);

    management_transport_test_state.tick = 10U;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.log_count);

    management_transport_test_state.tick = 1000U;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(2U, management_transport_test_state.log_count);
}

static void test_get_status_without_sntp_does_not_read_time(void)
{
    uint32_t request_length;
    response_view_t response;

    initialize_active_session();
    management_transport_test_state.unix_seconds = UINT32_MAX;
    management_transport_test_state.microseconds = 999999U;
    request_length = encode_request(MESSAGE_GET_STATUS, 300U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 1U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 300U, 0U, 15U);
    response = current_response();
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &response.payload[2], 14U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.time_read_count);
}

static void test_get_status_time_read_failure_returns_zero_time(void)
{
    uint32_t request_length;
    response_view_t response;

    initialize_active_session();
    management_transport_test_set_network(true, false, 10U, 0U, 0U, 2U);
    management_transport_test_state.sntp_synchronized = true;
    management_transport_test_state.sntp_time_available = false;
    management_transport_test_state.unix_seconds = UINT32_MAX;
    management_transport_test_state.microseconds = 999999U;
    request_length = encode_request(MESSAGE_GET_STATUS, 301U, NULL, 0U);
    deliver_bytes(request_buffer, request_length, 1U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 301U, 0U, 15U);
    response = current_response();
    TEST_ASSERT_EQUAL_UINT8(0U, response.payload[2]);
    TEST_ASSERT_EQUAL_UINT8(10U, response.payload[3]);
    TEST_ASSERT_EQUAL_UINT8(2U, response.payload[6]);
    TEST_ASSERT_EQUAL_UINT8(1U, response.payload[7]);
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &response.payload[8], 8U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.time_read_count);
}

static void test_empty_payload_commands_reject_extra_bytes(void)
{
    static const uint8_t types[] =
    {
        MESSAGE_GET_ACTIVE_CONFIGURATION, MESSAGE_GET_STATUS, MESSAGE_RESTART
    };
    static const uint8_t extra = 0xA5U;

    initialize_active_session();
    for (uint32_t index = 0U; index < sizeof(types); index++)
    {
        uint32_t length = encode_request(types[index], index, &extra, 1U);

        deliver_bytes(request_buffer, length, index);
        assert_response((uint8_t)(types[index] | 0x80U), index, 1U, 0U);
        complete_current_response();
        TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
        TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
    }
}

static void test_response_message_types_are_rejected_as_requests(void)
{
    static const uint8_t types[] =
    {
        MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, MESSAGE_PUT_CONFIGURATION_RESPONSE,
        MESSAGE_GET_STATUS_RESPONSE, MESSAGE_RESTART_RESPONSE, MESSAGE_ERROR_RESPONSE
    };

    initialize_active_session();
    for (uint32_t index = 0U; index < sizeof(types); index++)
    {
        uint32_t length = encode_request(types[index], index, NULL, 0U);

        deliver_bytes(request_buffer, length, index);
        assert_response(MESSAGE_ERROR_RESPONSE, index, 2U, 0U);
        complete_current_response();
    }
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
}

static void test_maximum_put_payload_crosses_usb_packets_without_early_write(void)
{
    static uint8_t payload[MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH];
    uint32_t length;

    for (uint32_t index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t)(index ^ 0xA5U);
    }
    initialize_active_session();
    length = encode_request(MESSAGE_PUT_CONFIGURATION, UINT32_MAX, payload, sizeof(payload));
    deliver_bytes(request_buffer, length - 1U, 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.cdc_send_count);
    deliver_bytes(&request_buffer[length - 1U], 1U, 2U);
    assert_response(MESSAGE_PUT_CONFIGURATION_RESPONSE, UINT32_MAX, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.write_count);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), management_transport_test_state.write_payload_length);
    TEST_ASSERT_EQUAL_MEMORY(payload, management_transport_test_state.write_payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
}

static void test_maximum_active_configuration_response_preserves_all_bytes(void)
{
    uint32_t length;
    response_view_t response;

    initialize_active_session();
    management_transport_test_state.encoded_configuration_length = CONFIGURATION_V1_MAX_PAYLOAD_LENGTH;
    for (uint32_t index = 0U; index < CONFIGURATION_V1_MAX_PAYLOAD_LENGTH; index++)
    {
        management_transport_test_state.encoded_configuration[index] = (uint8_t)(index ^ 0x5AU);
    }
    length = encode_request(MESSAGE_GET_ACTIVE_CONFIGURATION, 0U, NULL, 0U);
    deliver_bytes(request_buffer, length, 1U);
    assert_response(MESSAGE_GET_ACTIVE_CONFIGURATION_RESPONSE, 0U, 0U, CONFIGURATION_V1_MAX_PAYLOAD_LENGTH);
    response = current_response();
    TEST_ASSERT_EQUAL_MEMORY(management_transport_test_state.encoded_configuration, &response.payload[2],
                             CONFIGURATION_V1_MAX_PAYLOAD_LENGTH);
}

static void test_noise_and_bad_crc_do_not_execute_commands_and_parser_recovers(void)
{
    static const uint8_t noise[] =
    {
        0x11U, 'M', 'B', 0x00U, 0x22U
    };
    static const uint8_t payload = 0x01U;
    uint32_t length;

    initialize_active_session();
    deliver_bytes(noise, sizeof(noise), 1U);
    length = encode_request(MESSAGE_PUT_CONFIGURATION, 302U, &payload, 1U);
    request_buffer[length - 1U] ^= 0x80U;
    deliver_bytes(request_buffer, length, 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.cdc_send_count);
    request_buffer[length - 1U] ^= 0x80U;
    deliver_bytes(request_buffer, length, 3U);
    assert_response(MESSAGE_PUT_CONFIGURATION_RESPONSE, 302U, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.write_count);
}

static void test_session_reopen_discards_partial_frame(void)
{
    uint32_t length;

    initialize_active_session();
    length = encode_request(MESSAGE_GET_STATUS, 303U, NULL, 0U);
    deliver_bytes(request_buffer, 8U, 1U);
    management_transport_session_close_from_isr();
    management_transport_session_open_from_isr();
    management_transport_test_process();
    deliver_bytes(&request_buffer[8], length - 8U, 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.cdc_send_count);
    deliver_bytes(request_buffer, length, 3U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 303U, 0U, 15U);
}

static void test_session_reopen_discards_response_waiting_for_retry(void)
{
    uint32_t length;

    initialize_active_session();
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_BUSY;
    length = encode_request(MESSAGE_GET_STATUS, 304U, NULL, 0U);
    deliver_bytes(request_buffer, length, 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);
    TEST_ASSERT_NULL(management_transport_test_state.last_send_data);
    management_transport_session_close_from_isr();
    management_transport_session_open_from_isr();
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_OK;
    management_transport_test_process();
    management_transport_test_state.tick = 1000U;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);
    length = encode_request(MESSAGE_GET_STATUS, 305U, NULL, 0U);
    deliver_bytes(request_buffer, length, 1001U);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 305U, 0U, 15U);
    TEST_ASSERT_EQUAL_UINT32(2U, management_transport_test_state.cdc_send_count);
}

static void test_coalesced_requests_drop_second_transaction_without_writing(void)
{
    uint8_t packet[64];
    static const uint8_t payload = 0x01U;
    uint32_t first_length;
    uint32_t second_length;

    initialize_active_session();
    first_length = encode_request(MESSAGE_GET_STATUS, 306U, NULL, 0U);
    memcpy(packet, request_buffer, first_length);
    second_length = encode_request(MESSAGE_PUT_CONFIGURATION, 307U, &payload, 1U);
    memcpy(&packet[first_length], request_buffer, second_length);
    deliver_bytes(packet, first_length + second_length, 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_send_count);
    assert_response(MESSAGE_GET_STATUS_RESPONSE, 306U, 0U, 15U);
    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.write_count);
}

static void test_restart_waits_for_completion_even_when_time_advances(void)
{
    uint32_t length;

    initialize_active_session();
    length = encode_request(MESSAGE_RESTART, 308U, NULL, 0U);
    deliver_bytes(request_buffer, length, 1U);
    assert_response(MESSAGE_RESTART_RESPONSE, 308U, 0U, 0U);
    management_transport_test_state.tick = 10001U;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.delay_count);
    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.reset_count);
}

static void assert_restart_submission_failure_does_not_reset(management_transport_cdc_result_t result)
{
    uint32_t length;

    initialize_active_session();
    management_transport_test_state.cdc_send_result = result;
    length = encode_request(MESSAGE_RESTART, 309U, NULL, 0U);
    deliver_bytes(request_buffer, length, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
    TEST_ASSERT_NULL(management_transport_test_state.last_send_data);
    management_transport_test_state.tick = 100U;
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
    TEST_ASSERT_NULL(management_transport_test_state.last_send_data);
    management_transport_test_state.cdc_send_result = MANAGEMENT_TRANSPORT_CDC_OK;
    management_transport_test_state.tick = 200U;
    management_transport_test_process();
    assert_response(MESSAGE_RESTART_RESPONSE, 309U, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.reset_count);
}

static void test_restart_busy_retry_does_not_reset_before_completion(void)
{
    assert_restart_submission_failure_does_not_reset(MANAGEMENT_TRANSPORT_CDC_BUSY);
}

static void test_restart_failed_retry_does_not_reset_before_completion(void)
{
    assert_restart_submission_failure_does_not_reset(MANAGEMENT_TRANSPORT_CDC_FAILED);
}

static void test_session_close_during_restart_delay_cancels_reset(void)
{
    uint32_t length;

    initialize_active_session();
    length = encode_request(MESSAGE_RESTART, 310U, NULL, 0U);
    deliver_bytes(request_buffer, length, 1U);
    management_transport_test_state.delay_hook = management_transport_session_close_from_isr;
    complete_current_response();
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.reset_count);
}

static void test_zero_length_packet_rearms_receive_without_response(void)
{
    initialize_active_session();
    management_transport_receive_from_isr(NULL, 0U);
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(1U, management_transport_test_state.cdc_enable_count);
    TEST_ASSERT_EQUAL_UINT32(0U, management_transport_test_state.cdc_send_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lifecycle_reports_initialization_and_task_creation_failures);
    RUN_TEST(test_initialization_is_idempotent);
    RUN_TEST(test_initial_cdc_arm_accepts_packet_before_task_runs);
    RUN_TEST(test_get_active_configuration_returns_payload_and_not_ready);
    RUN_TEST(test_get_active_configuration_maps_internal_encoding_failures);
    RUN_TEST(test_put_configuration_maps_service_results_and_preserves_payload);
    RUN_TEST(test_configuration_unavailable_does_not_block_status_or_restart);
    RUN_TEST(test_get_status_encodes_network_time_and_mqtt_state);
    RUN_TEST(test_known_invalid_payload_and_unknown_message_return_protocol_errors);
    RUN_TEST(test_transactions_drop_in_flight_requests);
    RUN_TEST(test_restart_occurs_only_after_matching_transmit_completion);
    RUN_TEST(test_session_close_cancels_pending_restart);
    RUN_TEST(test_cdc_busy_retries_after_ten_milliseconds);
    RUN_TEST(test_cdc_failure_logs_are_rate_limited);
    RUN_TEST(test_get_status_without_sntp_does_not_read_time);
    RUN_TEST(test_get_status_time_read_failure_returns_zero_time);
    RUN_TEST(test_empty_payload_commands_reject_extra_bytes);
    RUN_TEST(test_response_message_types_are_rejected_as_requests);
    RUN_TEST(test_maximum_put_payload_crosses_usb_packets_without_early_write);
    RUN_TEST(test_maximum_active_configuration_response_preserves_all_bytes);
    RUN_TEST(test_noise_and_bad_crc_do_not_execute_commands_and_parser_recovers);
    RUN_TEST(test_session_reopen_discards_partial_frame);
    RUN_TEST(test_session_reopen_discards_response_waiting_for_retry);
    RUN_TEST(test_coalesced_requests_drop_second_transaction_without_writing);
    RUN_TEST(test_restart_waits_for_completion_even_when_time_advances);
    RUN_TEST(test_restart_busy_retry_does_not_reset_before_completion);
    RUN_TEST(test_restart_failed_retry_does_not_reset_before_completion);
    RUN_TEST(test_session_close_during_restart_delay_cancels_reset);
    RUN_TEST(test_zero_length_packet_rearms_receive_without_response);
    return UNITY_END();
}
