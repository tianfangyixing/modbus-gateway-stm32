#include "configuration_service.h"

#include "configuration_binary_codec.h"
#include "external_flash.h"
#include "memory_sections.h"

#include "debug_log.h"

#include "mbedtls/platform_util.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define CONFIGURATION_SLOT_MAGIC UINT32_C(0x57753102)

#define CONFIGURATION_FLASH_SIZE UINT32_C(0x01000000)
#define CONFIGURATION_SECTOR_SIZE UINT32_C(4096)
#define CONFIGURATION_SLOT_SIZE UINT32_C(8192)
#define CONFIGURATION_SLOT_HEADER_SIZE UINT32_C(20)
#define CONFIGURATION_SLOT_MAX_PAYLOAD_LENGTH (CONFIGURATION_SLOT_SIZE - CONFIGURATION_SLOT_HEADER_SIZE)
#define CONFIGURATION_SLOT_A_ADDRESS UINT32_C(0x00FFC000)
#define CONFIGURATION_SLOT_B_ADDRESS UINT32_C(0x00FFE000)

#define CONFIGURATION_SLOT_A UINT8_C(0)
#define CONFIGURATION_SLOT_B UINT8_C(1)

#define CONFIGURATION_GENERATION_FIRST UINT32_C(0)
#define CONFIGURATION_GENERATION_LAST UINT32_C(2)

#if CONFIGURATION_V1_MAX_PAYLOAD_LENGTH > CONFIGURATION_SLOT_MAX_PAYLOAD_LENGTH
#error "Schema v1 payload does not fit in a configuration slot"
#endif

#if CONFIGURATION_SLOT_A_ADDRESS % CONFIGURATION_SECTOR_SIZE != 0U || \
    CONFIGURATION_SLOT_B_ADDRESS % CONFIGURATION_SECTOR_SIZE != 0U
#error "Configuration slots must be sector aligned"
#endif

#if CONFIGURATION_SLOT_A_ADDRESS > CONFIGURATION_FLASH_SIZE - CONFIGURATION_SLOT_SIZE || \
    CONFIGURATION_SLOT_B_ADDRESS > CONFIGURATION_FLASH_SIZE - CONFIGURATION_SLOT_SIZE
#error "Configuration slots exceed the external Flash address range"
#endif

#if !(CONFIGURATION_SLOT_A_ADDRESS + CONFIGURATION_SLOT_SIZE <= CONFIGURATION_SLOT_B_ADDRESS || \
      CONFIGURATION_SLOT_B_ADDRESS + CONFIGURATION_SLOT_SIZE <= CONFIGURATION_SLOT_A_ADDRESS)
#error "Configuration slots overlap"
#endif

typedef enum
{
    CONFIGURATION_SERVICE_UNINITIALIZED = 0,
    CONFIGURATION_SERVICE_READY,
    CONFIGURATION_SERVICE_FAILED
} configuration_service_state_t;

typedef struct
{
    bool valid;
    uint32_t generation;
} configuration_slot_info_t;

static CCM_SRAM configuration_t active_configuration;
static CCM_SRAM configuration_t temporary_configuration;
static uint8_t workspace[CONFIGURATION_SLOT_SIZE];
static configuration_service_state_t service_state;
static bool persisted_configuration_exists;
static uint8_t persisted_slot;
static uint32_t persisted_generation;

static const char *network_mode_name(uint8_t mode)
{
    switch (mode)
    {
        case CONFIGURATION_NETWORK_MODE_DHCP:
            return "dhcp";

        case CONFIGURATION_NETWORK_MODE_STATIC:
            return "static";

        default:
            return "unknown";
    }
}

static const char *frame_format_name(uint8_t frame_format)
{
    switch (frame_format)
    {
        case CONFIGURATION_FRAME_FORMAT_8N1:
            return "8N1";

        case CONFIGURATION_FRAME_FORMAT_8E1:
            return "8E1";

        case CONFIGURATION_FRAME_FORMAT_8O1:
            return "8O1";

        case CONFIGURATION_FRAME_FORMAT_8N2:
            return "8N2";

        default:
            return "unknown";
    }
}

