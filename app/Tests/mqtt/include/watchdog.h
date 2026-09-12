#ifndef MQTT_TEST_WATCHDOG_H
#define MQTT_TEST_WATCHDOG_H
#define WATCHDOG_EVENT_MQTT 8U
#define WATCHDOG_EVENT_TCPIP 128U
void watchdog_report(unsigned int bits);
#endif
