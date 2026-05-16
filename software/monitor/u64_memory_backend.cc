#include "u64_memory_backend.h"
#include "u64_machine.h"
#include "u64.h"
#include "filemanager.h"

#include <string.h>

extern uint8_t _basic_bin_start[8192];
extern uint8_t _default_kernal_65_start[8192];
extern uint8_t _default_chars_bin_start[4096];

namespace {

static uint8_t monitor_basic_rom[8192];
static uint8_t monitor_kernal_rom[8192];
static uint8_t monitor_char_rom[4096];

static void snapshot_u64_rom(volatile uint8_t *src, uint8_t *dst, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

static bool rom_is_all_zero(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

}

void U64MemoryBackend :: load_monitor_char_rom_cache(C64 *machine)
{
    FRESULT fres = FR_NOT_READY;
    if (machine) {
        fres = FileManager :: getFileManager()->load_file(ROMS_DIRECTORY,
                                                          machine->get_cfg_string(CFG_C64_CHARFILE),
                                                          monitor_char_rom,
                                                          sizeof(monitor_char_rom),
                                                          NULL);
    }
    if (fres != FR_OK) {
        memcpy(monitor_char_rom, _default_chars_bin_start, sizeof(monitor_char_rom));
    }
}

static void load_monitor_kernal_rom_cache(C64 *machine)
{
    FRESULT fres = FR_NOT_READY;
    if (machine) {
        fres = FileManager :: getFileManager()->load_file(ROMS_DIRECTORY,
                                                          machine->get_cfg_string(CFG_C64_KERNFILE),
                                                          monitor_kernal_rom,
                                                          sizeof(monitor_kernal_rom),
                                                          NULL);
    }
    if (fres != FR_OK) {
        memcpy(monitor_kernal_rom, _default_kernal_65_start, sizeof(monitor_kernal_rom));
    }
}

void U64MemoryBackend :: load_monitor_rom_cache(C64 *machine)
{
    snapshot_u64_rom((volatile uint8_t *)U64_BASIC_BASE, monitor_basic_rom, sizeof(monitor_basic_rom));
    snapshot_u64_rom((volatile uint8_t *)U64_KERNAL_BASE, monitor_kernal_rom, sizeof(monitor_kernal_rom));

    if (rom_is_all_zero(monitor_basic_rom, sizeof(monitor_basic_rom))) {
        memcpy(monitor_basic_rom, _basic_bin_start, sizeof(monitor_basic_rom));
    }
    if (rom_is_all_zero(monitor_kernal_rom, sizeof(monitor_kernal_rom))) {
        load_monitor_kernal_rom_cache(machine);
    }
    load_monitor_char_rom_cache(machine);
}

enum CpuRegionMapping {
    MAP_RAM = 0,
    MAP_BASIC,
    MAP_CHAR,
    MAP_IO,
    MAP_KERNAL,
};

static CpuRegionMapping cpu_region_mapping(uint16_t address, uint8_t cpu_port)
{
    cpu_port &= 0x07;

    if (address >= 0xA000 && address <= 0xBFFF) {
        return ((cpu_port & 0x03) == 0x03) ? MAP_BASIC : MAP_RAM;
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        if ((cpu_port & 0x03) == 0x00) {
            return MAP_RAM;
        }
        return (cpu_port & 0x04) ? MAP_IO : MAP_CHAR;
    }
    if (address >= 0xE000) {
        return (cpu_port & 0x02) ? MAP_KERNAL : MAP_RAM;
    }
    return MAP_RAM;
}

static bool uses_live_mapping_for_address(uint16_t address, uint8_t live_cpu_port, uint8_t monitor_cpu_port)
{
    return cpu_region_mapping(address, live_cpu_port) == cpu_region_mapping(address, monitor_cpu_port);
}

bool U64MemoryBackend :: freeze_available(void) const
{
    return machine && !machine->is_accessible();
}

bool U64MemoryBackend :: read_monitor_rom_byte(uint16_t address, uint8_t cpu_port, uint8_t *value) const
{
    switch (cpu_region_mapping(address, cpu_port)) {
        case MAP_BASIC:
            *value = monitor_basic_rom[address - 0xA000];
            return true;
        case MAP_CHAR:
            *value = monitor_char_rom[address - 0xD000];
            return true;
        case MAP_KERNAL:
            *value = monitor_kernal_rom[address - 0xE000];
            return true;
        default:
            return false;
    }
}

void U64MemoryBackend :: begin_session(void)
{
    // Do NOT freeze the C64 by default — per-access DMA stops handle the
    // brief pauses required to read/write memory. The user can opt in via
    // the monitor's Z (freeze) toggle, which calls set_frozen(true).
    stopped_machine_for_session = false;
    load_monitor_rom_cache(machine);
}

void U64MemoryBackend :: end_session(void)
{
    if (machine && stopped_machine_for_session) {
        machine->end_stopped_session(stopped_machine_for_session);
        stopped_machine_for_session = false;
    }
}

void U64MemoryBackend :: set_frozen(bool on)
{
    if (!machine) {
        return;
    }

    if (on && !stopped_machine_for_session) {
        stopped_machine_for_session = machine->begin_stopped_session();
        load_monitor_rom_cache(machine);
    } else if (!on && stopped_machine_for_session) {
        machine->end_stopped_session(stopped_machine_for_session);
        stopped_machine_for_session = false;
    }
}

uint8_t U64MemoryBackend :: read(uint16_t address)
{
    if (!machine) {
        return 0;
    }

    uint8_t cpu_port = get_monitor_cpu_port();
    uint8_t live_cpu_port = machine->get_cpu_port();
    uint8_t rom_value = 0;
    bool use_cached_rom = machine->is_accessible() || is_frozen() ||
            !uses_live_mapping_for_address(address, live_cpu_port, cpu_port);

    if (read_monitor_rom_byte(address, cpu_port, &rom_value) && use_cached_rom) {
        return rom_value;
    }
    if (machine->is_accessible()) {
        return machine->peek_cpu(address, cpu_port);
    }
    if (!uses_live_mapping_for_address(address, live_cpu_port, cpu_port)) {
        return machine->peek_cpu(address, cpu_port);
    }
    return machine->peek(address);
}

void U64MemoryBackend :: write(uint16_t address, uint8_t value)
{
    if (!machine) {
        return;
    }

    uint8_t cpu_port = get_monitor_cpu_port();
    uint8_t live_cpu_port = machine->get_cpu_port();

    if (machine->is_accessible()) {
        machine->poke_cpu(address, value, cpu_port);
        return;
    }
    if (!uses_live_mapping_for_address(address, live_cpu_port, cpu_port)) {
        machine->poke_cpu(address, value, cpu_port);
        return;
    }
    machine->poke(address, value);
}

void U64MemoryBackend :: read_block(uint16_t address, uint8_t *dst, uint16_t len)
{
    while (len) {
        *dst++ = read(address++);
        len--;
    }
}

uint8_t U64MemoryBackend :: get_live_cpu_port(void)
{
    return machine ? machine->get_cpu_port() : 0;
}

uint8_t U64MemoryBackend :: get_live_vic_bank(void)
{
    if (!machine) {
        return 0;
    }

    uint8_t dd00 = machine->peek_cpu(0xDD00, 0x07);
    return (uint8_t)(3 - (dd00 & 0x03));
}

uint8_t U64MemoryBackend :: monitor_poll_hz(void) const
{
    return (C64_VIDEOFORMAT & VIDEO_FMT_60_HZ) ? 60 : 50;
}
