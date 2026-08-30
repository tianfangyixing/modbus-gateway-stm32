#include "configuration_binary_codec.h"
#include "configuration_service.h"
#include "configuration_service_test_adapter.h"
#include "configuration_test_support.h"
#include "unity.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_SCHEMA_V1_MODBUS_TCP_PORT_OFFSET 9U

static uint8_t payload_a[CONFIGURATION_V1_MAX_PAYLOAD_LENGTH];
static uint8_t payload_b[CONFIGURATION_V1_MAX_PAYLOAD_LENGTH];

void setUp(void)
{
    configuration_service_test_reset();
    configuration_service_test_flash_reset();
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

static void make_configuration(configuration_t *configuration, uint16_t listen_port)
{
    configuration_set_defaults(configuration);
    configuration->modbus_tcp.listen_port = listen_port;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(configuration));
}

static void encode_configuration(const configuration_t *configuration, uint8_t *payload, uint32_t *payload_length)
{
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_encode(configuration, payload,
                                                      CONFIGURATION_V1_MAX_PAYLOAD_LENGTH, payload_length));
}

static void assert_configuration_equal(const configuration_t *expected, const configuration_t *actual)
{
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(expected));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(actual));
    TEST_ASSERT_TRUE(configuration_equals(expected, actual));
}

static void assert_defaults_active(void)
{
    configuration_t defaults;

    configuration_set_defaults(&defaults);
    assert_configuration_equal(&defaults, configuration_service_active());
}

static void seed_valid_record(uint8_t slot, uint32_t generation, const configuration_t *configuration,
                              uint8_t *payload, uint32_t *payload_length)
{
    encode_configuration(configuration, payload, payload_length);
    TEST_ASSERT_TRUE(configuration_service_test_seed_record(slot, CONFIGURATION_SERVICE_TEST_MAGIC, generation,
                                                            payload, *payload_length));
}

static void assert_init_ok(void)
{
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_init());
}

static void assert_no_flash_operations(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, configuration_service_test_read_count());
    TEST_ASSERT_EQUAL_UINT32(0U, configuration_service_test_program_count());
    TEST_ASSERT_EQUAL_UINT32(0U, configuration_service_test_erase_count());
}

static void reboot_service(void)
{
    configuration_service_test_reset();
    configuration_service_test_operation_reset();
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
    assert_init_ok();
}

static void test_active_is_null_before_init_and_defaults_after_empty_init(void)
{
    const configuration_t *active;

    TEST_ASSERT_NULL(configuration_service_active());
    assert_init_ok();
    active = configuration_service_active();
    assert_defaults_active();

    assert_init_ok();
    TEST_ASSERT_EQUAL_PTR(active, configuration_service_active());
    assert_defaults_active();
}

static void test_init_loads_the_only_valid_slot(void)
{
    configuration_t expected;
    uint32_t payload_length;

    make_configuration(&expected, UINT16_C(1502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, UINT32_C(2), &expected, payload_a, &payload_length);

    assert_init_ok();

    assert_configuration_equal(&expected, configuration_service_active());
}

static void test_init_selects_newest_generation_and_handles_modulo_three_wrap(void)
{
    configuration_t older;
    configuration_t newer;
    uint32_t older_length;
    uint32_t newer_length;

    make_configuration(&older, UINT16_C(1502));
    make_configuration(&newer, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(0), &older, payload_a, &older_length);
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, UINT32_C(1), &newer, payload_b, &newer_length);

    assert_init_ok();
    assert_configuration_equal(&newer, configuration_service_active());

    configuration_service_test_reset();
    configuration_service_test_flash_reset();
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(2), &older, payload_a, &older_length);
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, 0U, &newer, payload_b, &newer_length);

    assert_init_ok();
    assert_configuration_equal(&newer, configuration_service_active());
}

