#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MQTT_TLS_POLICY_TEST
#include "../../MQTT/src/mqtt_altcp_tls_mbedtls.c"
#include "mbedtls/ssl_internal.h"

int main(void)
{
    mbedtls_ssl_config configuration;
    mbedtls_ssl_context ssl;
    char hostname[256];

    memset(hostname, 'h', 253U);
    hostname[63] = '.';
    hostname[127] = '.';
    hostname[191] = '.';
    hostname[253] = '\0';
    assert(mqtt_tls_policy_set_broker_hostname(hostname));
    mbedtls_ssl_config_init(&configuration);
    mbedtls_ssl_init(&ssl);
    assert(mbedtls_ssl_config_defaults(&configuration, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
    mqtt_tls_ssl_conf_authmode(&configuration, MBEDTLS_SSL_VERIFY_OPTIONAL);
    assert(configuration.authmode == MBEDTLS_SSL_VERIFY_REQUIRED);
    assert(mqtt_tls_ssl_setup(&ssl, &configuration) == 0);
    assert(strlen(ssl.hostname) == 253U && strcmp(ssl.hostname, hostname) == 0);
    hostname[0] = 'j';
    assert(ssl.hostname[0] == 'h');
    mbedtls_ssl_free(&ssl);
    mqtt_tls_policy_clear_broker_hostname();
    mbedtls_ssl_init(&ssl);
    assert(mqtt_tls_ssl_setup(&ssl, &configuration) == MBEDTLS_ERR_SSL_BAD_INPUT_DATA);
    mbedtls_ssl_free(&ssl);
    assert(!mqtt_tls_policy_set_broker_hostname(NULL));
    assert(!mqtt_tls_policy_set_broker_hostname(""));
    hostname[253] = 'h';
    hostname[254] = '\0';
    assert(!mqtt_tls_policy_set_broker_hostname(hostname));
    mbedtls_ssl_config_free(&configuration);
    printf("PASS real TLS setup: hostname=253, required verification; TLS buffers=%u+%u bytes\n",
           (unsigned int)MBEDTLS_SSL_IN_BUFFER_LEN, (unsigned int)MBEDTLS_SSL_OUT_BUFFER_LEN);
    return 0;
}
