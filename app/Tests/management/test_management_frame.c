#include "management_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do \
{ \
    if (!(condition)) \
    { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static const uint8_t status_request[] =
{
    0x4D, 0x42, 0x47, 0x57, 0x03, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, 0x51, 0xE4, 0xC0, 0x0E
};
static uint8_t storage[8494];
static uint8_t wire[17020];
static uint8_t payload[8478];
static management_frame_parser_t parser;
static uint32_t callbacks;
static uint32_t observed_ids[8];
static uint32_t expected_payload_length;

static void capture(const management_frame_view_t *frame, void *context)
{
    CHECK(context == &callbacks);
    CHECK(callbacks < 8U);
    CHECK(frame->message_type == 3U);
    CHECK(frame->payload_length == expected_payload_length);
    if (frame->payload_length != 0U)
    {
        CHECK(memcmp(frame->payload, payload, frame->payload_length) == 0);
    }
    observed_ids[callbacks++] = frame->transaction_id;
}

static void begin(void)
{
    callbacks = 0U;
    CHECK(management_frame_parser_init(&parser, storage, sizeof(storage)) == MANAGEMENT_FRAME_OK);
}

static void feed(const uint8_t *data, uint32_t length)
{
    CHECK(management_frame_parser_feed(&parser, data, length, capture, &callbacks) == MANAGEMENT_FRAME_OK);
}

static void fixed_vector(void)
{
    uint32_t length = 0U;
    CHECK(MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH == 8477U);
    CHECK(MANAGEMENT_FRAME_MAX_LENGTH == 8494U);
    CHECK(management_frame_crc32((const uint8_t *)"123456789", 9U) == UINT32_C(0xCBF43926));
    CHECK(management_frame_crc32(status_request, 13U) == UINT32_C(0x0EC0E451));
    CHECK(management_frame_encode(3U, UINT32_C(0x12345678), NULL, 0U, wire, sizeof(wire), &length) ==
          MANAGEMENT_FRAME_OK);
    CHECK(length == sizeof(status_request));
    CHECK(memcmp(wire, status_request, length) == 0);
    expected_payload_length = 0U;
    begin();
    for (uint32_t i = 0U; i < sizeof(status_request); i++)
    {
        feed(&status_request[i], 1U);
    }
    CHECK(callbacks == 1U && observed_ids[0] == UINT32_C(0x12345678));
}

static void boundaries_and_fragments(void)
{
    const uint32_t chunks[] =
    {
        1U, 3U, 7U, 13U, 64U, 127U, 2048U, 8494U
    };
    uint32_t length = 0U;
    memset(payload, 0xA5, sizeof(payload));
    CHECK(management_frame_encode(3U, 9U, payload, 8478U, wire, sizeof(wire), &length) ==
          MANAGEMENT_FRAME_PAYLOAD_TOO_LONG);
    CHECK(management_frame_encode(3U, 9U, payload, 8477U, wire, 8493U, &length) ==
          MANAGEMENT_FRAME_BUFFER_TOO_SMALL);
    CHECK(management_frame_parser_init(&parser, storage, 8493U) == MANAGEMENT_FRAME_BUFFER_TOO_SMALL);
    wire[8494] = 0xCE;
    CHECK(management_frame_encode(3U, 9U, payload, 8477U, wire, 8494U, &length) == MANAGEMENT_FRAME_OK);
    CHECK(length == 8494U && wire[8494] == 0xCE);
    CHECK(management_frame_read_u32_le(&wire[9]) == 8477U);
    expected_payload_length = 8477U;
    for (uint32_t c = 0U; c < sizeof(chunks) / sizeof(chunks[0]); c++)
    {
        begin();
        for (uint32_t offset = 0U; offset < length;)
        {
            uint32_t count = length - offset;
            if (count > chunks[c])
            {
                count = chunks[c];
            }
            feed(&wire[offset], count);
            offset += count;
        }
        CHECK(callbacks == 1U && observed_ids[0] == 9U);
    }
    memcpy(wire, payload, 8477U);
    CHECK(management_frame_encode(3U, 9U, wire, 8477U, wire, sizeof(wire), &length) == MANAGEMENT_FRAME_OK);
    CHECK(memcmp(&wire[13], payload, 8477U) == 0);
}

static void recover_and_reset(void)
{
    expected_payload_length = 0U;
    begin();
    memset(wire, 0xA5, 70U);
    memcpy(&wire[70], status_request, sizeof(status_request));
    feed(wire, 71U);
    feed(&wire[71], 86U - 71U);
    CHECK(callbacks == 0U);
    feed(&wire[86], 1U);
    CHECK(callbacks == 1U);

    begin();
    memcpy(wire, status_request, sizeof(status_request));
    wire[16] ^= 1U;
    memcpy(&wire[17], status_request, sizeof(status_request));
    feed(wire, 17U);
    CHECK(callbacks == 0U);
    feed(&wire[17], 17U);
    CHECK(callbacks == 1U);

    for (uint32_t declared = 8478U;; declared = UINT32_MAX)
    {
        begin();
        memcpy(wire, status_request, 13U);
        management_frame_write_u32_le(&wire[9], declared);
        memcpy(&wire[13], status_request, sizeof(status_request));
        feed(wire, 13U);
        CHECK(callbacks == 0U);
        feed(&wire[13], 17U);
        CHECK(callbacks == 1U);
        if (declared == UINT32_MAX)
        {
            break;
        }
    }

    begin();
    memcpy(wire, status_request, 17U);
    memcpy(&wire[17], status_request, 17U);
    feed(wire, 34U);
    CHECK(callbacks == 2U);

    begin();
    feed(status_request, 12U);
    management_frame_parser_reset(&parser);
    feed(&status_request[12], 5U);
    CHECK(callbacks == 0U);
    feed(status_request, 17U);
    CHECK(callbacks == 1U);
}

int main(void)
{
    fixed_vector();
    boundaries_and_fragments();
    recover_and_reset();
    puts("Management production frame tests passed");
    return EXIT_SUCCESS;
}
