#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "network_interface.h"
#include "rmii_interface.h"
#include "dump_hex.h"
#include "flash.h"

__inline uint32_t cpu_to_32le(uint32_t a)
{
#if NIOS
	return a;
#else
	uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
#endif
}

__inline uint16_t le16_to_cpu(uint16_t h)
{
#if NIOS
	return h;
#else // assume big endian
    return (h >> 8) | (h << 8);
#endif
}

RmiiInterface rmii_interface; // global object!

// entry point for free buffer callback
void RmiiInterface_free_buffer(void *drv, void *b) {
	((RmiiInterface *)drv)->free_buffer((uint8_t *)b);
}

// entry point for output packet callback
uint8_t RmiiInterface_output(void *drv, void *b, int len) {
	return ((RmiiInterface *)drv)->output_packet((uint8_t *)b, len);
}

extern "C" {
	void RmiiRxInterruptHandler(void)
	{
		rmii_interface.rx_interrupt_handler();
	}
}

RmiiInterface :: RmiiInterface()
{
    netstack = NULL;
    link_up = false;
    ram_buffer = new uint8_t[(128 * 1536) + 256];
    ram_base = (uint8_t *) (((uint32_t)ram_buffer + 255) & 0xFFFFFF00);
    RMII_FREE_BASE = (uint32_t)ram_base;

    printf("Rmii RAM buffer: %p.. Base = %p\n", ram_buffer, ram_base);

    if (ram_buffer) {
        xTaskCreate( RmiiInterface :: startRmiiTask, "RMII Driver Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
        queue = xQueueCreate(128, sizeof(struct EthPacket)); // single byte queue, only IDs!
    }
}

void RmiiInterface :: startRmiiTask(void *a)
{
	rmii_interface.rmiiTask();
}

#include "mdio.h"
void RmiiInterface :: rmiiTask(void)
{
	netstack = getNetworkStack(this, RmiiInterface_output, RmiiInterface_free_buffer);

	uint8_t flash_serial[8];
	Flash *flash = get_flash();
	flash->read_serial(flash_serial);

	local_mac[0] = 0x02;
	local_mac[1] = 0x15;
	local_mac[2] = 0x41;
	local_mac[3] = flash_serial[1] ^ flash_serial[5];
	local_mac[4] = flash_serial[2] ^ flash_serial[6];
	local_mac[5] = flash_serial[3] ^ flash_serial[7];

	for(int i=0;i<6;i++) {
    	// local_mac[i] = 0x11 * i;
    	RMII_RX_MAC(i) = local_mac[i];
    }

    // we're not a whore
    RMII_RX_PROMISC = 0;
    RMII_RX_ENABLE  = 0;
    RMII_FREE_RESET = 1;


    if (netstack) {
    	for(int i=0;i<64;i++) {
    		RMII_FREE_PUT = (uint8_t)i;
    	}
    	netstack->set_mac_address(local_mac);
    	netstack->start();
    	link_up = false;
    	ioWrite8(ITU_IRQ_ENABLE, ITU_INTERRUPT_RMIIRX);
    }
    RMII_RX_ENABLE = 1;
/*
    uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    for (int i=700;i<1530;i+=16) {
    	output_packet(broadcast, i);
    	vTaskDelay(10);
    }
*/

    struct EthPacket pkt;
    while(1) {
		if (xQueueReceive(queue, &pkt, 200) == pdTRUE) {
			input_packet(&pkt);
		} else {
			uint16_t status = mdio_read(1);
			if(status & 0x04) {
				if(!link_up) {
					printf("Bringing link up.\n");
					if (netstack)
						netstack->link_up();
					link_up = true;
				}
			} else {
				if(link_up) {
					printf("Bringing link down.\n");
					if (netstack)
						netstack->link_down();
					link_up = false;
				}
			}
		}
	}
}

RmiiInterface :: ~RmiiInterface()
{
	if(ram_buffer) {
		delete ram_buffer;
	}
	if(netstack) {
		netstack->stop();
		releaseNetworkStack(netstack);
	}
}


void RmiiInterface :: rx_interrupt_handler(void)
{
	struct EthPacket pkt;
	pkt.size = RMII_ALLOC_SIZE;
	pkt.id   = RMII_ALLOC_ID;
	RMII_ALLOC_POP = 1;
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	xQueueSendFromISR(rmii_interface.queue, &pkt, &xHigherPriorityTaskWoken);
}

void RmiiInterface :: input_packet(struct EthPacket *pkt)
{
	uint32_t offset = ((uint32_t) pkt->id) * 1536;
	uint8_t *buffer = &ram_base[offset];

	//printf("Rmii Rx: %b %p (%d)\n", pkt->id, buffer, pkt->size);
	//dump_hex_relative(buffer+2, pkt->size);

	if(netstack) {
		if (!netstack->input(buffer, buffer+2, pkt->size)) {
			RMII_FREE_PUT = pkt->id;
		}
	} else {
		RMII_FREE_PUT = pkt->id;
	}
}
 	
void RmiiInterface :: free_buffer(uint8_t *buffer)
{
	uint32_t offset = (uint32_t)buffer - (uint32_t)ram_base;
	uint8_t id = (offset / 1536);
	//printf("FREE PBUF CALLED %p (=> %8x => %b)!\n", buffer, offset, id);
	RMII_FREE_PUT = id;
}

uint8_t RmiiInterface :: output_packet(uint8_t *buffer, int pkt_len)
{
	if (!link_up)
		return 0;

	if (RMII_TX_BUSY) {
		printf("Oops.. tx is busy!\n");
	}
	//printf("Rmii Out Packet: %p %4x\n", buffer, pkt_len);
	//dump_hex_relative(buffer, (pkt_len > 64)?64:pkt_len);
	RMII_TX_ADDRESS = (uint32_t)buffer;
	RMII_TX_LENGTH  = (uint16_t)((pkt_len < 60)?60:pkt_len);
	RMII_TX_START   = 1;

	return 0;
}