static void test_init_falls_back_from_newer_invalid_record(void)
{
    static const uint8_t malformed_payload[] = {UINT8_C(0xFF), UINT8_C(0xA5)};
    configuration_t older;
    uint32_t payload_length;

    make_configuration(&older, UINT16_C(1502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(2), &older, payload_a, &payload_length);
    TEST_ASSERT_TRUE(configuration_service_test_seed_record(CONFIGURATION_SERVICE_TEST_SLOT_B,
                                                            CONFIGURATION_SERVICE_TEST_MAGIC, UINT32_C(0),
                                                            malformed_payload, sizeof(malformed_payload)));

    assert_init_ok();

    assert_configuration_equal(&older, configuration_service_active());
}

static void test_init_rejects_old_format_and_corruption(void)
{
    configuration_t configuration;
    uint32_t payload_length;

    make_configuration(&configuration, UINT16_C(1502));
    encode_configuration(&configuration, payload_a, &payload_length);
    TEST_ASSERT_TRUE(configuration_service_test_seed_record(CONFIGURATION_SERVICE_TEST_SLOT_A,
                                                            CONFIGURATION_SERVICE_TEST_OLD_MAGIC, 0U,
                                                            payload_a, payload_length));
    TEST_ASSERT_TRUE(configuration_service_test_seed_record(CONFIGURATION_SERVICE_TEST_SLOT_B,
                                                            CONFIGURATION_SERVICE_TEST_MAGIC, 1U,
                                                            payload_a, payload_length));
    configuration_service_test_corrupt_slot(CONFIGURATION_SERVICE_TEST_SLOT_B,
                                            CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE);

    assert_init_ok();

    assert_defaults_active();
}

static void test_init_uses_slot_b_as_tie_breaker_for_equal_generations(void)
{
    configuration_t first;
    configuration_t second;
    uint32_t first_length;
    uint32_t second_length;

    make_configuration(&first, UINT16_C(1502));
    make_configuration(&second, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &first, payload_a, &first_length);
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, UINT32_C(1), &second, payload_b, &second_length);

    assert_init_ok();
    assert_configuration_equal(&second, configuration_service_active());

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_a, first_length));
    assert_configuration_equal(&second, configuration_service_active());
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(2),
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_A));

    reboot_service();
    assert_configuration_equal(&first, configuration_service_active());
}

static void test_init_selects_one_valid_slot_even_when_record_generations_match(void)
{
    static const uint8_t malformed_payload[] = {UINT8_C(0xFF)};
    configuration_t expected;
    uint32_t payload_length;

    make_configuration(&expected, UINT16_C(1502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &expected, payload_a, &payload_length);
    TEST_ASSERT_TRUE(configuration_service_test_seed_record(CONFIGURATION_SERVICE_TEST_SLOT_B,
                                                            CONFIGURATION_SERVICE_TEST_MAGIC, UINT32_C(1),
                                                            malformed_payload, sizeof(malformed_payload)));

    assert_init_ok();

    assert_configuration_equal(&expected, configuration_service_active());
}

static void test_init_rejects_generation_outside_modulo_three(void)
{
    configuration_t configuration;
    uint32_t payload_length;

    make_configuration(&configuration, UINT16_C(1502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(3), &configuration,
                      payload_a, &payload_length);

    assert_init_ok();
    assert_defaults_active();
}

static void test_init_reports_io_error_for_read_failures(void)
{
    configuration_t first;
    configuration_t second;
    uint32_t first_length;
    uint32_t second_length;
    uint32_t failed_call;

    make_configuration(&first, UINT16_C(1502));
    make_configuration(&second, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &first, payload_a, &first_length);
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, UINT32_C(2), &second, payload_b, &second_length);

    for (failed_call = 1U; failed_call <= 2U; failed_call++)
    {
        configuration_service_test_reset();
        configuration_service_test_operation_reset();
        configuration_service_test_fail_read(failed_call);
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_IO_ERROR, configuration_service_init());
    }
}

static void test_init_reports_codec_resource_failure(void)
{
    configuration_t mqtt_configuration;
    configuration_service_result_t result;
    uint32_t payload_length;

    TEST_ASSERT_TRUE(configuration_test_make_valid_mqtt(&mqtt_configuration));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, 0U, &mqtt_configuration, payload_a, &payload_length);
    TEST_ASSERT_TRUE(configuration_test_use_failing_allocator());

    result = configuration_service_init();

    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE, result);
}

