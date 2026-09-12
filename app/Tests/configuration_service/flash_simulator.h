#ifndef CONFIGURATION_SERVICE_FLASH_SIMULATOR_H
#define CONFIGURATION_SERVICE_FLASH_SIMULATOR_H

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>

#define STORAGE_SLOT_A UINT32_C(0x00FFA000)
#define STORAGE_SLOT_B UINT32_C(0x00FFD000)
#define STORAGE_SLOT_SIZE UINT32_C(12288)
#define STORAGE_WORKSPACE_SIZE UINT32_C(8495)
#define STORAGE_FAULT_COMPLETE UINT32_MAX

typedef enum
{
    STORAGE_READ,
    STORAGE_PROGRAM,
    STORAGE_ERASE
} storage_operation_t;

typedef struct
{
    storage_operation_t operation;
    uint32_t address;
    uint32_t length;
    const uint8_t *buffer;
} storage_flash_event_t;

extern jmp_buf storage_flash_power_loss;

void storage_flash_initialize(void);
void storage_flash_clear_log(void);
void storage_flash_disable_fault(void);
void storage_flash_set_fault(uint32_t call_number, uint32_t completed_bytes, bool power_cut);
void storage_flash_corrupt_read(uint32_t byte_offset);
uint32_t storage_flash_event_count(void);
const storage_flash_event_t *storage_flash_event(uint32_t index);
uint8_t *storage_flash_bytes(uint32_t address, uint32_t length);

/* Production test seam; compile configuration_service.c with CONFIGURATION_SERVICE_TEST. */
void configuration_service_test_reset(void);

#endif
