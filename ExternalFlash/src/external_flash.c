#include "external_flash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "main.h"
#include "spi.h"

#define EXTERNAL_FLASH_CAPACITY UINT32_C(0x01000000)
#define EXTERNAL_FLASH_PAGE_SIZE UINT32_C(256)
#define EXTERNAL_FLASH_SECTOR_SIZE UINT32_C(4096)
#define EXTERNAL_FLASH_DMA_MAX_TRANSFER UINT32_C(65535)

#define EXTERNAL_FLASH_INSTRUCTION_WRITE_ENABLE UINT8_C(0x06)
#define EXTERNAL_FLASH_INSTRUCTION_READ_STATUS_1 UINT8_C(0x05)
#define EXTERNAL_FLASH_INSTRUCTION_READ_DATA UINT8_C(0x03)
#define EXTERNAL_FLASH_INSTRUCTION_PAGE_PROGRAM UINT8_C(0x02)
#define EXTERNAL_FLASH_INSTRUCTION_SECTOR_ERASE_4K UINT8_C(0x20)

#define EXTERNAL_FLASH_STATUS_BUSY UINT8_C(0x01)

#define EXTERNAL_FLASH_CONTROL_TIMEOUT_MS UINT32_C(10)
#define EXTERNAL_FLASH_DMA_TIMEOUT_MS UINT32_C(100)
#define EXTERNAL_FLASH_READY_TIMEOUT_MS UINT32_C(1000)
#define EXTERNAL_FLASH_READY_POLL_DELAY_MS UINT32_C(2)

static SemaphoreHandle_t transfer_semaphore;
static StaticSemaphore_t transfer_semaphore_buffer;
static volatile external_flash_result_t transfer_result;

static bool external_flash_initialized;

static external_flash_result_t external_flash_result_from_hal(HAL_StatusTypeDef hal_status)
{
    switch (hal_status)
    {
        case HAL_OK:
            return EXTERNAL_FLASH_RESULT_OK;
        case HAL_BUSY:
            return EXTERNAL_FLASH_RESULT_BUSY;
        case HAL_TIMEOUT:
            return EXTERNAL_FLASH_RESULT_TIMEOUT;
        case HAL_ERROR:
        default:
            return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }
}

static external_flash_result_t external_flash_transmit_polling(const uint8_t *source, uint16_t length)
{
    HAL_StatusTypeDef hal_status = HAL_SPI_Transmit(&hspi1, source, length, EXTERNAL_FLASH_CONTROL_TIMEOUT_MS);
    return external_flash_result_from_hal(hal_status);
}

static external_flash_result_t external_flash_send_address_command(uint8_t instruction, uint32_t address)
{
    uint8_t command[4];

    command[0] = instruction;
    command[1] = (uint8_t)(address >> 16U);
    command[2] = (uint8_t)(address >> 8U);
    command[3] = (uint8_t)address;
    return external_flash_transmit_polling(command, (uint16_t)sizeof(command));
}

static external_flash_result_t external_flash_write_enable(void)
{
    uint8_t instruction = EXTERNAL_FLASH_INSTRUCTION_WRITE_ENABLE;
    external_flash_result_t result;

    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_RESET);
    result = external_flash_transmit_polling(&instruction, 1U);
    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_SET);
    return result;
}

static external_flash_result_t external_flash_read_status_1(uint8_t *status_register)
{
    uint8_t transmit_buffer[2] =
    {
        EXTERNAL_FLASH_INSTRUCTION_READ_STATUS_1,
        UINT8_C(0xFF)
    };
    uint8_t receive_buffer[2] =
    {
        0U,
        0U
    };
    HAL_StatusTypeDef hal_status;

    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_RESET);
    hal_status = HAL_SPI_TransmitReceive(&hspi1, transmit_buffer, receive_buffer, (uint16_t)sizeof(transmit_buffer),
                                        EXTERNAL_FLASH_CONTROL_TIMEOUT_MS);
    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_SET);
    if (hal_status != HAL_OK)
    {
        return external_flash_result_from_hal(hal_status);
    }

    *status_register = receive_buffer[1];
    return EXTERNAL_FLASH_RESULT_OK;
}

