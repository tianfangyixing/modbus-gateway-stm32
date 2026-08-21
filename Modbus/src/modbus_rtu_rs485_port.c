#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "modbus_rtu.h"
#include "modbus_rtu_rs485_port.h"
#include "tim.h"
#include "usart.h"

/*
 * 当前配置：
 * - FreeRTOS tick = 1 ms
 * - USART3 = 9600 baud，8N1
 * - Modbus RTU 每字符按 11 bit 计算
 * - TIM7 tick = 1 us
 * - T3.5 = ceil(3.5 * 11 * 1000000 / 9600) = 4011 us
 *
 * 该端口只允许一个任务按 write -> read 顺序串行使用。RS485 收发方向由外部
 * 自动方向收发器处理；软件在发送期间关闭 RX，并在发送完成中断内立即启动逐字节中断接收。
 */
#define RS485_BAUD_RATE UINT32_C(9600)
#define RS485_T35_US UINT32_C(4011)
#define RS485_T35_GUARD_MS UINT32_C(7)
#define RS485_RX_ERROR_MASK (HAL_UART_ERROR_PE | HAL_UART_ERROR_NE | HAL_UART_ERROR_FE | HAL_UART_ERROR_ORE | \
                             HAL_UART_ERROR_DMA)

typedef enum
{
    RS485_PHASE_IDLE = 0,
    RS485_PHASE_TRANSMITTING,
    RS485_PHASE_RECEIVING,
    RS485_PHASE_FRAME_READY,
    RS485_PHASE_ERROR
} rs485_phase_t;

static uint8_t rx_buffer[MODBUS_RTU_MAX_LENGTH];
static uint8_t rx_byte;

static volatile uint16_t rx_length;
static volatile bool rx_overflow;
static volatile rs485_phase_t phase = RS485_PHASE_IDLE;
static volatile bool tx_completed_successfully;

static SemaphoreHandle_t rx_completed_semaphore;
static StaticSemaphore_t rx_completed_semaphore_buffer;

static SemaphoreHandle_t tx_completed_semaphore;
static StaticSemaphore_t tx_completed_semaphore_buffer;

static bool port_initialized;

static TickType_t rs485_timeout_to_ticks(uint32_t timeout_ms)
{
    TickType_t timeout_ticks = ((uint64_t)timeout_ms * configTICK_RATE_HZ + UINT64_C(999)) / UINT64_C(1000);

    if ((timeout_ms > 0U) && (timeout_ticks == 0U))
    {
        timeout_ticks = 1U;
    }

    return timeout_ticks;
}

static TickType_t rs485_t35_guard_ticks(void)
{
    TickType_t guard_ticks = pdMS_TO_TICKS(RS485_T35_GUARD_MS);

    if (guard_ticks == 0U)
    {
        guard_ticks = 1U;
    }

    return guard_ticks;
}

static HAL_StatusTypeDef rs485_start_t35_timer_from_isr(void)
{
    HAL_TIM_Base_Stop_IT(&htim7);
    __HAL_TIM_SET_AUTORELOAD(&htim7, RS485_T35_US - 1U);
    __HAL_TIM_SET_COUNTER(&htim7, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);

    return HAL_TIM_Base_Start_IT(&htim7);
}

static void rs485_pause_receive_from_isr(void)
{
    ATOMIC_CLEAR_BIT(huart3.Instance->CR1, USART_CR1_RXNEIE | USART_CR1_PEIE | USART_CR1_IDLEIE);
    ATOMIC_CLEAR_BIT(huart3.Instance->CR3, USART_CR3_EIE | USART_CR3_DMAR);
}

static HAL_StatusTypeDef rs485_start_receive_from_isr(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    rx_byte = 0U;
    rx_length = 0U;
    rx_overflow = false;
    phase = RS485_PHASE_RECEIVING;
    return HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);
}

static void rs485_signal_rx_failure_from_isr(BaseType_t *higher_priority_task_woken)
{
    if (phase != RS485_PHASE_RECEIVING)
    {
        return;
    }

    phase = RS485_PHASE_ERROR;
    HAL_TIM_Base_Stop_IT(&htim7);
    rs485_pause_receive_from_isr();
    xSemaphoreGiveFromISR(rx_completed_semaphore, higher_priority_task_woken);
}

