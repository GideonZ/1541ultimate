#include "u64_memory_backend.h"
#include "u64_machine.h"
#include "u64.h"

static bool uses_live_cpu_port(U64Machine *machine, uint8_t monitor_cpu_port)
{
    return machine->get_cpu_port() == (monitor_cpu_port & 0x07);
}

void U64MemoryBackend :: begin_session(void)
{
    // Do NOT freeze the C64 by default — per-access DMA stops handle the
    // brief pauses required to read/write memory. The user can opt in via
    // the monitor's Z (freeze) toggle, which calls set_frozen(true).
    stopped_machine_for_session = false;
}

void U64MemoryBackend :: end_session(void)
{
    if (stopped_machine_for_session) {
        ((U64Machine *)C64 :: getMachine())->end_monitor_session(stopped_machine_for_session);
        stopped_machine_for_session = false;
    }
}

void U64MemoryBackend :: set_frozen(bool on)
{
    U64Machine *machine = (U64Machine *)C64 :: getMachine();

    if (on && !stopped_machine_for_session) {
        // begin_monitor_session() returns true only if it actually had to
        // stop the machine. If something else (e.g. freezer overlay) has
        // it stopped already, leave it that way and don't try to resume it.
        stopped_machine_for_session = machine->begin_monitor_session();
    } else if (!on && stopped_machine_for_session) {
        machine->end_monitor_session(stopped_machine_for_session);
        stopped_machine_for_session = false;
    }
}

uint8_t U64MemoryBackend :: read(uint16_t address)
{
    U64Machine *machine = (U64Machine *)C64 :: getMachine();
    uint8_t cpu_port = get_monitor_cpu_port();

    // While frozen the live 6510 port latch is not observable through DMA,
    // and the C64 bus is held in MODE_ULTIMAX (BASIC/KERNAL/RAM-under-ROM
    // are not on the bus). Route through the CPU-mapped provider and treat
    // monitor_cpu_port as authoritative.
    if (machine->is_accessible()) {
        return machine->peek_cpu(address, cpu_port);
    }
    if (!uses_live_cpu_port(machine, cpu_port)) {
        return machine->peek_cpu(address, cpu_port);
    }
    return machine->peek(address);
}

void U64MemoryBackend :: write(uint16_t address, uint8_t value)
{
    U64Machine *machine = (U64Machine *)C64 :: getMachine();
    uint8_t cpu_port = get_monitor_cpu_port();

    if (machine->is_accessible()) {
        machine->poke_cpu(address, value, cpu_port);
        return;
    }
    if (!uses_live_cpu_port(machine, cpu_port)) {
        machine->poke_cpu(address, value, cpu_port);
        return;
    }
    machine->poke(address, value);
}

void U64MemoryBackend :: read_block(uint16_t address, uint8_t *dst, uint16_t len)
{
    U64Machine *machine = (U64Machine *)C64 :: getMachine();
    uint8_t cpu_port = get_monitor_cpu_port();

    if (machine->is_accessible()) {
        machine->read_cpu_block(address, dst, len, cpu_port);
        return;
    }
    if (!uses_live_cpu_port(machine, cpu_port)) {
        machine->read_cpu_block(address, dst, len, cpu_port);
        return;
    }

    machine->read_block(address, dst, len);
}

uint8_t U64MemoryBackend :: get_live_cpu_port(void)
{
    return ((U64Machine *)C64 :: getMachine())->get_cpu_port();
}

uint8_t U64MemoryBackend :: get_live_vic_bank(void)
{
    // CIA2 uses inverted bank bits; translate them back to the monitor's
    // user-facing VIC0..VIC3 order before rendering status text.
    uint8_t dd00 = ((U64Machine *)C64 :: getMachine())->peek_cpu(0xDD00, 0x07);
    return (uint8_t)(3 - (dd00 & 0x03));
}
