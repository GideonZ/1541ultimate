#include "u2_memory_backend.h"

#include "c64.h"

uint8_t U2MemoryBackend :: read(uint16_t address)
{
    if (!machine || !machine->exists()) {
        return 0;
    }
    return machine->peek(address);
}

void U2MemoryBackend :: write(uint16_t address, uint8_t value)
{
    if (!machine || !machine->exists()) {
        return;
    }
    machine->poke(address, value);
}

void U2MemoryBackend :: read_block(uint16_t address, uint8_t *dst, uint16_t len)
{
    if (!machine || !machine->exists()) {
        while (len) {
            *dst++ = 0;
            len--;
        }
        return;
    }
    while (len) {
        *dst++ = machine->peek(address++);
        len--;
    }
}

const char *U2MemoryBackend :: source_name(uint16_t) const
{
    // U2 reads the current CPU-visible aperture directly. Without ROM shadow
    // snapshots or monitor-selected banking, this is not guaranteed to be RAM.
    return "CPU";
}
