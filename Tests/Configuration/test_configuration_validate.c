#include "configuration.h"
#include "configuration_test_support.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

static void assert_validation_result(configuration_t *configuration,
                                     configuration_validation_result_t expected)
{
    configuration_t before;
    configuration_validation_result_t actual;

    memcpy(&before, configuration, sizeof(before));
    actual = configuration_validate(configuration);

    TEST_ASSERT_EQUAL_INT(expected, actual);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&before, (const uint8_t *)configuration, sizeof(before));
}

static void assert_valid(configuration_t *configuration)
{
    assert_validation_result(configuration, CONFIGURATION_VALIDATION_OK);
}

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

static void set_client_id_bytes(configuration_client_id_value_t *field, const uint8_t *bytes, size_t length)
{
    TEST_ASSERT_TRUE(configuration_test_set_client_id(field, bytes, length));
}

static void set_username_bytes(configuration_username_t *field, const uint8_t *bytes, size_t length)
{
    TEST_ASSERT_TRUE(configuration_test_set_username(field, bytes, length));
}

static void set_password_bytes(configuration_password_t *field, const uint8_t *bytes, size_t length)
{
    TEST_ASSERT_TRUE(configuration_test_set_password(field, bytes, length));
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

static size_t make_maximum_hostname(uint8_t *bytes)
{
    static const size_t label_lengths[] = {63U, 63U, 63U, 61U};
    size_t label_index;
    size_t offset = 0U;

    for (label_index = 0U; label_index < sizeof(label_lengths) / sizeof(label_lengths[0]); label_index++)
    {
        memset(&bytes[offset], (int)('a' + (int)label_index), label_lengths[label_index]);
        offset += label_lengths[label_index];
        if (label_index + 1U < sizeof(label_lengths) / sizeof(label_lengths[0]))
        {
            bytes[offset] = (uint8_t)'.';
            offset++;
        }
    }

    return offset;
}

static void make_default_configuration(configuration_t *configuration)
{
    memset(configuration, 0xA5, sizeof(*configuration));
    configuration_set_defaults(configuration);
}

static void make_static_configuration(configuration_t *configuration)
{
    make_default_configuration(configuration);
    configuration_test_make_valid_static_network(configuration);
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
    for (index = 0U; index < point_count && index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; index++)
    {
        TEST_ASSERT_TRUE(configuration_test_make_valid_point(&configuration->collection.points[index], index));
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

static void test_null_returns_invalid_argument(void)
{
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_INVALID_ARGUMENT, configuration_validate(NULL));
}

static void test_valid_default_and_static_configurations_are_accepted(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    assert_valid(&configuration);

    configuration_test_make_valid_static_network(&configuration);
    assert_valid(&configuration);
}

static void test_network_mode_and_dhcp_inactive_fields(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    configuration.network.mode = 2U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_default_configuration(&configuration);
    configuration.network.mode = UINT8_MAX;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_default_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 0U, 0U, 0U, 0U);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 255U, 0U, 255U, 0U);
    configuration_test_set_ipv4(&configuration.network.gateway, 127U, 0U, 0U, 1U);
    configuration_test_set_ipv4(&configuration.network.dns_primary, 169U, 254U, 1U, 1U);
    configuration_test_set_ipv4(&configuration.network.dns_secondary, 169U, 254U, 1U, 1U);
    assert_valid(&configuration);
}

static void test_static_subnet_mask_boundaries(void)
{
    configuration_t configuration;

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 10U, 0U, 0U, 2U);
    configuration_test_set_ipv4(&configuration.network.gateway, 10U, 0U, 0U, 1U);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 128U, 0U, 0U, 0U);
    assert_valid(&configuration);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 192U, 168U, 10U, 1U);
    configuration_test_set_ipv4(&configuration.network.gateway, 192U, 168U, 10U, 2U);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 255U, 255U, 255U, 252U);
    assert_valid(&configuration);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 0U, 0U, 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 255U, 255U, 255U, 254U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 255U, 255U, 255U, 255U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 255U, 0U, 255U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);
}

static void test_static_ip_address_rules(void)
{
    configuration_t configuration;

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 1U, 0U, 0U, 2U);
    configuration_test_set_ipv4(&configuration.network.gateway, 1U, 0U, 0U, 1U);
    configuration_test_set_ipv4(&configuration.network.subnet_mask, 255U, 0U, 0U, 0U);
    assert_valid(&configuration);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 223U, 1U, 1U, 2U);
    configuration_test_set_ipv4(&configuration.network.gateway, 223U, 1U, 1U, 1U);
    assert_valid(&configuration);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 0U, 0U, 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 0U, 1U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 224U, 1U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 127U, 0U, 0U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 169U, 254U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 192U, 168U, 10U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 192U, 168U, 10U, 255U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.ip_address, 192U, 168U, 11U, 10U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration.network.ip_address = configuration.network.gateway;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);
}

static void test_static_gateway_rules(void)
{
    configuration_t configuration;

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 0U, 0U, 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 0U, 1U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 255U, 1U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 127U, 0U, 0U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 169U, 254U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 192U, 168U, 10U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 192U, 168U, 10U, 255U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.gateway, 192U, 168U, 11U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);
}

