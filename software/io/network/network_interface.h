#ifndef NETWORK_H
#define NETWORK_H

extern "C" {
#include "lwip/init.h"
#include "lwip/ip_frag.h"
#include "netif/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"
#include "lwip/tcp_impl.h"
#include "lwip/udp.h"
#include <lwip/stats.h>
#include "small_printf.h"
}

class NetworkInterface {
public:
    BYTE mac_address[6];

    struct ip_addr my_ip;
    struct ip_addr my_netmask;
    struct ip_addr my_gateway;
    struct netif   my_net_if;
    struct netif  *netif;
    
    NetworkInterface() { }
    virtual ~NetworkInterface() { }

    virtual bool start_lwip();
    virtual void init_callback(struct netif *);
    virtual err_t output_callback(struct netif *, struct pbuf *) {
        printf("Network Interface: Output Callback - Base\n");
    }
    virtual void lwip_poll();
};

#endif
