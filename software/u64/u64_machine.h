#ifndef U64_MACHINE_H
#define U64_MACHINE_H

#include "c64.h"


class U64Machine : public C64
{
    U64Machine() { }
    ~U64Machine() { }

    bool before_memory_access();
    void after_memory_access(uint8_t *, bool);

    void get_all_memory(uint8_t *pb);
    uint8_t peek(uint32_t address);
    void poke(uint32_t address, uint8_t byte);

public:
    friend class C64;
};

#endif
