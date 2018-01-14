/*
    $Id: instruction.c 1498 2017-04-29 17:49:46Z soci $

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
#include "instruction.h"
#include <string.h>
#include "opcodes.h"
#include "obj.h"
#include "64tass.h"
#include "section.h"
#include "file.h"
#include "listing.h"
#include "error.h"
#include "longjump.h"
#include "arguments.h"
#include "optimizer.h"

#include "addressobj.h"
#include "listobj.h"
#include "registerobj.h"
#include "codeobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"

static const uint32_t *mnemonic;    /* mnemonics */
static const uint8_t *opcode;       /* opcodes */
static unsigned int last_mnem;

bool longaccu = false, longindex = false, autosize = false; /* hack */
uint32_t dpage = 0;
unsigned int databank = 0;
bool longbranchasjmp = false;
bool allowslowbranch = true;

int lookup_opcode(const uint8_t *s) {
    int32_t s4;
    unsigned int also, felso, elozo, no;
    int32_t name;

    name = (s[0] << 16) | (s[1] << 8) | s[2];
    if (arguments.caseinsensitive != 0) name |= 0x202020;
    also = 0;
    no = (felso = last_mnem) / 2;
    for (;;) {  /* do binary search */
        if ((s4 = name - (int32_t)mnemonic[no]) == 0) return (int)no;
        elozo = no;
        if (elozo == (no = ((s4 > 0) ? (felso + (also = no)) : (also + (felso = no))) / 2)) break;
    }
    return -1;
}

void select_opcodes(const struct cpu_s *cpumode) {
    last_mnem = cpumode->opcodes;
    mnemonic = cpumode->mnemonic; 
    opcode = cpumode->opcode;
}

MUST_CHECK bool touval(Obj *v1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Error *err = v1->obj->uval(v1, uv, bits, epoint);
    if (err == NULL) return false;
    err_msg_output_and_destroy(err);
    return true;
}

MUST_CHECK bool toival(Obj *v1, ival_t *iv, unsigned int bits, linepos_t epoint) {
    Error *err = v1->obj->ival(v1, iv, bits, epoint);
    if (err == NULL) return false;
    err_msg_output_and_destroy(err);
    return true;
}

MUST_CHECK Error *err_addressing(atype_t am, linepos_t epoint) {
    Error *v;
    if (am > MAX_ADDRESS_MASK) return new_error(ERROR__ADDR_COMPLEX, epoint);
    v = new_error(ERROR_NO_ADDRESSING, epoint);
    v->u.addressing = am;
    return v;
}

static Error *dump_instr(uint8_t cod, uint32_t adr, int ln, linepos_t epoint)  {
    if (diagnostics.optimize) cpu_opt(cod, adr, ln, epoint);
    if (ln >= 0) {
        uint32_t temp = adr;
        poke_pos = epoint;
        pokeb(cod);
        switch (ln) {
        case 4: pokeb(temp); temp >>= 8; /* fall through */
        case 3: pokeb(temp); temp >>= 8; /* fall through */
        case 2: pokeb(temp); temp >>= 8; /* fall through */
        case 1: pokeb(temp); /* fall through */
        default: break;
        }
    }
    listing_instr(listing, cod, adr, ln);
    return NULL;
}

