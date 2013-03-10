#include <string.h>
#include <stdlib.h>
extern "C" {
	#include "itu.h"
    #include "small_printf.h"
    #include "dump_hex.h"
}
#include "integer.h"
#include "usb_ax88772.h"
#include "event.h"

BYTE c_get_mac_address[]   = { 0xC0, 0x13, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00 };
BYTE c_write_gpio[]        = { 0x40, 0x1f, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_read_phy_addr[]     = { 0xC0, 0x19, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
BYTE c_write_softw_sel[]   = { 0x40, 0x22, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_softw_rst1[]  = { 0x40, 0x20, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_softw_rst2[]  = { 0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_softw_rst3[]  = { 0x40, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_clear_rx_ctrl[]     = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_read_rx_ctrl[]      = { 0xC0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
BYTE c_req_phy_access[]    = { 0x40, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_release_access[]    = { 0x40, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_softw_rst4[]  = { 0x40, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_softw_rst5[]  = { 0x40, 0x20, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_medium_mode[] = { 0x40, 0x1B, 0x36, 0x03, 0x00, 0x00, 0x00, 0x00 };
BYTE c_write_ipg_regs[]    = { 0x40, 0x12, 0x1D, 0x00, 0x12, 0x00, 0x00, 0x00 };
BYTE c_write_rx_control[]  = { 0x40, 0x10, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_read_rx_control[]   = { 0xC0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
BYTE c_read_medium_mode[]  = { 0xC0, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };

// FIXME: SHORTCUT:
void lwip_poll(void)
{
    static int divider = 120;
    if (divider == 0) {
        dhcp_coarse_tmr();
        divider = 120;
    } else {
        divider--;
        dhcp_fine_tmr();
    }

    tcp_tmr();
    ip_reass_tmr();
    etharp_tmr();
    // autoip_tmr();
    // igmp_tmr();
    dns_tmr();
}
// FIXME

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
UsbAx88772Driver usb_ax88772_driver_tester(usb_drivers);

UsbAx88772Driver :: UsbAx88772Driver(IndexedList<UsbDriver*> &list)
{
	list.append(this); // add tester
	
	// FIXME: THIS IS A SHORTCUT
    lwip_init();
    // FIXME
}

UsbAx88772Driver :: UsbAx88772Driver()
{
    device = NULL;
    host = NULL;
}

UsbAx88772Driver :: ~UsbAx88772Driver()
{

}

UsbAx88772Driver *UsbAx88772Driver :: create_instance(void)
{
	return new UsbAx88772Driver();
}

bool UsbAx88772Driver :: test_driver(UsbDevice *dev)
{
	printf("** Test USB Asix AX88772 Driver **\n");

    if(le16_to_cpu(dev->device_descr.vendor) != 0x0b95) {
		printf("Device is not from Asix..\n");
		return false;
	}
	if(le16_to_cpu(dev->device_descr.product) != 0x772A) {
		printf("Device product ID is not AX88772A.\n");
		return false;
	}

    printf("Asix AX88772A found!!\n");
	// TODO: More tests needed here?
	return true;
}

void UsbAx88772Driver :: install(UsbDevice *dev)
{
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    device = dev;
    
	dev->set_configuration(dev->device_config.config_value);

    irq_transaction = host->allocate_transaction(8);
    bulk_transaction = host->allocate_transaction(8);
    
    read_mac_address();

    bulk_in  = dev->find_endpoint(0x82);
    bulk_out = dev->find_endpoint(0x02);

//    write_mac_address();

    WORD dummy;

    // * 40 1f b0: Write GPIO register
    host->control_exchange(device->current_address,
                           c_write_gpio, 8,
                           NULL, 1, NULL);
    // * c0 19   : Read PHY address register  (response: e0 10)
    host->control_exchange(device->current_address,
                           c_read_phy_addr, 8,
                           &dummy, 2, NULL);
    printf("%4x\n", dummy);
    // * 40 22 01: Write Software Interface Selection Selection Register
    host->control_exchange(device->current_address,
                           c_write_softw_sel, 8,
                           NULL, 1, NULL);
    // * 40 20 48: Write software reset register
    host->control_exchange(device->current_address,
                           c_write_softw_rst1, 8,
                           NULL, 1, NULL);
    // * 40 20 00: Write software reset register
    host->control_exchange(device->current_address,
                           c_write_softw_rst2, 8,
                           NULL, 1, NULL);
    // * 40 20 20: Write software reset register
    host->control_exchange(device->current_address,
                           c_write_softw_rst3, 8,
                           NULL, 1, NULL);
    // * c0 0f   : Read Rx Control register (response: 18 03)
    // * 40 10 00 00 : write Rx Control register
    host->control_exchange(device->current_address,
                           c_clear_rx_ctrl, 8,
                           NULL, 1, NULL);
    // * c0 0f   : read Rx Control regsiter (response: 00 00)
    host->control_exchange(device->current_address,
                           c_read_rx_ctrl, 8,
                           &dummy, 2, NULL);
    printf("%4x\n", dummy);
    // % c0 07 10 00 02: Read PHY ID 10, Register address 02 (resp: 3b 00)
    dummy = read_phy_register(2);
    printf("%4x\n", dummy);
    // % c0 07 10 00 03: Read PHY 10, Reg 03 (resp: 61 18)
    dummy = read_phy_register(3);
    printf("%4x\n", dummy);
    // * 40 20 08: Write Software reset register
    host->control_exchange(device->current_address,
                           c_write_softw_rst4, 8,
                           NULL, 1, NULL);
    // * 40 20 28: Write Software reset register
    host->control_exchange(device->current_address,
                           c_write_softw_rst5, 8,
                           NULL, 1, NULL);
    // % 40 08 10 -- 00: Write PHY, Reg 00 (data phase: 00 80) - Reset PHY
    write_phy_register(0, 0x8000);
    // % 40 08 10 -- 04: Write PHY, Reg 04 (data phase: e1 01) - Capabilities
    write_phy_register(4, 0x01E1);
    // % c0 07 10 -- 00: Read PHY, Reg 00 (resp: 00 31) - Speed=100,AutoNeg,FullDup
    dummy = read_phy_register(0);
    printf("%4x\n", dummy);
    // % 40 08 10 -- 00: Write PHY, Reg 00 (data phase: 00 33) - +restart autoneg - unreset
    write_phy_register(0, 0x3300);
    // * 40 1b 36 03 : Write medium mode register
    host->control_exchange(device->current_address,
                           c_write_medium_mode, 8,
                           NULL, 1, NULL);
    // * 40 12 1d 00 12: Write IPG registers
    host->control_exchange(device->current_address,
                           c_write_ipg_regs, 8,
                           NULL, 1, NULL);
    // * 40 10 88 00 : Write Rx Control register, start operation, enable broadcast
    host->control_exchange(device->current_address,
                           c_write_rx_control, 8,
                           NULL, 1, NULL);
    // * c0 0f : Read Rx Control Register (response: 88 00)
    host->control_exchange(device->current_address,
                           c_read_rx_control, 8,
                           &dummy, 2, NULL);
    printf("%4x\n", dummy);
    // * c0 1a : Read Medium Status register (response: 36 03)
    host->control_exchange(device->current_address,
                           c_read_medium_mode, 8,
                           &dummy, 2, NULL);
    printf("%4x\n", dummy);

    // % c0 07 10 00 01: Read PHY reg 01 (resp: 09 78) 
    dummy = read_phy_register(1); printf("%4x\n", dummy);
    // % c0 07 10 00 01: Read PHY reg 01 (resp: 09 78)
    dummy = read_phy_register(1); printf("%4x\n", dummy);
    // % c0 07 10 00 00: Read PHY reg 00 (resp: 00 31)
    dummy = read_phy_register(0); printf("%4x\n", dummy);
    // % c0 07 10 00 01: Read PHY reg 01 (resp: 09 78)
    dummy = read_phy_register(1); printf("%4x\n", dummy);
    // % c0 07 10 00 04: Read PHY reg 04 (resp: e1 01)
    dummy = read_phy_register(4); printf("%4x\n", dummy);

    start_lwip();

/*
    wait_ms(2000);
    dummy = read_phy_register(1); printf("%4x\n", dummy);
    if (dummy & 0x4) {
        printf("Link up!\n");    
        netif_set_up(netif);
    }
*/            
    host->start_bulk_in(bulk_transaction, bulk_in, 512);
    
    // * 40 1b 34 01 : Write Medium Mode register - Enable flow control, disable full duplex??, Receive enable, expected: 36 01 
    // * 40 16 : Write multicast.. blah
    // * 40 16 : Write multicast.. blah
    // * 40 10 98 00: Write Rx Control Register
    // * 40 10 98 00: Write Rx Control Register
    // -- many interrupts
    // % c0 07 10 00 01: Read PHY Register 01 (resp: 0d 78)
    // BULK OUT
}

void UsbAx88772Driver :: deinstall(UsbDevice *dev)
{
    host->free_transaction(irq_transaction);
    host->free_transaction(bulk_transaction);
}

void UsbAx88772Driver :: write_phy_register(BYTE reg, WORD value) {
    BYTE c_write_phy_reg[8] = { 0x40, 0x08, 0x10, 0x00, 0xFF, 0x00, 0x02, 0x00 };
    c_write_phy_reg[4] = reg;
    WORD value_le = le16_to_cpu(value);
    host->control_exchange(device->current_address,
                           c_req_phy_access, 8,
                           NULL, 1, NULL);
    host->control_write(device->current_address,
                           c_write_phy_reg, 8,
                           &value_le, 2);
    host->control_exchange(device->current_address,
                           c_release_access, 8,
                           NULL, 1, NULL);
}

WORD UsbAx88772Driver :: read_phy_register(BYTE reg) {
    BYTE c_read_phy_reg[8] = { 0xC0, 0x07, 0x10, 0x00, 0xFF, 0x00, 0x02, 0x00 };
    c_read_phy_reg[4] = reg;
    WORD value_le;
    host->control_exchange(device->current_address,
                           c_req_phy_access, 8,
                           NULL, 1, NULL);
    host->control_exchange(device->current_address,
                           c_read_phy_reg, 8,
                           &value_le, 2, NULL);
    host->control_exchange(device->current_address,
                           c_release_access, 8,
                           NULL, 1, NULL);
    return le16_to_cpu(value_le);
}


void UsbAx88772Driver :: poll(void)
{
    static int divider = 0;
    int resp;
    if (--divider < 0) {
        divider = 100;
        resp = host->interrupt_in(irq_transaction, device->pipe_numbers[0], 8, irq_data);
        if(resp) {
            printf("AX88772 (ADDR=%d) IRQ data: ", device->current_address);
            for(int i=0;i<resp;i++) {
                printf("%b ", irq_data[i]);
            } printf("\n");
            if(irq_data[2] & 0x01) {
                if (!netif_is_up(netif)) {
                    printf("Bringing link up.\n");
                    dhcp_start(netif); // netif_set_up(netif);
                }
            } else {
                if (netif_is_up(netif)) {
                    printf("Bringing link down.\n");
                    dhcp_stop(netif); // netif_set_down(netif);
                }
            }
        }
    }
    resp = host->transaction_done(bulk_transaction);
    if (resp < 0) {
        printf("BULK IN error occurred\n");
    } else if(resp > 0) {
        process_data();
        // restart
        host->start_bulk_in(bulk_transaction, bulk_in, 512);
    }
    
    // FIXME: SHORTCUT!!
    lwip_poll();
    wait_ms(20);
    // FIXME
}

void UsbAx88772Driver :: process_data(void)
{
    BYTE *usb_buffer;
    int len = host->get_bulk_in_data(bulk_transaction, &usb_buffer);
    DWORD *pul = (DWORD *)usb_buffer;
    DWORD first = *pul;
    // printf("%8x\n", first);
    if (((first >> 16) ^ 0xFFFF) != (first & 0xFFFF)) {
        printf("Invalid packet\n");
        printf("%d:%8x:%8x:%8x\n", len, first, (first >> 16) ^ 0xFFFF, first & 0xFFFF);
        return;
    }
    int size = first & 0xFFFF; // le16_to_cpu(first >> 16);
    if (size > 2000) {
        return;
    }
    // Now we know the packet is valid; allocate pbuf (chain)
    struct pbuf *p, *q;
    p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
  
    if (p != NULL) {

#if ETH_PAD_SIZE
        pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
        int usb_remain = (size > 508)?508:size;
        usb_buffer += 4 + 0x1000000; 
        q = p;
        BYTE *qb = (BYTE *)q->payload;
        int q_remain = q->len;

        /* We iterate over the usb chunks, until we have read the entire
           packet into the pbuf chain. Storing in pbuf chain. */
        while(size > 0) {
            int now = (q_remain < usb_remain)?q_remain:usb_remain;
            printf("<- [%d:%d:%d:%p:%d]\n", size, usb_remain, q_remain, usb_buffer, now);

            // copy data
            memcpy(qb, usb_buffer, now);
            //dump_hex(qb, now);
            size -= now;
            qb += now;
            q_remain -= now;
            usb_buffer += now;
            usb_remain -= now;

            if (size > 0) {
                if (q_remain == 0) {
                    q = q->next;
                    q_remain = q->len;
                }
                if (usb_remain == 0) {
                    host->start_bulk_in(bulk_transaction, bulk_in, 512);
                    while(!host->transaction_done(bulk_transaction))
                        ;
                    usb_remain = host->get_bulk_in_data(bulk_transaction, &usb_buffer);
                    usb_buffer += 0x1000000;
                }
            }
        }
    
#if ETH_PAD_SIZE
        pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif

        LINK_STATS_INC(link.recv);

        struct eth_hdr *ethhdr = (eth_hdr *)p->payload;
        
        dump_hex_relative(p->payload, p->len);
        switch (htons(ethhdr->type)) {
        /* IP or ARP packet? */
        case ETHTYPE_IP:
        case ETHTYPE_ARP:
#if PPPOE_SUPPORT
        /* PPPoE packet? */
        case ETHTYPE_PPPOEDISC:
        case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
            /* full packet send to tcpip_thread to process */
            if (netif->input(p, netif)!=ERR_OK) {
                LWIP_DEBUGF(NETIF_DEBUG, ("dummy_if_input: IP input error\n"));
                pbuf_free(p);
            }
            break;
      
        default:
            pbuf_free(p);
            break;
        }


    } else {
        // drop packet
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
    }

}
 	
bool UsbAx88772Driver :: read_mac_address()
{
	int i = device->host->control_exchange(device->current_address,
                                           c_get_mac_address, 8,
                                           mac_address, 6, NULL);
    if(i == 6) {
        printf("MAC address:  ");
        for(int i=0;i<6;i++) {
            printf("%b%c", mac_address[i], (i==5)?' ':':');
        } printf("\n");
        return true;
    }
    return false;
}

void UsbAx88772Driver :: write_mac_address(void)
{
    BYTE c_write_mac[] = { 0x40, 0x14, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00 };
    BYTE new_mac[] = { 0x00, 0x4c, 0x49, 0x4c, 0x49, 0x00 };

    host->control_write(device->current_address,
                        c_write_mac, 8,
                        new_mac, 6);

    read_mac_address();
}

    
bool UsbAx88772Driver :: transmit_frame(BYTE *buffer, int length)
{
    BYTE prefix[4];
    prefix[0] = BYTE(length & 0xFF);
    prefix[1] = BYTE(length >> 8);
    prefix[2] = prefix[0] ^ 0xFF;
    prefix[3] = prefix[1] ^ 0xFF;

    int packet_size = (length > 508)?508:length;
    int res;
    res = host->bulk_out_with_prefix(prefix, 4, buffer, packet_size, bulk_out);
    if(res < 0)
        return false;

    if(packet_size != 508) // first packet was already smaller than 512 in total
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

err_t UsbAx88772Driver :: output_callback(struct netif *netif, struct pbuf *p) 
{
    BYTE *usb_buffer = host->get_bulk_out_buffer(bulk_out);
    int size = p->tot_len;
    
    usb_buffer[0] = BYTE(size & 0xFF);
    usb_buffer[1] = BYTE(size >> 8);
    usb_buffer[2] = usb_buffer[0] ^ 0xFF;
    usb_buffer[3] = usb_buffer[1] ^ 0xFF;
    usb_buffer += 4;
    int usb_remain = 508;
    struct pbuf *q = p;
    int q_remain = q->len;
    BYTE *qb = (BYTE *)q->payload;

    do {
        printf("-> [%d:%d:%d]\n", size, usb_remain, q_remain);
        int now = (q_remain < usb_remain)?q_remain:usb_remain;

        // copy data
        memcpy(usb_buffer, q->payload, now);
        size -= now;
        qb += now;
        q_remain -= now;
        usb_buffer += now;
        usb_remain -= now;

        if (usb_remain == 0) {
            host->bulk_out_actual(512, bulk_out);
            usb_buffer = host->get_bulk_out_buffer(bulk_out);
            usb_remain = 512;
        }
        if (q_remain == 0) {
            q = q->next;
            q_remain = q->len;
        }
    } while(size > 0);

    host->bulk_out_actual(512-usb_remain, bulk_out);
}
