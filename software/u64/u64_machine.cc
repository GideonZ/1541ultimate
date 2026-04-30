#include "u64.h"
#include "u64_machine.h"
#include "FreeRTOS.h"
#include <string.h>

namespace {

static uint16_t sanitize_c64_address(uint32_t address)
{
    return (uint16_t)address;
}

static uint8_t read_frozen_byte(volatile uint8_t *ram, bool freezerMenu, uint32_t address,
                                uint32_t *screen_backup, uint32_t *ram_backup)
{
    if (freezerMenu) {
        if ((address >= 1024) && (address < 2048)) {
            return ((uint8_t *)screen_backup)[address - 1024];
        }
        if ((address >= 2048) && (address < 4096)) {
            return ((uint8_t *)ram_backup)[address - 2048];
        }
    }
    return ram[address];
}

static void write_frozen_byte(volatile uint8_t *ram, bool freezerMenu, uint32_t address, uint8_t value,
                              uint32_t *screen_backup, uint32_t *ram_backup)
{
    ram[address] = value;
    if (freezerMenu) {
        if ((address >= 1024) && (address < 2048)) {
            ((uint8_t *)screen_backup)[address - 1024] = value;
        } else if ((address >= 2048) && (address < 4096)) {
            ((uint8_t *)ram_backup)[address - 2048] = value;
        }
    }
}

static void override_cpu_port(volatile uint8_t *ram, bool freezerMenu, uint8_t cpu_port,
                              uint8_t *saved_ddr, uint8_t *saved_port,
                              uint32_t *screen_backup, uint32_t *ram_backup)
{
    *saved_ddr = read_frozen_byte(ram, freezerMenu, 0x0000, screen_backup, ram_backup);
    *saved_port = read_frozen_byte(ram, freezerMenu, 0x0001, screen_backup, ram_backup);
    write_frozen_byte(ram, freezerMenu, 0x0000, (uint8_t)((*saved_ddr & 0xF8) | 0x07), screen_backup, ram_backup);
    write_frozen_byte(ram, freezerMenu, 0x0001, (uint8_t)((*saved_port & 0xF8) | (cpu_port & 0x07)), screen_backup, ram_backup);
    ram[0x0001];
    ram[0x0001];
}

static void restore_cpu_port(volatile uint8_t *ram, bool freezerMenu, uint8_t saved_ddr,
                             uint8_t saved_port, uint32_t *screen_backup, uint32_t *ram_backup)
{
    write_frozen_byte(ram, freezerMenu, 0x0000, saved_ddr, screen_backup, ram_backup);
    write_frozen_byte(ram, freezerMenu, 0x0001, saved_port, screen_backup, ram_backup);
    ram[0x0001];
    ram[0x0001];
}

static uint8_t read_visible_byte(volatile uint8_t *ram, bool freezerMenu, uint32_t address,
                                 uint32_t *screen_backup, uint32_t *ram_backup)
{
    uint8_t saved_memonly = C64_DMA_MEMONLY;

    C64_DMA_MEMONLY = 0;
    uint8_t value = read_frozen_byte(ram, freezerMenu, address, screen_backup, ram_backup);
    C64_DMA_MEMONLY = saved_memonly;
    return value;
}

static void write_visible_byte(volatile uint8_t *ram, bool freezerMenu, uint32_t address, uint8_t value,
                               uint32_t *screen_backup, uint32_t *ram_backup)
{
    uint8_t saved_memonly = C64_DMA_MEMONLY;

    C64_DMA_MEMONLY = 0;
    write_frozen_byte(ram, freezerMenu, address, value, screen_backup, ram_backup);
    C64_DMA_MEMONLY = saved_memonly;
}

static uint8_t read_cpu_mapped_byte(volatile uint8_t *ram, bool freezerMenu, uint32_t address, uint8_t cpu_port,
                                    uint32_t *screen_backup, uint32_t *ram_backup)
{
    uint8_t raw = read_frozen_byte(ram, freezerMenu, address, screen_backup, ram_backup);

    if (address == 0x0000) {
        return (uint8_t)((raw & 0xF8) | 0x07);
    }
    if (address == 0x0001) {
        return (uint8_t)((raw & 0xF8) | (cpu_port & 0x07));
    }
    if (address >= 0xA000 && address <= 0xBFFF) {
        if ((cpu_port & 0x03) == 0x03) {
            return ((volatile uint8_t *)U64_BASIC_BASE)[address - 0xA000];
        }
        return raw;
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        if ((cpu_port & 0x03) == 0x00) {
            return raw;
        }
        if (cpu_port & 0x04) {
            return read_visible_byte(ram, freezerMenu, address, screen_backup, ram_backup);
        }
        return ((volatile uint8_t *)U64_CHARROM_BASE)[address - 0xD000];
    }
    if (address >= 0xE000) {
        if (cpu_port & 0x02) {
            return ((volatile uint8_t *)U64_KERNAL_BASE)[address - 0xE000];
        }
        return raw;
    }
    return raw;
}

static void write_cpu_mapped_byte(volatile uint8_t *ram, bool freezerMenu, uint32_t address, uint8_t value, uint8_t cpu_port,
                                  uint32_t *screen_backup, uint32_t *ram_backup)
{
    if (address >= 0xD000 && address <= 0xDFFF && (cpu_port & 0x03) != 0x00 && (cpu_port & 0x04)) {
        write_visible_byte(ram, freezerMenu, address, value, screen_backup, ram_backup);
        return;
    }
    write_frozen_byte(ram, freezerMenu, address, value, screen_backup, ram_backup);
}

static void copy_raw_block(volatile uint8_t *ram, bool freezerMenu, uint32_t address, uint8_t *dst, uint32_t len,
                           uint32_t *screen_backup, uint32_t *ram_backup)
{
    memcpy(dst, (const void *)(ram + address), len);

    if (freezerMenu) {
        uint32_t overlap;
        if ((address < 2048) && ((address + len) > 1024)) {
            uint32_t start = (address > 1024) ? address : 1024;
            overlap = address + len - start;
            if (overlap > 1024) {
                overlap = 1024;
            }
            memcpy(dst + (start - address), ((uint8_t *)screen_backup) + (start - 1024), overlap);
        }
        if ((address < 4096) && ((address + len) > 2048)) {
            uint32_t start = (address > 2048) ? address : 2048;
            overlap = address + len - start;
            if (overlap > 2048) {
                overlap = 2048;
            }
            memcpy(dst + (start - address), ((uint8_t *)ram_backup) + (start - 2048), overlap);
        }
    }
}

}

