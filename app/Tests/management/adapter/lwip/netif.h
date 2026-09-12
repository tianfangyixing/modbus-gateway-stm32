#ifndef MANAGEMENT_TEST_NETIF_H
#define MANAGEMENT_TEST_NETIF_H
#include "lwip/ip4_addr.h"
#include <stdbool.h>
struct netif
{
    ip4_addr_t ipv4;
    bool link_up;
};
extern struct netif *netif_default;
#define netif_ip4_addr(netif) (&(netif)->ipv4)
#define netif_is_link_up(netif) ((netif)->link_up)
#endif
