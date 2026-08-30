#include "configuration.h"
#include "configuration_test_support.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

static void assert_zero_bytes(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0U; index < length; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0U, bytes[index]);
    }
}

static void assert_hostname(const configuration_hostname_t *hostname, const char *expected)
{
    size_t expected_length = strlen(expected);

    TEST_ASSERT_EQUAL_UINT16(expected_length, hostname->length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)expected, hostname->bytes, expected_length);
    assert_zero_bytes(&hostname->bytes[expected_length], sizeof(hostname->bytes) - expected_length);
}

static void assert_point_public_fields_equal(const configuration_collection_point_t *left,
                                             const configuration_collection_point_t *right)
{
    TEST_ASSERT_EQUAL_UINT8(left->slave_address, right->slave_address);
    TEST_ASSERT_EQUAL_UINT8(left->source, right->source);
    TEST_ASSERT_EQUAL_UINT16(left->address, right->address);
    TEST_ASSERT_EQUAL_UINT8(left->data_type, right->data_type);
    TEST_ASSERT_EQUAL_UINT32(left->poll_interval_ms, right->poll_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(left->first_byte_timeout_ms, right->first_byte_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(left->topic.length, right->topic.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->topic.bytes, right->topic.bytes, sizeof(left->topic.bytes));
    TEST_ASSERT_EQUAL_UINT8(left->qos, right->qos);
}

static void assert_message_public_fields_equal(const configuration_mqtt_message_t *left,
                                               const configuration_mqtt_message_t *right)
{
    TEST_ASSERT_EQUAL_UINT8(left->mode, right->mode);
    TEST_ASSERT_EQUAL_UINT16(left->topic.length, right->topic.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->topic.bytes, right->topic.bytes, sizeof(left->topic.bytes));
    TEST_ASSERT_EQUAL_UINT16(left->payload.length, right->payload.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->payload.bytes, right->payload.bytes, sizeof(left->payload.bytes));
    TEST_ASSERT_EQUAL_UINT8(left->qos, right->qos);
    TEST_ASSERT_EQUAL_UINT8(left->retain, right->retain);
}

static void assert_public_fields_equal(const configuration_t *left, const configuration_t *right)
{
    size_t index;

    TEST_ASSERT_EQUAL_UINT8(left->network.mode, right->network.mode);
    TEST_ASSERT_EQUAL_UINT32(left->network.ip_address.addr, right->network.ip_address.addr);
    TEST_ASSERT_EQUAL_UINT32(left->network.subnet_mask.addr, right->network.subnet_mask.addr);
    TEST_ASSERT_EQUAL_UINT32(left->network.gateway.addr, right->network.gateway.addr);
    TEST_ASSERT_EQUAL_UINT32(left->network.dns_primary.addr, right->network.dns_primary.addr);
    TEST_ASSERT_EQUAL_UINT32(left->network.dns_secondary.addr, right->network.dns_secondary.addr);
    TEST_ASSERT_EQUAL_UINT32(left->rtu.baud_rate, right->rtu.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(left->rtu.frame_format, right->rtu.frame_format);
    TEST_ASSERT_EQUAL_UINT16(left->rtu.first_byte_timeout_ms, right->rtu.first_byte_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(left->modbus_tcp.listen_port, right->modbus_tcp.listen_port);

    for (index = 0U; index < 2U; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(left->sntp.servers[index].type, right->sntp.servers[index].type);
        TEST_ASSERT_EQUAL_UINT16(left->sntp.servers[index].value.hostname.length,
                                 right->sntp.servers[index].value.hostname.length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(left->sntp.servers[index].value.hostname.bytes,
                                      right->sntp.servers[index].value.hostname.bytes,
                                      sizeof(left->sntp.servers[index].value.hostname.bytes));
    }

    TEST_ASSERT_EQUAL_UINT8(left->mqtt.mode, right->mqtt.mode);
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.broker_address.length, right->mqtt.broker_address.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->mqtt.broker_address.bytes, right->mqtt.broker_address.bytes,
                                  sizeof(left->mqtt.broker_address.bytes));
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.broker_port, right->mqtt.broker_port);
    TEST_ASSERT_EQUAL_UINT8(left->mqtt.client_id.mode, right->mqtt.client_id.mode);
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.client_id.explicit_value.length,
                             right->mqtt.client_id.explicit_value.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->mqtt.client_id.explicit_value.bytes,
                                  right->mqtt.client_id.explicit_value.bytes,
                                  sizeof(left->mqtt.client_id.explicit_value.bytes));
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.username.length, right->mqtt.username.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->mqtt.username.bytes, right->mqtt.username.bytes,
                                  sizeof(left->mqtt.username.bytes));
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.password.length, right->mqtt.password.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->mqtt.password.bytes, right->mqtt.password.bytes,
                                  sizeof(left->mqtt.password.bytes));
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.ca_certificate_pem.length, right->mqtt.ca_certificate_pem.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left->mqtt.ca_certificate_pem.bytes, right->mqtt.ca_certificate_pem.bytes,
                                  sizeof(left->mqtt.ca_certificate_pem.bytes));
    TEST_ASSERT_EQUAL_UINT16(left->mqtt.keep_alive_seconds, right->mqtt.keep_alive_seconds);
    assert_message_public_fields_equal(&left->mqtt.online_message, &right->mqtt.online_message);
    assert_message_public_fields_equal(&left->mqtt.will_message, &right->mqtt.will_message);
    TEST_ASSERT_EQUAL_UINT8(left->collection.point_count, right->collection.point_count);

    for (index = 0U; index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; index++)
    {
        assert_point_public_fields_equal(&left->collection.points[index], &right->collection.points[index]);
    }
}

