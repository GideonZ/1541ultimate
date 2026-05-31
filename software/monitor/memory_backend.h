#ifndef MEMORY_BACKEND_H
#define MEMORY_BACKEND_H

#include "integer.h"

class DebugSession;

enum MonitorBackingStore {
    MONITOR_BACKING_RAM = 0,
    MONITOR_BACKING_BASIC,
    MONITOR_BACKING_KERNAL,
    MONITOR_BACKING_IO,
    MONITOR_BACKING_CHAR
};

static inline MonitorBackingStore monitor_backing_store_for_cpu_port(uint16_t address, uint8_t cpu_port)
{
    cpu_port &= 0x07;

    if (address >= 0xA000 && address <= 0xBFFF) {
        return ((cpu_port & 0x03) == 0x03) ?
            MONITOR_BACKING_BASIC : MONITOR_BACKING_RAM;
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        if ((cpu_port & 0x03) == 0x00) {
            return MONITOR_BACKING_RAM;
        }
        return (cpu_port & 0x04) ? MONITOR_BACKING_IO : MONITOR_BACKING_CHAR;
    }
    if (address >= 0xE000) {
        return (cpu_port & 0x02) ? MONITOR_BACKING_KERNAL : MONITOR_BACKING_RAM;
    }
    return MONITOR_BACKING_RAM;
}

static inline const char *monitor_backing_store_tag(MonitorBackingStore target)
{
    switch (target) {
        case MONITOR_BACKING_BASIC:  return "BAS";
        case MONITOR_BACKING_KERNAL: return "KRN";
        case MONITOR_BACKING_IO:     return "I/O";
        case MONITOR_BACKING_CHAR:   return "CHR";
        case MONITOR_BACKING_RAM:
        default:                     return "RAM";
    }
}

static inline const char *monitor_backing_store_source_name(MonitorBackingStore target)
{
    switch (target) {
        case MONITOR_BACKING_BASIC:  return "BASIC";
        case MONITOR_BACKING_KERNAL: return "KERNAL";
        case MONITOR_BACKING_IO:     return "IO";
        case MONITOR_BACKING_CHAR:   return "CHAR";
        case MONITOR_BACKING_RAM:
        default:                     return "RAM";
    }
}

static inline bool monitor_backing_store_is_visible_rom(MonitorBackingStore target)
{
    return target == MONITOR_BACKING_BASIC ||
           target == MONITOR_BACKING_KERNAL ||
           target == MONITOR_BACKING_CHAR;
}

class MemoryBackend
{
    uint8_t monitor_cpu_port;
public:
    MemoryBackend() : monitor_cpu_port(0x07) { }
    virtual ~MemoryBackend() { }

    virtual uint8_t read(uint16_t address) = 0;
    virtual void write(uint16_t address, uint8_t value) = 0;
    virtual void begin_session(void) { }
    virtual void end_session(void) { }

    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len)
    {
        while (len) {
            *dst++ = read(address++);
            len--;
        }
    }

    // Freeze / pause control. Backends that can hold the host machine in a
    // stopped state across many reads/writes (so register/IO state is stable)
    // override these. The monitor exposes a Z toggle to drive this. Default
    // behaviour is a no-op so non-U64 backends remain unaffected.
    virtual bool supports_freeze(void) const { return false; }
    virtual bool freeze_available(void) const { return supports_freeze(); }
    virtual bool is_frozen(void) const { return false; }
    virtual void set_frozen(bool) { }
    virtual bool supports_cpu_banking(void) const { return true; }
    virtual bool supports_vic_bank(void) const { return true; }
    virtual bool supports_go(void) const { return true; }
    virtual bool supports_reset(void) const { return false; }
    virtual bool reset_machine(void) { return false; }
    virtual uint8_t monitor_poll_hz(void) const { return 50; }

    // Stepping / breakpoint / re-entry support. Backends that cannot
    // resume-and-trap the live CPU return NULL; Debug-mode key handling then
    // surfaces a clear refusal instead of silently faking results. The
    // returned object is owned by the caller.
    virtual DebugSession *create_debug_session(void) { return 0; }

    virtual void set_monitor_cpu_port(uint8_t value)
    {
        monitor_cpu_port = value & 0x07;
    }

    virtual uint8_t get_monitor_cpu_port(void) const
    {
        return monitor_cpu_port & 0x07;
    }

    virtual uint8_t get_live_cpu_port(void)
    {
        return read(0x0001) & 0x07;
    }

    virtual MonitorBackingStore backing_store_for_cpu_port(uint16_t address, uint8_t cpu_port) const
    {
        return monitor_backing_store_for_cpu_port(address, cpu_port);
    }

    virtual uint8_t get_live_vic_bank(void)
    {
        // CIA2 stores the VIC bank in inverted form; keep the monitor's
        // user-facing VIC0..VIC3 numbering aligned with the visible base.
        return (uint8_t)(3 - (read(0xDD00) & 0x03));
    }

    virtual void set_live_vic_bank(uint8_t vic_bank)
    {
        uint8_t saved_cpu_port = get_monitor_cpu_port();
        uint8_t dd00;

        set_monitor_cpu_port(0x07);
        dd00 = read(0xDD00);
        write(0xDD00, (uint8_t)((dd00 & 0xFC) | (uint8_t)(3 - (vic_bank & 0x03))));
        set_monitor_cpu_port(saved_cpu_port);
    }

    virtual const char *source_name(uint16_t address) const
    {
        return monitor_backing_store_source_name(
            backing_store_for_cpu_port(address, get_monitor_cpu_port()));
    }
};

#endif
