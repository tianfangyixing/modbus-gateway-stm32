#include "configuration_binary_codec.h"
#include "configuration_test_support.h"
#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_GUARD_SIZE 16U
#define TEST_CANARY UINT8_C(0xC3)
#define TEST_OUTPUT_FILL UINT8_C(0xA5)
#define TEST_MAX_BUFFER_SIZE (CONFIGURATION_V1_MAX_PAYLOAD_LENGTH + 1U)
#define TEST_OFFSET_UNSET SIZE_MAX
#define MINIMAL_RTU_BAUD_OFFSET 2U
#define MINIMAL_RTU_FRAME_OFFSET 6U
#define MINIMAL_RTU_TIMEOUT_OFFSET 7U
#define MINIMAL_TCP_PORT_OFFSET 9U
#define MINIMAL_SECOND_HOSTNAME_BYTE_OFFSET 18U

typedef struct
{
    uint8_t *bytes;
    size_t capacity;
    size_t length;
    bool valid;
} wire_builder_t;

typedef struct
{
    size_t network_mode;
    size_t network_ip_address;
    size_t network_gateway;
    size_t rtu_baud_rate;
    size_t rtu_frame_format;
    size_t modbus_tcp_port;
    size_t endpoint_type[2];
    size_t endpoint_hostname_length[2];
    size_t mqtt_mode;
    size_t broker_length;
    size_t broker_port;
    size_t client_id_mode;
    size_t client_id_length;
    size_t username_length;
    size_t password_length;
    size_t certificate_length;
    size_t certificate_bytes;
    size_t online_mode;
    size_t online_topic_length;
    size_t online_payload_length;
    size_t online_qos;
    size_t online_retain;
    size_t will_mode;
    size_t will_topic_length;
    size_t will_payload_length;
    size_t will_qos;
    size_t will_retain;
    size_t point_count;
    size_t point_source[4];
    size_t point_data_type[4];
    size_t point_topic_length[4];
    size_t point_qos[4];
} wire_offsets_t;

typedef struct
{
    uint8_t before[TEST_GUARD_SIZE];
    configuration_t configuration;
    uint8_t after[TEST_GUARD_SIZE];
} guarded_configuration_t;

typedef struct
{
    uint8_t before[TEST_GUARD_SIZE];
    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    uint8_t after[TEST_GUARD_SIZE];
} guarded_payload_t;

_Static_assert(offsetof(guarded_configuration_t, configuration) == TEST_GUARD_SIZE,
               "guarded configuration has unexpected leading padding");
_Static_assert(offsetof(guarded_configuration_t, after) == TEST_GUARD_SIZE + sizeof(configuration_t),
               "guarded configuration has unexpected trailing padding");
_Static_assert(sizeof(configuration_hostname_t) ==
                   offsetof(configuration_hostname_t, bytes) + sizeof(((configuration_hostname_t *)0)->bytes),
               "hostname storage has unexpected trailing padding");
_Static_assert(sizeof(((configuration_endpoint_address_t *)0)->value) == sizeof(configuration_hostname_t),
               "endpoint union has unexpected trailing padding");

static const uint8_t minimal_v1[] = {
    0x01U, 0x00U, 0x80U, 0x25U, 0x00U, 0x00U, 0x03U, 0xE8U, 0x03U, 0xF6U, 0x01U,
    0x00U, 0x01U, 0x00U, 0x61U, 0x00U, 0x01U, 0x00U, 0x62U, 0x00U, 0x00U
};

static const uint8_t default_v1[] = {
    0x01U, 0x00U, 0x80U, 0x25U, 0x00U, 0x00U, 0x03U, 0xE8U, 0x03U, 0xF6U, 0x01U,
    0x00U, 0x0EU, 0x00U, 0x6EU, 0x74U, 0x70U, 0x2EU, 0x61U, 0x6CU, 0x69U, 0x79U,
    0x75U, 0x6EU, 0x2EU, 0x63U, 0x6FU, 0x6DU, 0x00U, 0x0FU, 0x00U, 0x6EU, 0x74U,
    0x70U, 0x2EU, 0x74U, 0x65U, 0x6EU, 0x63U, 0x65U, 0x6EU, 0x74U, 0x2EU, 0x63U,
    0x6FU, 0x6DU, 0x00U, 0x00U
};

static uint8_t payload_snapshot[TEST_MAX_BUFFER_SIZE];
static guarded_payload_t guarded_payload;

static const uint8_t hostname_a[] = "a";
static const uint8_t hostname_b[] = "b";
static const uint8_t default_sntp_primary[] = "ntp.aliyun.com";
static const uint8_t default_sntp_secondary[] = "ntp.tencent.com";
static const uint8_t full_sntp_hostname[] = "time.example.com";
static const uint8_t full_broker[] = "broker.example.com";
static const uint8_t full_client_id[] = "Client42";
static const uint8_t full_username[] = "user";
static const uint8_t full_password[] = "pass word";
static const uint8_t full_online_topic[] = "gateway/online";
static const uint8_t full_online_payload[] = "{\"online\":true}";
static const uint8_t point_topic_coil[] = "points/coil";
static const uint8_t point_topic_discrete[] = "points/discrete";
static const uint8_t point_topic_holding[] = "points/holding";
static const uint8_t point_topic_input[] = "points/input";
static const uint8_t derived_broker[] = "broker.example.com";
static const uint8_t derived_username[] = "user";
static const uint8_t derived_password[] = "password";
static const uint8_t derived_point_topic[] = "points/0";

static void wire_offsets_reset(wire_offsets_t *offsets)
{
    memset(offsets, 0xFF, sizeof(*offsets));
}

static void wire_builder_start(wire_builder_t *builder, uint8_t *bytes, size_t capacity)
{
    builder->bytes = bytes;
    builder->capacity = capacity;
    builder->length = 0U;
    builder->valid = bytes != NULL;
}

static void wire_builder_append_bytes(wire_builder_t *builder, const uint8_t *bytes, size_t length)
{
    if (!builder->valid)
    {
        return;
    }
    if ((bytes == NULL && length > 0U) || length > builder->capacity - builder->length)
    {
        builder->valid = false;
        return;
    }

    if (length > 0U)
    {
        memcpy(&builder->bytes[builder->length], bytes, length);
    }
    builder->length += length;
}

static void wire_builder_append_u8(wire_builder_t *builder, uint8_t value)
{
    wire_builder_append_bytes(builder, &value, sizeof(value));
}

static void wire_builder_append_u16(wire_builder_t *builder, uint16_t value)
{
    uint8_t bytes[2];

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    wire_builder_append_bytes(builder, bytes, sizeof(bytes));
}

static void wire_builder_append_u32(wire_builder_t *builder, uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
    wire_builder_append_bytes(builder, bytes, sizeof(bytes));
}

static void wire_builder_append_ipv4(wire_builder_t *builder, uint8_t first, uint8_t second, uint8_t third,
                                     uint8_t fourth)
{
    uint8_t bytes[4];

    bytes[0] = first;
    bytes[1] = second;
    bytes[2] = third;
    bytes[3] = fourth;
    wire_builder_append_bytes(builder, bytes, sizeof(bytes));
}

static void wire_builder_append_text(wire_builder_t *builder, const uint8_t *bytes, size_t length)
{
    if (length > UINT16_MAX)
    {
        builder->valid = false;
        return;
    }

    wire_builder_append_u16(builder, (uint16_t)length);
    wire_builder_append_bytes(builder, bytes, length);
}

static bool wire_builder_finish(const wire_builder_t *builder, size_t *length)
{
    if (!builder->valid || length == NULL)
    {
        return false;
    }

    *length = builder->length;
    return true;
}

