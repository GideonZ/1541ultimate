#include <string.h>
#include <stdlib.h>
extern "C" {
	#include "itu.h"
    #include "small_printf.h"
    #include "dump_hex.h"
}
#include "integer.h"
#include "usb_em1010.h"

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

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/
// tester instance
FactoryRegistrator<UsbDevice *, UsbDriver *> em1010_tester(UsbDevice :: getUsbDriverFactory(), UsbEm1010Driver :: test_driver);

// Entry point for call-backs.
void UsbEm1010Driver_interrupt_callback(uint8_t *data, int data_length, void *object) {
	((UsbEm1010Driver *)object)->interrupt_handler(data, data_length);
}

// Entry point for call-backs.
void UsbEm1010Driver_bulk_callback(uint8_t *data, int data_length, void *object) {
	((UsbEm1010Driver *)object)->bulk_handler(data, data_length);
}

// entry point for free buffer callback
void UsbEm1010Driver_free_buffer(void *drv, void *b) {
	((UsbEm1010Driver *)drv)->free_buffer((uint8_t *)b);
}

// entry point for output packet callback
uint8_t UsbEm1010Driver_output(void *drv, void *b, int len) {
	return ((UsbEm1010Driver *)drv)->output_packet((uint8_t *)b, len);
}



UsbEm1010Driver :: UsbEm1010Driver()
{
    device = NULL;
    host = NULL;
    netstack = NULL;
    link_up = false;
}

UsbEm1010Driver :: ~UsbEm1010Driver()
{
	if(netstack)
		releaseNetworkStack(netstack);
}

UsbDriver *UsbEm1010Driver :: test_driver(UsbDevice *dev)
{
	//printf("** Test USB Eminent EM1010 Driver **\n");

	uint16_t vendor = le16_to_cpu(dev->device_descr.vendor);
	uint16_t product = le16_to_cpu(dev->device_descr.product);

	bool found = false;
	if (((vendor & 0xF7FF) == 0x662b) && (product == 0x4efe))
		found = true;
	if ((vendor == 0x07a6) && (product == 0x8515))
		found = true;

	if (found) {
		printf("** Eminent EM1010 (ADM8515 chip) found!!\n");
		return new UsbEm1010Driver();
	}
	return 0;
}

void UsbEm1010Driver :: install(UsbDevice *dev)
{
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    device = dev;
    
	dev->set_configuration(dev->get_device_config()->config_value);

    netstack = getNetworkStack(this, UsbEm1010Driver_output, UsbEm1010Driver_free_buffer);
    if(!netstack) {
    	puts("** Warning! No network stack available!");
    }

    read_mac_address();

    struct t_endpoint_descriptor *bin  = dev->find_endpoint(0x82);
    struct t_endpoint_descriptor *bout = dev->find_endpoint(0x02);
    struct t_endpoint_descriptor *iin  = dev->find_endpoint(0x83);

    irq_in   = (iin->endpoint_address & 0x0F);
    bulk_in  = (bin->endpoint_address & 0x0F);
    bulk_out = (bout->endpoint_address & 0x0F);

	bulk_out_pipe.DevEP = (device->current_address << 8) | bulk_out;
	bulk_out_pipe.MaxTrans = bout->max_packet_size;
	bulk_out_pipe.SplitCtl = 0;
	bulk_out_pipe.Command = 0;

    write_register(EMREG_EC1, 0x38); // 100M full duplex, reset MAC
    write_register(EMREG_EC2, 0x41); // SET EP3RC bit to clear interrupts on EP3 access
    write_register(EMREG_IPHYC, 0x03); // enable and reset PHY
    write_register(EMREG_IPHYC, 0x02); // enable PHY, clear reset
    write_register(EMREG_PHYA, 0x01); // default we use internal phy, which has address 1
    //write_register(EMREG_EC0, 0xCE); // 11001110 (Enable tx, rx, sbo and receive status and all multicasts)
    write_register(EMREG_EC0, 0xCC); // 11001110 (Enable tx, rx, sbo, status )

    dump_registers();

    if (netstack) {
    	//printf("Network stack: %s\n", netstack->identify());
    	struct t_pipe ipipe;
		ipipe.DevEP = uint16_t((device->current_address << 8) | irq_in);
		ipipe.Interval = 8000; // 1 Hz
		ipipe.Length = 16; // just read 16 bytes max
		ipipe.MaxTrans = iin->max_packet_size;
		ipipe.SplitCtl = 0;
		ipipe.Command = 0; // driver will fill in the command

		irq_transaction = host->allocate_input_pipe(&ipipe, UsbEm1010Driver_interrupt_callback, this);

		ipipe.DevEP = uint16_t((device->current_address << 8) | bulk_in);
		ipipe.Interval = 1; // fast!
		ipipe.Length = 1536; // big blocks!
		ipipe.MaxTrans = bin->max_packet_size;
		ipipe.SplitCtl = 0;
		ipipe.Command = 0; // driver will fill in the command

		bulk_transaction = host->allocate_input_pipe(&ipipe, UsbEm1010Driver_bulk_callback, this);

		netstack->start();
    }

}

