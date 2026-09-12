#include "mqtt_publisher.h"
#include "watchdog.h"

#include <stddef.h>
#include <stdint.h>

#define MQTT_PUBLISHER_DERIVED_CLIENT_ID_LENGTH 23U
#define MQTT_PUBLISHER_CLIENT_ID_BUFFER_SIZE (MQTT_PUBLISHER_DERIVED_CLIENT_ID_LENGTH + 1U)

static char mqtt_derived_client_id[MQTT_PUBLISHER_CLIENT_ID_BUFFER_SIZE];

static void mqtt_publisher_client_id_from_uid(uint32_t uid_word0, uint32_t uid_word1, uint32_t uid_word2)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint32_t uid_words[3] = {uid_word0, uid_word1, uid_word2};
    uint32_t accumulator = 0U;
    uint8_t uid_bytes[12];
    uint8_t accumulated_bits = 0U;
    size_t input_index;
    size_t output_index = 3U;

    for (input_index = 0U; input_index < 3U; input_index++)
    {
        uint32_t word = uid_words[input_index];
        size_t byte_index = input_index * 4U;

        uid_bytes[byte_index] = (uint8_t)(word >> 24U);
        uid_bytes[byte_index + 1U] = (uint8_t)(word >> 16U);
        uid_bytes[byte_index + 2U] = (uint8_t)(word >> 8U);
        uid_bytes[byte_index + 3U] = (uint8_t)word;
    }

    mqtt_derived_client_id[0] = 'S';
    mqtt_derived_client_id[1] = 'T';
    mqtt_derived_client_id[2] = 'M';
    for (input_index = 0U; input_index < sizeof(uid_bytes); input_index++)
    {
        accumulator = (accumulator << 8U) | uid_bytes[input_index];
        accumulated_bits = (uint8_t)(accumulated_bits + 8U);
        while (accumulated_bits >= 5U)
        {
            accumulated_bits = (uint8_t)(accumulated_bits - 5U);
            mqtt_derived_client_id[output_index] =
                alphabet[(accumulator >> accumulated_bits) & UINT32_C(0x1F)];
            output_index++;
        }
    }
    if (accumulated_bits != 0U)
    {
        mqtt_derived_client_id[output_index] =
            alphabet[(accumulator << (5U - accumulated_bits)) & UINT32_C(0x1F)];
        output_index++;
    }
    mqtt_derived_client_id[output_index] = '\0';
}

#if defined(MQTT_PUBLISHER_CLIENT_ID_TEST)
const char *mqtt_publisher_test_client_id_from_uid(uint32_t uid_word0, uint32_t uid_word1, uint32_t uid_word2)
{
    mqtt_publisher_client_id_from_uid(uid_word0, uid_word1, uid_word2);
    return mqtt_derived_client_id;
}
#else

#include "configuration_service.h"
#include "mqtt_tls_policy.h"

#include "FreeRTOS.h"
#include "debug_log.h"
#include "lwip/altcp_tls.h"
#include "lwip/api.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "main.h"
#include "semphr.h"
#include "sntp_service.h"
#include "task.h"

#include <string.h>
#include <stdbool.h>

#define MQTT_CONNECTION_TASK_STACK_DEPTH 768U
#define MQTT_CONNECTION_TASK_PRIORITY 22U
#define MQTT_CONNECTION_CHECK_INTERVAL_MS 3000U
#define MQTT_CONNECTION_CONNECTING_TIMEOUT_MS 15000U
#define PUBLISH_QUEUE_LENGTH (4)

typedef enum
{
    MQTT_PREREQUISITE_READY = 0,
    MQTT_PREREQUISITE_NETWORK_INTERFACE,
    MQTT_PREREQUISITE_NETWORK_INTERFACE_DOWN,
    MQTT_PREREQUISITE_NETWORK_LINK_DOWN,
    MQTT_PREREQUISITE_TIME_SYNC
} mqtt_prerequisite_status_t;

