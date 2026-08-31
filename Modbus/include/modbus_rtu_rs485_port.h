#ifndef MODBUS_RTU_RS485_PORT_H
#define MODBUS_RTU_RS485_PORT_H

#include <stdint.h>

typedef enum
{
    MODBUS_RTU_RS485_PORT_RESULT_OK,
    MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR,
    MODBUS_RTU_RS485_PORT_RESULT_SLAVE_TIMEOUT,
}modbus_rtu_rs485_port_result_t;

void modbus_rtu_rs485_port_init(uint32_t configured_baud_rate, uint8_t configured_format);
modbus_rtu_rs485_port_result_t modbus_rtu_rs485_port_transceive(void *context, const uint8_t *request, uint16_t request_length, uint8_t *response, uint32_t response_timeout_ms, uint16_t *receive_len);


#endif
