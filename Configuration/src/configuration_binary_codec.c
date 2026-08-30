#include "configuration_binary_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    const uint8_t *current;
    const uint8_t *end;
} configuration_binary_reader_t;

typedef struct
{
    uint8_t *begin;
    uint8_t *current;
    uint8_t *end;
} configuration_binary_writer_t;

static bool reader_read_u8(configuration_binary_reader_t *reader, uint8_t *value)
{
    if (reader->current == reader->end)
    {
        return false;
    }

    *value = *reader->current;
    reader->current++;
    return true;
}

static bool reader_read_u16(configuration_binary_reader_t *reader, uint16_t *value)
{
    if ((size_t)(reader->end - reader->current) < 2U)
    {
        return false;
    }

    *value = (uint16_t)((uint16_t)reader->current[0] | ((uint16_t)reader->current[1] << 8U));
    reader->current += 2;
    return true;
}

static bool reader_read_u32(configuration_binary_reader_t *reader, uint32_t *value)
{
    if ((size_t)(reader->end - reader->current) < 4U)
    {
        return false;
    }

    *value = (uint32_t)reader->current[0] | ((uint32_t)reader->current[1] << 8U) |
             ((uint32_t)reader->current[2] << 16U) | ((uint32_t)reader->current[3] << 24U);
    reader->current += 4;
    return true;
}

static bool reader_read_bytes(configuration_binary_reader_t *reader, uint8_t *destination, uint16_t length)
{
    if ((size_t)(reader->end - reader->current) < length)
    {
        return false;
    }

    if (length > 0U)
    {
        memcpy(destination, reader->current, length);
    }
    reader->current += length;
    return true;
}

static bool reader_read_text(configuration_binary_reader_t *reader, uint8_t *destination, uint16_t minimum_length,
                             uint16_t maximum_length, uint16_t *length)
{
    if (!reader_read_u16(reader, length) || *length < minimum_length || *length > maximum_length ||
        !reader_read_bytes(reader, destination, *length))
    {
        return false;
    }

    destination[*length] = 0U;
    return true;
}

static bool reader_read_ipv4(configuration_binary_reader_t *reader, ip4_addr_t *address)
{
    uint8_t octets[4];

    if (!reader_read_bytes(reader, octets, sizeof(octets)))
    {
        return false;
    }

    IP4_ADDR(address, octets[0], octets[1], octets[2], octets[3]);
    return true;
}

