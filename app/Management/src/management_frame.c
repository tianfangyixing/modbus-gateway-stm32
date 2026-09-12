#include "management_frame.h"

#include <stddef.h>
#include <string.h>

static const uint8_t management_frame_magic[MANAGEMENT_FRAME_MAGIC_LENGTH] = {'M', 'B', 'G', 'W'};

void management_frame_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

void management_frame_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

uint16_t management_frame_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

uint32_t management_frame_read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint32_t management_frame_find_magic(const uint8_t *bytes, uint32_t length)
{
    uint32_t index;

    if (length < MANAGEMENT_FRAME_MAGIC_LENGTH)
    {
        return length;
    }

    for (index = 0U; index <= length - MANAGEMENT_FRAME_MAGIC_LENGTH; index++)
    {
        if (memcmp(&bytes[index], management_frame_magic, MANAGEMENT_FRAME_MAGIC_LENGTH) == 0)
        {
            return index;
        }
    }

    return length;
}

static uint32_t management_frame_magic_suffix_length(const uint8_t *bytes, uint32_t length)
{
    uint32_t candidate_length = MANAGEMENT_FRAME_MAGIC_LENGTH - 1U;

    if (candidate_length > length)
    {
        candidate_length = length;
    }

    while (candidate_length != 0U)
    {
        if (memcmp(&bytes[length - candidate_length], management_frame_magic, candidate_length) == 0)
        {
            return candidate_length;
        }
        candidate_length--;
    }

    return 0U;
}

static void management_frame_parser_process(management_frame_parser_t *parser, management_frame_handler_t handler,
                                            void *context)
{
    for (;;)
    {
        management_frame_view_t frame;
        uint32_t expected_crc;
        uint32_t expected_length;
        uint32_t magic_index;
        uint32_t payload_length;
        uint32_t suffix_length;

        magic_index = management_frame_find_magic(parser->storage, parser->length);
        if (magic_index == parser->length)
        {
            suffix_length = management_frame_magic_suffix_length(parser->storage, parser->length);
            if (suffix_length != 0U)
            {
                memmove(parser->storage, &parser->storage[parser->length - suffix_length], suffix_length);
            }
            parser->length = suffix_length;
            return;
        }

        if (magic_index != 0U)
        {
            parser->length -= magic_index;
            memmove(parser->storage, &parser->storage[magic_index], parser->length);
        }

        if (parser->length < MANAGEMENT_FRAME_HEADER_LENGTH)
        {
            return;
        }

        payload_length = management_frame_read_u32_le(&parser->storage[9]);
        if (payload_length > MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH)
        {
            parser->length--;
            memmove(parser->storage, &parser->storage[1], parser->length);
            continue;
        }

        expected_length = MANAGEMENT_FRAME_HEADER_LENGTH + payload_length + MANAGEMENT_FRAME_CRC_LENGTH;
        if (parser->length < expected_length)
        {
            return;
        }

        expected_crc = management_frame_read_u32_le(&parser->storage[MANAGEMENT_FRAME_HEADER_LENGTH + payload_length]);
        if (management_frame_crc32(parser->storage, MANAGEMENT_FRAME_HEADER_LENGTH + payload_length) == expected_crc)
        {
            frame.message_type = parser->storage[4];
            frame.transaction_id = management_frame_read_u32_le(&parser->storage[5]);
            frame.payload = &parser->storage[MANAGEMENT_FRAME_HEADER_LENGTH];
            frame.payload_length = payload_length;
            handler(&frame, context);

            parser->length -= expected_length;
            if (parser->length != 0U)
            {
                memmove(parser->storage, &parser->storage[expected_length], parser->length);
            }
        }
        else
        {
            parser->length--;
            memmove(parser->storage, &parser->storage[1], parser->length);
        }

        if (parser->length == 0U)
        {
            return;
        }
    }
}

uint32_t management_frame_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t byte_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        uint8_t bit_index;

        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & UINT32_C(1)) != 0U)
            {
                crc = (crc >> 1U) ^ UINT32_C(0xEDB88320);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}

management_frame_result_t management_frame_encode(uint8_t message_type, uint32_t transaction_id,
                                                  const uint8_t *payload, uint32_t payload_length, uint8_t *frame,
                                                  uint32_t frame_capacity, uint32_t *frame_length)
{
    uint32_t crc;
    uint32_t required_length;

    if (frame == NULL || frame_length == NULL || (payload == NULL && payload_length != 0U))
    {
        return MANAGEMENT_FRAME_INVALID_ARGUMENT;
    }
    if (payload_length > MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH)
    {
        return MANAGEMENT_FRAME_PAYLOAD_TOO_LONG;
    }

    required_length = MANAGEMENT_FRAME_HEADER_LENGTH + payload_length + MANAGEMENT_FRAME_CRC_LENGTH;
    if (frame_capacity < required_length)
    {
        return MANAGEMENT_FRAME_BUFFER_TOO_SMALL;
    }

    if (payload_length != 0U)
    {
        memmove(&frame[MANAGEMENT_FRAME_HEADER_LENGTH], payload, payload_length);
    }
    memcpy(frame, management_frame_magic, sizeof(management_frame_magic));
    frame[4] = message_type;
    management_frame_write_u32_le(&frame[5], transaction_id);
    management_frame_write_u32_le(&frame[9], payload_length);

    crc = management_frame_crc32(frame, MANAGEMENT_FRAME_HEADER_LENGTH + payload_length);
    management_frame_write_u32_le(&frame[MANAGEMENT_FRAME_HEADER_LENGTH + payload_length], crc);
    *frame_length = required_length;
    return MANAGEMENT_FRAME_OK;
}

management_frame_result_t management_frame_parser_init(management_frame_parser_t *parser, uint8_t *storage,
                                                       uint32_t storage_capacity)
{
    if (parser == NULL || storage == NULL)
    {
        return MANAGEMENT_FRAME_INVALID_ARGUMENT;
    }
    if (storage_capacity < MANAGEMENT_FRAME_MAX_LENGTH)
    {
        return MANAGEMENT_FRAME_BUFFER_TOO_SMALL;
    }

    parser->storage = storage;
    parser->capacity = storage_capacity;
    parser->length = 0U;
    return MANAGEMENT_FRAME_OK;
}

void management_frame_parser_reset(management_frame_parser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    parser->length = 0U;
}

management_frame_result_t management_frame_parser_feed(management_frame_parser_t *parser, const uint8_t *data,
                                                       uint32_t length, management_frame_handler_t handler,
                                                       void *context)
{
    uint32_t index;

    if (parser == NULL || parser->storage == NULL || handler == NULL || (data == NULL && length != 0U))
    {
        return MANAGEMENT_FRAME_INVALID_ARGUMENT;
    }

    for (index = 0U; index < length; index++)
    {
        if (parser->length >= parser->capacity)
        {
            management_frame_parser_reset(parser);
            return MANAGEMENT_FRAME_BUFFER_TOO_SMALL;
        }

        parser->storage[parser->length] = data[index];
        parser->length++;
        management_frame_parser_process(parser, handler, context);
    }

    return MANAGEMENT_FRAME_OK;
}
