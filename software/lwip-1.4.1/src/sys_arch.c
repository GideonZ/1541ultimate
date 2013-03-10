#include "arch/cc.h"

u32_t sys_now(void)
{
    static u32_t time = 0;
    return ++time; // FIXME
}
