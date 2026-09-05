#ifndef MANAGEMENT_TRANSPORT_TEST_ADAPTER_H
#define MANAGEMENT_TRANSPORT_TEST_ADAPTER_H

#include "FreeRTOS.h"
#include "configuration_binary_codec.h"
#include "configuration_service.h"
#include "management_frame.h"
#include "management_transport.h"
#include "mqtt_publisher.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool task_creation_fails;
    uint32_t task_create_count;
    uint32_t task_notify_count;
    uint32_t isr_notify_count;
    uint32_t yield_count;
    uint32_t critical_depth;
    bool assert_failed;
    TickType_t tick;
    TickType_t last_wait_ticks;
    uint32_t notification_count;
    uint32_t delay_count;
    TickType_t last_delay_ticks;
    void (*delay_hook)(void);

    management_transport_cdc_result_t cdc_enable_result;
    management_transport_cdc_result_t cdc_send_result;
    uint32_t cdc_enable_count;
    uint32_t cdc_send_count;
    uint8_t *last_send_data;
    uint16_t last_send_length;
    uint8_t last_send_copy[MANAGEMENT_FRAME_MAX_LENGTH];

    bool configuration_ready;
    bool active_configuration_available;
    configuration_binary_codec_result_t encode_result;
    uint8_t encoded_configuration[CONFIGURATION_V1_MAX_PAYLOAD_LENGTH];
    uint32_t encoded_configuration_length;
    configuration_service_result_t write_result;
    uint32_t write_count;
    uint8_t write_payload[MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH];
    uint32_t write_payload_length;

    bool sntp_synchronized;
    bool sntp_time_available;
    uint32_t time_read_count;
    uint32_t unix_seconds;
    uint32_t microseconds;
    mqtt_publisher_state_t mqtt_state;

    uint32_t reset_count;
    uint32_t log_count;
} management_transport_test_state_t;

extern management_transport_test_state_t management_transport_test_state;

void management_transport_test_adapter_reset(void);
void management_transport_test_run_task(void);
void management_transport_test_stop_task(void);
void management_transport_test_set_network(bool present, bool link_up, uint8_t first, uint8_t second, uint8_t third,
                                           uint8_t fourth);
void management_transport_test_system_reset(void);

#endif
