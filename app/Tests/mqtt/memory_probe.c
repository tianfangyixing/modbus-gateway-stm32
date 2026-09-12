/* Compile with the firmware Arm compiler; inspect mqtt_memory_probe in the assembly. */
#include "lwip/opt.h"
#if defined(MQTT_MEMORY_PROBE_RING)
#undef MQTT_OUTPUT_RINGBUF_SIZE
#define MQTT_OUTPUT_RINGBUF_SIZE MQTT_MEMORY_PROBE_RING
#endif
#include "lwip/apps/mqtt_priv.h"
#include "../../MQTT/src/mqtt_altcp_tls_mbedtls.c"
const unsigned int mqtt_memory_probe[] =
{
    sizeof(void *),
    MQTT_OUTPUT_RINGBUF_SIZE,
    sizeof(mqtt_client_t),
    MBEDTLS_SSL_IN_BUFFER_LEN,
    MBEDTLS_SSL_OUT_BUFFER_LEN,
    sizeof(mbedtls_ssl_context),
    sizeof(mbedtls_ssl_config),
    sizeof(mbedtls_ssl_handshake_params),
    sizeof(mbedtls_ssl_transform),
    sizeof(mbedtls_ssl_session),
    DNS_MAX_NAME_LENGTH,
    MEM_SIZE,
    sizeof(altcp_mbedtls_state_t),
    sizeof(struct altcp_tls_config),
    sizeof(mbedtls_x509_crt)
};