MUST_CHECK Error *instruction(int prm, unsigned int w, Obj *vals, linepos_t epoint, struct linepos_s *epoints) {
    enum { AG_ZP, AG_B0, AG_PB, AG_PB2, AG_BYTE, AG_SBYTE, AG_CHAR, AG_DB3, AG_DB2, AG_WORD, AG_SWORD, AG_SINT, AG_RELPB, AG_RELL, AG_IMP, AG_NONE } adrgen;
    static unsigned int once;
    Adr_types opr;
    Reg_types reg;
    const uint8_t *cnmemonic; /* current nmemonic */
    unsigned int ln;
    uint8_t cod, longbranch;
    uint32_t adr;
    uval_t uval;
    Obj *val, *oval;
    linepos_t epoint2 = &epoints[0];
    Error *err;

    cnmemonic = opcode_table[opcode[prm]];
    longbranch = 0; reg = REG_A; adr = 0;


    if (vals->obj != ADDRLIST_OBJ) {
        oval = val = vals; goto single;
    } else {
        Addrlist *addrlist;
        atype_t am;
        switch (((Addrlist *)vals)->len) {
        case 0:
            if (cnmemonic[ADR_IMPLIED] != ____) {
                if (diagnostics.implied_reg && cnmemonic[ADR_REG] != 0) err_msg_implied_reg(epoint);
                adrgen = AG_IMP; opr = ADR_IMPLIED;
                break;
            }
            return err_addressing(A_NONE, epoint);
        case 1:
            addrlist = (Addrlist *)vals;
            oval = val = addrlist->data[0];
        single:
            val = val->obj->address(val, &am);
            switch (am) {
            case A_IMMEDIATE_SIGNED:
            case A_IMMEDIATE:
                if ((cod = cnmemonic[(opr = ADR_IMMEDIATE)]) == ____ && prm != 0) { /* 0x69 hack */
                    return err_addressing(am, epoint);
                }
                switch (cod) {
                case 0xE0:
                case 0xC0:
                case 0xA2:
                case 0xA0:  /* cpx cpy ldx ldy */
                    adrgen = (am == A_IMMEDIATE) ? (longindex ? AG_SWORD : AG_SBYTE) : (longindex ? AG_SINT : AG_CHAR);
                    break;
                case 0xF4: /* pea/phw #$ffff */
                    adrgen = (am == A_IMMEDIATE) ? AG_SWORD : AG_SINT;
                    break;
                case 0xC2:
                case 0xE2:
                case 0xEF:  /* sep rep mmu */
                    if (opcode == w65816.opcode || opcode == c65el02.opcode) {
                        if (am != A_IMMEDIATE) return err_addressing(am, epoint);
                        adrgen = AG_BYTE;
                        break;
                    }
                    /* fall through */
                case 0x00:
                case 0x02: /* brk cop */
                    adrgen = (am == A_IMMEDIATE) ? AG_SBYTE : AG_CHAR;
                    break;
                default:
                    adrgen = (am == A_IMMEDIATE) ? (longaccu ? AG_SWORD : AG_SBYTE) : (longaccu ? AG_SINT : AG_CHAR);
                }
                break;
            case (A_IMMEDIATE << 4) | A_BR: /* lda #$ffff,b */
            case A_BR:
                if (cnmemonic[ADR_ADDR] != ____ && cnmemonic[ADR_ADDR] != 0x4C && cnmemonic[ADR_ADDR] != 0x20 && cnmemonic[ADR_ADDR] != 0xF4) {/* jmp $ffff, jsr $ffff, pea */
                    adrgen = AG_WORD; opr = ADR_ADDR; /* lda $ffff,b */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 4) | A_KR:
            case A_KR:
                if (cnmemonic[ADR_REL] != ____) {
                    ln = 1; opr = ADR_REL;
                    longbranch = 0;
                    goto justrel2;
                }
                if (cnmemonic[ADR_REL_L] != ____) {
                    adrgen = AG_RELPB; opr = ADR_REL_L; /* brl */
                    break;
                }
                if (cnmemonic[ADR_ADDR] == 0x4C || cnmemonic[ADR_ADDR] == 0x20) {/* jmp $ffff, jsr $ffff */
                    adrgen = AG_WORD; opr = ADR_ADDR; /* jmp $ffff */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 4) | A_DR:           /* lda #$ff,d */
            case A_DR:
                if (cnmemonic[ADR_ZP] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP; /* lda $ff,d */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 8) | (A_BR << 4) | A_XR: /* lda #$ffff,b,x */
            case (A_BR << 4) | A_XR:
                if (cnmemonic[ADR_ADDR_X] != ____) {
                    adrgen = AG_WORD; opr = ADR_ADDR_X; /* lda $ffff,b,x */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 8) | (A_DR << 4) | A_XR: /* lda #$ff,d,x */
            case (A_DR << 4) | A_XR:
                if (cnmemonic[ADR_ZP_X] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_X; /* lda $ff,d,x */
                    break;
                }
                return err_addressing(am, epoint);
            case A_XR:
                if (cnmemonic[ADR_ZP_X] != ____ || cnmemonic[ADR_ADDR_X] != ____ || cnmemonic[ADR_LONG_X] != ____) {
                    adrgen = AG_DB3; opr = ADR_ZP_X; /* lda $ff,x lda $ffff,x lda $ffffff,x */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 8) | (A_BR << 4) | A_YR:/* ldx #$ffff,b,y */
            case (A_BR << 4) | A_YR:
                if (cnmemonic[ADR_ADDR_Y] != ____) {
                    adrgen = AG_WORD; opr = ADR_ADDR_Y; /* ldx $ffff,b,y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 8) | (A_DR << 4) | A_YR:/* ldx #$ff,d,y */
            case (A_DR << 4) | A_YR:
                if (cnmemonic[ADR_ZP_Y] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_Y; /* ldx $ff,d,y */
                    break;
                }
                return err_addressing(am, epoint);
            case A_YR: 
                if (cnmemonic[ADR_ZP_Y] != ____ || cnmemonic[ADR_ADDR_Y] != ____) {
                    adrgen = AG_DB2; opr = ADR_ZP_Y; /* lda $ff,y lda $ffff,y lda $ffffff,y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 4) | A_SR:           /* lda #$ff,s */
            case A_SR:
                if (cnmemonic[ADR_ZP_S] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_S; /* lda $ff,s */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 4) | A_RR:           /* lda #$ff,r */
            case A_RR:
                if (cnmemonic[ADR_ZP_R] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_R; /* lda $ff,r */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_DR << 8) | (A_I << 4) | A_YR:/* lda (#$ff,d),y */
            case (A_DR << 8) | (A_I << 4) | A_YR:
                if (cnmemonic[ADR_ZP_I_Y] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_I_Y; /* lda ($ff,d),y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_I << 4) | A_YR:
                if (cnmemonic[ADR_ZP_I_Y] != ____) {
                    adrgen = AG_ZP; opr = ADR_ZP_I_Y; /* lda ($ff),y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_DR << 8) | (A_I << 4) | A_ZR:/* lda (#$ff,d),z */
            case (A_DR << 8) | (A_I << 4) | A_ZR:
                if (cnmemonic[ADR_ZP_I_Z] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_I_Z; /* lda ($ff,d),z */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_I << 4) | A_ZR:
                if (cnmemonic[ADR_ZP_I_Z] != ____) {
                    adrgen = AG_ZP; opr = ADR_ZP_I_Z; /* lda ($ff),z */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_SR << 8) | (A_I << 4) | A_YR:/* lda (#$ff,s),y */
            case (A_SR << 8) | (A_I << 4) | A_YR:
                if (cnmemonic[ADR_ZP_S_I_Y] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_S_I_Y; /* lda ($ff,s),y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_RR << 8) | (A_I << 4) | A_YR:/* lda (#$ff,r),y */
            case (A_RR << 8) | (A_I << 4) | A_YR:
                if (cnmemonic[ADR_ZP_R_I_Y] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_R_I_Y; /* lda ($ff,r),y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_DR << 8) | (A_LI << 4) | A_YR:/* lda [#$ff,d],y */
            case (A_DR << 8) | (A_LI << 4) | A_YR:
                if (cnmemonic[ADR_ZP_LI_Y] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_LI_Y; /* lda [$ff,d],y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_LI << 4) | A_YR:
                if (cnmemonic[ADR_ZP_LI_Y] != ____) {
                    adrgen = AG_ZP; opr = ADR_ZP_LI_Y; /* lda [$ff],y */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_XR << 4) | A_I:
                if (cnmemonic[ADR_ADDR_X_I] == 0x7C || cnmemonic[ADR_ADDR_X_I] == 0xFC || cnmemonic[ADR_ADDR_X_I] == 0x23) {/* jmp ($ffff,x) jsr ($ffff,x) */
                    adrgen = AG_PB; opr = ADR_ADDR_X_I; /* jmp ($ffff,x) */
                    break;
                } 
                if (cnmemonic[ADR_ZP_X_I] != ____) {
                    adrgen = AG_ZP; opr = ADR_ZP_X_I; /* lda ($ff,x) */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_KR << 8) | (A_XR << 4) | A_I: /* jmp (#$ffff,k,x) */
            case (A_KR << 8) | (A_XR << 4) | A_I:
                if (cnmemonic[ADR_ADDR_X_I] == 0x7C || cnmemonic[ADR_ADDR_X_I] == 0xFC || cnmemonic[ADR_ADDR_X_I] == 0x23) {/* jmp ($ffff,x) jsr ($ffff,x) */
                    adrgen = AG_WORD; opr = ADR_ADDR_X_I; /* jmp ($ffff,k,x) */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 12) | (A_DR << 8) | (A_XR << 4) | A_I:/* lda (#$ff,d,x) */
            case (A_DR << 8) | (A_XR << 4) | A_I:
                if (cnmemonic[ADR_ZP_X_I] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_X_I; /* lda ($ff,d,x) */
                    break;
                }
                return err_addressing(am, epoint);
            case A_I:
                if (cnmemonic[ADR_ADDR_I] == 0x6C || cnmemonic[ADR_ADDR_I] == 0x22) {/* jmp ($ffff), jsr ($ffff) */
                    adrgen = AG_B0; opr = ADR_ADDR_I; /* jmp ($ffff) */
                    break;
                } 
                if (cnmemonic[ADR_ZP_I] != ____) {
                    adrgen = AG_ZP; opr = ADR_ZP_I; /* lda ($ff) */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 8) | (A_DR << 4) | A_I: /* lda (#$ff,d) */
            case (A_DR << 4) | A_I:
                if (cnmemonic[ADR_ZP_I] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_I; /* lda ($ff,d) */
                    break;
                }
                return err_addressing(am, epoint);
            case A_LI:
                if (cnmemonic[ADR_ADDR_LI] == 0xDC) { /* jmp [$ffff] */
                    adrgen = AG_B0; opr = ADR_ADDR_LI; /* jmp [$ffff] */
                    break;
                } 
                if (cnmemonic[ADR_ZP_LI] != ____) {
                    adrgen = AG_ZP; opr = ADR_ZP_LI; /* lda [$ff] */
                    break;
                }
                return err_addressing(am, epoint);
            case (A_IMMEDIATE << 8) | (A_DR << 4) | A_LI: /* lda [#$ff,d] */
            case (A_DR << 4) | A_LI:
                if (cnmemonic[ADR_ZP_LI] != ____) {
                    adrgen = AG_BYTE; opr = ADR_ZP_LI; /* lda [$ff,d] */
                    break;
                }
                return err_addressing(am, epoint);
            case A_NONE:
                goto noneaddr;
            default:
                if (am > MAX_ADDRESS_MASK) return new_error(ERROR__ADDR_COMPLEX, epoint2);
                return err_addressing(am, epoint); /* non-existing */
            }
            break;
        noneaddr:
            if (val->obj == REGISTER_OBJ) {
                Register *cpureg = (Register *)val;
                cod = cnmemonic[(opr = ADR_REG)];
                if (cod != 0 && cpureg->len == 1) {
                    const char *ind = strchr(reg_names, cpureg->data[0]);
                    if (ind != NULL) {
                        reg = (Reg_types)(ind - reg_names);
                        if (regopcode_table[cod][reg] != ____) {
                            adrgen = AG_IMP;
                            break;
                        }
                    }
                }
                err = new_error(ERROR___NO_REGISTER, epoint);
                err->u.reg = ref_register(cpureg);
                return err;
            }
            if (cnmemonic[ADR_REL] != ____) {
                struct star_s *s;
                bool labelexists;
                uint16_t xadr;
                uval_t oadr;
                bool labelexists2;
                bool crossbank;
                ln = 1; opr = ADR_REL;
                longbranch = 0;
                if (false) {
            justrel2:
                    if (touval(val, &uval, 16, epoint2)) uval = current_section->l_address.address + 1 + ln;
                    else uval &= 0xffff;
                    crossbank = false;
                } else {
            justrel: 
                    if (touval(val, &uval, all_mem_bits, epoint2)) {
                        uval = current_section->l_address.address + 1 + ln;
                        crossbank = false;
                    } else {
                        uval &= all_mem;
                        crossbank = ((uval_t)current_section->l_address.bank ^ uval) > 0xffff;
                    }
                }
                xadr = (uint16_t)adr;
                s = new_star(vline + 1, &labelexists);

                labelexists2 = labelexists;
                oadr = uval;
                if (labelexists2 && oval->obj == ADDRESS_OBJ) {
                    oval = ((Address *)oval)->val;
                }
                if (labelexists2 && oval->obj == CODE_OBJ && pass != ((Code *)oval)->apass) {
                    adr = (uint16_t)(uval - s->addr);
                } else {
                    adr = (uint16_t)(uval - current_section->l_address.address - 1 - ln); labelexists2 = false;
                }
                if ((adr<0xFF80 && adr>0x007F) || crossbank) {
                    if (cnmemonic[ADR_REL_L] != ____ && !crossbank) { /* 65CE02 long branches */
                        if (!labelexists2) adr = (uint16_t)adr; /* same + 2 offset! */
                        opr = ADR_REL_L;
                        ln = 2;
                    } else if (arguments.longbranch && (cnmemonic[ADR_ADDR] == ____)) { /* fake long branches */
                        if ((cnmemonic[ADR_REL] & 0x1f) == 0x10) {/* bxx branch */
                            bool exists;
                            struct longjump_s *lj = new_longjump(uval, &exists);
                            if (exists && lj->defpass == pass) {
                                if ((current_section->l_address.bank ^ lj->dest) <= 0xffff) {
                                    uint32_t adrk = (uint16_t)(lj->dest - current_section->l_address.address - 2);
                                    if (adrk >= 0xFF80 || adrk <= 0x007F) {
                                        adr = adrk;
                                        goto branchok;
                                    }
                                }
                            }
                            cpu_opt_long_branch(cnmemonic[ADR_REL]);
                            dump_instr(cnmemonic[ADR_REL] ^ 0x20, labelexists ? ((uint16_t)(s->addr - star - 2)) : 3, 1, epoint);
                            lj->dest = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
                            lj->defpass = pass;
                            if (diagnostics.long_branch) err_msg2(ERROR___LONG_BRANCH, NULL, epoint);
                            cpu_opt_long_branch(0xea);
                            err = instruction((cpu->brl >= 0 && !longbranchasjmp && !crossbank) ? cpu->brl : cpu->jmp, w, vals, epoint, epoints);
                            cpu_opt_long_branch(0);
                            goto branchend;
                        }
                        if (opr == ADR_BIT_ZP_REL) {
                            bool exists;
                            struct longjump_s *lj = new_longjump(uval, &exists);
                            if (crossbank) {
                                err_msg2(ERROR_CANT_CROSS_BA, val, epoint2);
                                goto branchok;
                            }
                            if (exists && lj->defpass == pass) {
                                if ((current_section->l_address.bank ^ lj->dest) <= 0xffff) {
                                    uint32_t adrk = (uint16_t)(lj->dest - current_section->l_address.address - 3);
                                    if (adrk >= 0xFF80 || adrk <= 0x007F) {
                                        adr = adrk;
                                        goto branchok;
                                    }
                                }
                            }
                            cpu_opt_long_branch(cnmemonic[ADR_BIT_ZP_REL] ^ longbranch);
                            dump_instr(cnmemonic[ADR_BIT_ZP_REL] ^ 0x80 ^ longbranch, xadr | 0x300, 2, epoint);
                            lj->dest = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
                            lj->defpass = pass;
                            if (diagnostics.long_branch) err_msg2(ERROR___LONG_BRANCH, NULL, epoint);
                            cpu_opt_long_branch(0xea);
                            err = instruction(cpu->jmp, w, val, epoint, epoints);
                            cpu_opt_long_branch(0);
                            goto branchend;
                        } else {/* bra */
                            if (cpu->brl >= 0 && !longbranchasjmp) { /* bra -> brl */
                            asbrl:
                                if (diagnostics.long_branch) err_msg2(ERROR___LONG_BRANCH, NULL, epoint);
                                cpu_opt_long_branch(cnmemonic[ADR_REL] | 0x100);
                                err = instruction(cpu->brl, w, vals, epoint, epoints);
                                cpu_opt_long_branch(0);
                                goto branchend;
                            } else if (cnmemonic[ADR_REL] == 0x82 && opcode == c65el02.opcode) { /* not a branch ! */
                                int dist = (int16_t)adr; dist += (dist < 0) ? 0x80 : -0x7f;
                                if (crossbank) {
                                    err_msg2(ERROR_CANT_CROSS_BA, val, epoint2);
                                } else err_msg2(ERROR_BRANCH_TOOFAR, &dist, epoint); /* rer not a branch */
                            } else { /* bra -> jmp */
                            asjmp:
                                if (diagnostics.long_branch) err_msg2(ERROR___LONG_BRANCH, NULL, epoint);
                                cpu_opt_long_branch(cnmemonic[ADR_REL] | 0x100);
                                err = instruction(cpu->jmp, w, vals, epoint, epoints);
                                cpu_opt_long_branch(0);
                            branchend:
                                if (labelexists && s->addr != ((current_section->l_address.address & 0xffff) | current_section->l_address.bank)) {
                                    if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, epoint);
                                    fixeddig = false;
                                }
                                s->addr = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
                                return err;
                            }
                        }
                    } else if (cnmemonic[ADR_ADDR] != ____) { /* gcc */
                        if (cpu->brl >= 0 && !longbranchasjmp) goto asbrl; /* gcc -> brl */
                        goto asjmp; /* gcc -> jmp */
                    } else { /* too long */
                        if (crossbank) {
                            err_msg2(ERROR_CANT_CROSS_BA, val, epoint2);
                        } else {
                            int dist = (int16_t)adr; dist += (dist < 0) ? 0x80 : -0x7f;
                            err_msg2(ERROR_BRANCH_TOOFAR, &dist, epoint);
                        }
                    }
                } else { /* short */
                    if (((uint16_t)(current_section->l_address.address + 1 + ln) & 0xff00) != (oadr & 0xff00)) {
                        int diff = (int8_t)oadr;
                        if (diff >= 0) diff++;
                        if (!allowslowbranch) err_msg2(ERROR__BRANCH_CROSS, &diff, epoint);
                        else if (diagnostics.branch_page) err_msg_branch_page(diff, epoint);
                    }
                    if (cnmemonic[ADR_ADDR] != ____) { /* gcc */
                        if (adr == 0) {
                            dump_instr(cnmemonic[ADR_REL], 0, -1, epoint);
                            err = NULL;
                            goto branchend;
                        } 
                        if (adr == 1) {
                            if ((cnmemonic[ADR_REL] & 0x1f) == 0x10) {
                                cpu_opt_long_branch(cnmemonic[ADR_REL] | 0x100);
                                dump_instr(cnmemonic[ADR_REL] ^ 0x20, 1, 0, epoint);
                                cpu_opt_long_branch(0);
                                err = NULL;
                                goto branchend;
                            }
                            if (cnmemonic[ADR_REL] == 0x80 && (opcode == r65c02.opcode || opcode == w65c02.opcode)) {
                                cpu_opt_long_branch(cnmemonic[ADR_REL] | 0x100);
                                dump_instr(0x82, 1, 0, epoint);
                                cpu_opt_long_branch(0);
                                err = NULL;
                                goto branchend;
                            }
                        }
                        if (adr == 2 && (opcode == c65ce02.opcode || opcode == c4510.opcode)) {
                            if ((cnmemonic[ADR_REL] & 0x1f) == 0x10) {
                                cpu_opt_long_branch(cnmemonic[ADR_REL] | 0x100);
                                dump_instr(cnmemonic[ADR_REL] ^ 0x23, 2, 0, epoint);
                                cpu_opt_long_branch(0);
                                err = NULL;
                                goto branchend;
                            }
                        }
                    }
                }
            branchok:
                if (labelexists && s->addr != ((star + 1 + ln) & all_mem)) {
                    if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, epoint);
                    fixeddig = false;
                }
                s->addr = (star + 1 + ln) & all_mem;
                if (opr == ADR_BIT_ZP_REL) adr = xadr | (adr << 8);
                adrgen = AG_NONE; break;
            }
            if (cnmemonic[ADR_REL_L] != ____) {
                adrgen = AG_RELL; opr = ADR_REL_L; /* brl */
                break;
            }
            if (cnmemonic[ADR_LONG] == 0x5C) {
                adrgen = AG_PB2; opr = ADR_ZP; /* jml */
                break;
            }
            if (cnmemonic[ADR_ADDR] == 0x20 || cnmemonic[ADR_ADDR] == 0x4C) {
                adrgen = AG_PB; opr = ADR_ADDR; /* jsr $ffff, jmp */
                break;
            }
            if (cnmemonic[ADR_ADDR] == 0xF4) {
                adrgen = AG_WORD; opr = ADR_ADDR; /* pea $ffff */
                break;
            } 
            if (cnmemonic[ADR_ZP] != ____ || cnmemonic[ADR_ADDR] != ____ || cnmemonic[ADR_LONG] != ____) {
                adrgen = AG_DB3; opr = ADR_ZP; /* lda $ff lda $ffff lda $ffffff */
                break;
            }
            if (val == &none_value->v) {
                return new_error(ERROR____STILL_NONE, epoint2);
            }
            err = new_error(ERROR___NO_LOT_OPER, epoint);
            err->u.opers = 1;
            return err;
        case 2:
            addrlist = (Addrlist *)vals;
            if (cnmemonic[ADR_MOVE] != ____) {
                val = addrlist->data[0];
                if (touval(val->obj->address(val, &am), &uval, 8, epoint2)) {}
                else {
                    if (am != A_NONE && am != A_IMMEDIATE) err_msg_output_and_destroy(err_addressing(am, epoint2));
                    else adr = (uval & 0xff) << 8;
                }
                epoint2 = &epoints[1];
                val = addrlist->data[1];
                if (touval(val->obj->address(val, &am), &uval, 8, epoint2)) {}
                else {
                    if (am != A_NONE && am != A_IMMEDIATE) err_msg_output_and_destroy(err_addressing(am, epoint2));
                    else adr |= uval & 0xff;
                }
                ln = 2; 
                adrgen = AG_NONE; opr = ADR_MOVE;
                break;
            } 
            if (cnmemonic[ADR_BIT_ZP] != ____) {
                if (w != 3 && w != 0) return new_error((w == 1) ? ERROR__NO_WORD_ADDR : ERROR__NO_LONG_ADDR, epoint);
                if (touval(addrlist->data[0], &uval, 3, epoint2)) {}
                else longbranch = ((uval & 7) << 4) & 0x70;
                oval = val = addrlist->data[1];
                epoint2 = &epoints[1];
                val = val->obj->address(val, &am);
                if (am == A_DR) {
                    adrgen = AG_BYTE;
                } else {
                    if (am != A_NONE) err_msg_output_and_destroy(err_addressing(am, epoint2));
                    adrgen = AG_ZP;
                }
                opr = ADR_BIT_ZP;
                break;
            }
            if (addrlist->data[0] == &none_value->v) {
                return new_error(ERROR____STILL_NONE, epoint2);
            }
            if (addrlist->data[1] == &none_value->v) {
                return new_error(ERROR____STILL_NONE, &epoints[1]);
            }
            err = new_error(ERROR___NO_LOT_OPER, epoint);
            err->u.opers = 2;
            return err;
        case 3:
            addrlist = (Addrlist *)vals;
            if (cnmemonic[ADR_BIT_ZP_REL] != ____) {
                if (w != 3 && w != 1) return new_error((w != 0) ? ERROR__NO_LONG_ADDR : ERROR__NO_BYTE_ADDR, epoint);
                if (touval(addrlist->data[0], &uval, 3, epoint2)) {}
                else longbranch = ((uval & 7) << 4) & 0x70;
                val = addrlist->data[1];
                epoint2 = &epoints[1];
                val = val->obj->address(val, &am);
                if (am == A_DR) {
                    if (touval(val, &uval, 8, epoint2)) {}
                    else adr = uval & 0xff;
                } else {
                    if (am != A_NONE) err_msg_output_and_destroy(err_addressing(am, epoint2));
                    else if (touval(val, &uval, all_mem_bits, epoint2)) {}
                    else {
                        uval &= all_mem;
                        if (uval <= 0xffff) {
                            adr = (uint16_t)(uval - dpage);
                            if (adr > 0xff || dpage > 0xffff) err_msg2(ERROR____NOT_DIRECT, val, epoint2);
                        } else err_msg2(ERROR_____NOT_BANK0, val, epoint2);
                    }
                }
                oval = val = addrlist->data[2];
                epoint2 = &epoints[2];
                ln = 2; opr = ADR_BIT_ZP_REL;
                val = val->obj->address(val, &am);
                if (am == A_KR) {
                    goto justrel2;
                }
                goto justrel;
            }
            /* fall through */
        default: 
            addrlist = (Addrlist *)vals;
            if (addrlist->data[0] == &none_value->v) {
                return new_error(ERROR____STILL_NONE, epoint2);
            }
            if (addrlist->data[1] == &none_value->v) {
                return new_error(ERROR____STILL_NONE, &epoints[1]);
            }
            if (addrlist->data[2] == &none_value->v) {
                return new_error(ERROR____STILL_NONE, &epoints[2]);
            }
            {
                size_t i;
                for (i = 3; i < addrlist->len; i++) {
                    if (addrlist->data[i] == &none_value->v) {
                        return new_error(ERROR____STILL_NONE, epoint);
                    }
                }
            }
            err = new_error(ERROR___NO_LOT_OPER, epoint);
            err->u.opers = addrlist->len;
            return err;
        }
    } 
    switch (adrgen) {
    case AG_ZP: /* zero page address only */
        if (w != 3 && w != 0) return new_error((w == 1) ? ERROR__NO_WORD_ADDR : ERROR__NO_LONG_ADDR, epoint);
        ln = 1;
        if (touval(val, &uval, all_mem_bits, epoint2)) break;
        uval &= all_mem;
        if (uval <= 0xffff) {
            adr = (uint16_t)(uval - dpage);
            if (adr > 0xff || dpage > 0xffff) err_msg2(ERROR____NOT_DIRECT, val, epoint2);
            break;
        }
        err_msg2(ERROR_____NOT_BANK0, val, epoint2);
        break;
    case AG_B0: /* bank 0 address only */
        if (w != 3 && w != 1) return new_error((w != 0) ? ERROR__NO_LONG_ADDR : ERROR__NO_BYTE_ADDR, epoint);
        ln = 2;
        if (touval(val, &uval, all_mem_bits, epoint2)) break;
        uval &= all_mem;
        if (uval <= 0xffff) {
            adr = uval;
            if (diagnostics.jmp_bug && cnmemonic[opr] == 0x6c && opcode != w65816.opcode && opcode != c65c02.opcode && opcode != r65c02.opcode && opcode != w65c02.opcode && opcode != c65ce02.opcode && opcode != c4510.opcode && opcode != c65el02.opcode && (~adr & 0xff) == 0) err_msg_jmp_bug(epoint);/* jmp ($xxff) */
            break;
        } 
        err_msg2(ERROR_____NOT_BANK0, val, epoint2);
        break;
    case AG_PB: /* address in program bank */
        if (w != 3 && w != 1) return new_error((w != 0) ? ERROR__NO_LONG_ADDR : ERROR__NO_BYTE_ADDR, epoint);
        ln = 2;
        if (touval(val, &uval, all_mem_bits, epoint2)) break;
        uval &= all_mem;
        if ((current_section->l_address.bank ^ uval) <= 0xffff) { 
            adr = uval;
            break; 
        }
        err_msg2(ERROR_CANT_CROSS_BA, val, epoint2);
        break;
    case AG_CHAR:
    case AG_SBYTE: /* byte only */
    case AG_BYTE: /* byte only */
        if (w != 3 && w != 0) return new_error((w == 1) ? ERROR__NO_WORD_ADDR : ERROR__NO_LONG_ADDR, epoint);
        ln = 1;
        if (adrgen == AG_CHAR) {
            if (toival(val, (ival_t *)&uval, 8, epoint2)) break;
        } else {
            if (touval(val, &uval, 8, epoint2)) {
                if (adrgen == AG_SBYTE && diagnostics.pitfalls) {
                    err = val->obj->ival(val, (ival_t *)&uval, 8, epoint2);
                    if (err != NULL) val_destroy(&err->v);
                    else if (once != pass) {
                        err_msg_immediate_note(epoint2);
                        once = pass;
                    }
                }
                break;
            }
        }
        adr = uval & 0xff;
        if (autosize && (opcode == c65el02.opcode || opcode == w65816.opcode)) {
            switch (cnmemonic[opr]) {
            case 0xC2:
                if ((adr & 0x10) != 0) longindex = true;
                if ((adr & 0x20) != 0) longaccu = true;
                break;
            case 0xE2:
                if ((adr & 0x10) != 0) longindex = false;
                if ((adr & 0x20) != 0) longaccu = false;
                break;
            }
        }
        break;
    case AG_DB3: /* 3 choice data bank */
        if (w == 3) {/* auto length */
            if (touval(val, &uval, all_mem_bits, epoint2)) w = (cnmemonic[opr - 1] != ____) ? 1 : 0;
            else {
                uval &= all_mem;
                if (cnmemonic[opr] != ____ && uval <= 0xffff && dpage <= 0xffff && (uint16_t)(uval - dpage) <= 0xff) {
                    if (diagnostics.immediate && opr == ADR_ZP && (cnmemonic[ADR_IMMEDIATE] != ____ || prm == 0) && oval->obj != CODE_OBJ && oval->obj != ADDRESS_OBJ) err_msg2(ERROR_NONIMMEDCONST, NULL, epoint2);
                    adr = uval - dpage; w = 0;
                } 
                else if (cnmemonic[opr - 1] != ____ && databank == (uval >> 16)) {adr = uval; w = 1;}
                else if (cnmemonic[opr - 2] != ____) {adr = uval; w = 2;}
                else {
                    w = (cnmemonic[opr - 1] != ____) ? 1 : 0;
                    err_msg2((w != 0) ? ERROR__NOT_DATABANK : ERROR____NOT_DIRECT, val, epoint2);
                }
            }
        } else {
            switch (w) {
            case 0:
                if (cnmemonic[opr] == ____) return new_error(ERROR__NO_BYTE_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                uval &= all_mem;
                if (uval <= 0xffff) {
                    adr = (uint16_t)(uval - dpage);
                    if (adr > 0xff || dpage > 0xffff) err_msg2(ERROR____NOT_DIRECT, val, epoint2);
                    break;
                }
                err_msg2(ERROR_____NOT_BANK0, val, epoint2);
                break;
            case 1:
                if (cnmemonic[opr - 1] == ____) return new_error(ERROR__NO_WORD_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                uval &= all_mem;
                adr = uval;
                if (databank != (uval >> 16)) err_msg2(ERROR__NOT_DATABANK, val, epoint2);
                break;
            case 2:
                if (cnmemonic[opr - 2] == ____) return new_error(ERROR__NO_LONG_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                adr = uval & all_mem;
                break;
            default: return new_error(ERROR__NO_LONG_ADDR, epoint); /* can't happen */
            }
        }
        opr = (Adr_types)(opr - w); ln = w + 1;
        break;
    case AG_DB2: /* 2 choice data bank */
        if (w == 3) {/* auto length */
            if (touval(val, &uval, all_mem_bits, epoint2)) w = (cnmemonic[opr - 1] != ____) ? 1 : 0;
            else {
                uval &= all_mem;
                if (cnmemonic[opr] != ____ && uval <= 0xffff && dpage <= 0xffff && (uint16_t)(uval - dpage) <= 0xff) {adr = uval - dpage; w = 0;}
                else if (cnmemonic[opr - 1] != ____ && databank == (uval >> 16)) {adr = uval; w = 1;}
                else {
                    w = (cnmemonic[opr - 1] != ____) ? 1 : 0;
                    err_msg2((w != 0) ? ERROR__NOT_DATABANK : ERROR____NOT_DIRECT, val, epoint2);
                }
            }
        } else {
            switch (w) {
            case 0:
                if (cnmemonic[opr] == ____) return new_error(ERROR__NO_BYTE_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                uval &= all_mem;
                if (uval <= 0xffff) {
                    adr = (uint16_t)(uval - dpage);
                    if (adr > 0xff || dpage > 0xffff) err_msg2(ERROR____NOT_DIRECT, val, epoint2);
                    break;
                } 
                err_msg2(ERROR_____NOT_BANK0, val, epoint2);
                break;
            case 1:
                if (cnmemonic[opr - 1] == ____) return new_error(ERROR__NO_WORD_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                uval &= all_mem;
                adr = uval;
                if (databank != (uval >> 16)) err_msg2(ERROR__NOT_DATABANK, val, epoint2);
                break;
            default: return new_error(ERROR__NO_LONG_ADDR, epoint);
            }
        }
        opr = (Adr_types)(opr - w); ln = w + 1;
        break;
    case AG_SINT:
    case AG_SWORD:
    case AG_WORD: /* word only */
        if (w != 3 && w != 1) return new_error((w != 0) ? ERROR__NO_LONG_ADDR : ERROR__NO_BYTE_ADDR, epoint);
        ln = 2;
        if (adrgen == AG_SINT) {
            if (toival(val, (ival_t *)&uval, 16, epoint2)) break;
        } else {
            if (touval(val, &uval, 16, epoint2)) {
                if (adrgen == AG_SWORD && diagnostics.pitfalls) {
                    err = val->obj->ival(val, (ival_t *)&uval, 16, epoint2);
                    if (err != NULL) val_destroy(&err->v);
                    else if (once != pass) {
                        err_msg_immediate_note(epoint2);
                        once = pass;
                    }
                }
                break;
            }
        }
        adr = uval & 0xffff;
        break;
    case AG_RELPB:
        if (w != 3 && w != 1) return new_error((w != 0) ? ERROR__NO_LONG_ADDR : ERROR__NO_BYTE_ADDR, epoint);
        ln = 2;
        if (touval(val, &uval, 16, epoint2)) break;
        uval &= 0xffff;
        adr = uval - current_section->l_address.address - ((opcode != c65ce02.opcode && opcode != c4510.opcode) ? 3 : 2);
        break;
    case AG_RELL:
        if (w != 3 && w != 1) return new_error((w != 0) ? ERROR__NO_LONG_ADDR : ERROR__NO_BYTE_ADDR, epoint);
        ln = 2;
        if (touval(val, &uval, all_mem_bits, epoint2)) break;
        uval &= all_mem;
        if ((current_section->l_address.bank ^ uval) <= 0xffff) {
            adr = uval - current_section->l_address.address - ((opcode != c65ce02.opcode && opcode != c4510.opcode) ? 3 : 2);
            break;
        }
        err_msg2(ERROR_CANT_CROSS_BA, val, epoint2);
        break;
    case AG_PB2:
        if (w == 3) {/* auto length */
            if (touval(val, &uval, all_mem_bits, epoint2)) w = (cnmemonic[ADR_ADDR] == ____) ? 2 : 1;
            else {
                uval &= all_mem;
                if (cnmemonic[ADR_ADDR] != ____ && (current_section->l_address.bank ^ uval) <= 0xffff) {adr = uval; w = 1;}
                else {adr = uval; w = 2;}
            }
        } else {
            switch (w) {
            case 1:
                if (cnmemonic[opr - 1] == ____) return new_error(ERROR__NO_WORD_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                uval &= all_mem;
                if ((current_section->l_address.bank ^ uval) <= 0xffff) adr = uval;
                else err_msg2(ERROR_CANT_CROSS_BA, val, epoint2);
                break;
            case 2:
                if (cnmemonic[opr - 2] == ____) return new_error(ERROR__NO_LONG_ADDR, epoint);
                if (touval(val, &uval, all_mem_bits, epoint2)) break;
                adr = uval & all_mem;
                break;
            default: return new_error(ERROR__NO_BYTE_ADDR, epoint);
            }
        }
        opr = (Adr_types)(opr - w); ln = w + 1;
        break;
    case AG_IMP:
        ln = 0;
        break;
    case AG_NONE:
        break;
    }

    cod = cnmemonic[opr];
    if (opr == ADR_REG) cod = regopcode_table[cod][reg];
    return dump_instr(cod ^ longbranch, adr, (int)ln, epoint);
}

