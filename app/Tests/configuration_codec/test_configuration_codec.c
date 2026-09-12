#include "configuration_binary_codec.h"
#include "configuration_test_fixtures.h"
#include "mbedtls/platform.h"
#include "unity.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static configuration_t model;
static configuration_t decoded;
static uint8_t payload[8476];
static uint32_t payload_length;
static size_t allocation_attempts;

void setUp(void)
{
    configuration_test_make_maximum(&model);
    memset(&decoded, 0xA5, sizeof(decoded));
    memset(payload, 0xA5, sizeof(payload));
    payload_length = UINT32_MAX;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, mbedtls_platform_set_calloc_free(calloc, free));
}

static void assert_zero_tail(const uint8_t *bytes, uint16_t length, size_t capacity)
{
    for (size_t index = length; index < capacity; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, bytes[index]);
    }
}

static void assert_hostname_tail(const configuration_hostname_t *hostname)
{
    assert_zero_tail(hostname->bytes, hostname->length, sizeof(hostname->bytes));
}

static void assert_message_tails(const configuration_mqtt_message_t *message)
{
    assert_zero_tail(message->topic.bytes, message->topic.length, sizeof(message->topic.bytes));
    assert_zero_tail(message->payload.bytes, message->payload.length, sizeof(message->payload.bytes));
}

static void assert_all_text_tails(const configuration_t *configuration)
{
    assert_hostname_tail(&configuration->sntp.servers[0].value.hostname);
    assert_hostname_tail(&configuration->sntp.servers[1].value.hostname);
    assert_hostname_tail(&configuration->mqtt.broker_address);
    assert_zero_tail(configuration->mqtt.client_id.explicit_value.bytes,
                     configuration->mqtt.client_id.explicit_value.length,
                     sizeof(configuration->mqtt.client_id.explicit_value.bytes));
    assert_zero_tail(configuration->mqtt.username.bytes, configuration->mqtt.username.length,
                     sizeof(configuration->mqtt.username.bytes));
    assert_zero_tail(configuration->mqtt.password.bytes, configuration->mqtt.password.length,
                     sizeof(configuration->mqtt.password.bytes));
    assert_zero_tail(configuration->mqtt.ca_certificate_pem.bytes, configuration->mqtt.ca_certificate_pem.length,
                     sizeof(configuration->mqtt.ca_certificate_pem.bytes));
    assert_message_tails(&configuration->mqtt.online_message);
    assert_message_tails(&configuration->mqtt.will_message);
    for (uint8_t index = 0U; index < CONFIGURATION_COLLECTION_POINT_MAX_COUNT; index++)
    {
        const configuration_topic_t *topic = &configuration->collection.points[index].topic;
        assert_zero_tail(topic->bytes, topic->length, sizeof(topic->bytes));
    }
}

static void assert_valid_round_trip(void)
{
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&model));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_encode(&model, payload, sizeof(payload), &payload_length));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_decode(payload, payload_length, &decoded));
    TEST_ASSERT_TRUE(configuration_equals(&model, &decoded));
    assert_all_text_tails(&decoded);
}

static uint8_t *auth_bytes(configuration_t *configuration, unsigned int field, uint16_t **length)
{
    if (field == 0U)
    {
        *length = &configuration->mqtt.client_id.explicit_value.length;
        return configuration->mqtt.client_id.explicit_value.bytes;
    }
    if (field == 1U)
    {
        *length = &configuration->mqtt.username.length;
        return configuration->mqtt.username.bytes;
    }
    *length = &configuration->mqtt.password.length;
    return configuration->mqtt.password.bytes;
}

static void set_auth_length(unsigned int field, uint16_t length)
{
    uint16_t *field_length;
    uint8_t *bytes = auth_bytes(&model, field, &field_length);
    memset(bytes, 'A', length);
    bytes[length] = 0U;
    *field_length = length;
}

