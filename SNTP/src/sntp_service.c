#include "sntp_service.h"

#include <time.h>

#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"
#include "stm32f4xx_hal.h"
#include "debug_log.h"

static volatile bool is_synchronized = false;

bool sntp_service_is_synchronized(void)
{
    return is_synchronized;
}


void sntp_service_get_time(uint32_t *unix_seconds, uint32_t *microseconds)
{
    extern RTC_HandleTypeDef hrtc;

    RTC_TimeTypeDef rtc_time = {0};
    RTC_DateTypeDef rtc_date = {0};
    struct tm calendar_time = {0};
    time_t current_time;

    *unix_seconds = 0U;
    *microseconds = 0U;

    if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        debug_log("SNTP failed to read RTC time\n");
        return;
    }

    if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        debug_log("SNTP failed to read RTC date\n");
        return;
    }

    if (rtc_time.SubSeconds > rtc_time.SecondFraction)
    {
        debug_log("SNTP RTC subsecond value is invalid\n");
        return;
    }

    calendar_time.tm_sec = rtc_time.Seconds;
    calendar_time.tm_min = rtc_time.Minutes;
    calendar_time.tm_hour = rtc_time.Hours;
    calendar_time.tm_mday = rtc_date.Date;
    calendar_time.tm_mon = rtc_date.Month - 1;
    calendar_time.tm_year = rtc_date.Year + 100;
    calendar_time.tm_isdst = -1;
    current_time = mktime(&calendar_time);
    if (current_time == (time_t)-1)
    {
        debug_log("SNTP failed to convert RTC time\n");
        return;
    }

    *unix_seconds = (uint32_t)current_time;
    *microseconds = (uint32_t)(((uint64_t)(rtc_time.SecondFraction - rtc_time.SubSeconds) * 1000000U) /
                              (rtc_time.SecondFraction + 1U));
}

void sntp_service_set_time(uint32_t unix_seconds)
{
    extern RTC_HandleTypeDef hrtc;

    char buf[32];
    struct tm current_time_val;
    RTC_TimeTypeDef rtc_time = {0};
    RTC_DateTypeDef rtc_date = {0};
    time_t current_time = (time_t)unix_seconds;

    if (localtime_r(&current_time, &current_time_val) == NULL)
    {
        debug_log("SNTP time conversion failed: %lu\n", (unsigned long)unix_seconds);
        return;
    }

    if ((current_time_val.tm_year < 100) || (current_time_val.tm_year > 199))
    {
        debug_log("SNTP time is outside the RTC range: %lu\n", (unsigned long)unix_seconds);
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

    if (HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        debug_log("SNTP failed to set RTC time\n");
        return;
    }

    if (HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        debug_log("SNTP failed to set RTC date\n");
        return;
    }

    if (strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &current_time_val) == 0U)
    {
        debug_log("SNTP failed to format synchronized time\n");
        return;
    }

    is_synchronized = true;
    debug_log("SNTP time: %s\n", buf);
}

void sntp_service_init(void)
{
	LOCK_TCPIP_CORE();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_setservername(1, "ntp.tencent.com");
    sntp_init();
	UNLOCK_TCPIP_CORE();
}
