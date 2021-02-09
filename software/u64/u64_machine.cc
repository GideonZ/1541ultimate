#include "u64.h"
#include "u64_machine.h"
#include "FreeRTOS.h"
#include <string.h>

void U64Machine :: get_all_memory(uint8_t *pb)
{
    bool freezerMenu = isFrozen;

    if (!freezerMenu) {
        stop(false);
    }
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = 1;
    memcpy(pb, (uint8_t *)C64_MEMORY_BASE, 65536);
    C64_DMA_MEMONLY = 0;
    portEXIT_CRITICAL();

    if (!freezerMenu) {
        resume();
    } else { // if we were in freezer menu, the backup of the 1K-4K RAM should be used
        // restore memory
        memcpy(pb + 1024, screen_backup, 1024);
        memcpy(pb + 2048, ram_backup, 2048);
    }
}
