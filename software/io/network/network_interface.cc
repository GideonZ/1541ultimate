/*
 * network_interface.cc
 *
 *  Created on: Mar 9, 2015
 *      Author: Gideon
 */

#include "network_interface.h"

NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free) __attribute__((weak));

void releaseNetworkStack(void *s) __attribute__((weak));


NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free)
{
	return 0;
}

void releaseNetworkStack(void *s)
{

}

//-----------------------------------
char *net_en_dis[] = { "Disabled", "Enabled" };
#define CFG_NET_DHCP_EN		0xE0
#define CFG_NET_IP          0xE1
#define CFG_NET_NETMASK		0xE2
#define CFG_NET_GATEWAY		0xE3

struct t_cfg_definition net_config[] = {
    { CFG_NET_DHCP_EN, CFG_TYPE_ENUM,   "Use DHCP",                      "%s", net_en_dis, 0,  1, 1 },
	{ CFG_NET_IP,      CFG_TYPE_STRING, "Static IP",					 "%s", NULL,       7, 16, (int)"192.168.2.64" },
	{ CFG_NET_NETMASK, CFG_TYPE_STRING, "Static Netmask",				 "%s", NULL,       7, 16, (int)"255.255.255.0" },
	{ CFG_NET_GATEWAY, CFG_TYPE_STRING, "Static Gateway",				 "%s", NULL,       7, 16, (int)"192.168.2.1" },
	{ CFG_NET_HOSTNAME,CFG_TYPE_STRING, "Host Name", 					 "%s", NULL,       3, 18, (int)"Ultimate-II" },
	{ CFG_TYPE_END,    CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

NetworkInterface :: NetworkInterface()
{
    register_store(0x4E657477, "Network settings", net_config);
}

