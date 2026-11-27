// lwIP configuration for the host tests: the real core ip4.c, netif.c and udp.c are
// compiled (see the Makefile), without TCP, ARP or a socket layer.
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 0
#define LWIP_UDP 1
#define LWIP_RAW 0
#define LWIP_ICMP 1
#define LWIP_ARP 0
#define LWIP_ETHERNET 0
#define LWIP_DNS 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_STATS 0
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT 8
#define SYS_LIGHTWEIGHT_PROT 0

// The hook the firmware configures, taken from its lwipopts.h by the Makefile. Left
// out of the pre-fix build, which is lwIP's lookup as it was.
#ifndef ROUTE_SELECT_LEGACY
#include "fw_hook_opts.h"
#endif
