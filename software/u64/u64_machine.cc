#include "u64.h"
#include "u64_machine.h"
#include "FreeRTOS.h"
#include <string.h>

bool U64Machine :: before_memory_access() {
    bool freezerMenu = isFrozen;
    if (!freezerMenu) {
        stop(false);
    }
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = 1;
    return freezerMenu;
}

void U64Machine :: after_memory_access(uint8_t *pb, bool freezerMenu) {
    C64_DMA_MEMONLY = 0;
    portEXIT_CRITICAL();
    if (!freezerMenu) {
        resume();
    } else { 
        // if we were in freezer menu, the backup of the 1K-4K RAM should be used to restore memory
        memcpy(pb + 1024, screen_backup, 1024);
        memcpy(pb + 2048, ram_backup, 2048);
    }
}

void U64Machine :: get_all_memory(uint8_t *pb)
{
    bool freezerMenu = before_memory_access();
    memcpy(pb, (uint8_t *)C64_MEMORY_BASE, 65536);
    after_memory_access(pb, freezerMenu);
}

uint8_t U64Machine :: peek(uint32_t address) 
{
    bool freezerMenu = before_memory_access();
    uint8_t *pb = new uint8_t[64 * 1024];
    get_all_memory(pb);
    
    uint8_t byte = *(pb + address);
    printf("peek(%d + %d)=%d", pb, address, byte);
    
    after_memory_access(pb, freezerMenu);
    delete[] pb;
    return byte;
}

void U64Machine :: poke(uint32_t address, uint8_t byte)
{
    bool freezerMenu = before_memory_access();    
    uint8_t *pb = new uint8_t[64 * 1024];
    get_all_memory(pb);

    // TODO Ensure this works on freeze mode, not just in HDMI overlay mode
    memset((void *) C64_MEMORY_BASE + address, byte, 1);
    printf("poke(%d + %d)=%d", C64_MEMORY_BASE, address, byte);

    after_memory_access(pb, freezerMenu);
    delete[] pb;
}

