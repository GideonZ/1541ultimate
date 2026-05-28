#include "monitor_breakpoints.h"

#include <string.h>
#include <stdio.h>

static char monitor_breakpoint_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

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
        slots[i].label[0] = 0;
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
            slots[i].label[0] = 0;
            return i;
        }
    }
    return -1;
}

void MonitorBreakpoints :: store_slot(int slot, uint16_t address, uint8_t cpu_port)
{
    if (slot < 0 || slot >= MONITOR_BREAKPOINT_SLOT_COUNT) {
        return;
    }
    bool was_used = slots[slot].used;
    slots[slot].used = true;
    slots[slot].enabled = true;
    slots[slot].address = address;
    slots[slot].cpu_port = (uint8_t)(cpu_port & 0x07);
    if (!was_used) {
        slots[slot].label[0] = 0;
    }
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
    slots[slot].label[0] = 0;
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

void MonitorBreakpoints :: set_label(int slot, const char *label)
{
    if (slot < 0 || slot >= MONITOR_BREAKPOINT_SLOT_COUNT) {
        return;
    }
    if (slots[slot].used) {
        normalize_label(slots[slot].label, sizeof(slots[slot].label), label);
    }
}

const MonitorBreakpointSlot *MonitorBreakpoints :: get(int slot) const
{
    if (slot < 0 || slot >= MONITOR_BREAKPOINT_SLOT_COUNT) {
        return 0;
    }
    return &slots[slot];
}

void MonitorBreakpoints :: normalize_label(char *out, int out_len, const char *input)
{
    int written = 0;

    if (!out || out_len <= 0) {
        return;
    }
    out[0] = 0;
    if (out_len <= 1 || !input) {
        return;
    }
    for (int i = 0; input[i] && written < MONITOR_BREAKPOINT_LABEL_MAX &&
                    written + 1 < out_len; i++) {
        char c = input[i];
        if (c == ' ' || c == '\t') {
            continue;
        }
        out[written++] = monitor_breakpoint_normalize_char(c);
    }
    out[written] = 0;
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
    if (bp->label[0]) {
        sprintf(out, "%d %s %-4s $%04X CPU%d", slot,
                bp->enabled ? "SET" : "OFF",
                bp->label,
                (unsigned)bp->address,
                (int)(bp->cpu_port & 0x07));
    } else {
        sprintf(out, "%d %s $%04X CPU%d", slot,
                bp->enabled ? "SET" : "OFF",
                (unsigned)bp->address,
                (int)(bp->cpu_port & 0x07));
    }
}
