#include "modbus_rtu_adu_pool.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdbool.h>
#include <stddef.h>

static modbus_rtu_adu_t pool_adus[MODBUS_RTU_ADU_POOL_CAPACITY];
static bool pool_available[MODBUS_RTU_ADU_POOL_CAPACITY];
static StaticSemaphore_t pool_mutex_buffer;
static SemaphoreHandle_t pool_mutex;


void modbus_rtu_adu_pool_init(void)
{
    pool_mutex = xSemaphoreCreateMutexStatic(&pool_mutex_buffer);

    for (size_t index = 0; index < MODBUS_RTU_ADU_POOL_CAPACITY; index++)
    {
        pool_available[index] = true;
        pool_adus[index].length = 0;
    }
}

modbus_rtu_adu_t *modbus_rtu_adu_pool_allocate(void)
{
    modbus_rtu_adu_t *allocated_adu = NULL;

    xSemaphoreTake(pool_mutex, portMAX_DELAY);

    for (size_t i = 0; i < MODBUS_RTU_ADU_POOL_CAPACITY; i++)
    {
        if (pool_available[i])
        {
            pool_available[i] = false;
            allocated_adu = &pool_adus[i];
            break;
        }
    }

    xSemaphoreGive(pool_mutex);
    return allocated_adu;
}

void modbus_rtu_adu_pool_release(modbus_rtu_adu_t *adu)
{
    xSemaphoreTake(pool_mutex, portMAX_DELAY);
    
    size_t index = (size_t)(adu - pool_adus);

    adu->length = 0;
    pool_available[index] = true;

    xSemaphoreGive(pool_mutex);
}
