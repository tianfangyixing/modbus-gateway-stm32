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
#include "main.h"
#include "configuration.h"

#define MAX_WAITING_REMAINING_RESPONSE_MS (5000)


typedef enum
{
    RS485_PHASE_IDLE = 0,
    RS485_PHASE_WAITING_TX_READY,
    RS485_PHASE_TRANSMITTING,
    RS485_PHASE_RECEIVING,
} rs485_phase_t;


static SemaphoreHandle_t send_completed_semaphore;
static StaticSemaphore_t send_completed_semaphore_buffer;


static SemaphoreHandle_t recv_completed_semaphore;
static StaticSemaphore_t recv_completed_semaphore_buffer;

static SemaphoreHandle_t first_byte_semaphore;
static StaticSemaphore_t first_byte_semaphore_buffer;


static volatile uint16_t rx_length = 0;
static volatile uint8_t *volatile rx_buffer;

static uint32_t baud_rate;
static uint8_t frame_format;
static volatile rs485_phase_t phase;

static volatile bool transmit_successful;
static volatile bool received_successful;

static uint32_t rtu_baud_rate_to_t35_us(uint32_t baud_rate)
{
    switch (baud_rate)
    {
    case UINT32_C(1200):
        return UINT32_C(32084);
    case UINT32_C(2400):
        return UINT32_C(16042);
    case UINT32_C(4800):
        return UINT32_C(8021);
    case UINT32_C(9600):
        return UINT32_C(4011);
    case UINT32_C(19200):
        return UINT32_C(2006);
    case UINT32_C(38400):
    case UINT32_C(57600):
    case UINT32_C(115200):
        return UINT32_C(1750);
    default:
        return UINT32_C(0);
    }
}

#define RS485_TX_TIMEOUT_GUARD_MS UINT32_C(500)

static TickType_t rs485_calculate_tx_timeout_ticks(uint16_t count)
{
    uint32_t bits_per_character;
    uint64_t transmission_ticks;
    uint64_t guard_ticks;

    switch (frame_format)
    {
    case CONFIGURATION_FRAME_FORMAT_8N1:
        bits_per_character = 10U;
        break;

    case CONFIGURATION_FRAME_FORMAT_8E1:
    case CONFIGURATION_FRAME_FORMAT_8O1:
    case CONFIGURATION_FRAME_FORMAT_8N2:
        bits_per_character = 11U;
        break;

    default:
        Error_Handler();
        return 0U;
    }

    transmission_ticks = ((uint64_t)count * bits_per_character * configTICK_RATE_HZ + baud_rate - 1U) / baud_rate;
    guard_ticks = ((uint64_t)RS485_TX_TIMEOUT_GUARD_MS * configTICK_RATE_HZ + UINT64_C(999)) / UINT64_C(1000);


    return (TickType_t)(transmission_ticks + guard_ticks);
}

static void timer_set(uint16_t timeout_us)
{
    if (timeout_us == 0U)
    {
        timeout_us = 1;
    }

    HAL_TIM_Base_Stop(&htim7);
    __HAL_TIM_SET_AUTORELOAD(&htim7, timeout_us - 1U);
    __HAL_TIM_SET_COUNTER(&htim7, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
    HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);

    if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK)
    {
        Error_Handler();
    }
}

static void timer_stop()
{
    HAL_TIM_Base_Stop(&htim7);
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
    HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);
}

void rs485_uart_tx_complete_callback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken1 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken2 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken3 = pdFALSE;

    transmit_successful = true;

    rx_length = 0;
    phase = RS485_PHASE_RECEIVING;
	__HAL_UART_CLEAR_OREFLAG(&huart3);
	if (HAL_UART_Receive_IT(&huart3, &rx_buffer[rx_length], 1) != HAL_OK)
    {
        received_successful = false;

        xSemaphoreGiveFromISR(first_byte_semaphore, &xHigherPriorityTaskWoken1);
        xSemaphoreGiveFromISR(recv_completed_semaphore, &xHigherPriorityTaskWoken2);
    }
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
    


    xSemaphoreGiveFromISR(send_completed_semaphore, &xHigherPriorityTaskWoken3);
    if(xHigherPriorityTaskWoken1 != pdFALSE || xHigherPriorityTaskWoken2 != pdFALSE || xHigherPriorityTaskWoken3 != pdFALSE)
    {
        portYIELD_FROM_ISR(pdTRUE);
    }
}