static StackType_t mqtt_connection_task_stack[MQTT_CONNECTION_TASK_STACK_DEPTH];
static StaticTask_t mqtt_connection_task_buffer;

static volatile bool publish_slot_available[PUBLISH_QUEUE_LENGTH];
static volatile mqtt_publisher_publish_callback_t publish_slot_cb[PUBLISH_QUEUE_LENGTH];
static void *volatile publish_slot_context[PUBLISH_QUEUE_LENGTH];
static volatile mqtt_publisher_state_t mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISABLED;
static TickType_t start_connecting_tick;

static const configuration_mqtt_t *mqtt_configuration;
static struct mqtt_connect_client_info_t mqtt_client_info;
static struct altcp_tls_config *mqtt_tls_config;
static mqtt_client_t *mqtt_client;

static void mqtt_connection_task(void *argument);


static bool mqtt_ensure_tls_config(void)
{
    bool result;

    LOCK_TCPIP_CORE();

    result = mqtt_tls_policy_set_broker_hostname((const char *)mqtt_configuration->broker_address.bytes);
    if (result && mqtt_tls_config == NULL)
    {
        mqtt_tls_config = altcp_tls_create_config_client(
            mqtt_configuration->ca_certificate_pem.bytes,
            (size_t)mqtt_configuration->ca_certificate_pem.length + 1U);
        result = mqtt_tls_config != NULL;
    }

    if (!result)
    {
        mqtt_tls_policy_clear_broker_hostname();
    }

    UNLOCK_TCPIP_CORE();

    if (!result)
    {
        debug_log_printf("MQTT TLS configuration failed\r\n");
    }

    return result;
}

/* 由 TCP/IP 核心锁保护，静态地址用于接收超时后仍可能到来的回调。 */
static ip_addr_t dns_resolve_addr;
static err_t dns_resolve_result = ERR_OK;

static void mqtt_dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    ip_addr_t *resolved_addr = callback_arg;

    if (ipaddr == NULL)
    {
        dns_resolve_result = ERR_VAL;
        debug_log_printf("MQTT DNS failed: %s\r\n", name);
        return;
    }

    *resolved_addr = *ipaddr;
    dns_resolve_result = ERR_OK;

    debug_log_printf("MQTT DNS resolved: %s -> %s\r\n", name, ipaddr_ntoa(ipaddr));
}

static err_t mqtt_resolve_host(ip_addr_t *mqtt_ip)
{
    if (mqtt_ip == NULL)
    {
        return ERR_ARG;
    }

    const char *broker_hostname = (const char *)mqtt_configuration->broker_address.bytes;
    const TickType_t timeout_ticks = pdMS_TO_TICKS(3000U);
    const TickType_t poll_ticks = pdMS_TO_TICKS(100U);
    err_t result;

    LOCK_TCPIP_CORE();

    /* 上次等待虽然超时，但底层查询可能尚未结束。 */
    if (dns_resolve_result == ERR_INPROGRESS)
    {
        UNLOCK_TCPIP_CORE();
        return ERR_INPROGRESS;
    }

    dns_resolve_result = dns_gethostbyname(broker_hostname, mqtt_ip, mqtt_dns_found, &dns_resolve_addr);
    result = dns_resolve_result;

    UNLOCK_TCPIP_CORE();

    /* 立即成功或发起失败，都不需要等待回调。 */
    if (result != ERR_INPROGRESS)
    {
        return result;
    }

    TickType_t started_tick = xTaskGetTickCount();

    for (;;)
    {
        LOCK_TCPIP_CORE();

        result = dns_resolve_result;

        if (result == ERR_OK)
        {
            *mqtt_ip = dns_resolve_addr;
        }

        UNLOCK_TCPIP_CORE();

        if (result != ERR_INPROGRESS)
        {
            return result;
        }

        TickType_t elapsed_ticks = xTaskGetTickCount() - started_tick;

        if (elapsed_ticks >= timeout_ticks)
        {
            debug_log_printf("MQTT DNS lookup timed out after 3000 ms: %s\r\n", broker_hostname);
            return ERR_TIMEOUT;
        }

        watchdog_report(WATCHDOG_EVENT_MQTT);

        TickType_t remaining_ticks = timeout_ticks - elapsed_ticks;
        TickType_t delay_ticks = remaining_ticks < poll_ticks ? remaining_ticks : poll_ticks;

        vTaskDelay(delay_ticks);
    }
}

