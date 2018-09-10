/*
 * usb_new.h
 *
 *  Created on: Feb 4, 2015
 *      Author: Gideon
 */

#ifndef USB_NANO_H_
#define USB_NANO_H_

#include "itu.h"
#include "iomap.h"

#define USB2_BASE    USB_BASE
#define NANO_BASE    USB_BASE

#define ATTR_FIFO_BASE    (NANO_BASE + 0x700)
#define ATTR_FIFO_ENTRIES  16

#define USB2_STATUS 	    (*(volatile uint16_t *)(NANO_BASE + 0x7CC))

#define NANO_SIMULATION	    (*(volatile uint16_t *)(NANO_BASE + 0x7D6))
#define NANO_DO_SUSPEND	    (*(volatile uint16_t *)(NANO_BASE + 0x7D8))
#define NANO_DO_RESET	    (*(volatile uint16_t *)(NANO_BASE + 0x7DA))
#define NANO_LINK_SPEED	    (*(volatile uint16_t *)(NANO_BASE + 0x7DC))
#define NANO_NUM_PIPES      (*(volatile uint16_t *)(NANO_BASE + 0x7DE))

#define ATTR_FIFO_TAIL      (*(volatile uint16_t *)(NANO_BASE + 0x7F0)) // updated by software only
#define ATTR_FIFO_HEAD      (*(volatile uint16_t *)(NANO_BASE + 0x7F2)) // updated by nano only

typedef struct _t_usb_descriptor {
    uint16_t command;
    uint16_t devEP;
    uint16_t length;
    uint16_t maxTrans;
    uint16_t interval;
    uint16_t lastFrame;
    uint16_t splitCtl;
    uint16_t result;
    uint16_t memLo;
    uint16_t memHi;
    uint16_t started;
    uint16_t timeout;
} t_usb_descriptor;

#define USB2_NUM_PIPES  8
#define USB2_PIPES_BASE (NANO_BASE + 0x600)
#define USB2_PIPE_SIZE  24

#define USB2_DESCRIPTOR(x)  ((volatile t_usb_descriptor *)(USB2_PIPES_BASE + x * USB2_PIPE_SIZE))
#define USB2_DESCRIPTORS    ((volatile t_usb_descriptor *)USB2_PIPES_BASE)

#define NANO_START  		(*(volatile uint8_t *)(NANO_BASE + 0x800))

// Bit definitions
#define USTAT_CONNECTED		0x01
#define USTAT_OPERATIONAL	0x02
#define USTAT_SUSPENDED	    0x04

#define UCMD_MEMREAD      0x8000
#define UCMD_MEMWRITE	  0x4000
#define UCMD_PING_ENABLE  0x2000
#define UCMD_TOGGLEBIT	  0x0800
#define UCMD_RETRY_ON_NAK 0x0400
#define UCMD_PAUSED		  0x0200
#define UCMD_DO_DATA	  0x0040
#define UCMD_SETUP		  0x0000
#define UCMD_OUT		  0x0001
#define UCMD_IN			  0x0002
#define UCMD_PING		  0x0003

#define SPLIT_START		0x0000
#define SPLIT_COMPLETE	0x0080
#define SPLIT_SPEED		0x0040
#define SPLIT_CONTROL   0x0000
#define SPLIT_ISOCHRO   0x0010
#define SPLIT_BULK      0x0020
#define SPLIT_INTERRUPT 0x0030
#define SPLIT_PORT_MSK  0x000F
#define SPLIT_HUBAD_MSK 0x7F00
#define SPLIT_DO_SPLIT	0x8000


#define URES_ABORTED    0x8000
#define URES_PACKET	    0x0000
#define URES_ACK	    0x1000
#define URES_NAK	    0x2000
#define URES_NYET	    0x3000
#define URES_STALL      0x4000
#define URES_ERROR	    0x5000
#define URES_RESULT_MSK 0xF000
#define URES_TOGGLE     0x0800
#define URES_NO_DATA    0x0400
#define URES_LENGTH_MSK 0x03FF

#endif /* USB_NANO_H_ */