static const char *endpoint_address_type_name(uint8_t type)
{
    switch (type)
    {
        case CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME:
            return "hostname";

        case CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4:
            return "ipv4";

        default:
            return "unknown";
    }
}

static const char *mqtt_mode_name(uint8_t mode)
{
    switch (mode)
    {
        case CONFIGURATION_MQTT_MODE_DISABLED:
            return "disabled";

        case CONFIGURATION_MQTT_MODE_ENABLED:
            return "enabled";

        default:
            return "unknown";
    }
}

static const char *client_id_mode_name(uint8_t mode)
{
    switch (mode)
    {
        case CONFIGURATION_CLIENT_ID_MODE_DERIVED:
            return "derived";

        case CONFIGURATION_CLIENT_ID_MODE_EXPLICIT:
            return "explicit";

        default:
            return "unknown";
    }
}

static const char *mqtt_message_mode_name(uint8_t mode)
{
    switch (mode)
    {
        case CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED:
            return "disabled";

        case CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM:
            return "custom";

        default:
            return "unknown";
    }
}

static const char *collection_source_name(uint8_t source)
{
    switch (source)
    {
        case CONFIGURATION_COLLECTION_SOURCE_COIL:
            return "coil";

        case CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT:
            return "discrete_input";

        case CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER:
            return "holding_register";

        case CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER:
            return "input_register";

        default:
            return "unknown";
    }
}

