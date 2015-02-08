/*
 * usb_new.h
 *
 *  Created on: Feb 4, 2015
 *      Author: Gideon
 */

#ifndef USB_NEW_H_
#define USB_NEW_H_

#include "itu.h"
#include "iomap.h"

#define USB2_BASE    USB_BASE
#define NANO_BASE   (USB_BASE + 0x1000)
#define BUFFER_BASE (USB_BASE + 0x2000)

#define USB2_COMMAND 		(*(volatile BYTE *)(USB2_BASE + 0x01))
#define USB2_BUFFER_CONTROL (*(volatile WORD *)(USB2_BASE + 0x06))
#define USB2_DEVICE_ENDP	(*(volatile WORD *)(USB2_BASE + 0x0A))
#define USB2_TARGET_DEVICE  (*(volatile BYTE *)(USB2_BASE + 0x0A))
#define USB2_TARGET_ENDP    (*(volatile BYTE *)(USB2_BASE + 0x0B))
#define USB2_SPLIT_INFO		(*(volatile WORD *)(USB2_BASE + 0x0E))
#define USB2_HUB_ADDR		(*(volatile BYTE *)(USB2_BASE + 0x0E))
#define USB2_HUB_PORT_FLAGS	(*(volatile BYTE *)(USB2_BASE + 0x0F))

#define USB2_COMMAND_RESULT (*(volatile WORD *)(USB2_BASE + 0x00))
#define USB2_STATUS			(*(volatile BYTE *)(USB2_BASE + 0x02))

#define NANO_DO_RESET	    (*(volatile BYTE *)(NANO_BASE + 0x7FA))
#define NANO_DO_SUSPEND	    (*(volatile BYTE *)(NANO_BASE + 0x7F8))
#define NANO_LINK_SPEED	    (*(volatile BYTE *)(NANO_BASE + 0x7FC))
#define NANO_START  		(*(volatile BYTE *)(NANO_BASE + 0x800))

#define UCMD_DO_SPLIT	0x80
#define UCMD_DO_DATA	0x40
#define UCMD_SETUP		0x00
#define UCMD_OUT		0x01
#define UCMD_IN			0x02

#define UBUF_BUFFER_0	0x0000
#define UBUF_BUFFER_1	0x4000
#define UBUF_BUFFER_2	0x8000
#define UBUF_BUFFER_3	0xC000
#define UBUF_NO_DATA	0x2000
#define UBUF_TOGGLEBIT	0x1000
#define UBUF_LENGTH_MSK 0x03FF

#define SPLIT_START		0x00
#define SPLIT_COMPLETE	0x80
#define SPLIT_SPEED		0x40
#define SPLIT_CONTROL   0x00
#define SPLIT_ISOCHRO   0x10
#define SPLIT_BULK      0x20
#define SPLIT_INTERRUPT 0x30
#define SPLIT_PORT_MSK  0x0F

#define URES_DONE	    0x8000
#define URES_PACKET	    0x0000
#define URES_ACK	    0x1000
#define URES_NAK	    0x2000
#define URES_NYET	    0x3000
#define URES_STALL      0x4000
#define URES_ERROR	    0x5000
#define URES_RESULT_MSK 0x7000
#define URES_TOGGLE     0x0800
#define URES_NO_DATA    0x0400
#define URES_LENGTH_MSK 0x03FF

#endif /* USB_NEW_H_ */
