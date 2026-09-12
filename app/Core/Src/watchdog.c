#include "watchdog.h"

#include "configuration_service.h"
#include "iwdg.h"
#include "management_transport.h"
#include "memory_sections.h"
#include "task.h"

#define WATCHDOG_TASK_STACK_DEPTH 256U
#define WATCHDOG_TASK_PRIORITY (configMAX_PRIORITIES - 1U)

static StaticEventGroup_t watchdog_event_buffer;
static EventGroupHandle_t watchdog_events;
static EventBits_t watchdog_required_bits;
static StaticTask_t watchdog_task_buffer;
static TaskHandle_t watchdog_task_handle;
static CCM_SRAM_ALIGNED(8) StackType_t watchdog_task_stack[WATCHDOG_TASK_STACK_DEPTH];

static void watchdog_task(void *argument)
{
    const configuration_t *configuration;

    for(int i = 0; i < 20; i++)
    {
        if(configuration_service_active() != NULL)
        {
            break;
        }
        vTaskDelay(100);
    }

    if (configuration_service_active() == NULL)
    {
        Error_Handler();
        return;
    }

    configuration = configuration_service_active();
    
    if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
    {
        Error_Handler();
    }

    watchdog_required_bits = WATCHDOG_EVENT_ALL;

    if (configuration->mqtt.mode == CONFIGURATION_MQTT_MODE_DISABLED)
    {
        watchdog_required_bits &= ~WATCHDOG_EVENT_MQTT;
    }
    if (configuration->collection.point_count == 0U)
    {
        watchdog_required_bits &= ~WATCHDOG_EVENT_COLLECTOR;
    }

    if (watchdog_required_bits == 0U)
    {
        Error_Handler();
        return;
    }

    for (;;)
    {
        EventBits_t bits = xEventGroupWaitBits(watchdog_events, watchdog_required_bits, pdTRUE, pdTRUE, portMAX_DELAY);

        if ((bits & watchdog_required_bits) == watchdog_required_bits)
        {
            if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
            {
                Error_Handler();
            }
        }
    }
}

void watchdog_init(void)
{
    watchdog_events = xEventGroupCreateStatic(&watchdog_event_buffer);
    watchdog_task_handle = xTaskCreateStatic(watchdog_task, "watchdog", WATCHDOG_TASK_STACK_DEPTH, NULL,
                                             WATCHDOG_TASK_PRIORITY, watchdog_task_stack, &watchdog_task_buffer);
}

void watchdog_report(EventBits_t task_bit)
{
    xEventGroupSetBits(watchdog_events, task_bit);
}
