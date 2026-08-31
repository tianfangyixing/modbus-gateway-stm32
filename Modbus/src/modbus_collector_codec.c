#include "modbus_collector_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MODBUS_COLLECTOR_QUANTITY UINT16_C(1)
#define MODBUS_COLLECTOR_SLAVE_ADDRESS_MIN UINT8_C(1)
#define MODBUS_COLLECTOR_SLAVE_ADDRESS_MAX UINT8_C(247)

static bool point_has_valid_slave(const configuration_collection_point_t *point)
{
    return point->slave_address >= MODBUS_COLLECTOR_SLAVE_ADDRESS_MIN &&
           point->slave_address <= MODBUS_COLLECTOR_SLAVE_ADDRESS_MAX;
}

static modbus_collector_codec_result_t encode_request_for_source(const configuration_collection_point_t *point,
                                                                 modbus_rtu_adu_t *request)
{
    modbus_rtu_result_t result;

    switch (point->source)
    {
    case CONFIGURATION_COLLECTION_SOURCE_COIL:
        result = modbus_rtu_read_coils_encode_request(request, point->slave_address, point->address,
                                                      MODBUS_COLLECTOR_QUANTITY);
        break;
    case CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT:
        result = modbus_rtu_read_discrete_inputs_encode_request(request, point->slave_address, point->address,
                                                                MODBUS_COLLECTOR_QUANTITY);
        break;
    case CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER:
        result = modbus_rtu_read_holding_registers_encode_request(request, point->slave_address, point->address,
                                                                  MODBUS_COLLECTOR_QUANTITY);
        break;
    case CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER:
        result = modbus_rtu_read_input_registers_encode_request(request, point->slave_address, point->address,
                                                                MODBUS_COLLECTOR_QUANTITY);
        break;
    default:
        return MODBUS_COLLECTOR_CODEC_SOURCE_INVALID;
    }

    if (result != MODBUS_RTU_OK)
    {
        return MODBUS_COLLECTOR_CODEC_RTU_ERROR;
    }
    return MODBUS_COLLECTOR_CODEC_OK;
}

static modbus_collector_codec_result_t format_register_value(uint16_t value, uint8_t data_type, char *output,
                                                             size_t output_capacity, uint16_t *output_length)
{
    int length;

    if (data_type == CONFIGURATION_DATA_TYPE_UINT16)
    {
        length = snprintf(output, output_capacity, "%u", (unsigned int)value);
    }
    else
    {
        length = snprintf(output, output_capacity, "%d", (int)(int16_t)value);
    }

    if (length < 0)
    {
        return MODBUS_COLLECTOR_CODEC_FORMAT_ERROR;
    }
    if ((size_t)length >= output_capacity)
    {
        return MODBUS_COLLECTOR_CODEC_PAYLOAD_CAPACITY_INSUFFICIENT;
    }

    *output_length = (uint16_t)length;
    return MODBUS_COLLECTOR_CODEC_OK;
}

static modbus_collector_codec_result_t decode_bit(const configuration_collection_point_t *point,
                                                  const modbus_rtu_adu_t *response, bool *value)
{
    uint8_t slave_address;
    modbus_rtu_result_t result;

    if (point->source == CONFIGURATION_COLLECTION_SOURCE_COIL)
    {
        result = modbus_rtu_read_coils_decode_response(response, &slave_address, MODBUS_COLLECTOR_QUANTITY, value);
    }
    else
    {
        result = modbus_rtu_read_discrete_inputs_decode_response(response, &slave_address,
                                                                 MODBUS_COLLECTOR_QUANTITY, value);
    }

    if (result != MODBUS_RTU_OK)
    {
        return MODBUS_COLLECTOR_CODEC_RTU_ERROR;
    }
    if (slave_address != point->slave_address)
    {
        return MODBUS_COLLECTOR_CODEC_SLAVE_MISMATCH;
    }
    return MODBUS_COLLECTOR_CODEC_OK;
}

static modbus_collector_codec_result_t decode_register(const configuration_collection_point_t *point,
                                                       const modbus_rtu_adu_t *response, uint16_t *value)
{
    uint8_t slave_address;
    modbus_rtu_result_t result;

    if (point->data_type != CONFIGURATION_DATA_TYPE_UINT16 && point->data_type != CONFIGURATION_DATA_TYPE_INT16)
    {
        return MODBUS_COLLECTOR_CODEC_DATA_TYPE_INVALID;
    }

    if (point->source == CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER)
    {
        result = modbus_rtu_read_holding_registers_decode_response(response, &slave_address,
                                                                   MODBUS_COLLECTOR_QUANTITY, value);
    }
    else
    {
        result = modbus_rtu_read_input_registers_decode_response(response, &slave_address,
                                                                 MODBUS_COLLECTOR_QUANTITY, value);
    }

    if (result != MODBUS_RTU_OK)
    {
        return MODBUS_COLLECTOR_CODEC_RTU_ERROR;
    }
    if (slave_address != point->slave_address)
    {
        return MODBUS_COLLECTOR_CODEC_SLAVE_MISMATCH;
    }
    return MODBUS_COLLECTOR_CODEC_OK;
}

modbus_collector_codec_result_t modbus_collector_codec_encode_request(
    const configuration_collection_point_t *point, modbus_rtu_adu_t *request)
{
    if (point == NULL || request == NULL)
    {
        return MODBUS_COLLECTOR_CODEC_INVALID_ARGUMENT;
    }
    if (!point_has_valid_slave(point))
    {
        return MODBUS_COLLECTOR_CODEC_INVALID_ARGUMENT;
    }
    return encode_request_for_source(point, request);
}

modbus_collector_codec_result_t modbus_collector_codec_decode_response(
    const configuration_collection_point_t *point, const modbus_rtu_adu_t *response, char *payload,
    uint16_t payload_capacity, uint16_t *payload_length)
{
    char formatted[MODBUS_COLLECTOR_PAYLOAD_CAPACITY];
    modbus_collector_codec_result_t result;
    uint16_t formatted_length;

    if (point == NULL || response == NULL || payload == NULL || payload_length == NULL || payload_capacity == 0U)
    {
        return MODBUS_COLLECTOR_CODEC_INVALID_ARGUMENT;
    }
    if (!point_has_valid_slave(point))
    {
        return MODBUS_COLLECTOR_CODEC_INVALID_ARGUMENT;
    }

    if (point->source == CONFIGURATION_COLLECTION_SOURCE_COIL ||
        point->source == CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT)
    {
        bool value;

        result = decode_bit(point, response, &value);
        if (result != MODBUS_COLLECTOR_CODEC_OK)
        {
            return result;
        }
        formatted[0] = value ? '1' : '0';
        formatted_length = UINT16_C(1);
    }
    else if (point->source == CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER ||
             point->source == CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER)
    {
        uint16_t value;

        result = decode_register(point, response, &value);
        if (result != MODBUS_COLLECTOR_CODEC_OK)
        {
            return result;
        }
        result = format_register_value(value, point->data_type, formatted, sizeof(formatted), &formatted_length);
        if (result != MODBUS_COLLECTOR_CODEC_OK)
        {
            return result;
        }
    }
    else
    {
        return MODBUS_COLLECTOR_CODEC_SOURCE_INVALID;
    }

    if (payload_capacity <= formatted_length)
    {
        return MODBUS_COLLECTOR_CODEC_PAYLOAD_CAPACITY_INSUFFICIENT;
    }

    memcpy(payload, formatted, formatted_length);
    payload[formatted_length] = '\0';
    *payload_length = formatted_length;
    return MODBUS_COLLECTOR_CODEC_OK;
}
