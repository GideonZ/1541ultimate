#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itu.h"
#include "dump_hex.h"
#include "integer.h"
#include "usb_ax88772.h"
#include "profiler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "endianness.h"

#define DEBUG_RAW_PKT 0
#define DEBUG_INVALID_PKT 0

#if DATACACHE
#define BIT31 0x80000000
#else
#define BIT31 0
#endif

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
uint8_t c_write_medium_mode[] = { 0x40, 0x1B, 0x16, 0x03, 0x00, 0x00, 0x00, 0x00 }; // disabled pause frames
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

// Entry point for call-backs.
void UsbAx88772Driver_interrupt_callback(void *object) {
	((UsbAx88772Driver *)object)->interrupt_handler();
}

// Entry point for call-backs.
void UsbAx88772Driver_bulk_callback(void *object) {
	((UsbAx88772Driver *)object)->bulk_handler();
}

// entry point for free buffer callback
void UsbAx88772Driver_free_buffer(void *drv, void *b) {
	((UsbAx88772Driver *)drv)->free_buffer((uint8_t *)b);
}

// entry point for output packet callback
err_t UsbAx88772Driver_output(void *drv, void *b, int len) {
	return ((UsbAx88772Driver *)drv)->output_packet((uint8_t *)b, len);
}

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/
// tester instance
FactoryRegistrator<UsbInterface *, UsbDriver *> ax88772_tester(UsbInterface :: getUsbDriverFactory(), UsbAx88772Driver :: test_driver);


UsbAx88772Driver :: UsbAx88772Driver(UsbInterface *intf, uint16_t prodID) : UsbDriver(intf), freeBuffers(NUM_AX_BUFFERS, NULL)
{
    device = NULL;
    host = NULL;
    netstack = NULL;
    link_up = false;
    this->prodID = prodID;

    dataBuffersBlock = new uint8_t[1536 * NUM_AX_BUFFERS];

    for (int i=0; i < NUM_AX_BUFFERS; i++) {
        freeBuffers.push(&dataBuffersBlock[BIT31 + 1536 * i]); // set bit 31, so that it is non-cacheable
    }
}

UsbAx88772Driver :: ~UsbAx88772Driver()
{
	if(netstack)
		releaseNetworkStack(netstack);
	delete[] dataBuffersBlock;
}

UsbDriver * UsbAx88772Driver :: test_driver(UsbInterface *intf)
{
	//printf("** Test USB Asix AX88772 Driver **\n");

	UsbDevice *dev = intf->getParentDevice();
	if(le16_to_cpu(dev->device_descr.vendor) != 0x0b95) {
		// printf("Device is not from Asix..\n");
		return 0;
	}
    uint16_t prodID = le16_to_cpu(dev->device_descr.product);
    if (((prodID & 0xFFFE) != 0x772A) && (prodID != 0x7720)) {
		// printf("Device product ID is not AX88772.\n");
		return 0;
	}

    printf("** Asix AX88772%c found **\n", (prodID & 1)?'B':'A');
	// TODO: More tests needed here?
	return new UsbAx88772Driver(intf, prodID);
}

