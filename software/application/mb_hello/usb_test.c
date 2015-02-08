/*
 * usb_test.c
 *
 *  Created on: Feb 4, 2015
 *      Author: Gideon
 */


#include "small_printf.h"
#include "itu.h"
#include "usb_new.h"
#include "dump_hex.h"
#include <string.h>

const BYTE c_get_device_descriptor[] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 };

void my_memcpy(void *dst, const void *src, int len) {
	DWORD *d = (DWORD *)dst;
	DWORD *s = (DWORD *)src;
	len = (len + 3) >> 2;
	for(int i=0;i<len;i++) {
		d[i] = s[i];
	}
}

int main()
{

    puts("Hello USB2!");

    NANO_START = 1;

    wait_ms(1000);

    printf("Status: %b\n", USB2_STATUS);

    NANO_DO_RESET = 1;

    for (int i=0; i<15; i++) {
        printf("Reset status: %b\n", USB2_STATUS);
    }

    my_memcpy((void *)BUFFER_BASE, c_get_device_descriptor, 8);
    puts("Setup data copied");
    USB2_BUFFER_CONTROL = UBUF_BUFFER_0	+ 8;
    puts("Buffer control written");
    USB2_COMMAND = UCMD_SETUP | UCMD_DO_DATA;
    puts("Command sent");

    WORD res = 0;

    for (int i=0; i<10; i++) {
    	printf("Setup Result: %04x\n", USB2_COMMAND_RESULT);
    }

    do {
    	puts("Sending IN command");
		USB2_COMMAND = UCMD_IN | UCMD_DO_DATA;
		do {
			res = USB2_COMMAND_RESULT;
	    	printf("In Result: %04x\n", res);
		} while (!(res & URES_DONE));
    } while ((res & URES_RESULT_MSK) == URES_NAK);

    dump_hex(BUFFER_BASE, res & URES_LENGTH_MSK);

    USB2_BUFFER_CONTROL = UBUF_BUFFER_0 | 0 | UBUF_NO_DATA | UBUF_TOGGLEBIT; // length = 0

    do {
    	puts("Sending OUT command");
    	USB2_COMMAND = UCMD_OUT | UCMD_DO_DATA;
		do {
			res = USB2_COMMAND_RESULT;
	    	printf("Out Result: %04x\n", res);
		} while (!(res & URES_DONE));
    } while ((res & URES_RESULT_MSK) == URES_NAK);

    return 0;
}

