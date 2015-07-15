#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itu.h"
#include "dump_hex.h"
#include "integer.h"
#include "usb_ax88772.h"
#include "event.h"
#include "profiler.h"

#define DEBUG_RAW_PKT 0
#define DEBUG_INVALID_PKT 0

uint8_t c_req_phy_access[]    = { 0x40, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_read_phy_reg[8]     = { 0xC0, 0x07, 0x10, 0x00, 0xFF, 0x00, 0x02, 0x00 };
uint8_t c_write_phy_reg[8]    = { 0x40, 0x08, 0x10, 0x00, 0xFF, 0x00, 0x02, 0x00 };
uint8_t c_release_access[]    = { 0x40, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

uint8_t c_srom_read_reg[8]    = { 0xC0, 0x0B, 0xAA, 0x00, 0x00, 0x00, 0x02, 0x00 };
uint8_t c_srom_write_reg[8]   = { 0x40, 0x0C, 0xAA, 0x00, 0xCC, 0xDD, 0x00, 0x00 };
uint8_t c_srom_write_enable[] = { 0x40, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_srom_write_disable[]= { 0x40, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

uint8_t c_read_rx_ctrl[]      = { 0xC0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
uint8_t c_write_rx_control[]  = { 0x40, 0x10, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_clear_rx_ctrl[]     = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_ipg_regs[]    = { 0x40, 0x12, 0x1D, 0x00, 0x12, 0x00, 0x00, 0x00 };
uint8_t c_get_mac_address[]   = { 0xC0, 0x13, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00 };
uint8_t c_read_phy_addr[]     = { 0xC0, 0x19, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
uint8_t c_read_medium_mode[]  = { 0xC0, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
uint8_t c_write_medium_mode[] = { 0x40, 0x1B, 0x36, 0x03, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_gpio[]        = { 0x40, 0x1f, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_softw_rst1[]  = { 0x40, 0x20, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_softw_rst2[]  = { 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_softw_rst3[]  = { 0x40, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_softw_rst4[]  = { 0x40, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_softw_rst5[]  = { 0x40, 0x20, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_write_softw_sel[]   = { 0x40, 0x22, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };

uint8_t c_write_rx_burst[]    = { 0x40, 0x2A, 0x00, 0x80, 0x01, 0x80, 0x00, 0x00 }; // set burst to 2K

const uint16_t good_srom[] = {
	0x155A, 0xED71, 0x2012, 0x2927, 0x000E, 0xAA00, 0x4A4A, 0x0904,
	0x6022, 0x7112, 0x190E, 0x3D04, 0x3D04, 0x3D04, 0x3D04, 0x9C05,
	0x0006, 0x10E0, 0x4224, 0x4012, 0x4927, 0xFFFF, 0x0000, 0xFFFF,
	0xFFFF, 0x0E03, 0x3000, 0x3000, 0x3000, 0x3000, 0x3000, 0x3200,
	0x1201, 0x0002, 0xFFFF, 0x0040, 0x950B, 0x2A77, 0x0100, 0x0102,
	0x0301, 0x0902, 0x2700, 0x0101, 0x04E0, 0x7D09, 0x0400, 0x0003,
	0xFFFF, 0x0007, 0x0705, 0x8103, 0x0800, 0x0B07, 0x0582, 0x0200,
	0x0200, 0x0705, 0x0302, 0x0002, 0x00FF, 0x0403, 0x3000, 0xFFFF,
	0x1201, 0x0002, 0xFFFF, 0x0008, 0x950B, 0x2A77, 0x0100, 0x0102,
	0x0301, 0x0902, 0x2700, 0x0101, 0x04E0, 0x7D09, 0x0400, 0x0003,
	0xFFFF, 0x0007, 0x0705, 0x8103, 0x0800, 0xA007, 0x0582, 0x0240,
	0x0000, 0x0705, 0x0302, 0x4000, 0x00DD, 0xFFFF, 0xAAAA, 0xBBBB,
	0x2203, 0x4100, 0x5300, 0x4900, 0x5800, 0x2000, 0x4500, 0x6C00,
	0x6500, 0x6300, 0x2E00, 0x2000, 0x4300, 0x6F00, 0x7200, 0x7000,
	0x2E00, 0x1203, 0x4100, 0x5800, 0x3800, 0x3800, 0x7800, 0x3700,
	0x3200, 0x4100, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

__inline uint32_t cpu_to_32le(uint32_t a)
{
    uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

__inline uint16_t le16_to_cpu(uint16_t h)
{
    return (h >> 8) | (h << 8);
}

// Entry point for call-backs.
void UsbAx88772Driver_interrupt_callback(uint8_t *data, int data_length, void *object) {
	((UsbAx88772Driver *)object)->interrupt_handler(data, data_length);
}

// Entry point for call-backs.
void UsbAx88772Driver_bulk_callback(uint8_t *data, int data_length, void *object) {
	((UsbAx88772Driver *)object)->bulk_handler(data, data_length);
}

// entry point for free buffer callback
void UsbAx88772Driver_free_buffer(void *drv, void *b) {
	((UsbAx88772Driver *)drv)->free_buffer((uint8_t *)b);
}

// entry point for output packet callback
uint8_t UsbAx88772Driver_output(void *drv, void *b, int len) {
	return ((UsbAx88772Driver *)drv)->output_packet((uint8_t *)b, len);
}

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/
// tester instance
FactoryRegistrator<UsbDevice *, UsbDriver *> ax88772_tester(UsbDevice :: getUsbDriverFactory(), UsbAx88772Driver :: test_driver);


UsbAx88772Driver :: UsbAx88772Driver()
{
    device = NULL;
    host = NULL;
    netstack = NULL;
    link_up = false;
}

UsbAx88772Driver :: ~UsbAx88772Driver()
{
	if(netstack)
		releaseNetworkStack(netstack);
}

UsbDriver * UsbAx88772Driver :: test_driver(UsbDevice *dev)
{
	//printf("** Test USB Asix AX88772 Driver **\n");

    if(le16_to_cpu(dev->device_descr.vendor) != 0x0b95) {
		printf("Device is not from Asix..\n");
		return 0;
	}
	if((le16_to_cpu(dev->device_descr.product) & 0xFFFE) != 0x772A) {
		printf("Device product ID is not AX88772A.\n");
		return 0;
	}

    printf("** Asix AX88772A found **\n");
	// TODO: More tests needed here?
	return new UsbAx88772Driver();
}

void UsbAx88772Driver :: install(UsbDevice *dev)
{
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    device = dev;
    
	dev->set_configuration(dev->get_device_config()->config_value);

    netstack = getNetworkStack(this, UsbAx88772Driver_output, UsbAx88772Driver_free_buffer);
    if(!netstack) {
    	puts("** Warning! No network stack available!");
    }

    read_mac_address();

    bulk_in  = dev->find_endpoint(0x82);
    bulk_out = dev->find_endpoint(0x02);

	bulk_out_pipe.DevEP = (device->current_address << 8) | 3;
	bulk_out_pipe.MaxTrans = 512;
	bulk_out_pipe.SplitCtl = 0;
	bulk_out_pipe.Command = 0;

    uint16_t dummy_read;

    // * 40 1f b0: Write GPIO register
    host->control_exchange(&device->control_pipe,
                           c_write_gpio, 8,
                           temp_buffer, 1);
    // * c0 19   : Read PHY address register  (response: e0 10)
    host->control_exchange(&device->control_pipe,
                           c_read_phy_addr, 8,
                           temp_buffer, 2);
    // * 40 22 01: Write Software Interface Selection Selection Register
    host->control_exchange(&device->control_pipe,
                           c_write_softw_sel, 8,
						   temp_buffer, 1);
    // * 40 20 48: Write software reset register
    host->control_exchange(&device->control_pipe,
                           c_write_softw_rst1, 8,
						   temp_buffer, 1);
    // * 40 20 00: Write software reset register
    host->control_exchange(&device->control_pipe,
                           c_write_softw_rst2, 8,
						   temp_buffer, 1);
    // * 40 20 20: Write software reset register
    host->control_exchange(&device->control_pipe,
                           c_write_softw_rst3, 8,
						   temp_buffer, 1);
    // * 40 10 00 00 : write Rx Control register
    host->control_exchange(&device->control_pipe,
                           c_clear_rx_ctrl, 8,
						   temp_buffer, 1);
    // * c0 0f   : read Rx Control register (response: 00 00)
    host->control_exchange(&device->control_pipe,
                           c_read_rx_ctrl, 8,
                           temp_buffer, 2);
    // % c0 07 10 00 02: Read PHY ID 10, Register address 02 (resp: 3b 00)
    dummy_read = read_phy_register(2);
    // % c0 07 10 00 03: Read PHY 10, Reg 03 (resp: 61 18)
    dummy_read = read_phy_register(3);
    // * 40 20 08: Write Software reset register
    host->control_exchange(&device->control_pipe,
                           c_write_softw_rst4, 8,
						   temp_buffer, 1);
    // * 40 20 28: Write Software reset register
    host->control_exchange(&device->control_pipe,
                           c_write_softw_rst5, 8,
						   temp_buffer, 1);
    // % 40 08 10 -- 00: Write PHY, Reg 00 (data phase: 00 80) - Reset PHY
    write_phy_register(0, 0x8000);
    // % 40 08 10 -- 04: Write PHY, Reg 04 (data phase: e1 01) - Capabilities
    write_phy_register(4, 0x01E1);
    // % c0 07 10 -- 00: Read PHY, Reg 00 (resp: 00 31) - Speed=100,AutoNeg,FullDup
    dummy_read = read_phy_register(0);
    // % 40 08 10 -- 00: Write PHY, Reg 00 (data phase: 00 33) - +restart autoneg - unreset
    write_phy_register(0, 0x3300);
    // * 40 1b 36 03 : Write medium mode register
    host->control_exchange(&device->control_pipe,
                           c_write_medium_mode, 8,
						   temp_buffer, 1);
    // * 40 12 1d 00 12: Write IPG registers
    host->control_exchange(&device->control_pipe,
                           c_write_ipg_regs, 8,
						   temp_buffer, 1);
    // * 40 2A 8000 8001 : Write Rx Burst Length register. Set it to 2K
    host->control_exchange(&device->control_pipe,
    					   c_write_rx_burst, 8,
						   temp_buffer, 1);

    // * 40 10 88 00 : Write Rx Control register, start operation, enable broadcast
    host->control_exchange(&device->control_pipe,
                           c_write_rx_control, 8,
						   temp_buffer, 1);
    // * c0 0f : Read Rx Control Register (response: 88 00)
    host->control_exchange(&device->control_pipe,
                           c_read_rx_ctrl, 8,
						   temp_buffer, 2);
    // * c0 1a : Read Medium Status register (response: 36 03)
    host->control_exchange(&device->control_pipe,
                           c_read_medium_mode, 8,
                           temp_buffer, 2);

/*
    // % c0 07 10 00 01: Read PHY reg 01 (resp: 09 78) 
    temp_buffer = read_phy_register(1);
    // % c0 07 10 00 01: Read PHY reg 01 (resp: 09 78)
    temp_buffer = read_phy_register(1);
    // % c0 07 10 00 00: Read PHY reg 00 (resp: 00 31)
    temp_buffer = read_phy_register(0);
    // % c0 07 10 00 01: Read PHY reg 01 (resp: 09 78)
    temp_buffer = read_phy_register(1);
    // % c0 07 10 00 04: Read PHY reg 04 (resp: e1 01)
    temp_buffer = read_phy_register(4);
*/
    
    if (netstack) {
    	//printf("Network stack: %s\n", netstack->identify());
    	struct t_pipe ipipe;
		ipipe.DevEP = uint16_t((device->current_address << 8) | 1);
		ipipe.Interval = 8000; // 1 Hz
		ipipe.Length = 16; // just read 16 bytes max
		ipipe.MaxTrans = 64;
		ipipe.SplitCtl = 0;
		ipipe.Command = 0; // driver will fill in the command

		irq_transaction = host->allocate_input_pipe(&ipipe, UsbAx88772Driver_interrupt_callback, this);

		ipipe.DevEP = uint16_t((device->current_address << 8) | 2);
		ipipe.Interval = 1; // fast!
		ipipe.Length = 1536; // big blocks!
		ipipe.MaxTrans = 512;
		ipipe.SplitCtl = 0;
		ipipe.Command = 0; // driver will fill in the command

		bulk_transaction = host->allocate_input_pipe(&ipipe, UsbAx88772Driver_bulk_callback, this);

		netstack->start();
    }
}

void UsbAx88772Driver :: deinstall(UsbDevice *dev)
{
    if (netstack) {
    	netstack->stop();

		host->free_input_pipe(irq_transaction);
		host->free_input_pipe(bulk_transaction);
    }
    printf("AX88772 Deinstalled.\n");
}

void UsbAx88772Driver :: write_phy_register(uint8_t reg, uint16_t value) {
    c_write_phy_reg[4] = reg;

    host->control_exchange(&device->control_pipe,
                           c_req_phy_access, 8,
						   temp_buffer, 1);
    temp_buffer[0] = uint8_t(value & 0xFF);
    temp_buffer[1] = uint8_t(value >> 8);
    host->control_write(&device->control_pipe,
                           c_write_phy_reg, 8,
						   temp_buffer, 2);
    host->control_exchange(&device->control_pipe,
                           c_release_access, 8,
						   temp_buffer, 1);
}

uint16_t UsbAx88772Driver :: read_phy_register(uint8_t reg) {
    c_read_phy_reg[4] = reg;

    host->control_exchange(&device->control_pipe,
                           c_req_phy_access, 8,
						   temp_buffer, 1);
    host->control_exchange(&device->control_pipe,
                           c_read_phy_reg, 8,
						   temp_buffer, 2);
    uint16_t retval = uint16_t(temp_buffer[0]) | (uint16_t(temp_buffer[1]) << 8);
    host->control_exchange(&device->control_pipe,
                           c_release_access, 8,
						   temp_buffer, 1);
    return retval;
}


void UsbAx88772Driver :: poll(void)
{
	if(netstack)
		netstack->poll();
}

void UsbAx88772Driver :: interrupt_handler(uint8_t *irq_data, int data_len)
{
    //printf("AX88772 (ADDR=%d) IRQ data: ", device->current_address);
	//for(int i=0;i<data_len;i++) {
	//	printf("%b ", irq_data[i]);
	//} printf("\n");

	if(irq_data[2] & 0x01) {
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
	host->resume_input_pipe(this->irq_transaction);
}

void UsbAx88772Driver :: bulk_handler(uint8_t *usb_buffer, int data_len)
{
	//printf("Packet %p Len: %d\n", usb_buffer, data_len);
	PROFILER_SUB = 6;

	if (!link_up) {
		host->free_input_buffer(bulk_transaction, usb_buffer);
		PROFILER_SUB = 0;
		return;
	}

	uint16_t pkt_size  = uint16_t(usb_buffer[0]) | (uint16_t(usb_buffer[1])) << 8;
	uint16_t pkt_size2 = (uint16_t(usb_buffer[2]) | (uint16_t(usb_buffer[3])) << 8) ^ 0xFFFF;

	pkt_size &= 0x7FF;
	pkt_size2 &= 0x7FF;

	//printf("Packet_sizes: %d %d\n", pkt_size, pkt_size2);

	if (pkt_size != pkt_size2) {
		printf("ERROR: Corrupted packet %4x %4x\n", pkt_size, pkt_size2);
		host->free_input_buffer(bulk_transaction, usb_buffer);
        //LINK_STATS_INC(link.drop);
		PROFILER_SUB = 0;
		return;
	}

	if (int(pkt_size) > (data_len - 4)) {
		printf("ERROR: Not enough data? %d %d\n", pkt_size, data_len);
		host->free_input_buffer(bulk_transaction, usb_buffer);
        //LINK_STATS_INC(link.drop);
		PROFILER_SUB = 0;
		return;
	}

	//LINK_STATS_INC(link.recv);

	if(netstack) {
		if (!netstack->input(usb_buffer, usb_buffer+4, pkt_size)) {
			host->free_input_buffer(bulk_transaction, usb_buffer);
		} /*else { // this else is to free immediately. this is temporary in order to test. free from lwip is not performed
			host->free_input_buffer(bulk_transaction, usb_buffer);
		}*/
	} else {
		host->free_input_buffer(bulk_transaction, usb_buffer);
	}
}
 	
void UsbAx88772Driver :: free_buffer(uint8_t *buffer)
{
//	printf("FREE PBUF CALLED %p!\n", buffer);
	host->free_input_buffer(bulk_transaction, buffer);
}

void UsbAx88772Driver :: read_srom()
{
	uint32_t temp_buf = 0;
	//BYTE local_buffer[128];
	for(int i=0;i<256;i++) {
		c_srom_read_reg[2] = (uint8_t)i;
		device->host->control_exchange(&device->control_pipe,
										c_srom_read_reg, 8,
	                                    &temp_buf, 2);
		printf("%3x: %4x\n", i, (temp_buf >> 16));
	}
}

void UsbAx88772Driver :: write_srom()
{
	uint8_t c_srom_write_reg[8]   = { 0x40, 0x0C, 0xAA, 0x00, 0xCC, 0xDD, 0x00, 0x00 };

	device->host->control_exchange(&device->control_pipe,
									c_srom_write_enable, 8,
                                    NULL, 0);

	for(int i=0;i<128;i++) {
		uint16_t data = good_srom[i];
		c_srom_write_reg[2] = (uint8_t)i;
		*((uint16_t *)&c_srom_write_reg[4]) = data;
		device->host->control_exchange(&device->control_pipe,
										c_srom_write_reg, 8,
	                                    NULL, 0);
	}

	device->host->control_exchange(&device->control_pipe,
									c_srom_write_disable, 8,
                                    NULL, 0);
}

bool UsbAx88772Driver :: read_mac_address()
{
	uint8_t local_mac[8];
	int i = device->host->control_exchange(&device->control_pipe,
                                           c_get_mac_address, 8,
                                           local_mac, 6);
	uint8_t check = 0;
	if(i == 6) {
        printf("MAC address:  ");
        for(int i=0;i<6;i++) {
            printf("%b%c", local_mac[i], (i==5)?' ':':');
            check |= local_mac[i];
        } printf("\n");
        if (!check) { // no mac address!!
//        	printf("Reading MAC address failed.. Going to program EEPROM with good data.\n");
//        	write_srom();
//        	printf("Writing SROM completed. Now re-insert.\n");
//        	return false;
        	printf("Reading MAC address failed.. Setting a default MAC.");
        	return write_mac_address();
        }
        if (netstack)
        	netstack->set_mac_address(local_mac);
        return true;
    }
    return false;
}

bool UsbAx88772Driver :: write_mac_address(void)
{
    uint8_t c_write_mac[] = { 0x40, 0x14, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00 };
    uint8_t new_mac[] = { 0x00, 0x4c, 0x49, 0x4c, 0x49, 0x00 };

    host->control_write(&device->control_pipe,
                        c_write_mac, 8,
                        new_mac, 6);

    return read_mac_address();
}

void UsbAx88772Driver :: pipe_error(int pipe) // called from IRQ!
{
	bool stop_stack = false;
	printf("UsbAx88772 ERROR ");
	if (pipe == irq_transaction) {
		printf("on IRQ pipe. ");
		stop_stack = true;
	} else if (pipe == bulk_transaction) {
		printf("on bulk pipe. ");
		stop_stack = true;
	} else {
		printf("Internal error.\n");
	}
	if (stop_stack) {
		// FIXME: push message in queue from interrupt
		if (netstack) {
			netstack->stop();
		}
		printf("Net stack stopped\n");
	}
}


uint8_t UsbAx88772Driver :: output_packet(uint8_t *buffer, int pkt_len)
{
	//printf("OUTPUT: payload = %p. Size = %d\n", buffer, pkt_len);
	if (!link_up)
		return 0;
	//dump_hex(buffer, 32);

	uint8_t *size = buffer - 4;
    size[0] = uint8_t(pkt_len & 0xFF);
    size[1] = uint8_t(pkt_len >> 8);
    size[2] = size[0] ^ 0xFF;
    size[3] = size[1] ^ 0xFF;

	//dump_hex(p, p->len + sizeof(struct pbuf));

	host->bulk_out(&bulk_out_pipe, size, pkt_len + 4);

	return 0;
}
