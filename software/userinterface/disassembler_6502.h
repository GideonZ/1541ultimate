#ifndef DISASSEMBLER_6502_H
#define DISASSEMBLER_6502_H

#include "integer.h"

struct Disassembled6502
{
    uint8_t length;
    bool illegal;
    bool has_target;
    uint16_t target;
    char text[32];
};

void disassemble_6502(uint16_t pc, const uint8_t *bytes, bool illegal_enabled, Disassembled6502 *out);

#endif