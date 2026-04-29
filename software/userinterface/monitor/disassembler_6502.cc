#include "disassembler_6502.h"

extern "C" {
#include "small_printf.h"
}

#include <string.h>

namespace {

static const char *const opcode_templates[256] = {
    "BRK          ", "ORA ($nn,X)  ", "HLT*         ", "ASO*($nn,X)  ",
    "NOP*$nn      ", "ORA $nn      ", "ASL $nn      ", "ASO*$nn      ",
    "PHP          ", "ORA #        ", "ASL A        ", "ANC*#        ",
    "NOP*$nnnn    ", "ORA $nnnn    ", "ASL $nnnn    ", "ASO*$nnnn    ",

    "BPL rel      ", "ORA ($nn),Y  ", "HLT*         ", "ASO*($nn),Y  ",
    "NOP*$nn,X    ", "ORA $nn,X    ", "ASL $nn,X    ", "ASO*$nn,X    ",
    "CLC          ", "ORA $nnnn,Y  ", "NOP*         ", "ASO*$nnnn,Y  ",
    "NOP*$nnnn,X  ", "ORA $nnnn,X  ", "ASL $nnnn,X  ", "ASO*$nnnn,X  ",

    "JSR $nnnn    ", "AND ($nn,X)  ", "HLT*         ", "RLA*($nn,X)  ",
    "BIT $nn      ", "AND $nn      ", "ROL $nn      ", "RLA*$nn      ",
    "PLP          ", "AND #        ", "ROL A        ", "ANC*#        ",
    "BIT $nnnn    ", "AND $nnnn    ", "ROL $nnnn    ", "RLA*$nnnn    ",

    "BMI rel      ", "AND ($nn),Y  ", "HLT*         ", "RLA*($nn),Y  ",
    "NOP*$nn,X    ", "AND $nn,X    ", "ROL $nn,X    ", "RLA*$nn,X    ",
    "SEC          ", "AND $nnnn,Y  ", "NOP*         ", "RLA*$nnnn,Y  ",
    "NOP*$nnnn,X  ", "AND $nnnn,X  ", "ROL $nnnn,X  ", "RLA*$nnnn,X  ",

    "RTI          ", "EOR ($nn,X)  ", "HLT*         ", "LSE*($nn,X)  ",
    "NOP*$nn      ", "EOR $nn      ", "LSR $nn      ", "LSE*$nn      ",
    "PHA          ", "EOR #        ", "LSR A        ", "ALR*#        ",
    "JMP $nnnn    ", "EOR $nnnn    ", "LSR $nnnn    ", "LSE*$nnnn    ",

    "BVC rel      ", "EOR ($nn),Y  ", "HLT*         ", "LSE*($nn),Y  ",
    "NOP $nn,x    ", "EOR $nn,X    ", "LSR $nn,X    ", "LSE*$nn,X    ",
    "CLI          ", "EOR $nnnn,Y  ", "NOP*         ", "LSE*$nnnn,Y  ",
    "NOP*$nnnn,X  ", "EOR $nnnn,X  ", "LSR $nnnn,X  ", "LSE*$nnnn,X  ",

    "RTS          ", "ADC ($nn,X)  ", "HLT*         ", "RRA*($nn,X)  ",
    "NOP*$nn      ", "ADC $nn      ", "ROR $nn      ", "RRA*$nn      ",
    "PLA          ", "ADC #        ", "ROR A        ", "ARR*#        ",
    "JMP ($nnnn)  ", "ADC $nnnn    ", "ROR $nnnn    ", "RRA*$nnnn    ",

    "BVS rel      ", "ADC ($nn),Y  ", "HLT*         ", "RRA*($nn),Y  ",
    "NOP $nn,x    ", "ADC $nn,X    ", "ROR $nn,X    ", "RRA*$nn,X    ",
    "SEI          ", "ADC $nnnn,Y  ", "NOP*         ", "RRA*$nnnn,Y  ",
    "NOP*$nnnn,X  ", "ADC $nnnn,X  ", "ROR $nnnn,X  ", "RRA*$nnnn,X  ",

    "NOP*#        ", "STA ($nn,X)  ", "NOP*#        ", "SAX*($nn,X)  ",
    "STY $nn      ", "STA $nn      ", "STX $nn      ", "SAX*$nn      ",
    "DEY          ", "NOP*#        ", "TXA          ", "XAA*         ",
    "STY $nnnn    ", "STA $nnnn    ", "STX $nnnn    ", "SAX*$nnnn    ",

    "BCC          ", "STA ($nn),Y  ", "HLT*         ", "AHX*($nn),Y  ",
    "STY $nn,X    ", "STA $nn,X    ", "STX $nn,Y    ", "SAX*$nn,Y    ",
    "TYA          ", "STA $nnnn,Y  ", "TXS          ", "TAS*$nnnn,Y  ",
    "SHY*$nnnn,X  ", "STA $nnnn,X  ", "SHX*$nnnn,Y  ", "AHX*$nnnn,Y  ",

    "LDY #        ", "LDA ($nn,X)  ", "LDX #        ", "LAX*($nn,X)  ",
    "LDY $nn      ", "LDA $nn      ", "LDX $nn      ", "LAX*$nn      ",
    "TAY          ", "LDA #        ", "TAX          ", "LAX*#        ",
    "LDY $nnnn    ", "LDA $nnnn    ", "LDX $nnnn    ", "LAX*$nnnn    ",

    "BCS          ", "LDA ($nn),Y  ", "HLT*         ", "LAX*($nn),Y  ",
    "LDY $nn,X    ", "LDA $nn,X    ", "LDX $nn,Y    ", "LAX*$nn,Y    ",
    "CLV          ", "LDA $nnnn,Y  ", "TSX          ", "LAS*$nnnn,Y  ",
    "LDY $nnnn,X  ", "LDA $nnnn,X  ", "LDX $nnnn,Y  ", "LAX*$nnnn,Y  ",

    "CPY #        ", "CMP ($nn,X)  ", "NOP*#        ", "DCM*($nn,X)  ",
    "CPY $nn      ", "CMP $nn      ", "DEC $nn      ", "DCM*$nn      ",
    "INY          ", "CMP #        ", "DEX          ", "AXS*# (used!)",
    "CPY $nnnn    ", "CMP $nnnn    ", "DEC $nnnn    ", "DCM*$nnnn    ",

    "BNE          ", "CMP ($nn),Y  ", "HLT*         ", "DCM*($nn),Y  ",
    "NOP*$nn,X    ", "CMP $nn,X    ", "DEC $nn,X    ", "DCM*$nn,X    ",
    "CLD          ", "CMP $nnnn,Y  ", "NOP*         ", "DCM*$nnnn,Y  ",
    "NOP*$nnnn,X  ", "CMP $nnnn,X  ", "DEC $nnnn,X  ", "DCM*$nnnn,X  ",

    "CPX #        ", "SBC ($nn,X)  ", "NOP*#        ", "INS*($nn,X)  ",
    "CPX $nn      ", "SBC $nn      ", "INC $nn      ", "INS*$nn      ",
    "INX          ", "SBC #        ", "NOP          ", "SBC*#        ",
    "CPX $nnnn    ", "SBC $nnnn    ", "INC $nnnn    ", "INS*$nnnn    ",

    "BEQ          ", "SBC ($nn),Y  ", "HLT*         ", "INS*($nn),Y  ",
    "NOP*$nn,S    ", "SBC $nn,X    ", "INC $nn,X    ", "INS*$nn,X    ",
    "SED          ", "SBC $nnnn,Y  ", "NOP*         ", "INS*$nnnn,Y  ",
    "NOP*$nnnn,X  ", "SBC $nnnn,X  ", "INC $nnnn,X  ", "INS*$nnnn,X  "
};

static const uint16_t illegal_map[16] = {
    0x989C, 0x9C9C, 0x888C, 0x9C9C,
    0x889C, 0x9C9C, 0x889C, 0x9C9C,
    0x8A8D, 0xD88C, 0x8888, 0x888C,
    0x888C, 0x9C9C, 0x888C, 0x9C9C
};

bool is_illegal_opcode(uint8_t opcode)
{
    uint16_t row = illegal_map[opcode >> 4];
    return ((row >> (opcode & 0x0F)) & 1) != 0;
}

void canonicalize_mnemonic(uint8_t opcode, const char *templ, char *mnemonic)
{
    mnemonic[0] = templ[0];
    mnemonic[1] = templ[1];
    mnemonic[2] = templ[2];
    mnemonic[3] = 0;

    if (!strcmp(mnemonic, "HLT")) {
        strcpy(mnemonic, "JAM");
    } else if (!strcmp(mnemonic, "ASO")) {
        strcpy(mnemonic, "SLO");
    } else if (!strcmp(mnemonic, "LSE")) {
        strcpy(mnemonic, "SRE");
    } else if (!strcmp(mnemonic, "DCM")) {
        strcpy(mnemonic, "DCP");
    } else if (!strcmp(mnemonic, "INS")) {
        strcpy(mnemonic, "ISC");
    }
}

uint8_t instruction_length(const char *templ)
{
    if ((templ[0] == 'B') && (templ[1] != 'R')) {
        return 2;
    }
    if (strstr(templ, "$nnnn") || strstr(templ, "ABS,X") || strstr(templ, "nnnn")) {
        return 3;
    }
    if (strstr(templ, "$nn") || strstr(templ, "#") || strstr(templ, "rel") || strchr(templ, 'A')) {
        return 2;
    }
    return 1;
}

void trim_operand(char *operand)
{
    int len = strlen(operand);
    while (len > 0 && operand[len - 1] == ' ') {
        operand[--len] = 0;
    }
}

void format_operand(uint8_t opcode, uint16_t pc, const uint8_t *bytes, uint8_t length, const char *templ, char *operand, bool *has_target, uint16_t *target)
{
    const char *spec = templ + 4;

    *has_target = false;
    *target = 0;
    operand[0] = 0;

    while (*spec == ' ') {
        spec++;
    }
    if (templ[0] == 'B' && templ[1] != 'R') {
        *has_target = true;
        *target = (uint16_t)(pc + 2 + (int8_t)bytes[1]);
        sprintf(operand, "$%04x", *target);
        return;
    }
    if (*spec == 0) {
        return;
    }
    if (!strncmp(spec, "A", 1)) {
        strcpy(operand, "A");
        return;
    }
    if (!strncmp(spec, "rel", 3)) {
        *has_target = true;
        *target = (uint16_t)(pc + 2 + (int8_t)bytes[1]);
        sprintf(operand, "$%04x", *target);
        return;
    }
    if (!strncmp(spec, "#", 1)) {
        if (length == 2) {
            sprintf(operand, "#$%02x", bytes[1]);
        }
        return;
    }
    if (!strncmp(spec, "($nn,X)", 7)) {
        sprintf(operand, "($%02x,X)", bytes[1]);
        return;
    }
    if (!strncmp(spec, "($nn),Y", 7)) {
        sprintf(operand, "($%02x),Y", bytes[1]);
        return;
    }
    if (!strncmp(spec, "($nnnn)", 7)) {
        sprintf(operand, "($%04x)", (uint16_t)(bytes[1] | (bytes[2] << 8)));
        return;
    }
    if (!strncmp(spec, "($nnnn,X)", 9) || !strncmp(spec, "(ABS,X)", 7)) {
        sprintf(operand, "($%04x,X)", (uint16_t)(bytes[1] | (bytes[2] << 8)));
        return;
    }
    if (!strncmp(spec, "$nnnn,Y", 7)) {
        sprintf(operand, "$%04x,Y", (uint16_t)(bytes[1] | (bytes[2] << 8)));
        return;
    }
    if (!strncmp(spec, "$nnnn,X", 7)) {
        sprintf(operand, "$%04x,X", (uint16_t)(bytes[1] | (bytes[2] << 8)));
        return;
    }
    if (!strncmp(spec, "$nnnn", 5)) {
        sprintf(operand, "$%04x", (uint16_t)(bytes[1] | (bytes[2] << 8)));
        return;
    }
    if (!strncmp(spec, "$nn,Y", 5)) {
        sprintf(operand, "$%02x,Y", bytes[1]);
        return;
    }
    if (!strncmp(spec, "$nn,X", 5)) {
        sprintf(operand, "$%02x,X", bytes[1]);
        return;
    }
    if (!strncmp(spec, "$nn,S", 5)) {
        sprintf(operand, "$%02x,S", bytes[1]);
        return;
    }
    if (!strncmp(spec, "$nn", 3)) {
        sprintf(operand, "$%02x", bytes[1]);
        return;
    }
    strncpy(operand, spec, 23);
    operand[23] = 0;
    trim_operand(operand);
}

}

void disassemble_6502(uint16_t pc, const uint8_t *bytes, bool illegal_enabled, Disassembled6502 *out)
{
    uint8_t opcode = bytes[0];
    const char *templ = opcode_templates[opcode];
    char mnemonic[4];
    char operand[24];

    memset(out, 0, sizeof(Disassembled6502));
    out->illegal = is_illegal_opcode(opcode);
    out->length = instruction_length(templ);

    if (out->illegal && !illegal_enabled) {
        strcpy(out->text, "???");
        out->length = 1;
        return;
    }

    canonicalize_mnemonic(opcode, templ, mnemonic);
    format_operand(opcode, pc, bytes, out->length, templ, operand, &out->has_target, &out->target);

    if (operand[0]) {
        sprintf(out->text, "%s %s", mnemonic, operand);
    } else {
        strcpy(out->text, mnemonic);
    }
}
const char *disassembler_6502_template(uint8_t opcode)
{
    return opcode_templates[opcode];
}

bool disassembler_6502_is_illegal(uint8_t opcode)
{
    return is_illegal_opcode(opcode);
}
