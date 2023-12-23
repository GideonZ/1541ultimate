#ifndef NETWORK_INTERFACE_H
#define NETWORK_INTERFACE_H

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
#include "config.h"
#include "browsable.h"
#include "netif/etharp.h"
#include "lwip/inet.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include <lwip/stats.h>
extern "C" {
#include "small_printf.h"
#include "arch/sys_arch.h"
}

#define CFG_NET_DHCP_EN		0xE0
#define CFG_NET_IP          0xE1
#define CFG_NET_NETMASK		0xE2
#define CFG_NET_GATEWAY		0xE3
#define CFG_NET_HOSTNAME    0xE4
#define CFG_VIC_UDP_IP      0xE8
#define CFG_VIC_UDP_PORT    0xE9
#define CFG_VIC_UDP_EN      0xEA

#include "fifo.h" // my oh so cool fifo! :)
#define PBUF_FIFO_SIZE 70

typedef void (*driver_free_function_t)(void *driver, void *buffer);
typedef uint8_t (*driver_output_function_t)(void *driver, void *buffer, int length);

class NetworkInterface : protected ConfigurableObject
{
	static IndexedList<NetworkInterface *>netInterfaces;
public:
	static void registerNetworkInterface(NetworkInterface *intf) {
		netInterfaces.append(intf);
	}
	static void unregisterNetworkInterface(NetworkInterface *intf) {
		netInterfaces.remove(intf);
	}
	static int getNumberOfInterfaces(void) {
		return netInterfaces.get_elements();
	}
	static NetworkInterface *getInterface(int i) {
	    return netInterfaces[i];
	}
    static bool DoWeHaveLink(void) {
        union {
            uint32_t ipaddr32[3];
            uint8_t ipaddr[12];
        } ip;

        int n = netInterfaces.get_elements();
        for(int i=0; i<n; i++) {
            NetworkInterface *intf = netInterfaces[i];
            intf->getIpAddr(ip.ipaddr);
            uint32_t my_ip = ip.ipaddr32[0];
            if (intf && intf->is_link_up() && my_ip != 0) {
                return true;
            }
        }
        return false;
    }

protected:
	struct pbuf_custom pbuf_array[PBUF_FIFO_SIZE];
	Fifo<struct pbuf_custom *> pbuf_fifo;

	uint8_t mac_address[6];
	char hostname[24];
    struct netif   my_net_if;
    int    dhcp_enable;
    bool   if_up;

    // fields that are filled in by the configuration
    struct ip_addr my_ip;
    struct ip_addr my_netmask;
    struct ip_addr my_gateway;

    void *driver;
    void (*driver_free_function)(void *driver, void *buffer);
    uint8_t (*driver_output_function)(void *driver, void *buffer, int pkt_len);
    
    // callbacks
    static err_t lwip_output_callback(struct netif *netif, struct pbuf *pbuf);
    static void lwip_free_callback(void *p);
public:

    NetworkInterface(void *driver,
                driver_output_function_t out,
				driver_free_function_t free);
    virtual ~NetworkInterface();

    virtual void attach_config();
    virtual const char *identify() { return "NetworkLWIP"; }

	bool start();
    void stop();
    void link_up();
    void link_down();
    bool is_link_up() { return if_up; }
    void set_mac_address(uint8_t *mac);
    bool input(void *raw_buffer, uint8_t *payload, int pkt_size);

    void init_callback();

    void free_pbuf(struct pbuf_custom *pbuf);

    // from ConfigurableObject
    virtual void effectuate_settings(void);

	void getIpAddr(uint8_t *a);
	void getMacAddr(uint8_t *a);
	void setIpAddr(uint8_t *a);
	char *getIpAddrString(char *buf, int buflen);
	bool peekArpTable(uint32_t ipToQuery, uint8_t *mac);

	// callbacks
	static void statusCallback(struct netif *);
	void statusUpdate(void);

    // User Interface
    virtual void getDisplayString(int index, char *buffer, int width);
    virtual void getSubItems(Browsable *parent, IndexedList<Browsable *> &list, int &error) { error = -1; }
    virtual void fetch_context_items(IndexedList<Action *> &items) {}
};

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free);

void releaseNetworkStack(void *netstack);

#endif
