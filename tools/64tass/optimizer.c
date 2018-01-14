/*
    $Id: optimizer.c 1493 2017-04-28 08:38:45Z soci $

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include "optimizer.h"
#include "stdbool.h"
#include "error.h"
#include "section.h"
#include "opcodes.h"
#include "opt_bit.h"

typedef struct Bit Bit;

typedef struct Reg8 {
    Bit *a[8];
} Reg8;

struct optimizer_s {
    bool branched;
    bool call;
    uint16_t lb;
    unsigned int pc;
    Reg8 a, x, y, z, s, sh, b;
    struct {
        Bit *n, *v, *e, *d, *i, *z, *c;
    } p;
    bool zcmp, ccmp;
    Bit *cc;
    Reg8 z1, z2, z3;
    unsigned int sir, sac;
};

static void set_bit(Bit **v, Bit *b) {
    del_bit(*v);
    *v = b;
}

static void change_bit(Bit **v, Bit *b) {
    if (*v == b) return;
    del_bit(*v);
    *v = ref_bit(b);
}

static const struct cpu_s *cputype;
static bool cputype_65c02, cputype_65ce02;

void cpu_opt_set_cpumode(const struct cpu_s *cpu) {
    cputype = cpu;
    cputype_65c02 = (cpu == &c65c02 || cpu == &r65c02 || cpu == &w65c02);
    cputype_65ce02 = (cpu == &c65ce02 || cpu == &c4510);
}

void cpu_opt_long_branch(uint16_t cod) {
    struct optimizer_s *cpu = current_section->optimizer;
    if (cpu == NULL) {
        cpu_opt_invalidate();
        cpu = current_section->optimizer;
    }
    cpu->lb = cod;
}

static void del_reg(Reg8 *r) {
    unsigned int i;
    for (i = 0; i < 8; i++) del_bit(r->a[i]);
}

static bool is_zero(const Reg8 *r) {
    unsigned int i;
    for (i = 0; i < 8; i++) if (get_bit(r->a[i]) != B0) return false;
    return true;
}

static bool is_known(const Reg8 *r) {
    unsigned int i;
    for (i = 0; i < 8; i++) if (get_bit(r->a[i]) == BU) return false;
    return true;
}

static Bit_types flag_c(struct optimizer_s *cpu) {
    unsigned int i;
    Bit *co;
    Bit_types b = get_bit(cpu->p.c);
    if (b != BU || !cpu->ccmp) return b;
    co = new_bit(B1);
    for (i = 0; i < 8; i++) {
        Bit *c, *tmp, *r = inv_bit(cpu->z3.a[i]);
        tmp = add_bit(cpu->z2.a[i], r, co, &c);
        set_bit(&co, c);
        del_bit(r);
        del_bit(tmp);
    }
    b = get_bit(co);
    if (b != BU) mod_bit(cpu->cc, b);
    del_bit(co);
    return b;
}

static bool flag_is_zero(struct optimizer_s *cpu) {
    unsigned int i;
    switch (get_bit(cpu->p.z)) {
    case B0: return false;
    case B1: return true;
    default: break;
    }
    if (cpu->zcmp) {
        for (i = 0; i < 8; i++) if (!eq_bit(cpu->z2.a[i], cpu->z3.a[i])) return false;
    } else {
        for (i = 0; i < 8; i++) if (get_bit(cpu->z1.a[i]) != B0) return false;
    }
    set_bit(&cpu->p.z, new_bit1());
    return true;
}

static bool flag_is_nonzero(struct optimizer_s *cpu) {
    unsigned int i;
    switch (get_bit(cpu->p.z)) {
    case B0: return true;
    case B1: return false;
    default: break;
    }
    if (cpu->zcmp) {
        for (i = 0; i < 8; i++) if (neq_bit(cpu->z2.a[i], cpu->z3.a[i])) {
            set_bit(&cpu->p.z, new_bit0());
            return true;
        }
    } else {
        for (i = 0; i < 8; i++) if (get_bit(cpu->z1.a[i]) == B1) {
            set_bit(&cpu->p.z, new_bit0());
            return true;
        }
    }
    return false;
}

static bool calc_z(struct optimizer_s *cpu, Reg8 *r) {
    unsigned int i;
    bool eq = true, neq = false, ret = true;
    Bit *b;
    for (i = 0; i < 8; i++) {
        Bit_types v = get_bit(r->a[i]);
        if (ret && !eq_bit(r->a[i], cpu->z1.a[i])) ret = false;
        change_bit(&cpu->z1.a[i], r->a[i]);
        if (eq && v != B0) eq = false;
        if (!neq && v == B1) neq = true;
    }
    if (eq || neq) {
        b = new_bit(eq ? B1 : B0);
        ret = eq_bit(cpu->p.z, b);
    } else {
        b = new_bitu();
        cpu->zcmp = false;
    }
    set_bit(&cpu->p.z, b);
    return ret;
}

static bool calc_nz(struct optimizer_s *cpu, Reg8 *r) {
    bool ret = eq_bit(cpu->p.n, r->a[7]);
    change_bit(&cpu->p.n, r->a[7]);
    return calc_z(cpu, r) && ret;
}

static bool transreg(Reg8 *b, Reg8 *r) {
    bool ret = true;
    unsigned int i;
    for (i = 0; i < 8; i++) {
        if (ret && !eq_bit(r->a[i], b->a[i])) ret = false;
        change_bit(&r->a[i], b->a[i]);
    }
    return ret;
}

static bool set_flag(Bit_types v, Bit **b) {
    if (get_bit(*b) == v) return true;
    set_bit(b, new_bit(v));
    return false;
}

static bool transreg2(struct optimizer_s *cpu, Reg8 *b, Reg8 *r) {
    bool ret = transreg(b, r);
    return calc_nz(cpu, r) && ret;
}

static bool shl(struct optimizer_s *cpu, Reg8 *r, Bit *b) {
    unsigned int i;
    bool ret = eq_bit(cpu->p.c, r->a[7]);
    change_bit(&cpu->p.c, r->a[7]);
    for (i = 7; i != 0; i--) {
        if (ret && !eq_bit(r->a[i], r->a[i - 1])) ret = false;
        change_bit(&r->a[i], r->a[i - 1]);
    }
    ret = ret && eq_bit(r->a[0], b);
    set_bit(&r->a[0], b);
    cpu->ccmp = false;
    return calc_nz(cpu, r) && ret;
}

static bool shr(struct optimizer_s *cpu, Reg8 *r, Bit *b) {
    unsigned int i;
    bool ret = eq_bit(cpu->p.c, r->a[0]);
    change_bit(&cpu->p.c, r->a[0]);
    for (i = 0; i < 7; i++) {
        if (ret && !eq_bit(r->a[i], r->a[i + 1])) ret = false;
        change_bit(&r->a[i], r->a[i + 1]);
    }
    ret = ret && eq_bit(r->a[7], b);
    set_bit(&r->a[7], b);
    cpu->ccmp = false;
    return calc_nz(cpu, r) && ret;
}

static bool rol(struct optimizer_s *cpu, Reg8 *r) {
    return shl(cpu, r, ref_bit(cpu->p.c));
}

static bool asl(struct optimizer_s *cpu, Reg8 *r) {
    return shl(cpu, r, new_bit(B0));
}

static bool ror(struct optimizer_s *cpu, Reg8 *r) {
    return shr(cpu, r, ref_bit(cpu->p.c));
}

static bool lsr(struct optimizer_s *cpu, Reg8 *r) {
    return shr(cpu, r, new_bit(B0));
}

static bool asr(struct optimizer_s *cpu, Reg8 *r) {
    return shr(cpu, r, ref_bit(r->a[7]));
}

static bool neg(struct optimizer_s *cpu, Reg8 *b) {
    unsigned int i;
    bool ret = true;
    Bit *co = new_bit1(), *n = new_bit0();
    for (i = 0; i < 8; i++) {
        Bit *bb = inv_bit(b->a[i]);
        Bit *c, *rr = add_bit(n, bb, co, &c);
        if (ret && !eq_bit(b->a[i], rr)) ret = false;
        set_bit(&b->a[i], rr);
        set_bit(&co, c);
        del_bit(bb);
    }
    del_bit(co);
    del_bit(n);
    return calc_nz(cpu, b) && ret;
}

static bool asri(struct optimizer_s *cpu, Reg8 *r, Reg8 *v) {
    unsigned int i;
    Bit *b = and_bit(r->a[0], v->a[0]);
    bool ret = eq_bit(cpu->p.c, b);
    set_bit(&cpu->p.c, b);
    del_bit(v->a[0]);
    for (i = 0; i < 7; i++) {
        b = and_bit(r->a[i + 1], v->a[i + 1]);
        if (ret && !eq_bit(r->a[i], b)) ret = false;
        set_bit(&r->a[i], b);
        del_bit(v->a[i + 1]);
    }
    b = new_bit0();
    ret = ret && eq_bit(r->a[7], b);
    set_bit(&r->a[7], b);
    cpu->ccmp = false;
    return calc_nz(cpu, r) && ret;
}

static void shs(Reg8 *r1, Reg8 *r2, Reg8 *v) {
    unsigned int i;
    for (i = 0; i < 8; i++) {
        set_bit(&v->a[i], and_bit(r1->a[i], r2->a[i]));
    }
}

static void incdec(struct optimizer_s *cpu, Reg8 *r, bool inc) {
    unsigned int i;
    Bit *co = new_bit(inc ? B1 : B0), *n = new_bit(inc ? B0 : B1);
    for (i = 0; i < 8; i++) {
        Bit *c, *rr = add_bit(n, r->a[i], co, &c);
        set_bit(&r->a[i], rr);
        set_bit(&co, c);
    }
    del_bit(co);
    del_bit(n);
    calc_nz(cpu, r);
}

static bool cmp(struct optimizer_s *cpu, Reg8 *s, Reg8 *v, const char **cc) {
    unsigned int i;
    Reg8 tmp;
    bool ret2 = true, ret, ret3;
    Bit *co = new_bit(B1);
    Bit_types b;

    for (i = 0; i < 8; i++) {
        Bit *c, *r = inv_bit(v->a[i]);
        tmp.a[i] = add_bit(s->a[i], r, co, &c);
        set_bit(&co, c);
        del_bit(r);
        if (ret2 && !eq_bit(cpu->z2.a[i], s->a[i])) ret2 = false;
        change_bit(&cpu->z2.a[i], s->a[i]);
        if (ret2 && !eq_bit(cpu->z3.a[i], v->a[i])) ret2 = false;
        set_bit(&cpu->z3.a[i], v->a[i]);
    }
    if (ret2 && cpu->ccmp) change_bit(&co, cpu->cc);
    b = get_bit(co);
    ret3 = eq_bit(cpu->p.c, co);
    set_bit(&cpu->p.c, co);
    change_bit(&cpu->cc, co);
    ret = calc_nz(cpu, (ret2 && cpu->zcmp) ? &cpu->z1 : &tmp);
    cpu->zcmp = true;
    cpu->ccmp = true;
    del_reg(&tmp);
    *cc = (!ret3 && ret && b != BU) ? (b == B0 ? "clc" : "sec") : NULL;
    return ret && ret3;
}

static bool adcsbc(struct optimizer_s *cpu, Reg8 *r, Reg8 *v, bool inv) {
    unsigned int i;
    bool ret;
    Bit *co, *o, *vv;

    if (get_bit(cpu->p.d) != B0) {
        reset_reg8(r->a);
        reset_reg8(cpu->z1.a);
        reset_bit(&cpu->p.n);
        reset_bit(&cpu->p.v);
        reset_bit(&cpu->p.z);
        reset_bit(&cpu->p.c);
        cpu->zcmp = false;
        cpu->ccmp = false;
        return false;
    }

    ret = true;
    co = ref_bit(cpu->p.c); o = ref_bit(r->a[7]);
    if (inv) {
        for (i = 0; i < 8; i++) {
            Bit *c, *r2 = inv_bit(r->a[i]);
            Bit *e = add_bit(v->a[i], r2, co, &c);
            set_bit(&co, c);
            del_bit(r2);
            if (ret && !eq_bit(e, v->a[i])) ret = false;
            set_bit(&r->a[i], e);
        }
    } else {
        for (i = 0; i < 8; i++) {
            Bit *c;
            Bit *e = add_bit(v->a[i], r->a[i], co, &c);
            set_bit(&co, c);
            if (ret && !eq_bit(e, v->a[i])) ret = false;
            set_bit(&r->a[i], e);
        }
    }
    vv = v_bit(o, v->a[7], r->a[7]);
    del_bit(o);
    ret = eq_bit(cpu->p.v, vv) && ret;
    set_bit(&cpu->p.v, vv);
    ret = eq_bit(cpu->p.c, co) && ret;
    set_bit(&cpu->p.c, co);
    ret = calc_nz(cpu, r) && ret;
    cpu->zcmp = false;
    cpu->ccmp = false;
    return ret;
}

static bool bincalc_reg(struct optimizer_s *cpu, Bit *cb(Bit *, Bit *), Reg8 *r, Reg8 *v, bool *r2) {
    bool ret = true, ret2 = true;
    unsigned int i;
    for (i = 0; i < 8; i++) {
        Bit *e = cb(r->a[i], v->a[i]);
        if (ret && !eq_bit(e, v->a[i])) ret = false;
        if (ret2 && !eq_bit(e, r->a[i])) ret2 = false;
        set_bit(&r->a[i], e);
    }
    *r2 = ret2;
    return calc_nz(cpu, r) && ret;
}

static bool ld_reg(struct optimizer_s *cpu, Reg8 *r, Reg8 *v) {
    bool ret = true;
    unsigned int i;
    for (i = 0; i < 8 && ret; i++) {
        if (ret && !eq_bit(r->a[i], v->a[i])) ret = false;
    }
    return calc_nz(cpu, r) && ret;
}

static void change_reg(Reg8 *a, Reg8 *b) {
    unsigned int i;
    for (i = 0; i < 8; i++) change_bit(&b->a[i], a->a[i]);
}

static void set_reg(Reg8 *a, Reg8 *b) {
    unsigned int i;
    for (i = 0; i < 8; i++) set_bit(&b->a[i], a->a[i]);
}

static bool eq_reg(const Reg8 *a, const Reg8 *b) {
    unsigned int i;
    for (i = 0; i < 8; i++) if (!eq_bit(a->a[i], b->a[i])) return false;
    return true;
}

static bool incdec_eq_reg(const Reg8 *a, const Reg8 *b, bool inc) {
    unsigned int i;
    bool ret = true;
    Bit *co = new_bit(inc ? B1 : B0), *n = new_bit(inc ? B0 : B1);
    for (i = 0; i < 8 && ret; i++) {
        Bit *c, *rr = add_bit(n, b->a[i], co, &c);
        ret = eq_bit(a->a[i], rr);
        del_bit(rr);
        set_bit(&co, c);
    }
    del_bit(co);
    del_bit(n);
    return ret;
}

static bool neg_eq_reg(const Reg8 *a, const Reg8 *b) {
    unsigned int i;
    bool ret = true;
    Bit *co = new_bit1(), *n = new_bit0();
    for (i = 0; i < 8 && ret; i++) {
        Bit *bb = inv_bit(b->a[i]);
        Bit *c, *rr = add_bit(n, bb, co, &c);
        ret = eq_bit(a->a[i], rr);
        del_bit(rr);
        set_bit(&co, c);
        del_bit(bb);
    }
    del_bit(co);
    del_bit(n);
    return ret;
}

static bool shl_eq_reg(const Reg8 *a, const Reg8 *b) {
    unsigned int i;
    for (i = 1; i < 8; i++) if (!eq_bit(a->a[i], b->a[i-1])) return false;
    return true;
}

static bool shr_eq_reg(const Reg8 *a, const Reg8 *b) {
    unsigned int i;
    for (i = 1; i < 8; i++) if (!eq_bit(a->a[i-1], b->a[i])) return false;
    return true;
}

static const char *try_a(struct optimizer_s *cpu, Reg8 *v) {
    if (eq_reg(v, &cpu->x)) return "txa"; /* 0x8A TXA */
    if (eq_reg(v, &cpu->y)) return "tya"; /* 0x98 TYA */
    if (cputype_65ce02) {
        if (eq_reg(v, &cpu->z)) return "tza"; /* 0x6B TZA */
        if (eq_reg(v, &cpu->b)) return "tba"; /* 0x7B TBA */
    }
    if (eq_bit(cpu->p.c, cpu->a.a[7])) {
        bool b = (get_bit(v->a[0]) == B0);
        if (b || eq_bit(cpu->p.c, v->a[0])) {
            if (shl_eq_reg(v, &cpu->a)) return b ? "asl a" : "rol a"; /* 0x0A ASL A */ /* 0x2A ROL A */
        }
    }
    if (eq_bit(cpu->p.c, cpu->a.a[0])) {
        bool b = (get_bit(v->a[7]) == B0);
        if (b || eq_bit(cpu->p.c, v->a[7])) {
            if (shr_eq_reg(v, &cpu->a)) return b ? "lsr a" : "ror a"; /* 0x4A LSR A */ /* 0x6A ROR A */
        }
        if (cputype_65ce02 && eq_bit(v->a[7], cpu->a.a[7])) {
            if (shr_eq_reg(v, &cpu->a)) return "asr a"; /* 0x43 ASR A */
        }
    }
    if (cputype_65c02 || cputype_65ce02) {
        if (incdec_eq_reg(v, &cpu->a, true)) return "inc a"; /* 0x1A INC A */
        if (incdec_eq_reg(v, &cpu->a, false)) return "dec a"; /* 0x3A DEC A */
    }
    if (cputype_65ce02) {
        if (neg_eq_reg(v, &cpu->a)) return "neg a"; /* 0x42 NEG A */
    }
    return NULL;
}

