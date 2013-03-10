#include "lwip/init.h"
#include "lwip/ip_frag.h"
#include "netif/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"

// ultimate specific
#include "small_printf.h"
#include "itu.h"

// driver
#include "dummy_if.h"

void lwip_poll(void);

struct ip_addr my_ip;
struct ip_addr my_netmask;
struct ip_addr my_gateway;

struct netif my_net_if;

int main(int argc, char **argv)
{
    IP4_ADDR(&my_ip, 192,168,0,64);
    IP4_ADDR(&my_netmask, 255,255,254,0);
    IP4_ADDR(&my_gateway, 192,168,0,1);
        
    lwip_init();

    struct netif *interface = netif_add(&my_net_if, &my_ip, &my_netmask, &my_gateway,
                                        NULL, dummy_if_init, ethernet_input);
    printf("NetIf: %p\n", interface);
    if (!interface)
        return -1;
        
    netif_set_default(interface);
    netif_set_up(interface);
    int i;
    struct pbuf *p, *ps[10];
    for(i=0;i<10;i++) {
        ps[i] = p = pbuf_alloc(PBUF_RAW, 800+100*i, PBUF_POOL);
        while(p) {
            printf("%p [%d @ %p] -> ", p, p->len, p->payload);
            p = p->next;
        }
        printf("\n");
    }
    for(i=0;i<10;i++) {
        if(ps[i])
            pbuf_free(ps[i]);
    }
    for(i=0;i<10;i++) {
        ps[i] = p = pbuf_alloc(PBUF_RAW, 800+100*i, PBUF_RAM);
        while(p) {
            printf("%p [%d @ %p] -> ", p, p->len, p->payload);
            p = p->next;
        }
        printf("\n");
    }
    for(i=0;i<10;i++) {
        if(ps[i])
            pbuf_free(ps[i]);
    }
    
    printf("DHCP start\n");
    dhcp_start(interface);
    printf("Polling start\n");
    for (i=0;i<100;i++) {
        lwip_poll();
        wait_ms(10);
    }
    return 0;
}

void lwip_poll(void)
{
    printf("#");
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
