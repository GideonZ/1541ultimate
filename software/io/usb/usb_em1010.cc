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

    if((le16_to_cpu(dev->device_descr.vendor)&0xF7FF) != 0x662b) {
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
    write_register(EMREG_EC1, 0x38); // 100M full duplex, reset MAC
    write_register(EMREG_EC2, 0x41); // SET EP3RC bit to clear interrupts on EP3 access
    write_register(EMREG_IPHYC, 0x03); // enable and reset PHY
    write_register(EMREG_IPHYC, 0x02); // enable PHY, clear reset
    write_register(EMREG_PHYA, 0x01); // default we use internal phy, which has address 1
    write_register(EMREG_EC0, 0xCE); // 11001110 (Enable tx, rx, sbo and receive status and all multicasts)

    dump_registers();

    printf("MAC address:  ");
    for(int i=0;i<6;i++) {
        read_register(EMREG_EID0+i, mac_address[i]);
        printf("%b%c", mac_address[i], (i==5)?' ':':');
    } printf("\n");

    BYTE status;
    WORD phy_reg;

    wait_ms(2000);
    
    for(int i=0;i<7;i++) {
        if(read_phy_register(i, &phy_reg))
            printf("Phy Reg %d = %4x.\n", i, le16_to_cpu(phy_reg));
        else
            printf("Couldn't read PHY register.\n");
    }        

    bulk_in  = dev->find_endpoint(0x82);
    bulk_out = dev->find_endpoint(0x02);

    /* Let's test transmit/receive */
    /* Send a DHCP discover to our router and wait for response. */
    int len = 0;
    receive_frame(rx_buffer, &len);
    printf("Received: %d bytes.\n", len);
    dump_hex_relative(rx_buffer, len);

    len = prepare_dhcp_discover();
    transmit_frame(tx_buffer, len);
    printf("DHCP discover sent!\n");
    poll();
    poll();
    poll();
    len = 0;
    receive_frame(rx_buffer, &len);
    printf("Received: %d bytes.\n", len);
    dump_hex_relative(rx_buffer, len);
}

void UsbEm1010Driver :: deinstall(UsbDevice *dev)
{
    host->free_transaction(irq_transaction);
}

