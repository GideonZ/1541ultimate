#include "network_interface.h"


err_t lwip_init_callback(struct netif *netif)
{
    NetworkInterface *ni = (NetworkInterface *)netif->state;
    ni->init_callback(netif);
    return ERR_OK;
}

err_t lwip_output_callback(struct netif *netif, struct pbuf *pbuf)
{
    NetworkInterface *ni = (NetworkInterface *)netif->state;
    return ni->output_callback(netif, pbuf);
}

bool NetworkInterface :: start_lwip()
{
    //IP4_ADDR(&my_ip, 192,168,0,64);
    //IP4_ADDR(&my_netmask, 255,255,254,0);
    //IP4_ADDR(&my_gateway, 192,168,0,1);

    IP4_ADDR(&my_ip, 0,0,0,0);
    IP4_ADDR(&my_netmask, 0,0,0,0);
    IP4_ADDR(&my_gateway, 0,0,0,0);

    netif = netif_add(&my_net_if, &my_ip, &my_netmask, &my_gateway,
                        this, lwip_init_callback, ethernet_input);

    netif_set_default(netif);
}
       
void NetworkInterface :: init_callback(struct netif *netif)
{
    /* set MAC hardware address length */
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
  
    /* set MAC hardware address */
    netif->hwaddr[0] = mac_address[0];
    netif->hwaddr[1] = mac_address[1];
    netif->hwaddr[2] = mac_address[2];
    netif->hwaddr[3] = mac_address[3];
    netif->hwaddr[4] = mac_address[4];
    netif->hwaddr[5] = mac_address[5];
  
    /* maximum transfer unit */
    netif->mtu = 1500;
    
    /* device capabilities */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "Ultimate-II";
#endif /* LWIP_NETIF_HOSTNAME */

    /*
     * Initialize the snmp variables and counters inside the struct netif.
     * The last argument should be replaced with your link speed, in units
     * of bits per second.
     */
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, 100000000);
  
    netif->name[0] = 'U';
    netif->name[1] = '2';
    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    netif->output = etharp_output;
    netif->linkoutput = lwip_output_callback;
}


__inline static bool check_time(DWORD timer, DWORD &variable, DWORD interval, char dbg)
{
    if (timer < variable) {
        if (((timer + 65536) - variable) > interval) {
            variable = timer;
            //printf("%c", dbg);
            return true;
        }
    } else if ((timer - variable) > interval) {
            variable = timer;
            //printf("%c", dbg);
            return true;
    }
    return false;
}

void NetworkInterface :: lwip_poll()
{
    // these variables say when was the last event
    static DWORD dhcp_coarse = 0;
    static DWORD dhcp_fine = 0;
    static DWORD tcp_timer = 0;
    static DWORD dns_timer = 0;
    static DWORD arp_timer = 0;
    static DWORD ip_timer = 0;
    
    DWORD timer = (DWORD)ITU_MS_TIMER;

    if (check_time(timer, dhcp_coarse, DHCP_COARSE_TIMER_MSECS, 'C'))
        dhcp_coarse_tmr();
    if (check_time(timer, dhcp_fine, DHCP_FINE_TIMER_MSECS, 'F'))
        dhcp_fine_tmr();
    if (check_time(timer, tcp_timer, TCP_TMR_INTERVAL, 't'))
        tcp_tmr();
    if (check_time(timer, dns_timer, DNS_TMR_INTERVAL, 'd'))
        dns_tmr();
    if (check_time(timer, arp_timer, ARP_TMR_INTERVAL, 'a'))
        etharp_tmr();
    if (check_time(timer, ip_timer, IP_TMR_INTERVAL, 'i'))                
        ip_reass_tmr();

    // autoip_tmr();
    // igmp_tmr();
}
