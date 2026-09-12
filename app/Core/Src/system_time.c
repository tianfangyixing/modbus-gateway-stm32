#include "system_time.h"

#include "stm32f4xx_hal.h"

uint32_t system_get_ms(void)
{
    return HAL_GetTick();
}

uint32_t system_time_elapsed_ms(uint32_t start_time_ms)
{
    return system_get_ms() - start_time_ms;
}
