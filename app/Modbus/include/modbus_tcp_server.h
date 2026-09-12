#ifndef MODBUS_TCP_SERVER_H
#define MODBUS_TCP_SERVER_H

#include "modbus_rtu_adu_pool.h"
#include "modbus_rtu_transaction_scheduler.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "lwip/errno.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>


#include <stdbool.h>
#include <stdint.h>

#define MODBUS_TCP_SERVER_MAX_CLIENTS 4U
#define MODBUS_TCP_ADU_MAX_LENGTH (260)
#define MODBUS_TCP_RESPONSE_TIMEOUT_MIN_MS UINT32_C(50)
#define MODBUS_TCP_RESPONSE_TIMEOUT_MAX_MS UINT32_C(60000)

typedef enum
{
    MODBUS_TCP_SERVER_OK = 0,
    MODBUS_TCP_SERVER_INVALID_ARGUMENT,
    MODBUS_TCP_SERVER_INVALID_STATE,
    MODBUS_TCP_SERVER_TASK_CREATE_FAILED
} modbus_tcp_server_result_t;

typedef enum
{
    MODBUS_TCP_SERVER_STATE_UNINITIALIZED = 0,
    MODBUS_TCP_SERVER_STATE_RUNNING,
    MODBUS_TCP_SERVER_STATE_FAILED
} modbus_tcp_server_state_t;


typedef struct
{
    QueueHandle_t request_queue;
    QueueHandle_t response_queue;
    uint16_t listen_port;
    uint32_t response_timeout_ms;
    const char *task_name;
    UBaseType_t task_priority;
    StackType_t *task_stack;
    uint32_t task_stack_depth;
    StaticTask_t *task_buffer;
} modbus_tcp_server_config_t;

typedef enum
{
    /* 正在接收并组装 Modbus TCP ADU。 */
    MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST,

    /* 已收到完整且合法的请求，等待提交到 RTU 调度队列。 */
    MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_SUBMIT,

    /* 请求已提交到 RTU 调度器，等待 RTU 响应。 */
    MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_RESPONSE,

    /* 已有正常响应或本地异常响应，正在向 TCP 客户端发送。 */
    MODBUS_TCP_SERVER_CLIENT_STATE_SENDING_TCP_RESPONSE
} modbus_tcp_server_client_state_t;

typedef struct
{
    int socket;
    modbus_tcp_server_client_state_t state;

    uint8_t recv_buffer[MODBUS_TCP_ADU_MAX_LENGTH];
    uint16_t recv_count; // 已接收字节数量
    uint32_t last_recv_time;

    modbus_rtu_transaction_scheduler_request_t rtu_request;

    uint8_t send_buffer[MODBUS_TCP_ADU_MAX_LENGTH];
    uint16_t send_length; // 需要发送的总长度
    uint16_t send_offset; // 已经发送的长度
    uint32_t send_start_time_ms;
} modbus_tcp_server_client_t;

typedef struct
{
    modbus_tcp_server_state_t state;
    uint16_t listener_port;
    uint32_t response_timeout_ms;
    QueueHandle_t request_queue;
    QueueHandle_t response_queue;
    TaskHandle_t task_handle;

    uint32_t available_token;

    fd_set read_sockets;
    fd_set write_sockets;
    
    int server_socket;
    modbus_tcp_server_client_t clients[MODBUS_TCP_SERVER_MAX_CLIENTS];
} modbus_tcp_server_t;

/**
  * @brief Initialize and start a Modbus TCP server using externally owned scheduler queues.
  * @pre server is zero-initialized and both queues are bound to a running transaction scheduler.
  */
modbus_tcp_server_result_t modbus_tcp_server_init(modbus_tcp_server_t *server,
                                                  const modbus_tcp_server_config_t *config);

#endif
