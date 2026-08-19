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
    // One stopped session for the whole block. Per-byte peek() would stop and
    // resume a running machine hundreds of times per redraw, and C64::stop()
    // ends in an unbounded wait once its two timed attempts fail, so a single
    // unhonoured stop would hang the caller for good.
    bool stopped_it = machine->begin_stopped_session();
    while (len) {
        *dst++ = machine->peek(address++);
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
    // Otherwise the NMI capture, DDR-resolved: lines the direction register
    // makes inputs read high, and that is what the PLA banks on. It is taken at
    // every freeze and when the monitor opens, and lives until C64::unfreeze(),
    // so while the machine is frozen it cannot go stale -- the 6510 is halted.
    // On a machine left running it is what the port was when it was sampled.
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
}

void U2MemoryBackend :: invalidate_live_cpu_port_cache(void)
{
    // Only the BRK reading. The NMI sample has its own lifetime, ended by
    // C64::unfreeze() or C64::reset() -- the two events after which the port
    // can differ. Dropping it here as well would throw away the only reading
    // there is whenever the debugger is left on a still-frozen machine, and
    // while frozen another one cannot be taken.
    observed_cpu_port_valid = false;
}

// CIA2 port A holds the VIC bank in its bottom two bits, inverted. While the
// freezer menu is up those bits have been turned into inputs, so read them from
// the value backup_io() captured instead of off the pins; see
// C64::get_frozen_cia2_porta().
uint8_t U2MemoryBackend :: read_cia2_porta(void)
{
    if (!machine || !machine->exists()) {
        return 0x03;    // bank 0 in the register's inverted encoding
    }
    if (machine->is_frozen()) {
        cached_cia2_porta = machine->get_frozen_cia2_porta();
    } else if (machine->is_stopped()) {
        // Already stopped, so this read costs nothing extra.
        cached_cia2_porta = machine->peek(0xDD00);
    }
    // Running and not frozen: deliberately do NOT read. poll() calls
    // get_live_vic_bank() every idle iteration, and peek() stops and resumes a
    // running machine per access, so reading here would stop the C64
    // continuously. The row this feeds is only on screen while frozen.
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
    uint8_t base;
    if (machine->is_frozen() || machine->is_stopped()) {
        base = read_cia2_porta();
    } else {
        // The cache is only refreshed while frozen or stopped, so on a running
        // machine it can still hold the power-on default. Writing that back
        // would drive the other six bits of CIA2 port A -- IEC ATN, CLK and
        // DATA out, and the RS-232 transmit line -- to zero, which corrupts a
        // transfer in progress. One short stopped session reads what is
        // actually there; this is a user bank selection, not a polled read.
        bool stopped_it = machine->begin_stopped_session();
        base = machine->peek(0xDD00);
        cached_cia2_porta = base;
        machine->end_stopped_session(stopped_it);
    }
    uint8_t porta = (uint8_t)((base & 0xFC) | (uint8_t)(3 - (vic_bank & 0x03)));
    // Write the register even while frozen: init_io()/restore_io() leave the
    // data register alone, so the program sees the new bank once the data
    // direction is handed back on unfreeze. The backup is updated too, or the
    // next read here returns the pre-write bank.
    machine->poke(0xDD00, porta);
    if (machine->is_frozen()) {
        machine->set_frozen_cia2_porta(porta);
    }
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
        return (loram && hiram) ? "BAS" : "RAM";
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        // $D000 is RAM whenever both bank bits are clear; otherwise CHAREN
        // decides between the character generator and the I/O space.
        if (!loram && !hiram) {
            return "RAM";
        }
        return charen ? "I/O" : "CHR";
    }
    if (address >= 0xE000) {
        return hiram ? "KRN" : "RAM";
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
