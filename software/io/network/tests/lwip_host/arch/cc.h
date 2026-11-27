// lwIP wants an arch/cc.h; the host compiler's defaults are enough.
#ifndef HOST_ARCH_CC_H
#define HOST_ARCH_CC_H
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)
#endif
