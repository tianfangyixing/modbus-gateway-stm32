#include "modbus_rtu.h"
#include "modbus_common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODBUS_FC_READ_COILS UINT8_C(0x01)
#define MODBUS_FC_READ_DISCRETE_INPUTS UINT8_C(0x02)
#define MODBUS_FC_READ_HOLDING_REGISTERS UINT8_C(0x03)
#define MODBUS_FC_READ_INPUT_REGISTERS UINT8_C(0x04)
#define MODBUS_FC_WRITE_SINGLE_COIL UINT8_C(0x05)
#define MODBUS_FC_WRITE_SINGLE_REGISTER UINT8_C(0x06)
#define MODBUS_FC_WRITE_MULTIPLE_COILS UINT8_C(0x0F)
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS UINT8_C(0x10)

#define MODBUS_RTU_ADDRESS_QUANTITY_REQUEST_LENGTH UINT16_C(8)
#define MODBUS_RTU_WRITE_SINGLE_REQUEST_LENGTH UINT16_C(8)
#define MODBUS_RTU_WRITE_MULTIPLE_REQUEST_OVERHEAD_LENGTH UINT16_C(9)
#define MODBUS_RTU_WRITE_RESPONSE_LENGTH UINT16_C(8)
#define MODBUS_RTU_EXCEPTION_RESPONSE_LENGTH UINT16_C(5)
#define MODBUS_RTU_BYTE_RESPONSE_OVERHEAD_LENGTH UINT16_C(5)

#define MODBUS_RTU_MIN_QUANTITY UINT16_C(1)
#define MODBUS_RTU_WRITE_MULTIPLE_COILS_MAX_QUANTITY UINT16_C(1968)
#define MODBUS_RTU_WRITE_MULTIPLE_REGISTERS_MAX_QUANTITY UINT16_C(123)
#define MODBUS_RTU_SLAVE_ADDRESS_MIN UINT8_C(1)
#define MODBUS_RTU_SLAVE_ADDRESS_MAX UINT8_C(247)

#define MODBUS_RTU_WRITE_SINGLE_COIL_VALUE_OFF UINT16_C(0x0000)
#define MODBUS_RTU_WRITE_SINGLE_COIL_VALUE_ON UINT16_C(0xFF00)

#define MODBUS_RTU_BITS_PER_CHARACTER UINT64_C(11)
#define MODBUS_RTU_SCHEDULING_MARGIN_MS UINT32_C(100)


#define MODBUS_RTU_SLAVE_ADDRESS_INDEX 0U
#define MODBUS_RTU_FUNCTION_INDEX 1U
#define MODBUS_RTU_START_ADDRESS_INDEX 2U
#define MODBUS_RTU_QUANTITY_INDEX 4U
#define MODBUS_RTU_READ_BYTE_COUNT_INDEX 2U
#define MODBUS_RTU_READ_VALUES_INDEX 3U
#define MODBUS_RTU_WRITE_BYTE_COUNT_INDEX 6U
#define MODBUS_RTU_WRITE_VALUES_INDEX 7U
#define MODBUS_RTU_EXCEPTION_CODE_INDEX 2U


static inline bool slave_address_is_routable(uint8_t slave_address)
{
    return (slave_address >= MODBUS_RTU_SLAVE_ADDRESS_MIN) && (slave_address <= MODBUS_RTU_SLAVE_ADDRESS_MAX);
}

static inline bool is_legal_request_function(uint8_t function)
{
    return (function >= UINT8_C(0x01)) && (function <= UINT8_C(0x7F));
}

static bool is_legal_exception_code(uint8_t exception_code)
{
    switch (exception_code)
    {
    case UINT8_C(0x01):
    case UINT8_C(0x02):
    case UINT8_C(0x03):
    case UINT8_C(0x04):
    case UINT8_C(0x05):
    case UINT8_C(0x06):
    case UINT8_C(0x08):
    case UINT8_C(0x0A):
    case UINT8_C(0x0B):
        return true;
    default:
        return false;
    }
}

