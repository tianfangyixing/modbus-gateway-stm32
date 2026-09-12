#include "configuration_service.h"
#include "configuration_binary_codec.h"
#include "flash_simulator.h"
#include "storage_record_fixtures.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

static uint8_t encoded_active[8475];
static uint8_t saved_old_slot[12288];
static uint32_t fault_scenarios;

int debug_log_printf(const char *format, ...)
{
    return format == NULL ? -1 : 0;
}

void setUp(void)
{
    configuration_service_test_reset();
    storage_flash_initialize();
}

void tearDown(void)
{
    storage_flash_disable_fault();
}

static void seed_record(uint32_t address, const uint8_t *record, uint32_t length)
{
    memcpy(storage_flash_bytes(address, length), record, length);
}

static void expect_active(const uint8_t *expected, uint32_t expected_length)
{
    uint32_t length = 0U;
    const configuration_t *active = configuration_service_active();

    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL(CONFIGURATION_BINARY_CODEC_OK,
                      configuration_binary_encode(active, encoded_active, sizeof(encoded_active), &length));
    TEST_ASSERT_EQUAL_UINT32(expected_length, length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded_active, length);
}

static void expect_event(uint32_t index, storage_operation_t operation, uint32_t address, uint32_t length)
{
    const storage_flash_event_t *event = storage_flash_event(index);

    TEST_ASSERT_EQUAL(operation, event->operation);
    TEST_ASSERT_EQUAL_HEX32(address, event->address);
    TEST_ASSERT_EQUAL_UINT32(length, event->length);
}

static void reboot(void)
{
    storage_flash_disable_fault();
    storage_flash_clear_log();
    configuration_service_test_reset();
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
}

static void expect_default_header_reads(void)
{
    TEST_ASSERT_EQUAL_UINT32(2U, storage_flash_event_count());
    expect_event(0U, STORAGE_READ, 0x00FFA000U, 20U);
    expect_event(1U, STORAGE_READ, 0x00FFD000U, 20U);
    expect_active(default_payload, 48U);
}

static void test_empty_flash_and_init_idempotence(void)
{
    TEST_ASSERT_NULL(configuration_service_active());
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_NOT_INITIALIZED,
                      configuration_service_write(default_payload, 48U));
    TEST_ASSERT_EQUAL_UINT32(0U, storage_flash_event_count());
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    expect_default_header_reads();
    TEST_ASSERT_EQUAL(CONFIGURATION_MQTT_MODE_DISABLED, configuration_service_active()->mqtt.mode);
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    TEST_ASSERT_EQUAL_UINT32(2U, storage_flash_event_count());
}

static void test_maximum_write_restart_and_active_immutability(void)
{
    const configuration_t *active;

    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    active = configuration_service_active();
    storage_flash_clear_log();
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_write(maximum_payload, 8475U));
    TEST_ASSERT_EQUAL_PTR(active, configuration_service_active());
    expect_active(default_payload, 48U);
    TEST_ASSERT_EQUAL_UINT32(6U, storage_flash_event_count());
    expect_event(0U, STORAGE_ERASE, 0x00FFA000U, 4096U);
    expect_event(1U, STORAGE_ERASE, 0x00FFB000U, 4096U);
    expect_event(2U, STORAGE_ERASE, 0x00FFC000U, 4096U);
    expect_event(3U, STORAGE_PROGRAM, 0x00FFA004U, 8491U);
    expect_event(4U, STORAGE_PROGRAM, 0x00FFA000U, 4U);
    expect_event(5U, STORAGE_READ, 0x00FFA000U, 8495U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(maximum_record_0, storage_flash_bytes(STORAGE_SLOT_A, 8495U), 8495U);
    for (uint32_t index = 8495U; index < 12288U; index++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFFU, *storage_flash_bytes(STORAGE_SLOT_A + index, 1U));
    }
    reboot();
    expect_active(maximum_payload, 8475U);
    TEST_ASSERT_EQUAL_UINT32(3U, storage_flash_event_count());
    expect_event(0U, STORAGE_READ, 0x00FFA000U, 20U);
    expect_event(1U, STORAGE_READ, 0x00FFA014U, 8475U);
    expect_event(2U, STORAGE_READ, 0x00FFD000U, 20U);

    storage_flash_clear_log();
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_write(alternate_payload, 48U));
    expect_active(maximum_payload, 8475U);
    expect_event(0U, STORAGE_ERASE, 0x00FFD000U, 4096U);
    expect_event(1U, STORAGE_ERASE, 0x00FFE000U, 4096U);
    expect_event(2U, STORAGE_ERASE, 0x00FFF000U, 4096U);
    expect_event(3U, STORAGE_PROGRAM, 0x00FFD004U, 64U);
    expect_event(4U, STORAGE_PROGRAM, 0x00FFD000U, 4U);
    expect_event(5U, STORAGE_READ, 0x00FFD000U, 68U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(alternate_record_1, storage_flash_bytes(STORAGE_SLOT_B, 68U), 68U);
    reboot();
    expect_active(alternate_payload, 48U);
}