static void test_static_dns_rules(void)
{
    configuration_t configuration;

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_primary, 9U, 9U, 9U, 9U);
    configuration_test_set_ipv4(&configuration.network.dns_secondary, 208U, 67U, 222U, 222U);
    assert_valid(&configuration);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_primary, 1U, 0U, 0U, 1U);
    configuration_test_set_ipv4(&configuration.network.dns_secondary, 223U, 0U, 0U, 1U);
    assert_valid(&configuration);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_primary, 0U, 0U, 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_secondary, 0U, 0U, 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_primary, 224U, 1U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_primary, 127U, 0U, 0U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration_test_set_ipv4(&configuration.network.dns_secondary, 169U, 254U, 1U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);

    make_static_configuration(&configuration);
    configuration.network.dns_secondary = configuration.network.dns_primary;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_NETWORK_INVALID);
}

static void test_all_documented_rtu_baud_rates_are_accepted(void)
{
    static const uint32_t baud_rates[] = {1200U, 2400U, 4800U, 9600U, 19200U, 38400U, 57600U, 115200U};
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(baud_rates) / sizeof(baud_rates[0]); index++)
    {
        make_default_configuration(&configuration);
        configuration.rtu.baud_rate = baud_rates[index];
        assert_valid(&configuration);
    }
}

static void test_undocumented_rtu_baud_rates_are_rejected(void)
{
    static const uint32_t baud_rates[] = {0U, 1199U, 1201U, 230400U, UINT32_MAX};
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(baud_rates) / sizeof(baud_rates[0]); index++)
    {
        make_default_configuration(&configuration);
        configuration.rtu.baud_rate = baud_rates[index];
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_RTU_INVALID);
    }
}

static void test_rtu_frame_format_and_timeout_boundaries(void)
{
    configuration_t configuration;
    uint8_t frame_format;

    for (frame_format = CONFIGURATION_FRAME_FORMAT_8N1; frame_format <= CONFIGURATION_FRAME_FORMAT_8N2;
         frame_format++)
    {
        make_default_configuration(&configuration);
        configuration.rtu.frame_format = frame_format;
        assert_valid(&configuration);
    }

    make_default_configuration(&configuration);
    configuration.rtu.frame_format = 4U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_RTU_INVALID);

    make_default_configuration(&configuration);
    configuration.rtu.frame_format = UINT8_MAX;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_RTU_INVALID);

    make_default_configuration(&configuration);
    configuration.rtu.first_byte_timeout_ms = 50U;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.rtu.first_byte_timeout_ms = 3000U;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.rtu.first_byte_timeout_ms = 49U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_RTU_INVALID);

    make_default_configuration(&configuration);
    configuration.rtu.first_byte_timeout_ms = 3001U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_RTU_INVALID);
}

static void test_modbus_tcp_port_boundaries(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    configuration.modbus_tcp.listen_port = 1U;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.modbus_tcp.listen_port = UINT16_MAX;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.modbus_tcp.listen_port = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MODBUS_TCP_INVALID);
}

static void test_sntp_endpoint_types_and_valid_ipv4_addresses(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    set_endpoint_ipv4(&configuration.sntp.servers[0], 10U, 0U, 0U, 1U);
    set_endpoint_ipv4(&configuration.sntp.servers[1], 223U, 1U, 1U, 1U);
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.sntp.servers[0].type = 2U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    make_default_configuration(&configuration);
    configuration.sntp.servers[1].type = UINT8_MAX;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);
}

static void test_sntp_hostname_length_and_label_boundaries(void)
{
    configuration_t configuration;
    uint8_t hostname[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t long_label[68];
    size_t hostname_length;

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "a");
    assert_valid(&configuration);

    hostname_length = make_maximum_hostname(hostname);
    TEST_ASSERT_EQUAL_UINT16(CONFIGURATION_HOSTNAME_MAX_LENGTH, hostname_length);
    make_default_configuration(&configuration);
    TEST_ASSERT_TRUE(configuration_test_set_hostname(&configuration.sntp.servers[0].value.hostname,
                                                      hostname, hostname_length));
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.sntp.servers[0].value.hostname.length = 0U;
    configuration.sntp.servers[0].value.hostname.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    make_default_configuration(&configuration);
    configuration.sntp.servers[0].value.hostname.length = CONFIGURATION_HOSTNAME_MAX_LENGTH + 1U;
    memset(configuration.sntp.servers[0].value.hostname.bytes, 'a',
           sizeof(configuration.sntp.servers[0].value.hostname.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    memset(long_label, 'a', 64U);
    memcpy(&long_label[64], ".com", 4U);
    make_default_configuration(&configuration);
    TEST_ASSERT_TRUE(configuration_test_set_hostname(&configuration.sntp.servers[0].value.hostname,
                                                      long_label, sizeof(long_label)));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);
}

static void test_sntp_hostname_syntax_and_byte_representation(void)
{
    static const char *invalid_hostnames[] = {
        "-host.example", "host-.example", ".host.example", "host..example", "host.example.",
        "host_name", "host name", "12345", "192.0.2.1"
    };
    configuration_t configuration;
    size_t index;

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "Host-123");
    assert_valid(&configuration);

    for (index = 0U; index < sizeof(invalid_hostnames) / sizeof(invalid_hostnames[0]); index++)
    {
        make_default_configuration(&configuration);
        set_endpoint_hostname(&configuration.sntp.servers[0], invalid_hostnames[index]);
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);
    }

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "host.example");
    configuration.sntp.servers[0].value.hostname.bytes[4] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "host.example");
    configuration.sntp.servers[0].value.hostname.bytes[
        configuration.sntp.servers[0].value.hostname.length] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "host.example");
    configuration.sntp.servers[0].value.hostname.bytes[
        configuration.sntp.servers[0].value.hostname.length + 1U] = 0xFFU;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "h.example");
    configuration.sntp.servers[0].value.hostname.bytes[0] = 0x80U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);
}

