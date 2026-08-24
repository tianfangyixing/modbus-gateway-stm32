#ifndef MQTT_TLS_POLICY_H
#define MQTT_TLS_POLICY_H

/* Keep DNS resolution, SNI and certificate identity verification on the same name. */
#define MQTT_TLS_SERVER_NAME "mqtt.tianfangyixing.xyz"

void mqtt_tls_require_secure_adapter(void);

#endif /* MQTT_TLS_POLICY_H */