static bool reader_read_endpoint(configuration_binary_reader_t *reader, configuration_endpoint_address_t *endpoint)
{
    if (!reader_read_u8(reader, &endpoint->type))
    {
        return false;
    }

    if (endpoint->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
    {
        return reader_read_text(reader, endpoint->value.hostname.bytes, 1U,
                                CONFIGURATION_HOSTNAME_MAX_LENGTH, &endpoint->value.hostname.length);
    }

    if (endpoint->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4)
    {
        return reader_read_ipv4(reader, &endpoint->value.ipv4);
    }

    return false;
}

static bool reader_read_client_id(configuration_binary_reader_t *reader, configuration_client_id_t *client_id)
{
    if (!reader_read_u8(reader, &client_id->mode))
    {
        return false;
    }

    if (client_id->mode == CONFIGURATION_CLIENT_ID_MODE_DERIVED)
    {
        return true;
    }

    if (client_id->mode == CONFIGURATION_CLIENT_ID_MODE_EXPLICIT)
    {
        return reader_read_text(reader, client_id->explicit_value.bytes, 1U,
                                CONFIGURATION_CLIENT_ID_MAX_LENGTH, &client_id->explicit_value.length);
    }

    return false;
}

static bool reader_read_mqtt_message(configuration_binary_reader_t *reader,
                                       configuration_mqtt_message_t *message)
{
    if (!reader_read_u8(reader, &message->mode))
    {
        return false;
    }

    if (message->mode == CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED)
    {
        return true;
    }

    if (message->mode != CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM ||
        !reader_read_text(reader, message->topic.bytes, 1U, CONFIGURATION_TOPIC_MAX_LENGTH, &message->topic.length) ||
        !reader_read_text(reader, message->payload.bytes, 0U, CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH,
                          &message->payload.length) ||
        !reader_read_u8(reader, &message->qos) || !reader_read_u8(reader, &message->retain))
    {
        return false;
    }

    return true;
}

static configuration_binary_codec_result_t decode_fields(configuration_binary_reader_t *reader,
                                                           configuration_t *configuration)
{
    uint8_t point_index;

    if (!reader_read_u8(reader, &configuration->network.mode))
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    if (configuration->network.mode == CONFIGURATION_NETWORK_MODE_STATIC)
    {
        if (!reader_read_ipv4(reader, &configuration->network.ip_address) ||
            !reader_read_ipv4(reader, &configuration->network.subnet_mask) ||
            !reader_read_ipv4(reader, &configuration->network.gateway) ||
            !reader_read_ipv4(reader, &configuration->network.dns_primary) ||
            !reader_read_ipv4(reader, &configuration->network.dns_secondary))
        {
            return CONFIGURATION_BINARY_CODEC_MALFORMED;
        }
    }
    else if (configuration->network.mode != CONFIGURATION_NETWORK_MODE_DHCP)
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    if (!reader_read_u32(reader, &configuration->rtu.baud_rate) ||
        !reader_read_u8(reader, &configuration->rtu.frame_format) ||
        !reader_read_u16(reader, &configuration->rtu.first_byte_timeout_ms) ||
        !reader_read_u16(reader, &configuration->modbus_tcp.listen_port) ||
        !reader_read_endpoint(reader, &configuration->sntp.servers[0]) ||
        !reader_read_endpoint(reader, &configuration->sntp.servers[1]))
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    if (!reader_read_u8(reader, &configuration->mqtt.mode))
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    if (configuration->mqtt.mode == CONFIGURATION_MQTT_MODE_ENABLED)
    {
        if (!reader_read_text(reader, configuration->mqtt.broker_address.bytes, 1U,
                              CONFIGURATION_HOSTNAME_MAX_LENGTH, &configuration->mqtt.broker_address.length) ||
            !reader_read_u16(reader, &configuration->mqtt.broker_port) ||
            !reader_read_client_id(reader, &configuration->mqtt.client_id) ||
            !reader_read_text(reader, configuration->mqtt.username.bytes, 1U,
                              CONFIGURATION_USERNAME_MAX_LENGTH, &configuration->mqtt.username.length) ||
            !reader_read_text(reader, configuration->mqtt.password.bytes, 1U,
                              CONFIGURATION_PASSWORD_MAX_LENGTH, &configuration->mqtt.password.length) ||
            !reader_read_text(reader, configuration->mqtt.ca_certificate_pem.bytes, 1U,
                              CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH,
                              &configuration->mqtt.ca_certificate_pem.length) ||
            !reader_read_u16(reader, &configuration->mqtt.keep_alive_seconds) ||
            !reader_read_mqtt_message(reader, &configuration->mqtt.online_message) ||
            !reader_read_mqtt_message(reader, &configuration->mqtt.will_message))
        {
            return CONFIGURATION_BINARY_CODEC_MALFORMED;
        }
    }
    else if (configuration->mqtt.mode != CONFIGURATION_MQTT_MODE_DISABLED)
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    if (!reader_read_u8(reader, &configuration->collection.point_count) ||
        configuration->collection.point_count > CONFIGURATION_COLLECTION_POINT_MAX_COUNT)
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    for (point_index = 0U; point_index < configuration->collection.point_count; point_index++)
    {
        configuration_collection_point_t *point = &configuration->collection.points[point_index];

        if (!reader_read_u8(reader, &point->slave_address) || !reader_read_u8(reader, &point->source) ||
            !reader_read_u16(reader, &point->address))
        {
            return CONFIGURATION_BINARY_CODEC_MALFORMED;
        }

        if (point->source == CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER ||
            point->source == CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER)
        {
            if (!reader_read_u8(reader, &point->data_type))
            {
                return CONFIGURATION_BINARY_CODEC_MALFORMED;
            }
        }
        else if (point->source != CONFIGURATION_COLLECTION_SOURCE_COIL &&
                 point->source != CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT)
        {
            return CONFIGURATION_BINARY_CODEC_MALFORMED;
        }

        if (!reader_read_u32(reader, &point->poll_interval_ms) ||
            !reader_read_u16(reader, &point->first_byte_timeout_ms) ||
            !reader_read_text(reader, point->topic.bytes, 1U, CONFIGURATION_TOPIC_MAX_LENGTH, &point->topic.length) ||
            !reader_read_u8(reader, &point->qos))
        {
            return CONFIGURATION_BINARY_CODEC_MALFORMED;
        }
    }

    return CONFIGURATION_BINARY_CODEC_OK;
}

configuration_binary_codec_result_t configuration_binary_decode(const uint8_t *payload, uint32_t payload_length,
                                                                  configuration_t *configuration)
{
    configuration_binary_reader_t reader;
    configuration_binary_codec_result_t decode_result;
    configuration_validation_result_t validation_result;
    uint8_t schema_version;

    if (payload == NULL || configuration == NULL)
    {
        return CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT;
    }

    if (payload_length == 0U || payload_length > CONFIGURATION_V1_MAX_PAYLOAD_LENGTH)
    {
        return CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID;
    }

    reader.current = payload;
    reader.end = payload + payload_length;
    if (!reader_read_u8(&reader, &schema_version) || schema_version != CONFIGURATION_SCHEMA_VERSION)
    {
        return CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED;
    }

    memset(configuration, 0, sizeof(*configuration));
    decode_result = decode_fields(&reader, configuration);
    if (decode_result != CONFIGURATION_BINARY_CODEC_OK || reader.current != reader.end)
    {
        return CONFIGURATION_BINARY_CODEC_MALFORMED;
    }

    validation_result = configuration_validate(configuration);
    if (validation_result == CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE)
    {
        return CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE;
    }
    if (validation_result != CONFIGURATION_VALIDATION_OK)
    {
        return CONFIGURATION_BINARY_CODEC_MODEL_INVALID;
    }

    return CONFIGURATION_BINARY_CODEC_OK;
}

static bool writer_write_u8(configuration_binary_writer_t *writer, uint8_t value)
{
    if (writer->current == writer->end)
    {
        return false;
    }

    *writer->current = value;
    writer->current++;
    return true;
}

static bool writer_write_u16(configuration_binary_writer_t *writer, uint16_t value)
{
    if ((size_t)(writer->end - writer->current) < 2U)
    {
        return false;
    }

    writer->current[0] = (uint8_t)value;
    writer->current[1] = (uint8_t)(value >> 8U);
    writer->current += 2;
    return true;
}

static bool writer_write_u32(configuration_binary_writer_t *writer, uint32_t value)
{
    if ((size_t)(writer->end - writer->current) < 4U)
    {
        return false;
    }

    writer->current[0] = (uint8_t)value;
    writer->current[1] = (uint8_t)(value >> 8U);
    writer->current[2] = (uint8_t)(value >> 16U);
    writer->current[3] = (uint8_t)(value >> 24U);
    writer->current += 4;
    return true;
}

static bool writer_write_bytes(configuration_binary_writer_t *writer, const uint8_t *source, uint16_t length)
{
    if ((size_t)(writer->end - writer->current) < length)
    {
        return false;
    }

    if (length > 0U)
    {
        memcpy(writer->current, source, length);
    }
    writer->current += length;
    return true;
}

static bool writer_write_text(configuration_binary_writer_t *writer, const uint8_t *source, uint16_t length)
{
    return writer_write_u16(writer, length) && writer_write_bytes(writer, source, length);
}

static bool writer_write_ipv4(configuration_binary_writer_t *writer, const ip4_addr_t *address)
{
    uint8_t octets[4];

    octets[0] = ip4_addr1(address);
    octets[1] = ip4_addr2(address);
    octets[2] = ip4_addr3(address);
    octets[3] = ip4_addr4(address);
    return writer_write_bytes(writer, octets, sizeof(octets));
}

static bool writer_write_endpoint(configuration_binary_writer_t *writer,
                                  const configuration_endpoint_address_t *endpoint)
{
    if (!writer_write_u8(writer, endpoint->type))
    {
        return false;
    }

    if (endpoint->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
    {
        return writer_write_text(writer, endpoint->value.hostname.bytes, endpoint->value.hostname.length);
    }

    return writer_write_ipv4(writer, &endpoint->value.ipv4);
}

static bool writer_write_client_id(configuration_binary_writer_t *writer, const configuration_client_id_t *client_id)
{
    if (!writer_write_u8(writer, client_id->mode))
    {
        return false;
    }

    return client_id->mode == CONFIGURATION_CLIENT_ID_MODE_DERIVED ||
           writer_write_text(writer, client_id->explicit_value.bytes, client_id->explicit_value.length);
}

static bool writer_write_mqtt_message(configuration_binary_writer_t *writer,
                                        const configuration_mqtt_message_t *message)
{
    if (!writer_write_u8(writer, message->mode))
    {
        return false;
    }

    if (message->mode == CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED)
    {
        return true;
    }

    return writer_write_text(writer, message->topic.bytes, message->topic.length) &&
           writer_write_text(writer, message->payload.bytes, message->payload.length) &&
           writer_write_u8(writer, message->qos) && writer_write_u8(writer, message->retain);
}

configuration_binary_codec_result_t configuration_binary_encode(const configuration_t *configuration,
                                                                  uint8_t *payload, uint32_t payload_capacity,
                                                                  uint32_t *payload_length)
{
    configuration_binary_writer_t writer;
    uint8_t point_index;

    if(!configuration || !payload || !payload_length)
    {
        return CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT;
    }

    *payload_length = 0U;
    writer.begin = payload;
    writer.current = payload;
    writer.end = payload + payload_capacity;

    if (!writer_write_u8(&writer, CONFIGURATION_SCHEMA_VERSION) ||
        !writer_write_u8(&writer, configuration->network.mode))
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    if (configuration->network.mode == CONFIGURATION_NETWORK_MODE_STATIC &&
        (!writer_write_ipv4(&writer, &configuration->network.ip_address) ||
         !writer_write_ipv4(&writer, &configuration->network.subnet_mask) ||
         !writer_write_ipv4(&writer, &configuration->network.gateway) ||
         !writer_write_ipv4(&writer, &configuration->network.dns_primary) ||
         !writer_write_ipv4(&writer, &configuration->network.dns_secondary)))
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    if (!writer_write_u32(&writer, configuration->rtu.baud_rate) ||
        !writer_write_u8(&writer, configuration->rtu.frame_format) ||
        !writer_write_u16(&writer, configuration->rtu.first_byte_timeout_ms) ||
        !writer_write_u16(&writer, configuration->modbus_tcp.listen_port) ||
        !writer_write_endpoint(&writer, &configuration->sntp.servers[0]) ||
        !writer_write_endpoint(&writer, &configuration->sntp.servers[1]))
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    if (!writer_write_u8(&writer, configuration->mqtt.mode))
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    if (configuration->mqtt.mode == CONFIGURATION_MQTT_MODE_ENABLED &&
        (!writer_write_text(&writer, configuration->mqtt.broker_address.bytes,
                            configuration->mqtt.broker_address.length) ||
         !writer_write_u16(&writer, configuration->mqtt.broker_port) ||
         !writer_write_client_id(&writer, &configuration->mqtt.client_id) ||
         !writer_write_text(&writer, configuration->mqtt.username.bytes, configuration->mqtt.username.length) ||
         !writer_write_text(&writer, configuration->mqtt.password.bytes, configuration->mqtt.password.length) ||
         !writer_write_text(&writer, configuration->mqtt.ca_certificate_pem.bytes,
                            configuration->mqtt.ca_certificate_pem.length) ||
         !writer_write_u16(&writer, configuration->mqtt.keep_alive_seconds) ||
         !writer_write_mqtt_message(&writer, &configuration->mqtt.online_message) ||
         !writer_write_mqtt_message(&writer, &configuration->mqtt.will_message)))
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    if (!writer_write_u8(&writer, configuration->collection.point_count))
    {
        return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
    }

    for (point_index = 0U; point_index < configuration->collection.point_count; point_index++)
    {
        const configuration_collection_point_t *point = &configuration->collection.points[point_index];

        if (!writer_write_u8(&writer, point->slave_address) || !writer_write_u8(&writer, point->source) ||
            !writer_write_u16(&writer, point->address) ||
            ((point->source == CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER ||
              point->source == CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER) &&
             !writer_write_u8(&writer, point->data_type)) ||
            !writer_write_u32(&writer, point->poll_interval_ms) ||
            !writer_write_u16(&writer, point->first_byte_timeout_ms) ||
            !writer_write_text(&writer, point->topic.bytes, point->topic.length) ||
            !writer_write_u8(&writer, point->qos))
        {
            return CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL;
        }
    }

    *payload_length = (uint32_t)(writer.current - writer.begin);
    if (*payload_length == 0U || *payload_length > CONFIGURATION_V1_MAX_PAYLOAD_LENGTH)
    {
        *payload_length = 0U;
        return CONFIGURATION_BINARY_CODEC_MODEL_INVALID;
    }

    return CONFIGURATION_BINARY_CODEC_OK;
}
