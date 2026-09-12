#ifndef CONFIGURATION_TEST_FIXTURES_H
#define CONFIGURATION_TEST_FIXTURES_H

#include "configuration.h"

extern const uint8_t configuration_test_default_payload[48];
extern const uint8_t configuration_test_maximum_payload[8475];
extern const uint8_t configuration_test_root_ca[4097];

/* Constructs the specified maximum model directly, without calling the production codec. */
void configuration_test_make_maximum(configuration_t *configuration);

#endif
