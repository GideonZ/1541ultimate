#ifndef U2_MEMORY_BACKEND_H
#define U2_MEMORY_BACKEND_H

#include "memory_backend.h"

class C64;

class U2MemoryBackend : public MemoryBackend
{
    C64 *machine;
    // Last sampled CIA2 port-A output; 0x03 is the VIC0 fallback before begin_session().
    uint8_t cached_cia2_porta;
    // Whether begin_redraw stopped the machine, and the bracket depth so a
    // nested caller cannot leave it stopped.
    bool redraw_stopped_machine;
    int redraw_depth;
    uint8_t read_cia2_porta(void);
public:
    explicit U2MemoryBackend(C64 *machine) : machine(machine), cached_cia2_porta(0x03),
        redraw_stopped_machine(false), redraw_depth(0) { }

    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len);
    virtual void write_block(uint16_t address, const uint8_t *src, uint16_t len);
    virtual bool supports_cpu_banking(void) const { return false; }
    virtual void begin_session(void);
    virtual void begin_redraw(void);
    virtual void end_redraw(void);
    virtual bool supports_vic_bank(void) const { return true; }
    virtual uint8_t get_live_vic_bank(void);
    virtual void set_live_vic_bank(uint8_t vic_bank);
    virtual bool supports_go(void) const { return true; }
    virtual bool supports_reset(void) const { return true; }
    virtual const char *source_name(uint16_t address) const;
};

#endif