static void test_constants_and_storage_capacity(void)
{
    TEST_ASSERT_EQUAL_UINT8(2, CONFIGURATION_SCHEMA_VERSION);
    TEST_ASSERT_EQUAL_UINT32(8475, CONFIGURATION_V2_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(48, CONFIGURATION_V2_DEFAULT_PAYLOAD_LENGTH);
    TEST_ASSERT_EQUAL_UINT16(253, CONFIGURATION_HOSTNAME_MAX_LENGTH);
    TEST_ASSERT_EQUAL_UINT16(256, CONFIGURATION_HOSTNAME_BUFFER_SIZE);
    TEST_ASSERT_EQUAL_UINT16(256, CONFIGURATION_CLIENT_ID_MAX_LENGTH);
    TEST_ASSERT_EQUAL_UINT16(256, CONFIGURATION_USERNAME_MAX_LENGTH);
    TEST_ASSERT_EQUAL_UINT16(256, CONFIGURATION_PASSWORD_MAX_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(256, sizeof(model.mqtt.broker_address.bytes));
    TEST_ASSERT_EQUAL_UINT32(257, sizeof(model.mqtt.client_id.explicit_value.bytes));
    TEST_ASSERT_EQUAL_UINT32(257, sizeof(model.mqtt.username.bytes));
    TEST_ASSERT_EQUAL_UINT32(257, sizeof(model.mqtt.password.bytes));
}

static void test_default_independent_vector_and_zeroing(void)
{
    memset(&model, 0xA5, sizeof(model));
    configuration_set_defaults(&model);
    assert_valid_round_trip();
    TEST_ASSERT_EQUAL_UINT32(48, payload_length);
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_default_payload, payload, 48);
    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_MQTT_MODE_DISABLED, decoded.mqtt.mode);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.collection.point_count);
    TEST_ASSERT_EQUAL_UINT32(0, ip4_addr_get_u32(&decoded.network.ip_address));
    TEST_ASSERT_EQUAL_UINT16(0, decoded.mqtt.broker_port);
    TEST_ASSERT_EQUAL_UINT16(0, decoded.mqtt.keep_alive_seconds);
    for (uint8_t index = 0U; index < 16U; index++)
    {
        TEST_ASSERT_EQUAL_UINT32(0, decoded.collection.points[index].poll_interval_ms);
    }
    configuration_set_defaults(NULL);
    configuration_set_defaults(&decoded);
    TEST_ASSERT_TRUE(configuration_equals(&model, &decoded));
}

static void test_maximum_independent_vector_and_real_ca(void)
{
    assert_valid_round_trip();
    TEST_ASSERT_EQUAL_UINT32(8475, payload_length);
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_maximum_payload, payload, 8475);
    TEST_ASSERT_EQUAL_UINT8(0, payload[802]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[803]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[1060]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[1061]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[1318]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[1319]);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_decode(configuration_test_maximum_payload, 8475, &decoded));
    TEST_ASSERT_TRUE(configuration_equals(&model, &decoded));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_encode(&decoded, payload, 8475, &payload_length));
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_maximum_payload, payload, 8475);
}

static void test_shortest_complete_vector(void)
{
    static const uint8_t shortest[21] =
    {
        0x02, 0x00, 0x80, 0x25, 0x00, 0x00, 0x03, 0xE8, 0x03, 0xF6, 0x01,
        0x00, 0x01, 0x00, 'a', 0x00, 0x01, 0x00, 'b', 0x00, 0x00
    };
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_decode(shortest, sizeof(shortest), &decoded));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&decoded));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_encode(&decoded, payload, 21, &payload_length));
    TEST_ASSERT_EQUAL_UINT32(21, payload_length);
    TEST_ASSERT_EQUAL_MEMORY(shortest, payload, 21);
}

static void test_each_credential_length_1_255_256(void)
{
    static const uint16_t lengths[] =
    {
        1U, 255U, 256U
    };
    for (unsigned int field = 0U; field < 3U; field++)
    {
        for (size_t index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); index++)
        {
            uint16_t *decoded_length;
            configuration_test_make_maximum(&model);
            set_auth_length(field, lengths[index]);
            assert_valid_round_trip();
            TEST_ASSERT_EQUAL_UINT32(8475U - 256U + lengths[index], payload_length);
            uint8_t *bytes = auth_bytes(&decoded, field, &decoded_length);
            TEST_ASSERT_EQUAL_UINT16(lengths[index], *decoded_length);
            TEST_ASSERT_EQUAL_UINT8('A', bytes[lengths[index] - 1U]);
        }
    }
}

static void test_each_credential_rejects_0_257_and_uint16_max(void)
{
    static const uint16_t invalid_lengths[] =
    {
        0U, 257U, UINT16_MAX
    };
    for (unsigned int field = 0U; field < 3U; field++)
    {
        for (size_t index = 0U; index < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); index++)
        {
            uint16_t *length;
            configuration_test_make_maximum(&model);
            uint8_t *bytes = auth_bytes(&model, field, &length);
            memset(bytes, 'A', 257U);
            *length = invalid_lengths[index];
            TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_MQTT_INVALID, configuration_validate(&model));
        }
    }
}

