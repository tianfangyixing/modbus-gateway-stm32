#include "mqtt_publisher.h"

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
#define MQTT_CONNECTION_RETRY_INTERVAL_MS 15000U
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
static StaticSemaphore_t publish_slot_mutex_buffer;
static SemaphoreHandle_t publish_slot_mutex;
static const configuration_mqtt_t *mqtt_configuration;
static struct mqtt_connect_client_info_t mqtt_client_info;
static mqtt_client_t *mqtt_client;
static struct altcp_tls_config *mqtt_tls_config;
static volatile bool connected;
static volatile mqtt_publisher_state_t mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISABLED;
static bool mqtt_initialized;

static void mqtt_connection_task(void *argument);

static mqtt_prerequisite_status_t mqtt_get_prerequisite_status(void)
{
    if (netif_default == NULL)
    {
        return MQTT_PREREQUISITE_NETWORK_INTERFACE;
    }
    if (!netif_is_up(netif_default))
    {
        return MQTT_PREREQUISITE_NETWORK_INTERFACE_DOWN;
    }
    if (!netif_is_link_up(netif_default))
    {
        return MQTT_PREREQUISITE_NETWORK_LINK_DOWN;
    }
    if (!sntp_service_is_synchronized())
    {
        return MQTT_PREREQUISITE_TIME_SYNC;
    }
    return MQTT_PREREQUISITE_READY;
}

static void mqtt_log_prerequisite_status(mqtt_prerequisite_status_t status)
{
    switch (status)
    {
    case MQTT_PREREQUISITE_NETWORK_INTERFACE:
        debug_log_printf("MQTT waiting: network interface is not initialized\r\n");
        break;

    case MQTT_PREREQUISITE_NETWORK_INTERFACE_DOWN:
        debug_log_printf("MQTT waiting: network interface is down\r\n");
        break;

    case MQTT_PREREQUISITE_NETWORK_LINK_DOWN:
        debug_log_printf("MQTT waiting: Ethernet link is down\r\n");
        break;

    case MQTT_PREREQUISITE_TIME_SYNC:
        debug_log_printf("MQTT waiting: SNTP time is not synchronized\r\n");
        break;

    default:
        break;
    }
}

static bool mqtt_ensure_tls_config(void)
{
    bool result;

    mqtt_tls_require_secure_adapter();
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

static err_t mqtt_resolve_host(ip_addr_t *mqtt_ip)
{
    const char *broker_hostname = (const char *)mqtt_configuration->broker_address.bytes;
    err_t result = netconn_gethostbyname(broker_hostname, mqtt_ip);

    if (result != ERR_OK)
    {
        debug_log_printf("MQTT DNS lookup failed: %s, err=%d\r\n", broker_hostname, (int)result);
        return result;
    }

    debug_log_printf("MQTT DNS resolved: %s -> %s\r\n", broker_hostname, ipaddr_ntoa(mqtt_ip));
    return ERR_OK;
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

    xSemaphoreTake(publish_slot_mutex, portMAX_DELAY);
    callback = publish_slot_cb[index];
    context = publish_slot_context[index];
    publish_slot_available[index] = true;
    xSemaphoreGive(publish_slot_mutex);

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

        xSemaphoreTake(publish_slot_mutex, portMAX_DELAY);
        if (!publish_slot_available[index])
        {
            callback = publish_slot_cb[index];
            context = publish_slot_context[index];
            publish_slot_available[index] = true;
            publish_slot_cb[index] = NULL;
            publish_slot_context[index] = NULL;
        }
        xSemaphoreGive(publish_slot_mutex);

        if (callback != NULL)
        {
            callback(context, result);
        }
    }
}

