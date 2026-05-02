#ifndef U64_MEMORY_BACKEND_H
#define U64_MEMORY_BACKEND_H

#include "memory_backend.h"

class C64;

class U64MemoryBackend : public MemoryBackend
{
    bool stopped_machine_for_session;

    void load_monitor_char_rom_cache(C64 *machine);
    void load_monitor_rom_cache(C64 *machine);
    bool read_monitor_rom_byte(uint16_t address, uint8_t cpu_port, uint8_t *value) const;
public:
    U64MemoryBackend() : stopped_machine_for_session(false) { }
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
};

#endif