static void test_each_credential_rejects_embedded_or_missing_nul(void)
{
    for (unsigned int field = 0U; field < 3U; field++)
    {
        uint16_t *length;
        configuration_test_make_maximum(&model);
        uint8_t *bytes = auth_bytes(&model, field, &length);
        bytes[127] = 0U;
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_MQTT_INVALID, configuration_validate(&model));
        bytes[127] = 'A';
        bytes[*length] = 'A';
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_MQTT_INVALID, configuration_validate(&model));
    }
}

static void test_client_id_character_set_exhaustive(void)
{
    set_auth_length(0U, 1U);
    for (unsigned int value = 1U; value <= 255U; value++)
    {
        bool accepted = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                        (value >= '0' && value <= '9') || value == '_' || value == '-';
        model.mqtt.client_id.explicit_value.bytes[0] = (uint8_t)value;
        TEST_ASSERT_EQUAL_INT(accepted ? CONFIGURATION_VALIDATION_OK : CONFIGURATION_VALIDATION_MQTT_INVALID,
                              configuration_validate(&model));
    }
}

static void test_username_password_printable_ascii_exhaustive(void)
{
    for (unsigned int field = 1U; field < 3U; field++)
    {
        uint16_t *length;
        configuration_test_make_maximum(&model);
        set_auth_length(field, 1U);
        uint8_t *bytes = auth_bytes(&model, field, &length);
        TEST_ASSERT_EQUAL_UINT16(1, *length);
        for (unsigned int value = 1U; value <= 255U; value++)
        {
            bytes[0] = (uint8_t)value;
            TEST_ASSERT_EQUAL_INT(value >= 0x20U && value <= 0x7EU ? CONFIGURATION_VALIDATION_OK :
                                  CONFIGURATION_VALIDATION_MQTT_INVALID, configuration_validate(&model));
        }
    }
}

static configuration_hostname_t *select_hostname(unsigned int field)
{
    if (field < 2U)
    {
        return &model.sntp.servers[field].value.hostname;
    }
    return &model.mqtt.broker_address;
}

static void test_all_hostnames_length_sentinel_and_label_rules(void)
{
    static const uint16_t invalid_lengths[] =
    {
        254U, 255U, 256U, UINT16_MAX
    };
    for (unsigned int field = 0U; field < 3U; field++)
    {
        configuration_test_make_maximum(&model);
        configuration_hostname_t *hostname = select_hostname(field);
        configuration_validation_result_t invalid = field < 2U ? CONFIGURATION_VALIDATION_SNTP_INVALID :
                                                                CONFIGURATION_VALIDATION_MQTT_INVALID;
        hostname->bytes[254] = 0xA5;
        hostname->bytes[255] = 0x5A;
        assert_valid_round_trip();
        for (size_t index = 0U; index < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); index++)
        {
            hostname->length = invalid_lengths[index];
            TEST_ASSERT_EQUAL_INT(invalid, configuration_validate(&model));
        }
        hostname->length = 253U;
        hostname->bytes[253] = 'x';
        TEST_ASSERT_EQUAL_INT(invalid, configuration_validate(&model));
        hostname->bytes[253] = 0U;
        hostname->bytes[1] = 0U;
        TEST_ASSERT_EQUAL_INT(invalid, configuration_validate(&model));
        hostname->bytes[1] = 'a';
        hostname->bytes[63] = 'a';
        TEST_ASSERT_EQUAL_INT(invalid, configuration_validate(&model));
        hostname->bytes[63] = '.';
        hostname->bytes[0] = '-';
        TEST_ASSERT_EQUAL_INT(invalid, configuration_validate(&model));
    }
}

static void test_all_unknown_schemas_including_v1(void)
{
    memcpy(payload, configuration_test_default_payload, 48);
    for (unsigned int version = 0U; version <= 255U; version++)
    {
        if (version != 2U)
        {
            payload[0] = (uint8_t)version;
            TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED,
                                  configuration_binary_decode(payload, 48, &decoded));
        }
    }
}

static void test_every_truncation_of_maximum_payload(void)
{
    for (uint32_t length = 1U; length < 8475U; length++)
    {
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MALFORMED,
                              configuration_binary_decode(configuration_test_maximum_payload, length, &decoded));
    }
}

