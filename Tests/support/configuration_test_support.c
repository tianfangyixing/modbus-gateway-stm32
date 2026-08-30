#include "configuration_test_support.h"

#include "mbedtls/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIGURATION_TEST_FIXTURE_DIR
#error "CONFIGURATION_TEST_FIXTURE_DIR must name the checked-in fixture directory"
#endif

#define CONFIGURATION_TEST_PATH_CAPACITY 1024U

static void *failing_calloc(size_t count, size_t size)
{
    if (size != 0U && count > SIZE_MAX / size)
    {
        return NULL;
    }

    return NULL;
}

static bool set_bytes(uint8_t *destination, size_t capacity, uint16_t *destination_length,
                      const uint8_t *source, size_t source_length)
{
    if (destination == NULL || destination_length == NULL || source == NULL || source_length >= capacity ||
        source_length > UINT16_MAX)
    {
        return false;
    }

    memset(destination, 0, capacity);
    memcpy(destination, source, source_length);
    *destination_length = (uint16_t)source_length;
    return true;
}

bool configuration_test_use_standard_allocator(void)
{
    return mbedtls_platform_set_calloc_free(calloc, free) == 0;
}

bool configuration_test_use_failing_allocator(void)
{
    return mbedtls_platform_set_calloc_free(failing_calloc, free) == 0;
}

void configuration_test_set_ipv4(ip4_addr_t *address, uint8_t first, uint8_t second, uint8_t third, uint8_t fourth)
{
    IP4_ADDR(address, first, second, third, fourth);
}

bool configuration_test_set_hostname(configuration_hostname_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_set_client_id(configuration_client_id_value_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_set_username(configuration_username_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_set_password(configuration_password_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_set_certificate(configuration_ca_certificate_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_set_topic(configuration_topic_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_set_payload(configuration_mqtt_message_payload_t *field, const uint8_t *bytes, size_t length)
{
    return field != NULL &&
           set_bytes(field->bytes, sizeof(field->bytes), &field->length, bytes, length);
}

bool configuration_test_load_fixture(const char *name, uint8_t *bytes, size_t capacity, size_t *length)
{
    char path[CONFIGURATION_TEST_PATH_CAPACITY];
    FILE *file;
    size_t bytes_read;
    int extra_byte;
    int read_error;
    int close_result;
    int path_length;

    if (name == NULL || bytes == NULL || capacity == 0U || length == NULL)
    {
        return false;
    }

    path_length = snprintf(path, sizeof(path), "%s/%s", CONFIGURATION_TEST_FIXTURE_DIR, name);
    if (path_length < 0 || (size_t)path_length >= sizeof(path))
    {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL)
    {
        return false;
    }

    bytes_read = fread(bytes, 1U, capacity, file);
    extra_byte = fgetc(file);
    read_error = ferror(file);
    close_result = fclose(file);
    if (read_error != 0 || extra_byte != EOF || close_result != 0)
    {
        return false;
    }

    *length = bytes_read;
    return true;
}

bool configuration_test_set_certificate_fixture(configuration_ca_certificate_t *field, const char *name)
{
    size_t length;

    if (field == NULL || !configuration_test_load_fixture(name, field->bytes, sizeof(field->bytes) - 1U, &length))
    {
        return false;
    }

    field->length = (uint16_t)length;
    field->bytes[length] = 0U;
    return true;
}

bool configuration_test_make_valid_point(configuration_collection_point_t *point, uint8_t index)
{
    char topic[32];
    int topic_length;

    if (point == NULL)
    {
        return false;
    }

    memset(point, 0, sizeof(*point));
    point->slave_address = (uint8_t)(index + 1U);
    point->source = CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER;
    point->address = index;
    point->data_type = CONFIGURATION_DATA_TYPE_UINT16;
    point->poll_interval_ms = UINT32_C(60000);
    point->first_byte_timeout_ms = UINT16_C(1000);
    point->qos = 0U;

    topic_length = snprintf(topic, sizeof(topic), "points/%u", (unsigned int)index);
    return topic_length > 0 && (size_t)topic_length < sizeof(topic) &&
           configuration_test_set_topic(&point->topic, (const uint8_t *)topic, (size_t)topic_length);
}

bool configuration_test_make_valid_mqtt(configuration_t *configuration)
{
    static const uint8_t broker[] = "broker.example.com";
    static const uint8_t username[] = "user";
    static const uint8_t password[] = "password";

    if (configuration == NULL)
    {
        return false;
    }

    configuration_set_defaults(configuration);
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_ENABLED;
    configuration->mqtt.broker_port = UINT16_C(8883);
    configuration->mqtt.client_id.mode = CONFIGURATION_CLIENT_ID_MODE_DERIVED;
    configuration->mqtt.keep_alive_seconds = UINT16_C(60);
    configuration->mqtt.online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    configuration->mqtt.will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    configuration->collection.point_count = 1U;

    return configuration_test_set_hostname(&configuration->mqtt.broker_address, broker, sizeof(broker) - 1U) &&
           configuration_test_set_username(&configuration->mqtt.username, username, sizeof(username) - 1U) &&
           configuration_test_set_password(&configuration->mqtt.password, password, sizeof(password) - 1U) &&
           configuration_test_set_certificate_fixture(&configuration->mqtt.ca_certificate_pem, "valid_root.pem") &&
           configuration_test_make_valid_point(&configuration->collection.points[0], 0U);
}

void configuration_test_make_valid_static_network(configuration_t *configuration)
{
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_STATIC;
    configuration_test_set_ipv4(&configuration->network.ip_address, 192U, 168U, 10U, 10U);
    configuration_test_set_ipv4(&configuration->network.subnet_mask, 255U, 255U, 255U, 0U);
    configuration_test_set_ipv4(&configuration->network.gateway, 192U, 168U, 10U, 1U);
    configuration_test_set_ipv4(&configuration->network.dns_primary, 8U, 8U, 8U, 8U);
    configuration_test_set_ipv4(&configuration->network.dns_secondary, 1U, 1U, 1U, 1U);
}
