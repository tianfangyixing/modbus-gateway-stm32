#include "configuration.h"
#include "configuration_test_support.h"
#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PROPERTY_ITERATION_COUNT UINT32_C(10000)
#define ARBITRARY_OBJECT_SEED UINT32_C(0x45A1D39B)
#define MUTATION_SEED UINT32_C(0xA8C751E3)
#define EQUALITY_SEED UINT32_C(0x19F04B6D)
#define DEFAULTS_SEED UINT32_C(0xD37A2C91)

static uint32_t random_state;

static uint32_t random_u32(void)
{
    uint32_t value = random_state;

    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    random_state = value;
    return value;
}

static void random_bytes(void *destination, size_t length)
{
    uint8_t *bytes = destination;
    size_t index;

    for (index = 0U; index < length; index++)
    {
        bytes[index] = (uint8_t)random_u32();
    }
}

static const char *property_message(uint32_t seed, uint32_t iteration)
{
    static char message[96];
    int result;

    result = snprintf(message, sizeof(message), "seed=0x%08lX iteration=%lu",
                      (unsigned long)seed, (unsigned long)iteration);
    if (result <= 0 || (size_t)result >= sizeof(message))
    {
        return "property failure; message formatting failed";
    }

    return message;
}

static bool validation_result_is_documented(configuration_validation_result_t result)
{
    return result >= CONFIGURATION_VALIDATION_OK && result <= CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE;
}

static void set_hostname_text(configuration_hostname_t *field, const char *text)
{
    TEST_ASSERT_TRUE(configuration_test_set_hostname(field, (const uint8_t *)text, strlen(text)));
}

static void randomize_ascii_case(configuration_hostname_t *hostname, uint32_t pattern)
{
    uint16_t index;

    for (index = 0U; index < hostname->length; index++)
    {
        uint8_t value = hostname->bytes[index];

        if (value >= (uint8_t)'a' && value <= (uint8_t)'z' && ((pattern >> (index % 32U)) & 1U) != 0U)
        {
            hostname->bytes[index] = (uint8_t)(value - ((uint8_t)'a' - (uint8_t)'A'));
        }
    }
}

static void make_random_valid_configuration(configuration_t *configuration)
{
    static const uint32_t baud_rates[] = {1200U, 2400U, 4800U, 9600U, 19200U, 38400U, 57600U, 115200U};
    char primary_hostname[48];
    char secondary_hostname[48];
    uint32_t primary_value;
    uint32_t secondary_value;
    int primary_length;
    int secondary_length;

    configuration_set_defaults(configuration);
    if ((random_u32() & 1U) != 0U)
    {
        configuration_test_make_valid_static_network(configuration);
    }

    configuration->rtu.baud_rate = baud_rates[random_u32() % (sizeof(baud_rates) / sizeof(baud_rates[0]))];
    configuration->rtu.frame_format = (uint8_t)(random_u32() % 4U);
    configuration->rtu.first_byte_timeout_ms = (uint16_t)(50U + random_u32() % 59951U);
    configuration->modbus_tcp.listen_port = (uint16_t)(1U + random_u32() % UINT16_MAX);

    primary_value = random_u32();
    secondary_value = random_u32();
    if (secondary_value == primary_value)
    {
        secondary_value++;
    }
    primary_length = snprintf(primary_hostname, sizeof(primary_hostname), "time-%08lx.example",
                              (unsigned long)primary_value);
    secondary_length = snprintf(secondary_hostname, sizeof(secondary_hostname), "backup-%08lx.example",
                                (unsigned long)secondary_value);
    TEST_ASSERT_GREATER_THAN_INT(0, primary_length);
    TEST_ASSERT_LESS_THAN_INT((int)sizeof(primary_hostname), primary_length);
    TEST_ASSERT_GREATER_THAN_INT(0, secondary_length);
    TEST_ASSERT_LESS_THAN_INT((int)sizeof(secondary_hostname), secondary_length);
    set_hostname_text(&configuration->sntp.servers[0].value.hostname, primary_hostname);
    set_hostname_text(&configuration->sntp.servers[1].value.hostname, secondary_hostname);
}