void UsbAx88772Driver :: install(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    interface = intf;
    device = intf->getParentDevice();
    
	dev->set_configuration(dev->get_device_config()->config_value);

#if 0
	static uint8_t testje[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static uint8_t testje_512_offset[516];

    printf("Testje: %p\n", testje);
	for(int i=1; i<=8; i++) {
		for(int k=0; k<4; k++) {
			if ((k == 1) && (i == 6)) {
				printf("Nice case to trigger.\n");
			}
			host->bulk_out(&device->control_pipe, testje+k, i);
			for(int j=0;j<16;j++) {
				testje[j] += 16;
			}
		}
    }

    device->control_pipe.MaxTrans = 512;

    for(int k=0; k<4; k++) {
	    testje_512_offset[0] = 0xEE;
	    testje_512_offset[1] = 0xDD;
	    testje_512_offset[2] = 0xCC;
	    testje_512_offset[3] = 0xBB;
	    testje_512_offset[512] = 0xAA;
	    testje_512_offset[513] = 0xBB;
	    testje_512_offset[514] = 0xCC;
	    testje_512_offset[515] = 0xDD;
		for(int i=0;i<512;i++) {
			testje_512_offset[i+k] = (uint8_t)i;
		}
	    host->bulk_out(&device->control_pipe, &testje_512_offset[k], 512);
    }

#endif

    netstack = getNetworkStack(this, UsbAx88772Driver_output, UsbAx88772Driver_free_buffer);
    if(!netstack) {
    	puts("** Warning! No network stack available!");
    }

    read_mac_address();

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

    if (prodID & 1) { // B version
		// * 40 2A 8000 8001 : Write Rx Burst Length register. Set it to 2K
		host->control_exchange(&device->control_pipe,
							   c_write_rx_burst, 8,
							   temp_buffer, 1);
    }

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
    
    struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);
    struct t_endpoint_descriptor *bin = interface->find_endpoint(0x82);
    struct t_endpoint_descriptor *bout = interface->find_endpoint(0x02);

    host->initialize_pipe(&bulk_out_pipe, device, bout);

    if (netstack) {
    	//printf("Network stack: %s\n", netstack->identify());
        host->initialize_pipe(&ipipe, device, iin);
		ipipe.Interval = 8000; // 1 Hz
		ipipe.Length = 8; // just read 8 bytes max
		ipipe.buffer = irq_data;
		irq_transaction = host->allocate_input_pipe(&ipipe, UsbAx88772Driver_interrupt_callback, this);
		host->resume_input_pipe(irq_transaction);

		host->initialize_pipe(&bpipe, device, bin);
		bpipe.Interval = 1; // fast!
		bpipe.Length = 1536; // big blocks!
		bpipe.buffer = getBuffer();
		bulk_transaction = host->allocate_input_pipe(&bpipe, UsbAx88772Driver_bulk_callback, this);

		netstack->start();
    }
}

void UsbAx88772Driver :: disable()
{
	host->free_input_pipe(irq_transaction);
	host->free_input_pipe(bulk_transaction);
    printf("AX88772 Disabled.\n");
    if (netstack) {
    	netstack->stop();
    	//netstack = NULL;
    }
}

void UsbAx88772Driver :: deinstall(UsbInterface *intf)
{
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

}

void UsbAx88772Driver :: interrupt_handler()
{
    int data_len = host->getReceivedLength(irq_transaction);

    /*
    printf("AX88772 (ADDR=%d) IRQ data: ", device->current_address);
	for(int i=0;i<data_len;i++) {
		printf("%b ", irq_data[i]);
	} printf("\n");
*/
    cache_load(irq_data, data_len);

    if(data_len) {
        if(irq_data[2] & 0x01) {
            if(!link_up) {
                printf("Bringing link up.\n");
                host->resume_input_pipe(bulk_transaction);
                if (netstack)
                    netstack->link_up();
                link_up = true;
            }
        } else {
            if(link_up) {
                printf("Bringing link down.\n");
                host->pause_input_pipe(bulk_transaction);
                if (netstack)
                    netstack->link_down();
                link_up = false;
            }
        }
    }
	host->resume_input_pipe(this->irq_transaction);
}

