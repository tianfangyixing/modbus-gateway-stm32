#ifndef CONFIGURATION_TEST_SUPPORT_H
#define CONFIGURATION_TEST_SUPPORT_H

#include "configuration.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool configuration_test_use_standard_allocator(void);
bool configuration_test_use_failing_allocator(void);

void configuration_test_set_ipv4(ip4_addr_t *address, uint8_t first, uint8_t second, uint8_t third, uint8_t fourth);
bool configuration_test_set_hostname(configuration_hostname_t *field, const uint8_t *bytes, size_t length);
bool configuration_test_set_client_id(configuration_client_id_value_t *field, const uint8_t *bytes, size_t length);
bool configuration_test_set_username(configuration_username_t *field, const uint8_t *bytes, size_t length);
bool configuration_test_set_password(configuration_password_t *field, const uint8_t *bytes, size_t length);
bool configuration_test_set_certificate(configuration_ca_certificate_t *field, const uint8_t *bytes, size_t length);
bool configuration_test_set_topic(configuration_topic_t *field, const uint8_t *bytes, size_t length);
bool configuration_test_set_payload(configuration_mqtt_message_payload_t *field, const uint8_t *bytes, size_t length);

bool configuration_test_load_fixture(const char *name, uint8_t *bytes, size_t capacity, size_t *length);
bool configuration_test_set_certificate_fixture(configuration_ca_certificate_t *field, const char *name);
bool configuration_test_make_valid_point(configuration_collection_point_t *point, uint8_t index);
bool configuration_test_make_valid_mqtt(configuration_t *configuration);
void configuration_test_make_valid_static_network(configuration_t *configuration);

#endif
