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
