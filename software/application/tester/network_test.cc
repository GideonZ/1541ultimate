#include "network_test.h"
#include "init_function.h"
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
}
//-----------------------------------
#include "fifo.h"

typedef struct {
	uint8_t *pointer;
	int length;
} PacketDescriptor;

PacketDescriptor nullDescriptor = { 0, 0 };

Fifo<PacketDescriptor> incomingPackets(30, nullDescriptor);

int getNetworkPacket(uint8_t **payload, int *length)
{
	if (incomingPackets.is_empty()) {
		return 0;
	}
	PacketDescriptor desc = incomingPackets.pop();
	*payload = desc.pointer;
	*length  = desc.length;
	return 1;
}

/**
 * Factory
 */
IndexedList<NetworkInterface *> NetworkInterface :: netInterfaces(4, NULL);

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free)
{
	return new NetworkInterface (driver, out, free);
}

void releaseNetworkStack(void *netstack)
{
	delete ((NetworkInterface *)netstack);
}

/**
 * Class / instance functions
 */
NetworkInterface :: NetworkInterface(void *driver,
							driver_output_function_t out,
							driver_free_function_t free)
{
    this->driver = driver;
	this->driver_free_function = free;
	this->driver_output_function = out;
	if_up = false;
}

NetworkInterface :: ~NetworkInterface()
{
}

void ethernet_init_inside_thread(void *classPointer)
{
	((NetworkInterface *)classPointer)->init_callback();
}

bool NetworkInterface :: start()
{
	init_callback();
    if_up = false;
    return true;
}

void NetworkInterface :: stop()
{
    //if_up = false;
}


void NetworkInterface :: init_callback( )
{
}


bool NetworkInterface :: input(void *raw_buffer, uint8_t *payload, int pkt_size)
{
	PacketDescriptor desc;
	desc.pointer = payload;
	desc.length = pkt_size;
	incomingPackets.push(desc);
	// dump_hex(payload, pkt_size);
	return true;
}

bool NetworkInterface :: output(uint8_t *raw_buffer, int pkt_size)
{
	if (driver_output_function(driver, raw_buffer, pkt_size) == 0) {
		return true;
	}
	return false;
}

void NetworkInterface :: link_up()
{
    // Enable the network interface
	if_up = true;
}

void NetworkInterface :: link_down()
{
	if_up = false;
}

void NetworkInterface :: set_mac_address(uint8_t *mac)
{
	// memcpy((void *)mac_address, (const void *)mac, 6);
}


/*
char *NetworkInterface :: getIpAddrString(char *buf, int buflen)
{
}

void NetworkInterface :: getIpAddr(uint8_t *buf)
{
}

void NetworkInterface :: getMacAddr(uint8_t *buf)
{
}

void NetworkInterface :: setIpAddr(uint8_t *buf)
{
}
*/
