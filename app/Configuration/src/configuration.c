#include "configuration.h"

#include "mbedtls/asn1.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pem.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"

#include <stddef.h>
#include <string.h>

#define CONFIGURATION_MICROSECONDS_PER_SECOND UINT32_C(1000000)
#define CONFIGURATION_MILLISECONDS_PER_SECOND UINT32_C(1000)
#define CONFIGURATION_RTU_T35_BAUD_RATE_THRESHOLD UINT32_C(19200)
#define CONFIGURATION_RTU_T35_FIXED_US UINT32_C(1750)
#define CONFIGURATION_RTU_T35_CHARACTER_US_FACTOR UINT32_C(3500000)
#define CONFIGURATION_RTU_REQUEST_CHARACTER_COUNT UINT32_C(8)
#define CONFIGURATION_RTU_BIT_RESPONSE_CHARACTER_COUNT UINT32_C(6)
#define CONFIGURATION_RTU_REGISTER_RESPONSE_CHARACTER_COUNT UINT32_C(7)
#define CONFIGURATION_BUS_UTILIZATION_LIMIT_US_PER_SECOND UINT32_C(500000)

static const uint8_t default_sntp_server_0[] = "ntp.aliyun.com";
static const uint8_t default_sntp_server_1[] = "ntp.tencent.com";


static uint32_t ipv4_to_u32(const ip4_addr_t *address)
{
    return lwip_ntohl(ip4_addr_get_u32(address));
}

static bool ipv4_is_nonzero_unicast(const ip4_addr_t *address)
{
    uint32_t value = ipv4_to_u32(address);
    uint8_t first_octet = ip4_addr1(address);

    return value != 0U && first_octet != 0U && first_octet < 224U && first_octet != 127U &&
           !(first_octet == 169U && ip4_addr2(address) == 254U);
}

static bool subnet_mask_is_valid(const ip4_addr_t *subnet_mask)
{
    uint32_t value = ipv4_to_u32(subnet_mask);
    uint32_t host_mask = ~value;

    return value != 0U && host_mask > UINT32_C(1) && ip_addr_netmask_valid(subnet_mask);
}

static bool address_is_usable_in_subnet(const ip4_addr_t *address, const ip4_addr_t *subnet_mask)
{
    uint32_t host_mask = ~ipv4_to_u32(subnet_mask);
    uint32_t host_part = ipv4_to_u32(address) & host_mask;

    if (host_mask <= UINT32_C(1))
    {
        return false;
    }

    return host_part != 0U && host_part != host_mask;
}

