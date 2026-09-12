/* Independent fixture reader; every codec/framer/validator call uses production C. */
#include "configuration_binary_codec.h"
#include "management_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            fprintf(stderr, "assertion at line %d: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static uint8_t input[9000];
static uint8_t output[9000];
static configuration_t configuration;
static configuration_t original;

static int check_text(uint8_t *bytes, uint16_t length, size_t capacity)
{
    size_t index;
    for (index = length; index < capacity; index++)
    {
        REQUIRE(bytes[index] == 0U);
    }
    bytes[length] = 1U;
    REQUIRE(configuration_validate(&configuration) != CONFIGURATION_VALIDATION_OK);
    bytes[length] = 0U;
    /* Keep the terminator; poison only spare capacity to prove it is not encoded. */
    for (index = (size_t)length + 1U; index < capacity; index++)
    {
        bytes[index] = 0xA5U;
    }
    return 0;
}

static int check_configuration(size_t length, const char *expectation)
{
    uint32_t encoded_length = 0;
    configuration_binary_codec_result_t decoded;
    size_t i;
    if (strcmp(expectation, "default") == 0)
    {
        configuration_set_defaults(&configuration);
        REQUIRE(configuration_validate(&configuration) == CONFIGURATION_VALIDATION_OK);
        REQUIRE(configuration_binary_encode(&configuration, output, sizeof(output), &encoded_length)
            == CONFIGURATION_BINARY_CODEC_OK);
        REQUIRE(encoded_length == length && memcmp(input, output, length) == 0);
    }
    decoded = configuration_binary_decode(input, (uint32_t)length, &configuration);
    printf("decode=%d\n", decoded);
    if (strcmp(expectation, "invalid") == 0)
    {
        REQUIRE(decoded != CONFIGURATION_BINARY_CODEC_OK);
        return 0;
    }
    if (strcmp(expectation, "schema") == 0)
    {
        REQUIRE(decoded == CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED);
        return 0;
    }
    REQUIRE(decoded == CONFIGURATION_BINARY_CODEC_OK);
    REQUIRE(configuration_validate(&configuration) == CONFIGURATION_VALIDATION_OK);
    original = configuration;
    for (i = 0; i < 2; i++)
    {
        configuration_hostname_t *host = &configuration.sntp.servers[i].value.hostname;
        if (configuration.sntp.servers[i].type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
        {
            REQUIRE(check_text(host->bytes, host->length, sizeof(host->bytes)) == 0);
        }
    }
    if (configuration.mqtt.mode == CONFIGURATION_MQTT_MODE_ENABLED)
    {
        REQUIRE(check_text(configuration.mqtt.broker_address.bytes,
            configuration.mqtt.broker_address.length, sizeof(configuration.mqtt.broker_address.bytes)) == 0);
        REQUIRE(check_text(configuration.mqtt.client_id.explicit_value.bytes,
            configuration.mqtt.client_id.explicit_value.length,
            sizeof(configuration.mqtt.client_id.explicit_value.bytes)) == 0);
        REQUIRE(check_text(configuration.mqtt.username.bytes, configuration.mqtt.username.length,
            sizeof(configuration.mqtt.username.bytes)) == 0);
        REQUIRE(check_text(configuration.mqtt.password.bytes, configuration.mqtt.password.length,
            sizeof(configuration.mqtt.password.bytes)) == 0);
        REQUIRE(check_text(configuration.mqtt.ca_certificate_pem.bytes,
            configuration.mqtt.ca_certificate_pem.length,
            sizeof(configuration.mqtt.ca_certificate_pem.bytes)) == 0);
        configuration.mqtt.username.bytes[configuration.mqtt.username.length] = 1;
        REQUIRE(configuration_validate(&configuration) != CONFIGURATION_VALIDATION_OK);
        configuration.mqtt.username.bytes[configuration.mqtt.username.length] = 0;
    }
    REQUIRE(configuration_validate(&configuration) == CONFIGURATION_VALIDATION_OK);
    REQUIRE(configuration_equals(&configuration, &original));
    REQUIRE(configuration_binary_encode(&configuration, output, sizeof(output), &encoded_length)
        == CONFIGURATION_BINARY_CODEC_OK);
    REQUIRE(encoded_length == length && memcmp(input, output, length) == 0);
    memset(output, 0xA5, sizeof(output));
    REQUIRE(configuration_binary_encode(&configuration, output, (uint32_t)length - 1U, &encoded_length)
        == CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL);
    REQUIRE(output[length - 1U] == 0xA5U);
    return 0;
}

typedef struct
{
    uint32_t count;
    const uint8_t *expected;
    uint32_t expected_length;
    int mismatch;
} frame_check_t;

static void check_frame(const management_frame_view_t *frame, void *context)
{
    frame_check_t *check = context;
    uint32_t length = 0;
    management_frame_result_t result = management_frame_encode(frame->message_type,
        frame->transaction_id, frame->payload, frame->payload_length, output, sizeof(output), &length);
    if (result != MANAGEMENT_FRAME_OK || length != check->expected_length ||
        memcmp(check->expected, output, length) != 0)
    {
        check->mismatch = 1;
    }
    check->count++;
}

static int check_management(size_t length)
{
    const uint32_t chunks[] = {1, 2, 3, 7, 13, 64, 255, 256, 511, 4096, 8494};
    uint8_t storage[MANAGEMENT_FRAME_MAX_LENGTH];
    management_frame_parser_t parser;
    frame_check_t check;
    size_t i;
    REQUIRE(MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH == 8477U);
    REQUIRE(MANAGEMENT_FRAME_MAX_LENGTH == 8494U);
    for (i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++)
    {
        uint32_t offset = 0;
        check.count = 0;
        check.expected = input;
        check.expected_length = (uint32_t)length;
        check.mismatch = 0;
        REQUIRE(management_frame_parser_init(&parser, storage, sizeof(storage)) == MANAGEMENT_FRAME_OK);
        while (offset < length)
        {
            uint32_t chunk = (uint32_t)length - offset;
            if (chunk > chunks[i])
            {
                chunk = chunks[i];
            }
            REQUIRE(management_frame_parser_feed(&parser, input + offset, chunk, check_frame, &check)
                == MANAGEMENT_FRAME_OK);
            offset += chunk;
        }
        REQUIRE(check.count == 1 && check.mismatch == 0);
        REQUIRE(parser.length == 0);
    }
    {
        const uint8_t oversized[] = {0x4D, 0x42, 0x47, 0x57, 1, 0, 0, 0, 0, 0x1E, 0x21, 0, 0};
        const uint8_t noise[] = {'n', 'o', 'i', 's', 'e'};
        uint8_t concatenated[18000];
        REQUIRE(management_frame_parser_init(&parser, storage, sizeof(storage)) == MANAGEMENT_FRAME_OK);
        check.count = 0;
        check.mismatch = 0;
        REQUIRE(management_frame_parser_feed(&parser, oversized, sizeof(oversized), check_frame, &check)
            == MANAGEMENT_FRAME_OK);
        REQUIRE(check.count == 0 && parser.length < 13U);
        REQUIRE(management_frame_parser_feed(&parser, noise, sizeof(noise), check_frame, &check)
            == MANAGEMENT_FRAME_OK);
        input[length - 1U] ^= 1U;
        REQUIRE(management_frame_parser_feed(&parser, input, (uint32_t)length, check_frame, &check)
            == MANAGEMENT_FRAME_OK);
        input[length - 1U] ^= 1U;
        REQUIRE(check.count == 0);
        memcpy(concatenated, input, length);
        memcpy(concatenated + length, input, length);
        REQUIRE(management_frame_parser_feed(&parser, concatenated, (uint32_t)length * 2U,
            check_frame, &check) == MANAGEMENT_FRAME_OK);
        REQUIRE(check.count == 2 && check.mismatch == 0 && parser.length == 0);
    }
    return 0;
}

static int check_oversized_header(size_t length)
{
    uint8_t storage[MANAGEMENT_FRAME_MAX_LENGTH];
    management_frame_parser_t parser;
    frame_check_t check = {0, input, (uint32_t)length, 0};
    REQUIRE(length == 13U);
    REQUIRE(management_frame_read_u32_le(input + 9) == 8478U);
    REQUIRE(management_frame_parser_init(&parser, storage, sizeof(storage)) == MANAGEMENT_FRAME_OK);
    REQUIRE(management_frame_parser_feed(&parser, input, (uint32_t)length, check_frame, &check)
        == MANAGEMENT_FRAME_OK);
    REQUIRE(check.count == 0 && parser.length < 13U);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *file;
    size_t length;
    int file_error;
    REQUIRE(argc == 3);
    REQUIRE(CONFIGURATION_SCHEMA_VERSION == 2U);
    REQUIRE(CONFIGURATION_V2_MAX_PAYLOAD_LENGTH == 8475U);
    REQUIRE(CONFIGURATION_V2_DEFAULT_PAYLOAD_LENGTH == 48U);
    REQUIRE(sizeof(configuration.mqtt.broker_address.bytes) == 256U);
    REQUIRE(sizeof(configuration.mqtt.username.bytes) >= 257U);
    file = fopen(argv[1], "rb");
    REQUIRE(file != NULL);
    length = fread(input, 1, sizeof(input), file);
    file_error = ferror(file);
    REQUIRE(fclose(file) == 0);
    REQUIRE(file_error == 0 && length < sizeof(input));
    if (strcmp(argv[2], "frame") == 0)
    {
        return check_management(length);
    }
    if (strcmp(argv[2], "oversized_header") == 0)
    {
        return check_oversized_header(length);
    }
    return check_configuration(length, argv[2]);
}