static void test_zero_oversize_and_trailing_byte(void)
{
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID,
                          configuration_binary_decode(payload, 0, &decoded));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID,
                          configuration_binary_decode(payload, 8476, &decoded));
    memcpy(payload, configuration_test_default_payload, 48);
    payload[48] = 0U;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MALFORMED,
                          configuration_binary_decode(payload, 49, &decoded));
}

static void test_small_encoder_buffers_have_no_overwrite(void)
{
    configuration_set_defaults(&model);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&model));
    for (uint32_t capacity = 0U; capacity < 48U; capacity++)
    {
        memset(payload, 0xA5, sizeof(payload));
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL,
                              configuration_binary_encode(&model, payload + 1, capacity, &payload_length));
        TEST_ASSERT_EQUAL_UINT8(0xA5, payload[0]);
        TEST_ASSERT_EQUAL_UINT8(0xA5, payload[capacity + 1U]);
    }
    configuration_test_make_maximum(&model);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&model));
    memset(payload, 0xA5, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL,
                          configuration_binary_encode(&model, payload + 1, 8474, &payload_length));
    TEST_ASSERT_EQUAL_UINT8(0xA5, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA5, payload[8475]);
}

static void test_null_arguments(void)
{
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_INVALID_ARGUMENT, configuration_validate(NULL));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT,
                          configuration_binary_decode(NULL, 48, &decoded));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT,
                          configuration_binary_decode(configuration_test_default_payload, 48, NULL));
    configuration_set_defaults(&model);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT,
                          configuration_binary_encode(NULL, payload, sizeof(payload), &payload_length));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT,
                          configuration_binary_encode(&model, NULL, sizeof(payload), &payload_length));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT,
                          configuration_binary_encode(&model, payload, sizeof(payload), NULL));
}

static void test_wire_credential_257_rejected_without_truncation(void)
{
    /* With all three credentials shortened to one byte, their length offsets are fixed below. */
    static const size_t offsets[] =
    {
        802U, 805U, 808U
    };
    for (unsigned int field = 0U; field < 3U; field++)
    {
        configuration_test_make_maximum(&model);
        for (unsigned int credential = 0U; credential < 3U; credential++)
        {
            set_auth_length(credential, 1U);
        }
        assert_valid_round_trip();
        TEST_ASSERT_EQUAL_UINT32(7710, payload_length);
        size_t offset = offsets[field];
        memmove(payload + offset + 259U, payload + offset + 3U, payload_length - offset - 3U);
        payload[offset] = 1U;
        payload[offset + 1U] = 1U;
        memset(payload + offset + 2U, 'A', 257U);
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MALFORMED,
                              configuration_binary_decode(payload, payload_length + 256U, &decoded));
    }
}

static void test_wire_nul_and_fixed_value_errors_map_to_model_invalid(void)
{
    static const size_t text_offsets[] =
    {
        34U, 290U, 546U, 804U, 1062U, 1320U, 1578U
    };
    for (size_t index = 0U; index < sizeof(text_offsets) / sizeof(text_offsets[0]); index++)
    {
        memcpy(payload, configuration_test_maximum_payload, 8475);
        payload[text_offsets[index]] = 0U;
        TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MODEL_INVALID,
                              configuration_binary_decode(payload, 8475, &decoded));
    }
    memcpy(payload, configuration_test_default_payload, 48);
    payload[6] = 0xFF;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MODEL_INVALID,
                          configuration_binary_decode(payload, 48, &decoded));
}

static void test_wire_hostname_254_and_invalid_branch_rejected(void)
{
    /* The default SNTP hostname is expanded from 14 to 254 content bytes. */
    memcpy(payload, configuration_test_default_payload, 48);
    memmove(payload + 268U, payload + 28U, 20U);
    payload[12] = 254U;
    payload[13] = 0U;
    memset(payload + 14U, 'a', 254U);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MALFORMED,
                          configuration_binary_decode(payload, 288U, &decoded));
    memcpy(payload, configuration_test_default_payload, 48);
    payload[1] = 2U;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_MALFORMED,
                          configuration_binary_decode(payload, 48, &decoded));
}

