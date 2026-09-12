#include "flash_simulator.h"
#include "external_flash.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

#define STORAGE_EVENT_CAPACITY UINT32_C(128)
#define STORAGE_FLASH_END UINT32_C(0x01000000)

static uint8_t flash_bytes[2U * STORAGE_SLOT_SIZE];
static storage_flash_event_t events[STORAGE_EVENT_CAPACITY];
static uint32_t event_count;
static uintptr_t workspace_base;
static uint32_t fault_call;
static uint32_t fault_completed;
static bool fault_power_cut;
static bool corrupt_read;
static uint32_t corrupt_read_offset;
jmp_buf storage_flash_power_loss;

void storage_flash_disable_fault(void)
{
    fault_call = 0U;
    fault_completed = 0U;
    fault_power_cut = false;
    corrupt_read = false;
    corrupt_read_offset = 0U;
}

void storage_flash_clear_log(void)
{
    memset(events, 0, sizeof(events));
    event_count = 0U;
}

void storage_flash_initialize(void)
{
    memset(flash_bytes, 0xFF, sizeof(flash_bytes));
    workspace_base = 0U;
    storage_flash_clear_log();
    storage_flash_disable_fault();
}

void storage_flash_set_fault(uint32_t call_number, uint32_t completed_bytes, bool power_cut)
{
    TEST_ASSERT_GREATER_THAN_UINT32(0U, call_number);
    fault_call = call_number;
    fault_completed = completed_bytes;
    fault_power_cut = power_cut;
}

void storage_flash_corrupt_read(uint32_t byte_offset)
{
    corrupt_read = true;
    corrupt_read_offset = byte_offset;
}

uint32_t storage_flash_event_count(void)
{
    return event_count;
}

const storage_flash_event_t *storage_flash_event(uint32_t index)
{
    TEST_ASSERT_LESS_THAN_UINT32(event_count, index);
    return &events[index];
}

uint8_t *storage_flash_bytes(uint32_t address, uint32_t length)
{
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(STORAGE_SLOT_A, address);
    TEST_ASSERT_LESS_THAN_UINT32(STORAGE_FLASH_END, address);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, length);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(STORAGE_FLASH_END - address, length);
    return &flash_bytes[address - STORAGE_SLOT_A];
}

static void log_operation(storage_operation_t operation, uint32_t address, uint32_t length, const uint8_t *buffer)
{
    TEST_ASSERT_LESS_THAN_UINT32(STORAGE_EVENT_CAPACITY, event_count);
    if (buffer != NULL)
    {
        uintptr_t pointer = (uintptr_t)buffer;

        if (workspace_base == 0U)
        {
            TEST_ASSERT_EQUAL(STORAGE_READ, operation);
            TEST_ASSERT_EQUAL_HEX32(STORAGE_SLOT_A, address);
            TEST_ASSERT_EQUAL_UINT32(20U, length);
            TEST_ASSERT_EQUAL_UINT32(0U, pointer % 4U);
            workspace_base = pointer;
        }
        TEST_ASSERT_TRUE(pointer >= workspace_base);
        TEST_ASSERT_TRUE(pointer - workspace_base < STORAGE_WORKSPACE_SIZE);
        TEST_ASSERT_TRUE(length <= STORAGE_WORKSPACE_SIZE - (pointer - workspace_base));
    }
    events[event_count].operation = operation;
    events[event_count].address = address;
    events[event_count].length = length;
    events[event_count].buffer = buffer;
    event_count++;
}

static uint32_t transfer_length(uint32_t requested)
{
    if (event_count == fault_call && fault_completed < requested)
    {
        return fault_completed;
    }
    return requested;
}

static external_flash_result_t finish_operation(void)
{
    if (event_count == fault_call)
    {
        if (fault_power_cut)
        {
            longjmp(storage_flash_power_loss, 1);
        }
        return EXTERNAL_FLASH_RESULT_IO_ERROR;
    }
    return EXTERNAL_FLASH_RESULT_OK;
}

external_flash_result_t external_flash_init(void)
{
    return EXTERNAL_FLASH_RESULT_OK;
}

external_flash_result_t external_flash_read(uint32_t address, uint8_t *destination, uint32_t length)
{
    uint8_t *source = storage_flash_bytes(address, length);

    TEST_ASSERT_NOT_NULL(destination);
    log_operation(STORAGE_READ, address, length, destination);
    memcpy(destination, source, transfer_length(length));
    if (corrupt_read)
    {
        TEST_ASSERT_LESS_THAN_UINT32(length, corrupt_read_offset);
        destination[corrupt_read_offset] ^= 1U;
        corrupt_read = false;
    }
    return finish_operation();
}

external_flash_result_t external_flash_program(uint32_t address, const uint8_t *source, uint32_t length)
{
    uint8_t *destination = storage_flash_bytes(address, length);
    uint32_t completed;

    TEST_ASSERT_NOT_NULL(source);
    log_operation(STORAGE_PROGRAM, address, length, source);
    completed = transfer_length(length);
    for (uint32_t index = 0U; index < completed; index++)
    {
        TEST_ASSERT_EQUAL_HEX8(source[index], destination[index] & source[index]);
        destination[index] &= source[index];
    }
    return finish_operation();
}

external_flash_result_t external_flash_erase_4k(uint32_t address)
{
    uint8_t *destination = storage_flash_bytes(address, 4096U);

    TEST_ASSERT_EQUAL_UINT32(0U, address % 4096U);
    log_operation(STORAGE_ERASE, address, 4096U, NULL);
    memset(destination, 0xFF, transfer_length(4096U));
    return finish_operation();
}
