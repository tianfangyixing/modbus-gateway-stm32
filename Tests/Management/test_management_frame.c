#include "management_frame.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

typedef struct
{
    uint32_t count;
    uint8_t message_types[4];
    uint32_t transaction_ids[4];
    uint32_t payload_lengths[4];
    uint8_t first_payload_byte[4];
    uint8_t last_payload_byte[4];
} frame_capture_t;

static uint8_t frame_buffer[MANAGEMENT_FRAME_MAX_LENGTH * 2U + 32U];
static uint8_t parser_storage[MANAGEMENT_FRAME_MAX_LENGTH];
static uint8_t max_payload[MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH];
static frame_capture_t capture;

void setUp(void)
{
    memset(frame_buffer, 0, sizeof(frame_buffer));
    memset(parser_storage, 0, sizeof(parser_storage));
    memset(max_payload, 0, sizeof(max_payload));
    memset(&capture, 0, sizeof(capture));
}

void tearDown(void)
{
}

static void capture_frame(const management_frame_view_t *frame, void *context)
{
    frame_capture_t *frame_capture = context;
    uint32_t index = frame_capture->count;

    TEST_ASSERT_LESS_THAN_UINT32(4U, index);
    frame_capture->message_types[index] = frame->message_type;
    frame_capture->transaction_ids[index] = frame->transaction_id;
    frame_capture->payload_lengths[index] = frame->payload_length;
    if (frame->payload_length != 0U)
    {
        frame_capture->first_payload_byte[index] = frame->payload[0];
        frame_capture->last_payload_byte[index] = frame->payload[frame->payload_length - 1U];
    }
    frame_capture->count++;
}

static uint32_t encode_frame_at(uint32_t offset, uint8_t message_type, uint32_t transaction_id,
                                const uint8_t *payload, uint32_t payload_length)
{
    uint32_t frame_length = 0U;

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_encode(message_type, transaction_id, payload, payload_length,
                                                  &frame_buffer[offset], sizeof(frame_buffer) - offset,
                                                  &frame_length));
    return frame_length;
}

static void test_crc32_matches_standard_vector(void)
{
    static const uint8_t vector[] = "123456789";

    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0xCBF43926), management_frame_crc32(vector, sizeof(vector) - 1U));
}

static void test_encode_matches_get_status_example(void)
{
    static const uint8_t expected[] =
    {
        0x4DU, 0x42U, 0x47U, 0x57U, 0x03U, 0x78U, 0x56U, 0x34U, 0x12U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x51U, 0xE4U, 0xC0U, 0x0EU
    };
    uint32_t frame_length = encode_frame_at(0U, 0x03U, UINT32_C(0x12345678), NULL, 0U);

    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), frame_length);
    TEST_ASSERT_EQUAL_MEMORY(expected, frame_buffer, sizeof(expected));
}

static void test_encode_and_parser_reject_invalid_arguments_and_capacity(void)
{
    management_frame_parser_t parser;
    uint8_t value = 0U;
    uint32_t frame_length = 0U;

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_encode(1U, 1U, NULL, 1U, frame_buffer, sizeof(frame_buffer),
                                                  &frame_length));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_encode(1U, 1U, &value, 1U, NULL, sizeof(frame_buffer), &frame_length));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_encode(1U, 1U, &value, 1U, frame_buffer, sizeof(frame_buffer), NULL));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_PAYLOAD_TOO_LONG,
                          management_frame_encode(1U, 1U, &value, MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH + 1U,
                                                  frame_buffer, sizeof(frame_buffer), &frame_length));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_BUFFER_TOO_SMALL,
                          management_frame_encode(1U, 1U, NULL, 0U, frame_buffer,
                                                  MANAGEMENT_FRAME_HEADER_LENGTH + MANAGEMENT_FRAME_CRC_LENGTH - 1U,
                                                  &frame_length));

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_parser_init(NULL, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_parser_init(&parser, NULL, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_BUFFER_TOO_SMALL,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage) - 1U));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_parser_feed(&parser, NULL, 1U, capture_frame, &capture));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_INVALID_ARGUMENT,
                          management_frame_parser_feed(&parser, frame_buffer, 1U, NULL, &capture));
}

