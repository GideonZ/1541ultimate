/*
 * UsbDriverTest.cc
 *
 *  Created on: Mar 7, 2015
 *      Author: Gideon
 */

extern "C" {
	#include "small_printf.h"
}
#include "usb_driver_test.h"

extern "C" void main_loop(void *a);

int main()
{
	puts("Starting main loop.");
	main_loop(0);
	return 0;
}
