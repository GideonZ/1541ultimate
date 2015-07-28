#ifndef NETWORK_H
#define NETWORK_H

#include "integer.h"
#include <string.h>
#include "config.h"

class NetworkInterface : public ConfigurableObject {
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

	uint8_t mac_address[6];

    NetworkInterface();
    virtual ~NetworkInterface() { }

    virtual const char *identify() { return "NetworkInterface"; }
    virtual bool start() { return true; }
    virtual void stop() { }
    virtual void poll() { }
    virtual void link_up() { }
    virtual void link_down() { }
    virtual void set_mac_address(uint8_t *mac) {
    	memcpy(mac_address, mac, 6);
    }
    virtual bool input(uint8_t *raw_buffer, uint8_t *payload, int pkt_size) { return false; }
    virtual void effectuate_settings(void) { }

	virtual void getIpAddr(uint8_t *a)  { bzero(a, 12); }
	virtual void getMacAddr(uint8_t *a) { bzero(a, 6); }
	virtual void setIpAddr(uint8_t *a)  { }
};

typedef void (*driver_free_function_t)(void *driver, void *buffer);
typedef uint8_t (*driver_output_function_t)(void *driver, void *buffer, int length);

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free);
void releaseNetworkStack(void *s);

#define CFG_NET_DHCP_EN		0xE0
#define CFG_NET_IP          0xE1
#define CFG_NET_NETMASK		0xE2
#define CFG_NET_GATEWAY		0xE3
#define CFG_NET_HOSTNAME    0xE4
#endif
