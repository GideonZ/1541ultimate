#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* Low profile */
#define NO_SYS                          1
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

#define LWIP_DHCP                       1
#define LWIP_DNS                        1
#define LWIP_ARP                        1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_NETIF_HOSTNAME             1

/* turn on debugging */
// turn on the macro
#define LWIP_DEBUG 1
// define debug of types
#define LWIP_DBG_TYPES_ON (LWIP_DBG_TRACE | LWIP_DBG_STATE | LWIP_DBG_FRESH | LWIP_DBG_HALT)

//#define MEM_DEBUG    LWIP_DBG_ON
//#define MEMP_DEBUG   LWIP_DBG_ON
//#define DHCP_DEBUG   LWIP_DBG_ON
//#define PBUF_DEBUG   LWIP_DBG_ON
//#define ETHARP_DEBUG LWIP_DBG_ON
#define NETIF_DEBUG  LWIP_DBG_ON
//#define TCP_DEBUG    LWIP_DBG_ON

//#define TCP_OUTPUT_DEBUG LWIP_DBG_ON
//#define TCP_INPUT_DEBUG  LWIP_DBG_ON

#define _DEBUG LWIP_DBG_ON

/* Keep everything at default */

#define MEM_SIZE                        65536
#define MEM_ALIGNMENT 4

//#define TCP_SND_QUEUELEN                40
//#define MEMP_NUM_TCP_SEG                TCP_SND_QUEUELEN
//#define TCP_SND_BUF                     (12 * TCP_MSS)
//#define TCP_WND                         (10 * TCP_MSS)


#endif /* __LWIPOPTS_H__ */
