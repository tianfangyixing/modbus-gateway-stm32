#include "platform_utils.h"
#include "sntp_service.h"


static int platform_utils_is_leap_year(unsigned int year)
{
    return ((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    static const unsigned int days_per_month[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    struct tm result = {0};
    mbedtls_time_t seconds;
    unsigned int days_since_epoch;
    unsigned int day_of_year;
    unsigned int days_in_year;
    unsigned int days_in_month;
    unsigned int year;
    unsigned int month;

    if ((tt == NULL) || (tm_buf == NULL))
    {
        return NULL;
    }

    seconds = *tt;
    result.tm_sec = (int)(seconds % 60U);
    seconds /= 60U;
    result.tm_min = (int)(seconds % 60U);
    seconds /= 60U;
    result.tm_hour = (int)(seconds % 24U);
    days_since_epoch = (unsigned int)(seconds / 24U);

    result.tm_wday = (int)((days_since_epoch + 4U) % 7U);
    day_of_year = days_since_epoch;
    year = 1970U;

    for (;;)
    {
        days_in_year = platform_utils_is_leap_year(year) ? 366U : 365U;
        if (day_of_year < days_in_year)
        {
            break;
        }

        day_of_year -= days_in_year;
        year++;
    }

    result.tm_year = (int)(year - 1900U);
    result.tm_yday = (int)day_of_year;

    for (month = 0U; month < 12U; month++)
    {
        days_in_month = days_per_month[month];
        if ((month == 1U) && platform_utils_is_leap_year(year))
        {
            days_in_month++;
        }

        if (day_of_year < days_in_month)
        {
            break;
        }

        day_of_year -= days_in_month;
    }

    result.tm_mon = (int)month;
    result.tm_mday = (int)day_of_year + 1;
    result.tm_isdst = 0;

    *tm_buf = result;
    return tm_buf;
}


time_t mbedtls_platform_time(time_t *timer)
{
    uint32_t unix_seconds;
    uint32_t microseconds;
    time_t current_time = (time_t)-1;

    if (sntp_service_get_time(&unix_seconds, &microseconds))
    {
        current_time = (time_t)unix_seconds;
    }

    if (timer != NULL)
    {
        *timer = current_time;
    }

    return current_time;
}

time_t time(time_t *timer)
{
    return mbedtls_platform_time(timer);
}