static void wire_write_u16_at(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static bool load_certificate_fixture(const char *name, uint8_t *bytes, uint16_t *length)
{
    size_t loaded_length;

    if (!configuration_test_load_fixture(name, bytes, CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH, &loaded_length) ||
        loaded_length > UINT16_MAX)
    {
        return false;
    }

    *length = (uint16_t)loaded_length;
    return true;
}

static bool set_hostname(configuration_endpoint_address_t *endpoint, const uint8_t *bytes, size_t length)
{
    endpoint->type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
    return configuration_test_set_hostname(&endpoint->value.hostname, bytes, length);
}

static void set_endpoint_ipv4(configuration_endpoint_address_t *endpoint, uint8_t first, uint8_t second,
                              uint8_t third, uint8_t fourth)
{
    endpoint->type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4;
    configuration_test_set_ipv4(&endpoint->value.ipv4, first, second, third, fourth);
}

static bool make_minimal_configuration(configuration_t *configuration)
{
    memset(configuration, 0, sizeof(*configuration));
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    configuration->rtu.baud_rate = UINT32_C(9600);
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration->rtu.first_byte_timeout_ms = UINT16_C(1000);
    configuration->modbus_tcp.listen_port = UINT16_C(502);
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;

    return set_hostname(&configuration->sntp.servers[0], hostname_a, sizeof(hostname_a) - 1U) &&
           set_hostname(&configuration->sntp.servers[1], hostname_b, sizeof(hostname_b) - 1U);
}

static bool make_default_configuration(configuration_t *configuration)
{
    memset(configuration, 0, sizeof(*configuration));
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    configuration->rtu.baud_rate = UINT32_C(9600);
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration->rtu.first_byte_timeout_ms = UINT16_C(1000);
    configuration->modbus_tcp.listen_port = UINT16_C(502);
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;

    return set_hostname(&configuration->sntp.servers[0], default_sntp_primary,
                        sizeof(default_sntp_primary) - 1U) &&
           set_hostname(&configuration->sntp.servers[1], default_sntp_secondary,
                        sizeof(default_sntp_secondary) - 1U);
}

static bool make_full_configuration(configuration_t *configuration)
{
    configuration_collection_point_t *point;
    bool fields_valid;

    memset(configuration, 0, sizeof(*configuration));
    configuration_test_make_valid_static_network(configuration);
    configuration->rtu.baud_rate = UINT32_C(115200);
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8E1;
    configuration->rtu.first_byte_timeout_ms = UINT16_C(250);
    configuration->modbus_tcp.listen_port = UINT16_C(1502);
    fields_valid = set_hostname(&configuration->sntp.servers[0], full_sntp_hostname,
                                sizeof(full_sntp_hostname) - 1U);
    set_endpoint_ipv4(&configuration->sntp.servers[1], 9U, 9U, 9U, 9U);

    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_ENABLED;
    configuration->mqtt.broker_port = UINT16_C(8883);
    configuration->mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    configuration->mqtt.keep_alive_seconds = UINT16_C(60);
    configuration->mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    configuration->mqtt.online_message.qos = 2U;
    configuration->mqtt.online_message.retain = 1U;
    configuration->mqtt.will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    fields_valid = fields_valid &&
                   configuration_test_set_hostname(&configuration->mqtt.broker_address, full_broker,
                                                   sizeof(full_broker) - 1U) &&
                   configuration_test_set_client_id(&configuration->mqtt.client_id.explicit_value, full_client_id,
                                                    sizeof(full_client_id) - 1U) &&
                   configuration_test_set_username(&configuration->mqtt.username, full_username,
                                                   sizeof(full_username) - 1U) &&
                   configuration_test_set_password(&configuration->mqtt.password, full_password,
                                                   sizeof(full_password) - 1U) &&
                   configuration_test_set_certificate_fixture(&configuration->mqtt.ca_certificate_pem,
                                                              "valid_root.pem") &&
                   configuration_test_set_topic(&configuration->mqtt.online_message.topic, full_online_topic,
                                                sizeof(full_online_topic) - 1U) &&
                   configuration_test_set_payload(&configuration->mqtt.online_message.payload, full_online_payload,
                                                  sizeof(full_online_payload) - 1U);

    configuration->collection.point_count = 4U;
    point = &configuration->collection.points[0];
    point->slave_address = 1U;
    point->source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    point->address = UINT16_C(0x0010);
    point->poll_interval_ms = UINT32_C(60000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 0U;
    fields_valid = fields_valid && configuration_test_set_topic(&point->topic, point_topic_coil,
                                                                 sizeof(point_topic_coil) - 1U);

    point = &configuration->collection.points[1];
    point->slave_address = 2U;
    point->source = CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT;
    point->address = UINT16_C(0x0020);
    point->poll_interval_ms = UINT32_C(61000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 1U;
    fields_valid = fields_valid && configuration_test_set_topic(&point->topic, point_topic_discrete,
                                                                 sizeof(point_topic_discrete) - 1U);

    point = &configuration->collection.points[2];
    point->slave_address = 3U;
    point->source = CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER;
    point->address = UINT16_C(0x1234);
    point->data_type = CONFIGURATION_DATA_TYPE_UINT16;
    point->poll_interval_ms = UINT32_C(62000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 0U;
    fields_valid = fields_valid && configuration_test_set_topic(&point->topic, point_topic_holding,
                                                                 sizeof(point_topic_holding) - 1U);

    point = &configuration->collection.points[3];
    point->slave_address = 4U;
    point->source = CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER;
    point->address = UINT16_C(0xABCD);
    point->data_type = CONFIGURATION_DATA_TYPE_INT16;
    point->poll_interval_ms = UINT32_C(63000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 1U;
    fields_valid = fields_valid && configuration_test_set_topic(&point->topic, point_topic_input,
                                                                 sizeof(point_topic_input) - 1U);
    return fields_valid;
}

static bool make_derived_configuration(configuration_t *configuration)
{
    configuration_collection_point_t *point;
    bool fields_valid;

    memset(configuration, 0, sizeof(*configuration));
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    configuration->rtu.baud_rate = UINT32_C(9600);
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration->rtu.first_byte_timeout_ms = UINT16_C(1000);
    configuration->modbus_tcp.listen_port = UINT16_C(502);
    fields_valid = set_hostname(&configuration->sntp.servers[0], default_sntp_primary,
                                sizeof(default_sntp_primary) - 1U) &&
                   set_hostname(&configuration->sntp.servers[1], default_sntp_secondary,
                                sizeof(default_sntp_secondary) - 1U);

    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_ENABLED;
    configuration->mqtt.broker_port = UINT16_C(8883);
    configuration->mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_DERIVED;
    configuration->mqtt.keep_alive_seconds = UINT16_C(60);
    configuration->mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    configuration->mqtt.will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    fields_valid = fields_valid &&
                   configuration_test_set_hostname(&configuration->mqtt.broker_address, derived_broker,
                                                   sizeof(derived_broker) - 1U) &&
                   configuration_test_set_username(&configuration->mqtt.username, derived_username,
                                                   sizeof(derived_username) - 1U) &&
                   configuration_test_set_password(&configuration->mqtt.password, derived_password,
                                                   sizeof(derived_password) - 1U) &&
                   configuration_test_set_certificate_fixture(&configuration->mqtt.ca_certificate_pem,
                                                              "valid_root.pem");

    configuration->collection.point_count = 1U;
    point = &configuration->collection.points[0];
    point->slave_address = 1U;
    point->source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    point->address = 0U;
    point->poll_interval_ms = UINT32_C(60000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 0U;
    fields_valid = fields_valid && configuration_test_set_topic(&point->topic, derived_point_topic,
                                                                 sizeof(derived_point_topic) - 1U);
    return fields_valid;
}

static void wire_builder_append_point(wire_builder_t *builder, wire_offsets_t *offsets, size_t index,
                                      uint8_t slave_address, uint8_t source, uint16_t address,
                                      bool include_data_type, uint8_t data_type, uint32_t poll_interval_ms,
                                      uint16_t first_byte_timeout_ms, const uint8_t *topic, size_t topic_length,
                                      uint8_t qos)
{
    wire_builder_append_u8(builder, slave_address);
    if (offsets != NULL && index < 4U)
    {
        offsets->point_source[index] = builder->length;
    }
    wire_builder_append_u8(builder, source);
    wire_builder_append_u16(builder, address);
    if (include_data_type)
    {
        if (offsets != NULL && index < 4U)
        {
            offsets->point_data_type[index] = builder->length;
        }
        wire_builder_append_u8(builder, data_type);
    }
    wire_builder_append_u32(builder, poll_interval_ms);
    wire_builder_append_u16(builder, first_byte_timeout_ms);
    if (offsets != NULL && index < 4U)
    {
        offsets->point_topic_length[index] = builder->length;
    }
    wire_builder_append_text(builder, topic, topic_length);
    if (offsets != NULL && index < 4U)
    {
        offsets->point_qos[index] = builder->length;
    }
    wire_builder_append_u8(builder, qos);
}

static bool build_full_payload(uint8_t *payload, size_t capacity, size_t *payload_length,
                               wire_offsets_t *offsets)
{
    uint8_t certificate[CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH];
    uint16_t certificate_length;
    wire_builder_t builder;

    if (!load_certificate_fixture("valid_root.pem", certificate, &certificate_length))
    {
        return false;
    }

    wire_offsets_reset(offsets);
    wire_builder_start(&builder, payload, capacity);
    wire_builder_append_u8(&builder, CONFIGURATION_SCHEMA_VERSION);
    offsets->network_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_NETWORK_MODE_STATIC);
    offsets->network_ip_address = builder.length;
    wire_builder_append_ipv4(&builder, 192U, 168U, 10U, 10U);
    wire_builder_append_ipv4(&builder, 255U, 255U, 255U, 0U);
    offsets->network_gateway = builder.length;
    wire_builder_append_ipv4(&builder, 192U, 168U, 10U, 1U);
    wire_builder_append_ipv4(&builder, 8U, 8U, 8U, 8U);
    wire_builder_append_ipv4(&builder, 1U, 1U, 1U, 1U);
    offsets->rtu_baud_rate = builder.length;
    wire_builder_append_u32(&builder, UINT32_C(115200));
    offsets->rtu_frame_format = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_FRAME_FORMAT_8E1);
    wire_builder_append_u16(&builder, UINT16_C(250));
    offsets->modbus_tcp_port = builder.length;
    wire_builder_append_u16(&builder, UINT16_C(1502));

    offsets->endpoint_type[0] = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    offsets->endpoint_hostname_length[0] = builder.length;
    wire_builder_append_text(&builder, full_sntp_hostname, sizeof(full_sntp_hostname) - 1U);
    offsets->endpoint_type[1] = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4);
    wire_builder_append_ipv4(&builder, 9U, 9U, 9U, 9U);

    offsets->mqtt_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MODE_ENABLED);
    offsets->broker_length = builder.length;
    wire_builder_append_text(&builder, full_broker, sizeof(full_broker) - 1U);
    offsets->broker_port = builder.length;
    wire_builder_append_u16(&builder, UINT16_C(8883));
    offsets->client_id_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_CLIENT_ID_MODE_EXPLICIT);
    offsets->client_id_length = builder.length;
    wire_builder_append_text(&builder, full_client_id, sizeof(full_client_id) - 1U);
    offsets->username_length = builder.length;
    wire_builder_append_text(&builder, full_username, sizeof(full_username) - 1U);
    offsets->password_length = builder.length;
    wire_builder_append_text(&builder, full_password, sizeof(full_password) - 1U);
    offsets->certificate_length = builder.length;
    wire_builder_append_u16(&builder, certificate_length);
    offsets->certificate_bytes = builder.length;
    wire_builder_append_bytes(&builder, certificate, certificate_length);
    wire_builder_append_u16(&builder, UINT16_C(60));

    offsets->online_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM);
    offsets->online_topic_length = builder.length;
    wire_builder_append_text(&builder, full_online_topic, sizeof(full_online_topic) - 1U);
    offsets->online_payload_length = builder.length;
    wire_builder_append_text(&builder, full_online_payload, sizeof(full_online_payload) - 1U);
    offsets->online_qos = builder.length;
    wire_builder_append_u8(&builder, 2U);
    offsets->online_retain = builder.length;
    wire_builder_append_u8(&builder, 1U);
    offsets->will_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED);

    offsets->point_count = builder.length;
    wire_builder_append_u8(&builder, 4U);
    wire_builder_append_point(&builder, offsets, 0U, 1U, CONFIGURATION_COLLECTION_SOURCE_COIL,
                              UINT16_C(0x0010), false, 0U, UINT32_C(60000), UINT16_C(1000), point_topic_coil,
                              sizeof(point_topic_coil) - 1U, 0U);
    wire_builder_append_point(&builder, offsets, 1U, 2U, CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT,
                              UINT16_C(0x0020), false, 0U, UINT32_C(61000), UINT16_C(1000), point_topic_discrete,
                              sizeof(point_topic_discrete) - 1U, 1U);
    wire_builder_append_point(&builder, offsets, 2U, 3U, CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER,
                              UINT16_C(0x1234), true, CONFIGURATION_DATA_TYPE_UINT16, UINT32_C(62000),
                              UINT16_C(1000), point_topic_holding, sizeof(point_topic_holding) - 1U, 0U);
    wire_builder_append_point(&builder, offsets, 3U, 4U, CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER,
                              UINT16_C(0xABCD), true, CONFIGURATION_DATA_TYPE_INT16, UINT32_C(63000),
                              UINT16_C(1000), point_topic_input, sizeof(point_topic_input) - 1U, 1U);
    return wire_builder_finish(&builder, payload_length);
}