static void mqtt_publish_callback(void *argument, err_t result)
{
    mqtt_publisher_publish_callback_t callback = NULL;
    void *context = NULL;
    int index = (int)(uintptr_t)argument;

    if (index < 0 || index >= PUBLISH_QUEUE_LENGTH)
    {
        return;
    }

    callback = publish_slot_cb[index];
    context = publish_slot_context[index];
    publish_slot_available[index] = true;

    if (callback == NULL)
    {
        return;
    }

    if (result == ERR_TIMEOUT)
    {
        callback(context, MQTT_PUBLISHER_PUBLISH_TIMEOUT);
    }
    else if (result == ERR_OK)
    {
        callback(context, MQTT_PUBLISHER_PUBLISH_OK);
    }
}

static void mqtt_publish_online_message(mqtt_client_t *client)
{
    const configuration_mqtt_message_t *online_message = &mqtt_configuration->online_message;
    err_t result;

    if (online_message->mode != CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM)
    {
        return;
    }

    result = mqtt_publish(client, (const char *)online_message->topic.bytes, online_message->payload.bytes,
                          online_message->payload.length, online_message->qos, online_message->retain, NULL, NULL);
    if (result != ERR_OK)
    {
        debug_log_printf("MQTT online message submission failed, err=%d\r\n", (int)result);
    }
}

static void mqtt_complete_publish_slots(mqtt_publisher_publish_result_t result)
{
    int index;

    for (index = 0; index < PUBLISH_QUEUE_LENGTH; index++)
    {
        mqtt_publisher_publish_callback_t callback = NULL;
        void *context = NULL;

        if (!publish_slot_available[index])
        {
            callback = publish_slot_cb[index];
            context = publish_slot_context[index];
            publish_slot_available[index] = true;
            publish_slot_cb[index] = NULL;
            publish_slot_context[index] = NULL;
        }

        if (callback != NULL)
        {
            callback(context, result);
        }
    }
}



static void mqtt_connection_cb(mqtt_client_t *client, void *argument, mqtt_connection_status_t status)
{
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        mqtt_publisher_state = MQTT_PUBLISHER_STATE_CONNECTED;
        debug_log_printf("MQTT connection established\r\n");
        mqtt_publish_online_message(client);
        return;
    }

    // 完成失败回调
    mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;
    mqtt_complete_publish_slots(MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED);
    debug_log_printf("MQTT connection unavailable, status=%d\r\n", (int)status);
}