static void randomize_inactive_fields(configuration_t *configuration)
{
    uint8_t network_mode = configuration->network.mode;

    if (network_mode == CONFIGURATION_NETWORK_MODE_DHCP)
    {
        random_bytes(&configuration->network.ip_address,
                     sizeof(configuration->network) - offsetof(configuration_network_t, ip_address));
        configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    }

    random_bytes(&configuration->mqtt.broker_address,
                 sizeof(configuration->mqtt) - offsetof(configuration_mqtt_t, broker_address));
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    random_bytes(configuration->collection.points, sizeof(configuration->collection.points));
    configuration->collection.point_count = 0U;

    configuration->sntp.servers[0].value.hostname.bytes[
        configuration->sntp.servers[0].value.hostname.length + 1U] = (uint8_t)random_u32();
    configuration->sntp.servers[1].value.hostname.bytes[
        configuration->sntp.servers[1].value.hostname.length + 1U] = (uint8_t)random_u32();
}

void setUp(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

static void test_validate_is_safe_deterministic_and_non_mutating_for_arbitrary_objects(void)
{
    configuration_t configuration;
    configuration_t before;
    configuration_validation_result_t first_result;
    configuration_validation_result_t second_result;
    uint32_t iteration;

    random_state = ARBITRARY_OBJECT_SEED;
    for (iteration = 0U; iteration < PROPERTY_ITERATION_COUNT; iteration++)
    {
        random_bytes(&configuration, sizeof(configuration));
        memcpy(&before, &configuration, sizeof(before));

        first_result = configuration_validate(&configuration);
        second_result = configuration_validate(&configuration);

        TEST_ASSERT_TRUE_MESSAGE(validation_result_is_documented(first_result),
                                 property_message(ARBITRARY_OBJECT_SEED, iteration));
        TEST_ASSERT_EQUAL_INT_MESSAGE(first_result, second_result,
                                      property_message(ARBITRARY_OBJECT_SEED, iteration));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)&before, (const uint8_t *)&configuration,
                                              sizeof(before), property_message(ARBITRARY_OBJECT_SEED, iteration));
    }
}

static void test_validate_is_safe_for_single_byte_mutations_of_valid_objects(void)
{
    configuration_t configuration;
    configuration_t before;
    configuration_validation_result_t first_result;
    configuration_validation_result_t second_result;
    uint32_t iteration;
    size_t byte_index;
    uint8_t mutation;

    random_state = MUTATION_SEED;
    for (iteration = 0U; iteration < PROPERTY_ITERATION_COUNT; iteration++)
    {
        if ((iteration % 16U) == 0U)
        {
            TEST_ASSERT_TRUE(configuration_test_make_valid_mqtt(&configuration));
        }
        else
        {
            configuration_set_defaults(&configuration);
        }

        byte_index = random_u32() % sizeof(configuration);
        mutation = (uint8_t)(1U + random_u32() % UINT8_MAX);
        ((uint8_t *)&configuration)[byte_index] ^= mutation;
        memcpy(&before, &configuration, sizeof(before));

        first_result = configuration_validate(&configuration);
        second_result = configuration_validate(&configuration);

        TEST_ASSERT_TRUE_MESSAGE(validation_result_is_documented(first_result),
                                 property_message(MUTATION_SEED, iteration));
        TEST_ASSERT_EQUAL_INT_MESSAGE(first_result, second_result, property_message(MUTATION_SEED, iteration));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)&before, (const uint8_t *)&configuration,
                                              sizeof(before), property_message(MUTATION_SEED, iteration));
    }
}