static bool build_derived_payload(uint8_t *payload, size_t capacity, size_t *payload_length,
                                  wire_offsets_t *offsets)
{
    uint8_t certificate[CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH];
    uint16_t certificate_length;
    wire_builder_t builder;

    if (!load_certificate_fixture("valid_root.pem", certificate, &certificate_length))
    {
        return false;
    }

    wire_offsets_reset(offsets);
    wire_builder_start(&builder, payload, capacity);
    wire_builder_append_u8(&builder, CONFIGURATION_SCHEMA_VERSION);
    offsets->network_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_NETWORK_MODE_DHCP);
    offsets->rtu_baud_rate = builder.length;
    wire_builder_append_u32(&builder, UINT32_C(9600));
    offsets->rtu_frame_format = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_FRAME_FORMAT_8N2);
    wire_builder_append_u16(&builder, UINT16_C(1000));
    offsets->modbus_tcp_port = builder.length;
    wire_builder_append_u16(&builder, UINT16_C(502));

    offsets->endpoint_type[0] = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    offsets->endpoint_hostname_length[0] = builder.length;
    wire_builder_append_text(&builder, default_sntp_primary, sizeof(default_sntp_primary) - 1U);
    offsets->endpoint_type[1] = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    offsets->endpoint_hostname_length[1] = builder.length;
    wire_builder_append_text(&builder, default_sntp_secondary, sizeof(default_sntp_secondary) - 1U);

    offsets->mqtt_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MODE_ENABLED);
    offsets->broker_length = builder.length;
    wire_builder_append_text(&builder, derived_broker, sizeof(derived_broker) - 1U);
    offsets->broker_port = builder.length;
    wire_builder_append_u16(&builder, UINT16_C(8883));
    offsets->client_id_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_CLIENT_ID_MODE_DERIVED);
    offsets->username_length = builder.length;
    wire_builder_append_text(&builder, derived_username, sizeof(derived_username) - 1U);
    offsets->password_length = builder.length;
    wire_builder_append_text(&builder, derived_password, sizeof(derived_password) - 1U);
    offsets->certificate_length = builder.length;
    wire_builder_append_u16(&builder, certificate_length);
    offsets->certificate_bytes = builder.length;
    wire_builder_append_bytes(&builder, certificate, certificate_length);
    wire_builder_append_u16(&builder, UINT16_C(60));
    offsets->online_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED);
    offsets->will_mode = builder.length;
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED);
    offsets->point_count = builder.length;
    wire_builder_append_u8(&builder, 1U);
    wire_builder_append_point(&builder, offsets, 0U, 1U, CONFIGURATION_COLLECTION_SOURCE_COIL, 0U, false, 0U,
                              UINT32_C(60000), UINT16_C(1000), derived_point_topic,
                              sizeof(derived_point_topic) - 1U, 0U);
    return wire_builder_finish(&builder, payload_length);
}

static void assert_endpoint_equal(const configuration_endpoint_address_t *expected,
                                  const configuration_endpoint_address_t *actual)
{
    TEST_ASSERT_EQUAL_UINT8(expected->type, actual->type);
    if (expected->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
    {
        TEST_ASSERT_EQUAL_UINT16(expected->value.hostname.length, actual->value.hostname.length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->value.hostname.bytes, actual->value.hostname.bytes,
                                      sizeof(expected->value.hostname.bytes));
    }
    else
    {
        TEST_ASSERT_EQUAL_UINT8(ip4_addr1(&expected->value.ipv4), ip4_addr1(&actual->value.ipv4));
        TEST_ASSERT_EQUAL_UINT8(ip4_addr2(&expected->value.ipv4), ip4_addr2(&actual->value.ipv4));
        TEST_ASSERT_EQUAL_UINT8(ip4_addr3(&expected->value.ipv4), ip4_addr3(&actual->value.ipv4));
        TEST_ASSERT_EQUAL_UINT8(ip4_addr4(&expected->value.ipv4), ip4_addr4(&actual->value.ipv4));
        TEST_ASSERT_EACH_EQUAL_HEX8(0U, &((const uint8_t *)&actual->value)[sizeof(ip4_addr_t)],
                                    sizeof(actual->value) - sizeof(ip4_addr_t));
    }
}

static void assert_message_equal(const configuration_mqtt_message_t *expected,
                                 const configuration_mqtt_message_t *actual)
{
    TEST_ASSERT_EQUAL_UINT8(expected->mode, actual->mode);
    TEST_ASSERT_EQUAL_UINT16(expected->topic.length, actual->topic.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->topic.bytes, actual->topic.bytes, sizeof(expected->topic.bytes));
    TEST_ASSERT_EQUAL_UINT16(expected->payload.length, actual->payload.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->payload.bytes, actual->payload.bytes, sizeof(expected->payload.bytes));
    TEST_ASSERT_EQUAL_UINT8(expected->qos, actual->qos);
    TEST_ASSERT_EQUAL_UINT8(expected->retain, actual->retain);
}

static void assert_point_equal(const configuration_collection_point_t *expected,
                               const configuration_collection_point_t *actual)
{
    TEST_ASSERT_EQUAL_UINT8(expected->slave_address, actual->slave_address);
    TEST_ASSERT_EQUAL_UINT8(expected->source, actual->source);
    TEST_ASSERT_EQUAL_UINT16(expected->address, actual->address);
    TEST_ASSERT_EQUAL_UINT8(expected->data_type, actual->data_type);
    TEST_ASSERT_EQUAL_UINT32(expected->poll_interval_ms, actual->poll_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(expected->first_byte_timeout_ms, actual->first_byte_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(expected->topic.length, actual->topic.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->topic.bytes, actual->topic.bytes, sizeof(expected->topic.bytes));
    TEST_ASSERT_EQUAL_UINT8(expected->qos, actual->qos);
}

static void assert_configuration_equal(const configuration_t *expected, const configuration_t *actual)
{
    size_t index;

    TEST_ASSERT_EQUAL_UINT8(expected->network.mode, actual->network.mode);
    TEST_ASSERT_EQUAL_UINT32(expected->network.ip_address.addr, actual->network.ip_address.addr);
    TEST_ASSERT_EQUAL_UINT32(expected->network.subnet_mask.addr, actual->network.subnet_mask.addr);
    TEST_ASSERT_EQUAL_UINT32(expected->network.gateway.addr, actual->network.gateway.addr);
    TEST_ASSERT_EQUAL_UINT32(expected->network.dns_primary.addr, actual->network.dns_primary.addr);
    TEST_ASSERT_EQUAL_UINT32(expected->network.dns_secondary.addr, actual->network.dns_secondary.addr);
    TEST_ASSERT_EQUAL_UINT32(expected->rtu.baud_rate, actual->rtu.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(expected->rtu.frame_format, actual->rtu.frame_format);
    TEST_ASSERT_EQUAL_UINT16(expected->rtu.first_byte_timeout_ms, actual->rtu.first_byte_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(expected->modbus_tcp.listen_port, actual->modbus_tcp.listen_port);
    assert_endpoint_equal(&expected->sntp.servers[0], &actual->sntp.servers[0]);
    assert_endpoint_equal(&expected->sntp.servers[1], &actual->sntp.servers[1]);

    TEST_ASSERT_EQUAL_UINT8(expected->mqtt.mode, actual->mqtt.mode);
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.broker_address.length, actual->mqtt.broker_address.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->mqtt.broker_address.bytes, actual->mqtt.broker_address.bytes,
                                  sizeof(expected->mqtt.broker_address.bytes));
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.broker_port, actual->mqtt.broker_port);
    TEST_ASSERT_EQUAL_UINT8(expected->mqtt.client_id.mode, actual->mqtt.client_id.mode);
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.client_id.explicit_value.length,
                             actual->mqtt.client_id.explicit_value.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->mqtt.client_id.explicit_value.bytes,
                                  actual->mqtt.client_id.explicit_value.bytes,
                                  sizeof(expected->mqtt.client_id.explicit_value.bytes));
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.username.length, actual->mqtt.username.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->mqtt.username.bytes, actual->mqtt.username.bytes,
                                  sizeof(expected->mqtt.username.bytes));
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.password.length, actual->mqtt.password.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->mqtt.password.bytes, actual->mqtt.password.bytes,
                                  sizeof(expected->mqtt.password.bytes));
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.ca_certificate_pem.length, actual->mqtt.ca_certificate_pem.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->mqtt.ca_certificate_pem.bytes, actual->mqtt.ca_certificate_pem.bytes,
                                  sizeof(expected->mqtt.ca_certificate_pem.bytes));
    TEST_ASSERT_EQUAL_UINT16(expected->mqtt.keep_alive_seconds, actual->mqtt.keep_alive_seconds);
    assert_message_equal(&expected->mqtt.online_message, &actual->mqtt.online_message);
    assert_message_equal(&expected->mqtt.will_message, &actual->mqtt.will_message);
    TEST_ASSERT_EQUAL_UINT8(expected->collection.point_count, actual->collection.point_count);

    for (index = 0U; index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; index++)
    {
        assert_point_equal(&expected->collection.points[index], &actual->collection.points[index]);
    }
}

static void assert_bytes_are(uint8_t expected, const uint8_t *bytes, size_t length, const char *message)
{
    if (length > 0U)
    {
        TEST_ASSERT_EACH_EQUAL_HEX8_MESSAGE(expected, bytes, length, message);
    }
}

static void prepare_guarded_configuration(guarded_configuration_t *guarded)
{
    memset(guarded, TEST_OUTPUT_FILL, sizeof(*guarded));
    memset(guarded->before, TEST_CANARY, sizeof(guarded->before));
    memset(guarded->after, TEST_CANARY, sizeof(guarded->after));
}

static void assert_decode_call(const uint8_t *payload, uint32_t payload_length,
                               configuration_binary_codec_result_t expected_result,
                               guarded_configuration_t *guarded, const char *case_name)
{
    configuration_binary_codec_result_t actual_result;

    TEST_ASSERT_TRUE_MESSAGE(payload != NULL, case_name);
    TEST_ASSERT_TRUE_MESSAGE(payload_length <= sizeof(payload_snapshot), case_name);
    prepare_guarded_configuration(guarded);
    memcpy(payload_snapshot, payload, payload_length);

    actual_result = configuration_binary_decode(payload, payload_length, &guarded->configuration);

    TEST_ASSERT_EQUAL_INT_MESSAGE(expected_result, actual_result, case_name);
    if (payload_length > 0U)
    {
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(payload_snapshot, payload, payload_length, case_name);
    }
    assert_bytes_are(TEST_CANARY, guarded->before, sizeof(guarded->before), case_name);
    assert_bytes_are(TEST_CANARY, guarded->after, sizeof(guarded->after), case_name);
}

