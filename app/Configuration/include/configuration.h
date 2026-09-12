#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "lwip/ip4_addr.h"

#include <stdbool.h>
#include <stdint.h>

#define CONFIGURATION_SCHEMA_VERSION UINT8_C(1)
#define CONFIGURATION_V1_MAX_PAYLOAD_LENGTH UINT32_C(7826)
#define CONFIGURATION_V1_DEFAULT_PAYLOAD_LENGTH UINT32_C(48)

#define CONFIGURATION_HOSTNAME_MAX_LENGTH UINT16_C(253)
#define CONFIGURATION_CLIENT_ID_MAX_LENGTH UINT16_C(23)
#define CONFIGURATION_USERNAME_MAX_LENGTH UINT16_C(32)
#define CONFIGURATION_PASSWORD_MAX_LENGTH UINT16_C(64)
#define CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH UINT16_C(4096)
#define CONFIGURATION_TOPIC_MAX_LENGTH UINT16_C(128)
#define CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH UINT16_C(128)
#define CONFIGURATION_COLLECTION_POINT_MAX_COUNT UINT8_C(16)

/** @defgroup Configuration_Network_Modes Configuration Network Modes
  * @{
  */
#define CONFIGURATION_NETWORK_MODE_DHCP (0U)
#define CONFIGURATION_NETWORK_MODE_STATIC (1U)
/**
  * @}
  */

/** @defgroup Configuration_Frame_Formats Configuration Frame Formats
  * @{
  */
#define CONFIGURATION_FRAME_FORMAT_8N1 (0U)
#define CONFIGURATION_FRAME_FORMAT_8E1 (1U)
#define CONFIGURATION_FRAME_FORMAT_8O1 (2U)
#define CONFIGURATION_FRAME_FORMAT_8N2 (3U)
/**
  * @}
  */

/** @defgroup Configuration_Endpoint_Address_Types Configuration Endpoint Address Types
  * @{
  */
#define CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME (0U)
#define CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4 (1U)
/**
  * @}
  */

/** @defgroup Configuration_MQTT_Modes Configuration MQTT Modes
  * @{
  */
#define CONFIGURATION_MQTT_MODE_DISABLED (0U)
#define CONFIGURATION_MQTT_MODE_ENABLED (1U)
/**
  * @}
  */

/** @defgroup Configuration_Client_ID_Modes Configuration Client ID Modes
  * @{
  */
#define CONFIGURATION_CLIENT_ID_MODE_DERIVED (0U)
#define CONFIGURATION_CLIENT_ID_MODE_EXPLICIT (1U)
/**
  * @}
  */

/** @defgroup Configuration_MQTT_Message_Modes Configuration MQTT Message Modes
  * @{
  */
#define CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED (0U)
#define CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM (1U)
/**
  * @}
  */

/** @defgroup Configuration_Collection_Sources Configuration Collection Sources
  * @{
  */
#define CONFIGURATION_COLLECTION_SOURCE_COIL (0U)
#define CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT (1U)
#define CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER (2U)
#define CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER (3U)
/**
  * @}
  */

/** @defgroup Configuration_Data_Types Configuration Data Types
  * @{
  */
#define CONFIGURATION_DATA_TYPE_UINT16 (0U)
#define CONFIGURATION_DATA_TYPE_INT16 (1U)
/**
  * @}
  */

typedef struct
{
    uint16_t length;
    /* The byte at bytes[length] is an in-memory NUL sentinel and is not encoded. */
    uint8_t bytes[CONFIGURATION_HOSTNAME_MAX_LENGTH + 1U];
} configuration_hostname_t;

typedef struct
{
    uint16_t length;
    uint8_t bytes[CONFIGURATION_CLIENT_ID_MAX_LENGTH + 1U];
} configuration_client_id_value_t;

typedef struct
{
    uint16_t length;
    uint8_t bytes[CONFIGURATION_USERNAME_MAX_LENGTH + 1U];
} configuration_username_t;

typedef struct
{
    uint16_t length;
    uint8_t bytes[CONFIGURATION_PASSWORD_MAX_LENGTH + 1U];
} configuration_password_t;

typedef struct
{
    uint16_t length;
    uint8_t bytes[CONFIGURATION_CA_CERTIFICATE_MAX_LENGTH + 1U];
} configuration_ca_certificate_t;

typedef struct
{
    uint16_t length;
    uint8_t bytes[CONFIGURATION_TOPIC_MAX_LENGTH + 1U];
} configuration_topic_t;

typedef struct
{
    uint16_t length;
    uint8_t bytes[CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH + 1U];
} configuration_mqtt_message_payload_t;

typedef struct
{
    uint8_t type; /*!< Specifies the endpoint address type.
                       This parameter can be a value of @ref Configuration_Endpoint_Address_Types */
    union
    {
        configuration_hostname_t hostname;
        ip4_addr_t ipv4;
    } value;
} configuration_endpoint_address_t;

typedef struct
{
    uint8_t mode; /*!< Specifies the MQTT client ID mode.
                       This parameter can be a value of @ref Configuration_Client_ID_Modes */
    configuration_client_id_value_t explicit_value;
} configuration_client_id_t;

typedef struct
{
    uint8_t mode; /*!< Specifies the MQTT message mode.
                       This parameter can be a value of @ref Configuration_MQTT_Message_Modes */
    configuration_topic_t topic;
    configuration_mqtt_message_payload_t payload;
    uint8_t qos;
    uint8_t retain;
} configuration_mqtt_message_t;

