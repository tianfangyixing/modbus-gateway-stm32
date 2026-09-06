#include "unity.h"
#include "watchdog.h"

/* Compile the actual driver; unused hardware entry points are removed by the linker. */
#include "../../LWIP/Target/ethernetif.c"

static struct netif test_netif;
static struct pbuf test_packets[ETH_TX_DESC_CNT + 1U];
static uint8_t test_payloads[ETH_TX_DESC_CNT + 1U][32];
static uint32_t test_tick;
static uint32_t transmit_count;
static uint32_t release_count;
static uint32_t wait_count;
static uint32_t reference_count;
static uint32_t free_count;
static uint32_t busy_attempts;
static uint32_t signal_delay;
static uint32_t last_wait_ticks;
static EventBits_t reported_bits;
static osStatus_t wait_result;
static HAL_StatusTypeDef release_result;
static struct pbuf *dma_packet;
static void (*wait_hook)(void);
static void (*transmit_hook)(void);
static bool transmit_error;

void setUp(void)
{
    memset(&test_netif, 0, sizeof(test_netif));
    memset(test_packets, 0, sizeof(test_packets));
    memset(&heth, 0, sizeof(heth));
    memset(&TxConfig, 0, sizeof(TxConfig));
    test_netif.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    test_netif.linkoutput = low_level_output;
    heth.gState = HAL_ETH_STATE_STARTED;
    TxPktSemaphore = &test_netif;
    test_packets[0].payload = test_payloads[0];
    test_packets[0].len = sizeof(test_payloads[0]);
    test_packets[0].tot_len = sizeof(test_payloads[0]);
    test_packets[0].ref = 1U;
    test_tick = 0U;
    transmit_count = 0U;
    release_count = 0U;
    wait_count = 0U;
    reference_count = 0U;
    free_count = 0U;
    busy_attempts = 0U;
    signal_delay = 1U;
    last_wait_ticks = 0U;
    reported_bits = 0U;
    wait_result = osErrorTimeout;
    release_result = HAL_OK;
    dma_packet = NULL;
    wait_hook = NULL;
    transmit_hook = NULL;
    transmit_error = false;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, reported_bits);
    TEST_ASSERT_NULL(dma_packet);
    TEST_ASSERT_EQUAL_UINT16(1U, test_packets[0].ref);
    TEST_ASSERT_EQUAL_UINT32(reference_count, free_count);
}

uint32_t osKernelGetTickCount(void)
{
    return test_tick;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
    TEST_ASSERT_EQUAL_PTR(TxPktSemaphore, semaphore_id);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, timeout);
    wait_count++;
    TEST_ASSERT_LESS_THAN_UINT32(10000U, wait_count);
    last_wait_ticks = timeout;
    if (wait_result == osErrorTimeout)
    {
        test_tick += timeout;
    }
    else if (wait_result == osOK)
    {
        test_tick += signal_delay < timeout ? signal_delay : timeout;
    }
    if (wait_hook != NULL)
    {
        wait_hook();
    }
    return wait_result;
}

HAL_ETH_StateTypeDef HAL_ETH_GetState(const ETH_HandleTypeDef *handle)
{
    TEST_ASSERT_EQUAL_PTR(&heth, handle);
    return handle->gState;
}

uint32_t HAL_ETH_GetError(const ETH_HandleTypeDef *handle)
{
    TEST_ASSERT_EQUAL_PTR(&heth, handle);
    return handle->ErrorCode;
}

HAL_StatusTypeDef HAL_ETH_ReleaseTxPacket(ETH_HandleTypeDef *handle)
{
    TEST_ASSERT_EQUAL_PTR(&heth, handle);
    release_count++;
    TEST_ASSERT_GREATER_THAN_UINT32(0U, wait_count);
    TEST_ASSERT_EQUAL_UINT32(wait_count, release_count);
    return release_result;
}

