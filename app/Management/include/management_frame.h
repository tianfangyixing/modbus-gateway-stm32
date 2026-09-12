#ifndef MANAGEMENT_FRAME_H
#define MANAGEMENT_FRAME_H

#include <stdint.h>

#define MANAGEMENT_FRAME_MAGIC_LENGTH UINT32_C(4)
#define MANAGEMENT_FRAME_HEADER_LENGTH UINT32_C(13)
#define MANAGEMENT_FRAME_CRC_LENGTH UINT32_C(4)
#define MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH UINT32_C(8192)
#define MANAGEMENT_FRAME_MAX_LENGTH \
    (MANAGEMENT_FRAME_HEADER_LENGTH + MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH + MANAGEMENT_FRAME_CRC_LENGTH)

typedef enum
{
    MANAGEMENT_FRAME_OK = 0,
    MANAGEMENT_FRAME_INVALID_ARGUMENT,
    MANAGEMENT_FRAME_PAYLOAD_TOO_LONG,
    MANAGEMENT_FRAME_BUFFER_TOO_SMALL
} management_frame_result_t;

typedef struct
{
    uint8_t message_type;
    uint32_t transaction_id;
    const uint8_t *payload;
    uint32_t payload_length;
} management_frame_view_t;

typedef void (*management_frame_handler_t)(const management_frame_view_t *frame, void *context);

typedef struct
{
    uint8_t *storage;
    uint32_t capacity;
    uint32_t length;
} management_frame_parser_t;

/**
 * @brief Calculate reflected CRC-32/ISO-HDLC for a byte sequence.
 * @param data Byte sequence. May be NULL only when length is zero.
 * @param length Number of bytes.
 * @return Calculated CRC32 value.
 */
uint32_t management_frame_crc32(const uint8_t *data, uint32_t length);

/** @brief Read a little-endian 16-bit integer from at least two readable bytes. */
uint16_t management_frame_read_u16_le(const uint8_t *bytes);

/** @brief Read a little-endian 32-bit integer from at least four readable bytes. */
uint32_t management_frame_read_u32_le(const uint8_t *bytes);

/** @brief Write a little-endian 16-bit integer to at least two writable bytes. */
void management_frame_write_u16_le(uint8_t *bytes, uint16_t value);

/** @brief Write a little-endian 32-bit integer to at least four writable bytes. */
void management_frame_write_u32_le(uint8_t *bytes, uint32_t value);

/**
 * @brief Encode one Management Frame.
 * @note payload may overlap frame; encoding uses overlap-safe movement.
 */
management_frame_result_t management_frame_encode(uint8_t message_type, uint32_t transaction_id,
                                                  const uint8_t *payload, uint32_t payload_length, uint8_t *frame,
                                                  uint32_t frame_capacity, uint32_t *frame_length);

/** @brief Initialize a streaming parser with storage for at least MANAGEMENT_FRAME_MAX_LENGTH bytes. */
management_frame_result_t management_frame_parser_init(management_frame_parser_t *parser, uint8_t *storage,
                                                       uint32_t storage_capacity);

/** @brief Discard all incomplete parser state. */
void management_frame_parser_reset(management_frame_parser_t *parser);

/**
 * @brief Feed arbitrary byte-stream fragments to the parser.
 * @note A frame view is valid only for the duration of its handler call.
 */
management_frame_result_t management_frame_parser_feed(management_frame_parser_t *parser, const uint8_t *data,
                                                       uint32_t length, management_frame_handler_t handler,
                                                       void *context);

#endif