static void test_sntp_ipv4_restrictions(void)
{
    static const uint8_t invalid_addresses[][4] = {
        {0U, 0U, 0U, 0U}, {224U, 0U, 0U, 1U}, {255U, 1U, 1U, 1U},
        {127U, 0U, 0U, 1U}, {169U, 254U, 1U, 1U}
    };
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(invalid_addresses) / sizeof(invalid_addresses[0]); index++)
    {
        make_default_configuration(&configuration);
        set_endpoint_ipv4(&configuration.sntp.servers[0], invalid_addresses[index][0],
                          invalid_addresses[index][1], invalid_addresses[index][2], invalid_addresses[index][3]);
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);
    }
}

static void test_sntp_duplicate_and_mixed_address_rules(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "TIME.EXAMPLE.COM");
    set_endpoint_hostname(&configuration.sntp.servers[1], "time.example.com");
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    make_default_configuration(&configuration);
    set_endpoint_ipv4(&configuration.sntp.servers[0], 10U, 0U, 0U, 1U);
    set_endpoint_ipv4(&configuration.sntp.servers[1], 10U, 0U, 0U, 1U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_SNTP_INVALID);

    make_default_configuration(&configuration);
    set_endpoint_hostname(&configuration.sntp.servers[0], "ten.example");
    set_endpoint_ipv4(&configuration.sntp.servers[1], 10U, 0U, 0U, 1U);
    assert_valid(&configuration);
}

static void test_mqtt_mode_and_disabled_inactive_fields(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    memset(&configuration.mqtt, 0xFF, sizeof(configuration.mqtt));
    configuration.mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    configuration.collection.point_count = 0U;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.mqtt.mode = 2U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_default_configuration(&configuration);
    configuration.mqtt.mode = UINT8_MAX;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
}

static void test_mqtt_enabled_baseline_broker_and_port_rules(void)
{
    configuration_t configuration;
    uint8_t hostname[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    size_t hostname_length;

    make_valid_mqtt_configuration(&configuration);
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_hostname_text(&configuration.mqtt.broker_address, "broker");
    configuration.mqtt.broker_port = 1U;
    assert_valid(&configuration);

    hostname_length = make_maximum_hostname(hostname);
    make_valid_mqtt_configuration(&configuration);
    TEST_ASSERT_TRUE(configuration_test_set_hostname(&configuration.mqtt.broker_address,
                                                      hostname, hostname_length));
    configuration.mqtt.broker_port = UINT16_MAX;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_hostname_text(&configuration.mqtt.broker_address, "192.0.2.1");
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_hostname_text(&configuration.mqtt.broker_address, "broker_name");
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.broker_address.length = 0U;
    configuration.mqtt.broker_address.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.broker_address.bytes[3] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.broker_address.bytes[configuration.mqtt.broker_address.length] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.broker_address.bytes[configuration.mqtt.broker_address.length + 1U] = 0xFFU;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.broker_port = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
}

static void test_mqtt_client_id_modes_boundaries_and_characters(void)
{
    static const uint8_t valid_one[] = {'A'};
    static const uint8_t invalid_underscore[] = {'A', '_'};
    static const uint8_t invalid_hyphen[] = {'A', '-'};
    static const uint8_t invalid_non_ascii[] = {'A', 0x80U};
    configuration_t configuration;
    uint8_t maximum[CONFIGURATION_CLIENT_ID_MAX_LENGTH];

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.explicit_value.length = UINT16_MAX;
    memset(configuration.mqtt.client_id.explicit_value.bytes, 0xFF,
           sizeof(configuration.mqtt.client_id.explicit_value.bytes));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = 2U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, valid_one, sizeof(valid_one));
    assert_valid(&configuration);

    memset(maximum, 'A', sizeof(maximum));
    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, maximum, sizeof(maximum));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    configuration.mqtt.client_id.explicit_value.length = 0U;
    configuration.mqtt.client_id.explicit_value.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    configuration.mqtt.client_id.explicit_value.length = CONFIGURATION_CLIENT_ID_MAX_LENGTH + 1U;
    memset(configuration.mqtt.client_id.explicit_value.bytes, 'A',
           sizeof(configuration.mqtt.client_id.explicit_value.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, invalid_underscore,
                        sizeof(invalid_underscore));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, invalid_hyphen, sizeof(invalid_hyphen));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, invalid_non_ascii,
                        sizeof(invalid_non_ascii));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, (const uint8_t *)"ABC", 3U);
    configuration.mqtt.client_id.explicit_value.bytes[1] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, (const uint8_t *)"ABC", 3U);
    configuration.mqtt.client_id.explicit_value.bytes[3] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    set_client_id_bytes(&configuration.mqtt.client_id.explicit_value, (const uint8_t *)"ABC", 3U);
    configuration.mqtt.client_id.explicit_value.bytes[4] = 0xFFU;
    assert_valid(&configuration);
}

