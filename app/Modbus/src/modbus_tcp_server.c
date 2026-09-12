#include "modbus_tcp_server.h"
#include "modbus_common.h"
#include "system_time.h"
#include "configuration_service.h"
#include "watchdog.h"

#define MODBUS_TCP_MIN_MBAP_LENGTH UINT16_C(2)
#define MODBUS_TCP_MAX_MBAP_LENGTH UINT16_C(254)

#define MODBUS_TCP_ADU_LENGTH_FIELD_INDEX (4)
#define MODBUS_TCP_ADU_UNIT_ID_INDEX (6)
#define MODBUS_TCP_ADU_FUNCTION_CODE_INDEX (7)
#define MODBUS_TCP_ADU_EXCEPTION_CODE_INDEX (8)
#define MODBUS_TCP_ADU_PROTOCOL_IDENTIFIER_INDEX (2)
#define MODBUS_TCP_ADU_TRANSACTION_IDENTIFIER_INDEX (0)

#define MODBUS_TCP_ADU_MBAP_HEADER_LENGTH UINT16_C(7)


#define MODBUS_TCP_SELECT_WAIT_US 10000L




#define MODBUS_TCP_KEEPALIVE_IDLE_SECONDS 60
#define MODBUS_TCP_KEEPALIVE_INTERVAL_SECONDS 10
#define MODBUS_TCP_KEEPALIVE_PROBE_COUNT 3


#define MODBUS_EXCEPTION_CODE_ILLEGAL_FUNCTION (0x01)
#define MODBUS_EXCEPTION_CODE_ILLEGAL_DATA_ADDRESS (0x02)
#define MODBUS_EXCEPTION_CODE_ILLEGAL_DATA_VALUE (0x03)
#define MODBUS_EXCEPTION_CODE_SERVER_DEVICE_FAILURE (0x04)
#define MODBUS_EXCEPTION_CODE_GATEWAY_PATH_UNAVAILABLE (0x0A)
#define MODBUS_EXCEPTION_CODE_GATEWAY_TARGET_FAILED_TO_RESPONSE (0x0B)

#define MODBUS_TCP_READ_TIMEOUT_MS (5000)
#define MODBUS_TCP_WRITE_TIMEOUT_MS (5000)


static uint32_t server_get_available_token(modbus_tcp_server_t *server)
{
    return server->available_token++;
}

static void server_accept(modbus_tcp_server_t *server)
{
    int accepted_socket = accept(server->server_socket, NULL, NULL);
    if (accepted_socket < 0)
    {
        //printf("select failed: errno=%d (%s)\n", errno, strerror(errno));
        return;
    }

    size_t client_index;

    for (client_index = 0; client_index < MODBUS_TCP_SERVER_MAX_CLIENTS; client_index++)
    {
        if (server->clients[client_index].socket < 0)
        {
            break;
        }
    }

    if (client_index == MODBUS_TCP_SERVER_MAX_CLIENTS)
    {
        close(accepted_socket);
        //printf("该路连接数已满，拒绝本次连接");
        return;
    }

    const int enabled = 1;
    const int idle_seconds = MODBUS_TCP_KEEPALIVE_IDLE_SECONDS;
    const int interval_seconds = MODBUS_TCP_KEEPALIVE_INTERVAL_SECONDS;
    const int probe_count = MODBUS_TCP_KEEPALIVE_PROBE_COUNT;

    if (fcntl(accepted_socket, F_SETFL, O_NONBLOCK) != 0 ||
        setsockopt(accepted_socket, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) != 0 ||
        setsockopt(accepted_socket, IPPROTO_TCP, TCP_KEEPIDLE, &idle_seconds, sizeof(idle_seconds)) != 0 ||
        setsockopt(accepted_socket, IPPROTO_TCP, TCP_KEEPINTVL, &interval_seconds, sizeof(interval_seconds)) != 0 ||
        setsockopt(accepted_socket, IPPROTO_TCP, TCP_KEEPCNT, &probe_count, sizeof(probe_count)) != 0)
    {
        close(accepted_socket);
        return;
    }


    server->clients[client_index].socket = accepted_socket;
    server->clients[client_index].state = MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST;
    server->clients[client_index].recv_count = 0;

}

static void client_close(modbus_tcp_server_t *server, size_t client_index)
{
    modbus_tcp_server_client_t *client = &server->clients[client_index];

    if (client->state == MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_SUBMIT)
    {
        if (client->rtu_request.rtu_adu)
        {
            modbus_rtu_adu_pool_release(client->rtu_request.rtu_adu);
            client->rtu_request.rtu_adu = NULL;
        }
    }

    if(client->socket >= 0)
    {
        close(client->socket);
        client->socket = -1;
    }
}

