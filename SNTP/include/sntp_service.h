#ifndef SNTP_SERVICE_H
#define SNTP_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

void sntp_service_init(void);
bool sntp_service_is_synchronized(void);
void sntp_service_get_time(uint32_t *unix_seconds, uint32_t *microseconds);
void sntp_service_set_time(uint32_t unix_seconds);

#endif
