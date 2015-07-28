#ifndef NETWORK_LWIP_H
#define NETWORK_LWIP_H

//#include "ipv4/lwip/ip_addr.h"
/*
#include "lwip/init.h"
#include "lwip/ip_frag.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"
#include "lwip/tcp_impl.h"
#include "lwip/udp.h"
*/
#include "netif/etharp.h"
#include "lwip/inet.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include <lwip/stats.h>
extern "C" {
#include "small_printf.h"
#include "arch/sys_arch.h"
}

#include "network_interface.h"
#include "fifo.h" // my oh so cool fifo! :)
#define PBUF_FIFO_SIZE 70

class NetworkLWIP : public NetworkInterface
{
	struct pbuf_custom pbuf_array[PBUF_FIFO_SIZE];
	Fifo<struct pbuf_custom *> pbuf_fifo;

	void lwip_poll();
public:
    struct netif   my_net_if;

    // fields that are filled in by the configuration
    struct ip_addr my_ip;
    struct ip_addr my_netmask;
    struct ip_addr my_gateway;
    int dhcp_enable;

    bool   if_up;

    void *driver;
    void (*driver_free_function)(void *driver, void *buffer);
    uint8_t (*driver_output_function)(void *driver, void *buffer, int pkt_len);

    NetworkLWIP(void *driver,
				driver_output_function_t out,
				driver_free_function_t free);
    virtual ~NetworkLWIP();

    const char *identify() { return "NetworkLWIP"; }

    bool start();
    void stop();
    void poll();
    void link_up();
    void link_down();
    void set_mac_address(uint8_t *mac);
    bool input(uint8_t *raw_buffer, uint8_t *payload, int pkt_size);

    virtual void init_callback();
    virtual uint8_t output_callback(struct netif *, struct pbuf *) {
        printf("Network Interface: Output Callback - Base\n");
    }

    void free_pbuf(struct pbuf_custom *pbuf);

    // from ConfigurableObject
    void effectuate_settings(void);

	void getIpAddr(uint8_t *a);
	void getMacAddr(uint8_t *a);
	void setIpAddr(uint8_t *a);
};

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free);

void releaseNetworkStack(void *netstack);

#endif