static void mqtt_disconnect_for_network_loss(void)
{
    LOCK_TCPIP_CORE();
    connected = false;
    if (mqtt_client != NULL)
    {
        mqtt_disconnect(mqtt_client);
    }
    UNLOCK_TCPIP_CORE();

    mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;
    mqtt_complete_publish_slots(MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *argument, mqtt_connection_status_t status)
{
    if (client != mqtt_client || argument != &mqtt_client_info)
    {
        return;
    }

    if (status == MQTT_CONNECT_ACCEPTED)
    {
        connected = true;
        mqtt_publisher_state = MQTT_PUBLISHER_STATE_CONNECTED;
        debug_log_printf("MQTT connection established\r\n");
        mqtt_publish_online_message(client);
        return;
    }

    connected = false;
    mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;

    mqtt_complete_publish_slots(MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED);
    debug_log_printf("MQTT connection unavailable, status=%d\r\n", (int)status);
}

static void mqtt_connection_task(void *argument)
{
    ip_addr_t mqtt_ip;
    err_t connect_result;

    if (argument != NULL)
    {
        mqtt_publisher_state = MQTT_PUBLISHER_STATE_ERROR;
    }

    for (;;)
    {
        mqtt_prerequisite_status_t prerequisite_status;


        prerequisite_status = mqtt_get_prerequisite_status();

        /* 必须先检查线路是否断开。因为线路断开，但mqtt必不能立刻得知connect回调，使得disconnect，最长时间在keepalive时发现断开连接 */

        if (prerequisite_status == MQTT_PREREQUISITE_NETWORK_INTERFACE ||
            prerequisite_status == MQTT_PREREQUISITE_NETWORK_INTERFACE_DOWN ||
            prerequisite_status == MQTT_PREREQUISITE_NETWORK_LINK_DOWN)
        {
            mqtt_disconnect_for_network_loss();
            mqtt_log_prerequisite_status(prerequisite_status);
            vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECTION_RETRY_INTERVAL_MS));
            continue;
        }
        else if(prerequisite_status != MQTT_PREREQUISITE_READY)
        {
            mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;
            mqtt_log_prerequisite_status(prerequisite_status);
            vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECTION_RETRY_INTERVAL_MS));
            continue;
        }

        if (connected)
        {
            vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECTION_RETRY_INTERVAL_MS));
            continue;
        }

        mqtt_publisher_state = MQTT_PUBLISHER_STATE_CONNECTING;
        if (!mqtt_ensure_tls_config() || mqtt_resolve_host(&mqtt_ip) != ERR_OK)
        {
            mqtt_publisher_state = MQTT_PUBLISHER_STATE_ERROR;
            vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECTION_RETRY_INTERVAL_MS));
            continue;
        }

        mqtt_client_info.tls_config = mqtt_tls_config;
        LOCK_TCPIP_CORE();
        if (connected)
        {
            UNLOCK_TCPIP_CORE();
            vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECTION_RETRY_INTERVAL_MS));
            continue;
        }
        connect_result = mqtt_client_connect(mqtt_client, &mqtt_ip, mqtt_configuration->broker_port,
                                             mqtt_connection_cb, &mqtt_client_info, &mqtt_client_info);
        if (connect_result == ERR_ISCONN)
        {
            mqtt_disconnect(mqtt_client);
        }
        UNLOCK_TCPIP_CORE();
        if (connect_result == ERR_OK)
        {
            debug_log_printf("MQTT connection attempt started: %s:%u\r\n",
                             (const char *)mqtt_configuration->broker_address.bytes,
                             (unsigned int)mqtt_configuration->broker_port);
        }
        else
        {
            mqtt_publisher_state = MQTT_PUBLISHER_STATE_ERROR;
            debug_log_printf("MQTT connection attempt rejected, err=%d\r\n", (int)connect_result);
        }
        vTaskDelay(pdMS_TO_TICKS(MQTT_CONNECTION_RETRY_INTERVAL_MS));
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

    if (mqtt_initialized)
    {
        debug_log_printf("MQTT publisher is already initialized\r\n");
        return;
    }
    active_configuration = configuration_service_active();
    if (active_configuration == NULL)
    {
        mqtt_configuration = NULL;
        mqtt_publisher_state = MQTT_PUBLISHER_STATE_ERROR;
        debug_log_printf("MQTT publisher initialization failed: active configuration unavailable\r\n");
        Error_Handler();
        return;
    }

    mqtt_configuration = &active_configuration->mqtt;
    if (mqtt_configuration->mode == CONFIGURATION_MQTT_MODE_DISABLED)
    {
        mqtt_initialized = true;
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

    publish_slot_mutex = xSemaphoreCreateMutexStatic(&publish_slot_mutex_buffer);

    for (index = 0; index < PUBLISH_QUEUE_LENGTH; index++)
    {
        publish_slot_available[index] = true;
        publish_slot_cb[index] = NULL;
        publish_slot_context[index] = NULL;
    }

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    if (mqtt_client == NULL)
    {
        mqtt_publisher_state = MQTT_PUBLISHER_STATE_ERROR;
        debug_log_printf("MQTT publisher initialization failed: MQTT client unavailable\r\n");
        Error_Handler();
    }

    connected = false;
    vTaskSuspendAll();
    connection_task_handle = xTaskCreateStatic(mqtt_connection_task, "mqtt_conn", MQTT_CONNECTION_TASK_STACK_DEPTH,
                                               NULL, MQTT_CONNECTION_TASK_PRIORITY, mqtt_connection_task_stack,
                                               &mqtt_connection_task_buffer);
    mqtt_initialized = true;
    mqtt_publisher_state = MQTT_PUBLISHER_STATE_DISCONNECTED;
    xTaskResumeAll();
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
    if (!mqtt_initialized || mqtt_publisher_state == MQTT_PUBLISHER_STATE_DISABLED)
    {
        return MQTT_PUBLISHER_PUBLISH_DISABLED;
    }
    if (!connected)
    {
        return MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED;
    }

    LOCK_TCPIP_CORE();
    if (!connected)
    {
        UNLOCK_TCPIP_CORE();
        return MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED;
    }

    xSemaphoreTake(publish_slot_mutex, portMAX_DELAY);
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
    xSemaphoreGive(publish_slot_mutex);
    if (index < 0)
    {
        UNLOCK_TCPIP_CORE();
        return MQTT_PUBLISHER_PUBLISH_NO_RESOURCE;
    }

    result = mqtt_publish(mqtt_client, topic, payload, payload_length, qos, retain, mqtt_publish_callback,
                          (void *)(uintptr_t)index);

    if (result != ERR_OK)
    {
        xSemaphoreTake(publish_slot_mutex, portMAX_DELAY);
        publish_slot_available[index] = true;
        xSemaphoreGive(publish_slot_mutex);
    }
    UNLOCK_TCPIP_CORE();

    if (result == ERR_OK)
    {
        return MQTT_PUBLISHER_PUBLISH_OK;
    }
    if (result == ERR_CONN)
    {
        return MQTT_PUBLISHER_PUBLISH_NOT_CONNECTED;
    }
    if (result == ERR_MEM)
    {
        return MQTT_PUBLISHER_PUBLISH_NO_RESOURCE;
    }
    if (result == ERR_ARG)
    {
        return MQTT_PUBLISHER_PUBLISH_INVALID_ARGUMENT;
    }

    debug_log_printf("MQTT publish rejected, result=%d\r\n", (int)result);
    return MQTT_PUBLISHER_PUBLISH_NO_RESOURCE;
}

mqtt_publisher_state_t mqtt_publisher_get_state(void)
{
    return mqtt_publisher_state;
}

#endif
