// Host stand-in for lwip/dns.h and its address types (IPv4 only, as LWIP_IPV6 is 0 in lwipopts.h).
// net_tls_test.cc fakes dns_gethostbyname and carries a copy of lwIP's ip4addr_aton.
#ifndef LWIP_DNS_H_NETSTUB
#define LWIP_DNS_H_NETSTUB
#include <stdint.h>
typedef int8_t err_t;
#define ERR_OK          0
#define ERR_MEM        -1
#define ERR_INPROGRESS -5
#define ERR_VAL        -6
#define ERR_ARG       -16
typedef struct { uint32_t addr; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
#define ip_2_ip4(ipaddr) (ipaddr)
#define ip4_addr_get_u32(src_ipaddr) ((src_ipaddr)->addr)
int ip4addr_aton(const char *cp, ip4_addr_t *addr);
typedef void (*dns_found_callback)(const char *name, const ip_addr_t *ipaddr, void *callback_arg);
err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr, dns_found_callback found, void *callback_arg);
#endif
