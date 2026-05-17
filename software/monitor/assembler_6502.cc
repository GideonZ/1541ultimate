#include "assembler_6502.h"

extern "C" {
#include "small_printf.h"
}

#include <string.h>

namespace {

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode template -> AsmAddrMode. Mirrors the structure of opcode_templates
// in disassembler_6502.cc.
static AsmAddrMode template_to_mode(uint8_t opcode, const char *t)
{
    // Branches: B?? rel — except BRK/BIT.
    if (t[0] == 'B' && t[1] != 'R' && t[1] != 'I') {
        return AM_REL;
    }
    const char *spec = t + 4;
    while (*spec == ' ') spec++;
    if (*spec == 0) return AM_IMP;
    if (spec[0] == 'A' && (spec[1] == ' ' || spec[1] == 0)) return AM_ACC;
    if (!strncmp(spec, "rel", 3)) return AM_REL;
    if (!strncmp(spec, "#", 1)) return AM_IMM;
    if (!strncmp(spec, "($nn,X)", 7)) return AM_IZX;
    if (!strncmp(spec, "($nn),Y", 7)) return AM_IZY;
    if (!strncmp(spec, "($nnnn,X)", 9) || !strncmp(spec, "(ABS,X)", 7)) return AM_IAX;
    if (!strncmp(spec, "($nnnn)", 7)) return AM_IND;
    if (!strncmp(spec, "$nnnn,Y", 7)) return AM_ABY;
    if (!strncmp(spec, "$nnnn,X", 7)) return AM_ABX;
    if (!strncmp(spec, "$nnnn", 5)) return AM_ABS;
    if (!strncmp(spec, "$nn,Y", 5)) return AM_ZPY;
    if (!strncmp(spec, "$nn,X", 5)) return AM_ZPX;
    if (!strncmp(spec, "$nn,S", 5)) return AM_ZPS;
    if (!strncmp(spec, "$nn", 3)) return AM_ZP;
    return AM_IMP;
}

static uint8_t mode_length(AsmAddrMode m)
{
    switch (m) {
        case AM_IMP: case AM_ACC: return 1;
        case AM_IMM: case AM_ZP: case AM_ZPX: case AM_ZPY: case AM_ZPS:
        case AM_IZX: case AM_IZY: case AM_REL: return 2;
        case AM_ABS: case AM_ABX: case AM_ABY: case AM_IND: case AM_IAX: return 3;
        default: return 1;
    }
}

static void canonicalize_mnem(uint8_t opcode, const char *t, char *m)
{
    m[0] = t[0]; m[1] = t[1]; m[2] = t[2]; m[3] = 0;
    if (!strcmp(m, "HLT")) strcpy(m, "JAM");
    else if (!strcmp(m, "ASO")) strcpy(m, "SLO");
    else if (!strcmp(m, "LSE")) strcpy(m, "SRE");
    else if (!strcmp(m, "DCM")) strcpy(m, "DCP");
    else if (!strcmp(m, "INS")) strcpy(m, "ISC");
}

struct ReverseEntry {
    uint8_t opcode;
    uint8_t length;
    bool    illegal;
    bool    valid;
};

// One slot per (mnemonic-id, addressing-mode). Mnemonics deduplicated below.
// Size must be >= number of distinct mnemonics across all 256 opcodes
// (including illegal forms). Today this is ~75 entries.
#define REVERSE_MNEMONIC_MAX 96
static char       reverse_mnemonics[REVERSE_MNEMONIC_MAX][4];
static int        reverse_mnemonic_count = 0;
static ReverseEntry reverse_table[REVERSE_MNEMONIC_MAX][AM_COUNT];
static bool       reverse_built = false;

static int find_or_add_mnem(const char *m)
{
    for (int i = 0; i < reverse_mnemonic_count; i++) {
        if (!strcmp(reverse_mnemonics[i], m)) return i;
    }
    if (reverse_mnemonic_count >= REVERSE_MNEMONIC_MAX) return -1;
    int i = reverse_mnemonic_count++;
    strcpy(reverse_mnemonics[i], m);
    return i;
}

static void build_reverse(void)
{
    if (reverse_built) return;
    memset(reverse_table, 0, sizeof(reverse_table));
    reverse_mnemonic_count = 0;
    for (int op = 0; op < 256; op++) {
        const char *t = disassembler_6502_template((uint8_t)op);
        char m[4];
        canonicalize_mnem((uint8_t)op, t, m);
        AsmAddrMode mode = template_to_mode((uint8_t)op, t);
        int idx = find_or_add_mnem(m);
        if (idx < 0) continue;
        ReverseEntry &e = reverse_table[idx][mode];
        // Prefer non-illegal if a duplicate exists.
        bool ill = disassembler_6502_is_illegal((uint8_t)op);
        if (!e.valid || (e.illegal && !ill)) {
            e.valid = true;
            e.opcode = (uint8_t)op;
            e.length = mode_length(mode);
            e.illegal = ill;
        }
    }
    reverse_built = true;
}

static int find_mnem(const char *m)
{
    for (int i = 0; i < reverse_mnemonic_count; i++) {
        if (!strcmp(reverse_mnemonics[i], m)) return i;
    }
    return -1;
}

static uint8_t candidate_mode_rank(AsmAddrMode mode)
{
    switch (mode) {
        case AM_IMP: return 0;
        case AM_ACC: return 1;
        case AM_IMM: return 2;
        case AM_ZP:  return 3;
        case AM_ZPX: return 4;
        case AM_ZPY: return 5;
        case AM_ZPS: return 6;
        case AM_IZX: return 7;
        case AM_IZY: return 8;
        case AM_ABS: return 9;
        case AM_ABX: return 10;
        case AM_ABY: return 11;
        case AM_IND: return 12;
        case AM_IAX: return 13;
        case AM_REL: return 14;
        default:     return 15;
    }
}

static void skip_ws(const char *&p) { while (*p == ' ' || *p == '\t') p++; }

// Parse a hex value of 1..4 nibbles. *digits returns nibble count.
static bool parse_hex_n(const char *&p, uint16_t *value, int *digits)
{
    int n = 0;
    uint32_t v = 0;
    while (is_hex(*p) && n < 4) {
        v = (v << 4) | (uint32_t)hex_val(*p++);
        n++;
    }
    *value = (uint16_t)v;
    *digits = n;
    return n > 0;
}

// Parse operand starting at p, return mode + value. value is the raw 8/16-bit
// operand prior to PC-relative adjustment.
static bool parse_operand(const char *p, AsmAddrMode *mode, uint16_t *value)
{
    skip_ws(p);
    if (*p == 0) { *mode = AM_IMP; *value = 0; return true; }
    if ((*p == 'A' || *p == 'a') && (p[1] == 0 || p[1] == ' ' || p[1] == '\t')) {
        const char *q = p + 1; skip_ws(q);
        if (*q == 0) { *mode = AM_ACC; *value = 0; return true; }
    }
    if (*p == '#') {
        p++; if (*p == '$') p++;
        int d; if (!parse_hex_n(p, value, &d)) return false;
        if (d > 2) return false;
        skip_ws(p); if (*p) return false;
        *mode = AM_IMM; return true;
    }
    if (*p == '(') {
        p++; if (*p == '$') p++;
        int d; uint16_t v; if (!parse_hex_n(p, &v, &d)) return false;
        if (*p == ',') {
            p++; skip_ws(p);
            if (*p != 'X' && *p != 'x') return false;
            p++; skip_ws(p);
            if (*p != ')') return false;
            p++; skip_ws(p); if (*p) return false;
            *value = v;
            *mode = (d <= 2) ? AM_IZX : AM_IAX;
            return true;
        }
        if (*p != ')') return false;
        p++; skip_ws(p);
        if (*p == ',') {
            p++; skip_ws(p);
            if (*p != 'Y' && *p != 'y') return false;
            p++; skip_ws(p); if (*p) return false;
            if (d > 2) return false;
            *value = v; *mode = AM_IZY; return true;
        }
        if (*p) return false;
        *value = v; *mode = AM_IND; return true;
    }
    if (*p == '$') p++;
    int d; uint16_t v; if (!parse_hex_n(p, &v, &d)) return false;
    skip_ws(p);
    if (*p == ',') {
        p++; skip_ws(p);
        char idx = up(*p);
        if (idx != 'X' && idx != 'Y' && idx != 'S') return false;
        p++; skip_ws(p); if (*p) return false;
        *value = v;
        if (idx == 'S') { *mode = AM_ZPS; return d <= 2; }
        if (idx == 'X') { *mode = (d <= 2) ? AM_ZPX : AM_ABX; return true; }
        *mode = (d <= 2) ? AM_ZPY : AM_ABY; return true;
    }
    if (*p) return false;
    *value = v;
    *mode = (d <= 2) ? AM_ZP : AM_ABS;
    return true;
}

}  // namespace