void UsbEm1010Driver :: deinstall(UsbDevice *dev)
{
    host->free_input_pipe(irq_transaction);
}

bool UsbEm1010Driver :: read_mac_address()
{
	uint8_t local_mac[8];

	printf("MAC address:  ");
	for(int i=0;i<6;i++) {
		read_register(EMREG_EID0+i, local_mac[i]);
		printf("%b%c", local_mac[i], (i==5)?' ':':');
	} printf("\n");
	if (netstack) {
		netstack->set_mac_address(local_mac);
		return true;
	}
    return false;
}

void UsbEm1010Driver :: interrupt_handler(uint8_t *irq_data, int data_len)
{
/*
	printf("EM1010 (ADDR=%d) IRQ data: ", device->current_address);
	for(int i=0;i<data_len;i++) {
		printf("%b ", irq_data[i]);
	} printf("\n");
*/


	if(irq_data[5] & 0x01) {
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
	
//                         bmreq breq  __wValue__  __wIndex__  __wLength_
uint8_t c_get_register[]  = { 0xC0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
uint8_t c_set_register[]  = { 0x40, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
uint8_t c_get_registers[] = { 0xC0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };

bool UsbEm1010Driver :: read_register(uint8_t offset, uint8_t &data)
{
    uint8_t read_value[4];
    
    c_get_register[4] = offset;
	int i = device->host->control_exchange(&device->control_pipe,
                                           c_get_register, 8,
                                           read_value, 1);
    data = read_value[0];
    return (i==1);
}

bool UsbEm1010Driver :: dump_registers(void)
{
    uint8_t read_values[128];
    
    for(int i=0;i<128;i+=16) {
        read_registers(i, &read_values[i], 16);
    }
    dump_hex_relative(read_values, 128);
    return true;
}

bool UsbEm1010Driver :: read_registers(uint8_t offset, uint8_t *data, int len)
{
    c_get_registers[4] = offset;
    c_get_registers[6] = uint8_t(len);
    
	int i = device->host->control_exchange(&device->control_pipe,
                                           c_get_registers, 8,
                                           data, len);
//    printf("Read registers. Received %d bytes.\n", i);
    return (i==len);
}

bool UsbEm1010Driver :: write_register(uint8_t offset, uint8_t data)
{
    c_set_register[4] = offset;
    c_set_register[2] = data;

	int i = device->host->control_exchange(&device->control_pipe,
                                           c_set_register, 8,
                                           NULL, 8);
//    printf("Write register. %d byte(s) received back.\n", i);
    return (i==0);
}

bool UsbEm1010Driver :: read_phy_register(uint8_t offset, uint16_t *data)
{
    uint8_t status;
    if(!write_register(EMREG_PHYAC, offset | 0x40)) // RDPHY
        return false;
    if(!read_register(EMREG_PHYAC, status))
        return false;
    if(status & 0x80) { // done
        return read_registers(EMREG_PHYDL, (uint8_t*)data, 2);
    }
    printf("Not done? %b\n", status);
    return false;
}
    
bool UsbEm1010Driver :: write_phy_register(uint8_t offset, uint16_t data)
{
	return false;
}
    
void UsbEm1010Driver :: bulk_handler(uint8_t *usb_buffer, int pkt_size)
{
//	printf("EM1010: Receive packet: %d\n", pkt_size);

	if (!link_up) {
		host->free_input_buffer(bulk_transaction, usb_buffer);
		return;
	}

	if(netstack) {
		if (!netstack->input(usb_buffer, usb_buffer, pkt_size-4)) {
			host->free_input_buffer(bulk_transaction, usb_buffer);
		}
	} else {
		host->free_input_buffer(bulk_transaction, usb_buffer);
	}
}

uint8_t UsbEm1010Driver :: output_packet(uint8_t *buffer, int pkt_len)
{
	if (!link_up)
		return 0;

	uint8_t *size = buffer - 2;
    size[0] = uint8_t(pkt_len & 0xFF);
    size[1] = uint8_t(pkt_len >> 8);

	host->bulk_out(&bulk_out_pipe, size, pkt_len + 2);
	return 0;
}

void UsbEm1010Driver :: poll(void)
{
	if(netstack)
		netstack->poll();
}

void UsbEm1010Driver :: pipe_error(int pipe)
{

}

void UsbEm1010Driver :: free_buffer(uint8_t *buffer)
{
//	printf("FREE PBUF CALLED %p!\n", buffer);
	host->free_input_buffer(bulk_transaction, buffer);
}
