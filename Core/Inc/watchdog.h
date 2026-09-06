#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "event_groups.h"

#define WATCHDOG_EVENT_RTU_SCHEDULER ((EventBits_t)1U << 0U)
#define WATCHDOG_EVENT_COLLECTOR ((EventBits_t)1U << 1U)
#define WATCHDOG_EVENT_MODBUS_TCP ((EventBits_t)1U << 2U)
#define WATCHDOG_EVENT_MQTT ((EventBits_t)1U << 3U)
#define WATCHDOG_EVENT_MANAGEMENT ((EventBits_t)1U << 4U)
#define WATCHDOG_EVENT_ETHERNET_RX ((EventBits_t)1U << 5U)
#define WATCHDOG_EVENT_ETHERNET_LINK ((EventBits_t)1U << 6U)
#define WATCHDOG_EVENT_TCPIP ((EventBits_t)1U << 7U)
#define WATCHDOG_EVENT_ALL \
    (WATCHDOG_EVENT_RTU_SCHEDULER | WATCHDOG_EVENT_COLLECTOR | WATCHDOG_EVENT_MODBUS_TCP | WATCHDOG_EVENT_MQTT | \
     WATCHDOG_EVENT_MANAGEMENT | WATCHDOG_EVENT_ETHERNET_RX | WATCHDOG_EVENT_ETHERNET_LINK | WATCHDOG_EVENT_TCPIP)

/**
 * @brief 创建静态事件组和最高优先级的看门狗任务，不初始化 IWDG 硬件。
 * @note 在 RTOS 对象创建阶段或普通任务上下文单次调用，不得并发调用或从 ISR 调用。
 *       IWDG 必须已由启动流程初始化，所有报到调用必须在事件组和任务创建完成之后开始。
 *       当前实现不检查对象创建结果，也不处理重复初始化。
 */
void watchdog_init(void);

/**
 * @brief 由受监控任务设置自己的单个 WATCHDOG_EVENT_* 事件位。
 * @note 必须已完成初始化，仅供任务上下文调用，不得从 ISR 调用。
 *       看门狗可能在本调用返回前清除已收齐的事件位。
 */
void watchdog_report(EventBits_t task_bit);

#endif /* WATCHDOG_H */
