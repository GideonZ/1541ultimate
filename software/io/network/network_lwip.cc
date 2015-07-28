#include "network_lwip.h"
#include "init_function.h"
#include <stdlib.h>
#include <string.h>
#include "lwip/netifapi.h"
#include "ftpd.h"
extern "C" {
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "profiler.h"

void echo_task(void *a);
}

/**
 * Initialization
 */
void initLwipCallback(void *a)
{
	printf("Lwip TCP init callback.\n");
	// ftpd_init();
	printf("FTPDaemon initialized\n");
}

void initLwip(void *a, void *b)
{
	printf("Initializing lwIP.\n");
	tcpip_init(initLwipCallback, NULL);
	printf("Starting network services.\n");
	xTaskCreate( echo_task, "Echo task", configMINIMAL_STACK_SIZE, NULL, 2, NULL );
}


InitFunction lwIP_initializer(initLwip, NULL, NULL);

/**
 * Callbacks
 */
err_t lwip_init_callback(struct netif *netif)
{
    return ERR_OK;
}

static uint8_t temporary_out_buffer[1536];

err_t lwip_output_callback(struct netif *netif, struct pbuf *pbuf)
{
    NetworkLWIP *ni = (NetworkLWIP *)netif->state;
    if (pbuf->len == pbuf->tot_len) {
    	return ni->driver_output_function(ni->driver, (uint8_t *)pbuf->payload, pbuf->len);
    }
    if (pbuf->next == NULL) {
    	return ERR_BUF;
    }
    uint8_t *temp = temporary_out_buffer;
    int total = pbuf->tot_len;
    if (total > 1536) {
    	return ERR_ARG;
    }
    while(pbuf) {
    	memcpy(temp, pbuf->payload, pbuf->len);
    	temp += pbuf->len;
    	if (pbuf->len == pbuf->tot_len)
    		break;
    	pbuf = pbuf->next;
    }
    return ni->driver_output_function(ni->driver, temporary_out_buffer, total);
}

void lwip_free_callback(void *p)
{
	struct pbuf_custom *pbuf = (struct pbuf_custom *)p;
	NetworkLWIP *ni = (NetworkLWIP *)pbuf->custom_obj;
    ni->driver_free_function(ni->driver, pbuf->buffer_start);
    ni->free_pbuf(pbuf);
}

/**
 * Factory
 */
NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free)
{
	return new NetworkLWIP (driver, out, free);
}

void releaseNetworkStack(void *netstack)
{
	delete ((NetworkLWIP *)netstack);
}

/**
 * Class / instance functions
 */
NetworkLWIP :: NetworkLWIP(void *driver,
							driver_output_function_t out,
							driver_free_function_t free) : pbuf_fifo(PBUF_FIFO_SIZE, NULL)
{
	this->driver = driver;
	this->driver_free_function = free;
	this->driver_output_function = out;
	if_up = false;
	for(int i=0;i<PBUF_FIFO_SIZE-1;i++) {
		pbuf_fifo.push(&pbuf_array[i]);
	}
	dhcp_enable = false;

	NetworkInterface :: registerNetworkInterface(this);
}

NetworkLWIP :: ~NetworkLWIP()
{
	NetworkInterface :: unregisterNetworkInterface(this);
	netifapi_dhcp_stop(&my_net_if);
	netif_set_down(&my_net_if);
	netif_remove(&my_net_if);
}

void ethernet_init_inside_thread(void *classPointer)
{
	((NetworkLWIP *)classPointer)->init_callback();
}

bool NetworkLWIP :: start()
{
	memset(&my_net_if, 0, sizeof(my_net_if));
	init_callback();
    if_up = false;
    return true;
}

void NetworkLWIP :: stop()
{
    //if_up = false;
}

void NetworkLWIP :: poll()
{
    if (netif_is_up(&my_net_if) && !if_up) {
        //printf("**** NETIF IS NOW UP ****\n");
        if_up = true;
    } else if(!netif_is_up(&my_net_if) && if_up) {
        //printf("#### NETIF IS NOW DOWN ####\n");
        if_up = false;
    }
}


