// lwIP configuration of the WiFi TX simulation (wifi_tx_sim_test.cpp): the real TCP, IPv4 and UDP
// core on the raw API (NO_SYS), no ARP or sockets; the TCP and memory options are the firmware's,
// taken from its lwipopts.h by the Makefile (fw_tcp_opts.h), so that the simulation runs the
// windows, buffers and queue lengths of the device.
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
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
#define LWIP_CALLBACK_API 1
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT 8
#define SYS_LIGHTWEIGHT_PROT 0
#define CHECKSUM_CHECK_IP 0
#define CHECKSUM_CHECK_TCP 0
#define CHECKSUM_CHECK_UDP 0
#include "fw_tcp_opts.h"