static void test_write_rejects_lifecycle_and_argument_errors(void)
{
    static const uint8_t payload[] = {CONFIGURATION_SCHEMA_VERSION};

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_NOT_INITIALIZED,
                          configuration_service_write(payload, sizeof(payload)));

    assert_init_ok();
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_INVALID_ARGUMENT, configuration_service_write(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_INVALID_ARGUMENT, configuration_service_write(payload, 0U));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_INVALID_ARGUMENT,
                          configuration_service_write(payload, CONFIGURATION_V1_MAX_PAYLOAD_LENGTH + 1U));
}

static void test_write_commits_in_order_without_changing_active(void)
{
    configuration_t old_configuration;
    configuration_t new_configuration;
    const configuration_t *active_before;
    uint32_t old_length;
    uint32_t new_length;

    make_configuration(&old_configuration, UINT16_C(1502));
    make_configuration(&new_configuration, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(2), &old_configuration,
                      payload_a, &old_length);
    encode_configuration(&new_configuration, payload_b, &new_length);
    assert_init_ok();
    active_before = configuration_service_active();
    configuration_service_test_operation_reset();

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_b, new_length));

    TEST_ASSERT_EQUAL_PTR(active_before, configuration_service_active());
    assert_configuration_equal(&old_configuration, configuration_service_active());
    TEST_ASSERT_EQUAL_UINT32(2U, configuration_service_test_erase_count());
    TEST_ASSERT_EQUAL_UINT32(3U, configuration_service_test_program_count());
    TEST_ASSERT_EQUAL_UINT32(1U, configuration_service_test_read_count());
    TEST_ASSERT_EQUAL_HEX32(CONFIGURATION_SERVICE_TEST_SLOT_B_ADDRESS +
                                CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE,
                            configuration_service_test_program_address(1U));
    TEST_ASSERT_EQUAL_UINT32(new_length, configuration_service_test_program_length(1U));
    TEST_ASSERT_EQUAL_HEX32(CONFIGURATION_SERVICE_TEST_SLOT_B_ADDRESS + 4U,
                            configuration_service_test_program_address(2U));
    TEST_ASSERT_EQUAL_UINT32(16U, configuration_service_test_program_length(2U));
    TEST_ASSERT_EQUAL_HEX32(CONFIGURATION_SERVICE_TEST_SLOT_B_ADDRESS,
                            configuration_service_test_program_address(3U));
    TEST_ASSERT_EQUAL_UINT32(4U, configuration_service_test_program_length(3U));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_B));

    reboot_service();
    assert_configuration_equal(&new_configuration, configuration_service_active());
}

static void test_write_replaces_newer_invalid_slot_from_selected_generation(void)
{
    static const uint8_t malformed_payload[] = {UINT8_C(0xFF), UINT8_C(0xA5)};
    configuration_t current_configuration;
    configuration_t new_configuration;
    uint32_t current_length;
    uint32_t new_length;

    make_configuration(&current_configuration, UINT16_C(1502));
    make_configuration(&new_configuration, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(2), &current_configuration,
                      payload_a, &current_length);
    TEST_ASSERT_TRUE(configuration_service_test_seed_record(CONFIGURATION_SERVICE_TEST_SLOT_B,
                                                            CONFIGURATION_SERVICE_TEST_MAGIC, UINT32_C(0),
                                                            malformed_payload, sizeof(malformed_payload)));
    encode_configuration(&new_configuration, payload_b, &new_length);
    assert_init_ok();
    assert_configuration_equal(&current_configuration, configuration_service_active());
    configuration_service_test_operation_reset();

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_b, new_length));

    TEST_ASSERT_EQUAL_HEX32(CONFIGURATION_SERVICE_TEST_SLOT_B_ADDRESS +
                                CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE,
                            configuration_service_test_program_address(1U));
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0),
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_B));
    assert_configuration_equal(&current_configuration, configuration_service_active());

    reboot_service();
    assert_configuration_equal(&new_configuration, configuration_service_active());
}