static void test_encode_supports_overlapping_payload(void)
{
    static const uint8_t payload[] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U};
    uint32_t frame_length = 0U;

    memcpy(&frame_buffer[MANAGEMENT_FRAME_HEADER_LENGTH], payload, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_encode(0x22U, 7U, &frame_buffer[MANAGEMENT_FRAME_HEADER_LENGTH],
                                                  sizeof(payload), frame_buffer, sizeof(frame_buffer), &frame_length));
    TEST_ASSERT_EQUAL_UINT32(MANAGEMENT_FRAME_HEADER_LENGTH + sizeof(payload) + MANAGEMENT_FRAME_CRC_LENGTH,
                             frame_length);
    TEST_ASSERT_EQUAL_MEMORY(payload, &frame_buffer[MANAGEMENT_FRAME_HEADER_LENGTH], sizeof(payload));

    memcpy(frame_buffer, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_encode(0x23U, 8U, frame_buffer, sizeof(payload), frame_buffer,
                                                  sizeof(frame_buffer), &frame_length));
    TEST_ASSERT_EQUAL_MEMORY(payload, &frame_buffer[MANAGEMENT_FRAME_HEADER_LENGTH], sizeof(payload));
}

static void test_parser_accepts_a_frame_one_byte_at_a_time(void)
{
    static const uint8_t payload[] = {0xA1U, 0xB2U, 0xC3U};
    management_frame_parser_t parser;
    uint32_t frame_length = encode_frame_at(0U, 0x42U, 99U, payload, sizeof(payload));
    uint32_t index;

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    for (index = 0U; index < frame_length; index++)
    {
        TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                              management_frame_parser_feed(&parser, &frame_buffer[index], 1U, capture_frame, &capture));
    }

    TEST_ASSERT_EQUAL_UINT32(1U, capture.count);
    TEST_ASSERT_EQUAL_HEX8(0x42U, capture.message_types[0]);
    TEST_ASSERT_EQUAL_UINT32(99U, capture.transaction_ids[0]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), capture.payload_lengths[0]);
    TEST_ASSERT_EQUAL_HEX8(payload[0], capture.first_payload_byte[0]);
    TEST_ASSERT_EQUAL_HEX8(payload[sizeof(payload) - 1U], capture.last_payload_byte[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, parser.length);
}

static void test_parser_skips_noise_and_emits_consecutive_frames(void)
{
    static const uint8_t first_payload[] = {0x11U};
    static const uint8_t second_payload[] = {0x22U, 0x33U};
    management_frame_parser_t parser;
    uint32_t first_length;
    uint32_t second_length;
    uint32_t stream_length;

    frame_buffer[0] = 0x00U;
    frame_buffer[1] = 0x4DU;
    frame_buffer[2] = 0x00U;
    first_length = encode_frame_at(3U, 0x10U, 1U, first_payload, sizeof(first_payload));
    second_length = encode_frame_at(3U + first_length, 0x20U, 2U, second_payload, sizeof(second_payload));
    stream_length = 3U + first_length + second_length;

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, frame_buffer, 2U, capture_frame, &capture));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, &frame_buffer[2], stream_length - 2U,
                                                       capture_frame, &capture));

    TEST_ASSERT_EQUAL_UINT32(2U, capture.count);
    TEST_ASSERT_EQUAL_HEX8(0x10U, capture.message_types[0]);
    TEST_ASSERT_EQUAL_HEX8(0x20U, capture.message_types[1]);
    TEST_ASSERT_EQUAL_UINT32(1U, capture.transaction_ids[0]);
    TEST_ASSERT_EQUAL_UINT32(2U, capture.transaction_ids[1]);
}

