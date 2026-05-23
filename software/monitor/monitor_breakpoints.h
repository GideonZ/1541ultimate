#ifndef MONITOR_BREAKPOINTS_H
#define MONITOR_BREAKPOINTS_H

#include "integer.h"

// Non-persistent breakpoint table used by Assembly Debug mode. 10 slots, by
// design. Slot state stays in RAM only - we deliberately do not write through
// to bookmark storage, both because breakpoints have very different semantics
// and because the spec calls out "Prefer non-persistent breakpoint state".
#define MONITOR_BREAKPOINT_SLOT_COUNT 10

struct MonitorBreakpointSlot {
    bool used;
    bool enabled;
    uint16_t address;
    // CPU bank context, captured at set-time, so the breakpoint UI can describe
    // where the BP lives even after the user changes CPU banks. Stepping code
    // consults this when patching/restoring BRK bytes.
    uint8_t cpu_port;
};

class MonitorBreakpoints
{
    MonitorBreakpointSlot slots[MONITOR_BREAKPOINT_SLOT_COUNT];
public:
    MonitorBreakpoints();

    void clear_all(void);

    // Return slot index for an existing breakpoint at this address, else -1.
    int find_at(uint16_t address) const;

    // Allocate the lowest free slot for `address`. Returns the slot index or
    // -1 if all slots are full. If a slot already exists at this address it
    // is returned and not reallocated.
    int allocate(uint16_t address, uint8_t cpu_port);

    void store_slot(int slot, uint16_t address, uint8_t cpu_port);
    void clear_slot(int slot);
    void set_enabled(int slot, bool enabled);

    int slot_count(void) const { return MONITOR_BREAKPOINT_SLOT_COUNT; }
    const MonitorBreakpointSlot *get(int slot) const;

    // Format a single slot row for the popup. `out` must hold at least 24
    // characters. Format: "0 SET   $C000 CPU7" or "0 EMPTY".
    static void format_popup_row(char *out, int out_len, int slot,
                                 const MonitorBreakpointSlot *bp);
};

#endif
