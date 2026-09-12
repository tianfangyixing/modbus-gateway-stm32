#ifndef MANAGEMENT_TRANSPORT_TEST_ADAPTER_H
#define MANAGEMENT_TRANSPORT_TEST_ADAPTER_H
#include <stdbool.h>
#include <stdint.h>
#include "management_transport.h"

typedef struct
{
    uint8_t sent[8494];
    uint8_t *tx_pointer;
    uint16_t tx_length;
    uint32_t tx_calls;
    uint32_t receive_rearms;
    uint32_t resets;
    uint32_t boot_requests;
    uint32_t time_reads;
    bool ready;
    bool boot_success;
    bool synchronized;
    bool time_available;
    bool close_during_reset_delay;
    management_transport_cdc_result_t send_result;
} management_test_platform_t;

extern management_test_platform_t management_test_platform;
void management_test_platform_reset(void);
void management_transport_test_run_task(void);
void management_transport_test_system_reset(void);
void management_test_complete_tx(void);
#endif