typedef struct
{
    uint8_t mode; /*!< Specifies the network mode.
                       This parameter can be a value of @ref Configuration_Network_Modes */
    ip4_addr_t ip_address;
    ip4_addr_t subnet_mask;
    ip4_addr_t gateway;
    ip4_addr_t dns_primary;
    ip4_addr_t dns_secondary;
} configuration_network_t;

typedef struct
{
    uint32_t baud_rate;
    uint8_t frame_format; /*!< Specifies the Modbus RTU frame format.
                               This parameter can be a value of @ref Configuration_Frame_Formats */
    uint16_t first_byte_timeout_ms;
} configuration_rtu_t;

typedef struct
{
    uint16_t listen_port;
} configuration_modbus_tcp_t;

typedef struct
{
    configuration_endpoint_address_t servers[2];
} configuration_sntp_t;

typedef struct
{
    uint8_t mode; /*!< Specifies whether MQTT publishing is enabled.
                       This parameter can be a value of @ref Configuration_MQTT_Modes */
    configuration_hostname_t broker_address;
    uint16_t broker_port;
    configuration_client_id_t client_id;
    configuration_username_t username;
    configuration_password_t password;
    configuration_ca_certificate_t ca_certificate_pem;
    uint16_t keep_alive_seconds;
    configuration_mqtt_message_t online_message;
    configuration_mqtt_message_t will_message;
} configuration_mqtt_t;

typedef struct
{
    uint8_t slave_address;
    uint8_t source; /*!< Specifies the Modbus data source.
                         This parameter can be a value of @ref Configuration_Collection_Sources */
    uint16_t address;
    uint8_t data_type; /*!< Specifies how register data is interpreted.
                            This parameter can be a value of @ref Configuration_Data_Types */
    uint32_t poll_interval_ms;
    uint16_t first_byte_timeout_ms;
    configuration_topic_t topic;
    uint8_t qos;
} configuration_collection_point_t;

typedef struct
{
    uint8_t point_count;
    configuration_collection_point_t points[CONFIGURATION_COLLECTION_POINT_MAX_COUNT];
} configuration_collection_t;

typedef struct
{
    configuration_network_t network;
    configuration_rtu_t rtu;
    configuration_modbus_tcp_t modbus_tcp;
    configuration_sntp_t sntp;
    configuration_mqtt_t mqtt;
    configuration_collection_t collection;
} configuration_t;

typedef enum
{
    CONFIGURATION_VALIDATION_OK = 0,
    CONFIGURATION_VALIDATION_INVALID_ARGUMENT,
    CONFIGURATION_VALIDATION_NETWORK_INVALID,
    CONFIGURATION_VALIDATION_RTU_INVALID,
    CONFIGURATION_VALIDATION_MODBUS_TCP_INVALID,
    CONFIGURATION_VALIDATION_SNTP_INVALID,
    CONFIGURATION_VALIDATION_MQTT_INVALID,
    CONFIGURATION_VALIDATION_CERTIFICATE_INVALID,
    CONFIGURATION_VALIDATION_COLLECTION_INVALID,
    CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED,
    CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE
} configuration_validation_result_t;

/**
  * @brief 将配置初始化为默认值。
  * @param configuration 待初始化的配置。允许为 NULL，为 NULL 时不执行任何操作。
  */
void configuration_set_defaults(configuration_t *configuration);

/**
  * @brief 校验配置是否有效。
  * @param configuration 待校验的配置。允许为 NULL。
  * @retval CONFIGURATION_VALIDATION_OK 配置有效。
  * @retval CONFIGURATION_VALIDATION_INVALID_ARGUMENT configuration 为 NULL。
  * @retval CONFIGURATION_VALIDATION_NETWORK_INVALID 网络配置无效。
  * @retval CONFIGURATION_VALIDATION_RTU_INVALID Modbus RTU 配置无效。
  * @retval CONFIGURATION_VALIDATION_MODBUS_TCP_INVALID Modbus TCP 配置无效。
  * @retval CONFIGURATION_VALIDATION_SNTP_INVALID SNTP 配置无效。
  * @retval CONFIGURATION_VALIDATION_MQTT_INVALID MQTT 配置无效。
  * @retval CONFIGURATION_VALIDATION_CERTIFICATE_INVALID MQTT CA 证书无效。
  * @retval CONFIGURATION_VALIDATION_COLLECTION_INVALID 采集配置无效。
  * @retval CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED 预计 Modbus RTU 总线利用率超过限制。
  * @retval CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE 校验 MQTT CA 证书所需的内存资源不可用。
  */
configuration_validation_result_t configuration_validate(const configuration_t *configuration);

/**
  * @brief 比较两个有效配置在语义上是否相等。
  * @param left 待比较的配置。不得为 NULL，且必须通过 configuration_validate() 校验。
  * @param right 待比较的配置。不得为 NULL，且必须通过 configuration_validate() 校验。
  * @return 两个配置在语义上相等时返回 true，否则返回 false。
  * @pre configuration_validate(left) 必须返回 CONFIGURATION_VALIDATION_OK。
  * @pre configuration_validate(right) 必须返回 CONFIGURATION_VALIDATION_OK。
  * @warning 若任一前置条件不满足，则行为未定义。
  */
bool configuration_equals(const configuration_t *left, const configuration_t *right);

#endif