void rs485_uart_rx_complete_callback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken1 = false;
    rx_length++;

    if (rx_length == 1)
    {
        if(HAL_UART_Receive_IT(&huart3, &rx_buffer[rx_length], 1) != HAL_OK)
        {
            received_successful = false;
            BaseType_t xHigherPriorityTaskWoken2 = false;

            timer_stop();
            xSemaphoreGiveFromISR(first_byte_semaphore, &xHigherPriorityTaskWoken1);
            xSemaphoreGiveFromISR(recv_completed_semaphore, &xHigherPriorityTaskWoken2);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken1 == pdFALSE ? xHigherPriorityTaskWoken2 : xHigherPriorityTaskWoken1);
            return;
        }
        timer_set(rtu_baud_rate_to_t35_us(baud_rate));
        xSemaphoreGiveFromISR(first_byte_semaphore, &xHigherPriorityTaskWoken1);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken1);
    }
    else if (rx_length >= MODBUS_RTU_MAX_LENGTH)
    {
        timer_set(rtu_baud_rate_to_t35_us(baud_rate)); // 等待t3.5结束
    }
    else
    {
        if(HAL_UART_Receive_IT(&huart3, &rx_buffer[rx_length], 1) != HAL_OK)
        {
            received_successful = false;
            timer_stop();
            xSemaphoreGiveFromISR(recv_completed_semaphore, &xHigherPriorityTaskWoken1);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken1);
            return;
        }
        timer_set(rtu_baud_rate_to_t35_us(baud_rate));
    }
}

void rs485_uart_error_callback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (phase == RS485_PHASE_TRANSMITTING)
    {
        transmit_successful = false;
        xSemaphoreGiveFromISR(send_completed_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else if (phase == RS485_PHASE_RECEIVING)
    {
        timer_stop();
        HAL_UART_Abort(&huart3);
        BaseType_t xHigherPriorityTaskWoken1 = pdFALSE;
        BaseType_t xHigherPriorityTaskWoken2 = pdFALSE;
        received_successful = false;
        xSemaphoreGiveFromISR(first_byte_semaphore, &xHigherPriorityTaskWoken1);
        xSemaphoreGiveFromISR(recv_completed_semaphore, &xHigherPriorityTaskWoken2);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken1 == pdFALSE ? xHigherPriorityTaskWoken2 : xHigherPriorityTaskWoken1);
    }
}

