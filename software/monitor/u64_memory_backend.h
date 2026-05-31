#ifndef U64_MEMORY_BACKEND_H
#define U64_MEMORY_BACKEND_H

#include "memory_backend.h"

class C64;
class U64Machine;

class U64MemoryBackend : public MemoryBackend
{
    U64Machine *machine;
    bool stopped_machine_for_session;
    bool observed_live_cpu_port_valid;
    uint8_t observed_live_cpu_port;

    void load_monitor_char_rom_cache(C64 *machine);
    void load_monitor_rom_cache(C64 *machine);
    bool read_monitor_rom_byte(uint16_t address, uint8_t cpu_port, uint8_t *value) const;
public:
    explicit U64MemoryBackend(U64Machine *machine)
        : machine(machine), stopped_machine_for_session(false),
          observed_live_cpu_port_valid(false), observed_live_cpu_port(0x07) { }
    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len);
    virtual uint8_t get_live_cpu_port(void);
    virtual uint8_t get_live_vic_bank(void);
    virtual uint8_t monitor_poll_hz(void) const;
    virtual void begin_session(void);
    virtual void end_session(void);

    virtual bool supports_freeze(void) const { return true; }
    virtual bool freeze_available(void) const;
    virtual bool is_frozen(void) const { return stopped_machine_for_session; }
    virtual void set_frozen(bool on);
    virtual bool supports_reset(void) const { return true; }
    virtual bool reset_machine(void);
    virtual DebugSession *create_debug_session(void);
    void set_observed_live_cpu_port(uint8_t cpu_port);
    void clear_observed_live_cpu_port(void);
};

#endif
