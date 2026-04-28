#ifndef U64_MEMORY_BACKEND_H
#define U64_MEMORY_BACKEND_H

#include "memory_backend.h"

class U64MemoryBackend : public MemoryBackend
{
public:
    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len);
    virtual uint8_t get_live_cpu_port(void);
    virtual uint8_t get_live_vic_bank(void);
};

#endif