bool U64Machine :: before_memory_access(bool memOnly, bool *stopped_it) {
    bool freezerMenu = isFrozen;
    bool wasStopped = is_stopped();

    if (stopped_it) {
        *stopped_it = (!freezerMenu && !wasStopped);
    }
    if (!freezerMenu && !wasStopped) {
        stop(false);
    }
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = memOnly ? 1 : 0;
    return freezerMenu;
}

void U64Machine :: after_memory_access(uint8_t *pb, bool freezerMenu, bool stopped_it) {
    C64_DMA_MEMONLY = 0;
    portEXIT_CRITICAL();
    if (!freezerMenu && stopped_it) {
        resume();
    } else if (pb) {
        // if we were in freezer menu, the backup of the 1K-4K RAM should be used to restore memory
        memcpy(pb + 1024, screen_backup, 1024);
        memcpy(pb + 2048, ram_backup, 2048);
    }
}

bool U64Machine :: begin_monitor_session()
{
    bool wasStopped = is_stopped();
    if (!wasStopped) {
        stop(false);
    }
    return !wasStopped;
}

void U64Machine :: end_monitor_session(bool stopped_it)
{
    if (stopped_it) {
        resume();
    }
}

void U64Machine :: get_all_memory(uint8_t *pb)
{
    // Match the REST C64_DMA_RAW_READ path: stop the machine and read the visible C64 aperture directly.
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(false, &stopped_it);
    memcpy(pb, (uint8_t *)C64_MEMORY_BASE, 65536);
    after_memory_access(pb, freezerMenu, stopped_it);
}

void U64Machine :: read_block(uint32_t address, uint8_t *dst, uint32_t len)
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(isFrozen, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;

    copy_raw_block(ram, freezerMenu, address, dst, len, screen_backup, ram_backup);

    after_memory_access(0, freezerMenu, stopped_it);
}

void U64Machine :: read_cpu_block(uint32_t address, uint8_t *dst, uint32_t len, uint8_t cpu_port)
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(true, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint8_t saved_serve = C64_SERVE_CONTROL;

    C64_SERVE_CONTROL = saved_serve | SERVE_WHILE_STOPPED;
    for (uint32_t offset = 0; offset < len; offset++) {
        dst[offset] = read_cpu_mapped_byte(ram, freezerMenu, address + offset, cpu_port, screen_backup, ram_backup);
    }
    C64_SERVE_CONTROL = saved_serve;
    after_memory_access(0, freezerMenu, stopped_it);
}