static const char *try_x(struct optimizer_s *cpu, Reg8 *v) {
    if (eq_reg(v, &cpu->a)) return "tax"; /* 0xAA TAX */
    if (eq_reg(v, &cpu->s)) return "tsx"; /* 0xBA TSX */
    if (incdec_eq_reg(v, &cpu->x, true)) return "inx"; /* 0xE8 INX */
    if (incdec_eq_reg(v, &cpu->x, false)) return "dex"; /* 0xCA DEX */
    return NULL;
}

static const char *try_y(struct optimizer_s *cpu, Reg8 *v) {
    if (eq_reg(v, &cpu->a)) return "tay"; /* 0xA8 TAY */
    if (cputype_65ce02) {
        if (eq_reg(v, &cpu->sh)) return "tsy"; /* 0x0B TSY */
    }
    if (incdec_eq_reg(v, &cpu->y, true)) return "iny"; /* 0xC8 INY */
    if (incdec_eq_reg(v, &cpu->y, false)) return "dey"; /* 0x88 DEY */
    return NULL;
}

static const char *try_z(struct optimizer_s *cpu, Reg8 *v) {
    if (eq_reg(v, &cpu->a)) return "taz"; /* 0x4B TAZ */
    if (incdec_eq_reg(v, &cpu->z, true)) return "inz"; /* 0x1B INZ */
    if (incdec_eq_reg(v, &cpu->z, false)) return "dez"; /* 0x3B DEZ */
    return NULL;
}

