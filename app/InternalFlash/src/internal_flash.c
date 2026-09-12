#include "internal_flash.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32f4xx_hal.h"

static internal_flash_result_t internal_flash_result_from_hal(HAL_StatusTypeDef hal_status)
{
    switch (hal_status)
    {
        case HAL_OK:
            return INTERNAL_FLASH_RESULT_OK;
        case HAL_BUSY:
            return INTERNAL_FLASH_RESULT_BUSY;
        case HAL_TIMEOUT:
            return INTERNAL_FLASH_RESULT_TIMEOUT;
        case HAL_ERROR:
        default:
            return INTERNAL_FLASH_RESULT_ERROR;
    }
}

static bool internal_flash_range_is_valid(uint32_t start_addr, uint32_t length)
{
    return start_addr >= FLASH_BASE && start_addr <= FLASH_END &&
           length <= FLASH_END - start_addr + 1U;
}

internal_flash_result_t internal_flash_program(uint32_t start_addr, const uint8_t *source, uint32_t length)
{
    HAL_StatusTypeDef hal_status;

    if (source == NULL || length == 0U || !internal_flash_range_is_valid(start_addr, length))
    {
        return INTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        HAL_FLASH_Lock();
        return internal_flash_result_from_hal(hal_status);
    }

    while (length > 0U)
    {
        uint32_t program_type;
        uint32_t program_data;
        uint32_t program_size;

        if ((start_addr & 3U) == 0U && length >= 4U)
        {
            program_type = FLASH_TYPEPROGRAM_WORD;
            program_size = 4U;
            program_data = (uint32_t)source[0] | ((uint32_t)source[1] << 8U) |
                           ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
        }
        else if ((start_addr & 1U) == 0U && length >= 2U)
        {
            program_type = FLASH_TYPEPROGRAM_HALFWORD;
            program_size = 2U;
            program_data = (uint32_t)source[0] | ((uint32_t)source[1] << 8U);
        }
        else
        {
            program_type = FLASH_TYPEPROGRAM_BYTE;
            program_size = 1U;
            program_data = source[0];
        }

        hal_status = HAL_FLASH_Program(program_type, start_addr, program_data);
        if (hal_status != HAL_OK)
        {
            /* A failed write may have changed some bits: invalidate read caches. */
            FLASH_FlushCaches();
            HAL_FLASH_Lock();
            return internal_flash_result_from_hal(hal_status);
        }

        start_addr += program_size;
        source += program_size;
        length -= program_size;
    }

    FLASH_FlushCaches();
    return internal_flash_result_from_hal(HAL_FLASH_Lock());
}

internal_flash_result_t internal_flash_read(uint32_t start_addr, uint8_t *destination, uint32_t length)
{
    const volatile uint8_t *source;

    if (destination == NULL || length == 0U || !internal_flash_range_is_valid(start_addr, length))
    {
        return INTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }
    
    source = (const volatile uint8_t *)(uintptr_t)start_addr;
    while (length > 0U)
    {
        *destination++ = *source++;
        length--;
    }

    return INTERNAL_FLASH_RESULT_OK;
}

internal_flash_result_t internal_flash_erase(uint32_t sector, uint32_t count)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = UINT32_MAX;
    HAL_StatusTypeDef hal_status;

    if (sector >= FLASH_SECTOR_TOTAL || count == 0U || count > FLASH_SECTOR_TOTAL - sector)
    {
        return INTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        HAL_FLASH_Lock();
        return internal_flash_result_from_hal(hal_status);
    }

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = sector;
    erase_init.NbSectors = count;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    hal_status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

    /* HAL already flushes on normal completion; do so on error paths too. */
    FLASH_FlushCaches();
    {
        HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
        if (hal_status != HAL_OK)
        {
            return internal_flash_result_from_hal(hal_status);
        }
        if (sector_error != UINT32_MAX)
        {
            return INTERNAL_FLASH_RESULT_ERROR;
        }
        return internal_flash_result_from_hal(lock_status);
    }

}

internal_flash_result_t internal_flash_is_erased(uint32_t start_addr, uint32_t length, bool *erased)
{
    uint8_t buffer[64];
    if (erased == NULL || length == 0U || !internal_flash_range_is_valid(start_addr, length))
    {
        return INTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }
    *erased = false;
    while (length != 0U)
    {
        uint32_t count = length > sizeof(buffer) ? sizeof(buffer) : length;
        internal_flash_result_t result = internal_flash_read(start_addr, buffer, count);
        if (result != INTERNAL_FLASH_RESULT_OK)
        {
            return result;
        }
        for (uint32_t i = 0U; i < count; ++i)
        {
            if (buffer[i] != 0xFFU)
            {
                return INTERNAL_FLASH_RESULT_OK;
            }
        }
        start_addr += count;
        length -= count;
    }
    *erased = true;
    return INTERNAL_FLASH_RESULT_OK;
}
