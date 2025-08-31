#include "network_config.h"
#include "network_interface.h"
#include "init_function.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/apps/mdns.h"
#include "filemanager.h"

extern "C" {
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "profiler.h"
}

#include "sntp_time.h"

void start_sntp() __attribute__((weak));
void start_sntp() { }

//-----------------------------------
struct t_cfg_definition net_config[] = {
    { CFG_NET_DHCP_EN, CFG_TYPE_ENUM,   "Use DHCP",         "%s", en_dis,     0,  1, 1 },
    { CFG_NET_IP,      CFG_TYPE_STRING, "Static IP",        "%s", NULL,       7, 16, (int)"192.168.2.64" },
    { CFG_NET_NETMASK, CFG_TYPE_STRING, "Static Netmask",   "%s", NULL,       7, 16, (int)"255.255.255.0" },
    { CFG_NET_GATEWAY, CFG_TYPE_STRING, "Static Gateway",   "%s", NULL,       7, 16, (int)"192.168.2.1" },
    { CFG_NET_DNS,     CFG_TYPE_STRING, "Static DNS",       "%s", NULL,       7, 16, (int)"8.8.8.8" },
    { CFG_TYPE_END,    CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};


/**
 * Initialization
 */
void initLwip(void *a, void *b)
{
//	printf("Initializing lwIP.\n");
	tcpip_init(NULL, NULL);
}

InitFunction lwIP_initializer("LwIP Networking", initLwip, NULL, NULL, 50);
IndexedList<NetworkInterface *> NetworkInterface :: netInterfaces(4, NULL);
/**
 * Callbacks
 */
err_t lwip_init_callback(struct netif *netif)
{
    NetworkInterface *ni = (NetworkInterface *)netif->state;
    ni->init_callback();
    return ERR_OK;
}


// This function has to be called by "tcpip_callback" to be safe.
void set_dns_server_unsafe(void* arg)
{
    ip_addr_t *dns = (ip_addr_t *)arg;
    if(dns->addr != 0) {
        dns_setserver(0, dns); // Sets the primary DNS
    } else {
        dns_setserver(0, 0); // Sets the primary DNS
    }
}
 
err_t NetworkInterface :: lwip_output_callback(struct netif *netif, struct pbuf *pbuf)
{
	static uint8_t temporary_out_buffer[1536];
    NetworkInterface *ni = (NetworkInterface *)netif->state;

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
        // Disabling the below debug message by default. It causes an infinite
        // stream of messages when remote syslogging is enabled. This printf()
        // will cause a syslog packet to be sent, and since THAT packet will
        // trigger this callback, another syslog message will be sent, again
        // causing.... ad infinitum.

    	// printf("Concat %p->%p (%d)\n", pbuf->payload, temp, pbuf->len);
    	memcpy(temp, pbuf->payload, pbuf->len);
    	temp += pbuf->len;
    	if (pbuf->len == pbuf->tot_len)
    		break;
    	pbuf = pbuf->next;
    }
    return ni->driver_output_function(ni->driver, temporary_out_buffer, total);
}

void NetworkInterface :: lwip_free_callback(struct pbuf *p)
{
	NetworkInterface *ni = (NetworkInterface *)p->custom_obj;
    ni->driver_free_function(ni->driver, p->buffer_start);
    ni->free_pbuf(p);
}

/**
 * Factory
 */
NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free)
{
	NetworkInterface *ni = new NetworkInterface (driver, out, free);
	ni->attach_config();
	return ni;
}

void releaseNetworkStack(void *netstack)
{
	delete ((NetworkInterface *)netstack);
}

/**
 * Class / instance functions
 */
NetworkInterface :: NetworkInterface(void *driver,
							driver_output_function_t out,
							driver_free_function_t free) : pbuf_fifo(PBUF_FIFO_SIZE, NULL)
{
	cfg = NULL;
    this->driver = driver;
    this->driver_free_function = free;
	this->driver_output_function = out;
	if_up = false;
	for(int i=0;i<PBUF_FIFO_SIZE-1;i++) {
		pbuf_fifo.push(&pbuf_array[i]);
	}
	dhcp_enable = false;
	memset(&my_net_if, 0, sizeof(my_net_if)); // clear the whole thing
    NetworkInterface :: registerNetworkInterface(this);

	// quick hack to perform update on the browser
	FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");
}

NetworkInterface :: ~NetworkInterface()
{
	NetworkInterface :: unregisterNetworkInterface(this);
	dhcp_stop(&my_net_if);
	netif_set_down(&my_net_if);
	netif_remove(&my_net_if);

	// quick hack to perform update on the browser
	FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");
}