HAL_StatusTypeDef HAL_ETH_Transmit_IT(ETH_HandleTypeDef *handle, ETH_TxPacketConfigTypeDef *configuration)
{
    struct pbuf *packet = &test_packets[0];
    ETH_BufferTypeDef *buffer = configuration->TxBuffer;

    TEST_ASSERT_EQUAL_PTR(&heth, handle);
    TEST_ASSERT_EQUAL_PTR(packet, configuration->pData);
    TEST_ASSERT_EQUAL_UINT32(packet->tot_len, configuration->Length);
    TEST_ASSERT_EQUAL_UINT16(2U, packet->ref);
    while (packet != NULL)
    {
        TEST_ASSERT_NOT_NULL(buffer);
        TEST_ASSERT_EQUAL_PTR(packet->payload, buffer->buffer);
        TEST_ASSERT_EQUAL_UINT32(packet->len, buffer->len);
        packet = packet->next;
        buffer = buffer->next;
    }
    TEST_ASSERT_NULL(buffer);
    transmit_count++;
    if (transmit_hook != NULL)
    {
        transmit_hook();
    }
    if (handle->gState != HAL_ETH_STATE_STARTED)
    {
        /* Match HAL: a stopped MAC returns an error without clearing old BUSY. */
        return HAL_ERROR;
    }
    if (transmit_error)
    {
        handle->ErrorCode = HAL_ETH_ERROR_PARAM;
        return HAL_ERROR;
    }
    if (transmit_count <= busy_attempts)
    {
        handle->ErrorCode |= HAL_ETH_ERROR_BUSY;
        return HAL_ERROR;
    }
    dma_packet = configuration->pData;
    return HAL_OK;
}

void pbuf_ref(struct pbuf *packet)
{
    TEST_ASSERT_EQUAL_PTR(&test_packets[0], packet);
    TEST_ASSERT_EQUAL_UINT16(1U, packet->ref);
    packet->ref++;
    reference_count++;
}

u8_t pbuf_free(struct pbuf *packet)
{
    TEST_ASSERT_EQUAL_PTR(&test_packets[0], packet);
    TEST_ASSERT_EQUAL_UINT16(2U, packet->ref);
    packet->ref--;
    free_count++;
    return 0U;
}

void watchdog_report(EventBits_t task_bit)
{
    reported_bits |= task_bit;
}

static err_t send_packet(void)
{
    return test_netif.linkoutput(&test_netif, &test_packets[0]);
}

static void complete_dma_packet(void)
{
    TEST_ASSERT_EQUAL_PTR(&test_packets[0], dma_packet);
    TEST_ASSERT_EQUAL_UINT32(0U, free_count);
    HAL_ETH_TxFreeCallback((uint32_t *)dma_packet);
    dma_packet = NULL;
}

static void stop_mac(void)
{
    heth.gState = HAL_ETH_STATE_READY;
}

static void drop_link(void)
{
    test_netif.flags &= (u8_t)~NETIF_FLAG_LINK_UP;
}

static void test_success_keeps_packet_until_dma_completion(void)
{
    TEST_ASSERT_EQUAL_INT(ERR_OK, send_packet());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(0U, wait_count);
    TEST_ASSERT_EQUAL_UINT32(0U, release_count);
    TEST_ASSERT_EQUAL_UINT16(2U, test_packets[0].ref);
    complete_dma_packet();
}

static void test_busy_then_completion_retries_successfully(void)
{
    busy_attempts = 1U;
    wait_result = osOK;
    TEST_ASSERT_EQUAL_INT(ERR_OK, send_packet());
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_count);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, release_count);
    complete_dma_packet();
}

static void test_missing_completion_signal_times_out_without_resubmitting(void)
{
    busy_attempts = 1U;
    TEST_ASSERT_EQUAL_INT(ERR_TIMEOUT, send_packet());
    TEST_ASSERT_EQUAL_UINT32(2000U, test_tick);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(1U, wait_count);
    TEST_ASSERT_EQUAL_UINT32(1U, release_count);
    TEST_ASSERT_NULL(dma_packet);
}

static void test_permanent_busy_stops_at_total_deadline(void)
{
    busy_attempts = UINT32_MAX;
    TEST_ASSERT_EQUAL_INT(ERR_TIMEOUT, send_packet());
    TEST_ASSERT_EQUAL_UINT32(2000U, test_tick);
    TEST_ASSERT_NULL(dma_packet);
}

static void test_completion_signals_do_not_restart_total_deadline(void)
{
    busy_attempts = UINT32_MAX;
    wait_result = osOK;
    signal_delay = 7U;
    TEST_ASSERT_EQUAL_INT(ERR_TIMEOUT, send_packet());
    TEST_ASSERT_EQUAL_UINT32(2000U, test_tick);
    TEST_ASSERT_LESS_THAN_UINT32(7U, last_wait_ticks);
}

