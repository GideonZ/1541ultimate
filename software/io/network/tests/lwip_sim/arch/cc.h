// lwIP's arch/cc.h for the TX simulation: an assertion that fails stops the run
#ifndef SIM_ARCH_CC_H
#define SIM_ARCH_CC_H
#include <stdio.h>
#include <stdlib.h>
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP assertion \"%s\" failed at %s:%d\n", x, __FILE__, __LINE__); abort(); } while (0)
#endif