void NetworkInterface :: attach_config()
{
	register_store(0x4E657477, "Ethernet Settings", net_config);
}

bool NetworkInterface :: start()
{
	memset(&my_net_if, 0, sizeof(my_net_if)); // clear the whole thing
    effectuate_settings(); // this will set the IP address, netmask and gateway
    my_net_if.state = this;
    netif_add(&my_net_if, &my_ip, &my_netmask, &my_gateway, this, lwip_init_callback, tcpip_input);

    //netif_set_default(&my_net_if);
    if_up = false;
    return true;
}

void NetworkInterface :: stop()
{
	link_down();
	netif_remove(&my_net_if);
}

void NetworkInterface :: statusCallback(struct netif *net)
{
	NetworkInterface *ni = (NetworkInterface *)net->state;
	ni->statusUpdate();
}

#include "lwip/ip_addr.h"
void NetworkInterface :: set_default_interface(void)
{
    for(int i=0;i<getNumberOfInterfaces();i++) {
        NetworkInterface *intf = getInterface(i);
        printf("Checking interface %d: Up: %d. IP = %08x\n", i,
            netif_is_up(&intf->my_net_if), intf->my_net_if.ip_addr.addr);

        if (netif_is_up(&intf->my_net_if) && (intf->my_net_if.ip_addr.addr != 0)) {
            netif_set_default(&intf->my_net_if);
            return;
        }        
    }
    netif_set_default(&(getInterface(0)->my_net_if));
}

void NetworkInterface :: statusUpdate(void)
{
    char str[16];
    printf("Status update IP = %s\n", ipaddr_ntoa_r(&(my_net_if.ip_addr), str, sizeof(str)));
    set_default_interface();

    // quick hack to perform update on the browser
	FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");

    start_sntp();
}


void NetworkInterface :: init_callback( )
{
	/* initialization of IP addresses */
	/* set name */
	my_net_if.name[0] = 'U';
	my_net_if.name[1] = '2';

	// effectuate_settings();

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
	my_net_if.hostname = this->hostname;
#endif /* LWIP_NETIF_HOSTNAME */

    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    my_net_if.output = etharp_output;
    my_net_if.linkoutput = NetworkInterface :: lwip_output_callback;

    /* device capabilities */
	my_net_if.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    mdns_resp_remove_netif(&my_net_if); // In case we are reinitializing the interface, remove the old one
    mdns_resp_add_netif(&my_net_if, this->hostname);

    netif_set_status_callback(&my_net_if, NetworkInterface :: statusCallback);
}

void NetworkInterface :: getDisplayString(int index, char *buffer, int width)
{
    char ip[16];
    if (is_link_up()) {
        sprintf(buffer, "Net%d    IP: %#s\eMLink Up", index, width - 21, getIpAddrString(ip, 16));
    } else {
        sprintf(buffer, "Net%d    MAC %b:%b:%b:%b:%b:%b%#s\eJLink Down", index, mac_address[0], mac_address[1],
                mac_address[2], mac_address[3], mac_address[4], mac_address[5], width - 38, "");
    }
}

bool NetworkInterface :: input(void *raw_buffer, uint8_t *payload, int pkt_size)
{
	PROFILER_SUB = 7;
	struct pbuf_custom *pc = pbuf_fifo.pop();
    struct pbuf *pbuf = (struct pbuf *)pc;

	if (!pbuf) {
		printf("No memory");
		PROFILER_SUB = 0;
		return false;
	}
	pbuf->custom_obj = this;
	pc->custom_free_function = NetworkInterface :: lwip_free_callback;
	pbuf->buffer_start = raw_buffer;

	pbuf->flags |= PBUF_FLAG_IS_CUSTOM;
	pbuf->len = pbuf->tot_len = pkt_size;
	pbuf->next = NULL;
	pbuf->payload = payload;
	pbuf->ref = 1;

	if (my_net_if.input(pbuf, &my_net_if)!=ERR_OK) {
		LWIP_DEBUGF(NETIF_DEBUG, ("net_if_input: IP input error\n"));
		printf("#");
		pbuf_fifo.push(pc);
		return false;
	}
	PROFILER_SUB = 15;
	return true;
}

void NetworkInterface :: link_up()
{
    // Enable the network interface
	if_up = true;
	netif_set_up(&my_net_if);
    if (dhcp_enable) {
    	dhcp_start(&my_net_if);
    } else {
        tcpip_callback((tcpip_callback_fn)set_dns_server_unsafe, &my_dns);
    }
    set_default_interface();
}

