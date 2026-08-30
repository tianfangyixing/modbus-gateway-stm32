#include "configuration_service_test_adapter.h"

#include "external_flash.h"

#include <stddef.h>
#include <string.h>

#define TEST_FLASH_BASE_ADDRESS UINT32_C(0x00FFC000)
#define TEST_FLASH_SIZE (CONFIGURATION_SERVICE_TEST_SLOT_SIZE * 2U)
#define TEST_SECTOR_SIZE UINT32_C(4096)
#define TEST_MAX_PROGRAM_CALLS UINT32_C(8)

typedef struct
{
    uint32_t address;
    uint32_t length;
} test_program_call_t;

static uint8_t flash_bytes[TEST_FLASH_SIZE];
static uint32_t failed_read_call;
static uint32_t failed_program_call;
static uint32_t failed_erase_call;
static uint32_t corrupted_read_call;
static uint32_t corrupted_read_offset;
static uint32_t read_calls;
static uint32_t program_calls;
static uint32_t erase_calls;
static test_program_call_t program_log[TEST_MAX_PROGRAM_CALLS];

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t byte_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        uint8_t bit_index;

        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ UINT32_C(0xEDB88320) : crc >> 1U;
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}

static bool map_range(uint32_t address, uint32_t length, uint32_t *offset)
{
    uint32_t relative_address;

    if (length == 0U || address < TEST_FLASH_BASE_ADDRESS)
    {
        return false;
    }

    relative_address = address - TEST_FLASH_BASE_ADDRESS;
    if (relative_address > TEST_FLASH_SIZE || length > TEST_FLASH_SIZE - relative_address)
    {
        return false;
    }

    *offset = relative_address;
    return true;
}

static uint32_t slot_offset(uint8_t slot)
{
    return slot == CONFIGURATION_SERVICE_TEST_SLOT_A ? 0U : CONFIGURATION_SERVICE_TEST_SLOT_SIZE;
}

void configuration_service_test_operation_reset(void)
{
    failed_read_call = 0U;
    failed_program_call = 0U;
    failed_erase_call = 0U;
    corrupted_read_call = 0U;
    corrupted_read_offset = 0U;
    read_calls = 0U;
    program_calls = 0U;
    erase_calls = 0U;
    memset(program_log, 0, sizeof(program_log));
}

void configuration_service_test_flash_reset(void)
{
    memset(flash_bytes, 0xFF, sizeof(flash_bytes));
    configuration_service_test_operation_reset();
}

bool configuration_service_test_seed_record(uint8_t slot, uint32_t magic, uint32_t generation,
                                            const uint8_t *payload, uint32_t payload_length)
{
    uint8_t *record;

    if (slot > CONFIGURATION_SERVICE_TEST_SLOT_B || payload == NULL || payload_length == 0U ||
        payload_length > CONFIGURATION_SERVICE_TEST_SLOT_SIZE - CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE)
    {
        return false;
    }

    record = &flash_bytes[slot_offset(slot)];
    memset(record, 0xFF, CONFIGURATION_SERVICE_TEST_SLOT_SIZE);
    write_u32_le(&record[0], magic);
    write_u32_le(&record[4], generation);
    write_u32_le(&record[8], payload_length);
    write_u32_le(&record[12], crc32(payload, payload_length));
    write_u32_le(&record[16], crc32(record, 16U));
    memcpy(&record[CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE], payload, payload_length);
    return true;
}

void configuration_service_test_corrupt_slot(uint8_t slot, uint32_t offset)
{
    if (slot <= CONFIGURATION_SERVICE_TEST_SLOT_B && offset < CONFIGURATION_SERVICE_TEST_SLOT_SIZE)
    {
        flash_bytes[slot_offset(slot) + offset] ^= UINT8_C(1);
    }
}

void configuration_service_test_fail_read(uint32_t call_index)
{
    failed_read_call = call_index;
}

void configuration_service_test_fail_program(uint32_t call_index)
{
    failed_program_call = call_index;
}

void configuration_service_test_fail_erase(uint32_t call_index)
{
    failed_erase_call = call_index;
}

void configuration_service_test_corrupt_read(uint32_t call_index, uint32_t relative_offset)
{
    corrupted_read_call = call_index;
    corrupted_read_offset = relative_offset;
}

uint32_t configuration_service_test_read_count(void)
{
    return read_calls;
}

uint32_t configuration_service_test_program_count(void)
{
    return program_calls;
}

uint32_t configuration_service_test_erase_count(void)
{
    return erase_calls;
}

uint32_t configuration_service_test_program_address(uint32_t call_index)
{
    return call_index > 0U && call_index <= TEST_MAX_PROGRAM_CALLS ? program_log[call_index - 1U].address : 0U;
}

uint32_t configuration_service_test_program_length(uint32_t call_index)
{
    return call_index > 0U && call_index <= TEST_MAX_PROGRAM_CALLS ? program_log[call_index - 1U].length : 0U;
}

uint32_t configuration_service_test_slot_generation(uint8_t slot)
{
    return slot <= CONFIGURATION_SERVICE_TEST_SLOT_B ? read_u32_le(&flash_bytes[slot_offset(slot) + 4U]) : 0U;
}

external_flash_result_t external_flash_init(void)
{
    return EXTERNAL_FLASH_RESULT_OK;
}

external_flash_result_t external_flash_program(uint32_t start_addr, const uint8_t *source, uint32_t length)
{
    uint32_t offset;
    uint32_t index;
    uint32_t programmed_length = length;

    if (source == NULL || !map_range(start_addr, length, &offset))
    {
        return EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }

    program_calls++;
    if (program_calls <= TEST_MAX_PROGRAM_CALLS)
    {
        program_log[program_calls - 1U].address = start_addr;
        program_log[program_calls - 1U].length = length;
    }

    if (program_calls == failed_program_call)
    {
        programmed_length = length > 1U ? length / 2U : 0U;
    }

    for (index = 0U; index < programmed_length; index++)
    {
        flash_bytes[offset + index] &= source[index];
    }

    return program_calls == failed_program_call ? EXTERNAL_FLASH_RESULT_IO_ERROR : EXTERNAL_FLASH_RESULT_OK;
}

external_flash_result_t external_flash_read(uint32_t start_addr, uint8_t *destination, uint32_t length)
{
    uint32_t offset;

    if (destination == NULL || !map_range(start_addr, length, &offset))
    {
        return EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }

    read_calls++;
    if (read_calls == failed_read_call)
    {
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }

    if (read_calls == corrupted_read_call && corrupted_read_offset < length)
    {
        flash_bytes[offset + corrupted_read_offset] ^= UINT8_C(1);
    }

    memcpy(destination, &flash_bytes[offset], length);
    return EXTERNAL_FLASH_RESULT_OK;
}

external_flash_result_t external_flash_erase_4k(uint32_t sector_address)
{
    uint32_t offset;

    if (sector_address % TEST_SECTOR_SIZE != 0U || !map_range(sector_address, TEST_SECTOR_SIZE, &offset))
    {
        return EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT;
    }

    erase_calls++;
    if (erase_calls == failed_erase_call)
    {
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }

    memset(&flash_bytes[offset], 0xFF, TEST_SECTOR_SIZE);
    return EXTERNAL_FLASH_RESULT_OK;
}