static void prepare_guarded_payload(uint32_t payload_capacity)
{
    TEST_ASSERT_TRUE(payload_capacity <= sizeof(guarded_payload.payload));
    memset(&guarded_payload, TEST_CANARY, sizeof(guarded_payload));
    memset(guarded_payload.payload, TEST_OUTPUT_FILL, payload_capacity);
}

static void assert_encode_call(const configuration_t *configuration, uint32_t payload_capacity,
                               configuration_binary_codec_result_t expected_result, uint32_t *encoded_length,
                               const char *case_name)
{
    configuration_t before;
    configuration_validation_result_t validation_result;
    configuration_binary_codec_result_t actual_result;
    uint32_t payload_length = UINT32_C(0xDEADBEEF);

    TEST_ASSERT_TRUE_MESSAGE(configuration != NULL, case_name);
    TEST_ASSERT_TRUE_MESSAGE(payload_capacity <= sizeof(guarded_payload.payload), case_name);
    validation_result = configuration_validate(configuration);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CONFIGURATION_VALIDATION_OK, validation_result, case_name);
    memcpy(&before, configuration, sizeof(before));
    prepare_guarded_payload(payload_capacity);

    actual_result = configuration_binary_encode(configuration, guarded_payload.payload, payload_capacity,
                                                &payload_length);

    TEST_ASSERT_EQUAL_INT_MESSAGE(expected_result, actual_result, case_name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)&before, (const uint8_t *)configuration,
                                          sizeof(before), case_name);
    assert_bytes_are(TEST_CANARY, guarded_payload.before, sizeof(guarded_payload.before), case_name);
    assert_bytes_are(TEST_CANARY, &guarded_payload.payload[payload_capacity],
                     sizeof(guarded_payload.payload) - payload_capacity, case_name);
    assert_bytes_are(TEST_CANARY, guarded_payload.after, sizeof(guarded_payload.after), case_name);
    if (expected_result == CONFIGURATION_BINARY_CODEC_OK)
    {
        TEST_ASSERT_TRUE_MESSAGE(payload_length <= payload_capacity, case_name);
        *encoded_length = payload_length;
    }
}

static size_t make_maximum_hostname(uint8_t *bytes, uint8_t label_byte)
{
    static const size_t label_lengths[] = {63U, 63U, 63U, 61U};
    size_t label_index;
    size_t offset = 0U;

    for (label_index = 0U; label_index < sizeof(label_lengths) / sizeof(label_lengths[0]); label_index++)
    {
        memset(&bytes[offset], label_byte, label_lengths[label_index]);
        offset += label_lengths[label_index];
        if (label_index + 1U < sizeof(label_lengths) / sizeof(label_lengths[0]))
        {
            bytes[offset] = (uint8_t)'.';
            offset++;
        }
    }
    return offset;
}

static void make_indexed_topic(uint8_t *bytes, size_t length, uint8_t index)
{
    memset(bytes, 'p', length);
    bytes[length - 2U] = (uint8_t)('0' + index / 10U);
    bytes[length - 1U] = (uint8_t)('0' + index % 10U);
}