static inline bool is_legal_write_single_coil_value(uint16_t value)
{
    return (value == MODBUS_RTU_WRITE_SINGLE_COIL_VALUE_OFF) || (value == MODBUS_RTU_WRITE_SINGLE_COIL_VALUE_ON);
}

static bool padding_bits_are_zero(const uint8_t *data, uint16_t quantity)
{
    uint16_t used_bits = (uint16_t)(quantity % UINT16_C(8));
    uint16_t last_byte_index;
    uint8_t padding_mask;

    if (used_bits == 0U)
    {
        return true;
    }
    last_byte_index = (uint16_t)((quantity - UINT16_C(1)) / UINT16_C(8));
    padding_mask = (uint8_t)(UINT8_C(0xFF) << used_bits);
    return (data[last_byte_index] & padding_mask) == 0U;
}



static bool has_valid_crc(const modbus_rtu_adu_t *rtu)
{
    uint16_t expected_crc = calculate_crc(rtu->data, (uint16_t)(rtu->length - MODBUS_RTU_CRC_LENGTH));

    return (rtu->data[rtu->length - UINT16_C(2)] == (uint8_t)expected_crc) && (rtu->data[rtu->length - UINT16_C(1)] == (uint8_t)(expected_crc >> 8U));
}

static void finalize_encoded_rtu(modbus_rtu_adu_t *rtu)
{
    uint16_t crc = calculate_crc(rtu->data, (uint16_t)(rtu->length - MODBUS_RTU_CRC_LENGTH));

    rtu->data[rtu->length - UINT16_C(2)] = (uint8_t)crc;
    rtu->data[rtu->length - UINT16_C(1)] = (uint8_t)(crc >> 8U);
}

static void prepare_encoded_rtu(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint8_t function, uint16_t length)
{
    rtu->length = length;
    rtu->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX] = slave_address;
    rtu->data[MODBUS_RTU_FUNCTION_INDEX] = function;
}

