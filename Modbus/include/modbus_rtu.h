#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_RTU_MAX_LENGTH 256U
#define MODBUS_RTU_CRC_LENGTH UINT16_C(2)
#define MODBUS_RTU_MIN_LENGTH UINT16_C(4)

#define MODBUS_READ_COILS_MAX_COILS 2000U
#define MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS 2000U
#define MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS 125U
#define MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS 125U

typedef struct {
    uint16_t length;
    uint8_t data[MODBUS_RTU_MAX_LENGTH];
} modbus_rtu_adu_t;

typedef enum {
    MODBUS_RTU_OK = 0,
    MODBUS_RTU_INVALID_ARGUMENT,
    MODBUS_RTU_LENGTH_INVALID,
    MODBUS_RTU_SLAVE_ADDRESS_INVALID,
    MODBUS_RTU_CRC_INVALID,
    MODBUS_RTU_FUNCTION_UNSUPPORTED,
    MODBUS_RTU_FUNCTION_DOMAIN_INVALID,
    MODBUS_RTU_FUNCTION_MISMATCH,
    MODBUS_RTU_ADDRESS_RANGE_INVALID,
    MODBUS_RTU_BYTE_COUNT_INVALID,
    MODBUS_RTU_VALUE_INVALID,
    MODBUS_RTU_QUANTITY_INVALID,
} modbus_rtu_result_t;

typedef int32_t (*modbus_rtu_read_fn)(void *context, uint8_t *buffer, uint16_t capacity, uint32_t timeout_ms);
typedef int32_t (*modbus_rtu_write_fn)(void *context, const uint8_t *buffer, uint16_t count, uint32_t timeout_ms);

typedef struct {
    void *context;
    modbus_rtu_read_fn read;
    modbus_rtu_write_fn write;
    uint32_t baud_rate;
} modbus_rtu_channel_t;

typedef enum {
    MODBUS_RTU_CHANNEL_OK = 0,
    MODBUS_RTU_CHANNEL_INVALID_ARGUMENT,
    MODBUS_RTU_CHANNEL_BAUD_RATE_INVALID
} modbus_rtu_channel_result_t;

typedef enum {
    MODBUS_RTU_TRANSACTION_OK = 0,
    MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE,
    MODBUS_RTU_TRANSACTION_INVALID_ARGUMENT,
    MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR,
    MODBUS_RTU_TRANSACTION_RESPONSE_TIMEOUT,
    MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID,
    MODBUS_RTU_TRANSACTION_RESPONSE_CRC_INVALID,
    MODBUS_RTU_TRANSACTION_RESPONSE_SLAVE_ADDRESS_MISMATCH,
    MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_MISMATCH,
    MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_INVALID,
    MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH,
    MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID,
} modbus_rtu_transaction_result_t;

modbus_rtu_result_t modbus_rtu_validate_request(const modbus_rtu_adu_t *rtu);

modbus_rtu_result_t modbus_rtu_read_coils_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity);
modbus_rtu_result_t modbus_rtu_read_coils_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, bool *coil_values);

modbus_rtu_result_t modbus_rtu_read_discrete_inputs_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity);
modbus_rtu_result_t modbus_rtu_read_discrete_inputs_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, bool *input_values);

modbus_rtu_result_t modbus_rtu_read_holding_registers_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity);
modbus_rtu_result_t modbus_rtu_read_holding_registers_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, uint16_t *register_values);

modbus_rtu_result_t modbus_rtu_read_input_registers_encode_request(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint16_t start_address, uint16_t quantity);
modbus_rtu_result_t modbus_rtu_read_input_registers_decode_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint16_t expected_quantity, uint16_t *register_values);

modbus_rtu_result_t modbus_rtu_encode_exception_response(modbus_rtu_adu_t *rtu, uint8_t slave_address, uint8_t request_function, uint8_t exception_code);
modbus_rtu_result_t modbus_rtu_decode_exception_response(const modbus_rtu_adu_t *response, uint8_t *slave_address, uint8_t *request_function, uint8_t *exception_code);

modbus_rtu_channel_result_t modbus_rtu_channel_init(
    modbus_rtu_channel_t *channel,
    void *context,
    modbus_rtu_read_fn read,
    modbus_rtu_write_fn write,
    uint32_t baud_rate);
modbus_rtu_transaction_result_t modbus_rtu_transact(modbus_rtu_channel_t *channel, const modbus_rtu_adu_t *request, uint32_t response_timeout_ms, modbus_rtu_adu_t *response);

#endif
