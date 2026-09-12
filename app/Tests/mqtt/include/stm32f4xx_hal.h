#ifndef MQTT_TEST_HAL_H
#define MQTT_TEST_HAL_H
#include <stdint.h>
typedef int RTC_HandleTypeDef;
typedef int HAL_StatusTypeDef;
typedef struct
{
    uint8_t Hours, Minutes, Seconds;
    uint32_t SubSeconds, SecondFraction, DayLightSaving, StoreOperation;
} RTC_TimeTypeDef;
typedef struct
{
    uint8_t WeekDay, Month, Date, Year;
} RTC_DateTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define RTC_FORMAT_BIN 0
#define RTC_DAYLIGHTSAVING_NONE 0
#define RTC_STOREOPERATION_RESET 0
#define RTC_WEEKDAY_SUNDAY 7
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t format);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *date, uint32_t format);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t format);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *date, uint32_t format);
#endif