static bool make_text_boundary_configuration(configuration_t *configuration, bool maximum)
{
    uint8_t sntp_primary[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t sntp_secondary[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t broker[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t client_id[CONFIGURATION_CLIENT_ID_MAX_LENGTH];
    uint8_t username[CONFIGURATION_USERNAME_MAX_LENGTH];
    uint8_t password[CONFIGURATION_PASSWORD_MAX_LENGTH];
    uint8_t online_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t will_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t point_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t online_payload[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];
    uint8_t will_payload[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];
    size_t sntp_primary_length;
    size_t sntp_secondary_length;
    size_t broker_length;
    size_t client_id_length;
    size_t username_length;
    size_t password_length;
    size_t message_topic_length;
    size_t message_payload_length;
    size_t point_topic_length;
    bool fields_valid;
    configuration_collection_point_t *point;

    if (maximum)
    {
        sntp_primary_length = make_maximum_hostname(sntp_primary, 'a');
        sntp_secondary_length = make_maximum_hostname(sntp_secondary, 'b');
        broker_length = make_maximum_hostname(broker, 'c');
        client_id_length = sizeof(client_id);
        username_length = sizeof(username);
        password_length = sizeof(password);
        message_topic_length = sizeof(online_topic);
        message_payload_length = sizeof(online_payload);
        point_topic_length = sizeof(point_topic);
        memset(client_id, 'C', sizeof(client_id));
        memset(username, 'u', sizeof(username));
        memset(password, 'p', sizeof(password));
        memset(online_topic, 'o', sizeof(online_topic));
        memset(will_topic, 'w', sizeof(will_topic));
        memset(point_topic, 't', sizeof(point_topic));
        memset(online_payload, 'x', sizeof(online_payload));
        memset(will_payload, 'y', sizeof(will_payload));
    }
    else
    {
        sntp_primary[0] = 'a';
        sntp_secondary[0] = 'b';
        broker[0] = 'c';
        client_id[0] = 'C';
        username[0] = 'u';
        password[0] = 'p';
        online_topic[0] = '/';
        will_topic[0] = '/';
        point_topic[0] = 't';
        online_payload[0] = 'x';
        will_payload[0] = 'y';
        sntp_primary_length = 1U;
        sntp_secondary_length = 1U;
        broker_length = 1U;
        client_id_length = 1U;
        username_length = 1U;
        password_length = 1U;
        message_topic_length = 1U;
        message_payload_length = 1U;
        point_topic_length = 1U;
    }

    memset(configuration, 0, sizeof(*configuration));
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    configuration->rtu.baud_rate = UINT32_C(115200);
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N1;
    configuration->rtu.first_byte_timeout_ms = UINT16_C(1000);
    configuration->modbus_tcp.listen_port = UINT16_C(502);
    fields_valid = set_hostname(&configuration->sntp.servers[0], sntp_primary, sntp_primary_length) &&
                   set_hostname(&configuration->sntp.servers[1], sntp_secondary, sntp_secondary_length);

    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_ENABLED;
    configuration->mqtt.broker_port = UINT16_C(8883);
    configuration->mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    configuration->mqtt.keep_alive_seconds = UINT16_C(60);
    configuration->mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    configuration->mqtt.online_message.qos = 0U;
    configuration->mqtt.online_message.retain = 0U;
    configuration->mqtt.will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    configuration->mqtt.will_message.qos = 1U;
    configuration->mqtt.will_message.retain = 1U;
    fields_valid = fields_valid &&
                   configuration_test_set_hostname(&configuration->mqtt.broker_address, broker, broker_length) &&
                   configuration_test_set_client_id(&configuration->mqtt.client_id.explicit_value, client_id,
                                                    client_id_length) &&
                   configuration_test_set_username(&configuration->mqtt.username, username, username_length) &&
                   configuration_test_set_password(&configuration->mqtt.password, password, password_length) &&
                   configuration_test_set_certificate_fixture(&configuration->mqtt.ca_certificate_pem,
                                                              "valid_root.pem") &&
                   configuration_test_set_topic(&configuration->mqtt.online_message.topic, online_topic,
                                                message_topic_length) &&
                   configuration_test_set_payload(&configuration->mqtt.online_message.payload, online_payload,
                                                  message_payload_length) &&
                   configuration_test_set_topic(&configuration->mqtt.will_message.topic, will_topic,
                                                message_topic_length) &&
                   configuration_test_set_payload(&configuration->mqtt.will_message.payload, will_payload,
                                                  message_payload_length);

    configuration->collection.point_count = 1U;
    point = &configuration->collection.points[0];
    point->slave_address = 1U;
    point->source = CONFIGURATION_COLLECTION_SOURCE_COIL;
    point->address = 0U;
    point->poll_interval_ms = UINT32_C(3600000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 0U;
    fields_valid = fields_valid &&
                   configuration_test_set_topic(&point->topic, point_topic, point_topic_length);
    return fields_valid;
}

static bool build_text_boundary_payload(uint8_t *payload, size_t capacity, size_t *payload_length,
                                        bool maximum, bool empty_online_payload)
{
    uint8_t certificate[CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH];
    uint8_t sntp_primary[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t sntp_secondary[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t broker[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t client_id[CONFIGURATION_CLIENT_ID_MAX_LENGTH];
    uint8_t username[CONFIGURATION_USERNAME_MAX_LENGTH];
    uint8_t password[CONFIGURATION_PASSWORD_MAX_LENGTH];
    uint8_t online_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t will_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t point_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t online_payload[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];
    uint8_t will_payload[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];
    uint16_t certificate_length;
    size_t sntp_primary_length;
    size_t sntp_secondary_length;
    size_t broker_length;
    size_t client_id_length;
    size_t username_length;
    size_t password_length;
    size_t message_topic_length;
    size_t message_payload_length;
    size_t point_topic_length;
    wire_builder_t builder;

    if (!load_certificate_fixture("valid_root.pem", certificate, &certificate_length))
    {
        return false;
    }

    if (maximum)
    {
        sntp_primary_length = make_maximum_hostname(sntp_primary, 'a');
        sntp_secondary_length = make_maximum_hostname(sntp_secondary, 'b');
        broker_length = make_maximum_hostname(broker, 'c');
        client_id_length = sizeof(client_id);
        username_length = sizeof(username);
        password_length = sizeof(password);
        message_topic_length = sizeof(online_topic);
        message_payload_length = sizeof(online_payload);
        point_topic_length = sizeof(point_topic);
        memset(client_id, 'C', sizeof(client_id));
        memset(username, 'u', sizeof(username));
        memset(password, 'p', sizeof(password));
        memset(online_topic, 'o', sizeof(online_topic));
        memset(will_topic, 'w', sizeof(will_topic));
        memset(point_topic, 't', sizeof(point_topic));
        memset(online_payload, 'x', sizeof(online_payload));
        memset(will_payload, 'y', sizeof(will_payload));
    }
    else
    {
        sntp_primary[0] = 'a';
        sntp_secondary[0] = 'b';
        broker[0] = 'c';
        client_id[0] = 'C';
        username[0] = 'u';
        password[0] = 'p';
        online_topic[0] = '/';
        will_topic[0] = '/';
        point_topic[0] = 't';
        online_payload[0] = 'x';
        will_payload[0] = 'y';
        sntp_primary_length = 1U;
        sntp_secondary_length = 1U;
        broker_length = 1U;
        client_id_length = 1U;
        username_length = 1U;
        password_length = 1U;
        message_topic_length = 1U;
        message_payload_length = 1U;
        point_topic_length = 1U;
    }

    wire_builder_start(&builder, payload, capacity);
    wire_builder_append_u8(&builder, CONFIGURATION_SCHEMA_VERSION);
    wire_builder_append_u8(&builder, CONFIGURATION_NETWORK_MODE_DHCP);
    wire_builder_append_u32(&builder, UINT32_C(115200));
    wire_builder_append_u8(&builder, CONFIGURATION_FRAME_FORMAT_8N1);
    wire_builder_append_u16(&builder, UINT16_C(1000));
    wire_builder_append_u16(&builder, UINT16_C(502));
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    wire_builder_append_text(&builder, sntp_primary, sntp_primary_length);
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    wire_builder_append_text(&builder, sntp_secondary, sntp_secondary_length);
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MODE_ENABLED);
    wire_builder_append_text(&builder, broker, broker_length);
    wire_builder_append_u16(&builder, UINT16_C(8883));
    wire_builder_append_u8(&builder, CONFIGURATION_CLIENT_ID_MODE_EXPLICIT);
    wire_builder_append_text(&builder, client_id, client_id_length);
    wire_builder_append_text(&builder, username, username_length);
    wire_builder_append_text(&builder, password, password_length);
    wire_builder_append_text(&builder, certificate, certificate_length);
    wire_builder_append_u16(&builder, UINT16_C(60));
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM);
    wire_builder_append_text(&builder, online_topic, message_topic_length);
    if (empty_online_payload)
    {
        wire_builder_append_text(&builder, NULL, 0U);
    }
    else
    {
        wire_builder_append_text(&builder, online_payload, message_payload_length);
    }
    wire_builder_append_u8(&builder, 0U);
    wire_builder_append_u8(&builder, 0U);
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM);
    wire_builder_append_text(&builder, will_topic, message_topic_length);
    wire_builder_append_text(&builder, will_payload, message_payload_length);
    wire_builder_append_u8(&builder, 1U);
    wire_builder_append_u8(&builder, 1U);
    wire_builder_append_u8(&builder, 1U);
    wire_builder_append_point(&builder, NULL, 0U, 1U, CONFIGURATION_COLLECTION_SOURCE_COIL, 0U, false, 0U,
                              UINT32_C(3600000), UINT16_C(1000), point_topic, point_topic_length, 0U);
    return wire_builder_finish(&builder, payload_length);
}

static bool build_bus_overuse_payload(uint8_t *payload, size_t capacity, size_t *payload_length)
{
    static const uint8_t point_topics[3][9] = {"points/0", "points/1", "points/2"};
    uint8_t certificate[CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH];
    uint16_t certificate_length;
    wire_builder_t builder;
    size_t index;
    static const uint32_t poll_intervals[] = {1000U, 1000U, 1673U};

    if (!load_certificate_fixture("valid_root.pem", certificate, &certificate_length))
    {
        return false;
    }

    wire_builder_start(&builder, payload, capacity);
    wire_builder_append_u8(&builder, CONFIGURATION_SCHEMA_VERSION);
    wire_builder_append_u8(&builder, CONFIGURATION_NETWORK_MODE_DHCP);
    wire_builder_append_u32(&builder, UINT32_C(1200));
    wire_builder_append_u8(&builder, CONFIGURATION_FRAME_FORMAT_8N2);
    wire_builder_append_u16(&builder, UINT16_C(1000));
    wire_builder_append_u16(&builder, UINT16_C(502));
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    wire_builder_append_text(&builder, default_sntp_primary, sizeof(default_sntp_primary) - 1U);
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    wire_builder_append_text(&builder, default_sntp_secondary, sizeof(default_sntp_secondary) - 1U);
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MODE_ENABLED);
    wire_builder_append_text(&builder, derived_broker, sizeof(derived_broker) - 1U);
    wire_builder_append_u16(&builder, UINT16_C(8883));
    wire_builder_append_u8(&builder, CONFIGURATION_CLIENT_ID_MODE_DERIVED);
    wire_builder_append_text(&builder, derived_username, sizeof(derived_username) - 1U);
    wire_builder_append_text(&builder, derived_password, sizeof(derived_password) - 1U);
    wire_builder_append_text(&builder, certificate, certificate_length);
    wire_builder_append_u16(&builder, UINT16_C(60));
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED);
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED);
    wire_builder_append_u8(&builder, 3U);
    for (index = 0U; index < 3U; index++)
    {
        wire_builder_append_point(&builder, NULL, index, (uint8_t)(index + 1U),
                                  CONFIGURATION_COLLECTION_SOURCE_COIL, (uint16_t)index, false, UINT8_MAX,
                                  poll_intervals[index], UINT16_C(1000), point_topics[index], 8U, 0U);
    }
    return wire_builder_finish(&builder, payload_length);
}

static bool build_maximum_length_payload(uint8_t *payload, size_t capacity, size_t *payload_length)
{
    uint8_t sntp_primary[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t sntp_secondary[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t broker[CONFIGURATION_HOSTNAME_MAX_LENGTH];
    uint8_t client_id[CONFIGURATION_CLIENT_ID_MAX_LENGTH];
    uint8_t username[CONFIGURATION_USERNAME_MAX_LENGTH];
    uint8_t password[CONFIGURATION_PASSWORD_MAX_LENGTH];
    uint8_t invalid_certificate[CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH];
    uint8_t online_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t will_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    uint8_t online_payload[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];
    uint8_t will_payload[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH];
    uint8_t point_topic[CONFIGURATION_TOPIC_MAX_LENGTH];
    wire_builder_t builder;
    size_t point_index;

    if (make_maximum_hostname(sntp_primary, 'a') != CONFIGURATION_HOSTNAME_MAX_LENGTH ||
        make_maximum_hostname(sntp_secondary, 'b') != CONFIGURATION_HOSTNAME_MAX_LENGTH ||
        make_maximum_hostname(broker, 'c') != CONFIGURATION_HOSTNAME_MAX_LENGTH)
    {
        return false;
    }
    memset(client_id, 'C', sizeof(client_id));
    memset(username, 'u', sizeof(username));
    memset(password, 'p', sizeof(password));
    memset(invalid_certificate, 'X', sizeof(invalid_certificate));
    memset(online_topic, 'o', sizeof(online_topic));
    memset(will_topic, 'w', sizeof(will_topic));
    memset(online_payload, 'x', sizeof(online_payload));
    memset(will_payload, 'y', sizeof(will_payload));

    wire_builder_start(&builder, payload, capacity);
    wire_builder_append_u8(&builder, CONFIGURATION_SCHEMA_VERSION);
    wire_builder_append_u8(&builder, CONFIGURATION_NETWORK_MODE_STATIC);
    wire_builder_append_ipv4(&builder, 192U, 168U, 10U, 10U);
    wire_builder_append_ipv4(&builder, 255U, 255U, 255U, 0U);
    wire_builder_append_ipv4(&builder, 192U, 168U, 10U, 1U);
    wire_builder_append_ipv4(&builder, 8U, 8U, 8U, 8U);
    wire_builder_append_ipv4(&builder, 1U, 1U, 1U, 1U);
    wire_builder_append_u32(&builder, UINT32_C(115200));
    wire_builder_append_u8(&builder, CONFIGURATION_FRAME_FORMAT_8N2);
    wire_builder_append_u16(&builder, UINT16_C(1000));
    wire_builder_append_u16(&builder, UINT16_C(502));
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    wire_builder_append_text(&builder, sntp_primary, sizeof(sntp_primary));
    wire_builder_append_u8(&builder, CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME);
    wire_builder_append_text(&builder, sntp_secondary, sizeof(sntp_secondary));
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MODE_ENABLED);
    wire_builder_append_text(&builder, broker, sizeof(broker));
    wire_builder_append_u16(&builder, UINT16_C(8883));
    wire_builder_append_u8(&builder, CONFIGURATION_CLIENT_ID_MODE_EXPLICIT);
    wire_builder_append_text(&builder, client_id, sizeof(client_id));
    wire_builder_append_text(&builder, username, sizeof(username));
    wire_builder_append_text(&builder, password, sizeof(password));
    wire_builder_append_text(&builder, invalid_certificate, sizeof(invalid_certificate));
    wire_builder_append_u16(&builder, UINT16_C(60));
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM);
    wire_builder_append_text(&builder, online_topic, sizeof(online_topic));
    wire_builder_append_text(&builder, online_payload, sizeof(online_payload));
    wire_builder_append_u8(&builder, 0U);
    wire_builder_append_u8(&builder, 0U);
    wire_builder_append_u8(&builder, CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM);
    wire_builder_append_text(&builder, will_topic, sizeof(will_topic));
    wire_builder_append_text(&builder, will_payload, sizeof(will_payload));
    wire_builder_append_u8(&builder, 0U);
    wire_builder_append_u8(&builder, 0U);
    wire_builder_append_u8(&builder, CONFIGURATION_COLLECTION_POINT_MAX_COUNT);
    for (point_index = 0U; point_index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; point_index++)
    {
        make_indexed_topic(point_topic, sizeof(point_topic), (uint8_t)point_index);
        wire_builder_append_point(&builder, NULL, point_index, (uint8_t)(point_index + 1U),
                                  CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER, (uint16_t)point_index, true,
                                  CONFIGURATION_DATA_TYPE_UINT16, UINT32_C(3600000), UINT16_C(1000), point_topic,
                                  sizeof(point_topic), 0U);
    }
    return wire_builder_finish(&builder, payload_length);
}

void setUp(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(configuration_test_use_standard_allocator());
}

static void test_decode_accepts_minimum_length_golden_vector(void)
{
    configuration_t expected;
    guarded_configuration_t guarded;

    TEST_ASSERT_EQUAL_UINT32(21U, sizeof(minimal_v1));
    TEST_ASSERT_TRUE(make_minimal_configuration(&expected));
    assert_decode_call(minimal_v1, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_OK, &guarded,
                       "minimum golden decode");
    assert_configuration_equal(&expected, &guarded.configuration);
}

static void test_decode_accepts_default_configuration_golden_vector(void)
{
    configuration_t expected;
    guarded_configuration_t guarded;

    TEST_ASSERT_EQUAL_UINT32(CONFIGURATION_V1_DEFAULT_PAYLOAD_LENGTH, sizeof(default_v1));
    TEST_ASSERT_TRUE(make_default_configuration(&expected));
    assert_decode_call(default_v1, sizeof(default_v1), CONFIGURATION_BINARY_CODEC_OK, &guarded,
                       "default golden decode");
    assert_configuration_equal(&expected, &guarded.configuration);
}

static void test_decode_accepts_full_conditional_branches_golden_vector(void)
{
    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    wire_offsets_t offsets;
    configuration_t expected;
    guarded_configuration_t guarded;

    TEST_ASSERT_TRUE(build_full_payload(payload, sizeof(payload), &payload_length, &offsets));
    TEST_ASSERT_TRUE(payload_length <= UINT32_MAX);
    TEST_ASSERT_TRUE(make_full_configuration(&expected));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&expected));
    assert_decode_call(payload, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_OK, &guarded,
                       "full conditional golden decode");
    assert_configuration_equal(&expected, &guarded.configuration);
}

