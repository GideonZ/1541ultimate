// The lwIP options sys_arch.c is built with on the host: the firmware's threading options
// (network/config/lwipopts.h), nothing else of the stack
#ifndef SYS_ARCH_HOST_LWIPOPTS_H
#define SYS_ARCH_HOST_LWIPOPTS_H
#define NO_SYS                  0
#define SYS_LIGHTWEIGHT_PROT    1
#define LWIP_TCPIP_CORE_LOCKING 1
#define SYS_STATS               0
#define LWIP_STATS              0
#endif
