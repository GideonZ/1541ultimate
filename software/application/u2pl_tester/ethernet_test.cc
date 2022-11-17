
#include "network_interface.h"
#include <stdio.h>

extern "C" {
int sendEthernetPacket(int len)
{
	static uint8_t packet[1000];

	NetworkInterface *intf = NetworkInterface :: getInterface(0);
	if (!intf) {
		printf("No Network Interface available.\n");
		return 1;
	}
	if (!(intf->is_link_up())) {
		printf("Network Interface is not up.\n");
		return 2;
	}
	for(int i=0;i<6;i++) {
		packet[i] = 0xFF;
	}
	for(int i=6;i<len;i++) {
		packet[i] = (uint8_t)i;
	}
	intf->output(packet, len);
	return 0;
}
}
