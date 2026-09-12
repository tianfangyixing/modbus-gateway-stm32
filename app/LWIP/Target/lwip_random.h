#ifndef LWIP_RANDOM_H
#define LWIP_RANDOM_H

#include <stdint.h>

void lwip_random_initialize(void);
uint32_t lwip_random_u32(void);

#endif /* LWIP_RANDOM_H */
