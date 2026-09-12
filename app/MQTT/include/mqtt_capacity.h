#ifndef MQTT_CAPACITY_H
#define MQTT_CAPACITY_H

#include "configuration.h"
#include "lwip/apps/mqtt_opts.h"

/* MQTT 3.1.1: ten-byte variable header and five length-prefixed strings. */
#define MQTT_PUBLISHER_CONNECT_MAX_REMAINING_LENGTH \
    (10U + 2U + CONFIGURATION_CLIENT_ID_MAX_LENGTH + \
     2U + CONFIGURATION_TOPIC_MAX_LENGTH + 2U + CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH + \
     2U + CONFIGURATION_USERNAME_MAX_LENGTH + 2U + CONFIGURATION_PASSWORD_MAX_LENGTH)
#define MQTT_PUBLISHER_CONNECT_LENGTH_OCTETS \
    ((MQTT_PUBLISHER_CONNECT_MAX_REMAINING_LENGTH < 128U) ? 1U : \
     (MQTT_PUBLISHER_CONNECT_MAX_REMAINING_LENGTH < 16384U) ? 2U : 3U)
#define MQTT_PUBLISHER_CONNECT_MAX_PACKET_LENGTH \
    (1U + MQTT_PUBLISHER_CONNECT_LENGTH_OCTETS + MQTT_PUBLISHER_CONNECT_MAX_REMAINING_LENGTH)
#define MQTT_PUBLISHER_PUBLISH_MAX_REMAINING_LENGTH \
    (2U + CONFIGURATION_TOPIC_MAX_LENGTH + 2U + CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH)
#define MQTT_PUBLISHER_PUBLISH_MAX_PACKET_LENGTH (3U + MQTT_PUBLISHER_PUBLISH_MAX_REMAINING_LENGTH)

/* C99/Arm Compiler 5 compatible checks against the vendor's real constraints.
 * CONNECT lengths are u16, except for will lengths, which are u8.
 * Keep packets strictly smaller than the ring: equal get/put means empty,
 * and the send path requires an advance strictly smaller than the ring.
 * Power-of-two sizing is project policy; bound index addition to u16.
 */
typedef char mqtt_capacity_ring_power_of_two[
    ((MQTT_OUTPUT_RINGBUF_SIZE > 0U) &&
     ((MQTT_OUTPUT_RINGBUF_SIZE & (MQTT_OUTPUT_RINGBUF_SIZE - 1U)) == 0U)) ? 1 : -1];
typedef char mqtt_capacity_ring_index_width[(MQTT_OUTPUT_RINGBUF_SIZE <= 32768U) ? 1 : -1];
typedef char mqtt_capacity_connect_length[(MQTT_PUBLISHER_CONNECT_MAX_REMAINING_LENGTH <= 65535U) ? 1 : -1];
typedef char mqtt_capacity_derived_client_id[(CONFIGURATION_CLIENT_ID_MAX_LENGTH >= 23U) ? 1 : -1];
typedef char mqtt_capacity_will_topic[(CONFIGURATION_TOPIC_MAX_LENGTH <= 255U) ? 1 : -1];
typedef char mqtt_capacity_will_payload[(CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH <= 255U) ? 1 : -1];
typedef char mqtt_capacity_connect_fits[
    (MQTT_PUBLISHER_CONNECT_MAX_PACKET_LENGTH < MQTT_OUTPUT_RINGBUF_SIZE) ? 1 : -1];
typedef char mqtt_capacity_publish_fits[
    (MQTT_PUBLISHER_PUBLISH_MAX_PACKET_LENGTH < MQTT_OUTPUT_RINGBUF_SIZE) ? 1 : -1];
typedef char mqtt_capacity_dns_hostname[(DNS_MAX_NAME_LENGTH > CONFIGURATION_HOSTNAME_MAX_LENGTH) ? 1 : -1];

#endif
