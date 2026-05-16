#ifndef ASSEMBLER_6502_H
#define ASSEMBLER_6502_H

#include "integer.h"
#include "disassembler_6502.h"
#include "machine_monitor.h"

enum AsmAddrMode {
    AM_IMP = 0,   // implied:        BRK, RTS, ...
    AM_ACC,       // accumulator:    ASL A
    AM_IMM,       // immediate:      LDA #$12
    AM_ZP,        // zero page:      LDA $12
    AM_ZPX,       // zero page,X:    LDA $12,X
    AM_ZPY,       // zero page,Y:    LDX $12,Y
    AM_ZPS,       // illegal $nn,S:  NOP* $nn,S
    AM_IZX,       // (zp,X):         LDA ($12,X)
    AM_IZY,       // (zp),Y:         LDA ($12),Y
    AM_ABS,       // absolute:       LDA $1234
    AM_ABX,       // absolute,X:     LDA $1234,X
    AM_ABY,       // absolute,Y:     LDA $1234,Y
    AM_IND,       // (absolute):     JMP ($1234)
    AM_IAX,       // (absolute,X):   JMP ($1234,X) (illegal-NOP)
    AM_REL,       // relative:       BNE $1234
    AM_COUNT
};

struct AsmInsn {
    uint8_t  bytes[3];
    uint8_t  length;
    bool     illegal;
};

// Returns true on success and fills *out. On failure, returns false and sets
// *err to a relevant MonitorError, *err_pos to the byte offset within `text`
// that triggered the failure (best-effort).
//
// pc is the address that the instruction will be written at (used to encode
// branch relative offsets). illegal_enabled gates illegal opcodes.
bool monitor_assemble_line(const char *text, bool illegal_enabled, uint16_t pc,
                           AsmInsn *out, MonitorError *err);

// Resolve the opcode/length for a given mnemonic + addressing mode. Returns
// true iff the combination exists (and matches illegal_enabled). On success,
// *opcode and *length are filled.
bool monitor_lookup_opcode(const char *mnemonic, AsmAddrMode mode,
                           bool illegal_enabled, uint8_t *opcode,
                           uint8_t *length);

// Collect opcode candidates whose canonical mnemonic matches `prefix`
// case-insensitively. Results are deterministic: candidates are ordered by
// mnemonic, then by addressing simplicity with operand-free variants first,
// then by opcode value as a final tiebreaker.
int monitor_collect_opcode_candidates(const char *prefix, bool illegal_enabled,
                                      uint8_t *opcode_out, int max_candidates);

// For HEX-host testing: reset assembler caches (no-op outside tests).
void monitor_assembler_reset_caches(void);

#endif
