#include "u2_memory_backend.h"
#include "monitor_debug_u2.h"

#include "c64.h"

uint8_t U2MemoryBackend :: read(uint16_t address)
{
    if (!machine || !machine->exists()) {
        return 0;
    }
    return machine->peek(address);
}

// dma_transfer_frozen's bank-changing bus decode races the freezer's own
// 6510 still driving that bus: measured 5 of 45 single-byte Hex edits at
// $1000+ lost while 18 of 18 below $1000 (no rebanking) landed. Stop the
// machine here, matching C64_Subsys::executeCommand and read_block/C64::peek.
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

bool U2MemoryBackend :: resolved_cpu_port(uint8_t *out) const
{
    // Best source: a value the 6510 itself reported. The BRK capture stub runs
    // LDA $01 on the CPU, so while a debug context is held this is exact.
    if (observed_cpu_port_valid) {
        if (out) *out = observed_cpu_port & 0x07;
        return true;
    }
    // Otherwise the DDR-resolved NMI capture (input lines read high, what the
    // PLA banks on), taken at every freeze/monitor-open and valid until
    // C64::unfreeze() — cannot go stale while frozen (6510 halted); on a
    // running machine it's the port as sampled.
    uint8_t port, ddr;
    if (machine && machine->get_captured_cpu_port(&port, &ddr)) {
        if (out) {
            *out = (uint8_t)(((port & ddr) | (uint8_t)(~ddr)) & 0x07);
        }
        return true;
    }
    return false;
}

uint8_t U2MemoryBackend :: get_live_cpu_port(void)
{
    uint8_t resolved;
    if (resolved_cpu_port(&resolved)) {
        return resolved;
    }
    // $07 is the shape callers need; live_cpu_port_known() reports false, so the
    // status row shows the VIC-only form rather than a CPU bank nobody measured.
    return 0x07;
}

void U2MemoryBackend :: set_observed_cpu_port(uint8_t cpu_port)
{
    observed_cpu_port = cpu_port & 0x07;
    observed_cpu_port_valid = true;
}

void U2MemoryBackend :: begin_session(void)
{
    if (machine) {
        machine->capture_cpu_port_via_nmi();
    }
    if (!machine || !machine->exists() || machine->is_accessible()) {
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    cached_cia2_porta = machine->peek(0xDD00);
    machine->end_stopped_session(stopped_it);
}

void U2MemoryBackend :: invalidate_live_cpu_port_cache(void)
{
    // Only the BRK reading; the NMI sample has its own lifetime, ended by
    // C64::unfreeze()/C64::reset(). Dropping it too would lose the only
    // reading available when left on a still-frozen machine, which cannot
    // take another one.
    observed_cpu_port_valid = false;
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

const char *U2MemoryBackend :: source_name(uint16_t address) const
{
    // Reads come back through the CPU-visible aperture, so what a byte is
    // depends on the 6510's port, which is only knowable from a BRK capture.
    // Until one exists there is nothing to classify against, and "CPU" says
    // the byte came from the CPU's view without naming the device.
    uint8_t resolved;
    if (!resolved_cpu_port(&resolved)) {
        return "CPU";
    }

    const uint8_t port = resolved;
    const bool loram = (port & 0x01) != 0;
    const bool hiram = (port & 0x02) != 0;
    const bool charen = (port & 0x04) != 0;

    if (address >= 0xA000 && address <= 0xBFFF) {
        return (loram && hiram) ? "BASIC" : "RAM";
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        // $D000 is RAM whenever both bank bits are clear; otherwise CHAREN
        // decides between the character generator and the I/O space.
        if (!loram && !hiram) {
            return "RAM";
        }
        return charen ? "IO" : "CHAR";
    }
    if (address >= 0xE000) {
        return hiram ? "KERNAL" : "RAM";
    }
    return "RAM";
}

bool U2MemoryBackend :: reset_machine(void)
{
    if (!machine) {
        return false;
    }
    // A reset puts the 6510's port back to its default, so the last BRK
    // capture no longer describes the machine.
    invalidate_live_cpu_port_cache();
    machine->reset();
    return true;
}

DebugSession *U2MemoryBackend :: create_debug_session(void)
{
    return create_u2_debug_session(this);
}
