#ifndef MANAGEMENT_TRANSPORT_H
#define MANAGEMENT_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MANAGEMENT_TRANSPORT_OK = 0,
    MANAGEMENT_TRANSPORT_INVALID_ARGUMENT,
    MANAGEMENT_TRANSPORT_NOT_INITIALIZED,
    MANAGEMENT_TRANSPORT_FAILED
} management_transport_result_t;

typedef enum
{
    MANAGEMENT_TRANSPORT_CDC_OK = 0,
    MANAGEMENT_TRANSPORT_CDC_BUSY,
    MANAGEMENT_TRANSPORT_CDC_FAILED
} management_transport_cdc_result_t;

/**
 * @brief Create the static Management task and initialize protocol state.
 * @note Call before USB Device initialization. Safe before the scheduler starts.
 */
management_transport_result_t management_transport_init(void);

/**
 * @brief Activate request processing after all status providers are initialized.
 * @note Must be called from task context.
 */
management_transport_result_t management_transport_activate(void);

/** @brief Notify Management that the USB CDC class opened a new session. */
management_transport_result_t management_transport_session_open_from_isr(void);

/** @brief Notify Management that the current USB CDC session closed. */
management_transport_result_t management_transport_session_close_from_isr(void);

/** @brief Deliver one completed USB CDC OUT packet. */
management_transport_result_t management_transport_receive_from_isr(const uint8_t *data, uint32_t length);

/** @brief Deliver the asynchronous completion of one USB CDC IN transfer. */
management_transport_result_t management_transport_transmit_complete_from_isr(const uint8_t *data, uint32_t length,
                                                                              uint8_t endpoint);

/** @brief Platform hook that rearms the USB CDC OUT endpoint. */
management_transport_cdc_result_t management_transport_cdc_enable_receive(void);

/** @brief Platform hook that submits one asynchronous USB CDC IN transfer. */
management_transport_cdc_result_t management_transport_cdc_send(uint8_t *data, uint16_t length);

/** @brief Platform hook reporting whether Configuration Service initialized successfully. */
bool management_transport_configuration_is_ready(void);

#if defined(MANAGEMENT_TRANSPORT_TEST)
void management_transport_test_reset(void);
void management_transport_test_process(void);
#endif

#endif