void NetworkLWIP :: init_callback( )
{
	/* initialization of IP addresses */
//	  IP4_ADDR(&my_ip, 192, 168, 2, 64);
//    IP4_ADDR(&my_netmask, 255, 255, 255, 0);
//    IP4_ADDR(&my_gateway, 192, 168, 2, 1);

	/* reset */
	my_net_if.state = NULL;
	my_net_if.next = NULL;

	effectuate_settings();

	/* set name */
	my_net_if.name[0] = 'U';
	my_net_if.name[1] = '2';

	/* set MAC hardware address length */
	my_net_if.hwaddr_len = ETHARP_HWADDR_LEN;
  
    /* set MAC hardware address */
	my_net_if.hwaddr[0] = mac_address[0];
	my_net_if.hwaddr[1] = mac_address[1];
	my_net_if.hwaddr[2] = mac_address[2];
	my_net_if.hwaddr[3] = mac_address[3];
	my_net_if.hwaddr[4] = mac_address[4];
	my_net_if.hwaddr[5] = mac_address[5];
  
    /* maximum transfer unit */
	my_net_if.mtu = 1500;
    
#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
	my_net_if.hostname = (char *)cfg->get_string(CFG_NET_HOSTNAME); // "Ultimate-II";
#endif /* LWIP_NETIF_HOSTNAME */

    /*
     * Initialize the snmp variables and counters inside the struct netif.
     * The last argument should be replaced with your link speed, in units
     * of bits per second.
     */
    NETIF_INIT_SNMP(&my_net_if, snmp_ifType_ethernet_csmacd, 100000000);
  
    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    my_net_if.output = etharp_output;
    my_net_if.linkoutput = lwip_output_callback;

    /* device capabilities */
	my_net_if.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

	err_t err = netifapi_netif_add(&my_net_if, &my_ip, &my_netmask, &my_gateway,
              this, lwip_init_callback, tcpip_input);

	/* device capabilities */
	my_net_if.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

	//dump_hex(nif, sizeof(struct netif));

    if(err == ERR_OK) {
    	netifapi_netif_set_default(&my_net_if);
    } else {
    	printf("Netif_add failed.\n");
    }
}


bool NetworkLWIP :: input(uint8_t *raw_buffer, uint8_t *payload, int pkt_size)
{
	//dump_hex(payload, pkt_size);

	PROFILER_SUB = 7;
	struct pbuf_custom *pbuf = pbuf_fifo.pop();

	if (!pbuf) {
		printf("No memory");
		PROFILER_SUB = 0;
		return false;
	}
	pbuf->custom_obj = this;
	pbuf->custom_free_function = lwip_free_callback;
	pbuf->buffer_start = raw_buffer;

	struct pbuf *p = &(pbuf->pbuf);
	p->flags |= PBUF_FLAG_IS_CUSTOM;
	p->len = p->tot_len = pkt_size;
	p->next = NULL;
	p->payload = payload;
	p->ref = 1;
	p->type = PBUF_REF;

	if (my_net_if.input(p, &my_net_if)!=ERR_OK) {
		LWIP_DEBUGF(NETIF_DEBUG, ("net_if_input: IP input error\n"));
		printf("é");
		pbuf_fifo.push(pbuf);
//		pbuf_free(p);
		return false;
	}
	PROFILER_SUB = 15;
	return true;
}

void NetworkLWIP :: link_up()
{
    // Enable the network interface
	netifapi_netif_set_up(&my_net_if);
    if (dhcp_enable)
    	netifapi_dhcp_start(&my_net_if);
}

void NetworkLWIP :: link_down()
{
	// Disable the network interface
	netifapi_netif_set_down(&my_net_if);
	netifapi_dhcp_stop(&my_net_if);
}

void NetworkLWIP :: set_mac_address(uint8_t *mac)
{
	memcpy((void *)mac_address, (const void *)mac, 6);
}

void NetworkLWIP :: free_pbuf(struct pbuf_custom *pbuf)
{
	pbuf_fifo.push(pbuf);
}

void NetworkLWIP :: effectuate_settings(void)
{
	my_ip.addr = inet_addr(cfg->get_string(CFG_NET_IP));
	my_netmask.addr = inet_addr(cfg->get_string(CFG_NET_NETMASK));
	my_gateway.addr = inet_addr(cfg->get_string(CFG_NET_GATEWAY));
	dhcp_enable = cfg->get_value(CFG_NET_DHCP_EN);

/* 3 states:
 * NetIF is not added yet. State = NULL. We only need to set our own ip addresses, since they will be copied upon net_if_add
 * NetIF is added, but there is no link. State != NULL. DHCP is not started yet. We can just call netif_set_addr
 * NetIF is running (link up). State != NULL. DHCP may be started and may need to be restarted.
 */
	if (my_net_if.state) { // is it initialized?
		netifapi_netif_set_addr(&my_net_if, &my_ip, &my_netmask, &my_gateway);
		if (netif_is_link_up(&my_net_if)) {
			netifapi_dhcp_stop(&my_net_if);
			if (dhcp_enable) {
				netifapi_dhcp_start(&my_net_if);
			}
		}
	}
}

void NetworkLWIP :: getIpAddr(uint8_t *buf)
{
	memcpy(&buf[0], &my_net_if.ip_addr, 4);
	memcpy(&buf[4], &my_net_if.netmask, 4);
	memcpy(&buf[8], &my_net_if.gw, 4);
}

void NetworkLWIP :: getMacAddr(uint8_t *buf)
{
	memcpy(buf, &my_net_if.hwaddr, 6);
}

void NetworkLWIP :: setIpAddr(uint8_t *buf)
{
	memcpy(&my_ip.addr, &buf[0], 4);
	memcpy(&my_netmask.addr, &buf[4], 4);
	memcpy(&my_gateway.addr, &buf[8], 4);

	if (my_net_if.state) { // is it initialized?
		netifapi_netif_set_addr(&my_net_if, &my_ip, &my_netmask, &my_gateway);
	}
}
