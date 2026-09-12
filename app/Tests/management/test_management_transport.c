#include "management_frame.h"
#include "management_transport.h"
#include "management_transport_test_adapter.h"
#include "configuration_service.h"
#include "configuration_test_fixtures.h"
#include "flash_simulator.h"
#include "lwip/netif.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

static uint8_t request[8494];
static uint8_t payload[8477];
static uint8_t glued[64];

void setUp(void)
{
    storage_flash_initialize();
    configuration_service_test_reset();
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    management_transport_test_reset();
    management_test_platform_reset();
    TEST_ASSERT_EQUAL(MANAGEMENT_TRANSPORT_OK, management_transport_init());
    management_transport_session_open_from_isr();
    management_transport_test_process();
    storage_flash_clear_log();
}

void tearDown(void)
{
}

static void deliver(const uint8_t *bytes, uint32_t length, uint32_t chunk)
{
    for (uint32_t offset = 0U; offset < length;)
    {
        uint32_t count = length - offset;
        if (count > chunk)
        {
            count = chunk;
        }
        management_transport_receive_from_isr(&bytes[offset], count);
        management_transport_test_process();
        offset += count;
    }
}

static uint32_t make_request(uint8_t type, uint32_t id, const uint8_t *data, uint32_t length)
{
    uint32_t wire_length = 0U;
    TEST_ASSERT_EQUAL(MANAGEMENT_FRAME_OK,
                      management_frame_encode(type, id, data, length, request, sizeof(request), &wire_length));
    return wire_length;
}

static void send_request(uint8_t type, uint32_t id, const uint8_t *data, uint32_t length)
{
    uint32_t wire_length = make_request(type, id, data, length);
    deliver(request, wire_length, 64U);
}

static void response(uint8_t type, uint32_t id, uint16_t result, uint32_t payload_length)
{
    const uint8_t *sent = management_test_platform.sent;
    TEST_ASSERT_EQUAL_UINT32(payload_length + 17U, management_test_platform.tx_length);
    TEST_ASSERT_EQUAL_MEMORY("MBGW", sent, 4U);
    TEST_ASSERT_EQUAL_UINT8(type, sent[4]);
    TEST_ASSERT_EQUAL_UINT32(id, management_frame_read_u32_le(&sent[5]));
    TEST_ASSERT_EQUAL_UINT32(payload_length, management_frame_read_u32_le(&sent[9]));
    TEST_ASSERT_EQUAL_UINT16(result, management_frame_read_u16_le(&sent[13]));
    TEST_ASSERT_EQUAL_HEX32(management_frame_crc32(sent, 13U + payload_length),
                           management_frame_read_u32_le(&sent[13U + payload_length]));
}

static void status_fixed_vector_and_time(void)
{
    static const uint8_t fixed[] =
    {
        0x4D, 0x42, 0x47, 0x57, 0x03, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, 0x51, 0xE4, 0xC0, 0x0E
    };
    deliver(fixed, sizeof(fixed), 1U);
    response(0x83U, UINT32_C(0x12345678), 0U, 17U);
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &management_test_platform.sent[15], 15U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.time_reads);
    management_test_complete_tx();

    management_test_platform.synchronized = true;
    send_request(3U, 10U, NULL, 0U);
    response(0x83U, 10U, 0U, 17U);
    TEST_ASSERT_EQUAL_UINT8(1U, management_test_platform.sent[20]);
    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0x12345678), management_frame_read_u32_le(&management_test_platform.sent[21]));
    TEST_ASSERT_EQUAL_UINT32(654321U, management_frame_read_u32_le(&management_test_platform.sent[25]));
    management_test_complete_tx();

    management_test_platform.time_available = false;
    send_request(3U, 11U, NULL, 0U);
    response(0x83U, 11U, 0U, 17U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_frame_read_u32_le(&management_test_platform.sent[21]));
    TEST_ASSERT_EQUAL_UINT32(0U, management_frame_read_u32_le(&management_test_platform.sent[25]));
}

static void maximum_status_and_oversize_recovery(void)
{
    memset(payload, 0xA5, sizeof(payload));
    send_request(3U, 12U, payload, 8477U);
    response(0x83U, 12U, 1U, 2U);
    management_test_complete_tx();

    uint32_t length = make_request(3U, 14U, NULL, 0U);
    memcpy(glued, request, 13U);
    management_frame_write_u32_le(&glued[5], 13U);
    management_frame_write_u32_le(&glued[9], 8478U);
    memcpy(&glued[13], request, length);
    deliver(glued, length + 13U, 1U);
    response(0x83U, 14U, 0U, 17U);
    TEST_ASSERT_EQUAL_UINT32(2U, management_test_platform.tx_calls);
}