static bool sbx(struct optimizer_s *cpu, Reg8 *s1, Reg8 *s2, Reg8 *v, const char **cc) {
    unsigned int i;
    bool ret2 = true, ret = true, ret3;
    Bit *co = new_bit(B1);
    Bit_types b;

    for (i = 0; i < 8; i++) {
        Bit *s = and_bit(s1->a[i], s2->a[i]);
        Bit *c, *r = inv_bit(v->a[i]);
        Bit *a = add_bit(s, r, co, &c);
        set_bit(&co, c);
        del_bit(r);
        if (ret2 && !eq_bit(cpu->z2.a[i], s)) ret2 = false;
        set_bit(&cpu->z2.a[i], s);
        if (ret2 && !eq_bit(cpu->z3.a[i], v->a[i])) ret2 = false;
        change_bit(&cpu->z3.a[i], v->a[i]);
        if (ret && !eq_bit(s1->a[i], a)) ret = false;
        set_bit(&v->a[i], a);
    }
    if (ret2 && cpu->ccmp) change_bit(&co, cpu->cc);
    b = get_bit(co);
    ret3 = eq_bit(cpu->p.c, co);
    set_bit(&cpu->p.c, co);
    change_bit(&cpu->cc, co);
    ret = calc_nz(cpu, v) && ret;
    cpu->zcmp = false;
    cpu->ccmp = false;
    if (ret3) {
        *cc = try_x(cpu, v);
    } else if (ret) {
        switch (b) {
        case B0: *cc = "clc"; break;
        case B1: *cc = "sec"; break;
        default: *cc = NULL;
        }
    } else *cc = NULL;
    return ret && ret3;
}

static bool bit_reg2(struct optimizer_s *cpu, Reg8 *r, Reg8 *v) {
    bool ret;
    unsigned int i;
    Reg8 tmp;
    for (i = 0; i < 8; i++) {
        Bit *e = r->a[i];
        tmp.a[i] = and_bit(e, v->a[i]);
        del_bit(e);
    }
    ret = calc_z(cpu, &tmp);
    del_reg(&tmp);
    return ret;
}

static bool bit_reg(struct optimizer_s *cpu, Reg8 *r, Reg8 *v) {
    bool ret = eq_bit(cpu->p.n, r->a[7]) && eq_bit(cpu->p.v, r->a[6]);
    change_bit(&cpu->p.n, r->a[7]);
    change_bit(&cpu->p.v, r->a[6]);
    return bit_reg2(cpu, r, v) && ret;
}

static void load_unk(Reg8 *r) {
    unsigned int i;
    for (i = 0; i < 8; i++) r->a[i] = new_bitu();
}

static void load_mem(Reg8 *r) {
    unsigned int i;
    for (i = 0; i < 8; i++) r->a[i] = new_bitu();
}

static void load_imm(uint32_t v, Reg8 *r) {
    unsigned int i;
    for (i = 0; i < 8; i++, v >>= 1) r->a[i] = ((v & 1) == 1) ? new_bit1() : new_bit0();
}

