#ifndef SYSTEM_TIME_H
#define SYSTEM_TIME_H

#include <stdint.h>

uint32_t system_get_ms(void);
uint32_t system_time_elapsed_ms(uint32_t start_time_ms);

#endif
