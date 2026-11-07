// The C64 memory window during a whole-RAM copy, and access to it from an interrupt handler.
#ifndef C64_WINDOW_H
#define C64_WINDOW_H
#include <stdint.h>
#include "FreeRTOS.h"
#include "c64.h"   // C64_PEEK, C64_POKE
#include "u64.h"   // C64_DMA_MEMONLY

// Set while a whole-RAM copy (U64Machine::clear_ram, get_all_memory, UCI SAVEMEM) has C64_DMA_MEMONLY = 1 with
// interrupts on (the copy runs with the scheduler suspended). Only c64_ram_copy_begin/end change it, and they
// change it together with the mode, so an interrupt handler never sees the one without the other.
extern volatile uint8_t c64_ram_copy_active;

static inline void c64_ram_copy_begin(void)
{
    portENTER_CRITICAL();
    C64_DMA_MEMONLY = 1;
    c64_ram_copy_active = 1;
    portEXIT_CRITICAL();
}

static inline void c64_ram_copy_end(void)
{
    portENTER_CRITICAL();
    c64_ram_copy_active = 0;
    C64_DMA_MEMONLY = 0;
    portEXIT_CRITICAL();
}

// Writes an I/O register through the C64 memory window (C64_POKE) from an interrupt handler. Outside a copy it is
// that one write, plus one load of the flag and a branch. During a copy the window reaches RAM, not I/O, so: let
// the task's last window write land in its own mode, switch to the bus, write, let ours land, and put the copy's
// mode back. A window write does not stall on the bus (see U64Machine::poke_visible); any read of the window waits
// for it, so both reads are of $0000, which is RAM in either mode (a read of an I/O register can have side
// effects: CIA ICR).
static inline void c64_isr_poke_io(uint16_t address, uint8_t value)
{
    if (!c64_ram_copy_active) {
        C64_POKE(address, value);
        return;
    }
    (void)C64_PEEK(0);
    C64_DMA_MEMONLY = 0;
    C64_POKE(address, value);
    (void)C64_PEEK(0);
    C64_DMA_MEMONLY = 1;
}

#endif