static void mqtt_connection_task(void *argument)
{
    ip_addr_t mqtt_ip;

    for (;;)
    {
        watchdog_report(WATCHDOG_EVENT_MQTT);
        
        LOCK_TCPIP_CORE();
        if(mqtt_publisher_state == MQTT_PUBLISHER_STATE_CONNECTED)
        {
            UNLOCK_TCPIP_CORE();
            vTaskDelay(MQTT_CONNECTION_CHECK_INTERVAL_MS);
            continue;
        }
        else if(mqtt_publisher_state == MQTT_PUBLISHER_STATE_CONNECTING)
        {
            TickType_t now_tick = xTaskGetTickCount();

            if(now_tick - start_connecting_tick >= pdMS_TO_TICKS(MQTT_CONNECTION_CONNECTING_TIMEOUT_MS))
            {
                mqtt_disconnect(mqtt_client);
                mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;
            }
            else
            {
                UNLOCK_TCPIP_CORE();
                if(MQTT_CONNECTION_CONNECTING_TIMEOUT_MS - (now_tick - start_connecting_tick) > MQTT_CONNECTION_CHECK_INTERVAL_MS)
                {
                    vTaskDelay(MQTT_CONNECTION_CHECK_INTERVAL_MS);
                }
                else
                {
                    vTaskDelay(MQTT_CONNECTION_CONNECTING_TIMEOUT_MS - (now_tick - start_connecting_tick));
                }
                continue;
            }
        }
        UNLOCK_TCPIP_CORE();

        if (!sntp_service_is_synchronized())
        {
            vTaskDelay(MQTT_CONNECTION_CHECK_INTERVAL_MS);
            continue;
        }

        if (!mqtt_ensure_tls_config() || mqtt_resolve_host(&mqtt_ip) != ERR_OK)
        {
            vTaskDelay(MQTT_CONNECTION_CHECK_INTERVAL_MS);
            continue;
        }

        mqtt_client_info.tls_config = mqtt_tls_config;

        LOCK_TCPIP_CORE();

        err_t connect_result = mqtt_client_connect(mqtt_client, &mqtt_ip, mqtt_configuration->broker_port,
                                             mqtt_connection_cb, NULL, &mqtt_client_info);
        if (connect_result == ERR_OK)
        {
            start_connecting_tick = xTaskGetTickCount();
            mqtt_publisher_state = MQTT_PUBLISHER_STATE_CONNECTING;
            debug_log_printf("MQTT connection attempt started: %s:%u\r\n",
                             (const char *)mqtt_configuration->broker_address.bytes,
                             (unsigned int)mqtt_configuration->broker_port);
        }
        else
        {
            debug_log_printf("MQTT connection attempt rejected, err=%d\r\n", (int)connect_result);
            mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;
        }
        UNLOCK_TCPIP_CORE();
        vTaskDelay(MQTT_CONNECTION_CHECK_INTERVAL_MS);
    }
}

static bool mqtt_topic_is_valid(const char *topic, uint16_t topic_length)
{
    if (topic == NULL || topic_length == 0U || topic_length > CONFIGURATION_TOPIC_MAX_LENGTH ||
        topic[topic_length] != '\0')
    {
        return false;
    }
    return memchr(topic, '\0', topic_length) == NULL;
}

void mqtt_publisher_init(void)
{
    const configuration_t *active_configuration;
    const configuration_mqtt_message_t *will_message;
    TaskHandle_t connection_task_handle;
    int index;

    debug_log_printf("MQTT publisher is initing...");

    active_configuration = configuration_service_active();
    if (active_configuration == NULL)
    {
        debug_log_printf("MQTT publisher initialization failed: active configuration unavailable\r\n");
        Error_Handler();
    }

    mqtt_configuration = &active_configuration->mqtt;
    if (mqtt_configuration->mode == CONFIGURATION_MQTT_MODE_DISABLED)
    {
        mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISABLED;
        debug_log_printf("MQTT publisher disabled by active configuration\r\n");
        return;
    }

    if (mqtt_configuration->client_id.mode == CONFIGURATION_CLIENT_ID_MODE_DERIVED)
    {
        mqtt_publisher_client_id_from_uid(HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2());
        mqtt_client_info.client_id = mqtt_derived_client_id;
    }
    else if (mqtt_configuration->client_id.mode == CONFIGURATION_CLIENT_ID_MODE_EXPLICIT)
    {
        mqtt_client_info.client_id = (const char *)mqtt_configuration->client_id.explicit_value.bytes;
    }

    mqtt_client_info.client_user = (const char *)mqtt_configuration->username.bytes;
    mqtt_client_info.client_pass = (const char *)mqtt_configuration->password.bytes;
    mqtt_client_info.keep_alive = mqtt_configuration->keep_alive_seconds;
    will_message = &mqtt_configuration->will_message;
    if (will_message->mode == CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM)
    {
        mqtt_client_info.will_topic = (const char *)will_message->topic.bytes;
        mqtt_client_info.will_msg = (const char *)will_message->payload.bytes;
        mqtt_client_info.will_qos = will_message->qos;
        mqtt_client_info.will_retain = will_message->retain;
    }
    else
    {
        mqtt_client_info.will_topic = NULL;
        mqtt_client_info.will_msg = NULL;
        mqtt_client_info.will_qos = 0U;
        mqtt_client_info.will_retain = 0U;
    }
    mqtt_client_info.tls_config = NULL;

    for (index = 0; index < PUBLISH_QUEUE_LENGTH; index++)
    {
        publish_slot_available[index] = true;
        publish_slot_cb[index] = NULL;
        publish_slot_context[index] = NULL;
    }
    mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    if (mqtt_client == NULL)
    {
        debug_log_printf("MQTT publisher initialization failed: MQTT client unavailable\r\n");
        Error_Handler();
    }

    connection_task_handle = xTaskCreateStatic(mqtt_connection_task, "mqtt_conn", MQTT_CONNECTION_TASK_STACK_DEPTH,
                                               NULL, MQTT_CONNECTION_TASK_PRIORITY, mqtt_connection_task_stack,
                                               &mqtt_connection_task_buffer);

    debug_log_printf("MQTT publisher started for %s:%u\r\n",
                     (const char *)mqtt_configuration->broker_address.bytes,
                     (unsigned int)mqtt_configuration->broker_port);
}

