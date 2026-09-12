#ifndef __INTERNAL_FLASH_H
#define __INTERNAL_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    INTERNAL_FLASH_RESULT_OK = 0,
    INTERNAL_FLASH_RESULT_INVALID_ARGUMENT,
    INTERNAL_FLASH_RESULT_BUSY,
    INTERNAL_FLASH_RESULT_TIMEOUT,
    INTERNAL_FLASH_RESULT_ERROR
} internal_flash_result_t;

/* STM32F407ZGT6 main Flash: 0x08000000..0x080FFFFF, sectors 0..11.
 * The caller is responsible for partition boundaries and update sequencing.
 * These blocking operations belong in the main loop (single Flash bank).
 */
internal_flash_result_t internal_flash_program(uint32_t start_addr, const uint8_t *source, uint32_t length);

internal_flash_result_t internal_flash_read(uint32_t start_addr, uint8_t *destination, uint32_t length);

internal_flash_result_t internal_flash_erase(uint32_t sector, uint32_t count);

internal_flash_result_t internal_flash_is_erased(uint32_t start_addr, uint32_t length, bool *erased);

#ifdef __cplusplus
}
#endif

#endif /* INTERNAL_FLASH_H */ 
