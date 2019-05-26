#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "network_interface.h"
#include "rmii_interface.h"
#include "dump_hex.h"
#include "flash.h"
#include "mdio.h"

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

// configuration
void RmiiInterface_config(void *drv, int feature, void *params) {
    ((RmiiInterface *)drv)->configure(feature, params);
}

extern "C" {
	void RmiiRxInterruptHandler(void)
	{
		rmii_interface.rx_interrupt_handler();
	}
}

RmiiInterface :: RmiiInterface()
{
    my_ip = 0;
    vic_dest_ip = 0;
    vic_dest_port = 0;
    vic_enable = 0;

    if(getFpgaCapabilities() & CAPAB_ETH_RMII) {
		netstack = NULL;
		link_up = false;
		ram_buffer = new uint8_t[(128 * 1536) + 256];
		ram_base = (uint8_t *) (((uint32_t)ram_buffer + 255) & 0xFFFFFF00);

		RMII_FREE_BASE = (uint32_t)ram_base;

		// printf("Rmii RAM buffer: %p.. Base = %p\n", ram_buffer, ram_base);

		mdio_write(0x1B, 0x0500); // enable link up, link down interrupts
		mdio_write(0x16, 0x0002); // disable factory reset mode

		if (ram_buffer) {
			xTaskCreate( RmiiInterface :: startRmiiTask, "RMII Driver Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
			queue = xQueueCreate(128, sizeof(struct EthPacket));
		}
    }
}

void RmiiInterface :: startRmiiTask(void *a)
{
	rmii_interface.rmiiTask();
}

void RmiiInterface :: rmiiTask(void)
{
	netstack = getNetworkStack(this, RmiiInterface_config, RmiiInterface_output, RmiiInterface_free_buffer);

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
    	if (!link_up) {
			uint16_t status = mdio_read(1);
			if(status & 0x04) {
				if(!link_up) {
					//printf("Bringing link up.\n");
					if (netstack)
						netstack->link_up();
					link_up = true;
				}
			} else {
				vTaskDelay(50);
			}
    	} else if (xQueueReceive(queue, &pkt, 200) == pdTRUE) {
			input_packet(&pkt);
		} else { // Link is up, but not received a packet in 1 second
			uint16_t status = mdio_read(1);
			if ((status & 0x04) == 0) {
				if(link_up) {
					//printf("Bringing link down.\n");
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
		return 1;
	}
	//printf("Rmii Out Packet: %p %4x\n", buffer, pkt_len);
	//dump_hex_relative(buffer, (pkt_len > 64)?64:pkt_len);
	RMII_TX_ADDRESS = (uint32_t)buffer;
	RMII_TX_LENGTH  = (uint16_t)((pkt_len < 60)?60:pkt_len);
	RMII_TX_START   = 1;

	return 0;
}

/*
uint8_t rmiiTransmit(uint8_t *buffer, int pkt_len)
{
	return rmii_interface.output_packet(buffer, pkt_len);
}
*/

void RmiiInterface :: configure(int feature, void *params)
{
    switch(feature) {
    case NET_FEATURE_SET_IP:
        my_ip = *((uint32_t *)params);
        break;

    case 100:
        vic_enable = *((uint8_t *)params);
        break;

    case 101:
        vic_dest_ip = *((uint32_t *)params);
        break;

    case 102:
        vic_dest_port = *((int *)params);
        break;
    }

    calculate_udp_headers();
}

#include "u64.h"
void RmiiInterface :: calculate_udp_headers()
{
    printf("__Calculate UDP Headers__\nIP = %08x, Dest = %08x:%d, Enable = %d\n", my_ip, vic_dest_ip, vic_dest_port, vic_enable);

    uint8_t *hardware = (uint8_t *)U64_UDP_BASE;

    uint8_t header[42] = { 0x01, 0x00, 0x5E, 0x00, 0x01, 0x40, // destination MAC
                           0x00, 0x15, 0x41, 0xAA, 0xAA, 0x01, // source MAC
                           0x08, 0x00, // ethernet type = IP
                           // Ethernet Header (offset 14)
                           0x45, 0x00,
                           0x00, 0x1C, // 16, length = 28, but needs to be updated
                           0xBB, 0xBB, // 18, ID // sum = 100e5 => 00e6
                           0x40, 0x00, // 20, fragmenting and stuff => disable, no offset
                           0x03, 0x11, // 22, TTL, protocol = UDP
                           0x00, 0x00, // 24, checksum to be filled in
                           0xEF, 0x00, 0x01, 0x40, // 26, source IP
                           0xC0, 0xA8, 0x01, 0x40, // 30, destination IP
                           // UDP header
                           0xD0, 0x00, // 34, source port (53248 = VIC)
                           0x2A, 0xF8, // 36, destination port (11000)
                           0x00, 0x08, // 38, Length of UDP = 8 + payload length
                           0x00, 0x00 }; // 40, unused checksum

    if ((my_ip == 0) || (vic_enable == 0)) {
        hardware[63] = 0; // disable
        return;
    }

    // Source MAC
    memcpy(header+6, local_mac, 6);

    // Source IP
    header[29] = (uint8_t)(my_ip >> 24);
    header[28] = (uint8_t)(my_ip >> 16);
    header[27] = (uint8_t)(my_ip >> 8);
    header[26] = (uint8_t)(my_ip >> 0);

    // Destination IP
    header[33] = (uint8_t)(vic_dest_ip >> 24);
    header[32] = (uint8_t)(vic_dest_ip >> 16);
    header[31] = (uint8_t)(vic_dest_ip >> 8);
    header[30] = (uint8_t)(vic_dest_ip >> 0);

    // destination mac
    if ((vic_dest_ip & 0x000000F8) == 0x000000E8) {
        header[3] = header[31] & 0x7F;
        header[4] = header[32];
        header[5] = header[33];
    } else { // also covers the case 255.255.255.255
        memset(header, 0xFF, 6); // broadcast
    }

    // Destination port
    header[36] = (uint8_t)(vic_dest_port >> 8);
    header[37] = (uint8_t)(vic_dest_port & 0xFF);

    // Now recalculate the IP checksum
    uint32_t sum = 0;
    for (int i=0; i < 10; i++) {
        uint16_t temp = ((uint16_t)header[2*i + 14]) << 8;
        temp |= header[2*i + 15];
        sum += temp;
    }
    // one's complement fix up
    while(sum & 0xFFFF0000) {
        sum += (sum >> 16);
        sum &= 0xFFFF;
    }
    // write back
    header[24] = 0xFF ^ (uint8_t)(sum >> 8);
    header[25] = 0xFF ^ (uint8_t)sum;

    // copy to hw
    for(int i=0;i<42;i++) {
        hardware[i] = header[i];
    }

    dump_hex_relative(header, 42);

    // enable
    hardware[63] = 1;
}