static void noise_crc_session_and_receive_bounds(void)
{
    uint32_t length = make_request(3U, 15U, NULL, 0U);
    request[length - 1U] ^= 1U;
    deliver(request, length, 7U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.tx_calls);
    memset(payload, 0xA5, 130U);
    deliver(payload, 70U, 64U);
    management_transport_receive_from_isr(payload, 129U);
    management_transport_test_process();
    send_request(3U, 16U, NULL, 0U);
    response(0x83U, 16U, 0U, 17U);
    management_test_complete_tx();

    length = make_request(3U, 17U, NULL, 0U);
    deliver(request, 12U, 12U);
    management_transport_session_close_from_isr();
    management_transport_test_process();
    management_transport_session_open_from_isr();
    management_transport_test_process();
    deliver(&request[12], length - 12U, 5U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.tx_calls);
    send_request(3U, 18U, NULL, 0U);
    response(0x83U, 18U, 0U, 17U);
}

static void single_transaction_drops_glued_and_pending_requests(void)
{
    uint32_t length = make_request(3U, 20U, NULL, 0U);
    memcpy(glued, request, length);
    length = make_request(5U, 21U, NULL, 0U);
    memcpy(&glued[17], request, length);
    deliver(glued, 34U, 64U);
    response(0x83U, 20U, 0U, 17U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.boot_requests);
    send_request(5U, 22U, NULL, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.boot_requests);
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.tx_calls);
    management_test_complete_tx();
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.tx_calls);

    length = make_request(3U, 23U, NULL, 0U);
    memcpy(glued, request, length);
    length = make_request(5U, 24U, NULL, 0U);
    memcpy(&glued[17], request, 8U);
    deliver(glued, 25U, 64U);
    response(0x83U, 23U, 0U, 17U);
    management_test_complete_tx();
    deliver(&request[8], length - 8U, 64U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.boot_requests);
    TEST_ASSERT_EQUAL_UINT32(2U, management_test_platform.tx_calls);
    send_request(3U, 25U, NULL, 0U);
    response(0x83U, 25U, 0U, 17U);
}


static void reopened_session_accepts_packet_before_task_observes_open(void)
{
    send_request(3U, 26U, NULL, 0U);
    response(0x83U, 26U, 0U, 17U);
    management_transport_session_close_from_isr();
    management_transport_session_open_from_isr();
    uint32_t length = make_request(3U, 27U, NULL, 0U);
    management_transport_receive_from_isr(request, length);
    management_transport_test_process();
    response(0x83U, 27U, 0U, 17U);
    TEST_ASSERT_EQUAL_UINT32(2U, management_test_platform.tx_calls);
}

static void restart_waits_for_completion_and_retries(void)
{
    management_test_platform.send_result = MANAGEMENT_TRANSPORT_CDC_BUSY;
    send_request(4U, 30U, NULL, 0U);
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.resets);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.tx_calls);
    management_test_platform.send_result = MANAGEMENT_TRANSPORT_CDC_OK;
    management_transport_test_process();
    response(0x84U, 30U, 0U, 2U);
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.resets);
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.tx_calls);
    management_test_complete_tx();
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.resets);
    management_transport_test_process();
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.resets);
    send_request(4U, 34U, payload, 1U);
    response(0x84U, 34U, 1U, 2U);
    management_test_complete_tx();
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.resets);
}

static void restart_cancelled_by_session_close_or_delay_disconnect(void)
{
    send_request(4U, 31U, NULL, 0U);
    management_transport_session_close_from_isr();
    management_test_complete_tx();
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.resets);
    management_transport_session_open_from_isr();
    management_transport_test_process();
    send_request(3U, 32U, NULL, 0U);
    response(0x83U, 32U, 0U, 17U);
    management_test_complete_tx();
    send_request(4U, 33U, NULL, 0U);
    management_test_platform.close_during_reset_delay = true;
    management_test_complete_tx();
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.resets);
}