mqtt_publisher_publish_result_t mqtt_publisher_publish(const char *topic, uint16_t topic_length, const void *payload,
                                                       uint16_t payload_length, uint8_t qos, uint8_t retain,
                                                       mqtt_publisher_publish_callback_t callback, void *context)
{
    err_t result;
    int index = -1;

    if (!mqtt_topic_is_valid(topic, topic_length) ||
        (payload_length != 0U && payload == NULL) || payload_length > CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH ||
        qos > 2U || retain > 1U)
    {
        return MQTT_PUBLISHER_PUBLISH_INVALID_ARGUMENT;
    }

    LOCK_TCPIP_CORE();
    if (mqtt_publisher_state == MQTT_PUBLISHER_STATE_DISABLED)
    {
        UNLOCK_TCPIP_CORE();
        return MQTT_PUBLISHER_PUBLISH_DISABLED;
    }
    else if(mqtt_publisher_state != MQTT_PUBLISHER_STATE_CONNECTED)
    {
        UNLOCK_TCPIP_CORE();
        return MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED;
    }

    for (int i = 0; i < PUBLISH_QUEUE_LENGTH; i++)
    {
        if (publish_slot_available[i])
        {
            index = i;
            publish_slot_available[index] = false;
            publish_slot_cb[index] = callback;
            publish_slot_context[index] = context;
            break;
        }
    }
    if (index < 0)
    {
        UNLOCK_TCPIP_CORE();
        return MQTT_PUBLISHER_PUBLISH_NO_RESOURCE;
    }

    result = mqtt_publish(mqtt_client, topic, payload, payload_length, qos, retain, mqtt_publish_callback,
                          (void *)(uintptr_t)index);

    if (result != ERR_OK)
    {
        publish_slot_available[index] = true;
    }
    UNLOCK_TCPIP_CORE();

    switch (result)
    {
        case ERR_OK:
            return MQTT_PUBLISHER_PUBLISH_OK;
        case ERR_CONN:
            return MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED;
        case ERR_MEM:
            return MQTT_PUBLISHER_PUBLISH_NO_RESOURCE;
        case ERR_ARG:
            return MQTT_PUBLISHER_PUBLISH_INVALID_ARGUMENT;
        default:
            debug_log_printf("mqtt publish unexpected result = %d\r\n", (int)result);
            Error_Handler();
			return MQTT_PUBLISHER_PUBLISH_UNKNOWN_ERROR;
    }
}

mqtt_publisher_state_t mqtt_publisher_get_state(void)
{
    return mqtt_publisher_state;
}

#endif