static void test_repeated_identical_writes_always_commit_and_alternate(void)
{
    configuration_t configuration;
    uint32_t payload_length;

    make_configuration(&configuration, UINT16_C(2502));
    encode_configuration(&configuration, payload_a, &payload_length);
    assert_init_ok();
    configuration_service_test_operation_reset();

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_a, payload_length));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_a, payload_length));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_a, payload_length));

    TEST_ASSERT_EQUAL_UINT32(6U, configuration_service_test_erase_count());
    TEST_ASSERT_EQUAL_UINT32(9U, configuration_service_test_program_count());
    TEST_ASSERT_EQUAL_UINT32(2U,
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_A));
    TEST_ASSERT_EQUAL_UINT32(1U,
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_B));
    assert_defaults_active();

    reboot_service();
    assert_configuration_equal(&configuration, configuration_service_active());
}

static void test_write_rejects_invalid_payload_without_storage_side_effects(void)
{
    configuration_t old_configuration;
    configuration_t new_configuration;
    const configuration_t *active_before;
    uint32_t old_length;
    uint32_t new_length;

    make_configuration(&old_configuration, UINT16_C(1502));
    make_configuration(&new_configuration, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(2), &old_configuration,
                      payload_a, &old_length);
    encode_configuration(&new_configuration, payload_b, &new_length);
    assert_init_ok();
    active_before = configuration_service_active();
    configuration_service_test_operation_reset();

    payload_b[0] = UINT8_MAX;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_INVALID_PAYLOAD,
                          configuration_service_write(payload_b, new_length));

    payload_b[0] = CONFIGURATION_SCHEMA_VERSION;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_INVALID_PAYLOAD,
                          configuration_service_write(payload_b, 1U));

    payload_b[TEST_SCHEMA_V1_MODBUS_TCP_PORT_OFFSET] = 0U;
    payload_b[TEST_SCHEMA_V1_MODBUS_TCP_PORT_OFFSET + 1U] = 0U;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_INVALID_PAYLOAD,
                          configuration_service_write(payload_b, new_length));

    assert_no_flash_operations();
    TEST_ASSERT_EQUAL_PTR(active_before, configuration_service_active());
    assert_configuration_equal(&old_configuration, configuration_service_active());

    encode_configuration(&new_configuration, payload_b, &new_length);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_b, new_length));
    TEST_ASSERT_EQUAL_UINT32(2U, configuration_service_test_erase_count());
    TEST_ASSERT_EQUAL_UINT32(3U, configuration_service_test_program_count());
    TEST_ASSERT_EQUAL_UINT32(1U, configuration_service_test_read_count());
    TEST_ASSERT_EQUAL_UINT32(0U,
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_B));

    reboot_service();
    assert_configuration_equal(&new_configuration, configuration_service_active());
}

static void test_write_reports_codec_resource_failure_without_storage_side_effects(void)
{
    configuration_t mqtt_configuration;
    configuration_service_result_t result;
    const configuration_t *active_before;
    uint32_t payload_length;

    TEST_ASSERT_TRUE(configuration_test_make_valid_mqtt(&mqtt_configuration));
    encode_configuration(&mqtt_configuration, payload_a, &payload_length);
    assert_init_ok();
    active_before = configuration_service_active();
    configuration_service_test_operation_reset();
    TEST_ASSERT_TRUE(configuration_test_use_failing_allocator());

    result = configuration_service_write(payload_a, payload_length);

    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE, result);
    assert_no_flash_operations();
    TEST_ASSERT_EQUAL_PTR(active_before, configuration_service_active());
    assert_defaults_active();

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_a, payload_length));
    TEST_ASSERT_EQUAL_UINT32(2U, configuration_service_test_erase_count());
    TEST_ASSERT_EQUAL_UINT32(3U, configuration_service_test_program_count());
    TEST_ASSERT_EQUAL_UINT32(1U, configuration_service_test_read_count());
    TEST_ASSERT_EQUAL_UINT32(0U,
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_A));

    reboot_service();
    assert_configuration_equal(&mqtt_configuration, configuration_service_active());
}

