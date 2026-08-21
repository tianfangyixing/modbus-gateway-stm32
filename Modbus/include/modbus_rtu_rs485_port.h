#ifndef MODBUS_RTU_RS485_PORT_H
#define MODBUS_RTU_RS485_PORT_H

#include <stdint.h>

void modbus_rtu_rs485_port_init(void);
int32_t modbus_rtu_rs485_port_read(void *context, uint8_t *buffer, uint16_t capacity, uint32_t timeout_ms);
int32_t modbus_rtu_rs485_port_write(void *context, const uint8_t *buffer, uint16_t count, uint32_t timeout_ms);

#endif
