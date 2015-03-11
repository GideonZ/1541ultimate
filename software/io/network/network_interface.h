#ifndef NETWORK_H
#define NETWORK_H

#include "integer.h"
#include <string.h>

class NetworkInterface {
public:
    BYTE mac_address[6];

    NetworkInterface() { }
    virtual ~NetworkInterface() { }

    virtual const char *identify() { return "NetworkInterface"; }
    virtual bool start() { return true; }
    virtual void stop() { }
    virtual void poll() { }
    virtual void link_up() { }
    virtual void link_down() { }
    virtual void set_mac_address(BYTE *mac) {
    	memcpy(mac_address, mac, 6);
    }
    virtual bool input(BYTE *raw_buffer, BYTE *payload, int pkt_size) { return false; }

};

typedef void (*driver_free_function_t)(void *driver, void *buffer);
typedef BYTE (*driver_output_function_t)(void *driver, void *buffer, int length);

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free);
void releaseNetworkStack(void *s);

#endif
