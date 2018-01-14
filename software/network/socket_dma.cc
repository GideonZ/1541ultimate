/*
 * socket_dma.cc
 *
 *  Created on: June 3, 2017
 *      Author: Gideon
 */

#include "itu.h"
#include "filemanager.h"
#include "socket_dma.h"
#include "socket.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dump_hex.h"
#include "c64.h"
#include "c64_subsys.h"

#define SOCKET_CMD_DMA         0xFF01
#define SOCKET_CMD_DMARUN      0xFF02
#define SOCKET_CMD_KEYB        0xFF03
#define SOCKET_CMD_RESET       0xFF04
#define SOCKET_CMD_WAIT	       0xFF05
#define SOCKET_CMD_DMAWRITE    0xFF06
#define SOCKET_CMD_REUWRITE    0xFF07
#define SOCKET_CMD_KERNALWRITE 0xFF08
#define SOCKET_CMD_DMAJUMP     0xFF09
#define SOCKET_CMD_LOADSIDCRT  0xFF71
#define SOCKET_CMD_LOADBOOTCRT 0xFF72

SocketDMA socket_dma; // global that causes the object to exist

extern cart_def sid_cart;
extern cart_def boot_cart;

SocketDMA::SocketDMA() {
	xTaskCreate( dmaThread, "DMA Load Task", configMINIMAL_STACK_SIZE, (void *)load_buffer, tskIDLE_PRIORITY + 1, NULL );
}

SocketDMA::~SocketDMA() {

}

void SocketDMA :: parseBuffer(void *load_buffer, int length)
{
	uint8_t *buf = (uint8_t *)load_buffer;
	int remaining = length;
	SubsysCommand *c64_command;

	while (remaining > 0) {
		uint16_t cmd = (uint16_t)buf[0] | (((uint16_t)buf[1]) << 8);
		buf += 2;
		remaining -= 2;
		uint16_t len = (uint16_t)buf[0] | (((uint16_t)buf[1]) << 8);
		buf += 2;
		remaining -= 2;
		uint16_t offs;
		uint32_t offs32;
		uint16_t i;
		uint16_t size;

		switch(cmd) {
		case SOCKET_CMD_DMA:
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD, buf, len);
			c64_command->execute();
			break;
		case SOCKET_CMD_DMARUN:
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD_RUN, buf, len);
			c64_command->execute();
			break;
		case SOCKET_CMD_DMAJUMP:
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD_JUMP, buf, len);
			c64_command->execute();
			break;
		case SOCKET_CMD_DMAWRITE:
			offs = (uint16_t)buf[0] | (((uint16_t)buf[1]) << 8);
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW, offs, buf + 2, len - 2);
			c64_command->execute();
			break;
		case SOCKET_CMD_KEYB:
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW, 0x0277, buf, len);
			c64_command->execute();
			buf[0] = len;
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW, 0x00C6, buf, 1);
			c64_command->execute();
			break;
		case SOCKET_CMD_RESET:
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0, buf, len);
			c64_command->execute();
			break;
		case SOCKET_CMD_WAIT:
			vTaskDelay(len);
			len = 0;
			break;
		case SOCKET_CMD_REUWRITE:
			offs32 = (uint32_t)buf[0] | (((uint32_t)buf[1]) << 8) | (((uint32_t)buf[2]) << 16);
			for (i=3; i<len; i++)
			   *(uint8_t *)(REU_MEMORY_BASE+ ((offs32+i-3)&0xffffff)) = buf[i];
			break;
		case SOCKET_CMD_KERNALWRITE:
		    /* GZW: Actually a driver for the cartridge mapping should be called here. */
		    offs32 = (uint32_t)buf[0] | (((uint32_t)buf[1]) << 8);
			for (i=2; i<len; i++)
			   *(uint8_t *)(C64_KERNAL_BASE+1+2*((offs32+i-2)&0x1fff)) = buf[i];
			break;
		case SOCKET_CMD_LOADSIDCRT:
		    size = (len > 0x2000) ? 0x2000 : len;
		    if (sid_cart.custom_addr) {
		        memcpy(sid_cart.custom_addr, buf, size);
		        sid_cart.length = size;
		    }
		    break;
        case SOCKET_CMD_LOADBOOTCRT:
            size = (len > 0x2000) ? 0x2000 : len;
            if (boot_cart.custom_addr) {
                memcpy(boot_cart.custom_addr, buf, size);
                boot_cart.length = size;
            }
            break;
		}
		buf += len;
		remaining -= len;
	}
}

void SocketDMA::dmaThread(void *load_buffer)
{
	int sockfd, newsockfd, portno;
	unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
	{
    	puts("ERROR dmaThread opening socket");
    	return;
	}

    printf("DMA Thread Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 64;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
 	    puts("dmaThread ERROR on binding");
	    return;
    }

    /* Now start listening for the clients, here process will
    * go in sleep mode and will wait for the incoming connection
    */

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while(1) {
		/* Accept actual connection from the client */
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (newsockfd < 0)
		{
			puts("dmaThread ERROR on accept");
			return;
		}

		printf("dmaThread newsockfd = %8x\n", newsockfd);


		/* If connection is established then start communicating */
		char *mempntr = (char *)load_buffer;
		int max_remain = 65536;
		int received = 0;

		do {
			int current = (max_remain > 8192) ? 8192 : max_remain;
			n = recv(newsockfd, mempntr, current, 0);
			mempntr += n;
			max_remain -= n;
			received += n;
		} while((n > 0) && (max_remain > 0));

		lwip_close(newsockfd);
		if (n < 0) {
			puts("ERROR reading from socket");
		} else {
			parseBuffer(load_buffer, received);
		}
    }

    // this will never happen
    lwip_close(sockfd);
}