static void test_write_uses_empty_init_state_without_rescanning(void)
{
    configuration_t configuration;
    uint32_t payload_length;

    make_configuration(&configuration, UINT16_C(2502));
    encode_configuration(&configuration, payload_a, &payload_length);
    assert_init_ok();
    assert_defaults_active();

    configuration_service_test_operation_reset();
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_a, payload_length));

    TEST_ASSERT_EQUAL_UINT32(1U, configuration_service_test_read_count());
    TEST_ASSERT_EQUAL_UINT32(2U, configuration_service_test_erase_count());
    TEST_ASSERT_EQUAL_UINT32(3U, configuration_service_test_program_count());
    TEST_ASSERT_EQUAL_UINT32(0U,
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_A));
    assert_defaults_active();
}

typedef enum
{
    WRITE_FAILURE_ERASE = 0,
    WRITE_FAILURE_PROGRAM
} write_failure_kind_t;

static void assert_failed_write_preserves_old(write_failure_kind_t failure_kind, uint32_t failed_call)
{
    configuration_t old_configuration;
    configuration_t new_configuration;
    uint32_t old_length;
    uint32_t new_length;

    configuration_service_test_reset();
    configuration_service_test_flash_reset();
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
    make_configuration(&old_configuration, UINT16_C(1502));
    make_configuration(&new_configuration, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &old_configuration,
                      payload_a, &old_length);
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, UINT32_C(0), &new_configuration,
                      payload_b, &new_length);
    assert_init_ok();
    configuration_service_test_operation_reset();

    if (failure_kind == WRITE_FAILURE_ERASE)
    {
        configuration_service_test_fail_erase(failed_call);
    }
    else
    {
        configuration_service_test_fail_program(failed_call);
    }

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_IO_ERROR,
                          configuration_service_write(payload_b, new_length));
    assert_configuration_equal(&old_configuration, configuration_service_active());

    reboot_service();
    assert_configuration_equal(&old_configuration, configuration_service_active());
}

static void test_write_failures_before_commit_preserve_old_configuration(void)
{
    uint32_t failed_call;

    for (failed_call = 1U; failed_call <= 2U; failed_call++)
    {
        assert_failed_write_preserves_old(WRITE_FAILURE_ERASE, failed_call);
    }

    for (failed_call = 1U; failed_call <= 3U; failed_call++)
    {
        assert_failed_write_preserves_old(WRITE_FAILURE_PROGRAM, failed_call);
    }
}

static void test_failed_write_retries_same_slot_and_generation_without_reboot(void)
{
    configuration_t current_configuration;
    configuration_t previous_configuration;
    configuration_t new_configuration;
    uint32_t current_length;
    uint32_t previous_length;
    uint32_t new_length;

    make_configuration(&current_configuration, UINT16_C(1502));
    make_configuration(&previous_configuration, UINT16_C(2502));
    make_configuration(&new_configuration, UINT16_C(3502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &current_configuration,
                      payload_a, &current_length);
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_B, UINT32_C(0), &previous_configuration,
                      payload_b, &previous_length);
    encode_configuration(&new_configuration, payload_b, &new_length);
    assert_init_ok();

    configuration_service_test_operation_reset();
    configuration_service_test_fail_program(1U);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_IO_ERROR,
                          configuration_service_write(payload_b, new_length));

    configuration_service_test_operation_reset();
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_OK, configuration_service_write(payload_b, new_length));

    TEST_ASSERT_EQUAL_HEX32(CONFIGURATION_SERVICE_TEST_SLOT_B_ADDRESS +
                                CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE,
                            configuration_service_test_program_address(1U));
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(2),
                             configuration_service_test_slot_generation(CONFIGURATION_SERVICE_TEST_SLOT_B));
    assert_configuration_equal(&current_configuration, configuration_service_active());

    reboot_service();
    assert_configuration_equal(&new_configuration, configuration_service_active());
}