void U64Machine :: read_visible_block(uint32_t address, uint8_t *dst, uint32_t len)
{
    bool freezerMenu = isFrozen;
    bool wasStopped = is_stopped();
    uint8_t saved_memonly = C64_DMA_MEMONLY;
    if (!freezerMenu && !wasStopped) {
        stop(false);
    }
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = 0;
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint8_t saved_serve = C64_SERVE_CONTROL;

    C64_SERVE_CONTROL = saved_serve | SERVE_WHILE_STOPPED;

    for (uint32_t offset = 0; offset < len; offset++) {
        uint32_t current = address + offset;
        uint8_t byte = ram[current];

        if (freezerMenu) {
            byte = read_frozen_byte(ram, true, current, screen_backup, ram_backup);
        }
        dst[offset] = byte;
    }

    C64_SERVE_CONTROL = saved_serve;
    C64_DMA_MEMONLY = saved_memonly;
    portEXIT_CRITICAL();

    if (!freezerMenu && !wasStopped) {
        resume();
    }
}

uint8_t U64Machine :: get_cpu_port(void)
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(isFrozen, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint8_t ddr = read_frozen_byte(ram, freezerMenu, 0x0000, screen_backup, ram_backup) & 0x07;
    uint8_t port = read_frozen_byte(ram, freezerMenu, 0x0001, screen_backup, ram_backup) & 0x07;

    after_memory_access(0, freezerMenu, stopped_it);
    return (uint8_t)(((port & ddr) | ((uint8_t)(~ddr) & 0x07)) & 0x07);
}

uint8_t U64Machine :: peek(uint32_t address) 
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(isFrozen, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint16_t safe_address = sanitize_c64_address(address);
    uint8_t byte = read_frozen_byte(ram, freezerMenu, safe_address, screen_backup, ram_backup);

    after_memory_access(0, freezerMenu, stopped_it);
    return byte;
}

uint8_t U64Machine :: peek_cpu(uint32_t address, uint8_t cpu_port)
{
    uint8_t byte = 0;
    read_cpu_block(address, &byte, 1, cpu_port);
    return byte;
}

uint8_t U64Machine :: peek_visible(uint32_t address)
{
    bool freezerMenu = isFrozen;
    bool wasStopped = is_stopped();
    uint8_t saved_memonly = C64_DMA_MEMONLY;
    if (!freezerMenu && !wasStopped) {
        stop(false);
    }
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = 0;
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint8_t saved_serve = C64_SERVE_CONTROL;

    C64_SERVE_CONTROL = saved_serve | SERVE_WHILE_STOPPED;

    uint8_t byte = ram[address];

    if (freezerMenu) {
        byte = read_frozen_byte(ram, true, address, screen_backup, ram_backup);
    }

    C64_SERVE_CONTROL = saved_serve;
    C64_DMA_MEMONLY = saved_memonly;
    portEXIT_CRITICAL();

    if (!freezerMenu && !wasStopped) {
        resume();
    }
    return byte;
}

void U64Machine :: poke(uint32_t address, uint8_t byte)
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(isFrozen, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint16_t safe_address = sanitize_c64_address(address);

    write_frozen_byte(ram, freezerMenu, safe_address, byte, screen_backup, ram_backup);

    after_memory_access(0, freezerMenu, stopped_it);
}

void U64Machine :: poke_cpu(uint32_t address, uint8_t byte, uint8_t cpu_port)
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(true, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;
    uint8_t saved_serve = C64_SERVE_CONTROL;

    C64_SERVE_CONTROL = saved_serve | SERVE_WHILE_STOPPED;
    write_cpu_mapped_byte(ram, freezerMenu, address, byte, cpu_port, screen_backup, ram_backup);
    C64_SERVE_CONTROL = saved_serve;
    after_memory_access(0, freezerMenu, stopped_it);
}

void U64Machine :: poke_visible(uint32_t address, uint8_t byte)
{
    bool stopped_it = false;
    bool freezerMenu = before_memory_access(false, &stopped_it);
    volatile uint8_t *ram = (volatile uint8_t *)C64_MEMORY_BASE;

    write_frozen_byte(ram, freezerMenu, address, byte, screen_backup, ram_backup);

    after_memory_access(0, freezerMenu, stopped_it);
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
