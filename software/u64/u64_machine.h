#ifndef U64_MACHINE_H
#define U64_MACHINE_H

#include "c64.h"


class U64Machine : public C64
{
    U64Machine() { }
    ~U64Machine() { }

    bool before_memory_access(bool memOnly, bool *stopped_it);
    void after_memory_access(uint8_t *, bool, bool stopped_it);

    void get_all_memory(uint8_t *pb);
public:
    bool begin_monitor_session();
    void end_monitor_session(bool stopped_it);
    void clear_ram();
    uint8_t get_cpu_port(void);
    uint8_t peek(uint32_t address);
    uint8_t peek_cpu(uint32_t address, uint8_t cpu_port);
    uint8_t peek_visible(uint32_t address);
    void poke(uint32_t address, uint8_t byte);
    void poke_cpu(uint32_t address, uint8_t byte, uint8_t cpu_port);
    void poke_visible(uint32_t address, uint8_t byte);
    void read_block(uint32_t address, uint8_t *dst, uint32_t len);
    void read_cpu_block(uint32_t address, uint8_t *dst, uint32_t len, uint8_t cpu_port);
    void read_visible_block(uint32_t address, uint8_t *dst, uint32_t len);
    friend class C64;
};

#endif