static void test_deadline_survives_tick_wraparound(void)
{
    uint32_t start_tick = UINT32_MAX - 999U;

    test_tick = start_tick;
    busy_attempts = UINT32_MAX;
    TEST_ASSERT_EQUAL_INT(ERR_TIMEOUT, send_packet());
    TEST_ASSERT_EQUAL_UINT32(2000U, test_tick - start_tick);
}

static void test_link_down_before_send_returns_without_submitting(void)
{
    drop_link();
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(0U, wait_count);
}

static void test_interface_down_before_send_returns_without_submitting(void)
{
    test_netif.flags &= (u8_t)~NETIF_FLAG_UP;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_count);
}

static void test_stopped_mac_with_stale_busy_returns_without_waiting(void)
{
    stop_mac();
    heth.ErrorCode = HAL_ETH_ERROR_BUSY;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(0U, wait_count);
}

static void test_mac_stopping_during_submission_does_not_retry_stale_busy(void)
{
    heth.ErrorCode = HAL_ETH_ERROR_BUSY;
    transmit_hook = stop_mac;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(0U, wait_count);
}

static void test_mac_stopping_during_wait_aborts_within_total_budget(void)
{
    busy_attempts = UINT32_MAX;
    wait_hook = stop_mac;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(2000U, test_tick);
    TEST_ASSERT_EQUAL_UINT32(1U, wait_count);
}

static void test_link_dropping_during_wait_aborts_within_total_budget(void)
{
    busy_attempts = UINT32_MAX;
    wait_hook = drop_link;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(2000U, test_tick);
    TEST_ASSERT_EQUAL_UINT32(1U, wait_count);
}

static void test_semaphore_error_releases_packet_and_returns(void)
{
    busy_attempts = UINT32_MAX;
    wait_result = osErrorParameter;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(1U, wait_count);
}

static void test_non_busy_hal_error_releases_packet_and_returns(void)
{
    transmit_error = true;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(0U, wait_count);
}

static void test_descriptor_release_error_returns_without_retrying(void)
{
    busy_attempts = 1U;
    wait_result = osOK;
    release_result = HAL_ERROR;
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_count);
    TEST_ASSERT_EQUAL_UINT32(1U, wait_count);
    TEST_ASSERT_EQUAL_UINT32(1U, release_count);
    TEST_ASSERT_NULL(dma_packet);
}

static void test_pbuf_chain_is_passed_to_hal_without_copying(void)
{
    test_packets[0].next = &test_packets[1];
    test_packets[1].payload = test_payloads[1];
    test_packets[1].len = sizeof(test_payloads[1]);
    test_packets[0].tot_len += test_packets[1].len;
    TEST_ASSERT_EQUAL_INT(ERR_OK, send_packet());
    complete_dma_packet();
}

static void test_oversized_chain_fails_without_acquiring_a_reference(void)
{
    for (uint32_t i = 0U; i < ETH_TX_DESC_CNT; i++)
    {
        test_packets[i].next = &test_packets[i + 1U];
    }
    TEST_ASSERT_EQUAL_INT(ERR_IF, send_packet());
    TEST_ASSERT_EQUAL_UINT32(0U, reference_count);
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_success_keeps_packet_until_dma_completion);
    RUN_TEST(test_busy_then_completion_retries_successfully);
    RUN_TEST(test_missing_completion_signal_times_out_without_resubmitting);
    RUN_TEST(test_permanent_busy_stops_at_total_deadline);
    RUN_TEST(test_completion_signals_do_not_restart_total_deadline);
    RUN_TEST(test_deadline_survives_tick_wraparound);
    RUN_TEST(test_link_down_before_send_returns_without_submitting);
    RUN_TEST(test_interface_down_before_send_returns_without_submitting);
    RUN_TEST(test_stopped_mac_with_stale_busy_returns_without_waiting);
    RUN_TEST(test_mac_stopping_during_submission_does_not_retry_stale_busy);
    RUN_TEST(test_mac_stopping_during_wait_aborts_within_total_budget);
    RUN_TEST(test_link_dropping_during_wait_aborts_within_total_budget);
    RUN_TEST(test_semaphore_error_releases_packet_and_returns);
    RUN_TEST(test_non_busy_hal_error_releases_packet_and_returns);
    RUN_TEST(test_descriptor_release_error_returns_without_retrying);
    RUN_TEST(test_pbuf_chain_is_passed_to_hal_without_copying);
    RUN_TEST(test_oversized_chain_fails_without_acquiring_a_reference);
    return UNITY_END();
}