static void test_mqtt_username_boundaries_and_characters(void)
{
    static const uint8_t valid_one[] = {'A'};
    static const uint8_t printable_boundaries[] = {0x20U, 0x7EU};
    static const uint8_t invalid_low[] = {0x1FU};
    static const uint8_t invalid_high[] = {0x7FU};
    static const uint8_t invalid_non_ascii[] = {0x80U};
    configuration_t configuration;
    uint8_t maximum[CONFIGURATION_USERNAME_MAX_LENGTH];

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, valid_one, sizeof(valid_one));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, printable_boundaries, sizeof(printable_boundaries));
    assert_valid(&configuration);

    memset(maximum, 'U', sizeof(maximum));
    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, maximum, sizeof(maximum));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.username.length = 0U;
    configuration.mqtt.username.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.username.length = CONFIGURATION_USERNAME_MAX_LENGTH + 1U;
    memset(configuration.mqtt.username.bytes, 'U', sizeof(configuration.mqtt.username.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, invalid_low, sizeof(invalid_low));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, invalid_high, sizeof(invalid_high));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, invalid_non_ascii, sizeof(invalid_non_ascii));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, (const uint8_t *)"user", 4U);
    configuration.mqtt.username.bytes[2] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, (const uint8_t *)"user", 4U);
    configuration.mqtt.username.bytes[4] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_username_bytes(&configuration.mqtt.username, (const uint8_t *)"user", 4U);
    configuration.mqtt.username.bytes[5] = 0xFFU;
    assert_valid(&configuration);
}

static void test_mqtt_password_boundaries_and_characters(void)
{
    static const uint8_t valid_one[] = {'A'};
    static const uint8_t printable_boundaries[] = {0x20U, 0x7EU};
    static const uint8_t invalid_low[] = {0x1FU};
    static const uint8_t invalid_high[] = {0x7FU};
    static const uint8_t invalid_non_ascii[] = {0x80U};
    configuration_t configuration;
    uint8_t maximum[CONFIGURATION_PASSWORD_MAX_LENGTH];

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, valid_one, sizeof(valid_one));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, printable_boundaries, sizeof(printable_boundaries));
    assert_valid(&configuration);

    memset(maximum, 'P', sizeof(maximum));
    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, maximum, sizeof(maximum));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.password.length = 0U;
    configuration.mqtt.password.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.password.length = CONFIGURATION_PASSWORD_MAX_LENGTH + 1U;
    memset(configuration.mqtt.password.bytes, 'P', sizeof(configuration.mqtt.password.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, invalid_low, sizeof(invalid_low));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, invalid_high, sizeof(invalid_high));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, invalid_non_ascii, sizeof(invalid_non_ascii));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, (const uint8_t *)"password", 8U);
    configuration.mqtt.password.bytes[4] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, (const uint8_t *)"password", 8U);
    configuration.mqtt.password.bytes[8] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_password_bytes(&configuration.mqtt.password, (const uint8_t *)"password", 8U);
    configuration.mqtt.password.bytes[9] = 0xFFU;
    assert_valid(&configuration);
}

static void test_mqtt_keep_alive_boundaries(void)
{
    configuration_t configuration;

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.keep_alive_seconds = 30U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.keep_alive_seconds = 3600U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.keep_alive_seconds = 29U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.keep_alive_seconds = 3601U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
}

static void test_disabled_mqtt_messages_ignore_their_other_fields(void)
{
    configuration_t configuration;

    make_valid_mqtt_configuration(&configuration);
    memset(&configuration.mqtt.online_message, 0xFF, sizeof(configuration.mqtt.online_message));
    configuration.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    memset(&configuration.mqtt.will_message, 0xFF, sizeof(configuration.mqtt.will_message));
    configuration.mqtt.will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.online_message.mode = 2U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.will_message.mode = UINT8_MAX;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
}

