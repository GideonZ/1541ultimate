#ifndef U64_MACHINE_H
#define U64_MACHINE_H

#include "c64.h"


class U64Machine : public C64
{
    U64Machine() { }
    ~U64Machine() { }
    void get_all_memory(uint8_t *pb);
public:
    void clear_ram();
    friend class C64;
};

#endif
