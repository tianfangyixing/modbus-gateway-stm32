/* Runs the real publisher, SNTP setup and vendor MQTT encoder/parser.
 * Only scheduler, HAL and network/transport boundaries are replaced.
 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "configuration_test_fixtures.h"
#include "lwip/apps/mqtt_priv.h"
#include "lwip/timeouts.h"
#include "lwip/mem.h"
#include "../../MQTT/src/mqtt_publisher.c"
#include "../../SNTP/src/sntp_service.c"

static configuration_t test_configuration;
static jmp_buf task_yield;
static void (*task_entry)(void *);
static struct altcp_pcb transport;
static unsigned char wire[2048];
static size_t wire_length;
static size_t allocated_client_bytes;
static const char *dns_hostname;
static const char *tls_hostname;
static const char *sntp_hostnames[2];
static int tls_configuration_token;
static unsigned int lock_depth;
sys_mutex_t lock_tcpip_core;

const configuration_t *configuration_service_active(void)
{
    return &test_configuration;
}

void Error_Handler(void)
{
    abort();
}

uint32_t HAL_GetUIDw0(void)
{
    return UINT32_C(0x01234567);
}

uint32_t HAL_GetUIDw1(void)
{
    return UINT32_C(0x89ABCDEF);
}

uint32_t HAL_GetUIDw2(void)
{
    return UINT32_C(0x10203040);
}

void debug_log_printf(const char *format, ...)
{
    assert(format != NULL);
}

void watchdog_report(unsigned int bits)
{
    assert(bits == WATCHDOG_EVENT_MQTT);
}

TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t depth, void *arg,
                               UBaseType_t priority, StackType_t *stack, StaticTask_t *buffer)
{
    assert(entry != NULL && strcmp(name, "mqtt_conn") == 0 && depth == 768U);
    assert(arg == NULL && priority == 22U && stack != NULL && buffer != NULL);
    task_entry = entry;
    return buffer;
}

TickType_t xTaskGetTickCount(void)
{
    return 0U;
}

void vTaskDelay(TickType_t ticks)
{
    assert(ticks > 0U && lock_depth == 0U);
    longjmp(task_yield, 1);
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer)
{
    assert(buffer != NULL);
    return buffer;
}

int xSemaphoreTake(SemaphoreHandle_t handle, TickType_t timeout)
{
    assert(handle != NULL && timeout == portMAX_DELAY);
    abort();
}

int xSemaphoreGive(SemaphoreHandle_t handle)
{
    assert(handle != NULL);
    abort();
}

RTC_HandleTypeDef hrtc;

HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t format)
{
    assert(rtc == &hrtc && time != NULL && format == RTC_FORMAT_BIN);
    abort();
}

HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *date, uint32_t format)
{
    assert(rtc == &hrtc && date != NULL && format == RTC_FORMAT_BIN);
    abort();
}

HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t format)
{
    assert(rtc == &hrtc && time != NULL && format == RTC_FORMAT_BIN);
    abort();
}

HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc, RTC_DateTypeDef *date, uint32_t format)
{
    assert(rtc == &hrtc && date != NULL && format == RTC_FORMAT_BIN);
    abort();
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    assert(mutex == &lock_tcpip_core && lock_depth == 0U);
    lock_depth++;
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    assert(mutex == &lock_tcpip_core && lock_depth == 1U);
    lock_depth--;
}

err_t dns_gethostbyname(const char *hostname, ip_addr_t *address, dns_found_callback found, void *arg)
{
    assert(hostname != NULL && address != NULL && found != NULL && arg != NULL);
    dns_hostname = hostname;
    IP4_ADDR(address, 127, 0, 0, 1);
    return ERR_OK;
}

bool mqtt_tls_policy_set_broker_hostname(const char *hostname)
{
    assert(hostname != NULL);
    tls_hostname = hostname;
    return true;
}

void mqtt_tls_policy_clear_broker_hostname(void)
{
    tls_hostname = NULL;
}

struct altcp_tls_config *altcp_tls_create_config_client(const u8_t *ca, size_t length)
{
    assert(ca == test_configuration.mqtt.ca_certificate_pem.bytes);
    assert(length == (size_t)test_configuration.mqtt.ca_certificate_pem.length + 1U);
    assert(ca[length - 1U] == 0U);
    return (struct altcp_tls_config *)&tls_configuration_token;
}

void sntp_setoperatingmode(u8_t mode)
{
    assert(mode == SNTP_OPMODE_POLL);
}

void sntp_setservername(u8_t index, const char *name)
{
    assert(index < 2U && name != NULL);
    sntp_hostnames[index] = name;
}

void sntp_setserver(u8_t index, const ip_addr_t *address)
{
    assert(index < 2U && address != NULL);
    abort();
}

void sntp_init(void)
{
    assert(sntp_hostnames[0] != NULL && sntp_hostnames[1] != NULL);
}

void *mem_calloc(mem_size_t count, mem_size_t size)
{
    assert(count == 1U);
    allocated_client_bytes = size;
    return calloc(count, size);
}

void mem_free(void *memory)
{
    assert(memory != NULL);
    free(memory);
}

struct altcp_pcb *altcp_tls_new(struct altcp_tls_config *configuration, u8_t type)
{
    assert(configuration == (struct altcp_tls_config *)&tls_configuration_token && type == IPADDR_TYPE_V4);
    memset(&transport, 0, sizeof(transport));
    return &transport;
}

struct altcp_pcb *altcp_tcp_new_ip_type(u8_t type)
{
    assert(type == IPADDR_TYPE_V4);
    abort();
}

void altcp_arg(struct altcp_pcb *connection, void *arg)
{
    assert(connection == &transport);
    connection->arg = arg;
}

err_t altcp_bind(struct altcp_pcb *connection, const ip_addr_t *address, u16_t port)
{
    assert(connection == &transport && address != NULL && port == 0U);
    return ERR_OK;
}

err_t altcp_connect(struct altcp_pcb *connection, const ip_addr_t *address, u16_t port,
                    altcp_connected_fn connected)
{
    assert(connection == &transport && address != NULL && port == 8883U && connected != NULL);
    connection->connected = connected;
    return ERR_OK;
}

void altcp_err(struct altcp_pcb *connection, altcp_err_fn callback)
{
    assert(connection == &transport);
    connection->err = callback;
}

void altcp_recv(struct altcp_pcb *connection, altcp_recv_fn callback)
{
    assert(connection == &transport);
    connection->recv = callback;
}

void altcp_sent(struct altcp_pcb *connection, altcp_sent_fn callback)
{
    assert(connection == &transport);
    connection->sent = callback;
}

void altcp_poll(struct altcp_pcb *connection, altcp_poll_fn callback, u8_t interval)
{
    assert(connection == &transport);
    connection->poll = callback;
    connection->pollinterval = interval;
}

u16_t altcp_sndbuf(struct altcp_pcb *connection)
{
    assert(connection == &transport);
    return 14600U;
}

err_t altcp_write(struct altcp_pcb *connection, const void *data, u16_t length, u8_t flags)
{
    assert(connection == &transport && data != NULL && (flags & TCP_WRITE_FLAG_COPY) != 0U);
    assert(wire_length + length <= sizeof(wire));
    memcpy(wire + wire_length, data, length);
    wire_length += length;
    return ERR_OK;
}

err_t altcp_output(struct altcp_pcb *connection)
{
    assert(connection == &transport);
    return ERR_OK;
}

void altcp_recved(struct altcp_pcb *connection, u16_t length)
{
    assert(connection == &transport && length == 4U);
}

err_t altcp_close(struct altcp_pcb *connection)
{
    assert(connection == &transport);
    return ERR_OK;
}

void altcp_abort(struct altcp_pcb *connection)
{
    assert(connection == &transport);
}

void sys_timeout(u32_t milliseconds, sys_timeout_handler handler, void *arg)
{
    assert(milliseconds > 0U && handler != NULL && arg != NULL);
}

void sys_untimeout(sys_timeout_handler handler, void *arg)
{
    assert(handler != NULL && arg != NULL);
}

u8_t pbuf_free(struct pbuf *buffer)
{
    assert(buffer != NULL && buffer->next == NULL);
    return 1U;
}

u8_t pbuf_get_at(const struct pbuf *buffer, u16_t offset)
{
    assert(buffer != NULL && buffer->next == NULL && offset < buffer->len);
    return ((const u8_t *)buffer->payload)[offset];
}

u16_t pbuf_copy_partial(const struct pbuf *buffer, void *destination, u16_t length, u16_t offset)
{
    assert(buffer != NULL && destination != NULL && buffer->next == NULL);
    assert((size_t)offset + length <= buffer->len);
    memcpy(destination, (const u8_t *)buffer->payload + offset, length);
    return length;
}

static void set_hostname(configuration_hostname_t *hostname, char letter)
{
    memset(hostname->bytes, letter, 253U);
    hostname->bytes[63] = '.';
    hostname->bytes[127] = '.';
    hostname->bytes[191] = '.';
    hostname->bytes[253] = 0U;
    hostname->length = 253U;
}

int main(int argc, char **argv)
{
    static const char client_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    static unsigned char connack_bytes[] = {0x20U, 0x02U, 0x00U, 0x00U};
    struct pbuf connack = {0};
    configuration_mqtt_t *configuration = &test_configuration.mqtt;
    FILE *capture;
    bool derived;
    size_t index;

    assert(argc == 3);
    assert(CONFIGURATION_SCHEMA_VERSION == 2U);
    assert(MQTT_PUBLISHER_CONNECT_MAX_PACKET_LENGTH == 1047U);
    derived = strcmp(argv[1], "derived") == 0;
    assert(derived || strcmp(argv[1], "explicit") == 0);
    configuration_test_make_maximum(&test_configuration);
    set_hostname(&configuration->broker_address, 'b');
    for (index = 0U; index < 2U; index++)
    {
        test_configuration.sntp.servers[index].type = CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME;
        set_hostname(&test_configuration.sntp.servers[index].value.hostname, (char)('s' + index));
    }
    configuration->client_id.mode = derived ? CONFIGURATION_CLIENT_ID_MODE_DERIVED :
                                             CONFIGURATION_CLIENT_ID_MODE_EXPLICIT;
    configuration->client_id.explicit_value.length = 256U;
    configuration->username.length = 256U;
    configuration->password.length = 256U;
    for (index = 0U; index < 256U; index++)
    {
        configuration->client_id.explicit_value.bytes[index] = client_alphabet[index % 64U];
        configuration->username.bytes[index] = (uint8_t)(32U + index % 95U);
        configuration->password.bytes[index] = (uint8_t)(126U - index % 95U);
    }
    configuration->client_id.explicit_value.bytes[256] = 0U;
    configuration->username.bytes[256] = 0U;
    configuration->password.bytes[256] = 0U;
    configuration->will_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM;
    configuration->will_message.topic.length = 128U;
    configuration->will_message.payload.length = 128U;
    memset(configuration->will_message.topic.bytes, 'T', 128U);
    memset(configuration->will_message.payload.bytes, 'P', 128U);
    configuration->will_message.topic.bytes[128] = 0U;
    configuration->will_message.payload.bytes[128] = 0U;
    configuration->will_message.qos = 2U;
    configuration->will_message.retain = 1U;
    configuration->keep_alive_seconds = 60U;
    configuration->broker_port = 8883U;
    configuration->online_message.mode = CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED;
    assert(configuration_validate(&test_configuration) == CONFIGURATION_VALIDATION_OK);

    sntp_service_init();
    for (index = 0U; index < 2U; index++)
    {
        assert(sntp_hostnames[index] == (const char *)test_configuration.sntp.servers[index].value.hostname.bytes);
        assert(strlen(sntp_hostnames[index]) == 253U);
    }
    is_synchronized = true;
    mqtt_publisher_init();
    assert(task_entry != NULL);
    assert(allocated_client_bytes == sizeof(mqtt_client_t));
    assert(mqtt_client_info.client_user == (const char *)configuration->username.bytes);
    assert(mqtt_client_info.client_pass == (const char *)configuration->password.bytes);
    if (!derived)
    {
        assert(mqtt_client_info.client_id == (const char *)configuration->client_id.explicit_value.bytes);
    }
    assert(strlen(mqtt_client_info.client_id) == (derived ? 23U : 256U));
    if (setjmp(task_yield) == 0)
    {
        task_entry(NULL);
        abort();
    }
    assert(dns_hostname == (const char *)configuration->broker_address.bytes);
    assert(tls_hostname == dns_hostname && strlen(dns_hostname) == 253U);
    assert(mqtt_publisher_get_state() == MQTT_PUBLISHER_STATE_CONNECTING);
    assert(transport.connected != NULL);
    assert(transport.connected(transport.arg, &transport, ERR_OK) == ERR_OK);
    assert(wire_length == (derived ? 814U : 1047U));
    connack.payload = connack_bytes;
    connack.len = sizeof(connack_bytes);
    connack.tot_len = sizeof(connack_bytes);
    assert(transport.recv(transport.arg, &transport, &connack, ERR_OK) == ERR_OK);
    assert(mqtt_publisher_get_state() == MQTT_PUBLISHER_STATE_CONNECTED);
    capture = fopen(argv[2], "wb");
    assert(capture != NULL);
    assert(fwrite(wire, 1U, wire_length, capture) == wire_length);
    assert(fclose(capture) == 0);
    printf("PASS %s CONNECT=%zu, client allocation=%zu, DNS/TLS/SNTP hostnames=253\n",
           argv[1], wire_length, allocated_client_bytes);
    mqtt_disconnect(mqtt_client);
    mqtt_client_free(mqtt_client);
    return 0;
}
