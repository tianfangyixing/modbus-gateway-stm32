#include "mqtt_tls_policy.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"

#if !defined(MBEDTLS_SSL_SERVER_NAME_INDICATION)
#error "MQTT TLS requires MBEDTLS_SSL_SERVER_NAME_INDICATION"
#endif

#if !defined(MBEDTLS_HAVE_TIME_DATE)
#error "MQTT TLS requires MBEDTLS_HAVE_TIME_DATE"
#endif

#if !defined(MBEDTLS_X509_CHECK_KEY_USAGE)
#error "MQTT TLS requires MBEDTLS_X509_CHECK_KEY_USAGE"
#endif

#if !defined(MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE)
#error "MQTT TLS requires MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE"
#endif

/*
 * Compile the lwIP adapter through this project-owned translation unit so client authentication is fail-closed
 * without modifying vendor Middleware sources.
 */

void mqtt_tls_require_secure_adapter(void)
{
}

static void mqtt_tls_ssl_conf_authmode(mbedtls_ssl_config *conf, int authmode)
{
    if (conf != NULL && conf->endpoint == MBEDTLS_SSL_IS_CLIENT)
    {
        authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
    }

    mbedtls_ssl_conf_authmode(conf, authmode);
}

static int mqtt_tls_ssl_setup(mbedtls_ssl_context *ssl, const mbedtls_ssl_config *conf)
{
    int result = mbedtls_ssl_setup(ssl, conf);

    if (result != 0 || conf == NULL || conf->endpoint != MBEDTLS_SSL_IS_CLIENT)
    {
        return result;
    }

    return mbedtls_ssl_set_hostname(ssl, MQTT_TLS_SERVER_NAME);
}

static int mqtt_tls_ssl_handshake(mbedtls_ssl_context *ssl)
{
    int result = mbedtls_ssl_handshake(ssl);

    if (result != 0 || ssl == NULL || ssl->conf == NULL || ssl->conf->endpoint != MBEDTLS_SSL_IS_CLIENT)
    {
        return result;
    }

    if (mbedtls_ssl_get_verify_result(ssl) != 0U)
    {
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }

    return 0;
}

#define mbedtls_ssl_conf_authmode mqtt_tls_ssl_conf_authmode
#define mbedtls_ssl_setup mqtt_tls_ssl_setup
#define mbedtls_ssl_handshake mqtt_tls_ssl_handshake

#include "../../Middlewares/Third_Party/LwIP/src/apps/altcp_tls/altcp_tls_mbedtls.c"

#undef mbedtls_ssl_handshake
#undef mbedtls_ssl_setup
#undef mbedtls_ssl_conf_authmode
