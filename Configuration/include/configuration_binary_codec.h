#ifndef CONFIGURATION_BINARY_CODEC_H
#define CONFIGURATION_BINARY_CODEC_H

#include "configuration.h"

#include <stdint.h>

typedef enum
{
    CONFIGURATION_BINARY_CODEC_OK = 0,
    CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT,
    CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED,
    CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID,
    CONFIGURATION_BINARY_CODEC_MALFORMED,
    CONFIGURATION_BINARY_CODEC_MODEL_INVALID,
    CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE,
    CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL
} configuration_binary_codec_result_t;

/**
 * @brief 将 Schema v1 二进制 payload 解码为配置。
 * @param payload 待解码的 payload。指向至少 payload_length 个可读字节，不得为 NULL。
 * @param payload_length payload 长度，单位为字节。
 * @param configuration 接收解码结果的配置。不得为 NULL；解码失败时内容未定义。
 * @retval CONFIGURATION_BINARY_CODEC_OK 解码成功，且配置模型校验通过。
 * @retval CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT payload 或 configuration 为 NULL。
 * @retval CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED payload 中的 Schema 版本不受支持。
 * @retval CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID payload_length 为 0 或超过 Schema v1 最大长度。
 * @retval CONFIGURATION_BINARY_CODEC_MALFORMED payload 结构不完整、字段格式无效或存在尾随字节。
 * @retval CONFIGURATION_BINARY_CODEC_MODEL_INVALID payload 结构有效，但解码后的配置模型无效。
 * @retval CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE 校验解码后配置所需的内存资源不可用。
 */
configuration_binary_codec_result_t configuration_binary_decode(const uint8_t *payload, uint32_t payload_length,
                                                                  configuration_t *configuration);

/**
 * @brief 将有效配置编码为以 CONFIGURATION_SCHEMA_VERSION 开头的 Schema v1 二进制 payload。
 * @param configuration 待编码的配置。不得为 NULL，且必须通过 configuration_validate() 校验。
 * @param payload 接收编码结果的缓冲区。指向至少 payload_capacity 个可写字节，不得为 NULL。
 * @param payload_capacity payload 缓冲区容量，单位为字节。
 * @param payload_length 接收实际编码长度。不得为 NULL；编码失败时其值未定义。
 * @retval CONFIGURATION_BINARY_CODEC_OK 编码成功。
 * @retval CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT configuration、payload 或 payload_length 为 NULL。
 * @retval CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL payload_capacity 小于完整编码所需长度。
 * @pre configuration_validate(configuration) 必须返回 CONFIGURATION_VALIDATION_OK。
 * @warning configuration 不满足前置条件时，行为未定义。
 */
configuration_binary_codec_result_t configuration_binary_encode(const configuration_t *configuration,
                                                                  uint8_t *payload, uint32_t payload_capacity,
                                                                  uint32_t *payload_length);

#endif
