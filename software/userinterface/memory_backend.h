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

    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len)
    {
        while (len) {
            *dst++ = read(address++);
            len--;
        }
    }

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
        return (uint8_t)(3 - (read(0xDD00) & 0x03));
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