static modbus_rtu_result_t validate_address_quantity(uint16_t start_address, uint16_t quantity, uint16_t maximum_quantity)
{
    uint32_t last_address;

    if ((quantity < MODBUS_RTU_MIN_QUANTITY) || (quantity > maximum_quantity))
    {
        return MODBUS_RTU_QUANTITY_INVALID;
    }
    last_address = (uint32_t)start_address + (uint32_t)quantity - UINT32_C(1);
    if (last_address > UINT16_MAX)
    {
        return MODBUS_RTU_ADDRESS_RANGE_INVALID;
    }
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t validate_request_length(const modbus_rtu_adu_t *rtu)
{
    uint16_t expected_length;

    if (rtu == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }
    if ((rtu->length < MODBUS_RTU_MIN_LENGTH) || (rtu->length > MODBUS_RTU_MAX_LENGTH))
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }

    switch (rtu->data[MODBUS_RTU_FUNCTION_INDEX])
    {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS:
        expected_length = MODBUS_RTU_ADDRESS_QUANTITY_REQUEST_LENGTH;
        break;
    case MODBUS_FC_WRITE_SINGLE_COIL:
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        expected_length = MODBUS_RTU_WRITE_SINGLE_REQUEST_LENGTH;
        break;
    case MODBUS_FC_WRITE_MULTIPLE_COILS:
        if (rtu->length < MODBUS_RTU_WRITE_MULTIPLE_REQUEST_OVERHEAD_LENGTH)
        {
            return MODBUS_RTU_LENGTH_INVALID;
        }
        expected_length = (uint16_t)(MODBUS_RTU_WRITE_MULTIPLE_REQUEST_OVERHEAD_LENGTH +
                                     rtu->data[MODBUS_RTU_WRITE_BYTE_COUNT_INDEX]);
        break;
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        if (rtu->length < MODBUS_RTU_WRITE_MULTIPLE_REQUEST_OVERHEAD_LENGTH)
        {
            return MODBUS_RTU_LENGTH_INVALID;
        }
        expected_length = (uint16_t)(MODBUS_RTU_WRITE_MULTIPLE_REQUEST_OVERHEAD_LENGTH +
                                     rtu->data[MODBUS_RTU_WRITE_BYTE_COUNT_INDEX]);
        break;
    default:
        return MODBUS_RTU_FUNCTION_UNSUPPORTED;
    }

    if (rtu->length != expected_length)
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t validate_address_quantity_request(const modbus_rtu_adu_t *rtu, uint16_t maximum_quantity)
{
    uint16_t start_address;
    uint16_t quantity;

    start_address = read_u16_be(&rtu->data[MODBUS_RTU_START_ADDRESS_INDEX]);
    quantity = read_u16_be(&rtu->data[MODBUS_RTU_QUANTITY_INDEX]);
    return validate_address_quantity(start_address, quantity, maximum_quantity);
}

static modbus_rtu_result_t validate_write_single_coil_request(const modbus_rtu_adu_t *rtu)
{
    uint16_t value;

    value = read_u16_be(&rtu->data[MODBUS_RTU_QUANTITY_INDEX]);
    if (!is_legal_write_single_coil_value(value))
    {
        return MODBUS_RTU_VALUE_INVALID;
    }
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t validate_write_multiple_coils_request(const modbus_rtu_adu_t *rtu)
{
    uint16_t start_address;
    uint16_t quantity;
    uint16_t expected_byte_count;
    modbus_rtu_result_t result;

    start_address = read_u16_be(&rtu->data[MODBUS_RTU_START_ADDRESS_INDEX]);
    quantity = read_u16_be(&rtu->data[MODBUS_RTU_QUANTITY_INDEX]);
    result = validate_address_quantity(start_address, quantity, MODBUS_RTU_WRITE_MULTIPLE_COILS_MAX_QUANTITY);
    if (result != MODBUS_RTU_OK)
    {
        return result;
    }

    expected_byte_count = (uint16_t)((quantity + UINT16_C(7)) / UINT16_C(8));
    if (rtu->data[MODBUS_RTU_WRITE_BYTE_COUNT_INDEX] != (uint8_t)expected_byte_count)
    {
        return MODBUS_RTU_BYTE_COUNT_INVALID;
    }
    if (!padding_bits_are_zero(&rtu->data[MODBUS_RTU_WRITE_VALUES_INDEX], quantity))
    {
        return MODBUS_RTU_VALUE_INVALID;
    }
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t validate_write_multiple_registers_request(const modbus_rtu_adu_t *rtu)
{
    uint16_t start_address;
    uint16_t quantity;
    uint16_t expected_byte_count;
    modbus_rtu_result_t result;

    start_address = read_u16_be(&rtu->data[MODBUS_RTU_START_ADDRESS_INDEX]);
    quantity = read_u16_be(&rtu->data[MODBUS_RTU_QUANTITY_INDEX]);
    result = validate_address_quantity(start_address, quantity, MODBUS_RTU_WRITE_MULTIPLE_REGISTERS_MAX_QUANTITY);
    if (result != MODBUS_RTU_OK)
    {
        return result;
    }

    expected_byte_count = (uint16_t)(quantity * UINT16_C(2));
    if (rtu->data[MODBUS_RTU_WRITE_BYTE_COUNT_INDEX] != (uint8_t)expected_byte_count)
    {
        return MODBUS_RTU_BYTE_COUNT_INVALID;
    }
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t encode_address_quantity_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint8_t function, uint16_t maximum_quantity, uint16_t start_address, uint16_t quantity)
{
    modbus_rtu_result_t result;

    if (rtu == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }
    if (!slave_address_is_routable(slave_address))
    {
        return MODBUS_RTU_SLAVE_ADDRESS_INVALID;
    }

    result = validate_address_quantity(start_address, quantity, maximum_quantity);
    if (result != MODBUS_RTU_OK)
    {
        return result;
    }

    prepare_encoded_rtu(rtu, slave_address, function, MODBUS_RTU_ADDRESS_QUANTITY_REQUEST_LENGTH);
    write_u16_be(&rtu->data[MODBUS_RTU_START_ADDRESS_INDEX], start_address);
    write_u16_be(&rtu->data[MODBUS_RTU_QUANTITY_INDEX], quantity);
    finalize_encoded_rtu(rtu);
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t decode_bit_response(const modbus_rtu_adu_t *response, uint8_t function, uint16_t maximum_quantity, uint8_t *slave_address, uint16_t expected_quantity, bool *values)
{
    uint16_t expected_byte_count;
    uint16_t index;

    if (response == NULL || slave_address == NULL || values == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }

    if(expected_quantity < MODBUS_RTU_MIN_QUANTITY || expected_quantity > maximum_quantity)
    {
        return MODBUS_RTU_QUANTITY_INVALID;
    }

    if (response->length < MODBUS_RTU_BYTE_RESPONSE_OVERHEAD_LENGTH || response->length > MODBUS_RTU_MAX_LENGTH)
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }

    if(response->length != response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX] + MODBUS_RTU_BYTE_RESPONSE_OVERHEAD_LENGTH)
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }

    if (!has_valid_crc(response))
    {
        return MODBUS_RTU_CRC_INVALID;
    }

    if (response->data[MODBUS_RTU_FUNCTION_INDEX] != function)
    {
        return MODBUS_RTU_FUNCTION_MISMATCH;
    }

    if (!slave_address_is_routable(response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX]))
    {
        return MODBUS_RTU_SLAVE_ADDRESS_INVALID;
    }

    expected_byte_count = (uint16_t)((expected_quantity + UINT16_C(7)) / UINT16_C(8));
    if (response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX] != expected_byte_count)
    {
        return MODBUS_RTU_BYTE_COUNT_INVALID;
    }

    if (!padding_bits_are_zero(&response->data[MODBUS_RTU_READ_VALUES_INDEX], expected_quantity))
    {
        return MODBUS_RTU_VALUE_INVALID;
    }

    for (index = 0U; index < expected_quantity; index += UINT16_C(1))
    {
        values[index] = ((response->data[MODBUS_RTU_READ_VALUES_INDEX + ((size_t)index / 8U)] >> (index % UINT16_C(8))) & UINT8_C(1)) != 0U;
    }
    *slave_address = response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX];
    return MODBUS_RTU_OK;
}

