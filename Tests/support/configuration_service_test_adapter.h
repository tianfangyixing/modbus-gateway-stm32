#ifndef CONFIGURATION_SERVICE_TEST_ADAPTER_H
#define CONFIGURATION_SERVICE_TEST_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIGURATION_SERVICE_TEST_MAGIC UINT32_C(0x57753102)
#define CONFIGURATION_SERVICE_TEST_OLD_MAGIC UINT32_C(0x57753101)
#define CONFIGURATION_SERVICE_TEST_SLOT_A UINT8_C(0)
#define CONFIGURATION_SERVICE_TEST_SLOT_B UINT8_C(1)
#define CONFIGURATION_SERVICE_TEST_SLOT_A_ADDRESS UINT32_C(0x00FFC000)
#define CONFIGURATION_SERVICE_TEST_SLOT_B_ADDRESS UINT32_C(0x00FFE000)
#define CONFIGURATION_SERVICE_TEST_SLOT_SIZE UINT32_C(8192)
#define CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE UINT32_C(20)

void configuration_service_test_reset(void);

void configuration_service_test_flash_reset(void);
void configuration_service_test_operation_reset(void);
bool configuration_service_test_seed_record(uint8_t slot, uint32_t magic, uint32_t generation,
                                            const uint8_t *payload, uint32_t payload_length);
void configuration_service_test_corrupt_slot(uint8_t slot, uint32_t offset);

void configuration_service_test_fail_read(uint32_t call_index);
void configuration_service_test_fail_program(uint32_t call_index);
void configuration_service_test_fail_erase(uint32_t call_index);
void configuration_service_test_corrupt_read(uint32_t call_index, uint32_t relative_offset);

uint32_t configuration_service_test_read_count(void);
uint32_t configuration_service_test_program_count(void);
uint32_t configuration_service_test_erase_count(void);
uint32_t configuration_service_test_program_address(uint32_t call_index);
uint32_t configuration_service_test_program_length(uint32_t call_index);
uint32_t configuration_service_test_slot_generation(uint8_t slot);

#endif
