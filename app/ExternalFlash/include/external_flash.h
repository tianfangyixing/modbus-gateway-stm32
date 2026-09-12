#ifndef EXTERNAL_FLASH_H
#define EXTERNAL_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    EXTERNAL_FLASH_RESULT_OK = 0,
    EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT,
    EXTERNAL_FLASH_RESULT_NOT_INITIALIZED,
    EXTERNAL_FLASH_RESULT_OUT_OF_RANGE,
    EXTERNAL_FLASH_RESULT_BUSY,
    EXTERNAL_FLASH_RESULT_TIMEOUT,
    EXTERNAL_FLASH_RESULT_IO_ERROR
} external_flash_result_t;

external_flash_result_t external_flash_init(void);
external_flash_result_t external_flash_program(uint32_t start_addr, const uint8_t *source, uint32_t length);
external_flash_result_t external_flash_read(uint32_t start_addr, uint8_t *destination, uint32_t length);
external_flash_result_t external_flash_erase_4k(uint32_t sector_address);

#ifdef __cplusplus
}
#endif

#endif /* EXTERNAL_FLASH_H */