void UsbAx88772Driver :: bulk_handler()
{
    uint8_t *usb_buffer = lastPacketBuffer;
    int data_len = host->getReceivedLength(this->bulk_transaction);

    //printf("Packet %p Len: %d\n", usb_buffer, data_len);
	PROFILER_SUB = 6;

	if (!link_up) {
	    free_buffer(usb_buffer);
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
        volatile t_usb_descriptor *descr = &USB2_DESCRIPTORS[bulk_transaction];
        dump_hex(usb_buffer, 64);
        dump_hex((void *)descr, 24);
		free_buffer(usb_buffer);
        //LINK_STATS_INC(link.drop);
		PROFILER_SUB = 0;
		return;
	}

	if (int(pkt_size) > (data_len - 4)) {
		printf("ERROR: Not enough data? %d %d\n", pkt_size, data_len);
	    volatile t_usb_descriptor *descr = &USB2_DESCRIPTORS[bulk_transaction];
		dump_hex(usb_buffer, 64);
		dump_hex((void *)descr, 24);

		free_buffer(usb_buffer);
        //LINK_STATS_INC(link.drop);
		PROFILER_SUB = 0;
		return;
	}

	//LINK_STATS_INC(link.recv);

	if(netstack) {
		if (!netstack->input(usb_buffer, usb_buffer+4, pkt_size)) {
		    free_buffer(usb_buffer);
		} /*else { // this else is to free immediately. this is temporary in order to test. free from lwip is not performed
			host->free_input_buffer(bulk_transaction, usb_buffer);
		}*/
	} else {
	    free_buffer(usb_buffer);
	}
	bpipe.buffer = getBuffer();
	if (bpipe.buffer) {
	    host->resume_input_pipe(bulk_transaction);
	} else {
	    printf("ERROR: No free buffers.");
	}
}
 	
void UsbAx88772Driver :: free_buffer(uint8_t *buffer)
{
//	printf("FREE PBUF CALLED %p!\n", buffer);
    freeBuffers.push(buffer);
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
        for(int i=0;i<3;i++) {
            printf("%b%c", local_mac[i], (i==5)?' ':':');
            check |= local_mac[i];
        }
        for(int i=3;i<6;i++) {
            printf("%b%c", local_mac[i], (i==5)?' ':':');
        } printf("\n");
        if (!check) { // no mac address!!
        	// printf("Reading MAC address failed.. Going to program EEPROM with good data.\n");
        	local_mac[0] = 0x02; // locally administered address
        	local_mac[1] = 0x15; // a well known number
        	local_mac[2] = 0x41;
        	// leave the others in tact
        	//        	write_srom();
//        	printf("Writing SROM completed. Now re-insert.\n");
//        	return false;
        	printf("Reading MAC address failed.. Setting a default MAC.");
        	return write_mac_address(local_mac);
        }
        if (netstack)
        	netstack->set_mac_address(local_mac);
        return true;
    }
    return false;
}

bool UsbAx88772Driver :: write_mac_address(uint8_t *local_mac)
{
    uint8_t c_write_mac[] = { 0x40, 0x14, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00 };
	uint8_t c_srom_write_reg[8]   = { 0x40, 0x0C, 0xAA, 0x00, 0xCC, 0xDD, 0x00, 0x00 };

    host->control_write(&device->control_pipe,
                        c_write_mac, 8,
                        local_mac, 6);

	device->host->control_exchange(&device->control_pipe,
									c_srom_write_enable, 8,
                                    NULL, 0);

	device->host->control_exchange(&device->control_pipe,
									c_srom_write_enable, 8,
                                    NULL, 0);

	vTaskDelay(10);

	for(int i=0;i<3;i++) {
		c_srom_write_reg[2] = (uint8_t)(i + 4);
		c_srom_write_reg[4] = local_mac[i*2];
		c_srom_write_reg[5] = local_mac[i*2 + 1];
		device->host->control_exchange(&device->control_pipe,
										c_srom_write_reg, 8,
	                                    NULL, 0);
		vTaskDelay(10);
	}

	device->host->control_exchange(&device->control_pipe,
									c_srom_write_disable, 8,
                                    NULL, 0);


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


err_t UsbAx88772Driver :: output_packet(uint8_t *buffer, int pkt_len)
{
	//printf("OUTPUT: payload = %p. Size = %d\n", buffer, pkt_len);
	if (!link_up)
		return ERR_CONN;
	//dump_hex(buffer, 32);

	uint8_t *size = buffer - 4;
    size[0] = uint8_t(pkt_len & 0xFF);
    size[1] = uint8_t(pkt_len >> 8);
    size[2] = size[0] ^ 0xFF;
    size[3] = size[1] ^ 0xFF;

	host->bulk_out(&bulk_out_pipe, size, pkt_len + 4);

	return ERR_OK;
}

uint8_t *UsbAx88772Driver :: getBuffer()
{
    lastPacketBuffer = freeBuffers.pop();
    return lastPacketBuffer;
}
