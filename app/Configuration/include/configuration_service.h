#ifndef CONFIGURATION_SERVICE_H
#define CONFIGURATION_SERVICE_H

#include "configuration.h"

#include <stdint.h>

typedef enum
{
    CONFIGURATION_SERVICE_OK = 0,
    CONFIGURATION_SERVICE_INVALID_ARGUMENT,
    CONFIGURATION_SERVICE_NOT_INITIALIZED,
    CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE,
    CONFIGURATION_SERVICE_IO_ERROR,
    CONFIGURATION_SERVICE_INVALID_PAYLOAD
} configuration_service_result_t;

/**
  * @brief 初始化 Configuration Service，并加载当前 active 配置。
  * @pre 首次调用前必须已成功完成 tcpip_init() 和 external_flash_init()。
  * @pre 不得从 ISR 中调用。
  * @pre 调用方必须保证本次调用与本模块的其他调用以及所有其他 External Flash 访问严格串行。
  * @retval CONFIGURATION_SERVICE_OK 初始化成功，或服务已经成功初始化。
  * @retval CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE 解码持久化配置所需的临时资源不足，初始化失败。
  * @retval CONFIGURATION_SERVICE_IO_ERROR 访问 External Flash 失败，初始化失败。
  * @note 初始化成功后的重复调用不再访问 External Flash，也不改变 active 配置。
  * @warning 首次调用失败后，服务进入不可恢复的失败状态，不得再调用本模块的任何函数。
  */
configuration_service_result_t configuration_service_init(void);

/**
  * @brief 获取当前 active 配置。
  * @return 初始化成功后返回当前 active 配置的只读指针，否则返回 NULL。
  * @pre 不得从 ISR 中调用。
  * @pre 调用方必须保证本次调用与本模块的其他调用严格串行。
  * @pre 首次调用 configuration_service_init() 已失败时不得调用。
  * @warning 调用方不得通过移除 const 限定修改返回的配置对象。
  * @note 初始化成功后返回的指针及其指向的配置在本次系统运行期内保持不变。
  */
const configuration_t *configuration_service_active(void);

/**
  * @brief 校验二进制配置载荷并原子提交到 External Flash，供后续系统启动时加载。
  * @param payload 待持久化载荷的起始地址。不得为 NULL；缓冲区必须完整位于 DMA 可访问的 SRAM 中，
  *                在函数返回前保持可读，并至少包含 payload_length 字节。
  * @param payload_length 载荷长度，单位为字节，必须位于 1..CONFIGURATION_V1_MAX_PAYLOAD_LENGTH 闭区间内。
  * @pre 必须先成功调用 configuration_service_init()。
  * @pre 不得从 ISR 中调用。
  * @pre 调用方必须保证本次调用与本模块的其他调用以及所有其他 External Flash 访问严格串行。
  * @retval CONFIGURATION_SERVICE_OK 载荷已完成持久化和回读校验，active 配置不变。
  * @retval CONFIGURATION_SERVICE_NOT_INITIALIZED 服务尚未成功初始化。
  * @retval CONFIGURATION_SERVICE_INVALID_ARGUMENT payload 为 NULL，或 payload_length 不在允许范围内。
  * @retval CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE 校验载荷所需的临时资源不足，未访问 External Flash。
  * @retval CONFIGURATION_SERVICE_IO_ERROR 访问 External Flash 失败，或持久化后的回读内容与载荷不一致。
  * @retval CONFIGURATION_SERVICE_INVALID_PAYLOAD 载荷的 schema、二进制结构或配置模型无效，未访问 External Flash。
  * @note 只有 configuration_binary_decode() 校验成功的载荷才会被提交。
  */
configuration_service_result_t configuration_service_write(const uint8_t *payload, uint32_t payload_length);

#endif
