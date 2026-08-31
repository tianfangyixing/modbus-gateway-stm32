#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <stdint.h>

typedef enum
{
    MQTT_PUBLISHER_STATE_DISABLED = 0,
    MQTT_PUBLISHER_STATE_DISCONNECTED = 1,
    MQTT_PUBLISHER_STATE_CONNECTING = 2,
    MQTT_PUBLISHER_STATE_CONNECTED = 3,
    MQTT_PUBLISHER_STATE_ERROR = 4
} mqtt_publisher_state_t;

typedef enum
{
    MQTT_PUBLISHER_PUBLISH_OK = 0,
    MQTT_PUBLISHER_PUBLISH_INVALID_ARGUMENT,
    MQTT_PUBLISHER_PUBLISH_DISABLED,
    MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED,
    MQTT_PUBLISHER_PUBLISH_NO_RESOURCE,
    MQTT_PUBLISHER_PUBLISH_TIMEOUT
} mqtt_publisher_publish_result_t;

typedef void (*mqtt_publisher_publish_callback_t)(void *context, mqtt_publisher_publish_result_t result);

/**
  * @brief 使用配置服务提供的稳定活动配置初始化 MQTT 发布器。
  * @note 必须在调度器启动后由普通 FreeRTOS 任务调用。
  */
void mqtt_publisher_init(void);

/**
 * @brief 提交一条长度受限的消息进行异步发布。
 * @note 必须由普通 FreeRTOS 任务调用，不得从中断服务程序或发布器回调中调用。
 * @note MQTT_PUBLISHER_PUBLISH_OK 表示请求已被接受，并不表示发布已经完成。
 * @note LwIP 会在本函数返回前复制主题和负载，函数返回后可立即复用二者。
 * @note 对于已接受的请求，非 NULL 回调会且仅会被调用一次，结果为 OK、TIMEOUT 或 NOT_CONNECTED。
 * @note 回调不得阻塞。
 * @note LwIP 接受请求后，回调可能在本函数返回前执行。
 */
mqtt_publisher_publish_result_t mqtt_publisher_publish(const char *topic, uint16_t topic_length, const void *payload,
                                                       uint16_t payload_length, uint8_t qos, uint8_t retain,
                                                       mqtt_publisher_publish_callback_t callback, void *context);

mqtt_publisher_state_t mqtt_publisher_get_state(void);

#if defined(MQTT_PUBLISHER_CLIENT_ID_TEST)
const char *mqtt_publisher_test_client_id_from_uid(uint32_t uid_word0, uint32_t uid_word1, uint32_t uid_word2);
#endif

#endif
