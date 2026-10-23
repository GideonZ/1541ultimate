// Minimal lwIP configuration for the host tests: only the netif and address types are used.
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 0
#define LWIP_UDP 0
#define LWIP_RAW 0
#define LWIP_ARP 0
#define LWIP_ETHERNET 0
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