void NetworkInterface :: link_down()
{
	if_up = false;
	// Disable the network interface
	netif_set_down(&my_net_if);
	dhcp_stop(&my_net_if);
    set_default_interface();
}

void NetworkInterface :: set_mac_address(uint8_t *mac)
{
	memcpy((void *)mac_address, (const void *)mac, 6);
}

void NetworkInterface :: free_pbuf(struct pbuf *pbuf)
{
	pbuf_fifo.push((struct pbuf_custom *)pbuf);
}

void NetworkInterface :: effectuate_settings(void)
{
    dhcp_enable = cfg->get_value(CFG_NET_DHCP_EN);
    if (!dhcp_enable) {
        my_ip.addr = inet_addr(cfg->get_string(CFG_NET_IP));
        my_netmask.addr = inet_addr(cfg->get_string(CFG_NET_NETMASK));
        my_gateway.addr = inet_addr(cfg->get_string(CFG_NET_GATEWAY));
        const char *dnsserver = cfg->get_string(CFG_NET_DNS);
        if (!*dnsserver) {
            // if no DNS server is set, use the gateway as DNS server
           dnsserver = cfg->get_string(CFG_NET_GATEWAY);
        }
        my_dns.addr = inet_addr(dnsserver);
    } else { // in case of DHCP, we start using 0.0.0.0
        my_ip.addr = 0L;
        my_netmask.addr = 0L;
        my_gateway.addr = 0L;
        my_dns.addr = 0L;
    }

    const char *h = networkConfig.cfg->get_string(CFG_NETWORK_HOSTNAME);
    const int len = strlen(h);
    int out = 0;
    for(int i=0; i < len; i++) {
        if (isalpha(h[i]) || isdigit(h[i]) || h[i] == '-') {
            hostname[out++] = h[i];
        }
    }
    hostname[out] = 0;
    // correct in configuration if invalid chars exist
    if (out != len) {
        networkConfig.cfg->set_string(CFG_NETWORK_HOSTNAME, hostname);
    }


/* 3 states:
 * NetIF is not added yet. State = NULL. We only need to set our own ip addresses, since they will be copied upon net_if_add
 * NetIF is added, but there is no link. State != NULL. DHCP is not started yet. We can just call netif_set_addr
 * NetIF is running (link up). State != NULL. DHCP may be started and may need to be restarted.
 */
    if (my_net_if.state) { // is it initialized?
        if (netif_is_link_up(&my_net_if)) {
            dhcp_stop(&my_net_if);
            if (dhcp_enable) {
                dhcp_start(&my_net_if);
            } else {
                netif_set_addr(&my_net_if, &my_ip, &my_netmask, &my_gateway);
                tcpip_callback((tcpip_callback_fn)set_dns_server_unsafe, &my_dns);
            }
        }
    }
}

char *NetworkInterface :: getIpAddrString(char *buf, int buflen)
{
	return ipaddr_ntoa_r(&(my_net_if.ip_addr), buf, buflen);
}

void NetworkInterface :: getIpAddr(uint8_t *buf)
{
	memcpy(&buf[0], &my_net_if.ip_addr, 4);
	memcpy(&buf[4], &my_net_if.netmask, 4);
	memcpy(&buf[8], &my_net_if.gw, 4);
}

void NetworkInterface :: getMacAddr(uint8_t *buf)
{
	memcpy(buf, &my_net_if.hwaddr, 6);
}

void NetworkInterface :: setIpAddr(uint8_t *buf)
{
	memcpy(&my_ip.addr, &buf[0], 4);
	memcpy(&my_netmask.addr, &buf[4], 4);
	memcpy(&my_gateway.addr, &buf[8], 4);

	if (my_net_if.state) { // is it initialized?
		netif_set_addr(&my_net_if, &my_ip, &my_netmask, &my_gateway);
	}
}

bool NetworkInterface :: peekArpTable(uint32_t ipToQuery, uint8_t *mac)
{
    struct eth_addr *his_mac = 0;
    ip_addr_t *his_ip = 0;
    ip_addr_t query_ip;

    query_ip.addr = ipToQuery;

    portENTER_CRITICAL();
    etharp_find_addr(&my_net_if, (const ip4_addr_t *)&query_ip, &his_mac, (const ip4_addr_t **)&his_ip);

    bool ret = false;
    if (his_mac) {
        memcpy(mac, his_mac->addr, 6);
        ret = true;
    }

    portEXIT_CRITICAL();

    return ret;
}
