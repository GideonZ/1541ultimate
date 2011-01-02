#include <string.h>
#include <stdlib.h>
extern "C" {
	#include "itu.h"
    #include "small_printf.h"
    #include "dump_hex.h"
}
#include "integer.h"
#include "usb_em1010.h"
#include "event.h"

__inline DWORD cpu_to_32le(DWORD a)
{
    DWORD m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

__inline WORD le16_to_cpu(WORD h)
{
    return (h >> 8) | (h << 8);
}

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/
// tester instance
UsbEm1010Driver usb_em1010_driver_tester(usb_drivers);

UsbEm1010Driver :: UsbEm1010Driver(IndexedList<UsbDriver*> &list)
{
	list.append(this); // add tester
}

UsbEm1010Driver :: UsbEm1010Driver()
{
    device = NULL;
    host = NULL;
}

UsbEm1010Driver :: ~UsbEm1010Driver()
{

}

UsbEm1010Driver *UsbEm1010Driver :: create_instance(void)
{
	return new UsbEm1010Driver();
}

bool UsbEm1010Driver :: test_driver(UsbDevice *dev)
{
	printf("** Test USB Eminent EM1010 Driver **\n");

    if(le16_to_cpu(dev->device_descr.vendor) != 0x6e2b) {
		printf("Device is not from Eminent..\n");
		return false;
	}
	if(le16_to_cpu(dev->device_descr.product) != 0x4efe) {
		printf("Device product ID is not EM1010.\n");
		return false;
	}

    printf("Eminent EM1010 found!!\n");
	// TODO: More tests needed here?
	return true;
}


void UsbEm1010Driver :: install(UsbDevice *dev)
{
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    device = dev;
    
	dev->set_configuration(dev->device_config.config_value);

    irq_transaction = host->allocate_transaction(8);
//    host->interrupt_in(irq_transaction, device->pipe_numbers[0], 1, irq_data);
    write_register(EMREG_EC2, 0x41); // SET EP3RC bit to clear interrupts on EP3 access
    write_register(EMREG_IPHYC, 0x03); // enable and reset PHY
    write_register(EMREG_IPHYC, 0x02); // enable PHY, clear reset
    write_register(EMREG_PHYA, 0x01); // default we use internal phy, which has address 1

    dump_registers();

    BYTE status;
    WORD phy_reg;

    wait_ms(2000);
    
    for(int i=0;i<7;i++) {
        if(read_phy_register(i, &phy_reg))
            printf("Phy Reg %d = %4x.\n", i, le16_to_cpu(phy_reg));
        else
            printf("Couldn't read PHY register.\n");
    }        
}

void UsbEm1010Driver :: deinstall(UsbDevice *dev)
{
    host->free_transaction(irq_transaction);
}

void UsbEm1010Driver :: poll(void)
{
/*
    int resp = host->interrupt_in(irq_transaction, device->pipe_numbers[2], 8, irq_data);
    if(resp) {
        printf("EM1010 (ADDR=%d) IRQ data: ", device->current_address);
        for(int i=0;i<resp;i++) {
            printf("%b ", irq_data[i]);
        } printf("\n");
    } else {
        return;
    }
*/
}
	
//                         bmreq breq  __wValue__  __wIndex__  __wLength_
BYTE c_get_register[]  = { 0xC0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
BYTE c_set_register[]  = { 0x40, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
BYTE c_get_registers[] = { 0xC0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };

bool UsbEm1010Driver :: read_register(BYTE offset, BYTE &data)
{
    BYTE read_value;
    
    c_get_register[4] = offset;
	int i = device->host->control_exchange(device->current_address,
                                           c_get_register, 8,
                                           &read_value, 1, NULL);
    data = read_value;
    return (i==1);
}

bool UsbEm1010Driver :: dump_registers(void)
{
    BYTE read_values[128];
    
    for(int i=0;i<128;i+=16) {
        read_registers(i, &read_values[i], 16);
    }
    dump_hex_relative(read_values, 128);
    return true;
}

bool UsbEm1010Driver :: read_registers(BYTE offset, BYTE *data, int len)
{
    c_get_registers[4] = offset;
    c_get_registers[6] = BYTE(len);
    
	int i = device->host->control_exchange(device->current_address,
                                           c_get_registers, 8,
                                           data, len, NULL);
//    printf("Read registers. Received %d bytes.\n", i);
    return (i==len);
}

bool UsbEm1010Driver :: write_register(BYTE offset, BYTE data)
{
    c_set_register[4] = offset;
    c_set_register[2] = data;

	int i = device->host->control_exchange(device->current_address,
                                           c_set_register, 8,
                                           NULL, 8, NULL);
//    printf("Write register. %d byte(s) received back.\n", i);
    return (i==0);
}

bool UsbEm1010Driver :: read_phy_register(BYTE offset, WORD *data)
{
    BYTE status;
    if(!write_register(EMREG_PHYAC, offset | 0x40)) // RDPHY
        return false;
    if(!read_register(EMREG_PHYAC, status))
        return false;
    if(status & 0x80) { // done
        return read_registers(EMREG_PHYDL, (BYTE*)data, 2);
    }
    printf("Not done? %b\n", status);
    return false;
}
    
bool UsbEm1010Driver :: write_phy_register(BYTE offset, WORD data)
{
}
    