void cpu_opt(uint8_t cod, uint32_t adr, int ln, linepos_t epoint) {
    struct optimizer_s *cpu = current_section->optimizer;
    const char *optname;
    Reg8 alu;
    Bit_types b;
    bool altmode = false, altlda;

    if (cpu == NULL) {
        cpu_opt_invalidate();
        cpu = current_section->optimizer;
    }

    if (cpu->branched || cpu->pc != current_section->l_address.address) {
        cpu_opt_invalidate();
        cpu->pc = current_section->l_address.address & 0xffff;
    }
    cpu->pc = ((int)cpu->pc + ln + 1) & 0xffff;

    if (cpu->call) {
        if (cod == 0x60) err_msg2(ERROR_____REDUNDANT, "if last 'jsr' is changed to 'jmp'", epoint);
        cpu->call = false;
    }

    if (cpu->lb != 0) {
        cod = cpu->lb & 0xff;
    }

    if (cputype == &w65816 || cputype == &c65el02) return; /* unsupported for now */

    switch (cod) {
    case 0x69: /* ADC #$12 */
        load_imm(adr, &alu);
        if (adcsbc(cpu, &alu, &cpu->a, false)) {
            del_reg(&alu);
            goto remove;
        }
        set_reg(&alu, &cpu->a);
        break;
    case 0x71: /* ADC ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto adc;
    case 0x79: /* ADC $1234,y */
        altmode = is_known(&cpu->y);
        goto adc;
    case 0x61: /* ADC ($12,x) */
        if (!cputype_65c02) goto adc;
        /* fall through */
    case 0x7D: /* ADC $1234,x */
    case 0x75: /* ADC $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x6D: /* ADC $1234 */
    case 0x65: /* ADC $12 */
        adc:
        load_mem(&alu);
        adcsbc(cpu, &alu, &cpu->a, false);
        set_reg(&alu, &cpu->a);
        if (altmode) goto constind;
        break;
    case 0xE9: /* SBC #$12 */
        load_imm(adr, &alu);
        if (adcsbc(cpu, &alu, &cpu->a, true)) {
            del_reg(&alu);
            goto remove;
        }
        set_reg(&alu, &cpu->a);
        break;
    case 0xF1: /* SBC ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto sbc;
    case 0xF9: /* SBC $1234,y */
        altmode = is_known(&cpu->y);
        goto sbc;
    case 0xE1: /* SBC ($12,x) */
        if (!cputype_65c02) goto sbc;
        /* fall through */
    case 0xFD: /* SBC $1234,x */
    case 0xF5: /* SBC $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0xED: /* SBC $1234 */
    case 0xE5: /* SBC $12 */
        sbc:
        load_mem(&alu);
        adcsbc(cpu, &alu, &cpu->a, true);
        set_reg(&alu, &cpu->a);
        if (altmode) goto constind;
        break;
    case 0x29: /* AND #$12 */
        load_imm(adr, &alu);
        if (bincalc_reg(cpu, and_bit, &alu, &cpu->a, &altlda)) {
            del_reg(&alu);
            goto remove;
        }
        optname = try_a(cpu, &alu);
        set_reg(&alu, &cpu->a);
        if (optname != NULL) goto replace;
        if (is_known(&alu)) goto constresult;
        if (altlda) goto indresult;
        break;
    case 0x31: /* AND ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto anda;
    case 0x39: /* AND $1234,y */
        altmode = is_known(&cpu->y);
        goto anda;
    case 0x21: /* AND ($12,x) */
        if (!cputype_65c02) goto anda;
        /* fall through */
    case 0x3D: /* AND $1234,x */
    case 0x35: /* AND $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x2D: /* AND $1234 */
    case 0x25: /* AND $12 */
    anda:
        load_mem(&alu);
    anda2:
        bincalc_reg(cpu, and_bit, &alu, &cpu->a, &altlda);
        set_reg(&alu, &cpu->a);
        if (altmode) goto constind;
        if (altlda) goto indresult;
        break;
    case 0x09: /* ORA #$12 */
        load_imm(adr, &alu);
        if (bincalc_reg(cpu, or_bit, &alu, &cpu->a, &altlda)) {
            del_reg(&alu);
            goto remove;
        }
        optname = try_a(cpu, &alu);
        set_reg(&alu, &cpu->a);
        if (optname != NULL) goto replace;
        if (is_known(&alu)) goto constresult;
        if (altlda) goto indresult;
        break;
    case 0x11: /* ORA ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto ora;
    case 0x19: /* ORA $1234,y */
        altmode = is_known(&cpu->y);
        goto ora;
    case 0x01: /* ORA ($12,x) */
        if (!cputype_65c02) goto ora;
        /* fall through */
    case 0x1D: /* ORA $1234,x */
    case 0x15: /* ORA $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x0D: /* ORA $1234 */
    case 0x05: /* ORA $12 */
    ora:
        load_mem(&alu);
    ora2:
        bincalc_reg(cpu, or_bit, &alu, &cpu->a, &altlda);
        set_reg(&alu, &cpu->a);
        if (altmode) goto constind;
        if (altlda) goto indresult;
        break;
    case 0x49: /* EOR #$12 */
        load_imm(adr, &alu);
        if (bincalc_reg(cpu, xor_bit, &alu, &cpu->a, &altlda)) {
            del_reg(&alu);
            goto remove;
        }
        optname = try_a(cpu, &alu);
        set_reg(&alu, &cpu->a);
        if (optname != NULL) goto replace;
        if (is_known(&alu)) goto constresult;
        if (altlda) goto indresult;
        break;
    case 0x51: /* EOR ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto eor;
    case 0x59: /* EOR $1234,y */
        altmode = is_known(&cpu->y);
        goto eor;
    case 0x41: /* EOR ($12,x) */
        if (!cputype_65c02) goto eor;
        /* fall through */
    case 0x5D: /* EOR $1234,x */
    case 0x55: /* EOR $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x4D: /* EOR $1234 */
    case 0x45: /* EOR $12 */
    eor:
        load_mem(&alu);
    eor2:
        bincalc_reg(cpu, xor_bit, &alu, &cpu->a, &altlda);
        set_reg(&alu, &cpu->a);
        if (altmode) goto constind;
        if (altlda) goto indresult;
        break;
    case 0x68: /* PLA */
        incdec(cpu, &cpu->s, true);
        goto lda;
    case 0xA9: /* LDA #$12 */
        load_imm(adr, &alu);
        if (ld_reg(cpu, &alu, &cpu->a)) {
            del_reg(&alu);
            goto remove;
        }
        optname = try_a(cpu, &alu);
        set_reg(&alu, &cpu->a);
        if (optname != NULL) goto replace;
        break;
    case 0xB1: /* LDA ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto lda;
    case 0xB9: /* LDA $1234,y */
        altmode = is_known(&cpu->y);
        goto lda;
    case 0xA1: /* LDA ($12,x) */
        if (!cputype_65c02) goto lda;
        /* fall through */
    case 0xBD: /* LDA $1234,x */
    case 0xB5: /* LDA $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0xAD: /* LDA $1234 */
    case 0xA5: /* LDA $12 */
    lda:
        load_mem(&alu);
        calc_nz(cpu, &alu);
        set_reg(&alu, &cpu->a);
        if (altmode) goto constind;
        break;
    case 0x0A: /* ASL A */
        if (asl(cpu, &cpu->a)) goto remove;
        break;
    case 0x2A: /* ROL A */
        b = flag_c(cpu);
        if (rol(cpu, &cpu->a)) goto remove;
        if (b == B0) {optname = "asl"; goto simplify;}
        break;
    case 0x4A: /* LSR A */
        if (lsr(cpu, &cpu->a)) goto remove;
        break;
    case 0x6A: /* ROR A */
        b = flag_c(cpu);
        if (ror(cpu, &cpu->a)) goto remove;
        if (b == B0) {optname = "lsr"; goto simplify;}
        break;
    case 0xC9: /* CMP #$12 */
        load_imm(adr, &alu);
        if (cmp(cpu, &cpu->a, &alu, &optname)) goto remove;
        if (optname != NULL) goto replace;
        break;
    case 0xD1: /* CMP ($12),y */
        altmode = (cputype_65c02 && is_zero(&cpu->y));
        goto cmp;
    case 0xD9: /* CMP $1234,y */
        altmode = is_known(&cpu->y);
        goto cmp;
    case 0xC1: /* CMP ($12,x) */
        if (!cputype_65c02) goto cmp;
        /* fall through */
    case 0xDD: /* CMP $1234,x */
    case 0xD5: /* CMP $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0xCD: /* CMP $1234 */
    case 0xC5: /* CMP $12 */
    cmp:
        load_mem(&alu);
        cmp(cpu, &cpu->a, &alu, &optname);
        if (altmode) goto constind;
        break;
    case 0xE0: /* CPX #$12 */
        load_imm(adr, &alu);
        if (cmp(cpu, &cpu->x, &alu, &optname)) goto remove;
        if (optname != NULL) goto replace;
        break;
    case 0xEC: /* CPX $1234 */
    case 0xE4: /* CPX $12 */
        load_mem(&alu);
        cmp(cpu, &cpu->x, &alu, &optname);
        break;
    case 0xC0: /* CPY #$12 */
        load_imm(adr, &alu);
        if (cmp(cpu, &cpu->y, &alu, &optname)) goto remove;
        if (optname != NULL) goto replace;
        break;
    case 0xCC: /* CPY $1234 */
    case 0xC4: /* CPY $12 */
        load_mem(&alu);
        cmp(cpu, &cpu->y, &alu, &optname);
        break;
    case 0x1E: /* ASL $1234,x */
    case 0x16: /* ASL $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x0E: /* ASL $1234 */
    case 0x06: /* ASL $12 */
        load_mem(&alu);
        asl(cpu, &alu);
        del_reg(&alu);
        if (altmode) goto constind;
        break;
    case 0x3E: /* ROL $1234,x */
    case 0x36: /* ROL $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x2E: /* ROL $1234 */
    case 0x26: /* ROL $12 */
        load_mem(&alu);
        b = flag_c(cpu);
        rol(cpu, &alu);
        del_reg(&alu);
        if (altmode) goto constind;
        if (b == B0) {optname = "asl"; goto simplify;}
        break;
    case 0x5E: /* LSR $1234,x */
    case 0x56: /* LSR $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x4E: /* LSR $1234 */
    case 0x46: /* LSR $12 */
        load_mem(&alu);
        lsr(cpu, &alu);
        del_reg(&alu);
        if (altmode) goto constind;
        break;
    case 0x7E: /* ROR $1234,x */
    case 0x76: /* ROR $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0x6E: /* ROR $1234 */
    case 0x66: /* ROR $12 */
        load_mem(&alu);
        b = flag_c(cpu);
        ror(cpu, &alu);
        del_reg(&alu);
        if (altmode) goto constind;
        if (b == B0) {optname = "lsr"; goto simplify;}
        break;
    case 0x10: /* BPL *+$12 */
    bpl:
        b = get_bit(cpu->p.n);
        if (b == B1) goto removecond;
        if (ln < 0) break;
        if (cpu->lb > 255) { cpu->branched = true; break; }
        if (adr == 0) goto jump;
        if (b == BU) mod_bit(cpu->p.n, B1); else set_bit(&cpu->p.n, new_bit1());
        if (b == B0) {
            cpu->branched = true;
            if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gpl"; goto replace; }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
        }
        break;
    case 0x30: /* BMI *+$12 */
    bmi:
        b = get_bit(cpu->p.n);
        if (b == B0) goto removecond;
        if (ln < 0) break;
        if (cpu->lb > 255) { cpu->branched = true; break; }
        if (adr == 0) goto jump;
        if (b == BU) mod_bit(cpu->p.n, B0); else set_bit(&cpu->p.n, new_bit0());
        if (b == B1) {
            cpu->branched = true;
            if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gmi"; goto replace; }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
        }
        break;
    case 0x50: /* BVC *+$12 */
    bvc:
        b = get_bit(cpu->p.v);
        if (b == B1) goto removecond;
        if (ln < 0) break;
        if (cpu->lb > 255) { cpu->branched = true; break; }
        if (adr == 0) goto jump;
        if (b == BU) mod_bit(cpu->p.v, B1); else set_bit(&cpu->p.v, new_bit1());
        if (b == B0) {
            cpu->branched = true;
            if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gvc"; goto replace; }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
        }
        break;
    case 0x70: /* BVS *+$12 */
    bvs:
        b = get_bit(cpu->p.v);
        if (b == B0) goto removecond;
        if (ln < 0) break;
        if (cpu->lb > 255) { cpu->branched = true; break; }
        if (adr == 0) goto jump;
        if (b == BU) mod_bit(cpu->p.v, B0); else set_bit(&cpu->p.v, new_bit0());
        if (b == B1) {
            cpu->branched = true;
            if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gvs"; goto replace; }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
        }
        break;
    case 0x90: /* BCC *+$12 */
    bcc:
        b = flag_c(cpu);
        if (b == B1) goto removecond;
        if (ln < 0) break;
        if (cpu->lb > 255) { cpu->branched = true; break; }
        if (adr == 0) goto jump;
        if (b == BU) mod_bit(cpu->p.c, B1); else set_bit(&cpu->p.c, new_bit1());
        if (cpu->ccmp) {
            unsigned int i, j;
            for (i = 0; i < 8; i++) {
                j = i ^ 7;
                if (get_bit(cpu->z3.a[j]) != B1) break;
                if (get_bit(cpu->z2.a[j]) == BU) mod_bit(cpu->z2.a[j], B1);
            }
            cpu->ccmp = false;
        }
        if (b == B0) {
            cpu->branched = true;
            if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gcc"; goto replace; }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
        }
        break;
    case 0xB0: /* BCS *+$12 */
    bcs:
        b = flag_c(cpu);
        if (b == B0) goto removecond;
        if (ln < 0) break;
        if (cpu->lb > 255) { cpu->branched = true; break; }
        if (adr == 0) goto jump;
        if (b == BU) mod_bit(cpu->p.c, B0); else set_bit(&cpu->p.c, new_bit0());
        if (cpu->ccmp) {
            unsigned int i, j;
            Bit_types bb;
            for (i = 0; i < 8; i++) {
                j = i ^ 7;
                bb = get_bit(cpu->z3.a[j]);
                if (bb != B0) break;
                if (get_bit(cpu->z2.a[j]) == BU) mod_bit(cpu->z2.a[j], B0);
            }
            if (bb == B1) {
                for (i = i + 1; i < 8; i++) if (get_bit(cpu->z3.a[i ^ 7]) != B0) break;
                if (i == 8 && get_bit(cpu->z2.a[j]) == BU) mod_bit(cpu->z2.a[j], B0);
            }
            if (cpu->zcmp) {
                if (get_bit(cpu->p.z) == BU) mod_bit(cpu->p.z, B0); else set_bit(&cpu->p.z, new_bit0());
                cpu->zcmp = false;
            }
            cpu->ccmp = false;
        }
        if (b == B1) {
            cpu->branched = true;
            if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gcs"; goto replace; }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
        }
        break;
    case 0xD0: /* BNE *+$12 */
        {
            unsigned int i;
            bool z;
        bne:
            if (flag_is_zero(cpu)) goto removecond;
            if (ln < 0) break;
            if (cpu->lb > 255) { cpu->branched = true; break; }
            if (adr == 0) goto jump;
            z = flag_is_nonzero(cpu);
            if (get_bit(cpu->p.z) == BU) mod_bit(cpu->p.z, B1); else set_bit(&cpu->p.z, new_bit1());
            if (cpu->zcmp) {
                for (i = 0; i < 8; i++) {
                    b = get_bit(cpu->z3.a[i]);
                    if (b != BU && get_bit(cpu->z2.a[i]) == BU) mod_bit(cpu->z2.a[i], b);
                }
                if (cpu->ccmp) { 
                    if (get_bit(cpu->p.c) == BU) mod_bit(cpu->p.c, B1); else set_bit(&cpu->p.c, new_bit1());
                    cpu->ccmp = false;
                }
                cpu->zcmp = false;
            } else {
                for (i = 0; i < 8; i++) {
                    if (get_bit(cpu->z1.a[i]) == BU) mod_bit(cpu->z1.a[i], B0);
                }
            }
            if (z) {
                cpu->branched = true;
                if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "gne"; goto replace; }
                if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
            }
            break;
        }
    case 0xF0: /* BEQ *+$12 */
        {
            bool z;
        beq:
            if (flag_is_nonzero(cpu)) goto removecond;
            if (ln < 0) break;
            if (cpu->lb > 255) { cpu->branched = true; break; }
            if (adr == 0) goto jump;
            z = flag_is_zero(cpu);
            if (get_bit(cpu->p.z) == BU) mod_bit(cpu->p.z, B0); else set_bit(&cpu->p.z, new_bit0());
            cpu->zcmp = false;
            if (z) {
                cpu->branched = true;
                if (adr == 1 || (cputype_65ce02 && adr == 2)) { optname = "geq"; goto replace; }
                if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) { optname = "bra"; goto simplify; }
            }
            break;
        }
    case 0x2C: /* BIT $1234 */
    case 0x24: /* BIT $12 */
    bit:
        load_mem(&alu);
        bit_reg(cpu, &alu, &cpu->a);
        if (altmode) goto constind;
        break;
    case 0x18: /* CLC */
        b = flag_c(cpu);
        cpu->ccmp = false;
        if (b == B0) goto removeclr;
        set_bit(&cpu->p.c, new_bit0());
        break;
    case 0x38: /* SEC */
        b = flag_c(cpu);
        cpu->ccmp = false;
        if (b == B1) goto removeset;
        set_bit(&cpu->p.c, new_bit1());
        break;
    case 0x58: /* CLI */
        if (set_flag(B0, &cpu->p.i)) goto removeclr;
        break;
    case 0x78: /* SEI */
        if (set_flag(B1, &cpu->p.i)) goto removeset;
        break;
    case 0xB8: /* CLV */
        if (set_flag(B0, &cpu->p.v)) goto removeclr;
        break;
    case 0xD8: /* CLD */
        if (set_flag(B0, &cpu->p.d)) goto removeclr;
        break;
    case 0xF8: /* SED */
        if (set_flag(B1, &cpu->p.d)) goto removeset;
        break;
    case 0xDE: /* DEC $1234,x */
    case 0xD6: /* DEC $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0xCE: /* DEC $1234 */
    case 0xC6: /* DEC $12 */
        load_mem(&alu);
        incdec(cpu, &alu, false);
        del_reg(&alu);
        if (altmode) goto constind;
        break;
    case 0xFE: /* INC $1234,x */
    case 0xF6: /* INC $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0xEE: /* INC $1234 */
    case 0xE6: /* INC $12 */
        load_mem(&alu);
        incdec(cpu, &alu, true);
        del_reg(&alu);
        if (altmode) goto constind;
        break;
    case 0xCA: /* DEX */
        incdec(cpu, &cpu->x, false);
        break;
    case 0x88: /* DEY */
        incdec(cpu, &cpu->y, false);
        break;
    case 0xE8: /* INX */
        incdec(cpu, &cpu->x, true);
        break;
    case 0xC8: /* INY */
        incdec(cpu, &cpu->y, true);
        break;
    case 0xA2: /* LDX #$12 */
        load_imm(adr, &alu);
        if (ld_reg(cpu, &alu, &cpu->x)) {
            del_reg(&alu);
            goto remove;
        }
        optname = try_x(cpu, &alu);
        set_reg(&alu, &cpu->x);
        if (optname != NULL) goto replace;
        break;
    case 0xBE: /* LDX $1234,y */
    case 0xB6: /* LDX $12,y */
        altmode = is_known(&cpu->y);
        /* fall through */
    case 0xAE: /* LDX $1234 */
    case 0xA6: /* LDX $12 */
    ldx:
        load_mem(&alu);
        calc_nz(cpu, &alu);
        set_reg(&alu, &cpu->x);
        if (altmode) goto constind;
        break;
    case 0xA0: /* LDY #$12 */
        load_imm(adr, &alu);
        if (ld_reg(cpu, &alu, &cpu->y)) {
            del_reg(&alu);
            goto remove;
        }
        optname = try_y(cpu, &alu);
        set_reg(&alu, &cpu->y);
        if (optname != NULL) goto replace;
        break;
    case 0xBC: /* LDY $1234,x */
    case 0xB4: /* LDY $12,x */
        altmode = is_known(&cpu->x);
        /* fall through */
    case 0xAC: /* LDY $1234 */
    case 0xA4: /* LDY $12 */
    ldy:
        load_mem(&alu);
        calc_nz(cpu, &alu);
        set_reg(&alu, &cpu->y);
        if (altmode) goto constind;
        break;
    case 0xEA: /* NOP */
        break;
    case 0x91: /* STA ($12),y */
        if (cputype_65c02 && is_zero(&cpu->y)) goto constind;
        break;
    case 0x99: /* STA $1234,y */
    case 0x96: /* STX $12,y */
        if (is_known(&cpu->y)) goto constind;
        break;
    case 0x81: /* STA ($12,x) */
        if (cputype_65c02 && is_known(&cpu->x)) goto constind;
        break;
    case 0x9D: /* STA $1234,x */
    case 0x95: /* STA $12,x */
        if (is_known(&cpu->x)) goto constind;
        /* fall through */
    case 0x8D: /* STA $1234 */
    case 0x85: /* STA $12 */
        if (cputype_65c02 && is_zero(&cpu->a)) {optname = "stz"; goto simplify;}
        break;
    case 0x8E: /* STX $1234 */
    case 0x86: /* STX $12 */
        if (cputype_65c02 && is_zero(&cpu->x)) {optname = "stz"; goto simplify;}
        break;
    case 0x94: /* STY $12,x */
        if (is_known(&cpu->x)) goto constind;
        /* fall through */
    case 0x8C: /* STY $1234 */
    case 0x84: /* STY $12 */
        if (cputype_65c02 && is_zero(&cpu->y)) {optname = "stz"; goto simplify;}
        break;
    case 0x48: /* PHA */
    case 0x08: /* PHP */
    push:
        incdec(cpu, &cpu->s, false);
        break;
    case 0x9A: /* TXS */
        if (transreg(&cpu->x, &cpu->s)) goto remove;
        break;
    case 0xBA: /* TSX */
        if (transreg2(cpu, &cpu->s, &cpu->x)) goto remove;
        break;
    case 0xAA: /* TAX */
        if (transreg2(cpu, &cpu->a, &cpu->x)) goto remove;
        break;
    case 0xA8: /* TAY */
        if (transreg2(cpu, &cpu->a, &cpu->y)) goto remove;
        break;
    case 0x8A: /* TXA */
        if (transreg2(cpu, &cpu->x, &cpu->a)) goto remove;
        break;
    case 0x98: /* TYA */
        if (transreg2(cpu, &cpu->y, &cpu->a)) goto remove;
        break;
    case 0x28: /* PLP */
        incdec(cpu, &cpu->s, true);
        reset_bit(&cpu->p.n);
        reset_bit(&cpu->p.v);
        reset_bit(&cpu->p.e);
        reset_bit(&cpu->p.d);
        reset_bit(&cpu->p.i);
        reset_bit(&cpu->p.z);
        reset_bit(&cpu->p.c);
        reset_reg8(cpu->z1.a);
        cpu->zcmp = false;
        cpu->ccmp = false;
        break;
    case 0x4C: /* JMP $1234 */
        if (cpu->pc == (uint16_t)adr) goto jump;
        cpu->branched = true;
        if ((uint16_t)(cpu->pc-adr+0x7e) < 0x100) {
            if (flag_is_nonzero(cpu)) {optname = "gne";goto replace;} /* 0xD0 BNE *+$12 */
            if (flag_is_zero(cpu))  {optname = "geq";goto replace;} /* 0xF0 BEQ *+$12 */
            switch (flag_c(cpu)) {
            case B0: optname = "gcc";goto replace; /* 0x90 BCC *+$12 */
            case B1: optname = "gcs";goto replace; /* 0xB0 BCS *+$12 */
            default: break;
            }
            switch (get_bit(cpu->p.n)) {
            case B0: optname = "gpl";goto replace; /* 0x10 BPL *+$12 */
            case B1: optname = "gmi";goto replace; /* 0x30 BMI *+$12 */
            default: break;
            }
            switch (get_bit(cpu->p.v)) {
            case B0: optname = "gvc";goto replace; /* 0x50 BVC *+$12 */
            case B1: optname = "gvs";goto replace; /* 0x70 BVS *+$12 */
            default: break;
            }
            if (cputype_65c02 || cputype_65ce02 || cputype == &c65dtv02) {optname = "gra";goto replace;}
        }
        break;
    case 0x6C: /* JMP ($1234) */
    case 0x40: /* RTI */
    case 0x60: /* RTS */
        cpu->branched = true;
        break;
    case 0x20: /* JSR $1234 */
    jsr:
        cpu->call = true;
        cpu->zcmp = false;
        cpu->ccmp = false;
        reset_reg8(cpu->a.a);
        reset_reg8(cpu->x.a);
        reset_reg8(cpu->y.a);
        reset_reg8(cpu->z.a);
        reset_reg8(cpu->b.a);
        reset_reg8(cpu->z1.a);
        cpu->sir = 256;
        cpu->sac = 256;
        reset_bit(&cpu->p.n);
        reset_bit(&cpu->p.v);
        reset_bit(&cpu->p.e);
        reset_bit(&cpu->p.d);
        reset_bit(&cpu->p.i);
        reset_bit(&cpu->p.z);
        reset_bit(&cpu->p.c);
        if (altmode) goto constind;
        break;
    case 0x00: /* BRK #$12 */
        cpu_opt_invalidate();
        break;
    default:
        if (cputype == &c6502i || cputype == &c65dtv02) {
            switch (cod) {
            case 0xBF: /* LAX $1234,y */
            case 0xB7: /* LAX $12,y */
                altmode = is_known(&cpu->y);
                /* fall through */
            case 0xAB: /* LAX #$12 */
            case 0xAF: /* LAX $1234 */
            case 0xA7: /* LAX $12 */
            case 0xA3: /* LAX ($12,x) */
            case 0xB3: /* LAX ($12),y */
                load_mem(&alu);
                calc_nz(cpu, &alu);
                change_reg(&alu, &cpu->a);
                set_reg(&alu, &cpu->x);
                if (altmode) goto constind;
                break;
            case 0x8B: /* ANE #$12 */
                load_unk(&alu); /* unstable */
                calc_nz(cpu, &alu);
                set_reg(&alu, &cpu->a);
                break;
            case 0x6B: /* ARR #$12 */
            /*case 0xEB:*/ /* SBC #$12 */
            arr:
                reset_reg8(cpu->a.a);
                reset_reg8(cpu->z1.a);
                reset_bit(&cpu->p.n);
                reset_bit(&cpu->p.v);
                reset_bit(&cpu->p.z);
                reset_bit(&cpu->p.c);
                cpu->zcmp = false;
                cpu->ccmp = false;
                if (altmode) goto constind;
                break;
            case 0x4B: /* ASR #$12 */
                load_imm(adr, &alu);
                if (asri(cpu, &cpu->a, &alu)) goto remove;
                break;
            case 0xDB: /* DCP $1234,y */
                altmode = is_known(&cpu->y);
                goto dcp;
            case 0xDF: /* DCP $1234,x */
            case 0xD7: /* DCP $12,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0xCF: /* DCP $1234 */
            case 0xC7: /* DCP $12 */
            case 0xC3: /* DCP ($12,x) */
            case 0xD3: /* DCP ($12),y */
            dcp:
                load_mem(&alu);
                incdec(cpu, &alu, false);
                cmp(cpu, &cpu->a, &alu, &optname);
                if (altmode) goto constind;
                break;
            case 0xFB: /* ISB $1234,y */
            case 0x7B: /* RRA $1234,y */
                altmode = is_known(&cpu->y);
                goto arr;
            case 0xFF: /* ISB $1234,x */
            case 0xF7: /* ISB $12,x */
            case 0x7F: /* RRA $1234,x */
            case 0x77: /* RRA $12,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0xEF: /* ISB $1234 */
            case 0xE7: /* ISB $12 */
            case 0xE3: /* ISB ($12,x) */
            case 0xF3: /* ISB ($12),y */
            case 0x6F: /* RRA $1234 */
            case 0x67: /* RRA $12 */
            case 0x63: /* RRA ($12,x) */
            case 0x73: /* RRA ($12),y */
                goto arr;
            case 0x3B: /* RLA $1234,y */
                altmode = is_known(&cpu->y);
                goto rla;
            case 0x3F: /* RLA $1234,x */
            case 0x37: /* RLA $12,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0x2F: /* RLA $1234 */
            case 0x27: /* RLA $12 */
            case 0x23: /* RLA ($12,x) */
            case 0x33: /* RLA ($12),y */
            rla:
                load_mem(&alu);
                flag_c(cpu);
                rol(cpu, &alu);
                goto anda2;
            case 0x1B: /* SLO $1234,y */
                altmode = is_known(&cpu->y);
                goto slo;
            case 0x1F: /* SLO $1234,x */
            case 0x17: /* SLO $12,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0x0F: /* SLO $1234 */
            case 0x07: /* SLO $12 */
            case 0x03: /* SLO ($12,x) */
            case 0x13: /* SLO ($12),y */
            slo:
                load_mem(&alu);
                asl(cpu, &alu);
                goto ora2;
            case 0x5B: /* SRE $1234,y */
                altmode = is_known(&cpu->y);
                goto sre;
            case 0x5F: /* SRE $1234,x */
            case 0x57: /* SRE $12,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0x4F: /* SRE $1234 */
            case 0x47: /* SRE $12 */
            case 0x43: /* SRE ($12,x) */
            case 0x53: /* SRE ($12),y */
            sre:
                load_mem(&alu);
                lsr(cpu, &alu);
                goto eor2;
            case 0x97: /* SAX $12,y */
                altmode = is_known(&cpu->y);
                /* fall through */
            case 0x8F: /* SAX $1234 */
            case 0x87: /* SAX $12 */
            case 0x83: /* SAX ($12,x) */
                if (altmode) goto constind;
                break;
            default:
                if (cputype == &c6502i) {
                    switch (cod) {
                    case 0x0B: /* ANC #$12 */
                        load_imm(adr, &alu);
                        if (bincalc_reg(cpu, and_bit, &alu, &cpu->a, &altlda)) {
                            if (eq_bit(cpu->p.c, cpu->a.a[7])) {
                                del_reg(&alu);
                                goto remove;
                            }
                            b = get_bit(cpu->a.a[7]);
                            change_bit(&cpu->p.c, cpu->a.a[7]);
                            if (b != BU) {
                                optname = (b == B0 ? "clc" : "sec");
                                del_reg(&alu);
                                goto replace;
                            }
                            break;
                        }
                        if (eq_bit(cpu->p.c, cpu->a.a[7])) {
                            optname = try_a(cpu, &alu);
                        } else {
                            change_bit(&cpu->p.c, cpu->a.a[7]);
                            optname = NULL;
                        }
                        set_reg(&alu, &cpu->a);
                        if (optname != NULL) goto replace;
                        break;
                    case 0xBB: /* LDS $1234,y */
                        load_mem(&alu);
                        bincalc_reg(cpu, and_bit, &alu, &cpu->s, &altlda);
                        transreg(&cpu->s, &cpu->a);
                        transreg(&cpu->s, &cpu->x);
                        break;
                    case 0x1C: /* NOP $1234,x */
                    /*case 0x3C:*/ /* NOP $1234,x */
                    /*case 0x5C:*/ /* NOP $1234,x */
                    /*case 0x7C:*/ /* NOP $1234,x */
                    /*case 0xDC:*/ /* NOP $1234,x */
                    /*case 0xFC:*/ /* NOP $1234,x */
                    case 0x14: /* NOP $12,x */
                    /*case 0x34:*/ /* NOP $12,x */
                    /*case 0x54:*/ /* NOP $12,x */
                    /*case 0x74:*/ /* NOP $12,x */
                    /*case 0xD4:*/ /* NOP $12,x */
                    /*case 0xF4:*/ /* NOP $12,x */
                        altmode = is_known(&cpu->x);
                        /* fall through */
                    case 0x80: /* NOP #$12 */
                    /*case 0x82:*/ /* NOP #$12 */
                    /*case 0xC2:*/ /* NOP #$12 */
                    /*case 0xE2:*/ /* NOP #$12 */
                    /*case 0x89:*/ /* NOP #$12 */
                    case 0x0C: /* NOP $1234 */
                    case 0x04: /* NOP $12 */
                    /*case 0x1A:*/ /* NOP */
                    /*case 0x3A:*/ /* NOP */
                    /*case 0x5A:*/ /* NOP */
                    /*case 0x7A:*/ /* NOP */
                    /*case 0xDA:*/ /* NOP */
                    /*case 0xFA:*/ /* NOP */
                        if (altmode) goto constind;
                        break;
                    case 0xCB: /* SBX #$12 */
                        load_imm(adr, &alu);
                        if (sbx(cpu, &cpu->x, &cpu->a, &alu, &optname)) {
                            del_reg(&alu);
                            goto remove;
                        }
                        set_reg(&alu, &cpu->x);
                        if (optname != NULL) goto replace;
                        break;
                    case 0x93: /* SHA $1234,x */
                    case 0x9F: /* SHA $1234,y */
                    case 0x9E: /* SHX $1234,y */
                    case 0x9C: /* SHY $1234,x */
                        break;
                    case 0x9B: /* SHS $1234,y */
                        shs(&cpu->a, &cpu->x, &cpu->s);
                        break;
                    default:
                        cpu_opt_invalidate();
                    }
                    break;
                }
                switch (cod) {
                case 0x12: /* BRA +$12 */
                    if (ln < 0) break;
                    if (cpu->lb > 255) { cpu->branched = true; break; }
                    if (adr == 0) goto jump;
                    cpu->branched = true;
                    break;
                case 0x32: /* SAC #$12 */
                    if (adr == cpu->sac) goto remove;
                    cpu->sac = adr;
                    reset_reg8(cpu->a.a);
                    reset_reg8(cpu->x.a);
                    reset_reg8(cpu->y.a);
                    break;
                case 0x42: /* SIR #$12 */
                    if (adr == cpu->sir) goto remove;
                    cpu->sir = adr;
                    reset_reg8(cpu->a.a);
                    reset_reg8(cpu->x.a);
                    reset_reg8(cpu->y.a);
                    break;
                default:
                    cpu_opt_invalidate();
                }
                break;
            }
            break;
        }
        if (!cputype_65c02 && !cputype_65ce02) {
            cpu_opt_invalidate();
            break;
        }
        switch (cod) {
        case 0x72: /* ADC ($12) */ /* ADC ($12),z */
            goto adc;
        case 0xF2: /* SBC ($12) */ /* SBC ($12),z */
            goto sbc;
        case 0x32: /* AND ($12) */ /* AND ($12),z */
            goto anda;
        case 0x80: /* BRA *+$12 */
            if (ln < 0) break;
            if (cpu->lb > 255) { cpu->branched = true; break; }
            if (adr == 0) goto jump;
            cpu->branched = true;
            if (adr == 1 && (cputype == &r65c02 || cputype == &w65c02)) { optname = "gra"; goto replace; }
            break;
        case 0x89: /* BIT #$12 */
            load_imm(adr, &alu);
            if (bit_reg2(cpu, &alu, &cpu->a)) goto remove;
            break;
        case 0x3C: /* BIT $1234,x */
        case 0x34: /* BIT $12,x */
            altmode = is_known(&cpu->x);
            goto bit;
        case 0xD2: /* CMP ($12) */ /* CMP ($12),z */
            goto cmp;
        case 0x3A: /* DEC A */
            incdec(cpu, &cpu->a, false);
            break;
        case 0x1A: /* INC A */
            incdec(cpu, &cpu->a, true);
            break;
        case 0x52: /* EOR ($12) */ /* EOR ($12),z */
            goto eor;
        case 0xB2: /* LDA ($12) */ /* LDA ($12),z */
            goto lda;
        case 0x7C: /* JMP ($1234,x) */
            altmode = is_known(&cpu->x);
            cpu->branched = true;
            if (altmode) goto constind;
            break;
        case 0x12: /* ORA ($12) */ /* ORA ($12),z */
            goto ora;
        case 0xDA: /* PHX */
        case 0x5A: /* PHY */
            goto push;
        case 0xFA: /* PLX */
            incdec(cpu, &cpu->s, true);
            goto ldx;
        case 0x7A: /* PLY */
            incdec(cpu, &cpu->y, true);
            goto ldy;
        case 0x92: /* STA ($12) */ /* STA ($12),z */
            break;
        case 0x9E: /* STZ $1234,x */
        case 0x74: /* STZ $12,x */
            if (is_known(&cpu->x)) goto constind;
            /* fall through */
        case 0x9C: /* STZ $1234 */
        case 0x64: /* STZ $12 */
            break;
        case 0x1C: /* TRB $1234 */
        case 0x14: /* TRB $12 */
        case 0x0C: /* TSB $1234 */
        case 0x04: /* TSB $12 */
            load_mem(&alu);
            bit_reg2(cpu, &alu, &cpu->a);
            break;
        default:
            if (cputype == &c65c02) {
                cpu_opt_invalidate();
                break;
            }
            if ((cod & 0xF) == 0xF) { /* BBR & BBS */
                if ((adr & 0xff00) == 0) goto jump;
                break;
            }
            if ((cod & 0x7) == 0x7) { /* RMB & SMB */
                break;
            }
            if (cod == 0x54) { /* NOP $12,x */
                if (is_known(&cpu->x)) goto constind;
                break;
            }
            if (cod == 0x82 || cod == 0xDC || cod == 0x44) { /* NOP #$12 */ /* NOP $1234 */ /* NOP $12 */
                break;
            }
            if (cputype == &w65c02) {
                if (cod == 0xDB) { /* 0xDB STP */
                    cpu->branched = true;
                    break;
                }
                if (cod == 0xCB) { /* 0xCB WAI */
                    break;
                }
            }
            if (!cputype_65ce02) {
                cpu_opt_invalidate();
                break;
            }
            switch (cod) {
            case 0x43: /* ASR A */
                if (asr(cpu, &cpu->a)) goto remove;
                break;
            case 0x54: /* ASR $12,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0x44: /* ASR $12 */
            case 0xCB: /* ASW $1234 */
                load_mem(&alu);
                asr(cpu, &alu);
                del_reg(&alu);
                if (altmode) goto constind;
                break;
            case 0x83: /* BRA *+$1234 */
                if (ln < 0) break;
                if (cpu->lb > 255) { cpu->branched = true; break; }
                if (adr == 0) goto jump;
                cpu->branched = true;
                break;
            case 0x93: goto bcc; /* BCC *+$1234 */
            case 0xB3: goto bcs; /* BCS *+$1234 */
            case 0xF3: goto beq; /* BEQ *+$1234 */
            case 0x33: goto bmi; /* BMI *+$1234 */
            case 0xD3: goto bne; /* BNE *+$1234 */
            case 0x13: goto bpl; /* BPL *+$1234 */
            case 0x53: goto bvc; /* BVC *+$1234 */
            case 0x73: goto bvs; /* BVS *+$1234 */
            case 0x02: /* CLE */
                if (set_flag(B0, &cpu->p.e)) goto removeclr;
                break;
            case 0x03: /* SEE */
                if (set_flag(B1, &cpu->p.e)) goto removeset;
                break;
            case 0xC2: /* CPZ #$12 */
                load_imm(adr, &alu);
                if (cmp(cpu, &cpu->z, &alu, &optname)) goto remove;
                if (optname != NULL) goto replace;
                break;
            case 0xD4: /* CPZ $12 */
            case 0xDC: /* CPZ $1234 */
                load_mem(&alu);
                cmp(cpu, &cpu->z, &alu, &optname);
                break;
            case 0xC3: /* DEW $12 */
                load_mem(&alu);
                incdec(cpu, &alu, false);
                del_reg(&alu);
                break;
            case 0xE3: /* INW $12 */
                load_mem(&alu);
                incdec(cpu, &alu, true);
                del_reg(&alu);
                break;
            case 0x3B: /* DEZ */
                incdec(cpu, &cpu->z, false);
                break;
            case 0x1B: /* INZ */
                incdec(cpu, &cpu->z, true);
                break;
            case 0xE2: /* LDA ($12,s),y */
                goto lda;
            case 0x23: /* JSR ($1234,x) */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0x63: /* BSR *+$1234 */
            case 0x22: /* JSR ($1234) */
                goto jsr;
            case 0xA3: /* LDZ #$12 */
                load_imm(adr, &alu);
                if (ld_reg(cpu, &alu, &cpu->z)) {
                    del_reg(&alu);
                    goto remove;
                }
                optname = try_z(cpu, &alu);
                set_reg(&alu, &cpu->z);
                if (optname != NULL) goto replace;
                break;
            case 0xBB: /* LDZ $1234,x */
                altmode = is_known(&cpu->x);
                /* fall through */
            case 0xAB: /* LDZ $1234 */
            ldz:
                load_mem(&alu);
                calc_nz(cpu, &alu);
                set_reg(&alu, &cpu->z);
                if (altmode) goto constind;
                break;
            case 0x42: /* NEG A */
                if (neg(cpu, &cpu->a)) goto remove;
                break;
            case 0xF4: /* PHW #$12 */
            case 0xFC: /* PHW $1234 */
                incdec(cpu, &cpu->s, false);
                /* fall through */
            case 0xDB: /* PHZ */
                goto push;
            case 0xFB: /* PLZ */
                incdec(cpu, &cpu->s, true);
                goto ldz;
            case 0xEB: /* ROW $1234 */
                load_mem(&alu);
                flag_c(cpu);
                rol(cpu, &alu);
                del_reg(&alu);
                break;
            case 0x62: /* RTS #$12 */
                cpu->branched = true;
                break;
            case 0x82: /* STA ($12,s),y */
                break;
            case 0x9B: /* STX $1234,y */
                if (is_known(&cpu->y)) goto constind;
                break;
            case 0x8B: /* STY $1234,x */
                if (is_known(&cpu->x)) goto constind;
                break;
            case 0x5B: /* TAB */
                if (transreg(&cpu->a, &cpu->b)) goto remove;
                break;
            case 0x4B: /* TAZ */
                if (transreg2(cpu, &cpu->a, &cpu->z)) goto remove;
                break;
            case 0x7B: /* TBA */
                if (transreg2(cpu, &cpu->b, &cpu->a)) goto remove;
                break;
            case 0x0B: /* TSY */
                if (transreg2(cpu, &cpu->sh, &cpu->y)) goto remove;
                break;
            case 0x2B: /* TYS */
                if (transreg(&cpu->y, &cpu->sh)) goto remove;
                break;
            case 0x6B: /* TZA */
                if (transreg2(cpu, &cpu->z, &cpu->a)) goto remove;
                break;
            default:
                if (cputype == &c4510 && cod == 0x5C) { /* MAP */
                    break;
                }
                cpu_opt_invalidate();
                break;
            }
            break;
        }
        break;
    }
    return;
