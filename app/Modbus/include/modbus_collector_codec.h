#ifndef MODBUS_COLLECTOR_CODEC_H
#define MODBUS_COLLECTOR_CODEC_H

#include "configuration.h"
#include "modbus_rtu.h"

#include <stdint.h>

#define MODBUS_COLLECTOR_PAYLOAD_CAPACITY 7U

typedef enum
{
    MODBUS_COLLECTOR_CODEC_OK = 0,
    MODBUS_COLLECTOR_CODEC_INVALID_ARGUMENT,
    MODBUS_COLLECTOR_CODEC_SOURCE_INVALID,
    MODBUS_COLLECTOR_CODEC_DATA_TYPE_INVALID,
    MODBUS_COLLECTOR_CODEC_RTU_ERROR,
    MODBUS_COLLECTOR_CODEC_SLAVE_MISMATCH,
    MODBUS_COLLECTOR_CODEC_PAYLOAD_CAPACITY_INSUFFICIENT,
    MODBUS_COLLECTOR_CODEC_FORMAT_ERROR
} modbus_collector_codec_result_t;

/**
  * @brief Encode one quantity-1 read request for a validated collection point.
  * @note point is never modified. On failure, request is not modified.
  */
modbus_collector_codec_result_t modbus_collector_codec_encode_request(
    const configuration_collection_point_t *point, modbus_rtu_adu_t *request);

/**
  * @brief Decode a quantity-1 response and format its scalar MQTT payload.
  * @param payload_capacity Total writable bytes in payload, including the trailing NUL byte.
  * @param payload_length Receives the payload byte length, excluding the trailing NUL byte.
  * @note point and response are never modified. On failure, payload and payload_length are not modified.
  */
modbus_collector_codec_result_t modbus_collector_codec_decode_response(
    const configuration_collection_point_t *point, const modbus_rtu_adu_t *response, char *payload,
    uint16_t payload_capacity, uint16_t *payload_length);

#endif