void setUp(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

static void test_null_is_safe(void)
{
    configuration_set_defaults(NULL);
}

static void test_sets_every_documented_default_and_clears_all_other_public_fields(void)
{
    configuration_t configuration;
    configuration_t zero_configuration;

    memset(&configuration, 0xA5, sizeof(configuration));
    memset(&zero_configuration, 0, sizeof(zero_configuration));
    configuration_set_defaults(&configuration);

    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_NETWORK_MODE_DHCP, configuration.network.mode);
    TEST_ASSERT_EQUAL_UINT32(0U, configuration.network.ip_address.addr);
    TEST_ASSERT_EQUAL_UINT32(0U, configuration.network.subnet_mask.addr);
    TEST_ASSERT_EQUAL_UINT32(0U, configuration.network.gateway.addr);
    TEST_ASSERT_EQUAL_UINT32(0U, configuration.network.dns_primary.addr);
    TEST_ASSERT_EQUAL_UINT32(0U, configuration.network.dns_secondary.addr);
    TEST_ASSERT_EQUAL_UINT32(9600U, configuration.rtu.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_FRAME_FORMAT_8N2, configuration.rtu.frame_format);
    TEST_ASSERT_EQUAL_UINT16(1000U, configuration.rtu.first_byte_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(502U, configuration.modbus_tcp.listen_port);
    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME, configuration.sntp.servers[0].type);
    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME, configuration.sntp.servers[1].type);
    assert_hostname(&configuration.sntp.servers[0].value.hostname, "ntp.aliyun.com");
    assert_hostname(&configuration.sntp.servers[1].value.hostname, "ntp.tencent.com");
    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_MQTT_MODE_DISABLED, configuration.mqtt.mode);
    TEST_ASSERT_EQUAL_UINT8(0U, configuration.collection.point_count);

    zero_configuration.rtu.baud_rate = 9600U;
    zero_configuration.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    zero_configuration.rtu.first_byte_timeout_ms = 1000U;
    zero_configuration.modbus_tcp.listen_port = 502U;
    zero_configuration.sntp.servers[0].type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
    zero_configuration.sntp.servers[1].type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
    TEST_ASSERT_TRUE(configuration_test_set_hostname(&zero_configuration.sntp.servers[0].value.hostname,
                                                      (const uint8_t *)"ntp.aliyun.com", 14U));
    TEST_ASSERT_TRUE(configuration_test_set_hostname(&zero_configuration.sntp.servers[1].value.hostname,
                                                      (const uint8_t *)"ntp.tencent.com", 15U));
    assert_public_fields_equal(&zero_configuration, &configuration);
}

static void test_result_is_independent_of_original_contents(void)
{
    configuration_t first;
    configuration_t second;

    memset(&first, 0x00, sizeof(first));
    memset(&second, 0xFF, sizeof(second));
    configuration_set_defaults(&first);
    configuration_set_defaults(&second);

    assert_public_fields_equal(&first, &second);
}

static void test_call_is_idempotent(void)
{
    configuration_t configuration;
    configuration_t after_first_call;

    memset(&configuration, 0x5A, sizeof(configuration));
    configuration_set_defaults(&configuration);
    memcpy(&after_first_call, &configuration, sizeof(configuration));
    configuration_set_defaults(&configuration);

    assert_public_fields_equal(&after_first_call, &configuration);
}

static void test_does_not_write_outside_target_object(void)
{
    struct
    {
        uint32_t before;
        configuration_t configuration;
        uint32_t after;
    } guarded;

    guarded.before = UINT32_C(0x12345678);
    guarded.after = UINT32_C(0xA5C3E17F);
    memset(&guarded.configuration, 0xCC, sizeof(guarded.configuration));

    configuration_set_defaults(&guarded.configuration);

    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0x12345678), guarded.before);
    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0xA5C3E17F), guarded.after);
}

static void test_default_configuration_is_valid(void)
{
    configuration_t configuration;

    memset(&configuration, 0xA5, sizeof(configuration));
    configuration_set_defaults(&configuration);

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&configuration));
}

int main(void)
{
    int unity_result;

    if (!configuration_test_use_standard_allocator())
    {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_null_is_safe);
    RUN_TEST(test_sets_every_documented_default_and_clears_all_other_public_fields);
    RUN_TEST(test_result_is_independent_of_original_contents);
    RUN_TEST(test_call_is_idempotent);
    RUN_TEST(test_does_not_write_outside_target_object);
    RUN_TEST(test_default_configuration_is_valid);
    unity_result = UNITY_END();
    return unity_result;
}
