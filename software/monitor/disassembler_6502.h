#ifndef DISASSEMBLER_6502_H
#define DISASSEMBLER_6502_H

#include "integer.h"

struct Disassembled6502
{
    uint8_t length;
    uint8_t operand_bytes;
    bool valid;
    bool illegal;
    bool has_target;
    uint16_t target;
    char text[32];
};

void disassemble_6502(uint16_t pc, const uint8_t *bytes, bool illegal_enabled, Disassembled6502 *out);

// Accessors used by the matching assembler so the encoder remains
// bit-aligned with the decoder by construction.
const char *disassembler_6502_template(uint8_t opcode);
bool        disassembler_6502_is_illegal(uint8_t opcode);

#endif
