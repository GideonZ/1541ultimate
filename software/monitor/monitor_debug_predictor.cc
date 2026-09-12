#include "monitor_debug_predictor.h"
#include "disassembler_6502.h"

#include <string.h>

void debug_predict(uint16_t pc, const uint8_t *bytes, bool illegal_enabled,
                   DebugPredictResult *out)
{
    if (!out) {
        return;
    }
    out->kind = DBG_PREDICT_UNSAFE;
    out->length = 0;
    out->fall_through = pc;
    out->has_target = false;
    out->branch_target = 0;

    if (!bytes) {
        return;
    }

    uint8_t opcode = bytes[0];

    // Fast classification for explicit control flow before we hand off to the
    // disassembler for fall-through addresses. Doing this here keeps the
    // predictor independent of the disassembler's text template.
    switch (opcode) {
        case 0x00: // BRK
            out->kind = DBG_PREDICT_BRK;
            out->length = 1;
            out->fall_through = (uint16_t)(pc + 1);
            return;
        case 0x20: // JSR abs
            out->kind = DBG_PREDICT_JSR;
            out->length = 3;
            out->fall_through = (uint16_t)(pc + 3);
            out->branch_target = (uint16_t)(bytes[1] | (bytes[2] << 8));
            out->has_target = true;
            return;
        case 0x4C: // JMP abs
            out->kind = DBG_PREDICT_JMP_ABS;
            out->length = 3;
            out->fall_through = (uint16_t)(pc + 3);
            out->branch_target = (uint16_t)(bytes[1] | (bytes[2] << 8));
            out->has_target = true;
            return;
        case 0x6C: // JMP (ind)
            out->kind = DBG_PREDICT_JMP_IND;
            out->length = 3;
            out->fall_through = (uint16_t)(pc + 3);
            return;
        case 0x60: // RTS
            out->kind = DBG_PREDICT_RTS;
            out->length = 1;
            out->fall_through = (uint16_t)(pc + 1);
            return;
        case 0x40: // RTI
            out->kind = DBG_PREDICT_RTI;
            out->length = 1;
            out->fall_through = (uint16_t)(pc + 1);
            return;
        case 0x10: case 0x30: case 0x50: case 0x70: // BPL/BMI/BVC/BVS
        case 0x90: case 0xB0: case 0xD0: case 0xF0: // BCC/BCS/BNE/BEQ
        {
            int8_t off = (int8_t)bytes[1];
            uint16_t taken = (uint16_t)(pc + 2 + off);
            out->kind = DBG_PREDICT_BRANCH;
            out->length = 2;
            out->fall_through = (uint16_t)(pc + 2);
            out->branch_target = taken;
            out->has_target = true;
            return;
        }
        default:
            break;
    }

    // Fall through: lean on the disassembler for length.
    Disassembled6502 d;
    disassemble_6502(pc, bytes, illegal_enabled, &d);
    if (!d.valid && !illegal_enabled) {
        out->kind = DBG_PREDICT_UNSAFE;
        out->length = 0;
        return;
    }
    if (d.length == 0 || d.length > 3) {
        out->kind = DBG_PREDICT_UNSAFE;
        out->length = 0;
        return;
    }
    out->kind = DBG_PREDICT_LINEAR;
    out->length = d.length;
    out->fall_through = (uint16_t)(pc + d.length);
    out->has_target = d.has_target;
    out->branch_target = d.target;
}