static void client_set_send_exception_response(modbus_tcp_server_client_t *client, uint8_t exception_code)
{
    client->send_length = 9;
    client->send_offset = 0;
    write_u16_be(&client->send_buffer[MODBUS_TCP_ADU_TRANSACTION_IDENTIFIER_INDEX], read_u16_be(&client->recv_buffer[MODBUS_TCP_ADU_TRANSACTION_IDENTIFIER_INDEX]));
    write_u16_be(&client->send_buffer[MODBUS_TCP_ADU_PROTOCOL_IDENTIFIER_INDEX], 0x0000);
    write_u16_be(&client->send_buffer[MODBUS_TCP_ADU_LENGTH_FIELD_INDEX], 0x0003);
    client->send_buffer[MODBUS_TCP_ADU_UNIT_ID_INDEX] = client->recv_buffer[MODBUS_TCP_ADU_UNIT_ID_INDEX];
    client->send_buffer[MODBUS_TCP_ADU_FUNCTION_CODE_INDEX] = client->recv_buffer[MODBUS_TCP_ADU_FUNCTION_CODE_INDEX] | 0x80;
    client->send_buffer[MODBUS_TCP_ADU_EXCEPTION_CODE_INDEX] = exception_code;
    client->send_start_time_ms = system_get_ms();
    client->state = MODBUS_TCP_SERVER_CLIENT_STATE_SENDING_TCP_RESPONSE;
}


static uint16_t get_protocol_id(uint8_t *buffer)
{
    return read_u16_be(&buffer[MODBUS_TCP_ADU_PROTOCOL_IDENTIFIER_INDEX]);
}

static uint16_t get_length_field(uint8_t *buffer)
{
    return read_u16_be(&buffer[MODBUS_TCP_ADU_LENGTH_FIELD_INDEX]);
}

static void client_recv_tcp_request(modbus_tcp_server_t *server, size_t client_index)
{
    modbus_tcp_server_client_t *client = &server->clients[client_index];

    if (!FD_ISSET(client->socket, &server->read_sockets))
    {
        if (client->recv_count >= 1 && system_time_elapsed_ms(client->last_recv_time) > MODBUS_TCP_READ_TIMEOUT_MS)
        {
            client_close(server, client_index);
        }
        return;
    }

    uint16_t last_recv_count = client->recv_count;
    uint16_t max_bytes;
    if (last_recv_count < MODBUS_TCP_ADU_MBAP_HEADER_LENGTH)
    {
        max_bytes = MODBUS_TCP_ADU_MBAP_HEADER_LENGTH - last_recv_count;
    }
    else
    {
        max_bytes = get_length_field(client->recv_buffer) + 6 - last_recv_count;
    }

    int received = recv(client->socket, &client->recv_buffer[last_recv_count], max_bytes, 0);

    if (received == 0)
    {
        client_close(server, client_index);
        return;
    }
    else if (received < 0)
    {
        int err = errno;

        if (err != EAGAIN && err != EWOULDBLOCK)
        {
            client_close(server, client_index);
        }
        return;
    }

    client->last_recv_time = system_get_ms();
    client->recv_count += received;

    // 完整获得MBAP头的第一时间
    if (last_recv_count < MODBUS_TCP_ADU_MBAP_HEADER_LENGTH && client->recv_count >= MODBUS_TCP_ADU_MBAP_HEADER_LENGTH)
    {
        if (get_length_field(client->recv_buffer) > MODBUS_TCP_MAX_MBAP_LENGTH ||
            get_length_field(client->recv_buffer) < MODBUS_TCP_MIN_MBAP_LENGTH ||
            get_protocol_id(client->recv_buffer) != 0x00)
        {
            client_close(server, client_index);
        }
    }
    else if (client->recv_count >= MODBUS_TCP_ADU_MBAP_HEADER_LENGTH &&
             client->recv_count == get_length_field(client->recv_buffer) + MODBUS_TCP_ADU_MBAP_HEADER_LENGTH - 1)
    {
        modbus_rtu_adu_t *rtu_adu = modbus_rtu_adu_pool_allocate();
        if (!rtu_adu)
        {
            client_set_send_exception_response(client, MODBUS_EXCEPTION_CODE_SERVER_DEVICE_FAILURE);
            return;
        }

        // 组成rtu包

        rtu_adu->length = get_length_field(client->recv_buffer) + MODBUS_RTU_CRC_LENGTH;

        for (size_t i = 0; i < rtu_adu->length - MODBUS_RTU_CRC_LENGTH; i++)
        {
            rtu_adu->data[i] = client->recv_buffer[i + MODBUS_TCP_ADU_UNIT_ID_INDEX];
        }

        uint16_t crc = calculate_crc(rtu_adu->data, rtu_adu->length - MODBUS_RTU_CRC_LENGTH);
        rtu_adu->data[rtu_adu->length - 1] = (uint8_t)(crc >> 8);
        rtu_adu->data[rtu_adu->length - 2] = (uint8_t)crc;

        // 验证包的合法性
        modbus_rtu_result_t validate_result = modbus_rtu_validate_request(rtu_adu);
        uint8_t exception_code = 0x00;
        switch (validate_result)
        {
        case MODBUS_RTU_OK:
            break;
        case MODBUS_RTU_FUNCTION_UNSUPPORTED:
            exception_code = MODBUS_EXCEPTION_CODE_ILLEGAL_FUNCTION;
            break;

        case MODBUS_RTU_SLAVE_ADDRESS_INVALID:
            exception_code = MODBUS_EXCEPTION_CODE_GATEWAY_PATH_UNAVAILABLE;
            break;
        case MODBUS_RTU_ADDRESS_RANGE_INVALID:

            exception_code = MODBUS_EXCEPTION_CODE_ILLEGAL_DATA_ADDRESS;
            break;
        case MODBUS_RTU_BYTE_COUNT_INVALID:
        case MODBUS_RTU_VALUE_INVALID:
        case MODBUS_RTU_QUANTITY_INVALID:
        case MODBUS_RTU_LENGTH_INVALID:

            exception_code = MODBUS_EXCEPTION_CODE_ILLEGAL_DATA_VALUE;
            break;
        default:
            exception_code = MODBUS_EXCEPTION_CODE_SERVER_DEVICE_FAILURE;
            break;
        }

        if (exception_code != 0x00)
        {
            client_set_send_exception_response(client, exception_code);
            modbus_rtu_adu_pool_release(rtu_adu);
            return;
        }

        // 转换至rtu请求发送状态
        client->rtu_request.response_timeout_ms = server->response_timeout_ms;
        client->rtu_request.rtu_adu = rtu_adu;
        client->rtu_request.token = server_get_available_token(server);
        client->state = MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_SUBMIT;
    }
}

