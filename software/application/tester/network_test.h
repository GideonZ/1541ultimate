#ifndef NETWORK_TEST_H
#define NETWORK_TEST_H

// #include "network_interface.h"
#include <stdint.h>
#include "lwip/err.h"
#include "indexed_list.h"

typedef void (*driver_free_function_t)(void *driver, void *buffer);
typedef err_t (*driver_output_function_t)(void *driver, void *buffer, int length);

#define PBUF_FIFO_SIZE 70

class NetworkInterface
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

    bool   if_up;

    void *driver;
    void (*driver_free_function)(void *driver, void *buffer);
    err_t (*driver_output_function)(void *driver, void *buffer, int pkt_len);

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
    bool output(uint8_t *raw_buffer, int pkt_size);

    virtual void init_callback();

	// callbacks
	void statusUpdate(void);
};

NetworkInterface *getNetworkStack(void *driver, driver_output_function_t out, driver_free_function_t free);

void releaseNetworkStack(void *netstack);

#endif