remove:
    err_msg2(ERROR_____REDUNDANT, "as it does not change anything", epoint);
    return;
removecond:
    err_msg2(ERROR_____REDUNDANT, "as the condition is never met", epoint);
    return;
removeset:
    err_msg2(ERROR_____REDUNDANT, "as flag is already set", epoint);
    return;
removeclr:
    err_msg2(ERROR_____REDUNDANT, "as flag is already clear", epoint);
    return;
jump:
    err_msg2(ERROR_____REDUNDANT, "as target is the next instruction", epoint);
    return;
constind:
    err_msg2(ERROR_____REDUNDANT, "indexing with a constant value", epoint);
    return;
constresult:
    err_msg2(ERROR__CONST_RESULT, NULL, epoint);
    return;
indresult:
    err_msg2(ERROR____IND_RESULT, NULL, epoint);
    return;
replace:
    err_msg2(ERROR___OPTIMIZABLE, optname, epoint);
    return;
simplify:
    err_msg2(ERROR______SIMPLIFY, optname, epoint);
}

void cpu_opt_invalidate(void) {
    struct optimizer_s *cpu = current_section->optimizer;
    unsigned int i;

    if (cpu == NULL) {
        cpu = (struct optimizer_s *)mallocx(sizeof *cpu);
        current_section->optimizer = cpu;
        for (i = 0; i < 8; i++) {
            cpu->a.a[i] = new_bitu();
            cpu->x.a[i] = new_bitu();
            cpu->y.a[i] = new_bitu();
            cpu->z.a[i] = new_bitu();
            cpu->s.a[i] = new_bitu();
            cpu->sh.a[i] = new_bitu();
            cpu->b.a[i] = new_bitu();
            cpu->z1.a[i] = new_bitu();
            cpu->z2.a[i] = new_bitu();
            cpu->z3.a[i] = new_bitu();
        }
        cpu->p.n = new_bitu();
        cpu->p.v = new_bitu();
        cpu->p.e = new_bitu();
        cpu->p.d = new_bitu();
        cpu->p.i = new_bitu();
        cpu->p.z = new_bitu();
        cpu->p.c = new_bitu();
        cpu->cc = new_bitu();
    } else {
        reset_reg8(cpu->a.a);
        reset_reg8(cpu->x.a);
        reset_reg8(cpu->y.a);
        reset_reg8(cpu->z.a);
        reset_reg8(cpu->s.a);
        reset_reg8(cpu->sh.a);
        reset_reg8(cpu->b.a);
        reset_reg8(cpu->z1.a);
        reset_bit(&cpu->p.n);
        reset_bit(&cpu->p.v);
        reset_bit(&cpu->p.e);
        reset_bit(&cpu->p.d);
        reset_bit(&cpu->p.i);
        reset_bit(&cpu->p.z);
        reset_bit(&cpu->p.c);
    }
    cpu->sir = 256;
    cpu->sac = 256;
    cpu->branched = false;
    cpu->call = false;
    cpu->lb = 0;
    cpu->pc = 65536;
    cpu->zcmp = false;
    cpu->ccmp = false;
}

void cpu_opt_destroy(struct optimizer_s *cpu) {
    if (cpu == NULL) {
        return;
    }
    del_reg(&cpu->a);
    del_reg(&cpu->x);
    del_reg(&cpu->y);
    del_reg(&cpu->z);
    del_reg(&cpu->s);
    del_reg(&cpu->sh);
    del_reg(&cpu->b);
    del_reg(&cpu->z1);
    del_reg(&cpu->z2);
    del_reg(&cpu->z3);
    del_bit(cpu->p.n);
    del_bit(cpu->p.v);
    del_bit(cpu->p.e);
    del_bit(cpu->p.d);
    del_bit(cpu->p.i);
    del_bit(cpu->p.z);
    del_bit(cpu->p.c);
    del_bit(cpu->cc);
    free(cpu);
}