static void test_legacy_addresses_magic_and_schema_are_rejected(void)
{
    seed_record(0x00FFC000U, legacy_record, sizeof(legacy_record));
    seed_record(0x00FFE000U, legacy_record, sizeof(legacy_record));
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    expect_default_header_reads();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(legacy_record, storage_flash_bytes(0x00FFC000U, 68U), 68U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(legacy_record, storage_flash_bytes(0x00FFE000U, 68U), 68U);

    seed_record(STORAGE_SLOT_A, legacy_record, sizeof(legacy_record));
    seed_record(STORAGE_SLOT_B, legacy_record, sizeof(legacy_record));
    reboot();
    expect_default_header_reads();
    seed_record(STORAGE_SLOT_A, legacy_in_cfg2_record, sizeof(legacy_in_cfg2_record));
    reboot();
    expect_active(default_payload, 48U);
    TEST_ASSERT_EQUAL_UINT32(3U, storage_flash_event_count());
    expect_event(1U, STORAGE_READ, 0x00FFA014U, 48U);
}

static void test_invalid_header_rejected_before_payload_read(void)
{
    const uint8_t *headers[] =
    {
        length_0_header,
        length_8476_header,
        length_12268_header,
        length_4294967295_header,
        invalid_generation_header
    };

    for (uint32_t index = 0U; index < sizeof(headers) / sizeof(headers[0]); index++)
    {
        storage_flash_initialize();
        seed_record(STORAGE_SLOT_A, headers[index], 20U);
        seed_record(STORAGE_SLOT_B, headers[index], 20U);
        reboot();
        expect_default_header_reads();
    }
}

static void test_header_and_payload_crc_corruption_recovers_old_slot(void)
{
    seed_record(STORAGE_SLOT_A, maximum_record_0, sizeof(maximum_record_0));
    seed_record(STORAGE_SLOT_B, alternate_record_1, sizeof(alternate_record_1));
    *storage_flash_bytes(STORAGE_SLOT_B + 12U, 1U) ^= 1U;
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    expect_active(maximum_payload, 8475U);
    TEST_ASSERT_EQUAL_UINT32(3U, storage_flash_event_count());

    seed_record(STORAGE_SLOT_B, alternate_record_1, sizeof(alternate_record_1));
    *storage_flash_bytes(STORAGE_SLOT_B + 29U, 1U) ^= 1U;
    reboot();
    expect_active(maximum_payload, 8475U);
    TEST_ASSERT_EQUAL_UINT32(4U, storage_flash_event_count());
    *storage_flash_bytes(STORAGE_SLOT_A + 20U, 1U) ^= 1U;
    reboot();
    expect_active(default_payload, 48U);
}

static void test_generation_selection_all_pairs(void)
{
    const uint8_t *records_a[] =
    {
        default_record_0, default_record_1, default_record_2
    };
    const uint8_t *records_b[] =
    {
        alternate_record_0, alternate_record_1, alternate_record_2
    };
    const bool choose_b[3][3] =
    {
        {true, true, false},
        {false, true, true},
        {true, false, true}
    };

    for (uint32_t a = 0U; a < 3U; a++)
    {
        for (uint32_t b = 0U; b < 3U; b++)
        {
            seed_record(STORAGE_SLOT_A, records_a[a], 68U);
            seed_record(STORAGE_SLOT_B, records_b[b], 68U);
            reboot();
            expect_active(choose_b[a][b] ? alternate_payload : default_payload, 48U);
        }
    }
}

static void test_writes_rotate_and_wrap_generation(void)
{
    const uint8_t *expected[] =
    {
        alternate_record_0, alternate_record_1, alternate_record_2,
        alternate_record_0, alternate_record_1, alternate_record_2, alternate_record_0
    };
    const uint32_t addresses[] =
    {
        0x00FFA000U, 0x00FFD000U, 0x00FFA000U, 0x00FFD000U, 0x00FFA000U, 0x00FFD000U, 0x00FFA000U
    };

    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    for (uint32_t index = 0U; index < 7U; index++)
    {
        storage_flash_clear_log();
        TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_write(alternate_payload, 48U));
        TEST_ASSERT_EQUAL_UINT32(6U, storage_flash_event_count());
        expect_event(0U, STORAGE_ERASE, addresses[index], 4096U);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[index], storage_flash_bytes(addresses[index], 68U), 68U);
        expect_active(default_payload, 48U);
    }
    reboot();
    expect_active(alternate_payload, 48U);
}