static void test_write_verification_corruption_preserves_old_configuration(void)
{
    configuration_t old_configuration;
    configuration_t new_configuration;
    uint32_t old_length;
    uint32_t new_length;

    make_configuration(&old_configuration, UINT16_C(1502));
    make_configuration(&new_configuration, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &old_configuration,
                      payload_a, &old_length);
    encode_configuration(&new_configuration, payload_b, &new_length);
    assert_init_ok();
    configuration_service_test_operation_reset();
    configuration_service_test_corrupt_read(1U, CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE);

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_IO_ERROR,
                          configuration_service_write(payload_b, new_length));
    assert_configuration_equal(&old_configuration, configuration_service_active());

    reboot_service();
    assert_configuration_equal(&old_configuration, configuration_service_active());
}

static void test_final_read_error_keeps_old_slot_even_if_new_record_committed(void)
{
    configuration_t old_configuration;
    configuration_t new_configuration;
    uint32_t old_length;
    uint32_t new_length;

    make_configuration(&old_configuration, UINT16_C(1502));
    make_configuration(&new_configuration, UINT16_C(2502));
    seed_valid_record(CONFIGURATION_SERVICE_TEST_SLOT_A, UINT32_C(1), &old_configuration,
                      payload_a, &old_length);
    encode_configuration(&new_configuration, payload_b, &new_length);
    assert_init_ok();
    configuration_service_test_operation_reset();
    configuration_service_test_fail_read(1U);

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_SERVICE_IO_ERROR,
                          configuration_service_write(payload_b, new_length));
    assert_configuration_equal(&old_configuration, configuration_service_active());

    reboot_service();
    assert_configuration_equal(&new_configuration, configuration_service_active());

    configuration_service_test_corrupt_slot(CONFIGURATION_SERVICE_TEST_SLOT_B,
                                            CONFIGURATION_SERVICE_TEST_SLOT_HEADER_SIZE);
    reboot_service();
    assert_configuration_equal(&old_configuration, configuration_service_active());
}

int main(void)
{
    int unity_result;

    UNITY_BEGIN();
    RUN_TEST(test_active_is_null_before_init_and_defaults_after_empty_init);
    RUN_TEST(test_init_loads_the_only_valid_slot);
    RUN_TEST(test_init_selects_newest_generation_and_handles_modulo_three_wrap);
    RUN_TEST(test_init_falls_back_from_newer_invalid_record);
    RUN_TEST(test_init_rejects_old_format_and_corruption);
    RUN_TEST(test_init_uses_slot_b_as_tie_breaker_for_equal_generations);
    RUN_TEST(test_init_selects_one_valid_slot_even_when_record_generations_match);
    RUN_TEST(test_init_rejects_generation_outside_modulo_three);
    RUN_TEST(test_init_reports_io_error_for_read_failures);
    RUN_TEST(test_init_reports_codec_resource_failure);
    RUN_TEST(test_write_rejects_lifecycle_and_argument_errors);
    RUN_TEST(test_write_commits_in_order_without_changing_active);
    RUN_TEST(test_write_replaces_newer_invalid_slot_from_selected_generation);
    RUN_TEST(test_repeated_identical_writes_always_commit_and_alternate);
    RUN_TEST(test_write_rejects_invalid_payload_without_storage_side_effects);
    RUN_TEST(test_write_reports_codec_resource_failure_without_storage_side_effects);
    RUN_TEST(test_write_uses_empty_init_state_without_rescanning);
    RUN_TEST(test_write_failures_before_commit_preserve_old_configuration);
    RUN_TEST(test_failed_write_retries_same_slot_and_generation_without_reboot);
    RUN_TEST(test_write_verification_corruption_preserves_old_configuration);
    RUN_TEST(test_final_read_error_keeps_old_slot_even_if_new_record_committed);
    unity_result = UNITY_END();
    return unity_result;
}