static void client_submit_rtu_request(modbus_tcp_server_t *server, size_t client_index)
{
    modbus_tcp_server_client_t *client = &server->clients[client_index];

    if (xQueueSend(server->request_queue, &client->rtu_request, 0) != pdPASS)
    {
        return;
    }

    client->state = MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_RESPONSE;
    client->rtu_request.rtu_adu = NULL;
}

static void client_recv_rtu_response(modbus_tcp_server_t *server, size_t client_index)
{
    modbus_tcp_server_client_t *client = &server->clients[client_index];
    modbus_rtu_transaction_scheduler_response_t response;

    if (xQueuePeek(server->response_queue, &response, 0) == pdPASS)
    {
        if (response.token != client->rtu_request.token)
        {
            return;
        }

        xQueueReceive(server->response_queue, &response, 0);

        uint8_t exception_code = 0x00;

        switch (response.result)
        {
        case MODBUS_RTU_TRANSACTION_OK:
        case MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE:
            break;
        case MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR:
            exception_code = MODBUS_EXCEPTION_CODE_GATEWAY_PATH_UNAVAILABLE;
            break;
        case MODBUS_RTU_TRANSACTION_RESPONSE_TIMEOUT:
        case MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID:
        case MODBUS_RTU_TRANSACTION_RESPONSE_CRC_INVALID:
        case MODBUS_RTU_TRANSACTION_RESPONSE_SLAVE_ADDRESS_MISMATCH:
        case MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_MISMATCH:
        case MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_INVALID:
        case MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH:
        case MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID:
            exception_code = MODBUS_EXCEPTION_CODE_GATEWAY_TARGET_FAILED_TO_RESPONSE;
            break;
        default:
            exception_code = MODBUS_EXCEPTION_CODE_SERVER_DEVICE_FAILURE;
            break;
        }

        if (exception_code != 0x00)
        {
            client_set_send_exception_response(client, exception_code);
            modbus_rtu_adu_pool_release(response.rtu_adu);
            return;
        }

        // 转发rtu的响应包（异常响应或正常响应）
        client->send_length = response.rtu_adu->length - MODBUS_RTU_CRC_LENGTH + 6;
        client->send_offset = 0;
        write_u16_be(&client->send_buffer[MODBUS_TCP_ADU_TRANSACTION_IDENTIFIER_INDEX], read_u16_be(&client->recv_buffer[MODBUS_TCP_ADU_TRANSACTION_IDENTIFIER_INDEX]));
        write_u16_be(&client->send_buffer[MODBUS_TCP_ADU_PROTOCOL_IDENTIFIER_INDEX], 0x0000);
        write_u16_be(&client->send_buffer[MODBUS_TCP_ADU_LENGTH_FIELD_INDEX], response.rtu_adu->length - MODBUS_RTU_CRC_LENGTH);
        for (size_t i = 0; i < response.rtu_adu->length - MODBUS_RTU_CRC_LENGTH; i++)
        {
            client->send_buffer[i + MODBUS_TCP_ADU_UNIT_ID_INDEX] = response.rtu_adu->data[i];
        }
        client->state = MODBUS_TCP_SERVER_CLIENT_STATE_SENDING_TCP_RESPONSE;
        client->send_start_time_ms = system_get_ms();

        modbus_rtu_adu_pool_release(response.rtu_adu);
    }
}

