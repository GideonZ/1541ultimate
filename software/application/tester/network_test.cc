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

extern "C" {
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

}

/**
 * Factory
 */
NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free)
{
	return new NetworkTest (driver, out, free);
}

void releaseNetworkStack(void *netstack)
{
	delete ((NetworkTest *)netstack);
}

/**
 * Class / instance functions
 */
NetworkTest :: NetworkTest(void *driver,
							driver_output_function_t out,
							driver_free_function_t free)
{
    this->driver = driver;
	this->driver_free_function = free;
	this->driver_output_function = out;
	if_up = false;
	NetworkInterface :: registerNetworkInterface(this);
}

NetworkTest :: ~NetworkTest()
{
	NetworkInterface :: unregisterNetworkInterface(this);
}

void ethernet_init_inside_thread(void *classPointer)
{
	((NetworkTest *)classPointer)->init_callback();
}

bool NetworkTest :: start()
{
	init_callback();
    if_up = false;
    return true;
}

void NetworkTest :: stop()
{
    //if_up = false;
}


void NetworkTest :: init_callback( )
{
}


bool NetworkTest :: input(uint8_t *raw_buffer, uint8_t *payload, int pkt_size)
{
	PacketDescriptor desc;
	desc.pointer = payload;
	desc.length = pkt_size;
	incomingPackets.push(desc);
	// dump_hex(payload, pkt_size);
	return true;
}

bool NetworkTest :: output(uint8_t *raw_buffer, int pkt_size)
{
	if (driver_output_function(driver, raw_buffer, pkt_size) == 0) {
		return true;
	}
	return false;
}

void NetworkTest :: link_up()
{
    // Enable the network interface
	if_up = true;
}

void NetworkTest :: link_down()
{
	if_up = false;
}

void NetworkTest :: set_mac_address(uint8_t *mac)
{
	memcpy((void *)mac_address, (const void *)mac, 6);
}


/*
char *NetworkTest :: getIpAddrString(char *buf, int buflen)
{
}

void NetworkTest :: getIpAddr(uint8_t *buf)
{
}

void NetworkTest :: getMacAddr(uint8_t *buf)
{
}

void NetworkTest :: setIpAddr(uint8_t *buf)
{
}
*/
