#ifndef NETWORK_TEST_H
#define NETWORK_TEST_H

#include "network_interface.h"
// #include "fifo.h" // my oh so cool fifo! :)
#define PBUF_FIFO_SIZE 70

class NetworkTest : public NetworkInterface
{
public:
    bool   if_up;

    void *driver;
    void (*driver_free_function)(void *driver, void *buffer);
    uint8_t (*driver_output_function)(void *driver, void *buffer, int pkt_len);

    NetworkTest(void *driver,
				driver_output_function_t out,
				driver_free_function_t free);
    virtual ~NetworkTest();

    const char *identify() { return "NetworkTest"; }

    bool start();
    void stop();
    void link_up();
    void link_down();
    bool is_link_up() { return if_up; }
    void set_mac_address(uint8_t *mac);
    bool input(uint8_t *raw_buffer, uint8_t *payload, int pkt_size);
    bool output(uint8_t *raw_buffer, int pkt_size);

    virtual void init_callback();

/*
	void getIpAddr(uint8_t *a);
	void getMacAddr(uint8_t *a);
	void setIpAddr(uint8_t *a);
	char *getIpAddrString(char *buf, int buflen);
*/

	// callbacks
	void statusUpdate(void);
};

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free);

void releaseNetworkStack(void *netstack);

#endif
