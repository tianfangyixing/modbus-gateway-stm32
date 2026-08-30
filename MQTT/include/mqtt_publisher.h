#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

typedef enum
{
    MQTT_PUBLISHER_STATE_DISABLED = 0,
    MQTT_PUBLISHER_STATE_DISCONNECTED = 1,
    MQTT_PUBLISHER_STATE_CONNECTING = 2,
    MQTT_PUBLISHER_STATE_CONNECTED = 3,
    MQTT_PUBLISHER_STATE_ERROR = 4
} mqtt_publisher_state_t;

void mqtt_example_init(void);
mqtt_publisher_state_t mqtt_publisher_get_state(void);

#endif