static void test_invalid_input_never_accesses_flash(void)
{
    uint8_t oversized[8476];
    uint8_t trailing[49];

    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    storage_flash_clear_log();
    memset(oversized, 0, sizeof(oversized));
    memcpy(trailing, default_payload, 48U);
    trailing[48] = 0U;
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_INVALID_ARGUMENT, configuration_service_write(NULL, 48U));
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_INVALID_ARGUMENT, configuration_service_write(default_payload, 0U));
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_INVALID_ARGUMENT, configuration_service_write(oversized, 8476U));
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_INVALID_PAYLOAD, configuration_service_write(legacy_payload, 48U));
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_INVALID_PAYLOAD, configuration_service_write(default_payload, 47U));
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_INVALID_PAYLOAD, configuration_service_write(trailing, 49U));
    TEST_ASSERT_EQUAL_UINT32(0U, storage_flash_event_count());
    expect_active(default_payload, 48U);
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_write(maximum_payload, 8475U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(maximum_record_0, storage_flash_bytes(STORAGE_SLOT_A, 8495U), 8495U);
}

/* The six externally observable commit phases are fixed by the storage contract:
 * erase sector 0/1/2, program body, program magic, read back. */
static void run_fault_scenario(uint32_t phase, uint32_t completed, bool power_cut, bool has_old)
{
    const configuration_t *active;
    bool new_record_valid = phase == 6U || (phase == 5U && completed >= 4U);

    configuration_service_test_reset();
    storage_flash_initialize();
    if (has_old)
    {
        seed_record(STORAGE_SLOT_A, alternate_record_2, 68U);
        /* Target B contains an older valid generation, including when erase fails before changing it. */
        seed_record(STORAGE_SLOT_B, default_record_1, 68U);
        memcpy(saved_old_slot, storage_flash_bytes(STORAGE_SLOT_A, 12288U), 12288U);
    }
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    active = configuration_service_active();
    storage_flash_clear_log();
    storage_flash_set_fault(phase, completed, power_cut);
    if (setjmp(storage_flash_power_loss) == 0)
    {
        TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_IO_ERROR, configuration_service_write(maximum_payload, 8475U));
        TEST_ASSERT_FALSE(power_cut);
        TEST_ASSERT_EQUAL_PTR(active, configuration_service_active());
        expect_active(has_old ? alternate_payload : default_payload, 48U);
    }
    else
    {
        TEST_ASSERT_TRUE(power_cut);
    }
    TEST_ASSERT_EQUAL_UINT32(phase, storage_flash_event_count());
    if (has_old)
    {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(saved_old_slot, storage_flash_bytes(STORAGE_SLOT_A, 12288U), 12288U);
    }
    reboot();
    if (new_record_valid)
    {
        expect_active(maximum_payload, 8475U);
    }
    else
    {
        expect_active(has_old ? alternate_payload : default_payload, 48U);
    }
    fault_scenarios++;
}

static void test_io_failures_and_power_loss_at_every_commit_phase(void)
{
    const uint32_t completion[] =
    {
        0U, 1U, 3U, STORAGE_FAULT_COMPLETE
    };

    for (uint32_t phase = 1U; phase <= 6U; phase++)
    {
        for (uint32_t index = 0U; index < 4U; index++)
        {
            run_fault_scenario(phase, completion[index], false, true);
            run_fault_scenario(phase, completion[index], true, true);
            run_fault_scenario(phase, completion[index], false, false);
            run_fault_scenario(phase, completion[index], true, false);
        }
    }
}

