#include "sntp_service.h"

#include <time.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"
#include "stm32f4xx_hal.h"
#include "debug_log.h"

static StaticSemaphore_t time_mutex_buffer;
static SemaphoreHandle_t time_mutex;
static volatile bool is_synchronized = false;

static const uint8_t sntp_service_days_per_month[12] =
{
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
};

static bool sntp_service_is_leap_year(uint32_t year)
{
    return ((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint32_t sntp_service_get_days_in_month(uint32_t year, uint32_t month)
{
    uint32_t days_in_month = sntp_service_days_per_month[month];

    if ((month == 1U) && sntp_service_is_leap_year(year))
    {
        days_in_month++;
    }

    return days_in_month;
}

static struct tm *sntp_service_gmtime_r(const time_t *timer, struct tm *result)
{
    struct tm calendar_time = {0};
    uint32_t remaining_seconds;
    uint32_t days_since_epoch;
    uint32_t day_of_year;
    uint32_t days_in_year;
    uint32_t days_in_month;
    uint32_t year;
    uint32_t month;

    if ((timer == NULL) || (result == NULL))
    {
        return NULL;
    }

    remaining_seconds = (uint32_t)*timer;
    calendar_time.tm_sec = (int)(remaining_seconds % 60U);
    remaining_seconds /= 60U;
    calendar_time.tm_min = (int)(remaining_seconds % 60U);
    remaining_seconds /= 60U;
    calendar_time.tm_hour = (int)(remaining_seconds % 24U);
    days_since_epoch = (uint32_t)(remaining_seconds / 24U);

    calendar_time.tm_wday = (int)((days_since_epoch + 4U) % 7U);
    day_of_year = days_since_epoch;
    year = 1970U;

    for (;;)
    {
        days_in_year = sntp_service_is_leap_year(year) ? 366U : 365U;
        if (day_of_year < days_in_year)
        {
            break;
        }

        day_of_year -= days_in_year;
        year++;
    }

    calendar_time.tm_year = (int)(year - 1900U);
    calendar_time.tm_yday = (int)day_of_year;

    for (month = 0U; month < 12U; month++)
    {
        days_in_month = sntp_service_get_days_in_month(year, month);
        if (day_of_year < days_in_month)
        {
            break;
        }

        day_of_year -= days_in_month;
    }

    if (month == 12U)
    {
        return NULL;
    }

    calendar_time.tm_mon = (int)month;
    calendar_time.tm_mday = (int)day_of_year + 1;
    calendar_time.tm_isdst = 0;

    *result = calendar_time;
    return result;
}

static time_t sntp_service_mktime(struct tm *calendar_time)
{
    uint32_t days_since_epoch = 0U;
    uint32_t total_days;
    uint64_t total_seconds;
    uint32_t day_of_year;
    uint32_t days_in_month;
    uint32_t year;
    uint32_t current_year;
    uint32_t month;
    uint32_t current_month;

    if (calendar_time == NULL)
    {
        return (time_t)-1;
    }

    if ((calendar_time->tm_year < 70) || (calendar_time->tm_year > 206) ||
        (calendar_time->tm_mon < 0) || (calendar_time->tm_mon > 11) ||
        (calendar_time->tm_hour < 0) || (calendar_time->tm_hour > 23) ||
        (calendar_time->tm_min < 0) || (calendar_time->tm_min > 59) ||
        (calendar_time->tm_sec < 0) || (calendar_time->tm_sec > 59))
    {
        return (time_t)-1;
    }

    year = (uint32_t)calendar_time->tm_year + 1900U;
    month = (uint32_t)calendar_time->tm_mon;
    days_in_month = sntp_service_get_days_in_month(year, month);
    if ((calendar_time->tm_mday < 1) || ((uint32_t)calendar_time->tm_mday > days_in_month))
    {
        return (time_t)-1;
    }

    for (current_year = 1970U; current_year < year; current_year++)
    {
        days_since_epoch += sntp_service_is_leap_year(current_year) ? 366U : 365U;
    }

    day_of_year = (uint32_t)calendar_time->tm_mday - 1U;
    for (current_month = 0U; current_month < month; current_month++)
    {
        day_of_year += sntp_service_get_days_in_month(year, current_month);
    }

    total_days = days_since_epoch + day_of_year;
    total_seconds = (uint64_t)total_days * 86400U;
    total_seconds += (uint32_t)calendar_time->tm_hour * 3600U;
    total_seconds += (uint32_t)calendar_time->tm_min * 60U;
    total_seconds += (uint32_t)calendar_time->tm_sec;
    if (total_seconds >= UINT32_MAX)
    {
        return (time_t)-1;
    }

    calendar_time->tm_wday = (int)((total_days + 4U) % 7U);
    calendar_time->tm_yday = (int)day_of_year;
    calendar_time->tm_isdst = 0;

    return (time_t)total_seconds;
}

bool sntp_service_is_synchronized(void)
{
    return is_synchronized;
}


bool sntp_service_get_time(uint32_t *unix_seconds, uint32_t *microseconds)
{
    extern RTC_HandleTypeDef hrtc;

    RTC_TimeTypeDef rtc_time = {0};
    RTC_DateTypeDef rtc_date = {0};
    struct tm calendar_time = {0};
    time_t current_time;
    bool result = false;

    xSemaphoreTake(time_mutex, portMAX_DELAY);
    *unix_seconds = 0U;
    *microseconds = 0U;

    if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        debug_log_printf("SNTP failed to read RTC time\n");
        goto release_mutex;
    }

    if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        debug_log_printf("SNTP failed to read RTC date\n");
        goto release_mutex;
    }

    if (rtc_time.SubSeconds > rtc_time.SecondFraction)
    {
        debug_log_printf("SNTP RTC subsecond value is invalid\n");
        goto release_mutex;
    }

    calendar_time.tm_sec = rtc_time.Seconds;
    calendar_time.tm_min = rtc_time.Minutes;
    calendar_time.tm_hour = rtc_time.Hours;
    calendar_time.tm_mday = rtc_date.Date;
    calendar_time.tm_mon = rtc_date.Month - 1;
    calendar_time.tm_year = rtc_date.Year + 100;
    calendar_time.tm_isdst = -1;
    current_time = sntp_service_mktime(&calendar_time);
    if (current_time == (time_t)-1)
    {
        debug_log_printf("SNTP failed to convert RTC time\n");
        goto release_mutex;
    }

    *unix_seconds = (uint32_t)current_time;
    *microseconds = (uint32_t)(((uint64_t)(rtc_time.SecondFraction - rtc_time.SubSeconds) * 1000000U) /
                              (rtc_time.SecondFraction + 1U));
    result = true;

release_mutex:
    xSemaphoreGive(time_mutex);
    return result;
}


void sntp_service_set_time(uint32_t unix_seconds)
{
    extern RTC_HandleTypeDef hrtc;

    char buf[32];
    struct tm current_time_val;
    RTC_TimeTypeDef rtc_time = {0};
    RTC_DateTypeDef rtc_date = {0};
    RTC_TimeTypeDef previous_rtc_time = {0};
    RTC_DateTypeDef previous_rtc_date = {0};
    time_t current_time = (time_t)unix_seconds;
    HAL_StatusTypeDef get_previous_time_status;
    HAL_StatusTypeDef get_previous_date_status = HAL_ERROR;
    HAL_StatusTypeDef set_time_status = HAL_ERROR;
    HAL_StatusTypeDef set_date_status = HAL_ERROR;
    HAL_StatusTypeDef restore_time_status = HAL_ERROR;
    HAL_StatusTypeDef restore_date_status = HAL_ERROR;

    if (sntp_service_gmtime_r(&current_time, &current_time_val) == NULL)
    {
        debug_log_printf("SNTP time conversion failed: %lu\n", (unsigned long)unix_seconds);
        return;
    }

    if ((current_time_val.tm_year < 100) || (current_time_val.tm_year > 199))
    {
        debug_log_printf("SNTP time is outside the RTC range: %lu\n", (unsigned long)unix_seconds);
        return;
    }

    rtc_time.Hours = (uint8_t)current_time_val.tm_hour;
    rtc_time.Minutes = (uint8_t)current_time_val.tm_min;
    rtc_time.Seconds = (uint8_t)current_time_val.tm_sec;
    rtc_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    rtc_time.StoreOperation = RTC_STOREOPERATION_RESET;

    rtc_date.WeekDay = (current_time_val.tm_wday == 0) ? RTC_WEEKDAY_SUNDAY : (uint8_t)current_time_val.tm_wday;
    rtc_date.Month = (uint8_t)(current_time_val.tm_mon + 1);
    rtc_date.Date = (uint8_t)current_time_val.tm_mday;
    rtc_date.Year = (uint8_t)(current_time_val.tm_year - 100);

    xSemaphoreTake(time_mutex, portMAX_DELAY);
    get_previous_time_status = HAL_RTC_GetTime(&hrtc, &previous_rtc_time, RTC_FORMAT_BIN);
    if (get_previous_time_status == HAL_OK)
    {
        get_previous_date_status = HAL_RTC_GetDate(&hrtc, &previous_rtc_date, RTC_FORMAT_BIN);
    }

    if ((get_previous_time_status == HAL_OK) && (get_previous_date_status == HAL_OK))
    {
        set_time_status = HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
        if (set_time_status == HAL_OK)
        {
            set_date_status = HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);
        }

        if ((set_time_status != HAL_OK) || (set_date_status != HAL_OK))
        {
            restore_time_status = HAL_RTC_SetTime(&hrtc, &previous_rtc_time, RTC_FORMAT_BIN);
            restore_date_status = HAL_RTC_SetDate(&hrtc, &previous_rtc_date, RTC_FORMAT_BIN);
        }
    }
    xSemaphoreGive(time_mutex);

    if (get_previous_time_status != HAL_OK)
    {
        debug_log_printf("SNTP failed to back up RTC time\n");
        return;
    }

    if (get_previous_date_status != HAL_OK)
    {
        debug_log_printf("SNTP failed to back up RTC date\n");
        return;
    }

    if (set_time_status != HAL_OK)
    {
        debug_log_printf("SNTP failed to set RTC time\n");
    }
    else if (set_date_status != HAL_OK)
    {
        debug_log_printf("SNTP failed to set RTC date\n");
    }

    if ((set_time_status != HAL_OK) || (set_date_status != HAL_OK))
    {
        if ((restore_time_status == HAL_OK) && (restore_date_status == HAL_OK))
        {
            debug_log_printf("SNTP restored the previous RTC time after synchronization failure\n");
        }
        else
        {
            debug_log_printf("SNTP failed to restore the previous RTC time and date\n");
        }
        return;
    }

    if (strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &current_time_val) == 0U)
    {
        debug_log_printf("SNTP failed to format synchronized time\n");
        return;
    }

    is_synchronized = true;
    debug_log_printf("SNTP time: %s\n", buf);
}

void sntp_service_init(void)
{
	time_mutex = xSemaphoreCreateMutexStatic(&time_mutex_buffer);
	LOCK_TCPIP_CORE();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_setservername(1, "ntp.tencent.com");
    sntp_init();
	UNLOCK_TCPIP_CORE();
}