static void test_custom_mqtt_topic_boundaries_and_allowed_forms(void)
{
    static const char *valid_topics[] = {"a", "/status", "status/", "a//b", "/", "A.b_c-d/0"};
    configuration_t configuration;
    uint8_t maximum[CONFIGURATION_TOPIC_MAX_LENGTH];
    size_t index;

    for (index = 0U; index < sizeof(valid_topics) / sizeof(valid_topics[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        set_custom_message(&configuration.mqtt.online_message, valid_topics[index], "online", 0U, 0U);
        assert_valid(&configuration);
    }

    memset(maximum, 'a', sizeof(maximum));
    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    TEST_ASSERT_TRUE(configuration_test_set_topic(&configuration.mqtt.online_message.topic,
                                                   maximum, sizeof(maximum)));
    set_payload_text(&configuration.mqtt.online_message.payload, "online");
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "a", "online", 0U, 0U);
    configuration.mqtt.online_message.topic.length = 0U;
    configuration.mqtt.online_message.topic.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "a", "online", 0U, 0U);
    configuration.mqtt.online_message.topic.length = CONFIGURATION_TOPIC_MAX_LENGTH + 1U;
    memset(configuration.mqtt.online_message.topic.bytes, 'a',
           sizeof(configuration.mqtt.online_message.topic.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
}

static void test_custom_mqtt_topic_rejects_invalid_bytes_and_honors_sentinel(void)
{
    static const char *invalid_topics[] = {"bad+topic", "bad#topic", "bad topic"};
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(invalid_topics) / sizeof(invalid_topics[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        set_custom_message(&configuration.mqtt.online_message, invalid_topics[index], "online", 0U, 0U);
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
    }

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "topic", "online", 0U, 0U);
    configuration.mqtt.online_message.topic.bytes[0] = 0x80U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "topic", "online", 0U, 0U);
    configuration.mqtt.online_message.topic.bytes[2] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "topic", "online", 0U, 0U);
    configuration.mqtt.online_message.topic.bytes[5] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "topic", "online", 0U, 0U);
    configuration.mqtt.online_message.topic.bytes[6] = 0xFFU;
    assert_valid(&configuration);
}

static void test_custom_mqtt_payload_boundaries_and_valid_utf8(void)
{
    static const uint8_t utf8_payload[] = {'a', '\n', '\t', 0x01U, 0xE4U, 0xB8U, 0xADU};
    configuration_t configuration;
    uint8_t maximum[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    set_topic_text(&configuration.mqtt.online_message.topic, "online");
    TEST_ASSERT_TRUE(configuration_test_set_payload(&configuration.mqtt.online_message.payload,
                                                     utf8_payload, sizeof(utf8_payload)));
    assert_valid(&configuration);

    memset(maximum, 'p', sizeof(maximum));
    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    set_topic_text(&configuration.mqtt.online_message.topic, "online");
    TEST_ASSERT_TRUE(configuration_test_set_payload(&configuration.mqtt.online_message.payload,
                                                     maximum, sizeof(maximum)));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "online", "p", 0U, 0U);
    configuration.mqtt.online_message.payload.length = 0U;
    configuration.mqtt.online_message.payload.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "online", "p", 0U, 0U);
    configuration.mqtt.online_message.payload.length = CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH + 1U;
    memset(configuration.mqtt.online_message.payload.bytes, 'p',
           sizeof(configuration.mqtt.online_message.payload.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
}

static void test_custom_mqtt_payload_rejects_invalid_utf8_and_honors_sentinel(void)
{
    static const uint8_t lone_continuation[] = {0x80U};
    static const uint8_t truncated_sequence[] = {0xE2U, 0x82U};
    static const uint8_t overlong_sequence[] = {0xC0U, 0xAFU};
    static const uint8_t surrogate[] = {0xEDU, 0xA0U, 0x80U};
    static const uint8_t out_of_range[] = {0xF4U, 0x90U, 0x80U, 0x80U};
    static const struct
    {
        const uint8_t *bytes;
        size_t length;
    } invalid_payloads[] = {
        {lone_continuation, sizeof(lone_continuation)},
        {truncated_sequence, sizeof(truncated_sequence)},
        {overlong_sequence, sizeof(overlong_sequence)},
        {surrogate, sizeof(surrogate)},
        {out_of_range, sizeof(out_of_range)}
    };
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(invalid_payloads) / sizeof(invalid_payloads[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        configuration.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
        set_topic_text(&configuration.mqtt.online_message.topic, "online");
        TEST_ASSERT_TRUE(configuration_test_set_payload(&configuration.mqtt.online_message.payload,
                                                         invalid_payloads[index].bytes,
                                                         invalid_payloads[index].length));
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);
    }

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "online", "payload", 0U, 0U);
    configuration.mqtt.online_message.payload.bytes[3] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "online", "payload", 0U, 0U);
    configuration.mqtt.online_message.payload.bytes[7] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "online", "payload", 0U, 0U);
    configuration.mqtt.online_message.payload.bytes[8] = 0xFFU;
    assert_valid(&configuration);
}

static void test_custom_mqtt_qos_retain_and_message_topic_relationships(void)
{
    configuration_t configuration;
    uint8_t qos;
    uint8_t retain;

    for (qos = 0U; qos <= 2U; qos++)
    {
        for (retain = 0U; retain <= 1U; retain++)
        {
            make_valid_mqtt_configuration(&configuration);
            set_custom_message(&configuration.mqtt.online_message, "status", "online", qos, retain);
            assert_valid(&configuration);
        }
    }

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "status", "online", 3U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "status", "online", 0U, 2U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_MQTT_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_custom_message(&configuration.mqtt.online_message, "status", "same", 1U, 1U);
    set_custom_message(&configuration.mqtt.will_message, "status", "same", 1U, 1U);
    assert_valid(&configuration);
}

static void test_certificate_accepts_all_documented_valid_forms(void)
{
    static const char *fixtures[] = {
        "valid_root.pem", "alternate_root.pem", "valid_root_crlf.pem", "valid_root_no_key_usage.pem",
        "expired_root.pem", "future_root.pem", "valid_ecdsa_root.pem"
    };
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        TEST_ASSERT_TRUE(configuration_test_set_certificate_fixture(&configuration.mqtt.ca_certificate_pem,
                                                                     fixtures[index]));
        assert_valid(&configuration);
    }
}

