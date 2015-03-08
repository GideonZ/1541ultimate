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

void main_loop(void);

int main()
{
	puts("Starting main loop.");
	main_loop();
	return 0;
}
