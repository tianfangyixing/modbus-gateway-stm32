#include "configuration.h"
#include "configuration_test_support.h"
#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void set_hostname_text(configuration_hostname_t *field, const char *text)
{
    TEST_ASSERT_TRUE(configuration_test_set_hostname(field, (const uint8_t *)text, strlen(text)));
}

static void set_endpoint_hostname(configuration_endpoint_address_t *endpoint, const char *hostname)
{
    endpoint->type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
    set_hostname_text(&endpoint->value.hostname, hostname);
}

static void set_endpoint_ipv4(configuration_endpoint_address_t *endpoint, uint8_t first, uint8_t second,
                              uint8_t third, uint8_t fourth)
{
    endpoint->type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4;
    configuration_test_set_ipv4(&endpoint->value.ipv4, first, second, third, fourth);
}

static void set_topic_text(configuration_topic_t *field, const char *text)
{
    TEST_ASSERT_TRUE(configuration_test_set_topic(field, (const uint8_t *)text, strlen(text)));
}

static void set_payload_text(configuration_mqtt_message_payload_t *field, const char *text)
{
    TEST_ASSERT_TRUE(configuration_test_set_payload(field, (const uint8_t *)text, strlen(text)));
}

static void set_custom_message(configuration_mqtt_message_t *message, const char *topic, const char *payload,
                               uint8_t qos, uint8_t retain)
{
    memset(message, 0, sizeof(*message));
    message->mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    set_topic_text(&message->topic, topic);
    set_payload_text(&message->payload, payload);
    message->qos = qos;
    message->retain = retain;
}

static void make_default_configuration(configuration_t *configuration)
{
    configuration_set_defaults(configuration);
}

static void make_valid_mqtt_configuration(configuration_t *configuration)
{
    TEST_ASSERT_TRUE(configuration_test_make_valid_mqtt(configuration));
}

static void make_mqtt_with_points(configuration_t *configuration, uint8_t point_count)
{
    uint8_t index;

    make_valid_mqtt_configuration(configuration);
    configuration->collection.point_count = point_count;
    for (index = 0U; index < point_count; index++)
    {
        TEST_ASSERT_TRUE(configuration_test_make_valid_point(&configuration->collection.points[index], index));
    }
}

static void make_manual_default(configuration_t *configuration, uint8_t fill)
{
    memset(configuration, fill, sizeof(*configuration));
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    configuration->rtu.baud_rate = 9600U;
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration->rtu.first_byte_timeout_ms = 1000U;
    configuration->modbus_tcp.listen_port = 502U;
    set_endpoint_hostname(&configuration->sntp.servers[0], "ntp.aliyun.com");
    set_endpoint_hostname(&configuration->sntp.servers[1], "ntp.tencent.com");
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    configuration->collection.point_count = 0U;
}

static void assert_equals_result(const configuration_t *left, const configuration_t *right, bool expected)
{
    configuration_t left_before;
    configuration_t right_before;
    bool forward;
    bool reverse;

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(left));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(right));
    memcpy(&left_before, left, sizeof(left_before));
    memcpy(&right_before, right, sizeof(right_before));

    forward = configuration_equals(left, right);
    reverse = configuration_equals(right, left);

    TEST_ASSERT_EQUAL_INT(expected, forward);
    TEST_ASSERT_EQUAL_INT(expected, reverse);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&left_before, (const uint8_t *)left, sizeof(left_before));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&right_before, (const uint8_t *)right, sizeof(right_before));
}

static void assert_equal(const configuration_t *left, const configuration_t *right)
{
    assert_equals_result(left, right, true);
}

static void assert_not_equal(const configuration_t *left, const configuration_t *right)
{
    assert_equals_result(left, right, false);
}

