#include "monitor_breakpoints.h"

#include <string.h>
#include <stdio.h>

MonitorBreakpoints :: MonitorBreakpoints()
{
    clear_all();
}

void MonitorBreakpoints :: clear_all(void)
{
    for (int i = 0; i < MONITOR_BREAKPOINT_SLOT_COUNT; i++) {
        slots[i].used = false;
        slots[i].enabled = false;
        slots[i].address = 0;
        slots[i].cpu_port = 0x07;
    }
}

int MonitorBreakpoints :: find_at(uint16_t address) const
{
    for (int i = 0; i < MONITOR_BREAKPOINT_SLOT_COUNT; i++) {
        if (slots[i].used && slots[i].address == address) {
            return i;
        }
    }
    return -1;
}

int MonitorBreakpoints :: allocate(uint16_t address, uint8_t cpu_port)
{
    int existing = find_at(address);
    if (existing >= 0) {
        return existing;
    }
    for (int i = 0; i < MONITOR_BREAKPOINT_SLOT_COUNT; i++) {
        if (!slots[i].used) {
            slots[i].used = true;
            slots[i].enabled = true;
            slots[i].address = address;
            slots[i].cpu_port = (uint8_t)(cpu_port & 0x07);
            return i;
        }
    }
    return -1;
}

void MonitorBreakpoints :: clear_slot(int slot)
{
    if (slot < 0 || slot >= MONITOR_BREAKPOINT_SLOT_COUNT) {
        return;
    }
    slots[slot].used = false;
    slots[slot].enabled = false;
    slots[slot].address = 0;
    slots[slot].cpu_port = 0x07;
}

void MonitorBreakpoints :: set_enabled(int slot, bool enabled)
{
    if (slot < 0 || slot >= MONITOR_BREAKPOINT_SLOT_COUNT) {
        return;
    }
    if (slots[slot].used) {
        slots[slot].enabled = enabled;
    }
}

const MonitorBreakpointSlot *MonitorBreakpoints :: get(int slot) const
{
    if (slot < 0 || slot >= MONITOR_BREAKPOINT_SLOT_COUNT) {
        return 0;
    }
    return &slots[slot];
}

void MonitorBreakpoints :: format_popup_row(char *out, int out_len, int slot,
                                            const MonitorBreakpointSlot *bp)
{
    if (!out || out_len <= 0) {
        return;
    }
    if (!bp || !bp->used) {
        sprintf(out, "%d EMPTY", slot);
        return;
    }
    sprintf(out, "%d %s $%04X CPU%d", slot,
            bp->enabled ? "SET" : "OFF",
            (unsigned)bp->address,
            (int)(bp->cpu_port & 0x07));
}
