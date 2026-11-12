#ifndef SIM_CC_H
#define SIM_CC_H
#include <stdio.h>
#include <stdlib.h>
extern int sim_asserts; extern const char *sim_last_assert;
/* record instead of for(;;), so the run can show what comes next */
#define LWIP_PLATFORM_ASSERT(x) do { sim_asserts++; sim_last_assert = (x); } while (0)
#define LWIP_PLATFORM_DIAG(x)
#endif