void UsbEm1010Driver :: poll(void)
{
    int resp = host->interrupt_in(irq_transaction, device->pipe_numbers[2], 8, irq_data);
    if(resp) {
        printf("EM1010 (ADDR=%d) IRQ data: ", device->current_address);
        for(int i=0;i<resp;i++) {
            printf("%b ", irq_data[i]);
        } printf("\n");
    } else {
        return;
    }
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
    
bool UsbEm1010Driver :: receive_frame(BYTE *buffer, int *length)
{
    int len, current;
    len = 0;
    do {
        current = host->bulk_in(buffer, 512, bulk_in);
        if(current < 0)
            return false;
        len += current;
        buffer += current;
    } while(current == 512);
    *length = len;
    return true;
}

bool UsbEm1010Driver :: transmit_frame(BYTE *buffer, int length)
{
    BYTE prefix[2];
    prefix[0] = BYTE(length & 0xFF);
    prefix[1] = BYTE(length >> 8);
    int packet_size = (length > 510)?510:length;
    int res;
    res = host->bulk_out_with_prefix(prefix, 2, buffer, packet_size, bulk_out);
    if(res < 0)
        return false;

    if(packet_size != 510) // first packet was already smaller than 512 in total
        return true;

    buffer += packet_size;
    length -= packet_size;
    do {
        packet_size = (length > 512)?512:length;
        res = host->bulk_out(buffer, packet_size, bulk_out);
        if(res < 0)
            return false;
        length -= 512; // !! This causes the last packet to be always less than 512; even 0.
        buffer += packet_size;
    } while(length >= 0);
    return true;
}

//// TEST ROUTINES //// SHOULDN'T BE PART OF THIS CLASS ////

int UsbEm1010Driver :: eth_header(BYTE *buf, WORD type) // makes IP header for broadcast
{
    // dst
    buf[0] = 0xFF;
    buf[1] = 0xFF;
    buf[2] = 0xFF;
    buf[3] = 0xFF;
    buf[4] = 0xFF;
    buf[5] = 0xFF;

    // src
    buf[6]  = mac_address[0];
    buf[7]  = mac_address[1];
    buf[8]  = mac_address[2];
    buf[9]  = mac_address[3];
    buf[10] = mac_address[4];
    buf[11] = mac_address[5];

    // ethernet type
    buf[12] = type >> 8;
    buf[13] = (BYTE)type;
    
    return 14;
}

int UsbEm1010Driver :: ip_header(BYTE *buf, WORD len, BYTE prot, DWORD src, DWORD dst)
{
    static WORD id = 0x1234;
    DWORD chk;
    BYTE b, *p;
        
    // header info
    buf[0] = 0x45; // length = 5, version = 4;
    buf[1] = 0x00; // TOS = normal packet
    len += 20;
    buf[2] = len >> 8;  // length high
    buf[3] = (BYTE)len; // length low
    buf[4] = id >> 8;   // id high
    buf[5] = (BYTE)id;  // id low
    buf[6] = 0x40;      // flags high (DF bit set)
    buf[7] = 0x00;      // flags low
    buf[8] = 0x10;      // max 16 hops
    buf[9] = prot;      // protocol
    buf[10] = 0;        // initial checksum
    buf[11] = 0;        // initial checksum

    id++;

    // IP addresses
    p = (BYTE *)&src;
    buf[12]= p[3];
    buf[13]= p[2];
    buf[14]= p[1];
    buf[15]= p[0];
    p = (BYTE *)&dst;
    buf[16]= p[3];
    buf[17]= p[2];
    buf[18]= p[1];
    buf[19]= p[0];

    // checksum
    chk=0;
    for(b=0;b<20;b+=2) {
        chk +=  (DWORD)buf[b+1];
        chk += ((DWORD)buf[b]) << 8;
        if(chk >= 65536L)
            chk -= 65535L; // one's complementing
//        printf("%ld\n", chk);
    }
    chk ^= 0xFFFF;
    
    buf[10] = (BYTE)(chk >> 8);
    buf[11] = (BYTE)chk;

    return 20;
}

int UsbEm1010Driver :: udp_header(BYTE *buf, WORD len, WORD src, WORD dst)
{
    len += 8;
    
    buf[0] = src >> 8;
    buf[1] = (BYTE)src;
    buf[2] = dst >> 8; 
    buf[3] = (BYTE)dst;
    buf[4] = len >> 8;  // length high
    buf[5] = (BYTE)len; // length low
    buf[6] = 0;
    buf[7] = 0;
    
    return 8;
}

WORD UsbEm1010Driver :: udp_sum_calc(WORD len_udp, BYTE *header, BYTE *buff)
{
    WORD prot_udp=17;
    WORD padd=0;
    WORD word16;
    DWORD sum;	
	int i;
	
	// Find out if the length of data is even or odd number. If odd,
	// add a padding byte = 0 at the end of packet
	if (len_udp & 1) {
		padd=1;
		buff[len_udp]=0;
	}
	
	//initialize sum to zero
	sum=0;
	
	// make 16 bit words out of every two adjacent 8 bit words and 
	// calculate the sum of all 16 vit words
	for (i=0;i<len_udp+padd;i=i+2){
//		word16 = *(WORD *)&buff[i];
		word16 =((buff[i]<<8)&0xFF00)+(buff[i+1]&0xFF);
//        printf("%04x ", word16);
		sum = sum + (unsigned long)word16;
	}	
//    printf("\na. Total: %08lx.\n", sum);
//	printf("\nheader:\n");

	// add the UDP pseudo header which contains the IP source and destinationn addresses
	for (i=0;i<8;i=i+2){
		word16 =((header[i]<<8)&0xFF00)+(header[i+1]&0xFF);
//		word16 = *(WORD *)&header[i];
//        printf("%04x ", word16);
		sum = sum + (unsigned long)word16;
	}
//	printf("\n");

//    printf("b. Total: %08lx.\n", sum);

	// the protocol number and the length of the UDP packet
	sum = sum + prot_udp + len_udp;

//    printf("c. Total: %08lx.\n", sum);

	// keep only the last 16 bits of the 32 bit calculated sum and add the carries
    	while (sum>>16)
		sum = (sum & 0xFFFF)+(sum >> 16);
		
//    printf("d. Total: %08lx.\n", sum);

	// Take the one's complement of sum
	sum = ~sum;

//    printf("e. Total: %08lx.\n", sum);

    return ((WORD) sum);
}

int UsbEm1010Driver :: prepare_dhcp_discover(void)
{
    static BYTE *tx;
    BYTE t;
    WORD chk;
    WORD len, udplen;
    
    // payload
    tx = &tx_buffer[42];

    // generate DHCP Discovery
    *(tx++) = 0x01; // Boot request
    *(tx++) = 0x01; // Htype = Ethernet
    *(tx++) = 0x06; // Address length = 6
    *(tx++) = 0x00; // Hops = 0
    
    *(tx++) = 0x39; // ID
    *(tx++) = 0x03;
    *(tx++) = 0xF3;
    *(tx++) = 0x26;

    for(t=0;t<20;t++) {
        *(tx++) = 0;
    }

    for(t=0;t<6;t++) {
        *(tx++) = mac_address[t];
    }

    for(t=6;t<208;t++) {
        *(tx++) = t; // should be 0
    }

    *(tx++) = 0x63; // Magic cookie
    *(tx++) = 0x82; // 
    *(tx++) = 0x53; // 
    *(tx++) = 0x63; // 
    
    *(tx++) = 0x35; // DHCP Discover option
    *(tx++) = 0x01; // Length = 1
    *(tx++) = 0x01; // 

    *(tx++) = 0x3D; // DHCP Client ID option
    *(tx++) = 0x07; // Length = 7
    *(tx++) = 0x01; // 
    for(t=0;t<6;t++) {
        *(tx++) = mac_address[t];
    }

    *(tx++) = 0x37; // DHCP Request list
    *(tx++) = 0x06; // Length = 1

    *(tx++) = 0x01; // Subnet Mask
    *(tx++) = 0x0F; // Domain Name
    *(tx++) = 0x03; // Router
    *(tx++) = 0x06; // DNS
    *(tx++) = 0x1F; // Router discover
    *(tx++) = 0x21; // Static Route

    *(tx++) = 0xFF; // DHCP End
    *(tx++) = 0x00; // Length = 0
    *(tx++) = 0x00; // Length = 0

    udplen = (int)tx - (int)&tx_buffer[42];

    // prepare to send broadcast packet
    len =  udplen + udp_header(&tx_buffer[34], udplen, 68, 67);
//    len =  udplen + udp_header(&tx_buffer[34], udplen, 680, 607);
    len += ip_header (&tx_buffer[14], len, 17, 0L, 0xFFFFFFFF);
    len += eth_header(&tx_buffer[0], 0x0800);

    // calculate checksum
    chk = udp_sum_calc(udplen+8, &tx_buffer[26], &tx_buffer[34]); 
    tx_buffer[40] = (BYTE)(chk >> 8);
    tx_buffer[41] = (BYTE)(chk & 0xFF);
    printf("Checksum as calculated: %04x.\n", chk);

    return len;
}