static void test_semantic_comparison_ignores_spare_bytes_and_point_order(void)
{
    assert_valid_round_trip();
    decoded.mqtt.broker_address.bytes[0] = 'M';
    decoded.mqtt.broker_address.bytes[254] = 0xA5;
    decoded.sntp.servers[0].value.hostname.bytes[255] = 0x5A;
    configuration_collection_point_t temporary = decoded.collection.points[0];
    decoded.collection.points[0] = decoded.collection.points[15];
    decoded.collection.points[15] = temporary;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&decoded));
    TEST_ASSERT_TRUE(configuration_equals(&model, &decoded));
    TEST_ASSERT_TRUE(configuration_equals(&decoded, &model));
    decoded.mqtt.client_id.explicit_value.bytes[0] = 'c';
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&decoded));
    TEST_ASSERT_FALSE(configuration_equals(&model, &decoded));
}

static void test_semantic_comparison_and_encoding_ignore_inactive_storage(void)
{
    configuration_set_defaults(&model);
    configuration_set_defaults(&decoded);
    memset(&decoded.mqtt, 0xA5, sizeof(decoded.mqtt));
    decoded.mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    memset(&decoded.collection.points, 0xA5, sizeof(decoded.collection.points));
    decoded.network.ip_address.addr = UINT32_MAX;
    decoded.sntp.servers[0].value.hostname.bytes[255] = 0xA5;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_OK, configuration_validate(&decoded));
    TEST_ASSERT_TRUE(configuration_equals(&model, &decoded));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_OK,
                          configuration_binary_encode(&decoded, payload, sizeof(payload), &payload_length));
    TEST_ASSERT_EQUAL_MEMORY(configuration_test_default_payload, payload, 48);
}

static void test_corrupt_ca_and_excessive_bus_load_rejected(void)
{
    /* Change one Base64 byte in the RSA signature; PEM remains syntactically readable. */
    uint16_t index = model.mqtt.ca_certificate_pem.length;
    while (index > 0U && model.mqtt.ca_certificate_pem.bytes[index - 1U] == '\n')
    {
        index--;
    }
    TEST_ASSERT_TRUE(index > 32U);
    index = (uint16_t)(index - 32U);
    model.mqtt.ca_certificate_pem.bytes[index] = model.mqtt.ca_certificate_pem.bytes[index] == 'A' ? 'B' : 'A';
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_CERTIFICATE_INVALID, configuration_validate(&model));
    configuration_test_make_maximum(&model);
    model.rtu.baud_rate = 1200U;
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED, configuration_validate(&model));
}

static void *failing_calloc(size_t count, size_t size)
{
    if (count > 0U && size > 0U)
    {
        allocation_attempts++;
    }
    return NULL;
}

static void test_real_certificate_allocation_failure_maps_to_resource_unavailable(void)
{
    allocation_attempts = 0U;
    TEST_ASSERT_EQUAL_INT(0, mbedtls_platform_set_calloc_free(failing_calloc, free));
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE, configuration_validate(&model));
    TEST_ASSERT_TRUE(allocation_attempts > 0U);
    TEST_ASSERT_EQUAL_INT(CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE,
                          configuration_binary_decode(configuration_test_maximum_payload, 8475, &decoded));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_constants_and_storage_capacity);
    RUN_TEST(test_default_independent_vector_and_zeroing);
    RUN_TEST(test_maximum_independent_vector_and_real_ca);
    RUN_TEST(test_shortest_complete_vector);
    RUN_TEST(test_each_credential_length_1_255_256);
    RUN_TEST(test_each_credential_rejects_0_257_and_uint16_max);
    RUN_TEST(test_each_credential_rejects_embedded_or_missing_nul);
    RUN_TEST(test_client_id_character_set_exhaustive);
    RUN_TEST(test_username_password_printable_ascii_exhaustive);
    RUN_TEST(test_all_hostnames_length_sentinel_and_label_rules);
    RUN_TEST(test_all_unknown_schemas_including_v1);
    RUN_TEST(test_every_truncation_of_maximum_payload);
    RUN_TEST(test_zero_oversize_and_trailing_byte);
    RUN_TEST(test_small_encoder_buffers_have_no_overwrite);
    RUN_TEST(test_null_arguments);
    RUN_TEST(test_wire_credential_257_rejected_without_truncation);
    RUN_TEST(test_wire_nul_and_fixed_value_errors_map_to_model_invalid);
    RUN_TEST(test_wire_hostname_254_and_invalid_branch_rejected);
    RUN_TEST(test_semantic_comparison_ignores_spare_bytes_and_point_order);
    RUN_TEST(test_semantic_comparison_and_encoding_ignore_inactive_storage);
    RUN_TEST(test_corrupt_ca_and_excessive_bus_load_rejected);
    RUN_TEST(test_real_certificate_allocation_failure_maps_to_resource_unavailable);
    return UNITY_END();
}
