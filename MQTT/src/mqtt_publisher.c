#include "mqtt_publisher.h"

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "debug_log.h"
#include "lwip/altcp_tls.h"
#include "lwip/api.h"
#include "lwip/apps/mqtt.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "mqtt_root_ca.h"
#include "mqtt_tls_policy.h"
#include "sntp_service.h"
#include "task.h"

#define MQTT_CONNECTION_TASK_STACK_DEPTH 512U
#define MQTT_CONNECTION_TASK_PRIORITY 22U
#define MQTT_PUBLISHER_TASK_STACK_DEPTH 256U
#define MQTT_PUBLISHER_TASK_PRIORITY 21U
#define MQTT_NETWORK_WAIT_MS 200U
#define MQTT_TIME_SYNC_WAIT_MS 1000U
#define MQTT_PREREQUISITE_LOG_INTERVAL_MS 5000U
#define MQTT_CONNECT_TIMEOUT_MS 15000U
#define MQTT_CONNECTION_MONITOR_MS 1000U
#define MQTT_RECONNECT_DELAY_MIN_MS 1000U
#define MQTT_RECONNECT_DELAY_MAX_MS 60000U
#define MQTT_CONNECTION_STATUS_PENDING (-1)
#define MQTT_PUBLISHER_DELAY_MS 1000U
#define MQTT_PUBLISHER_TOPIC "devices/stm32-001/msg"
#define MQTT_PUBLISHER_QOS 1U
#define MQTT_PUBLISHER_RETAIN 0U

typedef struct
{
    const char *payload;
    u16_t length;
} mqtt_publisher_message_t;

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
static TaskHandle_t mqtt_connection_task_handle;
static StackType_t mqtt_publisher_task_stack[MQTT_PUBLISHER_TASK_STACK_DEPTH];
static StaticTask_t mqtt_publisher_task_buffer;
static TaskHandle_t mqtt_publisher_task_handle;
static mqtt_client_t *mqtt_client;
static struct altcp_tls_config *mqtt_tls_config;
static volatile int32_t mqtt_last_connection_status = MQTT_CONNECTION_STATUS_PENDING;
static volatile BaseType_t mqtt_publish_in_flight;
static volatile err_t mqtt_publish_completion_result = ERR_CONN;

static const mqtt_publisher_message_t mqtt_publisher_messages[] =
{
    {"i", 1U},
    {"am", 2U},
    {"tianfang", 8U}
};

static struct mqtt_connect_client_info_t mqtt_client_info =
{
    "stm32-001",
    "stm32-001",
    "E>6!7p0~xLxcN7ajL?o00~u1nqe?G7q:4rpH)rR^i)Qt2L+M!",
    60,
    NULL,
    NULL,
    0,
    0,
    NULL
};

static const char mqtt_hostname[] = MQTT_TLS_SERVER_NAME;
static const u16_t mqtt_port = 8883U;

static void mqtt_connection_task(void *argument);
static void mqtt_publisher_task(void *argument);

static BaseType_t mqtt_network_is_ready(void)
{
    if (netif_default == NULL || !netif_is_up(netif_default) || !netif_is_link_up(netif_default))
    {
        return pdFALSE;
    }

    return pdTRUE;
}

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
            debug_log_printf("MQTT waiting: network interface is not initialized\n");
            break;

        case MQTT_PREREQUISITE_NETWORK_INTERFACE_DOWN:
            debug_log_printf("MQTT waiting: network interface is down\n");
            break;

        case MQTT_PREREQUISITE_NETWORK_LINK_DOWN:
            debug_log_printf("MQTT waiting: Ethernet link is down\n");
            break;

        case MQTT_PREREQUISITE_TIME_SYNC:
            debug_log_printf("MQTT waiting: SNTP time is not synchronized\n");
            break;

        default:
            break;
    }
}

static void mqtt_wait_for_prerequisites(void)
{
    mqtt_prerequisite_status_t previous_status = MQTT_PREREQUISITE_READY;
    TickType_t previous_log_tick = 0U;

    for (;;)
    {
        mqtt_prerequisite_status_t status = mqtt_get_prerequisite_status();
        TickType_t current_tick = xTaskGetTickCount();

        if (status == MQTT_PREREQUISITE_READY)
        {
            debug_log_printf("MQTT prerequisites ready\n");
            return;
        }

        if (status != previous_status ||
            current_tick - previous_log_tick >= pdMS_TO_TICKS(MQTT_PREREQUISITE_LOG_INTERVAL_MS))
        {
            mqtt_log_prerequisite_status(status);
            previous_status = status;
            previous_log_tick = current_tick;
        }

        if (status == MQTT_PREREQUISITE_TIME_SYNC)
        {
            vTaskDelay(pdMS_TO_TICKS(MQTT_TIME_SYNC_WAIT_MS));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(MQTT_NETWORK_WAIT_MS));
        }
    }
}

