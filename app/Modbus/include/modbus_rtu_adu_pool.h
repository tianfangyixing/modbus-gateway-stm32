#ifndef MODBUS_RTU_ADU_POOL_H
#define MODBUS_RTU_ADU_POOL_H

#include "modbus_rtu.h"

#define MODBUS_RTU_ADU_POOL_CAPACITY 6U

void modbus_rtu_adu_pool_init(void);
modbus_rtu_adu_t *modbus_rtu_adu_pool_allocate(void);
void modbus_rtu_adu_pool_release(modbus_rtu_adu_t *adu);

#endif