static void test_certificate_rejects_invalid_encodings_and_multiple_certificates(void)
{
    static const char *fixtures[] = {"malformed.pem", "valid_root.der", "bundle.pem"};
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        TEST_ASSERT_TRUE(configuration_test_set_certificate_fixture(&configuration.mqtt.ca_certificate_pem,
                                                                     fixtures[index]));
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_CERTIFICATE_INVALID);
    }
}

static void test_certificate_rejects_non_root_and_unsupported_certificates(void)
{
    static const char *fixtures[] = {
        "not_ca.pem", "intermediate_ca.pem", "cross_signed_ca.pem", "wrong_key_usage.pem",
        "bad_self_signature.pem", "unsupported_algorithm.pem"
    };
    configuration_t configuration;
    size_t index;

    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        TEST_ASSERT_TRUE(configuration_test_set_certificate_fixture(&configuration.mqtt.ca_certificate_pem,
                                                                     fixtures[index]));
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_CERTIFICATE_INVALID);
    }
}

static void test_certificate_byte_field_boundaries_and_inactive_tail(void)
{
    configuration_t configuration;
    uint16_t certificate_length;

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.ca_certificate_pem.length = 0U;
    configuration.mqtt.ca_certificate_pem.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_CERTIFICATE_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.ca_certificate_pem.length = CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH + 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_CERTIFICATE_INVALID);

    make_valid_mqtt_configuration(&configuration);
    certificate_length = configuration.mqtt.ca_certificate_pem.length;
    configuration.mqtt.ca_certificate_pem.bytes[certificate_length] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_CERTIFICATE_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.mqtt.ca_certificate_pem.bytes[10] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_CERTIFICATE_INVALID);

    make_valid_mqtt_configuration(&configuration);
    certificate_length = configuration.mqtt.ca_certificate_pem.length;
    configuration.mqtt.ca_certificate_pem.bytes[certificate_length + 1U] = 0xFFU;
    assert_valid(&configuration);
}

static void test_certificate_allocation_failure_is_resource_unavailable(void)
{
    configuration_t configuration;
    configuration_t before;
    configuration_validation_result_t result;

    make_valid_mqtt_configuration(&configuration);
    memcpy(&before, &configuration, sizeof(before));
    TEST_ASSERT_TRUE(configuration_test_use_failing_allocator());
    result = configuration_validate(&configuration);
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE, result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&before, (const uint8_t *)&configuration, sizeof(before));
}

static void test_collection_mode_and_point_count_boundaries(void)
{
    configuration_t configuration;

    make_default_configuration(&configuration);
    configuration.collection.point_count = 0U;
    assert_valid(&configuration);

    make_default_configuration(&configuration);
    configuration.collection.point_count = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.point_count = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_mqtt_with_points(&configuration, 1U);
    assert_valid(&configuration);

    make_mqtt_with_points(&configuration, CONFIGURATION_COLLECTION_POINT_MAX_COUNT);
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.point_count = CONFIGURATION_COLLECTION_POINT_MAX_COUNT + 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
}

static void test_collection_ignores_unused_points(void)
{
    configuration_t configuration;

    make_mqtt_with_points(&configuration, 1U);
    memset(&configuration.collection.points[1], 0xFF,
           sizeof(configuration.collection.points) - sizeof(configuration.collection.points[0]));
    assert_valid(&configuration);
}

static void test_collection_slave_address_and_register_address_boundaries(void)
{
    configuration_t configuration;

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].slave_address = 1U;
    configuration.collection.points[0].address = 0U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].slave_address = 247U;
    configuration.collection.points[0].address = UINT16_MAX;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].slave_address = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].slave_address = 248U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
}

static void test_collection_source_and_data_type_rules(void)
{
    configuration_t configuration;
    uint8_t source;
    uint8_t data_type;

    for (source = CONFIGURATION_COLLECTION_SOURCE_COIL;
         source <= CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT; source++)
    {
        make_valid_mqtt_configuration(&configuration);
        configuration.collection.points[0].source = source;
        configuration.collection.points[0].data_type = UINT8_MAX;
        assert_valid(&configuration);
    }

    for (source = CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER;
         source <= CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER; source++)
    {
        for (data_type = CONFIGURATION_DATA_TYPE_UINT16; data_type <= CONFIGURATION_DATA_TYPE_INT16; data_type++)
        {
            make_valid_mqtt_configuration(&configuration);
            configuration.collection.points[0].source = source;
            configuration.collection.points[0].data_type = data_type;
            assert_valid(&configuration);
        }

        make_valid_mqtt_configuration(&configuration);
        configuration.collection.points[0].source = source;
        configuration.collection.points[0].data_type = 2U;
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
    }

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].source = 4U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
}