static void upgrade_unknown_and_not_ready(void)
{
    send_request(5U, 40U, NULL, 0U);
    response(0x85U, 40U, 0U, 2U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.boot_requests);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.resets);
    management_test_complete_tx();
    send_request(5U, 41U, payload, 1U);
    response(0x85U, 41U, 1U, 2U);
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.boot_requests);
    management_test_complete_tx();
    management_test_platform.boot_success = false;
    send_request(5U, 42U, NULL, 0U);
    response(0x85U, 42U, 7U, 2U);
    management_test_complete_tx();
    send_request(0x83U, 43U, NULL, 0U);
    response(0xFFU, 43U, 2U, 2U);
    management_test_complete_tx();
    management_test_platform.ready = false;
    send_request(1U, 46U, payload, 1U);
    response(0x81U, 46U, 1U, 2U);
    management_test_complete_tx();
    send_request(1U, 44U, NULL, 0U);
    response(0x81U, 44U, 6U, 2U);
    management_test_complete_tx();
    send_request(2U, 45U, payload, 8476U);
    response(0x82U, 45U, 6U, 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, storage_flash_event_count());
}

static void invalid_configuration_length_and_v1(void)
{
    memset(payload, 0U, sizeof(payload));
    payload[0] = 2U;
    send_request(2U, 50U, payload, 8476U);
    response(0x82U, 50U, 3U, 2U);
    management_test_complete_tx();
    send_request(2U, 51U, payload, 8477U);
    response(0x82U, 51U, 3U, 2U);
    management_test_complete_tx();
    memcpy(payload, configuration_test_default_payload, 48U);
    payload[0] = 1U;
    send_request(2U, 52U, payload, 48U);
    response(0x82U, 52U, 3U, 2U);
    management_test_complete_tx();
    send_request(1U, 53U, NULL, 0U);
    response(0x81U, 53U, 0U, 50U);
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_default_payload, &management_test_platform.sent[15], 48U);
    TEST_ASSERT_EQUAL_UINT32(0U, storage_flash_event_count());
}

static void maximum_put_stays_inactive_until_reboot_then_maximum_get(void)
{
    uint32_t length = make_request(2U, 60U, configuration_test_maximum_payload, 8475U);
    TEST_ASSERT_EQUAL_UINT32(8492U, length);
    deliver(request, length, 64U);
    response(0x82U, 60U, 0U, 2U);
    uint32_t erases = 0U;
    for (uint32_t i = 0U; i < storage_flash_event_count(); i++)
    {
        const storage_flash_event_t *event = storage_flash_event(i);
        if (event->operation == STORAGE_ERASE)
        {
            TEST_ASSERT_EQUAL_HEX32(STORAGE_SLOT_A + erases * 4096U, event->address);
            erases++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3U, erases);
    management_test_complete_tx();
    send_request(1U, 61U, NULL, 0U);
    response(0x81U, 61U, 0U, 50U);
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_default_payload, &management_test_platform.sent[15], 48U);
    management_test_complete_tx();
    send_request(4U, 62U, NULL, 0U);
    response(0x84U, 62U, 0U, 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, management_test_platform.resets);
    management_test_complete_tx();
    TEST_ASSERT_EQUAL_UINT32(1U, management_test_platform.resets);

    management_transport_session_close_from_isr();
    management_transport_test_process();
    configuration_service_test_reset();
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    management_transport_session_open_from_isr();
    management_transport_test_process();
    send_request(1U, 63U, NULL, 0U);
    response(0x81U, 63U, 0U, 8477U);
    TEST_ASSERT_EQUAL_UINT32(8494U, management_test_platform.tx_length);
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_maximum_payload, &management_test_platform.sent[15], 8475U);
    management_test_complete_tx();
    send_request(3U, 64U, NULL, 0U);
    response(0x83U, 64U, 0U, 17U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(status_fixed_vector_and_time);
    RUN_TEST(maximum_status_and_oversize_recovery);
    RUN_TEST(noise_crc_session_and_receive_bounds);
    RUN_TEST(single_transaction_drops_glued_and_pending_requests);
    RUN_TEST(reopened_session_accepts_packet_before_task_observes_open);
    RUN_TEST(restart_waits_for_completion_and_retries);
    RUN_TEST(restart_cancelled_by_session_close_or_delay_disconnect);
    RUN_TEST(upgrade_unknown_and_not_ready);
    RUN_TEST(invalid_configuration_length_and_v1);
    RUN_TEST(maximum_put_stays_inactive_until_reboot_then_maximum_get);
    return UNITY_END();
}
