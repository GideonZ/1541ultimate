// Host stand-in for lwip/tcpip.h: the core lock, faked in net_tls_test.cc.
#ifndef LWIP_TCPIP_H_NETSTUB
#define LWIP_TCPIP_H_NETSTUB
void fake_lock_tcpip_core(void);
void fake_unlock_tcpip_core(void);
#define LOCK_TCPIP_CORE()   fake_lock_tcpip_core()
#define UNLOCK_TCPIP_CORE() fake_unlock_tcpip_core()
#endif