void setUp(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

static void test_relation_is_reflexive_symmetric_and_transitive(void)
{
    configuration_t first;
    configuration_t second;
    configuration_t third;

    make_default_configuration(&first);
    make_default_configuration(&second);
    make_default_configuration(&third);
    set_endpoint_hostname(&second.sntp.servers[0], "NTP.ALIYUN.COM");
    set_endpoint_hostname(&third.sntp.servers[0], "Ntp.Aliyun.Com");

    assert_equal(&first, &first);
    assert_equal(&first, &second);
    assert_equal(&second, &third);
    assert_equal(&first, &third);
}

static void test_padding_and_all_inactive_default_fields_are_ignored(void)
{
    configuration_t left;
    configuration_t right;

    make_manual_default(&left, 0x00U);
    make_manual_default(&right, 0xA5U);

    assert_equal(&left, &right);
}

static void test_network_mode_is_significant_but_dhcp_static_fields_are_ignored(void)
{
    configuration_t left;
    configuration_t right;

    make_default_configuration(&left);
    make_default_configuration(&right);
    configuration_test_set_ipv4(&right.network.ip_address, 127U, 0U, 0U, 1U);
    configuration_test_set_ipv4(&right.network.subnet_mask, 255U, 0U, 255U, 0U);
    configuration_test_set_ipv4(&right.network.gateway, 169U, 254U, 1U, 1U);
    configuration_test_set_ipv4(&right.network.dns_primary, 0U, 0U, 0U, 0U);
    configuration_test_set_ipv4(&right.network.dns_secondary, 0U, 0U, 0U, 0U);
    assert_equal(&left, &right);

    configuration_test_make_valid_static_network(&right);
    assert_not_equal(&left, &right);
}

static void test_each_static_network_field_is_significant(void)
{
    configuration_t left;
    configuration_t right;

    make_default_configuration(&left);
    configuration_test_make_valid_static_network(&left);
    right = left;
    configuration_test_set_ipv4(&right.network.ip_address, 192U, 168U, 10U, 11U);
    assert_not_equal(&left, &right);

    right = left;
    configuration_test_set_ipv4(&right.network.subnet_mask, 255U, 255U, 0U, 0U);
    assert_not_equal(&left, &right);

    right = left;
    configuration_test_set_ipv4(&right.network.gateway, 192U, 168U, 10U, 2U);
    assert_not_equal(&left, &right);

    right = left;
    configuration_test_set_ipv4(&right.network.dns_primary, 9U, 9U, 9U, 9U);
    assert_not_equal(&left, &right);

    right = left;
    configuration_test_set_ipv4(&right.network.dns_secondary, 208U, 67U, 222U, 222U);
    assert_not_equal(&left, &right);
}

static void test_rtu_and_modbus_tcp_fields_are_significant(void)
{
    configuration_t left;
    configuration_t right;

    make_default_configuration(&left);
    right = left;
    right.rtu.baud_rate = 19200U;
    assert_not_equal(&left, &right);

    right = left;
    right.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N1;
    assert_not_equal(&left, &right);

    right = left;
    right.rtu.first_byte_timeout_ms = 1001U;
    assert_not_equal(&left, &right);

    right = left;
    right.modbus_tcp.listen_port = 503U;
    assert_not_equal(&left, &right);
}

static void test_sntp_hostname_comparison_is_case_insensitive_but_content_sensitive(void)
{
    configuration_t left;
    configuration_t right;

    make_default_configuration(&left);
    right = left;
    set_endpoint_hostname(&right.sntp.servers[0], "NTP.ALIYUN.COM");
    assert_equal(&left, &right);

    right = left;
    set_endpoint_hostname(&right.sntp.servers[0], "ntp.example.com");
    assert_not_equal(&left, &right);

    right = left;
    set_endpoint_hostname(&right.sntp.servers[0], "xntp.aliyun.com");
    assert_not_equal(&left, &right);
}

static void test_sntp_type_ipv4_value_and_server_position_are_significant(void)
{
    configuration_t left;
    configuration_t right;
    configuration_endpoint_address_t temporary;

    make_default_configuration(&left);
    set_endpoint_ipv4(&left.sntp.servers[0], 10U, 0U, 0U, 1U);
    set_endpoint_ipv4(&left.sntp.servers[1], 10U, 0U, 0U, 2U);
    right = left;
    assert_equal(&left, &right);

    set_endpoint_ipv4(&right.sntp.servers[0], 10U, 0U, 0U, 3U);
    assert_not_equal(&left, &right);

    right = left;
    set_endpoint_hostname(&right.sntp.servers[0], "time-one.example");
    assert_not_equal(&left, &right);

    make_default_configuration(&left);
    right = left;
    temporary = right.sntp.servers[0];
    right.sntp.servers[0] = right.sntp.servers[1];
    right.sntp.servers[1] = temporary;
    assert_not_equal(&left, &right);
}

static void test_sntp_inactive_union_storage_and_hostname_tail_are_ignored(void)
{
    configuration_t left;
    configuration_t right;
    uint8_t *union_bytes;
    size_t index;

    make_default_configuration(&left);
    right = left;
    right.sntp.servers[0].value.hostname.bytes[right.sntp.servers[0].value.hostname.length + 1U] = 0xA5U;
    assert_equal(&left, &right);

    set_endpoint_ipv4(&left.sntp.servers[0], 10U, 0U, 0U, 1U);
    set_endpoint_ipv4(&left.sntp.servers[1], 10U, 0U, 0U, 2U);
    right = left;
    union_bytes = (uint8_t *)&right.sntp.servers[0].value;
    for (index = sizeof(ip4_addr_t); index < sizeof(right.sntp.servers[0].value); index++)
    {
        union_bytes[index] = 0x5AU;
    }
    assert_equal(&left, &right);
}

static void test_mqtt_mode_is_significant_but_disabled_fields_are_ignored(void)
{
    configuration_t left;
    configuration_t right;

    make_default_configuration(&left);
    right = left;
    memset(&right.mqtt.broker_address, 0xFF,
           sizeof(right.mqtt) - offsetof(configuration_mqtt_t, broker_address));
    right.mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    assert_equal(&left, &right);

    make_valid_mqtt_configuration(&right);
    assert_not_equal(&left, &right);
}

static void test_mqtt_broker_comparison_and_port(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    right = left;
    set_hostname_text(&right.mqtt.broker_address, "BROKER.EXAMPLE.COM");
    assert_equal(&left, &right);

    right = left;
    set_hostname_text(&right.mqtt.broker_address, "other.example.com");
    assert_not_equal(&left, &right);

    right = left;
    right.mqtt.broker_port = 8884U;
    assert_not_equal(&left, &right);
}

static void test_mqtt_client_id_mode_and_effective_value(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    right = left;
    right.mqtt.client_id.explicit_value.length = UINT16_MAX;
    memset(right.mqtt.client_id.explicit_value.bytes, 0xFF, sizeof(right.mqtt.client_id.explicit_value.bytes));
    assert_equal(&left, &right);

    right = left;
    right.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    TEST_ASSERT_TRUE(configuration_test_set_client_id(&right.mqtt.client_id.explicit_value,
                                                      (const uint8_t *)"Client1", 7U));
    assert_not_equal(&left, &right);

    left = right;
    right = left;
    TEST_ASSERT_TRUE(configuration_test_set_client_id(&right.mqtt.client_id.explicit_value,
                                                      (const uint8_t *)"Client2", 7U));
    assert_not_equal(&left, &right);

    right = left;
    right.mqtt.client_id.explicit_value.bytes[right.mqtt.client_id.explicit_value.length + 1U] = 0xA5U;
    assert_equal(&left, &right);
}

static void test_mqtt_authentication_certificate_and_keep_alive_are_exact(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    right = left;
    TEST_ASSERT_TRUE(configuration_test_set_username(&right.mqtt.username, (const uint8_t *)"User", 4U));
    assert_not_equal(&left, &right);

    right = left;
    TEST_ASSERT_TRUE(configuration_test_set_password(&right.mqtt.password, (const uint8_t *)"Password", 8U));
    assert_not_equal(&left, &right);

    right = left;
    TEST_ASSERT_TRUE(configuration_test_set_certificate_fixture(&right.mqtt.ca_certificate_pem,
                                                                 "alternate_root.pem"));
    assert_not_equal(&left, &right);

    right = left;
    TEST_ASSERT_TRUE(configuration_test_set_certificate_fixture(&right.mqtt.ca_certificate_pem,
                                                                 "valid_root_crlf.pem"));
    assert_not_equal(&left, &right);

    right = left;
    right.mqtt.keep_alive_seconds = 61U;
    assert_not_equal(&left, &right);
}

static void test_mqtt_variable_field_tails_are_ignored(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    right = left;
    right.mqtt.broker_address.bytes[right.mqtt.broker_address.length + 1U] = 0x11U;
    right.mqtt.username.bytes[right.mqtt.username.length + 1U] = 0x22U;
    right.mqtt.password.bytes[right.mqtt.password.length + 1U] = 0x33U;
    right.mqtt.ca_certificate_pem.bytes[right.mqtt.ca_certificate_pem.length + 1U] = 0x44U;
    right.collection.points[0].topic.bytes[right.collection.points[0].topic.length + 1U] = 0x55U;
    assert_equal(&left, &right);
}

static void test_disabled_message_fields_are_ignored_but_mode_is_significant(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    right = left;
    memset(&right.mqtt.online_message.topic, 0xFF,
           sizeof(right.mqtt.online_message) - offsetof(configuration_mqtt_message_t, topic));
    right.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    assert_equal(&left, &right);

    right = left;
    set_custom_message(&right.mqtt.online_message, "online", "payload", 0U, 0U);
    assert_not_equal(&left, &right);
}

static void test_each_custom_message_field_is_significant(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    set_custom_message(&left.mqtt.online_message, "online", "payload", 0U, 0U);
    right = left;
    set_topic_text(&right.mqtt.online_message.topic, "Online");
    assert_not_equal(&left, &right);

    right = left;
    set_payload_text(&right.mqtt.online_message.payload, "Payload");
    assert_not_equal(&left, &right);

    right = left;
    right.mqtt.online_message.qos = 1U;
    assert_not_equal(&left, &right);

    right = left;
    right.mqtt.online_message.retain = 1U;
    assert_not_equal(&left, &right);

    right = left;
    right.mqtt.online_message.topic.bytes[right.mqtt.online_message.topic.length + 1U] = 0xA5U;
    right.mqtt.online_message.payload.bytes[right.mqtt.online_message.payload.length + 1U] = 0x5AU;
    assert_equal(&left, &right);
}

static void test_online_and_will_message_roles_are_significant(void)
{
    configuration_t left;
    configuration_t right;
    configuration_mqtt_message_t temporary;

    make_valid_mqtt_configuration(&left);
    set_custom_message(&left.mqtt.online_message, "online", "up", 0U, 0U);
    set_custom_message(&left.mqtt.will_message, "offline", "down", 1U, 1U);
    right = left;
    temporary = right.mqtt.online_message;
    right.mqtt.online_message = right.mqtt.will_message;
    right.mqtt.will_message = temporary;
    assert_not_equal(&left, &right);

    set_custom_message(&left.mqtt.online_message, "status", "same", 1U, 1U);
    left.mqtt.will_message = left.mqtt.online_message;
    right = left;
    temporary = right.mqtt.online_message;
    right.mqtt.online_message = right.mqtt.will_message;
    right.mqtt.will_message = temporary;
    assert_equal(&left, &right);
}

static void test_collection_point_count_is_significant_and_unused_points_are_ignored(void)
{
    configuration_t left;
    configuration_t right;

    make_mqtt_with_points(&left, 1U);
    right = left;
    memset(&right.collection.points[1], 0xFF,
           sizeof(right.collection.points) - sizeof(right.collection.points[0]));
    assert_equal(&left, &right);

    make_mqtt_with_points(&right, 2U);
    assert_not_equal(&left, &right);
}

static void test_collection_order_is_ignored(void)
{
    configuration_t left;
    configuration_t right;
    configuration_collection_point_t temporary;

    make_mqtt_with_points(&left, 3U);
    right = left;
    temporary = right.collection.points[0];
    right.collection.points[0] = right.collection.points[2];
    right.collection.points[2] = temporary;

    assert_equal(&left, &right);
}

static void test_each_effective_collection_point_field_is_significant(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    right = left;
    right.collection.points[0].slave_address = 2U;
    assert_not_equal(&left, &right);

    right = left;
    right.collection.points[0].source = CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER;
    assert_not_equal(&left, &right);

    right = left;
    right.collection.points[0].address = 1U;
    assert_not_equal(&left, &right);

    right = left;
    right.collection.points[0].data_type = CONFIGURATION_DATA_TYPE_INT16;
    assert_not_equal(&left, &right);

    right = left;
    right.collection.points[0].poll_interval_ms = 60001U;
    assert_not_equal(&left, &right);

    right = left;
    right.collection.points[0].first_byte_timeout_ms = 1001U;
    assert_not_equal(&left, &right);

    right = left;
    set_topic_text(&right.collection.points[0].topic, "Points/0");
    assert_not_equal(&left, &right);

    right = left;
    right.collection.points[0].qos = 1U;
    assert_not_equal(&left, &right);
}

static void test_coil_and_discrete_input_data_type_is_ignored(void)
{
    configuration_t left;
    configuration_t right;

    make_valid_mqtt_configuration(&left);
    left.collection.points[0].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    left.collection.points[0].data_type = 10U;
    right = left;
    right.collection.points[0].data_type = 200U;
    assert_equal(&left, &right);

    left.collection.points[0].source = CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT;
    right = left;
    right.collection.points[0].data_type = UINT8_MAX;
    assert_equal(&left, &right);
}

int main(void)
{
    int unity_result;

    if (!configuration_test_use_standard_allocator())
    {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_relation_is_reflexive_symmetric_and_transitive);
    RUN_TEST(test_padding_and_all_inactive_default_fields_are_ignored);
    RUN_TEST(test_network_mode_is_significant_but_dhcp_static_fields_are_ignored);
    RUN_TEST(test_each_static_network_field_is_significant);
    RUN_TEST(test_rtu_and_modbus_tcp_fields_are_significant);
    RUN_TEST(test_sntp_hostname_comparison_is_case_insensitive_but_content_sensitive);
    RUN_TEST(test_sntp_type_ipv4_value_and_server_position_are_significant);
    RUN_TEST(test_sntp_inactive_union_storage_and_hostname_tail_are_ignored);
    RUN_TEST(test_mqtt_mode_is_significant_but_disabled_fields_are_ignored);
    RUN_TEST(test_mqtt_broker_comparison_and_port);
    RUN_TEST(test_mqtt_client_id_mode_and_effective_value);
    RUN_TEST(test_mqtt_authentication_certificate_and_keep_alive_are_exact);
    RUN_TEST(test_mqtt_variable_field_tails_are_ignored);
    RUN_TEST(test_disabled_message_fields_are_ignored_but_mode_is_significant);
    RUN_TEST(test_each_custom_message_field_is_significant);
    RUN_TEST(test_online_and_will_message_roles_are_significant);
    RUN_TEST(test_collection_point_count_is_significant_and_unused_points_are_ignored);
    RUN_TEST(test_collection_order_is_ignored);
    RUN_TEST(test_each_effective_collection_point_field_is_significant);
    RUN_TEST(test_coil_and_discrete_input_data_type_is_ignored);
    unity_result = UNITY_END();
    return unity_result;
}