static void test_collection_poll_and_timeout_boundaries_are_independent(void)
{
    configuration_t configuration;

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].poll_interval_ms = 1000U;
    configuration.collection.points[0].first_byte_timeout_ms = 3000U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].poll_interval_ms = 3600000U;
    configuration.collection.points[0].first_byte_timeout_ms = 50U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].poll_interval_ms = 999U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].poll_interval_ms = 3600001U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].first_byte_timeout_ms = 49U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].first_byte_timeout_ms = 3001U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
}

static void test_collection_topic_boundaries_and_characters(void)
{
    static const char *valid_topics[] = {"a", "/status", "status/", "a//b", "/", "A.b_c-d/0"};
    static const char *invalid_topics[] = {"bad+topic", "bad#topic", "bad topic"};
    configuration_t configuration;
    uint8_t maximum[CONFIGURATION_TOPIC_MAX_LENGTH];
    size_t index;

    for (index = 0U; index < sizeof(valid_topics) / sizeof(valid_topics[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        set_topic_text(&configuration.collection.points[0].topic, valid_topics[index]);
        assert_valid(&configuration);
    }

    memset(maximum, 't', sizeof(maximum));
    make_valid_mqtt_configuration(&configuration);
    TEST_ASSERT_TRUE(configuration_test_set_topic(&configuration.collection.points[0].topic,
                                                   maximum, sizeof(maximum)));
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].topic.length = 0U;
    configuration.collection.points[0].topic.bytes[0] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].topic.length = CONFIGURATION_TOPIC_MAX_LENGTH + 1U;
    memset(configuration.collection.points[0].topic.bytes, 't',
           sizeof(configuration.collection.points[0].topic.bytes));
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    for (index = 0U; index < sizeof(invalid_topics) / sizeof(invalid_topics[0]); index++)
    {
        make_valid_mqtt_configuration(&configuration);
        set_topic_text(&configuration.collection.points[0].topic, invalid_topics[index]);
        assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
    }

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "topic");
    configuration.collection.points[0].topic.bytes[0] = 0x80U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "topic");
    configuration.collection.points[0].topic.bytes[2] = 0U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "topic");
    configuration.collection.points[0].topic.bytes[5] = 1U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "topic");
    configuration.collection.points[0].topic.bytes[6] = 0xFFU;
    assert_valid(&configuration);
}

static void test_collection_qos_boundaries(void)
{
    configuration_t configuration;

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].qos = 0U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].qos = 1U;
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    configuration.collection.points[0].qos = 2U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
}

static void test_collection_allows_duplicate_addresses_with_unique_topics(void)
{
    configuration_t configuration;

    make_mqtt_with_points(&configuration, 2U);
    configuration.collection.points[1].slave_address = configuration.collection.points[0].slave_address;
    configuration.collection.points[1].source = configuration.collection.points[0].source;
    configuration.collection.points[1].address = configuration.collection.points[0].address;
    set_topic_text(&configuration.collection.points[0].topic, "same-address/0");
    set_topic_text(&configuration.collection.points[1].topic, "same-address/1");
    assert_valid(&configuration);
}

static void test_collection_topic_uniqueness_is_case_sensitive(void)
{
    configuration_t configuration;

    make_mqtt_with_points(&configuration, 2U);
    set_topic_text(&configuration.collection.points[0].topic, "Topic");
    set_topic_text(&configuration.collection.points[1].topic, "topic");
    assert_valid(&configuration);

    make_mqtt_with_points(&configuration, 2U);
    set_topic_text(&configuration.collection.points[0].topic, "duplicate");
    set_topic_text(&configuration.collection.points[1].topic, "duplicate");
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);
}

static void test_collection_topic_conflicts_with_only_enabled_messages(void)
{
    configuration_t configuration;

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "status");
    set_custom_message(&configuration.mqtt.online_message, "status", "online", 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "status");
    set_custom_message(&configuration.mqtt.will_message, "status", "offline", 0U, 0U);
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_COLLECTION_INVALID);

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "Status");
    set_custom_message(&configuration.mqtt.online_message, "status", "online", 0U, 0U);
    assert_valid(&configuration);

    make_valid_mqtt_configuration(&configuration);
    set_topic_text(&configuration.collection.points[0].topic, "status");
    set_topic_text(&configuration.mqtt.online_message.topic, "status");
    configuration.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    assert_valid(&configuration);
}

static void test_bus_utilization_accepts_exactly_half_and_rejects_more(void)
{
    configuration_t configuration;

    make_mqtt_with_points(&configuration, 3U);
    configuration.rtu.baud_rate = 1200U;
    configuration.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration.collection.points[0].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[1].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[2].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[0].data_type = UINT8_MAX;
    configuration.collection.points[1].data_type = UINT8_MAX;
    configuration.collection.points[2].data_type = UINT8_MAX;
    configuration.collection.points[0].poll_interval_ms = 1000U;
    configuration.collection.points[1].poll_interval_ms = 1000U;
    configuration.collection.points[2].poll_interval_ms = 1674U;
    assert_valid(&configuration);

    configuration.collection.points[2].poll_interval_ms = 1673U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED);
}