static void test_decode_accepts_derived_and_disabled_branches_golden_vector(void)
{
    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    wire_offsets_t offsets;
    configuration_t expected;
    guarded_configuration_t guarded;

    TEST_ASSERT_TRUE(build_derived_payload(payload, sizeof(payload), &payload_length, &offsets));
    TEST_ASSERT_TRUE(payload_length <= UINT32_MAX);
    TEST_ASSERT_TRUE(make_derived_configuration(&expected));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&expected));
    assert_decode_call(payload, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_OK, &guarded,
                       "derived and disabled golden decode");
    assert_configuration_equal(&expected, &guarded.configuration);
}

static void test_decode_accepts_effective_text_length_boundaries(void)
{
    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    configuration_t expected;
    guarded_configuration_t guarded;
    size_t case_index;
    static const bool maximum_cases[] = {false, true};
    static const char *case_names[] = {"minimum effective text lengths", "maximum effective text lengths"};

    for (case_index = 0U; case_index < sizeof(maximum_cases) / sizeof(maximum_cases[0]); case_index++)
    {
        TEST_ASSERT_TRUE_MESSAGE(build_text_boundary_payload(payload, sizeof(payload), &payload_length,
                                                             maximum_cases[case_index], false),
                                 case_names[case_index]);
        TEST_ASSERT_TRUE_MESSAGE(make_text_boundary_configuration(&expected, maximum_cases[case_index]),
                                 case_names[case_index]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CONFIGURATION_VALIDATION_OK, configuration_validate(&expected),
                                      case_names[case_index]);
        assert_decode_call(payload, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_OK, &guarded,
                           case_names[case_index]);
        assert_configuration_equal(&expected, &guarded.configuration);
    }
}

static void test_decode_rejects_each_null_argument(void)
{
    guarded_configuration_t guarded;
    configuration_binary_codec_result_t result;

    prepare_guarded_configuration(&guarded);
    result = configuration_binary_decode(NULL, sizeof(minimal_v1), &guarded.configuration);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT, result);
    assert_bytes_are(TEST_CANARY, guarded.before, sizeof(guarded.before), "NULL payload leading canary");
    assert_bytes_are(TEST_CANARY, guarded.after, sizeof(guarded.after), "NULL payload trailing canary");

    memcpy(payload_snapshot, minimal_v1, sizeof(minimal_v1));
    result = configuration_binary_decode(minimal_v1, sizeof(minimal_v1), NULL);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT, result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_snapshot, minimal_v1, sizeof(minimal_v1));
}

static void test_decode_enforces_payload_length_boundaries(void)
{
    uint8_t maximum_payload[CONFIGURATION_V1_MAX_PAYLOAD_LENGTH];
    uint8_t oversized_payload[TEST_MAX_BUFFER_SIZE];
    size_t maximum_length;
    guarded_configuration_t guarded;

    assert_decode_call(minimal_v1, 0U, CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID, &guarded,
                       "zero payload length");
    assert_decode_call(minimal_v1, 1U, CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                       "one-byte recognized schema");
    assert_decode_call(minimal_v1, 20U, CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                       "one byte below minimum complete length");
    assert_decode_call(minimal_v1, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_OK, &guarded,
                       "minimum complete length");

    TEST_ASSERT_TRUE(build_maximum_length_payload(maximum_payload, sizeof(maximum_payload), &maximum_length));
    TEST_ASSERT_EQUAL_UINT32(CONFIGURATION_V1_MAX_PAYLOAD_LENGTH, maximum_length);
    assert_decode_call(maximum_payload, (uint32_t)maximum_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID,
                       &guarded, "maximum accepted length with one invalid model field");

    memcpy(oversized_payload, maximum_payload, maximum_length);
    oversized_payload[maximum_length] = 0U;
    assert_decode_call(oversized_payload, (uint32_t)(maximum_length + 1U),
                       CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID, &guarded,
                       "one byte above maximum payload length");
}

static void test_decode_rejects_unsupported_schema(void)
{
    uint8_t payload[sizeof(minimal_v1)];
    guarded_configuration_t guarded;
    size_t case_index;
    static const uint8_t unsupported_versions[] = {0x00U, 0x02U, 0xFFU};

    for (case_index = 0U; case_index < sizeof(unsupported_versions); case_index++)
    {
        memcpy(payload, minimal_v1, sizeof(payload));
        payload[0] = unsupported_versions[case_index];
        assert_decode_call(payload, sizeof(payload), CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED, &guarded,
                           "unsupported schema version");
    }
}

static void test_decode_rejects_every_truncated_golden_prefix(void)
{
    typedef struct
    {
        const uint8_t *payload;
        size_t length;
        const char *name;
    } golden_case_t;

    uint8_t full_payload[TEST_MAX_BUFFER_SIZE];
    uint8_t derived_payload[TEST_MAX_BUFFER_SIZE];
    size_t full_length;
    size_t derived_length;
    wire_offsets_t offsets;
    golden_case_t cases[4];
    guarded_configuration_t guarded;
    size_t case_index;
    size_t prefix_length;
    char case_name[96];
    int name_length;

    TEST_ASSERT_TRUE(build_full_payload(full_payload, sizeof(full_payload), &full_length, &offsets));
    TEST_ASSERT_TRUE(build_derived_payload(derived_payload, sizeof(derived_payload), &derived_length, &offsets));
    cases[0].payload = minimal_v1;
    cases[0].length = sizeof(minimal_v1);
    cases[0].name = "minimal";
    cases[1].payload = default_v1;
    cases[1].length = sizeof(default_v1);
    cases[1].name = "default";
    cases[2].payload = full_payload;
    cases[2].length = full_length;
    cases[2].name = "full";
    cases[3].payload = derived_payload;
    cases[3].length = derived_length;
    cases[3].name = "derived";

    for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++)
    {
        for (prefix_length = 1U; prefix_length < cases[case_index].length; prefix_length++)
        {
            name_length = snprintf(case_name, sizeof(case_name), "%s prefix length %u", cases[case_index].name,
                                   (unsigned int)prefix_length);
            TEST_ASSERT_TRUE(name_length > 0 && (size_t)name_length < sizeof(case_name));
            assert_decode_call(cases[case_index].payload, (uint32_t)prefix_length,
                               CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded, case_name);
        }
    }
}

static void test_decode_rejects_illegal_layout_discriminants(void)
{
    typedef struct
    {
        size_t offset;
        const char *name;
    } discriminant_case_t;

    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    uint8_t mutated[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    wire_offsets_t offsets;
    discriminant_case_t cases[9];
    guarded_configuration_t guarded;
    size_t case_index;

    TEST_ASSERT_TRUE(build_full_payload(payload, sizeof(payload), &payload_length, &offsets));
    cases[0] = (discriminant_case_t){offsets.network_mode, "network mode"};
    cases[1] = (discriminant_case_t){offsets.endpoint_type[0], "first endpoint type"};
    cases[2] = (discriminant_case_t){offsets.endpoint_type[1], "second endpoint type"};
    cases[3] = (discriminant_case_t){offsets.mqtt_mode, "MQTT mode"};
    cases[4] = (discriminant_case_t){offsets.client_id_mode, "client ID mode"};
    cases[5] = (discriminant_case_t){offsets.online_mode, "online message mode"};
    cases[6] = (discriminant_case_t){offsets.will_mode, "will message mode"};
    cases[7] = (discriminant_case_t){offsets.point_source[0], "Coil point source"};
    cases[8] = (discriminant_case_t){offsets.point_source[2], "register point source"};

    for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++)
    {
        TEST_ASSERT_TRUE_MESSAGE(cases[case_index].offset != TEST_OFFSET_UNSET, cases[case_index].name);
        memcpy(mutated, payload, payload_length);
        mutated[cases[case_index].offset] = UINT8_MAX;
        assert_decode_call(mutated, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                           cases[case_index].name);
    }
}

static void test_decode_rejects_out_of_range_text_lengths(void)
{
    typedef struct
    {
        size_t offset;
        uint16_t maximum;
        bool minimum_is_one;
        const char *name;
    } text_length_case_t;

    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    uint8_t mutated[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    wire_offsets_t offsets;
    text_length_case_t cases[9];
    guarded_configuration_t guarded;
    size_t case_index;

    TEST_ASSERT_TRUE(build_full_payload(payload, sizeof(payload), &payload_length, &offsets));
    cases[0] = (text_length_case_t){offsets.endpoint_hostname_length[0], CONFIGURATION_HOSTNAME_MAX_LENGTH,
                                    true, "endpoint hostname length"};
    cases[1] = (text_length_case_t){offsets.broker_length, CONFIGURATION_HOSTNAME_MAX_LENGTH, true,
                                    "broker length"};
    cases[2] = (text_length_case_t){offsets.client_id_length, CONFIGURATION_CLIENT_ID_MAX_LENGTH, true,
                                    "client ID length"};
    cases[3] = (text_length_case_t){offsets.username_length, CONFIGURATION_USERNAME_MAX_LENGTH, true,
                                    "username length"};
    cases[4] = (text_length_case_t){offsets.password_length, CONFIGURATION_PASSWORD_MAX_LENGTH, true,
                                    "password length"};
    cases[5] = (text_length_case_t){offsets.certificate_length, CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH, true,
                                    "certificate length"};
    cases[6] = (text_length_case_t){offsets.online_topic_length, CONFIGURATION_TOPIC_MAX_LENGTH, true,
                                    "message topic length"};
    cases[7] = (text_length_case_t){offsets.online_payload_length,
                                    CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH, false, "message payload length"};
    cases[8] = (text_length_case_t){offsets.point_topic_length[0], CONFIGURATION_TOPIC_MAX_LENGTH, true,
                                    "point topic length"};

    for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++)
    {
        TEST_ASSERT_TRUE_MESSAGE(cases[case_index].offset != TEST_OFFSET_UNSET, cases[case_index].name);
        if (cases[case_index].minimum_is_one)
        {
            memcpy(mutated, payload, payload_length);
            wire_write_u16_at(mutated, cases[case_index].offset, 0U);
            assert_decode_call(mutated, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                               cases[case_index].name);
        }

        memcpy(mutated, payload, payload_length);
        wire_write_u16_at(mutated, cases[case_index].offset, (uint16_t)(cases[case_index].maximum + 1U));
        assert_decode_call(mutated, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                           cases[case_index].name);
    }
}