static modbus_rtu_result_t decode_register_response(const modbus_rtu_adu_t *response, uint8_t function, uint16_t maximum_quantity, uint8_t *slave_address,  uint16_t expected_quantity, uint16_t *register_values)
{
    uint16_t byte_count;
    uint16_t decoded_quantity;
    uint16_t index;

    if (response == NULL || slave_address == NULL || register_values == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }

    if(expected_quantity < MODBUS_RTU_MIN_QUANTITY || expected_quantity > maximum_quantity)
    {
        return MODBUS_RTU_QUANTITY_INVALID;
    }

    if (response->length < MODBUS_RTU_BYTE_RESPONSE_OVERHEAD_LENGTH || response->length > MODBUS_RTU_MAX_LENGTH)
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }

    if(response->length != response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX] + MODBUS_RTU_BYTE_RESPONSE_OVERHEAD_LENGTH)
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }

    if (!has_valid_crc(response))
    {
        return MODBUS_RTU_CRC_INVALID;
    }

    if (!slave_address_is_routable(response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX]))
    {
        return MODBUS_RTU_SLAVE_ADDRESS_INVALID;
    }

    if (response->data[MODBUS_RTU_FUNCTION_INDEX] != function)
    {
        return MODBUS_RTU_FUNCTION_MISMATCH;
    }

    byte_count = response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX];

    if ((byte_count & UINT16_C(1)) != 0U)
    {
        return MODBUS_RTU_BYTE_COUNT_INVALID;
    }

    decoded_quantity = (uint16_t)(byte_count / UINT16_C(2));

    if (expected_quantity != decoded_quantity)
    {
        return MODBUS_RTU_BYTE_COUNT_INVALID;
    }

    for (index = 0U; index < decoded_quantity; index += UINT16_C(1))
    {
        size_t value_index = MODBUS_RTU_READ_VALUES_INDEX + ((size_t)index * 2U);

        register_values[index] = (uint16_t)(((uint16_t)response->data[value_index] << 8U) |
                                            (uint16_t)response->data[value_index + 1U]);
    }
    *slave_address = response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX];
    return MODBUS_RTU_OK;
}