static void test_equals_laws_hold_for_random_valid_configurations(void)
{
    configuration_t first;
    configuration_t second;
    configuration_t third;
    configuration_t first_before;
    configuration_t second_before;
    configuration_t third_before;
    uint32_t iteration;
    bool first_second;
    bool second_first;
    bool second_third;
    bool first_third;

    random_state = EQUALITY_SEED;
    for (iteration = 0U; iteration < PROPERTY_ITERATION_COUNT; iteration++)
    {
        make_random_valid_configuration(&first);
        second = first;
        third = first;
        randomize_ascii_case(&second.sntp.servers[0].value.hostname, random_u32());
        randomize_ascii_case(&second.sntp.servers[1].value.hostname, random_u32());
        randomize_ascii_case(&third.sntp.servers[0].value.hostname, random_u32());
        randomize_ascii_case(&third.sntp.servers[1].value.hostname, random_u32());
        randomize_inactive_fields(&second);
        randomize_inactive_fields(&third);

        TEST_ASSERT_EQUAL_INT_MESSAGE(CONFIGURATION_VALIDATION_OK, configuration_validate(&first),
                                      property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_EQUAL_INT_MESSAGE(CONFIGURATION_VALIDATION_OK, configuration_validate(&second),
                                      property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_EQUAL_INT_MESSAGE(CONFIGURATION_VALIDATION_OK, configuration_validate(&third),
                                      property_message(EQUALITY_SEED, iteration));
        memcpy(&first_before, &first, sizeof(first_before));
        memcpy(&second_before, &second, sizeof(second_before));
        memcpy(&third_before, &third, sizeof(third_before));

        TEST_ASSERT_TRUE_MESSAGE(configuration_equals(&first, &first), property_message(EQUALITY_SEED, iteration));
        first_second = configuration_equals(&first, &second);
        second_first = configuration_equals(&second, &first);
        second_third = configuration_equals(&second, &third);
        first_third = configuration_equals(&first, &third);
        TEST_ASSERT_TRUE_MESSAGE(first_second, property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_EQUAL_INT_MESSAGE(first_second, second_first, property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_TRUE_MESSAGE(second_third, property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_TRUE_MESSAGE(first_third, property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)&first_before, (const uint8_t *)&first,
                                              sizeof(first), property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)&second_before, (const uint8_t *)&second,
                                              sizeof(second), property_message(EQUALITY_SEED, iteration));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)&third_before, (const uint8_t *)&third,
                                              sizeof(third), property_message(EQUALITY_SEED, iteration));
    }
}

static void test_defaults_is_idempotent_for_random_prior_contents(void)
{
    configuration_t configuration;
    configuration_t after_first_call;
    uint32_t iteration;

    random_state = DEFAULTS_SEED;
    for (iteration = 0U; iteration < PROPERTY_ITERATION_COUNT; iteration++)
    {
        random_bytes(&configuration, sizeof(configuration));
        configuration_set_defaults(&configuration);
        memcpy(&after_first_call, &configuration, sizeof(after_first_call));
        configuration_set_defaults(&configuration);

        TEST_ASSERT_EQUAL_INT_MESSAGE(CONFIGURATION_VALIDATION_OK, configuration_validate(&configuration),
                                      property_message(DEFAULTS_SEED, iteration));
        TEST_ASSERT_TRUE_MESSAGE(configuration_equals(&after_first_call, &configuration),
                                 property_message(DEFAULTS_SEED, iteration));
    }
}

int main(void)
{
    int unity_result;

    if (!configuration_test_use_standard_allocator())
    {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_validate_is_safe_deterministic_and_non_mutating_for_arbitrary_objects);
    RUN_TEST(test_validate_is_safe_for_single_byte_mutations_of_valid_objects);
    RUN_TEST(test_equals_laws_hold_for_random_valid_configurations);
    RUN_TEST(test_defaults_is_idempotent_for_random_prior_contents);
    unity_result = UNITY_END();
    return unity_result;
}
