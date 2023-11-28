#ifndef DUMMY_DRIVER_H
#define DUMMY_DRIVER_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "network_interface.h"

class DummyDriver
{
	NetworkInterface *netstack;
	bool    link_up;
	uint8_t local_mac[6];

    static void startDummyTask(void *);
    void DummyTask(void);
public:
    DummyDriver();
	~DummyDriver();

	void init(void);
	void forceLinkUp(void);
    void forceLinkDown(void);
	uint8_t output_packet(uint8_t *buffer, int pkt_len);
    void free_buffer(uint8_t *b);
};

extern DummyDriver dummy_interface;

#endif