static void client_send_tcp_response(modbus_tcp_server_t *server, size_t client_index)
{
    modbus_tcp_server_client_t *client = &server->clients[client_index];

    if (!FD_ISSET(client->socket, &server->write_sockets))
    {
        if (system_time_elapsed_ms(client->send_start_time_ms) >= MODBUS_TCP_WRITE_TIMEOUT_MS)
        {
            client_close(server, client_index);
        }
        return;
    }

    int sent = send(client->socket, client->send_buffer + client->send_offset, client->send_length - client->send_offset, 0);
    if (sent < 0)
    {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            client_close(server, client_index);
        }
        return;
    }
    else if (sent == 0)
    {
        return;
    }

    client->send_offset += sent;

    if (client->send_offset == client->send_length)
    {
        client->state = MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST;
        client->recv_count = 0;
    }
}

static void clients_update(modbus_tcp_server_t *server)
{
    for (size_t i = 0; i < MODBUS_TCP_SERVER_MAX_CLIENTS; i++)
    {
        if (server->clients[i].socket < 0)
        {
            continue;
        }

        switch (server->clients[i].state)
        {
        case MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST:
            client_recv_tcp_request(server, i);
            break;
        case MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_SUBMIT:
            client_submit_rtu_request(server, i);
            break;
        case MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_RESPONSE:
            client_recv_rtu_response(server, i);
            break;
        case MODBUS_TCP_SERVER_CLIENT_STATE_SENDING_TCP_RESPONSE:
            client_send_tcp_response(server, i);
            break;
        }
    }
}

static bool client_should_receive(modbus_tcp_server_client_t *client)
{
    return client->socket >= 0 && client->state == MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST;
}

static bool client_should_send(modbus_tcp_server_client_t *client)
{
    return client->socket >= 0 && client->state == MODBUS_TCP_SERVER_CLIENT_STATE_SENDING_TCP_RESPONSE;
}

