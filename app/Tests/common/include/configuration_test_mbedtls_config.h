#ifndef CONFIGURATION_TEST_MBEDTLS_CONFIG_H
#define CONFIGURATION_TEST_MBEDTLS_CONFIG_H

/* Keep the firmware algorithms/limits; replace only hardware/platform boundaries. */
#include "mbedtls_config.h"
#undef MBEDTLS_ENTROPY_HARDWARE_ALT
#undef MBEDTLS_NO_PLATFORM_ENTROPY
#undef MBEDTLS_PLATFORM_TIME_ALT
#undef MBEDTLS_PLATFORM_GMTIME_R_ALT

#endif