static void test_decode_rejects_point_count_above_schema_limit(void)
{
    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    wire_offsets_t offsets;
    guarded_configuration_t guarded;

    TEST_ASSERT_TRUE(build_derived_payload(payload, sizeof(payload), &payload_length, &offsets));
    payload[offsets.point_count] = (uint8_t)(CONFIGURATION_COLLECTION_POINT_MAX_COUNT + 1U);
    assert_decode_call(payload, (uint32_t)payload_length, CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                       "point count above schema limit");
}

static void test_decode_rejects_trailing_bytes(void)
{
    uint8_t payload[sizeof(minimal_v1) + 1U];
    guarded_configuration_t guarded;

    memcpy(payload, minimal_v1, sizeof(minimal_v1));
    payload[sizeof(minimal_v1)] = 0U;
    assert_decode_call(payload, sizeof(payload), CONFIGURATION_BINARY_CODEC_MALFORMED, &guarded,
                       "one trailing byte");
}

static void test_decode_maps_non_layout_values_to_model_invalid(void)
{
    uint8_t full_payload[TEST_MAX_BUFFER_SIZE];
    uint8_t text_payload[TEST_MAX_BUFFER_SIZE];
    uint8_t mutated[TEST_MAX_BUFFER_SIZE];
    size_t full_length;
    size_t text_length;
    wire_offsets_t offsets;
    guarded_configuration_t guarded;

    memcpy(mutated, minimal_v1, sizeof(minimal_v1));
    memset(&mutated[MINIMAL_RTU_BAUD_OFFSET], 0, sizeof(uint32_t));
    assert_decode_call(mutated, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "unsupported fixed-width baud rate");

    memcpy(mutated, minimal_v1, sizeof(minimal_v1));
    mutated[MINIMAL_RTU_FRAME_OFFSET] = UINT8_MAX;
    assert_decode_call(mutated, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "unsupported fixed-width frame format");

    TEST_ASSERT_TRUE(build_full_payload(full_payload, sizeof(full_payload), &full_length, &offsets));
    memcpy(mutated, full_payload, full_length);
    mutated[offsets.online_qos] = UINT8_MAX;
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "unsupported message QoS");

    memcpy(mutated, full_payload, full_length);
    mutated[offsets.online_retain] = UINT8_MAX;
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "unsupported message retain");

    memcpy(mutated, full_payload, full_length);
    mutated[offsets.point_data_type[2]] = UINT8_MAX;
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "unsupported register data type");

    memcpy(mutated, full_payload, full_length);
    mutated[offsets.point_qos[0]] = UINT8_MAX;
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "unsupported point QoS");

    TEST_ASSERT_TRUE(build_text_boundary_payload(text_payload, sizeof(text_payload), &text_length, false, true));
    assert_decode_call(text_payload, (uint32_t)text_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "wire-valid empty custom message payload");
}

static void test_decode_maps_each_model_validation_category_to_model_invalid(void)
{
    uint8_t full_payload[TEST_MAX_BUFFER_SIZE];
    uint8_t bus_payload[TEST_MAX_BUFFER_SIZE];
    uint8_t mutated[TEST_MAX_BUFFER_SIZE];
    size_t full_length;
    size_t bus_length;
    wire_offsets_t offsets;
    guarded_configuration_t guarded;

    TEST_ASSERT_TRUE(build_full_payload(full_payload, sizeof(full_payload), &full_length, &offsets));

    memcpy(mutated, full_payload, full_length);
    memcpy(&mutated[offsets.network_gateway], &mutated[offsets.network_ip_address], 4U);
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "Network validation result");

    memcpy(mutated, minimal_v1, sizeof(minimal_v1));
    wire_write_u16_at(mutated, MINIMAL_RTU_TIMEOUT_OFFSET, 0U);
    assert_decode_call(mutated, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "RTU validation result");

    memcpy(mutated, minimal_v1, sizeof(minimal_v1));
    wire_write_u16_at(mutated, MINIMAL_TCP_PORT_OFFSET, 0U);
    assert_decode_call(mutated, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "Modbus TCP validation result");

    memcpy(mutated, minimal_v1, sizeof(minimal_v1));
    mutated[MINIMAL_SECOND_HOSTNAME_BYTE_OFFSET] = 'a';
    assert_decode_call(mutated, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "SNTP validation result");

    memcpy(mutated, full_payload, full_length);
    wire_write_u16_at(mutated, offsets.broker_port, 0U);
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "MQTT validation result");

    memcpy(mutated, full_payload, full_length);
    mutated[offsets.certificate_bytes] = 'X';
    assert_decode_call(mutated, (uint32_t)full_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "Certificate validation result");

    memcpy(mutated, full_payload, full_length);
    mutated[offsets.point_count] = 0U;
    assert_decode_call(mutated, (uint32_t)(offsets.point_count + 1U), CONFIGURATION_BINARY_CODEC_MODEL_INVALID,
                       &guarded, "Collection validation result");

    TEST_ASSERT_TRUE(build_bus_overuse_payload(bus_payload, sizeof(bus_payload), &bus_length));
    assert_decode_call(bus_payload, (uint32_t)bus_length, CONFIGURATION_BINARY_CODEC_MODEL_INVALID, &guarded,
                       "Bus utilization validation result");
}

static void test_decode_maps_allocator_failure_to_resource_unavailable(void)
{
    uint8_t payload[TEST_MAX_BUFFER_SIZE];
    size_t payload_length;
    wire_offsets_t offsets;
    guarded_configuration_t guarded;
    configuration_binary_codec_result_t result;
    bool allocator_restored;

    TEST_ASSERT_TRUE(build_derived_payload(payload, sizeof(payload), &payload_length, &offsets));
    prepare_guarded_configuration(&guarded);
    memcpy(payload_snapshot, payload, payload_length);
    TEST_ASSERT_TRUE(configuration_test_use_failing_allocator());

    result = configuration_binary_decode(payload, (uint32_t)payload_length, &guarded.configuration);
    allocator_restored = configuration_test_use_standard_allocator();

    TEST_ASSERT_TRUE(allocator_restored);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE, result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_snapshot, payload, payload_length);
    assert_bytes_are(TEST_CANARY, guarded.before, sizeof(guarded.before), "allocator failure leading canary");
    assert_bytes_are(TEST_CANARY, guarded.after, sizeof(guarded.after), "allocator failure trailing canary");
}

static void test_decode_result_is_independent_of_output_contents(void)
{
    guarded_configuration_t first;
    guarded_configuration_t second;
    configuration_t expected;
    configuration_binary_codec_result_t first_result;
    configuration_binary_codec_result_t second_result;

    memset(&first, 0x00, sizeof(first));
    memset(&second, 0xFF, sizeof(second));
    memset(first.before, TEST_CANARY, sizeof(first.before));
    memset(first.after, TEST_CANARY, sizeof(first.after));
    memset(second.before, TEST_CANARY, sizeof(second.before));
    memset(second.after, TEST_CANARY, sizeof(second.after));
    memcpy(payload_snapshot, default_v1, sizeof(default_v1));

    first_result = configuration_binary_decode(default_v1, sizeof(default_v1), &first.configuration);
    second_result = configuration_binary_decode(default_v1, sizeof(default_v1), &second.configuration);

    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK, first_result);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK, second_result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_snapshot, default_v1, sizeof(default_v1));
    assert_bytes_are(TEST_CANARY, first.before, sizeof(first.before), "zero-filled output leading canary");
    assert_bytes_are(TEST_CANARY, first.after, sizeof(first.after), "zero-filled output trailing canary");
    assert_bytes_are(TEST_CANARY, second.before, sizeof(second.before), "FF-filled output leading canary");
    assert_bytes_are(TEST_CANARY, second.after, sizeof(second.after), "FF-filled output trailing canary");
    TEST_ASSERT_TRUE(make_default_configuration(&expected));
    assert_configuration_equal(&expected, &first.configuration);
    assert_configuration_equal(&expected, &second.configuration);
}

static void test_encode_matches_minimum_length_golden_vector(void)
{
    configuration_t configuration;
    uint32_t encoded_length = 0U;

    TEST_ASSERT_TRUE(make_minimal_configuration(&configuration));
    assert_encode_call(&configuration, sizeof(minimal_v1), CONFIGURATION_BINARY_CODEC_OK, &encoded_length,
                       "minimum golden encode");
    TEST_ASSERT_EQUAL_UINT32(sizeof(minimal_v1), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(minimal_v1, guarded_payload.payload, sizeof(minimal_v1));
}

static void test_encode_matches_default_configuration_golden_vector(void)
{
    configuration_t configuration;
    uint32_t encoded_length = 0U;

    TEST_ASSERT_TRUE(make_default_configuration(&configuration));
    assert_encode_call(&configuration, sizeof(default_v1), CONFIGURATION_BINARY_CODEC_OK, &encoded_length,
                       "default golden encode");
    TEST_ASSERT_EQUAL_UINT32(sizeof(default_v1), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(default_v1, guarded_payload.payload, sizeof(default_v1));
}

static void test_encode_matches_full_conditional_branches_golden_vector(void)
{
    uint8_t expected_payload[TEST_MAX_BUFFER_SIZE];
    size_t expected_length;
    wire_offsets_t offsets;
    configuration_t configuration;
    uint32_t encoded_length = 0U;

    TEST_ASSERT_TRUE(build_full_payload(expected_payload, sizeof(expected_payload), &expected_length, &offsets));
    TEST_ASSERT_TRUE(make_full_configuration(&configuration));
    assert_encode_call(&configuration, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK, &encoded_length,
                       "full conditional golden encode");
    TEST_ASSERT_EQUAL_UINT32(expected_length, encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload, guarded_payload.payload, expected_length);
}

static void test_encode_matches_derived_and_disabled_branches_golden_vector(void)
{
    uint8_t expected_payload[TEST_MAX_BUFFER_SIZE];
    size_t expected_length;
    wire_offsets_t offsets;
    configuration_t configuration;
    uint32_t encoded_length = 0U;

    TEST_ASSERT_TRUE(build_derived_payload(expected_payload, sizeof(expected_payload), &expected_length, &offsets));
    TEST_ASSERT_TRUE(make_derived_configuration(&configuration));
    assert_encode_call(&configuration, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK, &encoded_length,
                       "derived and disabled golden encode");
    TEST_ASSERT_EQUAL_UINT32(expected_length, encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload, guarded_payload.payload, expected_length);
}

static void test_encode_matches_effective_text_length_boundaries(void)
{
    uint8_t expected_payload[TEST_MAX_BUFFER_SIZE];
    size_t expected_length;
    configuration_t configuration;
    uint32_t encoded_length = 0U;
    size_t case_index;
    static const bool maximum_cases[] = {false, true};
    static const char *case_names[] = {"minimum effective text lengths", "maximum effective text lengths"};

    for (case_index = 0U; case_index < sizeof(maximum_cases) / sizeof(maximum_cases[0]); case_index++)
    {
        TEST_ASSERT_TRUE_MESSAGE(build_text_boundary_payload(expected_payload, sizeof(expected_payload),
                                                             &expected_length, maximum_cases[case_index], false),
                                 case_names[case_index]);
        TEST_ASSERT_TRUE_MESSAGE(make_text_boundary_configuration(&configuration, maximum_cases[case_index]),
                                 case_names[case_index]);
        assert_encode_call(&configuration, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK,
                           &encoded_length, case_names[case_index]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected_length, encoded_length, case_names[case_index]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_payload, guarded_payload.payload, expected_length,
                                              case_names[case_index]);
    }
}

static void assert_payload_outside_capacity_is_unchanged(uint32_t payload_capacity, const char *case_name)
{
    assert_bytes_are(TEST_CANARY, guarded_payload.before, sizeof(guarded_payload.before), case_name);
    assert_bytes_are(TEST_CANARY, &guarded_payload.payload[payload_capacity],
                     sizeof(guarded_payload.payload) - payload_capacity, case_name);
    assert_bytes_are(TEST_CANARY, guarded_payload.after, sizeof(guarded_payload.after), case_name);
}

static void test_encode_rejects_each_null_argument(void)
{
    configuration_t configuration;
    configuration_t before;
    configuration_binary_codec_result_t result;
    uint32_t payload_length = UINT32_C(0xDEADBEEF);

    TEST_ASSERT_TRUE(make_default_configuration(&configuration));
    memcpy(&before, &configuration, sizeof(before));

    prepare_guarded_payload(sizeof(default_v1));
    result = configuration_binary_encode(NULL, guarded_payload.payload, sizeof(default_v1), &payload_length);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT, result);
    assert_payload_outside_capacity_is_unchanged(sizeof(default_v1), "NULL configuration canary");

    result = configuration_binary_encode(&configuration, NULL, sizeof(default_v1), &payload_length);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT, result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&before, (const uint8_t *)&configuration, sizeof(before));

    prepare_guarded_payload(sizeof(default_v1));
    result = configuration_binary_encode(&configuration, guarded_payload.payload, sizeof(default_v1), NULL);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT, result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&before, (const uint8_t *)&configuration, sizeof(before));
    assert_payload_outside_capacity_is_unchanged(sizeof(default_v1), "NULL payload length canary");
}

