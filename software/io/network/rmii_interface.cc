#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "network_interface.h"
#include "rmii_interface.h"
#include "dump_hex.h"
#include "flash.h"
#include "mdio.h"
#include "endianness.h"
#include "init_function.h"

RmiiInterface *rmii_interface = NULL; // global object!
static void init(void *_a, void *_b)
{
    rmii_interface = new RmiiInterface();
}
InitFunction rmii_init_func("RMII Interface", init, NULL, NULL, 51);

// entry point for free buffer callback
void RmiiInterface_free_buffer(void *drv, void *b) {
	((RmiiInterface *)drv)->free_buffer((uint8_t *)b);
}

// entry point for output packet callback
err_t RmiiInterface_output(void *drv, void *b, int len) {
	return ((RmiiInterface *)drv)->output_packet((uint8_t *)b, len);
}

extern "C" {
	void RmiiRxInterruptHandler(void)
	{
		rmii_interface->rx_interrupt_handler();
	}
}

RmiiInterface :: RmiiInterface()
{
	rmii_interface = this;
    if(getFpgaCapabilities() & CAPAB_ETH_RMII) {
    	netstack = getNetworkStack(this, RmiiInterface_output, RmiiInterface_free_buffer);
		link_up = false;
		ram_buffer = new uint8_t[(128 * 1536) + 256];
		ram_base = (uint8_t *) (((uint32_t)ram_buffer + 255) & 0xFFFFFF00);

		RMII_FREE_BASE = (uint32_t)ram_base;

		// printf("Rmii RAM buffer: %p.. Base = %p\n", ram_buffer, ram_base);
		addr = 0;
		uint16_t phy_ident = mdio_read(0x02, addr);
		if (phy_ident != 0x0022) {
			addr = 3;
			phy_ident = mdio_read(0x02, addr);
			if (phy_ident != 0x0022) {
				printf("--> RmiiInterface could not find Ethernet PHY!\n");
			} else {
				printf("--> RmiiInterface found PHY at MDIO address 3\n");
			}
		}

        mdio_write(0x04, 0x01E1, addr); // set the correct auto negotiation bits
        mdio_write(0x1B, 0x0500, addr); // enable link up, link down interrupts
        mdio_write(0x16, 0x0002, addr); // disable factory test mode, in case it was enabled
        mdio_write(0x00, 0x1200, addr); // restart auto negotiation

        if (ram_buffer) {
            queue = xQueueCreate(128, sizeof(struct EthPacket));
			xTaskCreate( RmiiInterface :: startRmiiTask, "RMII Driver Task", configMINIMAL_STACK_SIZE, this, PRIO_DRIVER, NULL );
        }
    }
}

void RmiiInterface :: startRmiiTask(void *a)
{
	rmii_interface->rmiiTask();
}

void RmiiInterface :: initRx(void)
{
    RMII_RX_ENABLE  = 0;
    vTaskDelay(1); // wait a few ms
    RMII_FREE_RESET = 1;
    for(int i=0;i<127;i++) {
        RMII_FREE_PUT = (uint8_t)i;
    }
//    printf("RMII Initialized, pointers: %08x\n", RMII_FREE_BASE);
    RMII_RX_ENABLE = 1;
    ioWrite8(ITU_IRQ_ENABLE, ITU_INTERRUPT_RMIIRX);
}

void RmiiInterface :: deinitRx(void)
{
    RMII_RX_ENABLE  = 0;
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_RMIIRX);
}

void RmiiInterface :: rmiiTask(void)
{
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
    if (netstack) {
    	netstack->set_mac_address(local_mac);
    	netstack->start();
    	link_up = false;
    }

/*
    uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    for (int i=700;i<1530;i+=16) {
    	output_packet(broadcast, i);
    	vTaskDelay(10);
    }
*/

    struct EthPacket pkt;
    while(1) {
    	if (!link_up) {
			uint16_t status = mdio_read(1, addr);
			if(status & 0x04) {
				if(!link_up) {
					//printf("Bringing link up.\n");
					if (netstack) {
					    initRx();
						netstack->link_up();
					}
					link_up = true;
				}
			} else {
				vTaskDelay(50);
			}
    	} else if (xQueueReceive(queue, &pkt, 200) == pdTRUE) {
			input_packet(&pkt);
		} else { // Link is up, but not received a packet in 1 second
			uint16_t status = mdio_read(1, addr);
			if ((status & 0x04) == 0) {
				if(link_up) {
					//printf("Bringing link down.\n");
					if (netstack) {
						deinitRx();
						netstack->link_down();
					}
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
	xQueueSendFromISR(rmii_interface->queue, &pkt, &xHigherPriorityTaskWoken);
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
	//printf("FREE PBUF CALLED %p (=> %8x => %b)! Pointers: %08x %b\n", buffer, offset, id, RMII_FREE_BASE, RMII_FREE_PUT);
	RMII_FREE_PUT = id;
}

err_t RmiiInterface :: output_packet(uint8_t *buffer, int pkt_len)
{
	if (!link_up)
		return ERR_CONN;

	if (RMII_TX_BUSY) {
		printf("Oops.. tx is busy!\n");
		return ERR_INPROGRESS;
	}
	//printf("Rmii Out Packet: %p %4x\n", buffer, pkt_len);
	//dump_hex_relative(buffer, (pkt_len > 64)?64:pkt_len);
	RMII_TX_ADDRESS = (uint32_t)buffer;
	RMII_TX_LENGTH  = (uint16_t)((pkt_len < 60)?60:pkt_len);
	RMII_TX_START   = 1;

	return ERR_OK;
}