static bool ascii_is_alphanumeric(uint8_t value)
{
    return (value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
           (value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
           (value >= (uint8_t)'0' && value <= (uint8_t)'9');
}

static uint8_t ascii_to_lower(uint8_t value)
{
    if (value >= (uint8_t)'A' && value <= (uint8_t)'Z')
    {
        return (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'));
    }

    return value;
}

static bool hostname_is_valid(const configuration_hostname_t *hostname)
{
    bool has_alphabetic = false;
    uint16_t label_start = 0U;
    uint16_t final_label_length;
    uint16_t index;

    if (hostname->length == 0U || hostname->length > CONFIGURATION_HOSTNAME_MAX_LENGTH ||
        hostname->bytes[hostname->length] != 0U)
    {
        return false;
    }

    for (index = 0U; index < hostname->length; index++)
    {
        uint8_t value = hostname->bytes[index];

        if (value == (uint8_t)'.')
        {
            uint16_t label_length = (uint16_t)(index - label_start);

            if (label_length == 0U || label_length > 63U || hostname->bytes[label_start] == (uint8_t)'-' ||
                hostname->bytes[index - 1U] == (uint8_t)'-')
            {
                return false;
            }
            label_start = (uint16_t)(index + 1U);
        }
        else if (!ascii_is_alphanumeric(value) && value != (uint8_t)'-')
        {
            return false;
        }
        else if ((value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
                 (value >= (uint8_t)'a' && value <= (uint8_t)'z'))
        {
            has_alphabetic = true;
        }
    }

    final_label_length = (uint16_t)(hostname->length - label_start);
    return has_alphabetic && final_label_length > 0U && final_label_length <= 63U &&
           hostname->bytes[label_start] != (uint8_t)'-' && hostname->bytes[hostname->length - 1U] != (uint8_t)'-';
}

static bool hostnames_equal(const configuration_hostname_t *left, const configuration_hostname_t *right)
{
    uint16_t index;

    if (left->length != right->length)
    {
        return false;
    }

    for (index = 0U; index < left->length; index++)
    {
        if (ascii_to_lower(left->bytes[index]) != ascii_to_lower(right->bytes[index]))
        {
            return false;
        }
    }

    return true;
}

static bool endpoint_addresses_equal(const configuration_endpoint_address_t *left,
                                     const configuration_endpoint_address_t *right)
{
    if (left->type != right->type)
    {
        return false;
    }

    if (left->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
    {
        return hostnames_equal(&left->value.hostname, &right->value.hostname);
    }

    return ip4_addr_cmp(&left->value.ipv4, &right->value.ipv4);
}

static bool endpoint_address_is_valid(const configuration_endpoint_address_t *endpoint)
{
    if (endpoint->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME)
    {
        return hostname_is_valid(&endpoint->value.hostname);
    }

    if (endpoint->type == CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4)
    {
        return ipv4_is_nonzero_unicast(&endpoint->value.ipv4);
    }

    return false;
}

static bool utf8_is_valid(const uint8_t *bytes, uint16_t length, bool allow_nul)
{
    uint16_t index = 0U;

    while (index < length)
    {
        uint8_t first = bytes[index];
        uint8_t continuation_count;
        uint32_t code_point;
        uint32_t minimum_code_point;
        uint8_t continuation_index;

        if (first <= UINT8_C(0x7F))
        {
            if (!allow_nul && first == 0U)
            {
                return false;
            }
            index++;
            continue;
        }

        if (first >= UINT8_C(0xC2) && first <= UINT8_C(0xDF))
        {
            continuation_count = 1U;
            code_point = first & UINT8_C(0x1F);
            minimum_code_point = UINT32_C(0x80);
        }
        else if (first >= UINT8_C(0xE0) && first <= UINT8_C(0xEF))
        {
            continuation_count = 2U;
            code_point = first & UINT8_C(0x0F);
            minimum_code_point = UINT32_C(0x800);
        }
        else if (first >= UINT8_C(0xF0) && first <= UINT8_C(0xF4))
        {
            continuation_count = 3U;
            code_point = first & UINT8_C(0x07);
            minimum_code_point = UINT32_C(0x10000);
        }
        else
        {
            return false;
        }

        if ((uint32_t)index + continuation_count >= length)
        {
            return false;
        }

        for (continuation_index = 0U; continuation_index < continuation_count; continuation_index++)
        {
            uint8_t continuation = bytes[index + continuation_index + 1U];

            if ((continuation & UINT8_C(0xC0)) != UINT8_C(0x80))
            {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & UINT8_C(0x3F));
        }

        if (code_point < minimum_code_point || code_point > UINT32_C(0x10FFFF) ||
            (code_point >= UINT32_C(0xD800) && code_point <= UINT32_C(0xDFFF)))
        {
            return false;
        }
        index = (uint16_t)(index + continuation_count + 1U);
    }

    return true;
}

static bool visible_ascii_is_valid(const uint8_t *bytes, uint16_t length, uint16_t minimum, uint16_t maximum)
{
    uint16_t index;

    if (length < minimum || length > maximum || bytes[length] != 0U)
    {
        return false;
    }

    for (index = 0U; index < length; index++)
    {
        if (bytes[index] < UINT8_C(0x20) || bytes[index] > UINT8_C(0x7E))
        {
            return false;
        }
    }

    return true;
}

static bool topic_is_valid(const configuration_topic_t *topic)
{
    uint16_t index;

    if (topic->length == 0U || topic->length > CONFIGURATION_TOPIC_MAX_LENGTH || topic->bytes[topic->length] != 0U)
    {
        return false;
    }

    for (index = 0U; index < topic->length; index++)
    {
        uint8_t value = topic->bytes[index];

        if (!ascii_is_alphanumeric(value) && value != (uint8_t)'.' && value != (uint8_t)'_' &&
            value != (uint8_t)'-' && value != (uint8_t)'/')
        {
            return false;
        }
    }

    return true;
}

static int topic_compare(const configuration_topic_t *left, const configuration_topic_t *right)
{
    uint16_t common_length = left->length < right->length ? left->length : right->length;
    int comparison = memcmp(left->bytes, right->bytes, common_length);

    if (comparison != 0)
    {
        return comparison;
    }
    if (left->length < right->length)
    {
        return -1;
    }
    if (left->length > right->length)
    {
        return 1;
    }
    return 0;
}

static bool client_id_is_valid(const configuration_client_id_t *client_id)
{
    uint16_t index;

    if (client_id->mode == CONFIGURATION_CLIENT_ID_MODE_DERIVED)
    {
        return true;
    }

    if (client_id->mode != CONFIGURATION_CLIENT_ID_MODE_EXPLICIT || client_id->explicit_value.length == 0U ||
        client_id->explicit_value.length > CONFIGURATION_CLIENT_ID_MAX_LENGTH ||
        client_id->explicit_value.bytes[client_id->explicit_value.length] != 0U)
    {
        return false;
    }

    for (index = 0U; index < client_id->explicit_value.length; index++)
    {
        if (!ascii_is_alphanumeric(client_id->explicit_value.bytes[index]))
        {
            return false;
        }
    }

    return true;
}

static bool mqtt_message_is_valid(const configuration_mqtt_message_t *message)
{
    if (message->mode == CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED)
    {
        return true;
    }

    if (message->mode != CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM || !topic_is_valid(&message->topic) ||
        message->payload.length > CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH ||
        message->payload.length < 1 ||
        message->payload.bytes[message->payload.length] != 0U ||
        !utf8_is_valid(message->payload.bytes, message->payload.length, false) || message->qos > 2U ||
        message->retain > 1U)
    {
        return false;
    }

    return true;
}

static bool mbedtls_result_is_allocation_failure(int result)
{
    uint32_t magnitude;
    uint32_t high_level;
    uint32_t low_level;

    if (result >= 0)
    {
        return false;
    }

    magnitude = (uint32_t)(-result);
    high_level = magnitude & UINT32_C(0xFF80);
    low_level = magnitude & UINT32_C(0x007F);

    return high_level == (uint32_t)(-MBEDTLS_ERR_ECP_ALLOC_FAILED) ||
           high_level == (uint32_t)(-MBEDTLS_ERR_MD_ALLOC_FAILED) ||
           high_level == (uint32_t)(-MBEDTLS_ERR_PEM_ALLOC_FAILED) ||
           high_level == (uint32_t)(-MBEDTLS_ERR_PK_ALLOC_FAILED) ||
           high_level == (uint32_t)(-MBEDTLS_ERR_X509_ALLOC_FAILED) ||
           low_level == (uint32_t)(-MBEDTLS_ERR_ASN1_ALLOC_FAILED) ||
           low_level == (uint32_t)(-MBEDTLS_ERR_MPI_ALLOC_FAILED);
}

static bool x509_names_are_identical(const mbedtls_x509_buf *left, const mbedtls_x509_buf *right)
{
    return left->len > 0U && left->len == right->len && memcmp(left->p, right->p, left->len) == 0;
}

static configuration_validation_result_t x509_self_signature_is_valid(mbedtls_x509_crt *certificate)
{
    const mbedtls_md_info_t *digest_info;
    unsigned char digest[MBEDTLS_MD_MAX_SIZE];
    int digest_result;
    int verify_result;

    digest_info = mbedtls_md_info_from_type(certificate->sig_md);
    if (digest_info == NULL || !mbedtls_pk_can_do(&certificate->pk, certificate->sig_pk))
    {
        return CONFIGURATION_VALIDATION_CERTIFICATE_INVALID;
    }

    digest_result = mbedtls_md(digest_info, certificate->tbs.p, certificate->tbs.len, digest);
    if (mbedtls_result_is_allocation_failure(digest_result))
    {
        return CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE;
    }
    if (digest_result != 0)
    {
        return CONFIGURATION_VALIDATION_CERTIFICATE_INVALID;
    }

    verify_result = mbedtls_pk_verify_ext(certificate->sig_pk, certificate->sig_opts, &certificate->pk,
                                          certificate->sig_md, digest, mbedtls_md_get_size(digest_info),
                                          certificate->sig.p, certificate->sig.len);
    if (mbedtls_result_is_allocation_failure(verify_result))
    {
        return CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE;
    }

    return verify_result == 0 ? CONFIGURATION_VALIDATION_OK : CONFIGURATION_VALIDATION_CERTIFICATE_INVALID;
}

static configuration_validation_result_t ca_certificate_is_valid(const configuration_ca_certificate_t *certificate)
{
    mbedtls_x509_crt parsed_certificate;
    configuration_validation_result_t signature_result;
    int parse_result;

    if (certificate->length == 0U || certificate->length > CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH ||
        certificate->bytes[certificate->length] != 0U ||
        !utf8_is_valid(certificate->bytes, certificate->length, false))
    {
        return CONFIGURATION_VALIDATION_CERTIFICATE_INVALID;
    }

    mbedtls_x509_crt_init(&parsed_certificate);
    parse_result = mbedtls_x509_crt_parse(&parsed_certificate, certificate->bytes, certificate->length + 1U);
    if (mbedtls_result_is_allocation_failure(parse_result))
    {
        mbedtls_x509_crt_free(&parsed_certificate);
        return CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE;
    }

    if (parse_result != 0 || parsed_certificate.next != NULL || parsed_certificate.ca_istrue == 0 ||
        mbedtls_x509_crt_check_key_usage(&parsed_certificate, MBEDTLS_X509_KU_KEY_CERT_SIGN) != 0 ||
        !x509_names_are_identical(&parsed_certificate.subject_raw, &parsed_certificate.issuer_raw))
    {
        mbedtls_x509_crt_free(&parsed_certificate);
        return CONFIGURATION_VALIDATION_CERTIFICATE_INVALID;
    }

    signature_result = x509_self_signature_is_valid(&parsed_certificate);
    mbedtls_x509_crt_free(&parsed_certificate);

    return signature_result;
}

static bool baud_rate_is_valid(uint32_t baud_rate)
{
    switch (baud_rate)
    {
        case UINT32_C(1200):
        case UINT32_C(2400):
        case UINT32_C(4800):
        case UINT32_C(9600):
        case UINT32_C(19200):
        case UINT32_C(38400):
        case UINT32_C(57600):
        case UINT32_C(115200):
            return true;

        default:
            return false;
    }
}

static bool frame_format_is_valid(uint8_t frame_format)
{
    switch(frame_format)
    {
        case CONFIGURATION_FRAME_FORMAT_8N2:
        case CONFIGURATION_FRAME_FORMAT_8O1:
        case CONFIGURATION_FRAME_FORMAT_8E1:
        case CONFIGURATION_FRAME_FORMAT_8N1:
            return true;
        default:
            return false;
    }
}

static uint32_t divide_round_up_u64(uint64_t numerator, uint32_t denominator)
{
    return (uint32_t)((numerator + denominator - UINT32_C(1)) / denominator);
}

static uint32_t rtu_t35_us(uint32_t baud_rate, uint8_t bits_per_character)
{
    if (baud_rate > CONFIGURATION_RTU_T35_BAUD_RATE_THRESHOLD)
    {
        return CONFIGURATION_RTU_T35_FIXED_US;
    }

    return divide_round_up_u64((uint64_t)bits_per_character * CONFIGURATION_RTU_T35_CHARACTER_US_FACTOR,
                               baud_rate);
}

static bool collection_point_is_valid(const configuration_collection_point_t *point)
{
    if (point->slave_address == 0U || point->slave_address > 247U ||
        point->poll_interval_ms < UINT32_C(1000) || point->poll_interval_ms > UINT32_C(3600000) ||
        point->first_byte_timeout_ms < UINT16_C(50) || point->first_byte_timeout_ms > UINT16_C(3000) ||
        !topic_is_valid(&point->topic) || point->qos > 1U)
    {
        return false;
    }

    if (point->source == CONFIGURATION_COLLECTION_SOURCE_COIL ||
        point->source == CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT)
    {
        return true;
    }

    if (point->source != CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER &&
        point->source != CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER)
    {
        return false;
    }

    return point->data_type == CONFIGURATION_DATA_TYPE_UINT16 ||
           point->data_type == CONFIGURATION_DATA_TYPE_INT16;
}

static bool rtu_collection_bus_utilization_is_within_limit(const configuration_t *configuration)
{
    uint8_t bits_per_character = configuration->rtu.frame_format == CONFIGURATION_FRAME_FORMAT_8N1 ? 10U : 11U;
    uint32_t t35_us = rtu_t35_us(configuration->rtu.baud_rate, bits_per_character);
    uint32_t utilization_us_per_second = 0U;

    for (uint8_t index = 0U; index < configuration->collection.point_count; index++)
    {
        const configuration_collection_point_t *point = &configuration->collection.points[index];
        uint32_t character_count = 0;

        switch(point->source)
        {
            case CONFIGURATION_COLLECTION_SOURCE_COIL:
            case CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT:
                character_count = CONFIGURATION_RTU_REQUEST_CHARACTER_COUNT +
                                  CONFIGURATION_RTU_BIT_RESPONSE_CHARACTER_COUNT;
                break;
            case CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER:
            case CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER:
                character_count = CONFIGURATION_RTU_REQUEST_CHARACTER_COUNT +
                                  CONFIGURATION_RTU_REGISTER_RESPONSE_CHARACTER_COUNT;
                break;
        }

        uint32_t character_time_us = divide_round_up_u64(
            (uint64_t)character_count * bits_per_character * CONFIGURATION_MICROSECONDS_PER_SECOND,
            configuration->rtu.baud_rate);
        uint32_t transaction_time_us = t35_us + t35_us + character_time_us;
        uint32_t contribution_us_per_second = divide_round_up_u64(
            (uint64_t)transaction_time_us * CONFIGURATION_MILLISECONDS_PER_SECOND, point->poll_interval_ms);

        if (contribution_us_per_second >
            CONFIGURATION_BUS_UTILIZATION_LIMIT_US_PER_SECOND - utilization_us_per_second)
        {
            return false;
        }
        utilization_us_per_second += contribution_us_per_second;
    }

    return true;
}

void configuration_set_defaults(configuration_t *configuration)
{
    if (configuration == NULL)
    {
        return;
    }

    memset(configuration, 0, sizeof(*configuration));
    configuration->network.mode = CONFIGURATION_NETWORK_MODE_DHCP;
    configuration->rtu.baud_rate = UINT32_C(9600);
    configuration->rtu.frame_format = CONFIGURATION_FRAME_FORMAT_8N2;
    configuration->rtu.first_byte_timeout_ms = UINT16_C(1000);
    configuration->modbus_tcp.listen_port = UINT16_C(502);
    configuration->sntp.servers[0].type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
    configuration->sntp.servers[0].value.hostname.length = (uint16_t)(sizeof(default_sntp_server_0) - 1U);
    memcpy(configuration->sntp.servers[0].value.hostname.bytes, default_sntp_server_0,
           sizeof(default_sntp_server_0));
    configuration->sntp.servers[1].type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
    configuration->sntp.servers[1].value.hostname.length = (uint16_t)(sizeof(default_sntp_server_1) - 1U);
    memcpy(configuration->sntp.servers[1].value.hostname.bytes, default_sntp_server_1,
           sizeof(default_sntp_server_1));
    configuration->mqtt.mode = CONFIGURATION_MQTT_MODE_DISABLED;
    configuration->collection.point_count = 0U;
}

configuration_validation_result_t configuration_validate(const configuration_t *configuration)
{
    configuration_validation_result_t certificate_result;

    if (configuration == NULL)
    {
        return CONFIGURATION_VALIDATION_INVALID_ARGUMENT;
    }

    if (configuration->network.mode == CONFIGURATION_NETWORK_MODE_STATIC)
    {
        if (!ipv4_is_nonzero_unicast(&configuration->network.ip_address) ||
            !subnet_mask_is_valid(&configuration->network.subnet_mask) ||
            !address_is_usable_in_subnet(&configuration->network.ip_address, &configuration->network.subnet_mask) ||
            !ipv4_is_nonzero_unicast(&configuration->network.gateway) ||
            !address_is_usable_in_subnet(&configuration->network.gateway, &configuration->network.subnet_mask) ||
            !ip4_addr_netcmp(&configuration->network.ip_address, &configuration->network.gateway,
                             &configuration->network.subnet_mask) ||
            ip4_addr_cmp(&configuration->network.ip_address, &configuration->network.gateway) ||
            !ipv4_is_nonzero_unicast(&configuration->network.dns_primary) ||
            !ipv4_is_nonzero_unicast(&configuration->network.dns_secondary) ||
            ip4_addr_cmp(&configuration->network.dns_primary, &configuration->network.dns_secondary))
        {
            return CONFIGURATION_VALIDATION_NETWORK_INVALID;
        }
    }
    else if (configuration->network.mode != CONFIGURATION_NETWORK_MODE_DHCP)
    {
        return CONFIGURATION_VALIDATION_NETWORK_INVALID;
    }

    if (!baud_rate_is_valid(configuration->rtu.baud_rate) ||
        !frame_format_is_valid(configuration->rtu.frame_format) ||
        configuration->rtu.first_byte_timeout_ms < UINT16_C(50) ||
        configuration->rtu.first_byte_timeout_ms > UINT16_C(3000))
    {
        return CONFIGURATION_VALIDATION_RTU_INVALID;
    }

    if (configuration->modbus_tcp.listen_port == 0U)
    {
        return CONFIGURATION_VALIDATION_MODBUS_TCP_INVALID;
    }

    if (!endpoint_address_is_valid(&configuration->sntp.servers[0]) ||
        !endpoint_address_is_valid(&configuration->sntp.servers[1]) ||
        endpoint_addresses_equal(&configuration->sntp.servers[0], &configuration->sntp.servers[1]))
    {
        return CONFIGURATION_VALIDATION_SNTP_INVALID;
    }

    if (configuration->mqtt.mode == CONFIGURATION_MQTT_MODE_ENABLED)
    {
        if (!hostname_is_valid(&configuration->mqtt.broker_address) ||
            configuration->mqtt.broker_port == 0U || !client_id_is_valid(&configuration->mqtt.client_id) ||
            !visible_ascii_is_valid(configuration->mqtt.username.bytes, configuration->mqtt.username.length, 1U,
                                    CONFIGURATION_USERNAME_MAX_LENGTH) ||
            !visible_ascii_is_valid(configuration->mqtt.password.bytes, configuration->mqtt.password.length, 1U,
                                    CONFIGURATION_PASSWORD_MAX_LENGTH) ||
            configuration->mqtt.keep_alive_seconds < UINT16_C(30) ||
            configuration->mqtt.keep_alive_seconds > UINT16_C(3600) ||
            !mqtt_message_is_valid(&configuration->mqtt.online_message) ||
            !mqtt_message_is_valid(&configuration->mqtt.will_message))
        {
            return CONFIGURATION_VALIDATION_MQTT_INVALID;
        }

        certificate_result = ca_certificate_is_valid(&configuration->mqtt.ca_certificate_pem);
        if (certificate_result != CONFIGURATION_VALIDATION_OK)
        {
            return certificate_result;
        }
    }
    else if (configuration->mqtt.mode != CONFIGURATION_MQTT_MODE_DISABLED)
    {
        return CONFIGURATION_VALIDATION_MQTT_INVALID;
    }

    if(configuration->mqtt.mode == CONFIGURATION_MQTT_MODE_DISABLED)
    {
        return configuration->collection.point_count == 0 ? CONFIGURATION_VALIDATION_OK : CONFIGURATION_VALIDATION_COLLECTION_INVALID;
    }
    else if(configuration->collection.point_count > CONFIGURATION_COLLECTION_POINT_MAX_COUNT ||
            configuration->collection.point_count < 1U)
    {
        return CONFIGURATION_VALIDATION_COLLECTION_INVALID;
    }

    uint8_t left_index;
    uint8_t right_index;

    for (left_index = 0U; left_index < configuration->collection.point_count; left_index++)
    {
        const configuration_collection_point_t *point = &configuration->collection.points[left_index];

        if (!collection_point_is_valid(point))
        {
            return CONFIGURATION_VALIDATION_COLLECTION_INVALID;
        }

        if ((configuration->mqtt.online_message.mode == CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM &&
             topic_compare(&point->topic, &configuration->mqtt.online_message.topic) == 0) ||
            (configuration->mqtt.will_message.mode == CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM &&
             topic_compare(&point->topic, &configuration->mqtt.will_message.topic) == 0))
        {
            return CONFIGURATION_VALIDATION_COLLECTION_INVALID;
        }

        for (right_index = (uint8_t)(left_index + 1U); right_index < configuration->collection.point_count;
             right_index++)
        {
            if (topic_compare(&point->topic, &configuration->collection.points[right_index].topic) == 0)
            {
                return CONFIGURATION_VALIDATION_COLLECTION_INVALID;
            }
        }
    }

    return rtu_collection_bus_utilization_is_within_limit(configuration)
               ? CONFIGURATION_VALIDATION_OK
               : CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED;
}

static bool byte_strings_equal(const uint8_t *left, uint16_t left_length, const uint8_t *right, uint16_t right_length)
{
    return left_length == right_length && memcmp(left, right, left_length) == 0;
}

static bool mqtt_messages_equal(const configuration_mqtt_message_t *left,
                                const configuration_mqtt_message_t *right)
{
    if (left->mode != right->mode)
    {
        return false;
    }

    if (left->mode == CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED)
    {
        return true;
    }

    return byte_strings_equal(left->topic.bytes, left->topic.length, right->topic.bytes, right->topic.length) &&
           byte_strings_equal(left->payload.bytes, left->payload.length, right->payload.bytes, right->payload.length) &&
           left->qos == right->qos && left->retain == right->retain;
}

static bool collection_points_equal(const configuration_collection_point_t *left,
                                    const configuration_collection_point_t *right)
{

    if ((left->source == CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER ||
         left->source == CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER) &&
        left->data_type != right->data_type)
    {
        return false;
    }

    return left->slave_address == right->slave_address && left->source == right->source &&
           left->address == right->address && left->poll_interval_ms == right->poll_interval_ms &&
           left->first_byte_timeout_ms == right->first_byte_timeout_ms &&
           byte_strings_equal(left->topic.bytes, left->topic.length, right->topic.bytes, right->topic.length) &&
           left->qos == right->qos;
}

bool configuration_equals(const configuration_t *left, const configuration_t *right)
{
    uint8_t server_index;
    uint8_t left_index;

    if (left->network.mode != right->network.mode)
    {
        return false;
    }

    if (left->network.mode == CONFIGURATION_NETWORK_MODE_STATIC &&
        (!ip4_addr_cmp(&left->network.ip_address, &right->network.ip_address) ||
         !ip4_addr_cmp(&left->network.subnet_mask, &right->network.subnet_mask) ||
         !ip4_addr_cmp(&left->network.gateway, &right->network.gateway) ||
         !ip4_addr_cmp(&left->network.dns_primary, &right->network.dns_primary) ||
         !ip4_addr_cmp(&left->network.dns_secondary, &right->network.dns_secondary)))
    {
        return false;
    }

    if (left->rtu.baud_rate != right->rtu.baud_rate || left->rtu.frame_format != right->rtu.frame_format ||
        left->rtu.first_byte_timeout_ms != right->rtu.first_byte_timeout_ms)
    {
        return false;
    }

    if (left->modbus_tcp.listen_port != right->modbus_tcp.listen_port)
    {
        return false;
    }

    for (server_index = 0U; server_index < 2U; server_index++)
    {
        if (!endpoint_addresses_equal(&left->sntp.servers[server_index], &right->sntp.servers[server_index]))
        {
            return false;
        }
    }

    if (left->mqtt.mode != right->mqtt.mode)
    {
        return false;
    }

    if (left->mqtt.mode == CONFIGURATION_MQTT_MODE_ENABLED)
    {
        if (!hostnames_equal(&left->mqtt.broker_address, &right->mqtt.broker_address) ||
            left->mqtt.broker_port != right->mqtt.broker_port ||
            left->mqtt.client_id.mode != right->mqtt.client_id.mode ||
            (left->mqtt.client_id.mode == CONFIGURATION_CLIENT_ID_MODE_EXPLICIT &&
             !byte_strings_equal(left->mqtt.client_id.explicit_value.bytes,
                                 left->mqtt.client_id.explicit_value.length,
                                 right->mqtt.client_id.explicit_value.bytes,
                                 right->mqtt.client_id.explicit_value.length)) ||
            !byte_strings_equal(left->mqtt.username.bytes, left->mqtt.username.length, right->mqtt.username.bytes,
                                right->mqtt.username.length) ||
            !byte_strings_equal(left->mqtt.password.bytes, left->mqtt.password.length, right->mqtt.password.bytes,
                                right->mqtt.password.length) ||
            !byte_strings_equal(left->mqtt.ca_certificate_pem.bytes, left->mqtt.ca_certificate_pem.length,
                                right->mqtt.ca_certificate_pem.bytes, right->mqtt.ca_certificate_pem.length) ||
            left->mqtt.keep_alive_seconds != right->mqtt.keep_alive_seconds ||
            !mqtt_messages_equal(&left->mqtt.online_message, &right->mqtt.online_message) ||
            !mqtt_messages_equal(&left->mqtt.will_message, &right->mqtt.will_message))
        {
            return false;
        }
    }

    if (left->collection.point_count != right->collection.point_count)
    {
        return false;
    }

    for (left_index = 0U; left_index < left->collection.point_count; left_index++)
    {
        uint8_t right_index;
        bool found = false;

        /* 规定传入config必须验证成功的，验证成功的config不存在重复的point */
        for (right_index = 0U; right_index < right->collection.point_count; right_index++)
        {
            if (collection_points_equal(&left->collection.points[left_index], &right->collection.points[right_index]))
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            return false;
        }
    }

    return true;
}