static void test_power_loss_inside_body_pages_and_each_magic_byte(void)
{
    /* The first body page starts at slot+4 and has 252 available bytes. */
    for (uint32_t completed = 252U; completed < 8491U; completed += 256U)
    {
        run_fault_scenario(4U, completed, true, true);
    }
    run_fault_scenario(4U, 8490U, true, true);
    for (uint32_t completed = 0U; completed <= 4U; completed++)
    {
        run_fault_scenario(5U, completed, true, true);
    }
    TEST_ASSERT_EQUAL_UINT32(135U, fault_scenarios);
}

static void test_failed_write_can_retry_without_erasing_confirmed_slot(void)
{
    seed_record(STORAGE_SLOT_A, alternate_record_2, 68U);
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
    storage_flash_clear_log();
    storage_flash_set_fault(6U, 0U, false);
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_IO_ERROR, configuration_service_write(maximum_payload, 8475U));
    storage_flash_disable_fault();
    storage_flash_clear_log();
    TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_write(default_payload, 48U));
    expect_event(0U, STORAGE_ERASE, 0x00FFD000U, 4096U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(alternate_record_2, storage_flash_bytes(STORAGE_SLOT_A, 68U), 68U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(default_record_0, storage_flash_bytes(STORAGE_SLOT_B, 68U), 68U);
    expect_active(alternate_payload, 48U);
    reboot();
    expect_active(default_payload, 48U);
}

static void test_readback_mismatch_reports_error_and_can_still_commit(void)
{
    const uint32_t offsets[] =
    {
        0U, 19U, 20U, 8494U
    };

    for (uint32_t index = 0U; index < 4U; index++)
    {
        storage_flash_initialize();
        configuration_service_test_reset();
        seed_record(STORAGE_SLOT_A, alternate_record_2, 68U);
        TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_OK, configuration_service_init());
        storage_flash_clear_log();
        storage_flash_corrupt_read(offsets[index]);
        TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_IO_ERROR, configuration_service_write(maximum_payload, 8475U));
        expect_active(alternate_payload, 48U);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(alternate_record_2, storage_flash_bytes(STORAGE_SLOT_A, 68U), 68U);
        reboot();
        expect_active(maximum_payload, 8475U);
    }
}

static void test_startup_io_failure_is_not_default_fallback(void)
{
    for (uint32_t phase = 1U; phase <= 4U; phase++)
    {
        storage_flash_initialize();
        configuration_service_test_reset();
        seed_record(STORAGE_SLOT_A, default_record_0, 68U);
        seed_record(STORAGE_SLOT_B, alternate_record_1, 68U);
        storage_flash_set_fault(phase, 0U, false);
        TEST_ASSERT_EQUAL(CONFIGURATION_SERVICE_IO_ERROR, configuration_service_init());
        TEST_ASSERT_EQUAL_UINT32(phase, storage_flash_event_count());
        /* The API forbids any subsequent call in a failed lifetime; only reboot resets it. */
        reboot();
        expect_active(alternate_payload, 48U);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_flash_and_init_idempotence);
    RUN_TEST(test_maximum_write_restart_and_active_immutability);
    RUN_TEST(test_legacy_addresses_magic_and_schema_are_rejected);
    RUN_TEST(test_invalid_header_rejected_before_payload_read);
    RUN_TEST(test_header_and_payload_crc_corruption_recovers_old_slot);
    RUN_TEST(test_generation_selection_all_pairs);
    RUN_TEST(test_writes_rotate_and_wrap_generation);
    RUN_TEST(test_invalid_input_never_accesses_flash);
    RUN_TEST(test_io_failures_and_power_loss_at_every_commit_phase);
    RUN_TEST(test_power_loss_inside_body_pages_and_each_magic_byte);
    RUN_TEST(test_failed_write_can_retry_without_erasing_confirmed_slot);
    RUN_TEST(test_readback_mismatch_reports_error_and_can_still_commit);
    RUN_TEST(test_startup_io_failure_is_not_default_fallback);
    return UNITY_END();
}