modbus_rtu_result_t modbus_rtu_validate_request(const modbus_rtu_adu_t *rtu)
{
    uint8_t function;
    modbus_rtu_result_t result;

    result = validate_request_length(rtu);
    if (result != MODBUS_RTU_OK)
    {
        return result;
    }

    if (!has_valid_crc(rtu))
    {
        return MODBUS_RTU_CRC_INVALID;
    }
    if (!slave_address_is_routable(rtu->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX]))
    {
        return MODBUS_RTU_SLAVE_ADDRESS_INVALID;
    }

    function = rtu->data[MODBUS_RTU_FUNCTION_INDEX];


    switch (function)
    {
    case MODBUS_FC_READ_COILS:
        return validate_address_quantity_request(rtu, MODBUS_READ_COILS_MAX_COILS);
    case MODBUS_FC_READ_DISCRETE_INPUTS:
        return validate_address_quantity_request(rtu, MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS);
    case MODBUS_FC_READ_HOLDING_REGISTERS:
        return validate_address_quantity_request(rtu, MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS);
    case MODBUS_FC_READ_INPUT_REGISTERS:
        return validate_address_quantity_request(rtu, MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS);
    case MODBUS_FC_WRITE_SINGLE_COIL:
        return validate_write_single_coil_request(rtu);
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        return MODBUS_RTU_OK;
    case MODBUS_FC_WRITE_MULTIPLE_COILS:
        return validate_write_multiple_coils_request(rtu);
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        return validate_write_multiple_registers_request(rtu);
    default:
        return MODBUS_RTU_FUNCTION_UNSUPPORTED;
    }
}

modbus_rtu_result_t modbus_rtu_read_coils_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity)
{
    return encode_address_quantity_request(rtu, slave_address, MODBUS_FC_READ_COILS, MODBUS_READ_COILS_MAX_COILS, start_address, quantity);
}

modbus_rtu_result_t modbus_rtu_read_coils_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, bool *coil_values)
{
    return decode_bit_response(response, MODBUS_FC_READ_COILS, MODBUS_READ_COILS_MAX_COILS, slave_address, expected_quantity, coil_values);
}

modbus_rtu_result_t modbus_rtu_read_discrete_inputs_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity)
{
    return encode_address_quantity_request(rtu, slave_address, MODBUS_FC_READ_DISCRETE_INPUTS, MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS, start_address, quantity);
}

modbus_rtu_result_t modbus_rtu_read_discrete_inputs_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, bool *input_values)
{
    return decode_bit_response(response, MODBUS_FC_READ_DISCRETE_INPUTS, MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS, slave_address, expected_quantity, input_values);
}

modbus_rtu_result_t modbus_rtu_read_holding_registers_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity)
{
    return encode_address_quantity_request(rtu, slave_address, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS, start_address, quantity);
}

modbus_rtu_result_t modbus_rtu_read_holding_registers_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, uint16_t *register_values)
{
    return decode_register_response(response, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS, slave_address, expected_quantity, register_values);
}

modbus_rtu_result_t modbus_rtu_read_input_registers_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity)
{
    return encode_address_quantity_request(rtu, slave_address, MODBUS_FC_READ_INPUT_REGISTERS, MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS, start_address, quantity);
}

modbus_rtu_result_t modbus_rtu_read_input_registers_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, uint16_t *register_values)
{
    return decode_register_response(response, MODBUS_FC_READ_INPUT_REGISTERS, MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS, slave_address, expected_quantity, register_values);
}

modbus_rtu_result_t modbus_rtu_encode_exception_response(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint8_t request_function, uint8_t exception_code)
{
    if (rtu == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }

    if (!slave_address_is_routable(slave_address))
    {
        return MODBUS_RTU_SLAVE_ADDRESS_INVALID;
    }

    if (!is_legal_request_function(request_function))
    {
        return MODBUS_RTU_FUNCTION_UNSUPPORTED;
    }
    if (!is_legal_exception_code(exception_code))
    {
        return MODBUS_RTU_VALUE_INVALID;
    }

    prepare_encoded_rtu(rtu, slave_address, (uint8_t)(request_function | UINT8_C(0x80)), MODBUS_RTU_EXCEPTION_RESPONSE_LENGTH);
    rtu->data[MODBUS_RTU_EXCEPTION_CODE_INDEX] = exception_code;
    finalize_encoded_rtu(rtu);
    return MODBUS_RTU_OK;
}