static external_flash_result_t external_flash_wait_ready(void)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(EXTERNAL_FLASH_READY_TIMEOUT_MS);

    while (true)
    {
        external_flash_result_t result;
        uint8_t status_register;

        result = external_flash_read_status_1(&status_register);
        if (result != EXTERNAL_FLASH_RESULT_OK)
        {
            return result;
        }
        if ((status_register & EXTERNAL_FLASH_STATUS_BUSY) == 0U)
        {
            return EXTERNAL_FLASH_RESULT_OK;
        }
        if (xTaskGetTickCount() - start_ticks >= timeout_ticks)
        {
            return EXTERNAL_FLASH_RESULT_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(EXTERNAL_FLASH_READY_POLL_DELAY_MS));
    }
}

static external_flash_result_t external_flash_transmit_dma(const uint8_t *source, uint16_t length)
{
    HAL_StatusTypeDef hal_status;

    xSemaphoreTake(transfer_semaphore, 0);

    hal_status = HAL_SPI_Transmit_DMA(&hspi1, source, length);
    if(hal_status != HAL_OK)
    {
        return external_flash_result_from_hal(hal_status);
    }
    
    if(xSemaphoreTake(transfer_semaphore, pdMS_TO_TICKS(EXTERNAL_FLASH_DMA_TIMEOUT_MS)) == pdTRUE)
    {
        return transfer_result;
    }
    else
    {
        HAL_SPI_DMAStop(&hspi1);
        return EXTERNAL_FLASH_RESULT_TIMEOUT;
    }
}

static external_flash_result_t external_flash_receive_dma(uint8_t *destination, uint16_t length)
{
    HAL_StatusTypeDef hal_status;

    xSemaphoreTake(transfer_semaphore, 0);

    hal_status = HAL_SPI_Receive_DMA(&hspi1, destination, length);
    if(hal_status != HAL_OK)
    {
        return external_flash_result_from_hal(hal_status);
    }
    
    if(xSemaphoreTake(transfer_semaphore, pdMS_TO_TICKS(EXTERNAL_FLASH_DMA_TIMEOUT_MS)) == pdTRUE)
    {
        return transfer_result;
    }
    else
    {
        HAL_SPI_DMAStop(&hspi1);
        return EXTERNAL_FLASH_RESULT_TIMEOUT;
    }
}

static bool external_flash_range_is_valid(uint32_t start_addr, uint32_t length)
{
    return start_addr < EXTERNAL_FLASH_CAPACITY && length <= EXTERNAL_FLASH_CAPACITY - start_addr;
}

static void external_flash_signal_transfer_complete_from_isr(external_flash_result_t result)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    transfer_result = result;

    if (xSemaphoreGiveFromISR(transfer_semaphore, &higher_priority_task_woken) == pdTRUE)
    {
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static void external_flash_transfer_complete_callback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1)
    {
        external_flash_signal_transfer_complete_from_isr(EXTERNAL_FLASH_RESULT_OK);
    }
}

static void external_flash_transfer_error_callback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1)
    {
        external_flash_signal_transfer_complete_from_isr(EXTERNAL_FLASH_RESULT_IO_ERROR);
    }
}



external_flash_result_t external_flash_init(void)
{
    if (external_flash_initialized)
    {
        return EXTERNAL_FLASH_RESULT_OK;
    }

    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_SET);

    transfer_semaphore = xSemaphoreCreateBinaryStatic(&transfer_semaphore_buffer);

    if (HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_COMPLETE_CB_ID, external_flash_transfer_complete_callback) != HAL_OK)
    {
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }

    if (HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_RX_COMPLETE_CB_ID, external_flash_transfer_complete_callback) != HAL_OK)
    {
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }

    if (HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_RX_COMPLETE_CB_ID, external_flash_transfer_complete_callback) != HAL_OK)
    {
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }

    if (HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_ERROR_CB_ID, external_flash_transfer_error_callback) != HAL_OK)
    {
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }

    external_flash_initialized = true;
    return EXTERNAL_FLASH_RESULT_OK;
}

