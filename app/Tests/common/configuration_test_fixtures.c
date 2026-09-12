#include "configuration_test_fixtures.h"

#include <string.h>

const uint8_t configuration_test_default_payload[48] =
{
#include "default_v2.inc"
};

const uint8_t configuration_test_maximum_payload[8475] =
{
#include "maximum_v2.inc"
};

const uint8_t configuration_test_root_ca[4097] =
{
#include "root_ca.inc"
    0U
};

static void fill_text(uint8_t *bytes, uint16_t *length, uint16_t size, uint8_t value)
{
    memset(bytes, value, size);
    bytes[size] = 0U;
    *length = size;
}

static void fill_hostname(configuration_hostname_t *hostname, uint8_t initial)
{
    fill_text(hostname->bytes, &hostname->length, 253U, (uint8_t)'a');
    hostname->bytes[0] = initial;
    hostname->bytes[63] = (uint8_t)'.';
    hostname->bytes[127] = (uint8_t)'.';
    hostname->bytes[191] = (uint8_t)'.';
}

static void fill_message(configuration_mqtt_message_t *message, uint8_t topic_initial, uint8_t payload_value)
{
    message->mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    fill_text(message->topic.bytes, &message->topic.length, 128U, (uint8_t)'t');
    message->topic.bytes[0] = topic_initial;
    fill_text(message->payload.bytes, &message->payload.length, 128U, payload_value);
    message->qos = 2U;
    message->retain = 1U;
}

void configuration_test_make_maximum(configuration_t *configuration)
{
    configuration_set_defaults(configuration);
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_STATIC;
    IP4_ADDR(&configuration->network.ip_address, 192, 168, 1, 10);
    IP4_ADDR(&configuration->network.subnet_mask, 255, 255, 255, 0);
    IP4_ADDR(&configuration->network.gateway, 192, 168, 1, 1);
    IP4_ADDR(&configuration->network.dns_primary, 1, 1, 1, 1);
    IP4_ADDR(&configuration->network.dns_secondary, 8, 8, 8, 8);
    configuration->rtu.baud_rate = 115200U;
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N1;
    fill_hostname(&configuration->sntp.servers[0].value.hostname, (uint8_t)'s');
    fill_hostname(&configuration->sntp.servers[1].value.hostname, (uint8_t)'t');
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_ENABLED;
    fill_hostname(&configuration->mqtt.broker_address, (uint8_t)'m');
    configuration->mqtt.broker_port = 8883U;
    configuration->mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    fill_text(configuration->mqtt.client_id.explicit_value.bytes,
              &configuration->mqtt.client_id.explicit_value.length, 256U, (uint8_t)'C');
    configuration->mqtt.client_id.explicit_value.bytes[254] = (uint8_t)'_';
    configuration->mqtt.client_id.explicit_value.bytes[255] = (uint8_t)'-';
    fill_text(configuration->mqtt.username.bytes, &configuration->mqtt.username.length, 256U, (uint8_t)'U');
    fill_text(configuration->mqtt.password.bytes, &configuration->mqtt.password.length, 256U, (uint8_t)'P');
    configuration->mqtt.username.bytes[0] = (uint8_t)' ';
    configuration->mqtt.password.bytes[255] = (uint8_t)'~';
    configuration->mqtt.ca_certificate_pem.length = 4096U;
    memcpy(configuration->mqtt.ca_certificate_pem.bytes, configuration_test_root_ca, 4097U);
    configuration->mqtt.keep_alive_seconds = 60U;
    fill_message(&configuration->mqtt.online_message, (uint8_t)'O', (uint8_t)'o');
    fill_message(&configuration->mqtt.will_message, (uint8_t)'W', (uint8_t)'w');
    configuration->collection.point_count = 16U;
    for (uint8_t index = 0U; index < 16U; index++)
    {
        configuration_collection_point_t *point = &configuration->collection.points[index];
        point->slave_address = (uint8_t)(index + 1U);
        point->source = CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER;
        point->address = index;
        point->data_type = CONFIGURATION_DATA_TYPE_UINT16;
        point->poll_interval_ms = 1000U;
        point->first_byte_timeout_ms = 50U;
        fill_text(point->topic.bytes, &point->topic.length, 128U, (uint8_t)'t');
        point->topic.bytes[0] = (uint8_t)('a' + index);
        point->qos = 1U;
    }
}
