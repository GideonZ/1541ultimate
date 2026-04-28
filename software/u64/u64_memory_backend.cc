#include "u64_memory_backend.h"
#include "u64_machine.h"
#include "u64.h"

namespace {

static uint8_t effective_cpu_port(uint8_t ddr, uint8_t port)
{
    return (uint8_t)(((port & ddr) | ((uint8_t)(~ddr) & 0x07)) & 0x07);
}

static bool uses_live_cpu_port(U64Machine *machine, uint8_t monitor_cpu_port)
{
    uint8_t ddr = machine->peek(0x0000) & 0x07;
    uint8_t port = machine->peek(0x0001) & 0x07;
    return effective_cpu_port(ddr, port) == (monitor_cpu_port & 0x07);
}

}

void U64MemoryBackend :: begin_session(void)
{
    U64Machine *machine = (U64Machine *)C64 :: getMachine();

    stopped_machine_for_session = machine->begin_monitor_session();
}

void U64MemoryBackend :: end_session(void)
{
    if (stopped_machine_for_session) {
        ((U64Machine *)C64 :: getMachine())->end_monitor_session(stopped_machine_for_session);
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
    U64Machine *machine = (U64Machine *)C64 :: getMachine();
    uint8_t ddr = machine->peek(0x0000) & 0x07;
    uint8_t port = machine->peek(0x0001) & 0x07;

    // The active bank is determined by the 6510 data-direction register and port latch together.
    return effective_cpu_port(ddr, port);
}

uint8_t U64MemoryBackend :: get_live_vic_bank(void)
{
    uint8_t dd00 = ((U64Machine *)C64 :: getMachine())->peek_cpu(0xDD00, 0x07);
    return (uint8_t)(3 - (dd00 & 0x03));
}