modbus_rtu_result_t modbus_rtu_decode_exception_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint8_t *request_function, uint8_t *exception_code)
{
    uint8_t decoded_function;
    uint8_t decoded_exception_code;

    if (response == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }

    if (slave_address == NULL || request_function == NULL || exception_code == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT;
    }

    if (response->length != MODBUS_RTU_EXCEPTION_RESPONSE_LENGTH)
    {
        return MODBUS_RTU_LENGTH_INVALID;
    }

    if (!has_valid_crc(response))
    {
        return MODBUS_RTU_CRC_INVALID;
    }

    if (!slave_address_is_routable(response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX]))
    {
        return MODBUS_RTU_SLAVE_ADDRESS_INVALID;
    }

    if (response->data[MODBUS_RTU_FUNCTION_INDEX] <= UINT8_C(0x80))
    {
        return MODBUS_RTU_FUNCTION_DOMAIN_INVALID;
    }
    decoded_function = (uint8_t)(response->data[MODBUS_RTU_FUNCTION_INDEX] & UINT8_C(0x7F));
    if (!is_legal_request_function(decoded_function))
    {
        return MODBUS_RTU_FUNCTION_DOMAIN_INVALID;
    }
    decoded_exception_code = response->data[MODBUS_RTU_EXCEPTION_CODE_INDEX];
    if (!is_legal_exception_code(decoded_exception_code))
    {
        return MODBUS_RTU_VALUE_INVALID;
    }

    *slave_address = response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX];
    *request_function = decoded_function;
    *exception_code = decoded_exception_code;
    return MODBUS_RTU_OK;
}

modbus_rtu_channel_result_t modbus_rtu_channel_init(
    modbus_rtu_channel_t *channel,
    void *context)
{
    if (channel == NULL)
    {
        return MODBUS_RTU_CHANNEL_INVALID_ARGUMENT;
    }

    channel->context = context;
    return MODBUS_RTU_CHANNEL_OK;
}