/* tim7中断优先级与uart3相等 */
void rs485_t35_elapsed_callback(TIM_HandleTypeDef *htim)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if(phase == RS485_PHASE_WAITING_TX_READY)
    {
        xSemaphoreGiveFromISR(send_completed_semaphore, &xHigherPriorityTaskWoken);
    }
    else if(phase == RS485_PHASE_RECEIVING)
    {
        received_successful = true;
        HAL_UART_AbortReceive(&huart3);
        xSemaphoreGiveFromISR(recv_completed_semaphore, &xHigherPriorityTaskWoken);
    }

    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void modbus_rtu_rs485_port_init(uint32_t configured_baud_rate, uint8_t configured_format)
{
    send_completed_semaphore = xSemaphoreCreateBinaryStatic(&send_completed_semaphore_buffer);
    recv_completed_semaphore = xSemaphoreCreateBinaryStatic(&recv_completed_semaphore_buffer);
    first_byte_semaphore = xSemaphoreCreateBinaryStatic(&first_byte_semaphore_buffer);

    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    huart3.Init.BaudRate = configured_baud_rate;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    switch (configured_format)
    {
    case CONFIGURATION_FRAME_FORMAT_8N1:
        huart3.Init.WordLength = UART_WORDLENGTH_8B;
        huart3.Init.StopBits = UART_STOPBITS_1;
        huart3.Init.Parity = UART_PARITY_NONE;
        break;

    case CONFIGURATION_FRAME_FORMAT_8E1:
        huart3.Init.WordLength = UART_WORDLENGTH_9B;
        huart3.Init.StopBits = UART_STOPBITS_1;
        huart3.Init.Parity = UART_PARITY_EVEN;
        break;

    case CONFIGURATION_FRAME_FORMAT_8O1:
        huart3.Init.WordLength = UART_WORDLENGTH_9B;
        huart3.Init.StopBits = UART_STOPBITS_1;
        huart3.Init.Parity = UART_PARITY_ODD;
        break;

    case CONFIGURATION_FRAME_FORMAT_8N2:
        huart3.Init.WordLength = UART_WORDLENGTH_8B;
        huart3.Init.StopBits = UART_STOPBITS_2;
        huart3.Init.Parity = UART_PARITY_NONE;
        break;

    default:
        Error_Handler();
        return;
    }

    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        Error_Handler();
        return;
    }

    baud_rate = configured_baud_rate;
    frame_format = configured_format;

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
}


modbus_rtu_rs485_port_result_t modbus_rtu_rs485_port_transceive(void *context, const uint8_t *request, uint16_t request_length, uint8_t *response, uint32_t response_timeout_ms, uint16_t *receive_len)
{

    // flush
    xSemaphoreTake(send_completed_semaphore, 0U);
    xSemaphoreTake(recv_completed_semaphore, 0U);
    xSemaphoreTake(first_byte_semaphore, 0U);

    rx_buffer = response;

    // send
    phase = RS485_PHASE_WAITING_TX_READY;
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
    timer_set(50); // 等待50us，确保MAX485转换为发送状态
    if (xSemaphoreTake(send_completed_semaphore, 5) != pdTRUE)
    {
        timer_stop();
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
        phase = RS485_PHASE_IDLE;
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }
    phase = RS485_PHASE_TRANSMITTING;
    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart3, request, request_length);
    if (status != HAL_OK)
    {
        phase = RS485_PHASE_IDLE;
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }

    TickType_t timeout_ticks = rs485_calculate_tx_timeout_ticks(request_length);

    if (xSemaphoreTake(send_completed_semaphore, timeout_ticks) != pdTRUE)
    {
        HAL_UART_AbortTransmit(&huart3);
        HAL_UART_AbortReceive(&huart3);
        timer_stop();
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
        phase = RS485_PHASE_IDLE;
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }

    if (!transmit_successful)
    {
        phase = RS485_PHASE_IDLE;
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }

    // 等待接收，接收已在tc发出

    if (xSemaphoreTake(first_byte_semaphore, pdMS_TO_TICKS(response_timeout_ms)) != pdTRUE)
    {
        HAL_UART_AbortReceive(&huart3);
        timer_stop();
        phase = RS485_PHASE_IDLE;
        return MODBUS_RTU_RS485_PORT_RESULT_SLAVE_TIMEOUT;
    }

    if(xSemaphoreTake(recv_completed_semaphore, pdMS_TO_TICKS(MAX_WAITING_REMAINING_RESPONSE_MS)) != pdTRUE)
    {
        HAL_UART_AbortReceive(&huart3);
        timer_stop();
        phase = RS485_PHASE_IDLE;
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }

    phase = RS485_PHASE_IDLE;
    if(!received_successful)
    {
        return MODBUS_RTU_RS485_PORT_RESULT_UART_ERROR;
    }
    else
    {
        *receive_len = rx_length;
        return MODBUS_RTU_RS485_PORT_RESULT_OK;
    }

}