static BaseType_t mqtt_ensure_tls_config(void)
{
    mqtt_tls_require_secure_adapter();

    if (mqtt_tls_config != NULL)
    {
        return pdTRUE;
    }

    LOCK_TCPIP_CORE();
    mqtt_tls_config = altcp_tls_create_config_client(mqtt_root_ca_pem, mqtt_root_ca_pem_size);
    UNLOCK_TCPIP_CORE();

    if (mqtt_tls_config == NULL)
    {
        debug_log_printf("MQTT TLS config creation failed\n");
        return pdFALSE;
    }

    return pdTRUE;
}

static err_t mqtt_resolve_host(ip_addr_t *mqtt_ip)
{
    err_t result = netconn_gethostbyname(mqtt_hostname, mqtt_ip);

    if (result != ERR_OK)
    {
        debug_log_printf("MQTT DNS lookup failed: %s, err %d\n", mqtt_hostname, (int)result);
        return result;
    }

    debug_log_printf("MQTT DNS resolved: %s -> %s\n", mqtt_hostname, ipaddr_ntoa(mqtt_ip));
    return ERR_OK;
}

static void mqtt_publish_complete(err_t result)
{
    if (mqtt_publish_in_flight == pdFALSE)
    {
        return;
    }

    mqtt_publish_in_flight = pdFALSE;
    mqtt_publish_completion_result = result;

    if (mqtt_publisher_task_handle != NULL)
    {
        xTaskNotifyGive(mqtt_publisher_task_handle);
    }
}

static void mqtt_cleanup_client(void)
{
    mqtt_client_t *client;

    LOCK_TCPIP_CORE();
    client = mqtt_client;
    mqtt_client = NULL;
    mqtt_publish_complete(ERR_CONN);

    if (client != NULL)
    {
        mqtt_disconnect(client);
        mqtt_client_free(client);
    }
    UNLOCK_TCPIP_CORE();
}

static BaseType_t mqtt_client_is_connected_safe(void)
{
    BaseType_t is_connected = pdFALSE;

    LOCK_TCPIP_CORE();
    if (mqtt_client != NULL && mqtt_client_is_connected(mqtt_client) != 0U)
    {
        is_connected = pdTRUE;
    }
    UNLOCK_TCPIP_CORE();

    return is_connected;
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;
    LWIP_UNUSED_ARG(data);

    debug_log_printf("MQTT client \"%s\" data cb: len %d, flags %d\n", client_info->client_id, (int)len, (int)flags);
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;

    debug_log_printf("MQTT client \"%s\" publish cb: topic %s, len %d\n", client_info->client_id, topic, (int)tot_len);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;

    mqtt_last_connection_status = (int32_t)status;
    debug_log_printf("MQTT client \"%s\" connection status: %d\n", client_info->client_id, (int)status);

    if (status != MQTT_CONNECT_ACCEPTED)
    {
        mqtt_publish_complete(ERR_CONN);
    }

    if (mqtt_connection_task_handle != NULL)
    {
        xTaskNotifyGive(mqtt_connection_task_handle);
    }
}

static void mqtt_publish_request_cb(void *arg, err_t err)
{
    LWIP_UNUSED_ARG(arg);
    mqtt_publish_complete(err);
}

static err_t mqtt_start_connection(const ip_addr_t *mqtt_ip)
{
    err_t connect_result = ERR_MEM;

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    if (mqtt_client != NULL)
    {
        mqtt_client_info.tls_config = mqtt_tls_config;
        connect_result = mqtt_client_connect(mqtt_client, mqtt_ip, mqtt_port, mqtt_connection_cb,
                                             LWIP_CONST_CAST(void *, &mqtt_client_info), &mqtt_client_info);
        if (connect_result == ERR_OK)
        {
            mqtt_set_inpub_callback(mqtt_client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb,
                                    LWIP_CONST_CAST(void *, &mqtt_client_info));
        }
    }
    UNLOCK_TCPIP_CORE();

    return connect_result;
}

static void mqtt_delay_before_reconnect(uint32_t *delay_ms)
{
    debug_log_printf("MQTT reconnecting in %lu ms\n", (unsigned long)*delay_ms);
    vTaskDelay(pdMS_TO_TICKS(*delay_ms));

    if (*delay_ms < MQTT_RECONNECT_DELAY_MAX_MS / 2U)
    {
        *delay_ms *= 2U;
    }
    else
    {
        *delay_ms = MQTT_RECONNECT_DELAY_MAX_MS;
    }
}