static void rs485_uart_tx_complete_callback(UART_HandleTypeDef *huart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (huart != &huart3 || phase != RS485_PHASE_TRANSMITTING)
    {
        return;
    }

    /* 在唤醒写任务前启动 RX，避免任务调度延迟形成接收盲区。 */
    tx_completed_successfully = true;
    if (rs485_start_receive_from_isr() != HAL_OK)
    {
        phase = RS485_PHASE_ERROR;
    }

    xSemaphoreGiveFromISR(tx_completed_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void rs485_uart_rx_complete_callback(UART_HandleTypeDef *huart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    HAL_StatusTypeDef status;

    if (huart != &huart3 || phase != RS485_PHASE_RECEIVING)
    {
        return;
    }

    if ((huart->ErrorCode & RS485_RX_ERROR_MASK) != 0U)
    {
        rs485_signal_rx_failure_from_isr(&higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
        return;
    }

    if (rx_length < MODBUS_RTU_MAX_LENGTH)
    {
        rx_buffer[rx_length] = rx_byte;
        rx_length++;
    }
    else
    {
        rx_overflow = true;
    }

    status = HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);
    if (status == HAL_OK)
    {
        status = rs485_start_t35_timer_from_isr();
    }

    if (status != HAL_OK)
    {
        rs485_signal_rx_failure_from_isr(&higher_priority_task_woken);
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void rs485_uart_error_callback(UART_HandleTypeDef *huart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint32_t error_code;

    if (huart != &huart3)
    {
        return;
    }

    error_code = huart->ErrorCode;

    if (phase == RS485_PHASE_TRANSMITTING && (error_code & HAL_UART_ERROR_DMA) != 0U)
    {
        phase = RS485_PHASE_ERROR;
        tx_completed_successfully = false;
        xSemaphoreGiveFromISR(tx_completed_semaphore, &higher_priority_task_woken);
    }
    else if (phase == RS485_PHASE_RECEIVING && (error_code & RS485_RX_ERROR_MASK) != 0U)
    {
        rs485_signal_rx_failure_from_isr(&higher_priority_task_woken);
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void rs485_t35_elapsed_callback(TIM_HandleTypeDef *htim)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (htim != &htim7)
    {
        return;
    }

    HAL_TIM_Base_Stop_IT(&htim7);

    if (phase != RS485_PHASE_RECEIVING)
    {
        return;
    }

    rs485_pause_receive_from_isr();
    phase = rx_length > 0U ? RS485_PHASE_FRAME_READY : RS485_PHASE_ERROR;
    xSemaphoreGiveFromISR(rx_completed_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static HAL_StatusTypeDef rs485_reset_receive_path(void)
{
    HAL_StatusTypeDef status;

    taskENTER_CRITICAL();
    phase = RS485_PHASE_IDLE;
    HAL_TIM_Base_Stop_IT(&htim7);
    __HAL_TIM_SET_COUNTER(&htim7, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
    taskEXIT_CRITICAL();

    status = HAL_UART_AbortReceive(&huart3);

    taskENTER_CRITICAL();
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    rx_byte = 0U;
    rx_length = 0U;
    rx_overflow = false;
    xSemaphoreTake(rx_completed_semaphore, 0U);
    taskEXIT_CRITICAL();

    return status;
}

static bool rs485_prepare_transmit(void)
{
    rs485_phase_t phase_snapshot;

    taskENTER_CRITICAL();
    phase_snapshot = phase;
    taskEXIT_CRITICAL();

    if (phase_snapshot != RS485_PHASE_IDLE || rs485_reset_receive_path() != HAL_OK)
    {
        return false;
    }

    /*
     * 单主站总线在此恢复窗口内不得产生新流量。窗口开始和结束各清理一次 RX，
     * 但不再为任意迟到字节无限重启等待。
     */
    vTaskDelay(rs485_t35_guard_ticks());
    return rs485_reset_receive_path() == HAL_OK;
}

static void rs485_cancel_transmit(void)
{
    taskENTER_CRITICAL();
    phase = RS485_PHASE_IDLE;
    taskEXIT_CRITICAL();

    HAL_UART_AbortTransmit(&huart3);
    rs485_reset_receive_path();

    taskENTER_CRITICAL();
    xSemaphoreTake(tx_completed_semaphore, 0U);
    taskEXIT_CRITICAL();
}

void modbus_rtu_rs485_port_init(void)
{
    if (port_initialized)
    {
        return;
    }

    rx_completed_semaphore = xSemaphoreCreateBinaryStatic(&rx_completed_semaphore_buffer);
    tx_completed_semaphore = xSemaphoreCreateBinaryStatic(&tx_completed_semaphore_buffer);

    if (rx_completed_semaphore == NULL || tx_completed_semaphore == NULL ||
        huart3.Init.BaudRate != RS485_BAUD_RATE ||
        huart3.Init.WordLength != UART_WORDLENGTH_8B ||
        huart3.Init.StopBits != UART_STOPBITS_1 || huart3.Init.Parity != UART_PARITY_NONE ||
        huart3.Init.Mode != UART_MODE_TX_RX || huart3.hdmatx == NULL || huart3.hdmatx->Init.Mode != DMA_NORMAL)
    {
        Error_Handler();
    }

    if (HAL_UART_RegisterCallback(&huart3, HAL_UART_TX_COMPLETE_CB_ID, rs485_uart_tx_complete_callback) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UART_RegisterCallback(&huart3, HAL_UART_RX_COMPLETE_CB_ID, rs485_uart_rx_complete_callback) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UART_RegisterCallback(&huart3, HAL_UART_ERROR_CB_ID, rs485_uart_error_callback) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIM_RegisterCallback(&htim7, HAL_TIM_PERIOD_ELAPSED_CB_ID, rs485_t35_elapsed_callback) != HAL_OK)
    {
        Error_Handler();
    }

    rx_byte = 0U;
    rx_length = 0U;
    rx_overflow = false;
    phase = RS485_PHASE_IDLE;
    tx_completed_successfully = false;
    port_initialized = true;

    if (rs485_reset_receive_path() != HAL_OK)
    {
        Error_Handler();
    }
}

int32_t modbus_rtu_rs485_port_read(void *context, uint8_t *buffer, uint16_t capacity, uint32_t timeout_ms)
{
    TickType_t timeout_ticks;
    rs485_phase_t phase_snapshot;
    uint16_t length_snapshot;
    bool overflow_snapshot;

    if (!port_initialized || buffer == NULL || capacity == 0U)
    {
        return -1;
    }

    taskENTER_CRITICAL();
    phase_snapshot = phase;
    taskEXIT_CRITICAL();

    if (phase_snapshot == RS485_PHASE_ERROR)
    {
        rs485_reset_receive_path();
        return -1;
    }

    if (phase_snapshot != RS485_PHASE_RECEIVING && phase_snapshot != RS485_PHASE_FRAME_READY)
    {
        return -1;
    }

    timeout_ticks = rs485_timeout_to_ticks(timeout_ms);
    if (xSemaphoreTake(rx_completed_semaphore, timeout_ticks) != pdTRUE)
    {
        return rs485_reset_receive_path() == HAL_OK ? 0 : -1;
    }

    taskENTER_CRITICAL();
    phase_snapshot = phase;
    length_snapshot = rx_length;
    overflow_snapshot = rx_overflow;
    taskEXIT_CRITICAL();

    if (rs485_reset_receive_path() != HAL_OK)
    {
        return -1;
    }

    if (phase_snapshot != RS485_PHASE_FRAME_READY || overflow_snapshot || length_snapshot > capacity)
    {
        return -1;
    }

    memcpy(buffer, rx_buffer, length_snapshot);
    return (int32_t)length_snapshot;
}

int32_t modbus_rtu_rs485_port_write(void *context, const uint8_t *buffer, uint16_t count, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    TickType_t timeout_ticks;
    bool failed;

    if (!port_initialized || buffer == NULL || count == 0U)
    {
        return -1;
    }

    if (!rs485_prepare_transmit())
    {
        return -1;
    }

    taskENTER_CRITICAL();
    xSemaphoreTake(tx_completed_semaphore, 0U);
    tx_completed_successfully = false;
    phase = RS485_PHASE_TRANSMITTING;
    status = HAL_UART_Transmit_DMA(&huart3, buffer, count);
    if (status != HAL_OK)
    {
        phase = RS485_PHASE_IDLE;
    }
    taskEXIT_CRITICAL();

    if (status != HAL_OK)
    {
        return -1;
    }

    timeout_ticks = rs485_timeout_to_ticks(timeout_ms);
    if (xSemaphoreTake(tx_completed_semaphore, timeout_ticks) != pdTRUE)
    {
        rs485_cancel_transmit();
        return -1;
    }

    taskENTER_CRITICAL();
    failed = !tx_completed_successfully;
    taskEXIT_CRITICAL();

    if (failed)
    {
        rs485_cancel_transmit();
        return -1;
    }

    return (int32_t)count;
}
