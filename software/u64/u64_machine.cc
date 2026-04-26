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
    } else if (pb) {
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
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint8_t byte = ram[address];

    if (freezerMenu) {
        if ((address >= 1024) && (address < 2048)) {
            byte = screen_backup[address - 1024];
        } else if ((address >= 2048) && (address < 4096)) {
            byte = ram_backup[address - 2048];
        }
    }

    after_memory_access(0, freezerMenu);
    return byte;
}

void U64Machine :: poke(uint32_t address, uint8_t byte)
{
    bool freezerMenu = before_memory_access();
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;

    ram[address] = byte;
    if (freezerMenu) {
        if ((address >= 1024) && (address < 2048)) {
            screen_backup[address - 1024] = byte;
        } else if ((address >= 2048) && (address < 4096)) {
            ram_backup[address - 2048] = byte;
        }
    }

    after_memory_access(0, freezerMenu);
}

void U64Machine :: clear_ram()
{
#ifndef RECOVERYAPP
    // Clear RAM from $0000 to $FFFF
    extern const uint8_t _raminit_bin_start[65536];
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;

    bool freezerMenu = isFrozen;
    if (!freezerMenu) {
        stop(false);
    }
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = 1;
    memcpy((uint8_t*)ram, _raminit_bin_start, 65536);
    C64_DMA_MEMONLY = 0;
    portEXIT_CRITICAL();
    if (!freezerMenu) {
        resume();
    } else { // if we were in freezer menu, the backup of the 1K-4K RAM should be used
        // restore memory
        memcpy(screen_backup, _raminit_bin_start + 1024, 1024);
        memcpy(ram_backup, _raminit_bin_start + 2048, 2048);
    }
#endif
}

