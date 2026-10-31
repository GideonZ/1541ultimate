// lwIP configuration for the reference-pbuf pool test: the real core pbuf.c and memp.c are
// compiled (see the Makefile) with static pools, as on the device, and with the firmware's
// MEMP_NUM_PBUF, taken from software/network/config/lwipopts.h by the Makefile.
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 0
#define LWIP_UDP 1
#define LWIP_RAW 0
#define LWIP_ICMP 0
#define LWIP_ARP 0
#define LWIP_ETHERNET 0
#define LWIP_DNS 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_STATS 0
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 0
#define MEM_ALIGNMENT 8
#define SYS_LIGHTWEIGHT_PROT 0

// The pre-fix build keeps the single entry the firmware had
#ifdef REF_POOL_LEGACY
#define MEMP_NUM_PBUF 1
#else
#include "fw_pool_opts.h"
#endif
