#ifndef U2_MEMORY_BACKEND_H
#define U2_MEMORY_BACKEND_H

#include "memory_backend.h"

class C64;

class U2MemoryBackend : public MemoryBackend
{
    C64 *machine;
    bool observed_cpu_port_valid;
    uint8_t observed_cpu_port;
    uint8_t cached_cia2_porta;
    // Whether begin_redraw stopped the machine, and the bracket depth so a
    // nested caller cannot leave it stopped.
    bool redraw_stopped_machine;
    int redraw_depth;
    uint8_t read_cia2_porta(void);

public:
    explicit U2MemoryBackend(C64 *machine)
        : machine(machine), observed_cpu_port_valid(false), observed_cpu_port(0x07),
          cached_cia2_porta(0x03), redraw_stopped_machine(false), redraw_depth(0) { }

    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len);
    virtual void write_block(uint16_t address, const uint8_t *src, uint16_t len);
    // The view bank cannot be selected here: a DMA read of $0001 returns a
    // mirror that is only refreshed at reset, not the live 6510 port.
    virtual bool supports_cpu_banking(void) const { return false; }

    // The 6510's own port, as reported by code that ran on the 6510: the BRK
    // capture stub executes LDA $01 and stores the result where the cartridge
    // can read it. install_brk_at() decides RAM versus ROM from this value.
    virtual uint8_t get_live_cpu_port(void);
    bool resolved_cpu_port(uint8_t *out) const;
    virtual bool live_cpu_port_known(void) const
    { return resolved_cpu_port(0); }
    // Only the BRK capture, deliberately not the NMI one. The NMI capture asks
    // this to decide whether it can skip its round trip, so answering with any
    // known port would let one stale value keep refusing its own replacement.
    virtual bool has_debug_observed_cpu_port(void) const
    { return observed_cpu_port_valid; }
    void set_observed_cpu_port(uint8_t cpu_port);
    virtual void invalidate_live_cpu_port_cache(void);
    // Opening the monitor takes one CPU-port reading, so the banking is on the
    // footer from the first redraw instead of after the user freezes. On a
    // running machine that reading is a sample: it is dropped again whenever
    // the debugger lets the program run, because the program can rewrite $01.
    virtual void begin_session(void);
    virtual void begin_redraw(void);
    virtual void end_redraw(void);
    virtual bool supports_vic_bank(void) const { return true; }
    virtual uint8_t get_live_vic_bank(void);
    virtual void set_live_vic_bank(uint8_t vic_bank);
    virtual bool supports_go(void) const { return true; }
    virtual bool supports_reset(void) const { return true; }
    virtual bool reset_machine(void);
    virtual const char *source_name(uint16_t address) const;
    virtual DebugSession *create_debug_session(void);
};

#endif