static void test_encode_enforces_payload_capacity_boundaries(void)
{
    configuration_t configuration;
    uint32_t encoded_length = 0U;

    TEST_ASSERT_TRUE(make_default_configuration(&configuration));
    assert_encode_call(&configuration, 0U, CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL, &encoded_length,
                       "zero capacity with non-NULL payload");
    assert_encode_call(&configuration, sizeof(default_v1) - 1U, CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL,
                       &encoded_length, "one byte below required capacity");
    assert_encode_call(&configuration, sizeof(default_v1), CONFIGURATION_BINARY_CODEC_OK, &encoded_length,
                       "capacity exactly equals required length");
    TEST_ASSERT_EQUAL_UINT32(sizeof(default_v1), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(default_v1, guarded_payload.payload, sizeof(default_v1));

    assert_encode_call(&configuration, sizeof(default_v1) + 1U, CONFIGURATION_BINARY_CODEC_OK, &encoded_length,
                       "capacity one byte above required length");
    TEST_ASSERT_EQUAL_UINT32(sizeof(default_v1), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(default_v1, guarded_payload.payload, sizeof(default_v1));

    assert_encode_call(&configuration, CONFIGURATION_V1_MAX_PAYLOAD_LENGTH + 1U,
                       CONFIGURATION_BINARY_CODEC_OK, &encoded_length, "capacity above schema maximum");
    TEST_ASSERT_EQUAL_UINT32(sizeof(default_v1), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(default_v1, guarded_payload.payload, sizeof(default_v1));
}

static void test_encode_ignores_inactive_fields_and_is_deterministic(void)
{
    uint8_t expected_payload[TEST_MAX_BUFFER_SIZE];
    uint8_t first_encoding[TEST_MAX_BUFFER_SIZE];
    size_t expected_length;
    wire_offsets_t offsets;
    configuration_t baseline;
    configuration_t variant;
    uint32_t first_length = 0U;
    uint32_t second_length = 0U;
    size_t index;

    TEST_ASSERT_TRUE(make_default_configuration(&baseline));
    memcpy(&variant, &baseline, sizeof(variant));
    configuration_test_set_ipv4(&variant.network.ip_address, 192U, 0U, 2U, 1U);
    configuration_test_set_ipv4(&variant.network.subnet_mask, 255U, 255U, 0U, 0U);
    configuration_test_set_ipv4(&variant.network.gateway, 203U, 0U, 113U, 1U);
    configuration_test_set_ipv4(&variant.network.dns_primary, 8U, 8U, 4U, 4U);
    configuration_test_set_ipv4(&variant.network.dns_secondary, 1U, 0U, 0U, 1U);
    memset(&variant.mqtt, TEST_OUTPUT_FILL, sizeof(variant.mqtt));
    variant.mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    memset(variant.collection.points, TEST_OUTPUT_FILL, sizeof(variant.collection.points));
    variant.collection.point_count = 0U;

    assert_encode_call(&baseline, sizeof(default_v1), CONFIGURATION_BINARY_CODEC_OK, &first_length,
                       "default inactive baseline");
    memcpy(first_encoding, guarded_payload.payload, first_length);
    assert_encode_call(&variant, sizeof(default_v1), CONFIGURATION_BINARY_CODEC_OK, &second_length,
                       "default inactive variant");
    TEST_ASSERT_EQUAL_UINT32(first_length, second_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first_encoding, guarded_payload.payload, first_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(default_v1, guarded_payload.payload, sizeof(default_v1));

    TEST_ASSERT_TRUE(build_full_payload(expected_payload, sizeof(expected_payload), &expected_length, &offsets));
    TEST_ASSERT_TRUE(make_full_configuration(&baseline));
    memcpy(&variant, &baseline, sizeof(variant));
    memset(&((uint8_t *)&variant.sntp.servers[1].value)[sizeof(ip4_addr_t)], TEST_OUTPUT_FILL,
           sizeof(variant.sntp.servers[1].value) - sizeof(ip4_addr_t));
    assert_encode_call(&baseline, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK, &first_length,
                       "IPv4 endpoint inactive union baseline");
    memcpy(first_encoding, guarded_payload.payload, first_length);
    assert_encode_call(&variant, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK, &second_length,
                       "IPv4 endpoint inactive union variant");
    TEST_ASSERT_EQUAL_UINT32(first_length, second_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first_encoding, guarded_payload.payload, first_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload, guarded_payload.payload, expected_length);

    TEST_ASSERT_TRUE(build_derived_payload(expected_payload, sizeof(expected_payload), &expected_length, &offsets));
    TEST_ASSERT_TRUE(make_derived_configuration(&baseline));
    memcpy(&variant, &baseline, sizeof(variant));
    configuration_test_set_ipv4(&variant.network.ip_address, 198U, 51U, 100U, 7U);
    memset(&variant.mqtt.client_id.explicit_value, TEST_OUTPUT_FILL,
           sizeof(variant.mqtt.client_id.explicit_value));
    memset(&variant.mqtt.online_message, TEST_OUTPUT_FILL, sizeof(variant.mqtt.online_message));
    variant.mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    memset(&variant.mqtt.will_message, TEST_OUTPUT_FILL, sizeof(variant.mqtt.will_message));
    variant.mqtt.will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    variant.collection.points[0].data_type = UINT8_MAX;
    for (index = 1U; index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; index++)
    {
        memset(&variant.collection.points[index], TEST_OUTPUT_FILL, sizeof(variant.collection.points[index]));
    }

    assert_encode_call(&baseline, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK,
                       &first_length, "derived inactive baseline");
    memcpy(first_encoding, guarded_payload.payload, first_length);
    assert_encode_call(&variant, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK,
                       &second_length, "derived inactive variant");
    TEST_ASSERT_EQUAL_UINT32(first_length, second_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first_encoding, guarded_payload.payload, first_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload, guarded_payload.payload, expected_length);

    assert_encode_call(&baseline, (uint32_t)expected_length, CONFIGURATION_BINARY_CODEC_OK,
                       &second_length, "repeated deterministic encode");
    TEST_ASSERT_EQUAL_UINT32(first_length, second_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first_encoding, guarded_payload.payload, first_length);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_accepts_minimum_length_golden_vector);
    RUN_TEST(test_decode_accepts_default_configuration_golden_vector);
    RUN_TEST(test_decode_accepts_full_conditional_branches_golden_vector);
    RUN_TEST(test_decode_accepts_derived_and_disabled_branches_golden_vector);
    RUN_TEST(test_decode_accepts_effective_text_length_boundaries);
    RUN_TEST(test_decode_rejects_each_null_argument);
    RUN_TEST(test_decode_enforces_payload_length_boundaries);
    RUN_TEST(test_decode_rejects_unsupported_schema);
    RUN_TEST(test_decode_rejects_every_truncated_golden_prefix);
    RUN_TEST(test_decode_rejects_illegal_layout_discriminants);
    RUN_TEST(test_decode_rejects_out_of_range_text_lengths);
    RUN_TEST(test_decode_rejects_point_count_above_schema_limit);
    RUN_TEST(test_decode_rejects_trailing_bytes);
    RUN_TEST(test_decode_maps_non_layout_values_to_model_invalid);
    RUN_TEST(test_decode_maps_each_model_validation_category_to_model_invalid);
    RUN_TEST(test_decode_maps_allocator_failure_to_resource_unavailable);
    RUN_TEST(test_decode_result_is_independent_of_output_contents);
    RUN_TEST(test_encode_matches_minimum_length_golden_vector);
    RUN_TEST(test_encode_matches_default_configuration_golden_vector);
    RUN_TEST(test_encode_matches_full_conditional_branches_golden_vector);
    RUN_TEST(test_encode_matches_derived_and_disabled_branches_golden_vector);
    RUN_TEST(test_encode_matches_effective_text_length_boundaries);
    RUN_TEST(test_encode_rejects_each_null_argument);
    RUN_TEST(test_encode_enforces_payload_capacity_boundaries);
    RUN_TEST(test_encode_ignores_inactive_fields_and_is_deterministic);
    return UNITY_END();
}