static void test_parser_recovers_after_bad_crc(void)
{
    management_frame_parser_t parser;
    uint32_t bad_length = encode_frame_at(0U, 0x31U, 3U, NULL, 0U);
    uint32_t good_length = encode_frame_at(bad_length, 0x32U, 4U, NULL, 0U);

    frame_buffer[bad_length - 1U] ^= 0x80U;
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, frame_buffer, bad_length + good_length,
                                                       capture_frame, &capture));

    TEST_ASSERT_EQUAL_UINT32(1U, capture.count);
    TEST_ASSERT_EQUAL_HEX8(0x32U, capture.message_types[0]);
    TEST_ASSERT_EQUAL_UINT32(4U, capture.transaction_ids[0]);
}

static void test_parser_recovers_after_oversized_length_declaration(void)
{
    management_frame_parser_t parser;
    uint32_t good_length;

    memcpy(frame_buffer, "MBGW", 4U);
    frame_buffer[4] = 0x44U;
    management_frame_write_u32_le(&frame_buffer[5], 7U);
    management_frame_write_u32_le(&frame_buffer[9], MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH + 1U);
    good_length = encode_frame_at(MANAGEMENT_FRAME_HEADER_LENGTH, 0x45U, 8U, NULL, 0U);

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, frame_buffer,
                                                       MANAGEMENT_FRAME_HEADER_LENGTH + good_length,
                                                       capture_frame, &capture));

    TEST_ASSERT_EQUAL_UINT32(1U, capture.count);
    TEST_ASSERT_EQUAL_HEX8(0x45U, capture.message_types[0]);
}

static void test_parser_accepts_maximum_payload(void)
{
    management_frame_parser_t parser;
    uint32_t index;
    uint32_t frame_length;

    for (index = 0U; index < sizeof(max_payload); index++)
    {
        max_payload[index] = (uint8_t)index;
    }
    frame_length = encode_frame_at(0U, 0x55U, UINT32_MAX, max_payload, sizeof(max_payload));

    TEST_ASSERT_EQUAL_UINT32(MANAGEMENT_FRAME_MAX_LENGTH, frame_length);
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, frame_buffer, frame_length, capture_frame, &capture));
    TEST_ASSERT_EQUAL_UINT32(1U, capture.count);
    TEST_ASSERT_EQUAL_UINT32(MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH, capture.payload_lengths[0]);
    TEST_ASSERT_EQUAL_HEX8(max_payload[0], capture.first_payload_byte[0]);
    TEST_ASSERT_EQUAL_HEX8(max_payload[sizeof(max_payload) - 1U], capture.last_payload_byte[0]);
}

static void test_parser_reset_discards_partial_frame(void)
{
    management_frame_parser_t parser;
    uint32_t frame_length = encode_frame_at(0U, 0x60U, 1U, NULL, 0U);

    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_init(&parser, parser_storage, sizeof(parser_storage)));
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, frame_buffer, 8U, capture_frame, &capture));
    TEST_ASSERT_NOT_EQUAL(0U, parser.length);
    management_frame_parser_reset(&parser);
    TEST_ASSERT_EQUAL_UINT32(0U, parser.length);
    TEST_ASSERT_EQUAL_INT(MANAGEMENT_FRAME_OK,
                          management_frame_parser_feed(&parser, &frame_buffer[8], frame_length - 8U,
                                                       capture_frame, &capture));
    TEST_ASSERT_EQUAL_UINT32(0U, capture.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_matches_standard_vector);
    RUN_TEST(test_encode_matches_get_status_example);
    RUN_TEST(test_encode_and_parser_reject_invalid_arguments_and_capacity);
    RUN_TEST(test_encode_supports_overlapping_payload);
    RUN_TEST(test_parser_accepts_a_frame_one_byte_at_a_time);
    RUN_TEST(test_parser_skips_noise_and_emits_consecutive_frames);
    RUN_TEST(test_parser_recovers_after_bad_crc);
    RUN_TEST(test_parser_recovers_after_oversized_length_declaration);
    RUN_TEST(test_parser_accepts_maximum_payload);
    RUN_TEST(test_parser_reset_discards_partial_frame);
    return UNITY_END();
}
