// lwIP's arch/cc.h for sys_arch.c on the build host: the compiler's types, and the
// protection type of the firmware's cc.h (network/config/arch/cc.h)
#ifndef SYS_ARCH_HOST_CC_H
#define SYS_ARCH_HOST_CC_H
#include <stdint.h>
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)
typedef uint32_t sys_prot_t;
#endif
