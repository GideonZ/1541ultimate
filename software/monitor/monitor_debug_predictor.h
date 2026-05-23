#ifndef MONITOR_DEBUG_PREDICTOR_H
#define MONITOR_DEBUG_PREDICTOR_H

#include "integer.h"

// Classification of a 6502 instruction for Debug-mode stepping.
// Pure logic; no firmware/memory access. The caller supplies up to three
// instruction bytes at the current PC.
enum DebugPredictKind {
    DBG_PREDICT_LINEAR,   // Falls through: install BRK at PC + length.
    DBG_PREDICT_JSR,      // Step-over target is PC + 3.
    DBG_PREDICT_JMP_ABS,  // PC := operand word; install BRK at branch_target.
    DBG_PREDICT_JMP_IND,  // PC := (operand word); target unresolved here.
    DBG_PREDICT_BRANCH,   // Conditional: falls through OR jumps to target.
    DBG_PREDICT_RTS,      // Pops PC from stack + 1; target unresolved here.
    DBG_PREDICT_RTI,      // Pops SR + PC; target unresolved here.
    DBG_PREDICT_BRK,      // Refuse: cannot patch over a BRK.
    DBG_PREDICT_UNSAFE    // Illegal or undecodable; refuse.
};

struct DebugPredictResult {
    DebugPredictKind kind;
    uint8_t length;          // Instruction length (1..3); 0 on UNSAFE.
    uint16_t fall_through;   // PC + length (only meaningful for non-UNSAFE).
    bool has_target;
    uint16_t branch_target;  // Absolute target for JMP/JSR/branch when known.
};

void debug_predict(uint16_t pc, const uint8_t *bytes, bool illegal_enabled,
                   DebugPredictResult *out);

#endif