modbus_rtu_transaction_result_t modbus_rtu_transact(modbus_rtu_channel_t *channel, const modbus_rtu_adu_t *request, uint32_t response_timeout_ms, modbus_rtu_adu_t *response)
{
    if (channel == NULL || request == NULL || response == NULL || response_timeout_ms == 0U)
    {
        return MODBUS_RTU_TRANSACTION_INVALID_ARGUMENT;
    }

    uint16_t recv_len = 0;

    modbus_rtu_rs485_port_result_t result =  modbus_rtu_rs485_port_transceive(channel->context, request->data, request->length,
                                                                                response->data, response_timeout_ms, &recv_len);

    if(result == MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR)
    {
        return MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR;
    }
    else if(result == MODBUS_RTU_RS485_PORT_RESULT_SLAVE_TIMEOUT)
    {
        return MODBUS_RTU_TRANSACTION_RESPONSE_TIMEOUT;
    }

    response->length = recv_len;

    uint8_t request_function = request->data[MODBUS_RTU_FUNCTION_INDEX];
    uint8_t response_function = response->data[MODBUS_RTU_FUNCTION_INDEX];

    // 先检查是否截断，再检查crc

    if (response->length < MODBUS_RTU_MIN_LENGTH)
    {
        return MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID;
    }

    switch (response_function)
    {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS:
        if (response->length != MODBUS_RTU_BYTE_RESPONSE_OVERHEAD_LENGTH + response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX])
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID;
        }
        break;
    case MODBUS_FC_WRITE_SINGLE_COIL:
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
    case MODBUS_FC_WRITE_MULTIPLE_COILS:
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        if (response->length != MODBUS_RTU_WRITE_RESPONSE_LENGTH)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID;
        }
        break;
    default:
        if((response_function & UINT8_C(0x80)) != 0 && response_function != 0x80)
        {
            if (response->length != MODBUS_RTU_EXCEPTION_RESPONSE_LENGTH)
            {
                return MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID;
            }
            break;
        }

        return MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_INVALID;
    }

    if (!has_valid_crc(response))
    {
        return MODBUS_RTU_TRANSACTION_RESPONSE_CRC_INVALID;
    }

    // 检查响应从机地址，功能码是否能与请求对应上

    if (response->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX] != request->data[MODBUS_RTU_SLAVE_ADDRESS_INDEX])
    {
        return MODBUS_RTU_TRANSACTION_RESPONSE_SLAVE_ADDRESS_MISMATCH;
    }

    if (response_function != request_function && response_function != (uint8_t)(request_function | UINT8_C(0x80)))
    {
        return MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_MISMATCH;
    }

    if (response_function == (uint8_t)(request_function | UINT8_C(0x80)))
    {
        if(!is_legal_exception_code(response->data[MODBUS_RTU_EXCEPTION_CODE_INDEX]))
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID;
        }
        return MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE;
    }

    // 检查响应数据部分是否能与请求对应上

    uint16_t request_quantity;
    uint16_t expected_byte_count;
    uint16_t declared_byte_count;
    uint16_t request_address;
    uint16_t request_value;
    uint16_t response_address;
    uint16_t response_value;
    uint16_t request_start_address;
    uint16_t response_start_address;
    uint16_t response_quantity;

    switch (request_function)
    {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
        request_quantity = read_u16_be(&request->data[MODBUS_RTU_QUANTITY_INDEX]);
        expected_byte_count = (uint16_t)((request_quantity + UINT16_C(7)) / UINT16_C(8));

        if (response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX] != (uint8_t)expected_byte_count)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH;
        }

        if (padding_bits_are_zero(&response->data[MODBUS_RTU_READ_VALUES_INDEX], request_quantity) == false)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID;
        }

        break;
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS:
        request_quantity = read_u16_be(&request->data[MODBUS_RTU_QUANTITY_INDEX]);
        declared_byte_count = response->data[MODBUS_RTU_READ_BYTE_COUNT_INDEX];

        if(declared_byte_count % 2 != 0)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID;
        }

        if (request_quantity != declared_byte_count / 2)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH;
        }
        break;
    case MODBUS_FC_WRITE_SINGLE_COIL:
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        request_address = read_u16_be(&request->data[MODBUS_RTU_START_ADDRESS_INDEX]);
        request_value = read_u16_be(&request->data[MODBUS_RTU_QUANTITY_INDEX]);
        response_address = read_u16_be(&response->data[MODBUS_RTU_START_ADDRESS_INDEX]);
        response_value = read_u16_be(&response->data[MODBUS_RTU_QUANTITY_INDEX]);

        if (request_value != response_value || request_address != response_address)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH;
        }
        break;
    case MODBUS_FC_WRITE_MULTIPLE_COILS:
        request_start_address = read_u16_be(&request->data[MODBUS_RTU_START_ADDRESS_INDEX]);
        request_quantity = read_u16_be(&request->data[MODBUS_RTU_QUANTITY_INDEX]);
        response_start_address = read_u16_be(&response->data[MODBUS_RTU_START_ADDRESS_INDEX]);
        response_quantity = read_u16_be(&response->data[MODBUS_RTU_QUANTITY_INDEX]);

        if (request_start_address != response_start_address || request_quantity != response_quantity)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH;
        }
        break;
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        request_start_address = read_u16_be(&request->data[MODBUS_RTU_START_ADDRESS_INDEX]);
        request_quantity = read_u16_be(&request->data[MODBUS_RTU_QUANTITY_INDEX]);
        response_start_address = read_u16_be(&response->data[MODBUS_RTU_START_ADDRESS_INDEX]);
        response_quantity = read_u16_be(&response->data[MODBUS_RTU_QUANTITY_INDEX]);
        if (request_start_address != response_start_address || request_quantity != response_quantity)
        {
            return MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH;
        }
        break;
    default:
        break;
    }

    return MODBUS_RTU_TRANSACTION_OK;
}
