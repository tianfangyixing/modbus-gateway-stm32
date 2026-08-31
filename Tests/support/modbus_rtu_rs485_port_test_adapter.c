#include "modbus_rtu_rs485_port.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t test_baud_rate;
static uint8_t test_frame_format;

void modbus_rtu_rs485_port_init(uint32_t configured_baud_rate, uint8_t configured_format)
{
    test_baud_rate = configured_baud_rate;
    test_frame_format = configured_format;
}

modbus_rtu_rs485_port_result_t modbus_rtu_rs485_port_transceive(
    void *context, const uint8_t *request, uint16_t request_length, uint8_t *response, uint32_t response_timeout_ms,
    uint16_t *receive_len)
{
    if (request == NULL || request_length == 0U || response == NULL || response_timeout_ms == 0U ||
        receive_len == NULL)
    {
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }

    *receive_len = 0U;
    if (context != NULL || test_baud_rate == UINT32_MAX || test_frame_format == UINT8_MAX)
    {
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }
    return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
}