external_flash_result_t external_flash_program(uint32_t start_addr, const uint8_t *source, uint32_t length)
{
    external_flash_result_t result;
    uint32_t remaining = length;

    if (!external_flash_initialized)
    {
        return EXTERNAL_FLASH_RESULT_NOT_INITIALIZED;
    }
    if (source == NULL || length == 0U)
    {
        return EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }
    if (!external_flash_range_is_valid(start_addr, length))
    {
        return EXTERNAL_FLASH_RESULT_OUT_OF_RANGE;
    }

    result = external_flash_wait_ready();

    while (result == EXTERNAL_FLASH_RESULT_OK && remaining > 0U)
    {
        uint32_t page_remaining = EXTERNAL_FLASH_PAGE_SIZE - (start_addr & (EXTERNAL_FLASH_PAGE_SIZE - 1U));
        uint32_t transfer_length = remaining < page_remaining ? remaining : page_remaining;

        result = external_flash_write_enable();
        if (result != EXTERNAL_FLASH_RESULT_OK)
        {
            break;
        }

        HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_RESET);
        result = external_flash_send_address_command(EXTERNAL_FLASH_INSTRUCTION_PAGE_PROGRAM, start_addr);
        if (result == EXTERNAL_FLASH_RESULT_OK)
        {
            result = external_flash_transmit_dma(source, (uint16_t)transfer_length);
        }
        HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_SET);

        if (result != EXTERNAL_FLASH_RESULT_OK)
        {
            break;
        }

        result = external_flash_wait_ready();
        start_addr += transfer_length;
        source += transfer_length;
        remaining -= transfer_length;
    }

    return result;
}

external_flash_result_t external_flash_read(uint32_t start_addr, uint8_t *destination, uint32_t length)
{
    external_flash_result_t result;
    uint32_t remaining = length;

    if (!external_flash_initialized)
    {
        return EXTERNAL_FLASH_RESULT_NOT_INITIALIZED;
    }
    if (destination == NULL || length == 0U)
    {
        return EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }
    if (!external_flash_range_is_valid(start_addr, length))
    {
        return EXTERNAL_FLASH_RESULT_OUT_OF_RANGE;
    }

    result = external_flash_wait_ready();
    if (result != EXTERNAL_FLASH_RESULT_OK)
    {
        return result;
    }

    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_RESET);
    result = external_flash_send_address_command(EXTERNAL_FLASH_INSTRUCTION_READ_DATA, start_addr);
    while (result == EXTERNAL_FLASH_RESULT_OK && remaining > 0U)
    {
        uint32_t transfer_length = remaining < EXTERNAL_FLASH_DMA_MAX_TRANSFER
                                       ? remaining
                                       : EXTERNAL_FLASH_DMA_MAX_TRANSFER;

        result = external_flash_receive_dma(destination, (uint16_t)transfer_length);
        destination += transfer_length;
        remaining -= transfer_length;
    }
    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_SET);

    return result;
}

external_flash_result_t external_flash_erase_4k(uint32_t sector_address)
{
    external_flash_result_t result;

    if (!external_flash_initialized)
    {
        return EXTERNAL_FLASH_RESULT_NOT_INITIALIZED;
    }
    if (sector_address > EXTERNAL_FLASH_CAPACITY - EXTERNAL_FLASH_SECTOR_SIZE)
    {
        return EXTERNAL_FLASH_RESULT_OUT_OF_RANGE;
    }
    if ((sector_address & (EXTERNAL_FLASH_SECTOR_SIZE - 1U)) != 0U)
    {
        return EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }

    result = external_flash_wait_ready();
    if (result != EXTERNAL_FLASH_RESULT_OK)
    {
        return result;
    }

    result = external_flash_write_enable();
    if (result != EXTERNAL_FLASH_RESULT_OK)
    {
        return result;
    }

    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_RESET);
    result = external_flash_send_address_command(EXTERNAL_FLASH_INSTRUCTION_SECTOR_ERASE_4K, sector_address);
    HAL_GPIO_WritePin(W25_CS_GPIO_Port, W25_CS_Pin, GPIO_PIN_SET);
    if (result != EXTERNAL_FLASH_RESULT_OK)
    {
        return result;
    }

    return external_flash_wait_ready();
}
