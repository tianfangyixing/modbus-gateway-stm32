#ifndef MODBUS_COMMON_H
#define MODBUS_COMMON_H

#include "modbus_rtu.h"

#include <stdint.h>

static inline uint16_t read_u16_be(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static inline void write_u16_be(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static uint16_t calculate_crc(const uint8_t *data, uint16_t length)
{
    uint16_t crc = UINT16_C(0xFFFF);
    uint16_t byte_index;

    for (byte_index = 0U; byte_index < length; byte_index += UINT16_C(1))
    {
        uint8_t bit_index;
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < UINT8_C(8); bit_index += UINT8_C(1))
        {
            if ((crc & UINT16_C(1)) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ UINT16_C(0xA001));
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

#endif
