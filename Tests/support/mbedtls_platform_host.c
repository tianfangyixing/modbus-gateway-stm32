#include "mbedtls/platform_util.h"

#include <time.h>

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *time_value, struct tm *result)
{
    if (time_value == NULL || result == NULL)
    {
        return NULL;
    }

#if defined(_WIN32)
    return gmtime_s(result, time_value) == 0 ? result : NULL;
#else
    return gmtime_r(time_value, result);
#endif
}
