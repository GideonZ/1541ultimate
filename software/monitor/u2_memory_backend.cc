#include "u2_memory_backend.h"

#include "c64.h"

uint8_t U2MemoryBackend :: read(uint16_t address)
{
    if (!machine || !machine->exists()) {
        return 0;
    }
    return machine->peek(address);
}

// dma_transfer_frozen puts the frozen C64 mode back and clears
// C64_DMA_MEMONLY around the access, both of which change what the bus decodes
// at the address being written. While the freezer holds the machine the 6510
// is still executing the freezer's own code, so it is still driving that bus,
// and a write issued into that window is intermittently lost: measured on an
// Ultimate II+L in a C64 Ultimate, 5 of 45 single-byte Hex edits at addresses
// of $1000 and above did not reach memory, while 18 of 18 below $1000, which
// need nothing rebanked, all did. The C64's own DMA path stops the machine for
// the same call for the same reason (C64_Subsys::executeCommand in
// io/c64/c64_subsys.cc), and read_block below stops it through C64::peek.
// Stopping it here makes the monitor's write path agree with both.
void U2MemoryBackend :: write(uint16_t address, uint8_t value)
{
    if (!machine || !machine->exists()) {
        return;
    }
    if (machine->is_accessible()) {
        bool stopped_it = machine->begin_stopped_session();
        machine->dma_transfer_frozen(address, &value, 1, 0);
        machine->end_stopped_session(stopped_it);
        return;
    }
    machine->poke(address, value);
}

void U2MemoryBackend :: read_block(uint16_t address, uint8_t *dst, uint16_t len)
{
    if (!machine || !machine->exists()) {
        while (len) {
            *dst++ = 0;
            len--;
        }
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    while (len) {
        *dst++ = machine->peek(address++);
        len--;
    }
    machine->end_stopped_session(stopped_it);
}

void U2MemoryBackend :: write_block(uint16_t address, const uint8_t *src, uint16_t len)
{
    if (!machine || !machine->exists()) {
        return;
    }
    if (machine->is_accessible()) {
        // Same reason as write() above, and the same stop covers the whole
        // block, so the block is written while one CPU state holds for all of
        // it rather than per byte.
        bool stopped_it = machine->begin_stopped_session();
        machine->dma_transfer_frozen(address, const_cast<uint8_t *>(src), len, 0);
        machine->end_stopped_session(stopped_it);
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    while (len) {
        machine->poke(address++, *src++);
        len--;
    }
    machine->end_stopped_session(stopped_it);
}

void U2MemoryBackend :: begin_session(void)
{
    if (!machine || !machine->exists() || machine->is_accessible()) {
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    cached_cia2_porta = machine->peek(0xDD00);
    machine->end_stopped_session(stopped_it);
}

void U2MemoryBackend :: begin_redraw(void)
{
    if (redraw_depth++ > 0) {
        return;
    }
    redraw_stopped_machine = false;
    if (!machine || !machine->exists()) {
        return;
    }
    redraw_stopped_machine = machine->begin_stopped_session();
}

void U2MemoryBackend :: end_redraw(void)
{
    // An end without a begin releases nothing; it never stopped the machine.
    if (redraw_depth == 0 || --redraw_depth > 0) {
        return;
    }
    if (machine) {
        machine->end_stopped_session(redraw_stopped_machine);
    }
    redraw_stopped_machine = false;
}

uint8_t U2MemoryBackend :: read_cia2_porta(void)
{
    if (!machine || !machine->exists()) {
        return 0x03;
    }
    if (machine->is_accessible()) {
        cached_cia2_porta = machine->get_frozen_cia2_porta();
    } else if (machine->is_stopped()) {
        cached_cia2_porta = machine->peek(0xDD00);
    }
    return cached_cia2_porta;
}

uint8_t U2MemoryBackend :: get_live_vic_bank(void)
{
    return (uint8_t)(3 - (read_cia2_porta() & 0x03));
}

void U2MemoryBackend :: set_live_vic_bank(uint8_t vic_bank)
{
    if (!machine || !machine->exists()) {
        return;
    }
    if (machine->is_accessible()) {
        uint8_t porta = read_cia2_porta();
        porta = (uint8_t)((porta & 0xFC) | (uint8_t)(3 - (vic_bank & 0x03)));
        machine->set_frozen_cia2_porta(porta);
        cached_cia2_porta = porta;
        return;
    }
    uint8_t porta;
    if (machine->is_stopped()) {
        porta = read_cia2_porta();
    } else {
        bool stopped_it = machine->begin_stopped_session();
        porta = machine->peek(0xDD00);
        machine->end_stopped_session(stopped_it);
    }
    porta = (uint8_t)((porta & 0xFC) | (uint8_t)(3 - (vic_bank & 0x03)));
    machine->poke(0xDD00, porta);
    cached_cia2_porta = porta;
}

const char *U2MemoryBackend :: source_name(uint16_t) const
{
    // U2 reads the current CPU-visible aperture directly. Without ROM shadow
    // snapshots or monitor-selected banking, this is not guaranteed to be RAM.
    return "CPU";
}
