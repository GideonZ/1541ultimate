// Stand-in for u64/c64_window.h in the SAVEMEM test: the copy begin/end record when the flag goes up and down
// and whether the scheduler was suspended at the time.
#ifndef UCI_STUB_C64_WINDOW_H
#define UCI_STUB_C64_WINDOW_H
#include "u64.h"
static inline void c64_ram_copy_begin(void)
{
    C64_DMA_MEMONLY = 1;
    uci_fake_u64.copy_flag = 1;
    uci_fake_u64.begins++;
    if (uci_fake_u64.suspended) uci_fake_u64.begins_suspended++;
}
static inline void c64_ram_copy_end(void)
{
    uci_fake_u64.copy_flag = 0;
    C64_DMA_MEMONLY = 0;
    uci_fake_u64.ends++;
    if (uci_fake_u64.suspended) uci_fake_u64.ends_suspended++;
}
#endif