int monitor_collect_opcode_candidates(const char *prefix, bool illegal_enabled,
                                      uint8_t *opcode_out, int max_candidates)
{
    struct Candidate {
        uint8_t opcode;
        char mnemonic[4];
        uint8_t mode_rank;
    };

    if (!opcode_out || max_candidates <= 0) {
        return 0;
    }

    build_reverse();

    char want[4];
    int prefix_len = 0;
    while (prefix && prefix[prefix_len] && prefix_len < 3) {
        want[prefix_len] = up(prefix[prefix_len]);
        prefix_len++;
    }
    want[prefix_len] = 0;

    Candidate candidates[256];
    int candidate_count = 0;

    for (int mnemonic_idx = 0; mnemonic_idx < reverse_mnemonic_count; mnemonic_idx++) {
        const char *mnemonic = reverse_mnemonics[mnemonic_idx];
        bool match = true;
        for (int i = 0; i < prefix_len; i++) {
            if (mnemonic[i] != want[i]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }

        for (int mode = 0; mode < AM_COUNT; mode++) {
            const ReverseEntry &entry = reverse_table[mnemonic_idx][mode];
            if (!entry.valid) {
                continue;
            }
            if (entry.illegal && !illegal_enabled) {
                continue;
            }

            Candidate candidate;
            candidate.opcode = entry.opcode;
            candidate.mnemonic[0] = mnemonic[0];
            candidate.mnemonic[1] = mnemonic[1];
            candidate.mnemonic[2] = mnemonic[2];
            candidate.mnemonic[3] = 0;
            candidate.mode_rank = candidate_mode_rank((AsmAddrMode)mode);

            int insert_at = candidate_count;
            while (insert_at > 0) {
                const Candidate &prev = candidates[insert_at - 1];
                int mnemonic_cmp = strcmp(candidate.mnemonic, prev.mnemonic);
                if (mnemonic_cmp > 0) {
                    break;
                }
                if (mnemonic_cmp == 0 && candidate.mode_rank > prev.mode_rank) {
                    break;
                }
                if (mnemonic_cmp == 0 && candidate.mode_rank == prev.mode_rank &&
                    candidate.opcode >= prev.opcode) {
                    break;
                }
                candidates[insert_at] = candidates[insert_at - 1];
                insert_at--;
            }
            candidates[insert_at] = candidate;
            candidate_count++;
        }
    }

    if (candidate_count > max_candidates) {
        candidate_count = max_candidates;
    }
    for (int i = 0; i < candidate_count; i++) {
        opcode_out[i] = candidates[i].opcode;
    }
    return candidate_count;
}

bool monitor_lookup_opcode(const char *mnemonic, AsmAddrMode mode,
                           bool illegal_enabled, uint8_t *opcode, uint8_t *length)
{
    build_reverse();
    char m[4];
    int n = 0;
    while (mnemonic[n] && n < 3) { m[n] = up(mnemonic[n]); n++; }
    m[n] = 0;
    if (n != 3) return false;
    int idx = find_mnem(m);
    if (idx < 0) return false;
    const ReverseEntry &e = reverse_table[idx][mode];
    if (!e.valid) return false;
    if (e.illegal && !illegal_enabled) return false;
    *opcode = e.opcode;
    *length = e.length;
    return true;
}

bool monitor_assemble_line(const char *text, bool illegal_enabled, uint16_t pc,
                           AsmInsn *out, MonitorError *err)
{
    build_reverse();
    if (err) *err = MONITOR_OK;
    memset(out, 0, sizeof(*out));

    const char *p = text;
    skip_ws(p);
    if (!*p) { if (err) *err = MONITOR_SYNTAX; return false; }

    char m[4]; int n = 0;
    while (*p && *p != ' ' && *p != '\t' && n < 3) { m[n++] = up(*p++); }
    m[n] = 0;
    if (n != 3) { if (err) *err = MONITOR_SYNTAX; return false; }

    AsmAddrMode mode; uint16_t value = 0;
    if (!parse_operand(p, &mode, &value)) { if (err) *err = MONITOR_SYNTAX; return false; }

    // Try direct match first.
    uint8_t opcode = 0, length = 0;
    bool ok = monitor_lookup_opcode(m, mode, illegal_enabled, &opcode, &length);

    // Branch promotion: ABS-form branches resolve to REL.
    if (!ok && (mode == AM_ABS || mode == AM_ZP)) {
        if (monitor_lookup_opcode(m, AM_REL, illegal_enabled, &opcode, &length)) {
            int32_t delta = (int32_t)value - (int32_t)((uint16_t)(pc + 2));
            if (delta < -128 || delta > 127) { if (err) *err = MONITOR_RANGE; return false; }
            out->bytes[0] = opcode;
            out->bytes[1] = (uint8_t)(delta & 0xFF);
            out->length = 2;
            out->illegal = false;
            return true;
        }
        // ZP can promote to ABS.
        if (mode == AM_ZP) {
            if (monitor_lookup_opcode(m, AM_ABS, illegal_enabled, &opcode, &length)) {
                ok = true; mode = AM_ABS;
            }
        }
    }
    if (!ok) {
        if (mode == AM_ZPX && monitor_lookup_opcode(m, AM_ABX, illegal_enabled, &opcode, &length)) { ok = true; mode = AM_ABX; }
        else if (mode == AM_ZPY && monitor_lookup_opcode(m, AM_ABY, illegal_enabled, &opcode, &length)) { ok = true; mode = AM_ABY; }
    }
    if (!ok) { if (err) *err = MONITOR_SYNTAX; return false; }

    out->bytes[0] = opcode;
    out->length = length;
    out->illegal = disassembler_6502_is_illegal(opcode);
    if (length == 2) {
        if (mode == AM_REL) {
            int32_t delta = (int32_t)value - (int32_t)((uint16_t)(pc + 2));
            if (delta < -128 || delta > 127) { if (err) *err = MONITOR_RANGE; return false; }
            out->bytes[1] = (uint8_t)(delta & 0xFF);
        } else {
            if (value > 0xFF) { if (err) *err = MONITOR_VALUE; return false; }
            out->bytes[1] = (uint8_t)value;
        }
    } else if (length == 3) {
        out->bytes[1] = (uint8_t)(value & 0xFF);
        out->bytes[2] = (uint8_t)((value >> 8) & 0xFF);
    }
    return true;
}

void monitor_assembler_reset_caches(void)
{
    reverse_built = false;
    reverse_mnemonic_count = 0;
}