static void test_bus_utilization_accounts_for_frame_format_and_response_size(void)
{
    configuration_t configuration;

    make_mqtt_with_points(&configuration, 3U);
    configuration.rtu.baud_rate = 1200U;
    configuration.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N1;
    configuration.collection.points[0].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[1].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[2].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[0].poll_interval_ms = 1000U;
    configuration.collection.points[1].poll_interval_ms = 1000U;
    configuration.collection.points[2].poll_interval_ms = 1673U;
    assert_valid(&configuration);

    configuration.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED);

    make_mqtt_with_points(&configuration, 3U);
    configuration.rtu.baud_rate = 1200U;
    configuration.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration.collection.points[0].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[1].source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    configuration.collection.points[2].source = CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER;
    configuration.collection.points[2].data_type = CONFIGURATION_DATA_TYPE_UINT16;
    configuration.collection.points[0].poll_interval_ms = 1000U;
    configuration.collection.points[1].poll_interval_ms = 1000U;
    configuration.collection.points[2].poll_interval_ms = 1674U;
    assert_validation_result(&configuration, CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED);
}

static void test_high_baud_rate_utilization_is_within_limit_for_maximum_points(void)
{
    configuration_t configuration;
    uint8_t index;

    make_mqtt_with_points(&configuration, CONFIGURATION_COLLECTION_POINT_MAX_COUNT);
    configuration.rtu.baud_rate = 19200U;
    configuration.rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    for (index = 0U; index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; index++)
    {
        configuration.collection.points[index].poll_interval_ms = 1000U;
    }
    assert_valid(&configuration);

    configuration.rtu.baud_rate = 38400U;
    assert_valid(&configuration);
}

int main(void)
{
    int unity_result;

    if (!configuration_test_use_standard_allocator())
    {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_null_returns_invalid_argument);
    RUN_TEST(test_valid_default_and_static_configurations_are_accepted);
    RUN_TEST(test_network_mode_and_dhcp_inactive_fields);
    RUN_TEST(test_static_subnet_mask_boundaries);
    RUN_TEST(test_static_ip_address_rules);
    RUN_TEST(test_static_gateway_rules);
    RUN_TEST(test_static_dns_rules);
    RUN_TEST(test_all_documented_rtu_baud_rates_are_accepted);
    RUN_TEST(test_undocumented_rtu_baud_rates_are_rejected);
    RUN_TEST(test_rtu_frame_format_and_timeout_boundaries);
    RUN_TEST(test_modbus_tcp_port_boundaries);
    RUN_TEST(test_sntp_endpoint_types_and_valid_ipv4_addresses);
    RUN_TEST(test_sntp_hostname_length_and_label_boundaries);
    RUN_TEST(test_sntp_hostname_syntax_and_byte_representation);
    RUN_TEST(test_sntp_ipv4_restrictions);
    RUN_TEST(test_sntp_duplicate_and_mixed_address_rules);
    RUN_TEST(test_mqtt_mode_and_disabled_inactive_fields);
    RUN_TEST(test_mqtt_enabled_baseline_broker_and_port_rules);
    RUN_TEST(test_mqtt_client_id_modes_boundaries_and_characters);
    RUN_TEST(test_mqtt_username_boundaries_and_characters);
    RUN_TEST(test_mqtt_password_boundaries_and_characters);
    RUN_TEST(test_mqtt_keep_alive_boundaries);
    RUN_TEST(test_disabled_mqtt_messages_ignore_their_other_fields);
    RUN_TEST(test_custom_mqtt_topic_boundaries_and_allowed_forms);
    RUN_TEST(test_custom_mqtt_topic_rejects_invalid_bytes_and_honors_sentinel);
    RUN_TEST(test_custom_mqtt_payload_boundaries_and_valid_utf8);
    RUN_TEST(test_custom_mqtt_payload_rejects_invalid_utf8_and_honors_sentinel);
    RUN_TEST(test_custom_mqtt_qos_retain_and_message_topic_relationships);
    RUN_TEST(test_certificate_accepts_all_documented_valid_forms);
    RUN_TEST(test_certificate_rejects_invalid_encodings_and_multiple_certificates);
    RUN_TEST(test_certificate_rejects_non_root_and_unsupported_certificates);
    RUN_TEST(test_certificate_byte_field_boundaries_and_inactive_tail);
    RUN_TEST(test_certificate_allocation_failure_is_resource_unavailable);
    RUN_TEST(test_collection_mode_and_point_count_boundaries);
    RUN_TEST(test_collection_ignores_unused_points);
    RUN_TEST(test_collection_slave_address_and_register_address_boundaries);
    RUN_TEST(test_collection_source_and_data_type_rules);
    RUN_TEST(test_collection_poll_and_timeout_boundaries_are_independent);
    RUN_TEST(test_collection_topic_boundaries_and_characters);
    RUN_TEST(test_collection_qos_boundaries);
    RUN_TEST(test_collection_allows_duplicate_addresses_with_unique_topics);
    RUN_TEST(test_collection_topic_uniqueness_is_case_sensitive);
    RUN_TEST(test_collection_topic_conflicts_with_only_enabled_messages);
    RUN_TEST(test_bus_utilization_accepts_exactly_half_and_rejects_more);
    RUN_TEST(test_bus_utilization_accounts_for_frame_format_and_response_size);
    RUN_TEST(test_high_baud_rate_utilization_is_within_limit_for_maximum_points);
    unity_result = UNITY_END();
    return unity_result;
}
