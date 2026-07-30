#ifndef MONITOR_BREAKPOINTS_H
#define MONITOR_BREAKPOINTS_H

#include "integer.h"
#include "memory_backend.h"

// Non-persistent breakpoint table used by Assembly Debug mode. 10 slots, by
// design. Slot state stays in RAM only - we deliberately do not write through
// to bookmark storage, both because breakpoints have very different semantics
// and because the spec calls out "Prefer non-persistent breakpoint state".
#define MONITOR_BREAKPOINT_SLOT_COUNT 10
#define MONITOR_BREAKPOINT_LABEL_MAX 4
#define MONITOR_BREAKPOINT_LABEL_STORAGE (MONITOR_BREAKPOINT_LABEL_MAX + 1)

struct MonitorBreakpointSlot {
    bool used;
    bool enabled;
    uint16_t address;
    // View CPU-port context captured at set-time. The backing-store target is
    // the breakpoint identity; the port is the concrete route used to patch it.
    uint8_t view_cpu_port;
    MonitorBackingStore target;
    char label[MONITOR_BREAKPOINT_LABEL_STORAGE];
};

class MonitorBreakpoints
{
    MonitorBreakpointSlot slots[MONITOR_BREAKPOINT_SLOT_COUNT];
public:
    MonitorBreakpoints();

    void clear_all(void);

    // Return slot index for an existing breakpoint at this address/target,
    // else -1. The address-only overload is for callers that deliberately want
    // any breakpoint at the address regardless of backing store.
    int find_at(uint16_t address) const;
    int find_at(uint16_t address, MonitorBackingStore target) const;

    // Allocate the lowest free slot for `address`. Returns the slot index or
    // -1 if all slots are full. If a slot already exists at this
    // address/target it is returned and not reallocated.
    int allocate(uint16_t address, uint8_t cpu_port);
    int allocate(uint16_t address, uint8_t cpu_port, MonitorBackingStore target);

    void store_slot(int slot, uint16_t address, uint8_t cpu_port);
    void store_slot(int slot, uint16_t address, uint8_t cpu_port, MonitorBackingStore target);
    void clear_slot(int slot);
    void set_enabled(int slot, bool enabled);
    void set_label(int slot, const char *label);

    int slot_count(void) const { return MONITOR_BREAKPOINT_SLOT_COUNT; }
    const MonitorBreakpointSlot *get(int slot) const;
    static void normalize_label(char *out, int out_len, const char *input);

    // Format a single slot row for the popup. `out` must hold at least 24
    // characters. Format: "0 SET LABL $C000 RAM" or "0 EMPTY".
    static void format_popup_row(char *out, int out_len, int slot,
                                 const MonitorBreakpointSlot *bp);
};

#endif
