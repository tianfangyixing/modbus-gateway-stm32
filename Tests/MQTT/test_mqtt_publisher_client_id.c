#include "unity.h"

#include "mqtt_publisher.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DERIVED_CLIENT_ID_LENGTH 23U
#define DERIVED_CLIENT_ID_BUFFER_SIZE (DERIVED_CLIENT_ID_LENGTH + 1U)

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_client_id_fixed_vectors(void)
{
    static const struct
    {
        uint32_t words[3];
        const char *expected;
    } vectors[] =
    {
        {{UINT32_C(0), UINT32_C(0), UINT32_C(0)}, "STMAAAAAAAAAAAAAAAAAAAA"},
        {{UINT32_MAX, UINT32_MAX, UINT32_MAX}, "STM7777777777777777777Q"},
        {{UINT32_C(0x80000000), UINT32_C(0), UINT32_C(0)}, "STMQAAAAAAAAAAAAAAAAAAA"},
        {{UINT32_C(0), UINT32_C(0), UINT32_C(1)}, "STMAAAAAAAAAAAAAAAAAAAQ"},
        {{UINT32_C(0x01234567), UINT32_C(0x89ABCDEF), UINT32_C(0xFEDCBA98)},
         "STMAERUKZ4JVPG677W4XKMA"}
    };
    size_t vector_index;

    for (vector_index = 0U; vector_index < sizeof(vectors) / sizeof(vectors[0]); vector_index++)
    {
        const char *output = mqtt_publisher_test_client_id_from_uid(vectors[vector_index].words[0],
                                                                    vectors[vector_index].words[1],
                                                                    vectors[vector_index].words[2]);

        TEST_ASSERT_EQUAL_STRING(vectors[vector_index].expected, output);
        TEST_ASSERT_EQUAL_UINT16(DERIVED_CLIENT_ID_LENGTH, (uint16_t)strlen(output));
        TEST_ASSERT_EQUAL_CHAR('\0', output[DERIVED_CLIENT_ID_LENGTH]);
    }
}

static void test_client_id_is_alphanumeric_and_deterministic(void)
{
    char first[DERIVED_CLIENT_ID_BUFFER_SIZE];
    const char *generated;
    size_t index;

    generated = mqtt_publisher_test_client_id_from_uid(UINT32_C(0x13579BDF), UINT32_C(0x2468ACE0),
                                                        UINT32_C(0x55AA00FF));
    memcpy(first, generated, sizeof(first));
    generated = mqtt_publisher_test_client_id_from_uid(UINT32_C(0x13579BDF), UINT32_C(0x2468ACE0),
                                                        UINT32_C(0x55AA00FF));
    TEST_ASSERT_EQUAL_STRING(first, generated);
    for (index = 0U; index < DERIVED_CLIENT_ID_LENGTH; index++)
    {
        TEST_ASSERT_TRUE((generated[index] >= 'A' && generated[index] <= 'Z') ||
                         (generated[index] >= '0' && generated[index] <= '9'));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_client_id_fixed_vectors);
    RUN_TEST(test_client_id_is_alphanumeric_and_deterministic);
    return UNITY_END();
}
