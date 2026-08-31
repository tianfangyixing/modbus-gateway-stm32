#include "lwip_random.h"

#include "rng.h"

#include <stdlib.h>

uint32_t lwip_random_u32(void)
{
    uint32_t random_value = 0U;

    if (HAL_RNG_GenerateRandomNumber(&hrng, &random_value) != HAL_OK)
    {
        Error_Handler();
    }

    return random_value;
}

void lwip_random_initialize(void)
{
    srand((unsigned int)lwip_random_u32());
}
