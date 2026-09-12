#ifndef MQTT_TLS_POLICY_H
#define MQTT_TLS_POLICY_H

#include <stdbool.h>

void mqtt_tls_require_secure_adapter(void);
bool mqtt_tls_policy_set_broker_hostname(const char *broker_hostname);
void mqtt_tls_policy_clear_broker_hostname(void);

#endif /* MQTT_TLS_POLICY_H */
