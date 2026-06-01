#ifndef U2_MEMORY_BACKEND_H
#define U2_MEMORY_BACKEND_H

#include "memory_backend.h"

class C64;

class U2MemoryBackend : public MemoryBackend
{
    C64 *machine;
public:
    explicit U2MemoryBackend(C64 *machine) : machine(machine) { }

    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len);
    virtual bool supports_cpu_banking(void) const { return false; }
    virtual bool supports_vic_bank(void) const { return false; }
    virtual bool supports_go(void) const { return true; }
    virtual const char *source_name(uint16_t address) const;
};

#endif
