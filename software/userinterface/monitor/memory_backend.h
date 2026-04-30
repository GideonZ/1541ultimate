#ifndef MEMORY_BACKEND_H
#define MEMORY_BACKEND_H

#include "integer.h"

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
    virtual bool is_frozen(void) const { return false; }
    virtual void set_frozen(bool) { }

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
        uint8_t cpu_port = get_monitor_cpu_port();

        if (address >= 0xA000 && address <= 0xBFFF) {
            return ((cpu_port & 0x03) == 0x03) ? "BASIC" : "RAM";
        }
        if (address >= 0xD000 && address <= 0xDFFF) {
            if ((cpu_port & 0x03) == 0x00) {
                return "RAM";
            }
            return (cpu_port & 0x04) ? "IO" : "CHAR";
        }
        if (address >= 0xE000) {
            return (cpu_port & 0x02) ? "KERNAL" : "RAM";
        }
        return "RAM";
    }
};

#endif