static void tcp_task(void *argument)
{
    modbus_tcp_server_t *server = argument;

    // create server sokcet

    for (;;)
    {
        struct sockaddr_in address;

        server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server->server_socket < 0)
        {
            goto server_socket_fail;
        }


        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(server->listener_port);
        address.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

        if (bind(server->server_socket, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
            listen(server->server_socket, 1) != 0 ||
            fcntl(server->server_socket, F_SETFL, O_NONBLOCK) != 0)

        {
            goto server_socket_fail;
        }

        break;
    server_socket_fail:
        if (server->server_socket != -1)
        {
            close(server->server_socket);
            server->server_socket = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // set clinets to reset state

    for (;;)
    {
        struct timeval select_timeout;
        int max_socket;
        int select_result;

        watchdog_report(WATCHDOG_EVENT_MODBUS_TCP);

        FD_ZERO(&server->read_sockets);
        FD_ZERO(&server->write_sockets);
        max_socket = server->server_socket;
        FD_SET(server->server_socket, &server->read_sockets);

        for (uint8_t i = 0; i < MODBUS_TCP_SERVER_MAX_CLIENTS; i++)
        {
            if (client_should_receive(&server->clients[i]))
            {
                FD_SET(server->clients[i].socket, &server->read_sockets);
            }
            if (client_should_send(&server->clients[i]))
            {
                FD_SET(server->clients[i].socket, &server->write_sockets);
            }
            if (server->clients[i].socket > max_socket)
            {
                max_socket = server->clients[i].socket;
            }
        }

        select_timeout.tv_sec = 0L;
        select_timeout.tv_usec = MODBUS_TCP_SELECT_WAIT_US;
        select_result = select(max_socket + 1, &server->read_sockets, &server->write_sockets, NULL, &select_timeout);

        if (select_result < 0)
        {
            const int error = errno;

            if(error == EINTR)
            {
                continue;
            }
            else if(error == EBADF)
            {
                if(fcntl(server->server_socket, F_GETFL, 0) == -1 && errno == EBADF)
                {
                    for (size_t i = 0; i < MODBUS_TCP_SERVER_MAX_CLIENTS; i++)
                    {
                        if (server->clients[i].socket >= 0)
                        {
                            client_close(server, i);
                        }
                    }
                    goto server_socket_fail;
                }

                for (size_t i = 0; i < MODBUS_TCP_SERVER_MAX_CLIENTS; i++)
                {
                    if (server->clients[i].socket < 0)
                    {
                        continue;
                    }

                    if (!FD_ISSET(server->clients[i].socket, &server->read_sockets) && !FD_ISSET(server->clients[i].socket, &server->write_sockets))
                    {
                        continue;
                    }

                    if (fcntl(server->clients[i].socket, F_GETFL, 0) == -1 && errno == EBADF)
                    {
                        client_close(server, i);
                    }
                }
            }
            else
            {
                //printf("select failed: errno=%d (%s)\n", error, strerror(error));
                vTaskDelay(10);
                goto clean_expired_response;
            }
        }

        if (FD_ISSET(server->server_socket, &server->read_sockets))
        {
            server_accept(server);
        }

        clients_update(server);

        // 清理已销毁连接的遗留请求所对应的响应
        clean_expired_response:
        {
            modbus_rtu_transaction_scheduler_response_t response;
            if (xQueuePeek(server->response_queue, &response, 0) == pdPASS)
            {
                bool should_dropped = true;
                for (size_t i = 0; i < MODBUS_TCP_SERVER_MAX_CLIENTS; i++)
                {
                    if (server->clients[i].socket >= 0 &&
                        server->clients[i].state == MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_RESPONSE &&
                        server->clients[i].rtu_request.token == response.token)
                    {
                        should_dropped = false;
                        break;
                    }
                }

                if (should_dropped)
                {
                    xQueueReceive(server->response_queue, &response, 0);
                    modbus_rtu_adu_pool_release(response.rtu_adu);
                }
            }
        }
    }
}

modbus_tcp_server_result_t modbus_tcp_server_init(modbus_tcp_server_t *server,
                                                  const modbus_tcp_server_config_t *config)
{
    if (server == NULL || config == NULL)
    {
        return MODBUS_TCP_SERVER_INVALID_ARGUMENT;
    }
    if (server->state != MODBUS_TCP_SERVER_STATE_UNINITIALIZED)
    {
        return MODBUS_TCP_SERVER_INVALID_STATE;
    }
    if (config->request_queue == NULL || config->response_queue == NULL || config->listen_port == 0U ||
        config->response_timeout_ms < MODBUS_TCP_RESPONSE_TIMEOUT_MIN_MS ||
        config->response_timeout_ms > MODBUS_TCP_RESPONSE_TIMEOUT_MAX_MS ||
        config->task_name == NULL || config->task_stack == NULL ||
        config->task_stack_depth == 0U || config->task_buffer == NULL ||
        config->task_priority >= configMAX_PRIORITIES)
    {
        return MODBUS_TCP_SERVER_INVALID_ARGUMENT;
    }

    memset(server, 0, sizeof(*server));
    server->listener_port = config->listen_port;
    server->response_timeout_ms = config->response_timeout_ms;
    server->request_queue = config->request_queue;
    server->response_queue = config->response_queue;
    server->available_token = 0U;
    server->server_socket = -1;

    for (uint8_t i = 0; i < MODBUS_TCP_SERVER_MAX_CLIENTS; i++)
    {
        server->clients[i].socket = -1;
    }

    server->task_handle = xTaskCreateStatic(tcp_task, config->task_name, config->task_stack_depth, server,
                                            config->task_priority, config->task_stack, config->task_buffer);
    if (server->task_handle == NULL)
    {
        server->request_queue = NULL;
        server->response_queue = NULL;
        server->state = MODBUS_TCP_SERVER_STATE_FAILED;
        return MODBUS_TCP_SERVER_TASK_CREATE_FAILED;
    }

    server->state = MODBUS_TCP_SERVER_STATE_RUNNING;
    return MODBUS_TCP_SERVER_OK;
}
