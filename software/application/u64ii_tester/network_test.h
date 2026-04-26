#ifndef NETWORK_TEST_H
#define NETWORK_TEST_H

#include <stdint.h>
#include "indexed_list.h"

typedef int8_t err_t;
typedef void (*driver_free_function_t)(void *driver, void *buffer);
typedef err_t (*driver_output_function_t)(void *driver, void *buffer, int length);

class NetworkInterface
{
	static IndexedList<NetworkInterface *>netInterfaces;
    bool   if_up;
	uint8_t mac_address[6];

    void *driver;
    void (*driver_free_function)(void *driver, void *buffer);
    err_t (*driver_output_function)(void *driver, void *buffer, int pkt_len);
public:
	static NetworkInterface *getInterface(int i) {
	    return netInterfaces[i];
	}
	static void registerNetworkInterface(NetworkInterface *intf) {
		netInterfaces.append(intf);
	}
	static void unregisterNetworkInterface(NetworkInterface *intf) {
		netInterfaces.remove(intf);
	}

    NetworkInterface(void *driver,
				driver_output_function_t out,
				driver_free_function_t free);
    virtual ~NetworkInterface();

    const char *identify() { return "NetworkTest"; }

    bool start();
    void stop();
    void link_up();
    void link_down();
    bool is_link_up() { return if_up; }
    void set_mac_address(uint8_t *mac);
    bool input(void *raw_buffer, uint8_t *payload, int pkt_size);
    bool output(uint8_t *raw_buffer, int pkt_size); // Manual output - only used in test environments

    virtual void init_callback();

	// callbacks
	void statusUpdate(void);
};

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free);

void releaseNetworkStack(void *netstack);
int getNetworkPacket(uint8_t **payload, int *length);

#endif