static void mqtt_connection_task(void *argument)
{
    uint32_t reconnect_delay_ms = MQTT_RECONNECT_DELAY_MIN_MS;
    ip_addr_t mqtt_ip;

    LWIP_UNUSED_ARG(argument);
    mqtt_connection_task_handle = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        err_t connect_result;
        int32_t connection_status;

        mqtt_wait_for_prerequisites();

        if (mqtt_ensure_tls_config() == pdFALSE)
        {
            mqtt_delay_before_reconnect(&reconnect_delay_ms);
            continue;
        }

        if (mqtt_resolve_host(&mqtt_ip) != ERR_OK)
        {
            mqtt_delay_before_reconnect(&reconnect_delay_ms);
            continue;
        }

        ulTaskNotifyTake(pdTRUE, 0U);
        mqtt_last_connection_status = MQTT_CONNECTION_STATUS_PENDING;
        debug_log_printf("MQTT connecting to %s:%u, timeout %lu ms\n", ipaddr_ntoa(&mqtt_ip), (unsigned int)mqtt_port,
                  (unsigned long)MQTT_CONNECT_TIMEOUT_MS);

        connect_result = mqtt_start_connection(&mqtt_ip);
        if (connect_result != ERR_OK)
        {
            debug_log_printf("MQTT connect request failed: err %d\n", (int)connect_result);
            mqtt_cleanup_client();
            mqtt_delay_before_reconnect(&reconnect_delay_ms);
            continue;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS)) == 0U)
        {
            debug_log_printf("MQTT TCP/TLS connect timed out after %lu ms\n", (unsigned long)MQTT_CONNECT_TIMEOUT_MS);
            mqtt_cleanup_client();
            mqtt_delay_before_reconnect(&reconnect_delay_ms);
            continue;
        }

        connection_status = mqtt_last_connection_status;
        if (connection_status != MQTT_CONNECT_ACCEPTED || mqtt_client_is_connected_safe() == pdFALSE)
        {
            debug_log_printf("MQTT connection attempt failed: status %ld\n", (long)connection_status);
            mqtt_cleanup_client();
            mqtt_delay_before_reconnect(&reconnect_delay_ms);
            continue;
        }

        debug_log_printf("MQTT connection established\n");
        reconnect_delay_ms = MQTT_RECONNECT_DELAY_MIN_MS;

        while (mqtt_network_is_ready() != pdFALSE && mqtt_client_is_connected_safe() != pdFALSE)
        {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_CONNECTION_MONITOR_MS)) != 0U &&
                mqtt_last_connection_status != MQTT_CONNECT_ACCEPTED)
            {
                break;
            }
        }

        if (mqtt_network_is_ready() == pdFALSE)
        {
            debug_log_printf("MQTT network link unavailable\n");
        }
        else
        {
            debug_log_printf("MQTT connection lost: status %ld\n", (long)mqtt_last_connection_status);
        }

        mqtt_cleanup_client();
        mqtt_delay_before_reconnect(&reconnect_delay_ms);
    }
}

void mqtt_example_init(void)
{
    if (mqtt_connection_task_handle != NULL || mqtt_publisher_task_handle != NULL)
    {
        debug_log_printf("MQTT publisher already initialized\n");
        return;
    }

    mqtt_publisher_task_handle = xTaskCreateStatic(mqtt_publisher_task, "mqtt_pub", MQTT_PUBLISHER_TASK_STACK_DEPTH,
                                                    NULL, MQTT_PUBLISHER_TASK_PRIORITY, mqtt_publisher_task_stack,
                                                    &mqtt_publisher_task_buffer);
    if (mqtt_publisher_task_handle == NULL)
    {
        debug_log_printf("MQTT publisher task creation failed\n");
        return;
    }

    mqtt_connection_task_handle = xTaskCreateStatic(mqtt_connection_task, "mqtt_conn", MQTT_CONNECTION_TASK_STACK_DEPTH,
                                                     NULL, MQTT_CONNECTION_TASK_PRIORITY, mqtt_connection_task_stack,
                                                     &mqtt_connection_task_buffer);
    if (mqtt_connection_task_handle == NULL)
    {
        debug_log_printf("MQTT connection task creation failed\n");
        return;
    }

    debug_log_printf("MQTT tasks started\n");
}

static void mqtt_publisher_task(void *argument)
{
    size_t message_index = 0U;

    LWIP_UNUSED_ARG(argument);
    mqtt_publisher_task_handle = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        const mqtt_publisher_message_t *message = &mqtt_publisher_messages[message_index];
        err_t publish_result = ERR_CONN;

        ulTaskNotifyTake(pdTRUE, 0U);

        LOCK_TCPIP_CORE();
        if (mqtt_client != NULL && mqtt_client_is_connected(mqtt_client) != 0U)
        {
            mqtt_publish_completion_result = ERR_CONN;
            mqtt_publish_in_flight = pdTRUE;
            publish_result = mqtt_publish(mqtt_client, MQTT_PUBLISHER_TOPIC, message->payload, message->length,
                                          MQTT_PUBLISHER_QOS, MQTT_PUBLISHER_RETAIN, mqtt_publish_request_cb, NULL);
            if (publish_result != ERR_OK)
            {
                mqtt_publish_in_flight = pdFALSE;
            }
        }
        UNLOCK_TCPIP_CORE();

        if (publish_result == ERR_OK)
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            if (mqtt_publish_completion_result == ERR_OK)
            {
                message_index++;
                if (message_index >= LWIP_ARRAYSIZE(mqtt_publisher_messages))
                {
                    message_index = 0U;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISHER_DELAY_MS));
    }
}