static const char *data_type_name(uint8_t data_type)
{
    switch (data_type)
    {
        case CONFIGURATION_DATA_TYPE_UINT16:
            return "uint16";

        case CONFIGURATION_DATA_TYPE_INT16:
            return "int16";

        default:
            return "unknown";
    }
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t byte_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        uint8_t bit_index;

        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & UINT32_C(1)) != 0U)
            {
                crc = (crc >> 1U) ^ UINT32_C(0xEDB88320);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint32_t slot_address(uint8_t slot_index)
{
    return slot_index == CONFIGURATION_SLOT_A ? CONFIGURATION_SLOT_A_ADDRESS : CONFIGURATION_SLOT_B_ADDRESS;
}

static uint8_t other_slot(uint8_t slot_index)
{
    return slot_index == CONFIGURATION_SLOT_A ? CONFIGURATION_SLOT_B : CONFIGURATION_SLOT_A;
}

static uint32_t next_generation(uint32_t generation)
{
    return generation == CONFIGURATION_GENERATION_LAST ? CONFIGURATION_GENERATION_FIRST : generation + 1U;
}

static bool generation_is_valid(uint32_t generation)
{
    return generation <= CONFIGURATION_GENERATION_LAST;
}

static bool generation_is_newer(uint32_t candidate, uint32_t reference)
{
    return candidate == next_generation(reference);
}

static configuration_service_result_t read_slot(uint8_t slot_index, configuration_t *configuration,
                                                 configuration_slot_info_t *slot_info)
{
    configuration_binary_codec_result_t codec_result;
    uint32_t header_crc;
    uint32_t payload_crc;
    uint32_t payload_length;

    slot_info->valid = false;
    slot_info->generation = CONFIGURATION_GENERATION_FIRST;

    if (external_flash_read(slot_address(slot_index), workspace, CONFIGURATION_SLOT_SIZE) != EXTERNAL_FLASH_RESULT_OK)
    {
        return CONFIGURATION_SERVICE_IO_ERROR;
    }

    slot_info->generation = read_u32_le(&workspace[4]);
    payload_length = read_u32_le(&workspace[8]);
    payload_crc = read_u32_le(&workspace[12]);
    header_crc = read_u32_le(&workspace[16]);

    if (read_u32_le(&workspace[0]) != CONFIGURATION_SLOT_MAGIC ||
        !generation_is_valid(slot_info->generation) || payload_length == 0U ||
        payload_length > CONFIGURATION_SLOT_MAX_PAYLOAD_LENGTH || crc32(workspace, 16U) != header_crc ||
        crc32(&workspace[CONFIGURATION_SLOT_HEADER_SIZE], payload_length) != payload_crc)
    {
        return CONFIGURATION_SERVICE_OK;
    }

    codec_result = configuration_binary_decode(&workspace[CONFIGURATION_SLOT_HEADER_SIZE], payload_length,
                                               configuration);
    if (codec_result == CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE)
    {
        return CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE;
    }

    slot_info->valid = codec_result == CONFIGURATION_BINARY_CODEC_OK;
    return CONFIGURATION_SERVICE_OK;
}

static configuration_service_result_t load_configuration(void)
{
    configuration_service_result_t result;
    configuration_slot_info_t slot_a;
    configuration_slot_info_t slot_b;

    persisted_configuration_exists = false;

    result = read_slot(CONFIGURATION_SLOT_A, &temporary_configuration, &slot_a);
    if (result != CONFIGURATION_SERVICE_OK)
    {
        return result;
    }
    if (slot_a.valid)
    {
        active_configuration = temporary_configuration;
        persisted_configuration_exists = true;
        persisted_slot = CONFIGURATION_SLOT_A;
        persisted_generation = slot_a.generation;
    }

    result = read_slot(CONFIGURATION_SLOT_B, &temporary_configuration, &slot_b);
    if (result != CONFIGURATION_SERVICE_OK)
    {
        return result;
    }

    if (slot_b.valid && (!slot_a.valid || slot_b.generation == slot_a.generation ||
                         generation_is_newer(slot_b.generation, slot_a.generation)))
    {
        active_configuration = temporary_configuration;
        persisted_configuration_exists = true;
        persisted_slot = CONFIGURATION_SLOT_B;
        persisted_generation = slot_b.generation;
    }

    if (!persisted_configuration_exists)
    {
        configuration_set_defaults(&active_configuration);
    }

    return CONFIGURATION_SERVICE_OK;
}

static configuration_service_result_t write_slot(uint8_t slot_index, uint32_t generation, const uint8_t *payload,
                                                  uint32_t payload_length)
{
    uint8_t expected_header[CONFIGURATION_SLOT_HEADER_SIZE];
    uint32_t address = slot_address(slot_index);

    for (uint8_t sector_index = 0U; sector_index < 2U; sector_index++)
    {
        if (external_flash_erase_4k(address + (uint32_t)sector_index * CONFIGURATION_SECTOR_SIZE) !=
            EXTERNAL_FLASH_RESULT_OK)
        {
            return CONFIGURATION_SERVICE_IO_ERROR;
        }
    }

    write_u32_le(&expected_header[0], CONFIGURATION_SLOT_MAGIC);
    write_u32_le(&expected_header[4], generation);
    write_u32_le(&expected_header[8], payload_length);
    write_u32_le(&expected_header[12], crc32(payload, payload_length));
    write_u32_le(&expected_header[16], crc32(expected_header, 16U));
    memcpy(workspace, expected_header, CONFIGURATION_SLOT_HEADER_SIZE);
    memcpy(&workspace[CONFIGURATION_SLOT_HEADER_SIZE], payload, payload_length);

    if (external_flash_program(address + CONFIGURATION_SLOT_HEADER_SIZE, &workspace[CONFIGURATION_SLOT_HEADER_SIZE], payload_length) !=
        EXTERNAL_FLASH_RESULT_OK)
    {
        return CONFIGURATION_SERVICE_IO_ERROR;
    }

    if (external_flash_program(address + 4U, &workspace[4], CONFIGURATION_SLOT_HEADER_SIZE - 4U) !=
        EXTERNAL_FLASH_RESULT_OK)
    {
        return CONFIGURATION_SERVICE_IO_ERROR;
    }

    if (external_flash_program(address, workspace, 4U) != EXTERNAL_FLASH_RESULT_OK)
    {
        return CONFIGURATION_SERVICE_IO_ERROR;
    }

    if (external_flash_read(address, workspace, CONFIGURATION_SLOT_SIZE) != EXTERNAL_FLASH_RESULT_OK)
    {
        return CONFIGURATION_SERVICE_IO_ERROR;
    }

    if (memcmp(workspace, expected_header, CONFIGURATION_SLOT_HEADER_SIZE) != 0 ||
        memcmp(&workspace[CONFIGURATION_SLOT_HEADER_SIZE], payload, payload_length) != 0)
    {
        return CONFIGURATION_SERVICE_IO_ERROR;
    }

    return CONFIGURATION_SERVICE_OK;
}

configuration_service_result_t configuration_service_init(void)
{
    configuration_service_result_t result;

    if (service_state != CONFIGURATION_SERVICE_UNINITIALIZED)
    {
        return service_state == CONFIGURATION_SERVICE_READY ? CONFIGURATION_SERVICE_OK
                                                            : CONFIGURATION_SERVICE_NOT_INITIALIZED;
    }

    result = load_configuration();
    service_state = result == CONFIGURATION_SERVICE_OK ? CONFIGURATION_SERVICE_READY : CONFIGURATION_SERVICE_FAILED;
    if (result == CONFIGURATION_SERVICE_OK)
    {
        const configuration_t *configuration = &active_configuration;
        uint8_t index;

        debug_log_printf("\r\n[configuration] begin\r\n");
        debug_log_printf("[configuration][network] mode=%s\r\n", network_mode_name(configuration->network.mode));
        if (configuration->network.mode == CONFIGURATION_NETWORK_MODE_STATIC)
        {
            debug_log_printf("[configuration][network] ip_address=%u.%u.%u.%u\r\n",
                              (unsigned int)ip4_addr1(&configuration->network.ip_address),
                              (unsigned int)ip4_addr2(&configuration->network.ip_address),
                              (unsigned int)ip4_addr3(&configuration->network.ip_address),
                              (unsigned int)ip4_addr4(&configuration->network.ip_address));
            debug_log_printf("[configuration][network] subnet_mask=%u.%u.%u.%u\r\n",
                              (unsigned int)ip4_addr1(&configuration->network.subnet_mask),
                              (unsigned int)ip4_addr2(&configuration->network.subnet_mask),
                              (unsigned int)ip4_addr3(&configuration->network.subnet_mask),
                              (unsigned int)ip4_addr4(&configuration->network.subnet_mask));
            debug_log_printf("[configuration][network] gateway=%u.%u.%u.%u\r\n",
                              (unsigned int)ip4_addr1(&configuration->network.gateway),
                              (unsigned int)ip4_addr2(&configuration->network.gateway),
                              (unsigned int)ip4_addr3(&configuration->network.gateway),
                              (unsigned int)ip4_addr4(&configuration->network.gateway));
            debug_log_printf("[configuration][network] dns_primary=%u.%u.%u.%u\r\n",
                              (unsigned int)ip4_addr1(&configuration->network.dns_primary),
                              (unsigned int)ip4_addr2(&configuration->network.dns_primary),
                              (unsigned int)ip4_addr3(&configuration->network.dns_primary),
                              (unsigned int)ip4_addr4(&configuration->network.dns_primary));
            debug_log_printf("[configuration][network] dns_secondary=%u.%u.%u.%u\r\n",
                              (unsigned int)ip4_addr1(&configuration->network.dns_secondary),
                              (unsigned int)ip4_addr2(&configuration->network.dns_secondary),
                              (unsigned int)ip4_addr3(&configuration->network.dns_secondary),
                              (unsigned int)ip4_addr4(&configuration->network.dns_secondary));
        }

        debug_log_printf("[configuration][rtu] baud_rate=%u frame_format=%s first_byte_timeout_ms=%u\r\n",
                          (unsigned int)configuration->rtu.baud_rate,
                          frame_format_name(configuration->rtu.frame_format),
                          (unsigned int)configuration->rtu.first_byte_timeout_ms);
        debug_log_printf("[configuration][modbus_tcp] listen_port=%u\r\n",
                          (unsigned int)configuration->modbus_tcp.listen_port);

        for (index = 0U; index < 2U; index++)
        {
            const configuration_endpoint_address_t *server = &configuration->sntp.servers[index];

            debug_log_printf("[configuration][sntp][%u] type=%s\r\n", (unsigned int)index,
                              endpoint_address_type_name(server->type));
            if (server->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
            {
                debug_log_printf("[configuration][sntp][%u] hostname=%.*s\r\n", (unsigned int)index,
                                  (int)server->value.hostname.length,
                                  (const char *)server->value.hostname.bytes);
            }
            else if (server->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4)
            {
                debug_log_printf("[configuration][sntp][%u] ipv4=%u.%u.%u.%u\r\n", (unsigned int)index,
                                  (unsigned int)ip4_addr1(&server->value.ipv4),
                                  (unsigned int)ip4_addr2(&server->value.ipv4),
                                  (unsigned int)ip4_addr3(&server->value.ipv4),
                                  (unsigned int)ip4_addr4(&server->value.ipv4));
            }
        }

        debug_log_printf("[configuration][mqtt] mode=%s\r\n", mqtt_mode_name(configuration->mqtt.mode));
        if (configuration->mqtt.mode == CONFIGURATION_MQTT_MODE_ENABLED)
        {
            debug_log_printf("[configuration][mqtt] broker_address=%.*s broker_port=%u\r\n",
                              (int)configuration->mqtt.broker_address.length,
                              (const char *)configuration->mqtt.broker_address.bytes,
                              (unsigned int)configuration->mqtt.broker_port);
            debug_log_printf("[configuration][mqtt] client_id_mode=%s\r\n",
                              client_id_mode_name(configuration->mqtt.client_id.mode));
            if (configuration->mqtt.client_id.mode == CONFIGURATION_CLIENT_ID_MODE_EXPLICIT)
            {
                debug_log_printf("[configuration][mqtt] client_id=%.*s\r\n",
                                  (int)configuration->mqtt.client_id.explicit_value.length,
                                  (const char *)configuration->mqtt.client_id.explicit_value.bytes);
            }
            debug_log_printf("[configuration][mqtt] username=%.*s\r\n",
                              (int)configuration->mqtt.username.length,
                              (const char *)configuration->mqtt.username.bytes);
            debug_log_printf("[configuration][mqtt] password=<redacted> length=%u\r\n",
                              (unsigned int)configuration->mqtt.password.length);
            debug_log_printf("[configuration][mqtt] keep_alive_seconds=%u\r\n",
                              (unsigned int)configuration->mqtt.keep_alive_seconds);

            debug_log_printf("[configuration][mqtt][online_message] mode=%s\r\n",
                              mqtt_message_mode_name(configuration->mqtt.online_message.mode));
            if (configuration->mqtt.online_message.mode == CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM)
            {
                debug_log_printf("[configuration][mqtt][online_message] topic=%.*s\r\n",
                                  (int)configuration->mqtt.online_message.topic.length,
                                  (const char *)configuration->mqtt.online_message.topic.bytes);
                debug_log_printf("[configuration][mqtt][online_message] payload=%.*s\r\n",
                                  (int)configuration->mqtt.online_message.payload.length,
                                  (const char *)configuration->mqtt.online_message.payload.bytes);
                debug_log_printf("[configuration][mqtt][online_message] qos=%u retain=%s\r\n",
                                  (unsigned int)configuration->mqtt.online_message.qos,
                                  configuration->mqtt.online_message.retain != 0U ? "true" : "false");
            }

            debug_log_printf("[configuration][mqtt][will_message] mode=%s\r\n",
                              mqtt_message_mode_name(configuration->mqtt.will_message.mode));
            if (configuration->mqtt.will_message.mode == CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM)
            {
                debug_log_printf("[configuration][mqtt][will_message] topic=%.*s\r\n",
                                  (int)configuration->mqtt.will_message.topic.length,
                                  (const char *)configuration->mqtt.will_message.topic.bytes);
                debug_log_printf("[configuration][mqtt][will_message] payload=%.*s\r\n",
                                  (int)configuration->mqtt.will_message.payload.length,
                                  (const char *)configuration->mqtt.will_message.payload.bytes);
                debug_log_printf("[configuration][mqtt][will_message] qos=%u retain=%s\r\n",
                                  (unsigned int)configuration->mqtt.will_message.qos,
                                  configuration->mqtt.will_message.retain != 0U ? "true" : "false");
            }
        }

        debug_log_printf("[configuration][collection] point_count=%u\r\n",
                          (unsigned int)configuration->collection.point_count);
        for (index = 0U; index < configuration->collection.point_count; index++)
        {
            const configuration_collection_point_t *point = &configuration->collection.points[index];

            debug_log_printf("[configuration][collection][%u] slave_address=%u source=%s address=%u\r\n",
                              (unsigned int)index, (unsigned int)point->slave_address,
                              collection_source_name(point->source), (unsigned int)point->address);
            if (point->source == CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER ||
                point->source == CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER)
            {
                debug_log_printf("[configuration][collection][%u] data_type=%s\r\n", (unsigned int)index,
                                  data_type_name(point->data_type));
            }
            debug_log_printf(
                              "[configuration][collection][%u] poll_interval_ms=%u first_byte_timeout_ms=%u qos=%u\r\n",
                              (unsigned int)index, (unsigned int)point->poll_interval_ms,
                              (unsigned int)point->first_byte_timeout_ms, (unsigned int)point->qos);
            debug_log_printf("[configuration][collection][%u] topic=%.*s\r\n", (unsigned int)index,
                              (int)point->topic.length, (const char *)point->topic.bytes);
        }
        debug_log_printf("[configuration] end\r\n");
    }
    return result;
}

const configuration_t *configuration_service_active(void)
{
    return service_state == CONFIGURATION_SERVICE_READY ? &active_configuration : NULL;
}

configuration_service_result_t configuration_service_write(const uint8_t *payload, uint32_t payload_length)
{
    configuration_binary_codec_result_t codec_result;
    configuration_service_result_t result;
    uint32_t generation;
    uint8_t target_slot;

    if (service_state != CONFIGURATION_SERVICE_READY)
    {
        return CONFIGURATION_SERVICE_NOT_INITIALIZED;
    }
    if (payload == NULL || payload_length == 0U || payload_length > CONFIGURATION_V1_MAX_PAYLOAD_LENGTH)
    {
        return CONFIGURATION_SERVICE_INVALID_ARGUMENT;
    }

    codec_result = configuration_binary_decode(payload, payload_length, &temporary_configuration);
    if (codec_result == CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE)
    {
        return CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE;
    }
    if (codec_result != CONFIGURATION_BINARY_CODEC_OK)
    {
        return CONFIGURATION_SERVICE_INVALID_PAYLOAD;
    }

    target_slot = persisted_configuration_exists ? other_slot(persisted_slot) : CONFIGURATION_SLOT_A;
    generation = persisted_configuration_exists ? next_generation(persisted_generation)
                                                 : CONFIGURATION_GENERATION_FIRST;
    result = write_slot(target_slot, generation, payload, payload_length);
    if (result == CONFIGURATION_SERVICE_OK)
    {
        persisted_configuration_exists = true;
        persisted_slot = target_slot;
        persisted_generation = generation;
    }

    return result;
}

#if defined(CONFIGURATION_SERVICE_TEST)
void configuration_service_test_reset(void)
{
    mbedtls_platform_zeroize(&active_configuration, sizeof(active_configuration));
    mbedtls_platform_zeroize(&temporary_configuration, sizeof(temporary_configuration));
    mbedtls_platform_zeroize(workspace, sizeof(workspace));
    service_state = CONFIGURATION_SERVICE_UNINITIALIZED;
    persisted_configuration_exists = false;
    persisted_slot = CONFIGURATION_SLOT_A;
    persisted_generation = CONFIGURATION_GENERATION_FIRST;
}
#endif
