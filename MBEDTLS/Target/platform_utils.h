#ifndef MBEDTLS_TARGET_PLATFORM_UTILS_H
#define MBEDTLS_TARGET_PLATFORM_UTILS_H

#include <time.h>

#include "mbedtls/platform_time.h"

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf);


time_t mbedtls_platform_time(time_t *timer);


#endif /* MBEDTLS_TARGET_PLATFORM_UTILS_H */
