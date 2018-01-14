/*
    $Id: eval.c 1507 2017-04-30 14:20:11Z soci $

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

#include "eval.h"
#include <string.h>
#include "math.h"
#include "file.h"
#include "section.h"
#include "encoding.h"
#include "macro.h"
#include "variables.h"
#include "64tass.h"
#include "unicode.h"
#include "listing.h"
#include "error.h"
#include "values.h"
#include "arguments.h"
#include "optimizer.h"

#include "floatobj.h"
#include "boolobj.h"
#include "intobj.h"
#include "bitsobj.h"
#include "strobj.h"
#include "codeobj.h"
#include "bytesobj.h"
#include "addressobj.h"
#include "listobj.h"
#include "dictobj.h"
#include "registerobj.h"
#include "namespaceobj.h"
#include "operobj.h"
#include "gapobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "labelobj.h"
#include "errorobj.h"
#include "identobj.h"

size_t get_label(void) {
    uchar_t ch;
    unsigned int l;
    const struct properties_s *prop;
    const uint8_t *s = pline + lpoint.pos;
    const uint8_t *e;
    static const uint8_t typ[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0 */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 1 */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 2 */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, /* 3 */
        0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 4 */
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, /* 5 */
        0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 6 */
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, /* 7 */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 8 */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 9 */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* a */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* b */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* c */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* d */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* e */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3  /* f */
    };

    switch (typ[*s]) {
    default:
    case 1:
    case 0: return 0;
    case 2:
        e = s;
        s++;
        break;
    case 3:
        if (!arguments.to_ascii) return 0;
        l = utf8in(s, &ch);
        prop = uget_property(ch);
        if ((prop->property & id_Start) == 0) return 0;
        e = s;
        s += l;
    }
    for (;;) {
        switch (typ[*s]) {
        default:
        case 0: break;
        case 1:
        case 2: s++; continue;
        case 3:
            if (!arguments.to_ascii) break;
            l = utf8in(s, &ch);
            prop = uget_property(ch);
            if ((prop->property & (id_Continue | id_Start)) == 0) break;
            s += l;
            continue;
        }
        break;
    }
    lpoint.pos = (linecpos_t)(s - pline);
    return (size_t)(s - e);
}

static MUST_CHECK Obj *get_dec(linepos_t epoint) {
    Obj *v;
    size_t len, len2;

    v = int_from_decstr(pline + lpoint.pos, &len, &len2, epoint);
    lpoint.pos += len;
    return v;
}

static double ldexp10(double d, unsigned int expo, bool neg) {
    static const double nums[10] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
    double scal = expo < 10 ? nums[expo] : pow(10.0, (double)expo);
    return neg ? d / scal : d * scal;
}

static MUST_CHECK Obj *get_exponent(double real, linepos_t epoint) {
    uint8_t base = here() | 0x20;

    if (base == 'p' || base == 'e') {
        bool neg = false;
        neg = (pline[lpoint.pos + 1] == '-');
        if (neg || pline[lpoint.pos + 1] == '+') {
            if ((pline[lpoint.pos + 2] ^ 0x30) < 10) lpoint.pos++;
        }
        if ((pline[lpoint.pos + 1] ^ 0x30) < 10) {
            uval_t expo;
            Error *err;
            size_t len, len2;
            Obj *v;
            lpoint.pos++;
           
            v = int_from_decstr(pline + lpoint.pos, &len, &len2, epoint);
            err = v->obj->uval(v, &expo, 8 * (sizeof expo < sizeof(int) ? sizeof expo : sizeof(int)) - 1, &lpoint);
            val_destroy(v);
            lpoint.pos += len;
            if (err != NULL) return &err->v;

            if (expo != 0) real = (base == 'e') ? ldexp10(real, expo, neg) : ldexp(real, neg ? -(ival_t)expo : (ival_t)expo);
        }
    }
    return float_from_double(real, epoint);
}

static double toreal_destroy(Obj *v, linepos_t epoint) {
    Obj *err = FLOAT_OBJ->create(v, epoint);
    double real;
    if (err->obj != FLOAT_OBJ) {
        if (err == &none_value->v) err_msg_still_none(NULL, epoint);
        else if (err->obj == ERROR_OBJ) err_msg_output((Error *)err);
        real = 0;
    } else {
        real = ((Float *)err)->real;
    }
    val_destroy(err);
    val_destroy(v);
    return real;
}

static MUST_CHECK Obj *get_exponent2(Obj *v, linepos_t epoint) {
    double real;
    switch (here() | 0x20) {
    case 'e':
    case 'p':
        if (pline[lpoint.pos + 1] == '-' || pline[lpoint.pos + 1] == '+') {
            if ((pline[lpoint.pos + 2] ^ 0x30) < 10) {
                real = toreal_destroy(v, &lpoint);
                return get_exponent(real, epoint);
            }
        } else if ((pline[lpoint.pos + 1] ^ 0x30) < 10) {
            real = toreal_destroy(v, &lpoint);
            return get_exponent(real, epoint);
        }
        break;
    default: break;
    }
    return v;
}

static MUST_CHECK Obj *get_hex(linepos_t epoint) {
    size_t len, len2;
    Obj *v;

    lpoint.pos++;
    v = bits_from_hexstr(pline + lpoint.pos, &len, &len2, epoint);

    if (pline[lpoint.pos + len] == '.' && pline[lpoint.pos + len + 1] != '.') {
        double real, real2;
        real = toreal_destroy(v, &lpoint);
        lpoint.pos += len + 1;

        v = bits_from_hexstr(pline + lpoint.pos, &len, &len2, epoint);
        real2 = toreal_destroy(v, &lpoint);
        lpoint.pos += len;

        if (real2 != 0.0) real += ldexp(real2, -(int)(4*len2));
        return get_exponent(real, epoint);
    }
    lpoint.pos += len;
    return get_exponent2(v, epoint);
}

static MUST_CHECK Obj *get_bin(linepos_t epoint) {
    size_t len, len2;
    Obj *v;

    lpoint.pos++;
    v = bits_from_binstr(pline + lpoint.pos, &len, &len2, epoint);

    if (pline[lpoint.pos + len] == '.' && pline[lpoint.pos + len + 1] != '.') {
        double real, real2;
        real = toreal_destroy(v, &lpoint);
        lpoint.pos += len + 1;

        v = bits_from_binstr(pline + lpoint.pos, &len, &len2, epoint);
        real2 = toreal_destroy(v, &lpoint);
        lpoint.pos += len;

        if (real2 != 0.0) real += ldexp(real2, -(int)len2);
        return get_exponent(real, epoint);
    }
    lpoint.pos += len;
    return get_exponent2(v, epoint);
}

static MUST_CHECK Obj *get_float(linepos_t epoint) {
    size_t len, len2;
    Obj *v;

    v = int_from_decstr(pline + lpoint.pos, &len, &len2, epoint);
    if (pline[lpoint.pos + len] == '.' && pline[lpoint.pos + len + 1] != '.') {
        double real, real2;
        real = toreal_destroy(v, &lpoint);
        lpoint.pos += len + 1;

        v = int_from_decstr(pline + lpoint.pos, &len, &len2, epoint);
        real2 = toreal_destroy(v, &lpoint);
        lpoint.pos += len;

        if (real2 != 0.0) real += ldexp10(real2, (unsigned int)len2, true);
        return get_exponent(real, epoint);
    }
    lpoint.pos += len;
    return get_exponent2(v, epoint);
}

static MUST_CHECK Obj *get_string(linepos_t epoint) {
    char txt[4];
    size_t len;
    Obj *v = str_from_str(pline + lpoint.pos, &len, epoint);
    if (v->obj == STR_OBJ) {
        lpoint.pos += len;
        return v;
    }
    txt[1] = (char)here();
    txt[2] = txt[0] = txt[1] ^ ('\'' ^ '"');
    txt[3] = 0;
    lpoint.pos += len;
    err_msg2(ERROR______EXPECTED, txt, &lpoint);
    return v;
}

void touch_label(Label *tmp) {
    if (referenceit) tmp->ref = true;
    tmp->usepass = pass;
}

static uval_t bitscalc(Bits *val) {
    size_t b = val->bits;
    if (b >= 8 * sizeof(star)) return (uval_t)b;
    if ((star >> b) == 0) return (uval_t)b;
    if (star <= 0xff) return 8;
    if (star <= 0xffff) return 16;
    return all_mem_bits;
}

static uval_t bytescalc(Bytes *val) {
    size_t b = val->len < 0 ? (size_t)~val->len : (size_t)val->len;
    if (b >= 8 * sizeof(star)) return (uval_t)b;
    if ((star >> (b << 3)) == 0) return (uval_t)b;
    if (star <= 0xff) return 1;
    if (star <= 0xffff) return 2;
    return all_mem_bits >> 3;
}

MUST_CHECK Obj *get_star_value(Obj *val) {
    switch (val->obj->type) {
    case T_BITS: return (Obj *)bits_from_uval(star, bitscalc((Bits *)val));
    case T_CODE: return get_star_value(((Code *)val)->addr);
    default:
    case T_BOOL:
    case T_INT: return (Obj *)int_from_uval(star);
    case T_FLOAT: return (Obj *)new_float(star + (((Float *)val)->real - trunc(((Float *)val)->real)));
    case T_STR: return (Obj *)bytes_from_uval(star, all_mem_bits >> 3);
    case T_BYTES: return (Obj *)bytes_from_uval(star, bytescalc((Bytes *)val));
    case T_ADDRESS: return (Obj *)new_address(get_star_value(((Address *)val)->val), ((Address *)val)->type);
    }
}

static MUST_CHECK Obj *get_star(linepos_t epoint) {
    struct star_s *tmp;
    bool labelexists;

    if (diagnostics.optimize) cpu_opt_invalidate();
    tmp = new_star(vline, &labelexists);
    if (labelexists && tmp->addr != star) {
        if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, epoint);
        fixeddig = false;
    }
    tmp->addr = star;
    return get_star_value(current_section->l_address_val);
}

static size_t evxnum, evx_p;
static struct eval_context_s {
    struct values_s *values;
    size_t values_len;
    size_t values_p;
    size_t values_size;
    size_t outp, outp2;
    int gstop;
    struct values_s *o_out;
    size_t out_size;
} **evx;

static struct eval_context_s *eval;

static void extend_o_out(void) {
    size_t i;
    eval->out_size += 64;
    if (/*eval->out_size < 64 ||*/ eval->out_size > SIZE_MAX / sizeof *eval->o_out) err_msg_out_of_memory(); /* overflow */
    eval->o_out = (struct values_s *)reallocx(eval->o_out, eval->out_size * sizeof *eval->o_out);
    for (i = eval->outp + 1; i < eval->out_size; i++) eval->o_out[i].val = NULL;
}

static inline void push_oper(Obj *val, linepos_t epoint) {
    if (eval->outp >= eval->out_size) extend_o_out();
    else if (eval->o_out[eval->outp].val != NULL) val_destroy(eval->o_out[eval->outp].val);
    eval->o_out[eval->outp].val = val;
    eval->o_out[eval->outp++].epoint = *epoint;
}

static bool get_exp_compat(int stop) {/* length in bytes, defined */
    uint8_t ch;

    Obj *conv, *conv2;
    struct {
        Oper *val;
        struct linepos_s epoint;
    } o_oper[256];
    uint8_t operp = 0;
    struct linepos_s epoint, cpoint = {0, 0};
    size_t llen;
    bool first;
    str_t ident;
    Label *l;

    eval->outp = 0;
    o_oper[0].val = &o_COMMA;
rest:
    ignore();
    conv = conv2 = NULL;
    first = (here() == '(') && (stop == 3 || stop == 4);
    if (eval->outp == 0 && here() == '#') {
        conv2 = &o_HASH.v; lpoint.pos++;
    }
    switch (here()) {
    case 0:
    case ';': return true;
    case '<': conv = &o_LOWER.v; cpoint = lpoint; lpoint.pos++;break; 
    case '>': conv = &o_HIGHER.v;cpoint = lpoint; lpoint.pos++;break; 
    }
    for (;;) {
        Oper *op;
        ignore();ch = here(); epoint = lpoint;

        switch (ch) {
        case '(': op = &o_PARENT;goto add;
        case '$': push_oper(get_hex(&epoint), &epoint);goto other;
        case '%': push_oper(get_bin(&epoint), &epoint);goto other;
        case '"': push_oper(get_string(&epoint), &epoint);goto other;
        case '*': lpoint.pos++;push_oper(get_star(&epoint), &epoint);goto other;
        case '0':
            if (diagnostics.leading_zeros && pline[lpoint.pos + 1] >= '0' && pline[lpoint.pos + 1] <= '9') err_msg2(ERROR_LEADING_ZEROS, NULL, &lpoint);
            /* fall through */
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            push_oper(get_dec(&epoint), &epoint);goto other;
        default: 
            if (get_label() == 0) {
                if (operp != 0) epoint = o_oper[operp - 1].epoint;
                goto syntaxe;
            }
            break;
        }
    as_ident:
        ident.data = pline + epoint.pos;
        ident.len = lpoint.pos - epoint.pos;
        l = find_label(&ident, NULL);
        if (l != NULL) {
            if (diagnostics.case_symbol && str_cmp(&ident, &l->name) != 0) err_msg_symbol_case(&ident, l, &epoint);
            touch_label(l);
            l->shadowcheck = true;
            push_oper(val_reference(l->value), &epoint);
        } else {
            Error *err = new_error(ERROR___NOT_DEFINED, &epoint);
            err->u.notdef.ident = ident;
            err->u.notdef.names = ref_namespace(current_context);
            err->u.notdef.down = true;
            push_oper((Obj *)err, &epoint);
        }
    other:
        if (stop != 2) ignore();
        ch = here(); epoint = lpoint;

        while (operp != 0 && o_oper[operp - 1].val != &o_PARENT) {
            operp--;
            push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
        }
        switch (ch) {
        case ',':
            lpoint.pos++;
            llen = get_label();
            if (llen == 1) {
                switch (pline[epoint.pos + 1] | arguments.caseinsensitive) {
                case 'x': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_COMMAX;goto other;
                case 'y': o_oper[operp].epoint = epoint; o_oper[operp++].val = &o_COMMAY;goto other;
                default: break;
                }
            }
            if (conv != NULL) push_oper(conv, &cpoint);
            if (conv2 != NULL) push_oper(conv2, &cpoint);
            if (stop == 1) {lpoint = epoint;break;}
            if (llen != 0) {
                epoint.pos++;
                goto as_ident;
            }
            goto rest;
        case '&': op = &o_AND; goto add;
        case '.': op = &o_OR; goto add;
        case ':': op = &o_XOR; goto add;
        case '*': op = &o_MUL; goto add;
        case '/': op = &o_DIV; goto add;
        case '+': op = &o_ADD; goto add;
        case '-': op = &o_SUB; 
              add: o_oper[operp].epoint = epoint; o_oper[operp++].val = op; lpoint.pos++;continue;
        case ')':
            if (operp == 0) {err_msg2(ERROR______EXPECTED, "')' not", &lpoint); goto error;}
            lpoint.pos++;
            operp--;
            if (operp == 0 && first) {
                o_oper[operp].epoint = o_oper[operp].epoint; o_oper[operp++].val = &o_TUPLE;
                first = false;
            }
            goto other;
        case 0:
        case ';':
        case '\t':
        case ' ':
            if (conv != NULL) push_oper(conv, &cpoint);
            if (conv2 != NULL) push_oper(conv2, &cpoint);
            break;
        default: goto syntaxe;
        }
        if (operp == 0) return true;
        epoint = o_oper[operp - 1].epoint;
        err_msg2(ERROR______EXPECTED, "')'", &epoint); goto error;
    syntaxe:
        err_msg2(ERROR_EXPRES_SYNTAX, NULL, &epoint);
    error:
        break;
    }
    return false;
}

static struct values_s *extend_values(struct eval_context_s *ev, size_t by) {
    size_t j = ev->values_size;
    struct values_s *values;
    ev->values_size += by;
    if (ev->values_size < by || ev->values_size > SIZE_MAX / sizeof *values) err_msg_out_of_memory(); /* overflow */
    ev->values = values = (struct values_s *)reallocx(ev->values, ev->values_size * sizeof *values);
    for (; j < ev->values_size; j++) values[j].val = NULL;
    return values;
}

static bool get_val2_compat(struct eval_context_s *ev) {/* length in bytes, defined */
    size_t vsp = 0;
    Oper_types op;
    Oper *op2;
    size_t i;
    Obj *val;
    struct values_s *v1, *v2;
    struct values_s *o_out;
    struct values_s *values;

    ev->values_p = 0;
    values = ev->values;

    for (i = ev->outp2; i < ev->outp; i++) {
        o_out = &ev->o_out[i];
        val = o_out->val;
        if (val->obj != OPER_OBJ) {
            if (vsp >= ev->values_size) values = extend_values(ev, 16);
            o_out->val = values[vsp].val;
            values[vsp].val = val;
            values[vsp++].epoint = o_out->epoint;
            continue;
        }
        op2 = (Oper *)val;
        op = op2->op;

        if (vsp < 1) goto syntaxe;
        v1 = &values[vsp - 1];
        switch (op) {
        case O_LOWER:
        case O_HIGHER:
        case O_HASH:
        case O_COMMAX:
        case O_COMMAY:
        case O_TUPLE:
            switch (v1->val->obj->type) {
            case T_ADDRESS:
                switch (op) {
                case O_COMMAX:
                case O_COMMAY:
                case O_TUPLE:
                    {
                        Address *old = (Address *)v1->val;
                        Address *val2 = new_address(val_reference(old->val), (old->type << 4) | ((op == O_TUPLE) ? A_I : (op == O_COMMAX) ? A_XR : A_YR));
                        val_destroy(v1->val); v1->val = (Obj *)val2;
                        v1->epoint = o_out->epoint;
                        continue;
                    }
                default:break;
                }
                err_msg_invalid_oper(op2, v1->val, NULL, &o_out->epoint);
                val_replace(&v1->val, (Obj *)none_value); 
                break;
            case T_CODE:
            case T_INT:
            case T_BITS:
            case T_STR:
                {
                    uint16_t val1;
                    uval_t uval;
                    Error *err = v1->val->obj->uval(v1->val, &uval, 8 * sizeof uval, &v1->epoint);
                    if (err != NULL) {
                        val_destroy(v1->val);
                        v1->val = &err->v;
                        break;
                    }
                    val1 = (uint16_t)uval;

                    switch (op) {
                    case O_HASH:
                    case O_COMMAX:
                    case O_COMMAY:
                        {
                            Address *val2 = new_address(v1->val, (op == O_HASH) ? A_IMMEDIATE : (op == O_COMMAX) ? A_XR : A_YR);
                            v1->val = &val2->v;
                            v1->epoint = o_out->epoint;
                            continue;
                        }
                    case O_HIGHER: 
                        val1 >>= 8;
                        /* fall through */
                    case O_LOWER: 
                        val1 = (uint8_t)val1;
                        break;
                    case O_TUPLE:
                        {
                            Address *val2 = new_address(v1->val, A_I);
                            v1->val = &val2->v;
                            v1->epoint = o_out->epoint;
                            continue;
                        }
                    default: break;
                    }
                    val_destroy(v1->val);
                    v1->val = (Obj *)int_from_uval(val1);
                    break;
                }
            default:
                err_msg_invalid_oper(op2, v1->val, NULL, &o_out->epoint);
                val_replace(&v1->val, (Obj *)none_value); 
                break;
            case T_ERROR:
            case T_NONE:break;
            }
            v1->epoint = o_out->epoint;
            continue;
        default:break;
        }
        if (vsp < 2) {
        syntaxe:
            err_msg(ERROR_EXPRES_SYNTAX,NULL);
            ev->outp2 = ev->outp;
            ev->values_len = 0;
            return false;
        }
        v2 = v1; v1 = &values[--vsp - 1];
        switch (v1->val->obj->type) {
        case T_INT:
        case T_BITS:
        case T_CODE:
        case T_STR:
        case T_ADDRESS:
            switch (v2->val->obj->type) {
            case T_INT:
            case T_BITS:
            case T_CODE:
            case T_STR:
            case T_ADDRESS:
                {
                    uint16_t val1, val2;
                    uval_t uval;
                    Error *err = v1->val->obj->uval(v1->val, &uval, 8 * sizeof uval, &v1->epoint);
                    if (err != NULL) {
                        val_destroy(v1->val);
                        v1->val = &err->v;
                        continue;
                    }
                    val1 = (uint16_t)uval;
                    err = v2->val->obj->uval(v2->val, &uval, 8 * sizeof uval, &v2->epoint);
                    if (err != NULL) {
                        val_destroy(v1->val);
                        v1->val = &err->v;
                        continue;
                    }
                    val2 = (uint16_t)uval;

                    switch (op) {
                    case O_MUL: val1 *= val2; break;
                    case O_DIV: 
                        if (val2 == 0) {
                            err = new_error(ERROR_DIVISION_BY_Z, &o_out->epoint);
                            val_destroy(v1->val); v1->val = &err->v;
                            continue;
                        }
                        val1 /= val2; break;
                    case O_ADD: val1 += val2; break;
                    case O_SUB: val1 -= val2; break;
                    case O_AND: val1 &= val2; break;
                    case O_OR:  val1 |= val2; break;
                    case O_XOR: val1 ^= val2; break;
                    default: break;
                    }
                    val_destroy(v1->val);
                    v1->val = (Obj *)int_from_uval(val1);
                    continue;
                }
            default: err_msg_invalid_oper(op2, v1->val, v2->val, &o_out->epoint); break;
            case T_ERROR:
            case T_NONE:
                val_replace(&v1->val, v2->val);
                continue;
            }
            break;
        default:
            err_msg_invalid_oper(op2, v1->val, v2->val, &o_out->epoint); break;
        case T_ERROR:
        case T_NONE: continue;
        }
        val_replace(&v1->val, (Obj *)none_value);
    }
    ev->outp2 = i;
    ev->values_len = vsp;
    return true;
}

MUST_CHECK Error *indexoffs(Obj *v1, size_t len, size_t *offs, linepos_t epoint) {
    ival_t ival;
    Error *err = v1->obj->ival(v1, &ival, 8 * sizeof ival, epoint);
    if (err != NULL) return err;

    if (ival >= 0) {
        if ((uval_t)ival < len) {
            *offs = (uval_t)ival;
            return NULL;
        }
    } else {
        ival = -ival;
        if ((uval_t)ival <= len) {
            *offs = len - (uval_t)ival;
            return NULL;
        }
    }
    return new_error_key(ERROR___INDEX_RANGE, v1, epoint);
}

MUST_CHECK Obj *sliceparams(const struct List *v2, size_t len2, uval_t *olen, ival_t *offs2, ival_t *end2, ival_t *step2, linepos_t epoint) {
    Error *err;
    ival_t len, offs, end, step = 1;

    if (len2 >= (1u << (8 * sizeof(ival_t) - 1))) return (Obj *)new_error_mem(epoint); /* overflow */
    len = (ival_t)len2;
    if (v2->len > 3 || v2->len < 1) {
        err_msg_argnum(v2->len, 1, 3, epoint);
        return (Obj *)ref_none();
    }
    end = len;
    if (v2->len > 2) {
        if (v2->data[2] != &default_value->v) {
            err = v2->data[2]->obj->ival(v2->data[2], &step, 8 * sizeof step, epoint);
            if (err != NULL) return &err->v;
            if (step == 0) {
                return (Obj *)new_error(ERROR_NO_ZERO_VALUE, epoint);
            }
        }
    }
    if (v2->len > 1) {
        if (v2->data[1] == &default_value->v) end = (step > 0) ? len : -1;
        else {
            err = v2->data[1]->obj->ival(v2->data[1], &end, 8 * sizeof end, epoint);
            if (err != NULL) return &err->v;
            if (end >= 0) {
                if (end > len) end = len;
            } else {
                end += len;
                if (end < -1) end = -1;
            }
        }
    } else end = len;
    if (v2->data[0] == &default_value->v) offs = (step > 0) ? 0 : len - 1;
    else {
        ival_t minus;
        err = v2->data[0]->obj->ival(v2->data[0], &offs, 8 * sizeof offs, epoint);
        if (err != NULL) return &err->v;
        minus = (step < 0) ? -1 : 0;
        if (offs >= 0) {
            if (offs > len + minus) offs = len + minus;
        } else {
            offs += len;
        }
        if (offs < minus) offs = minus;
    }

    if (step > 0) {
        if (offs > end) offs = end;
        *olen = (uval_t)(end - offs + step - 1) / (uval_t)step;
    } else {
        if (end > offs) end = offs;
        *olen = (uval_t)(offs - end - step - 1) / (uval_t)-step;
    }

    *offs2 = offs;
    *end2 = end;
    *step2 = step;
    return NULL;
}

static MUST_CHECK Obj *apply_addressing(Obj *o1, Address_types am) {
    switch (o1->obj->type) {
    case T_ADDRESS:
        {
            Address *v1 = (Address *)o1;
            return (Obj *)new_address(val_reference(v1->val), am | (v1->type << 4));
        }
    case T_LIST:
    case T_TUPLE:
        {
            List *v1 = (List *)o1;
            List *v = (List *)val_alloc(o1->obj);
            Obj **vals = list_create_elements(v, v1->len);
            size_t i;
            for (i = 0; i < v1->len; i++) {
                vals[i] = apply_addressing(v1->data[i], am);
            }
            v->len = v1->len;
            v->data = vals;
            return &v->v;
        }
    case T_ERROR:
        err_msg_output((Error *)o1);
        return (Obj *)new_address((Obj *)ref_none(), am);
    default:
        return (Obj *)new_address(val_reference(o1), am);
    }
}

static bool get_val2(struct eval_context_s *ev) {
    size_t vsp = 0;
    size_t i;
    Oper_types op;
    Oper *op2;
    struct values_s *v1, *v2;
    bool stop = (ev->gstop == 3 || ev->gstop == 4);
    struct values_s *o_out;
    Obj *val;
    struct values_s *values;
    struct oper_s oper;
    Address_types am;

    ev->values_p = 0;
    values = ev->values;

    for (i = ev->outp2; i < ev->outp; i++) {
        o_out = &ev->o_out[i];
        val = o_out->val;
        if (val->obj != OPER_OBJ || val == &o_PARENT.v || val == &o_BRACKET.v || val == &o_BRACE.v) {
            if (vsp >= ev->values_size) values = extend_values(ev, 16);
            o_out->val = values[vsp].val;
            values[vsp].val = val;
            values[vsp++].epoint = o_out->epoint;
            continue;
        }

        if (val == &o_COMMA.v || val == &o_COLON2.v) continue;
        op2 = (Oper *)val;
        op = op2->op;
        if (vsp == 0) goto syntaxe;
        v1 = &values[vsp - 1];
        switch (op) {
        case O_FUNC:
        case O_INDEX:
            {
                unsigned int args = 0;
                Funcargs *tmp = (Funcargs *)val_alloc(FUNCARGS_OBJ);
                op = (op == O_FUNC) ? O_PARENT : O_BRACKET;
                while (v1->val->obj != OPER_OBJ || ((Oper *)v1->val)->op != op) {
                    args++;
                    if (vsp <= args) goto syntaxe;
                    v1 = &values[vsp - 1 - args];
                }
                tmp->val = &values[vsp - args];
                tmp->len = args; /* assumes no referencing */
                if (v1 == values) goto syntaxe;
                v1--;

                oper.op = op2;
                oper.v1 = v1->val;
                oper.v2 = (Obj *)tmp;
                oper.epoint = &v1->epoint;
                oper.epoint2 = (args != 0) ? &tmp->val->epoint : &o_out->epoint;
                oper.epoint3 = &o_out->epoint;
                if (op == O_BRACKET) {
                    val = oper.v1->obj->slice(oper.v1, &oper, 0);
                } else {
                    val = oper.v1->obj->calc2(&oper);
                }
                val_destroy(&tmp->v);
                val_destroy(v1->val); v1->val = val;

                vsp -= args + 1;
                continue;
            }
        case O_RBRACKET:
        case O_RPARENT:
        case O_TUPLE:
        case O_LIST:
            {
                List *list;
                bool tup = (op == O_RPARENT), expc = (op == O_TUPLE || op == O_LIST);
                size_t args = 0;
                op = (op == O_RBRACKET || op == O_LIST) ? O_BRACKET : O_PARENT;
                while (v1->val->obj != OPER_OBJ || ((Oper *)v1->val)->op != op) {
                    args++;
                    if (vsp <= args) goto syntaxe;
                    v1 = &values[vsp - 1 - args];
                }
                if (args == 1) {
                    if (stop && !expc) {
                        size_t j = i + 1;
                        vsp--;
                        if (tup && j < ev->outp) {
                            Obj *obj = ev->o_out[j].val;
                            if (obj->obj != OPER_OBJ ||
                                    (obj != &o_RPARENT.v &&   /* ((3)) */
                                     obj != &o_RBRACKET.v &&  /* [(3)] */
                                     obj != &o_FUNC.v &&      /* f((3)) */
                                     obj != &o_LIST.v &&      /* [(3),] */
                                     obj != &o_COMMA.v &&     /* [(3),(3)] */
                                     !(((Oper *)obj)->op >= O_COMMAX && ((Oper *)obj)->op <= O_COMMAK) /* (3),y */
                                    )) {
                                v1->val = values[vsp].val; 
                                values[vsp].val = NULL;
                                continue;
                            }
                        }
                        am = (op == O_BRACKET) ? A_LI : A_I;
                        v1->val = apply_addressing(values[vsp].val, am);
                        continue;
                    }
                    if (tup) {
                        vsp--;
                        v1->val = values[vsp].val; 
                        values[vsp].val = NULL;
                        continue;
                    }
                }
                if (args != 0) {
                    list = (List *)val_alloc((op == O_BRACKET) ? LIST_OBJ : TUPLE_OBJ);
                    list->len = args;
                    list->data = list_create_elements(list, args);
                    while ((args--) != 0) {
                        v2 = &values[vsp - 1];
                        if (v2->val->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)v2->val); v2->val = (Obj *)ref_none(); }
                        list->data[args] = v2->val;
                        v2->val = NULL;
                        vsp--;
                    }
                } else list = (List *)val_reference((op == O_BRACKET) ? &null_list->v : &null_tuple->v);
                v1->val = (Obj *)list;
                continue;
            }
        case O_RBRACE:
        case O_DICT:
            {
                unsigned int args = 0;
                while (v1->val->obj != OPER_OBJ || ((Oper *)v1->val)->op != O_BRACE) {
                    args++;
                    if (vsp <= args) goto syntaxe;
                    v1 = &values[vsp - 1 - args];
                }
                vsp -= args;
                v1->val = dictobj_parse(&values[vsp], args);
                continue;
            }
        case O_COND:
            v2 = v1; vsp--;
            if (vsp == 0) goto syntaxe;
            v1 = &values[vsp - 1]; vsp--;
            if (vsp == 0) goto syntaxe;
            val = values[vsp - 1].val;
            if (val == &true_value->v) {
            cond_true:
                values[vsp - 1].val = v1->val;
                values[vsp - 1].epoint = v1->epoint;
                v1->val = val;
                continue;
            } 
            if (val == &false_value->v) {
            cond_false:
                values[vsp - 1].val = v2->val;
                values[vsp - 1].epoint = v2->epoint;
                v2->val = val;
                continue;
            }
            {
                Obj *tmp = val->obj->truth(val, TRUTH_BOOL, &values[vsp - 1].epoint);
                if (tmp->obj != BOOL_OBJ) {
                    val_destroy(val);
                    values[vsp - 1].val = tmp;
                    continue;
                }
                val_destroy(tmp);
                if (diagnostics.strict_bool) err_msg_bool(ERROR_____CANT_BOOL, val, &values[vsp - 1].epoint);
                if (tmp == &true_value->v) goto cond_true;
                goto cond_false;
            }
        case O_QUEST:
            vsp--;
            if (vsp == 0) goto syntaxe;
            v1 = &values[vsp - 1];
            err_msg2(ERROR______EXPECTED,"':'", &o_out->epoint);
            val_replace(&v1->val, (Obj *)none_value);
            continue;
        case O_COLON:
            v2 = v1; v1 = &values[--vsp - 1];
            if (vsp == 0) goto syntaxe;
            switch (v1->val->obj->type) {
            case T_COLONLIST:
                if (v1->val->refcount == 1) {
                    Colonlist *l1 = (Colonlist *)v1->val;
                    Colonlist *list = new_colonlist();
                    if (v2->val->obj == COLONLIST_OBJ && v2->val->refcount == 1) {
                        Colonlist *l2 = (Colonlist *)v2->val;
                        list->len = l1->len + l2->len;
                        if (list->len < l2->len) err_msg_out_of_memory(); /* overflow */
                        list->data = list_create_elements(list, list->len);
                        memcpy(list->data, l1->data, l1->len * sizeof *list->data);
                        memcpy(list->data + l1->len, l2->data, l2->len * sizeof *list->data);
                        l1->len = 0;
                        l2->len = 0;
                        val_destroy(v1->val); v1->val = (Obj *)list;
                        continue;
                    }
                    list->len = l1->len + 1;
                    if (list->len < 1) err_msg_out_of_memory(); /* overflow */
                    list->data = list_create_elements(list, list->len);
                    memcpy(list->data, l1->data, l1->len * sizeof *list->data);
                    list->data[l1->len] = v2->val;
                    l1->len = 0;
                    v2->val = v1->val;
                    v1->val = (Obj *)list;
                    continue;
                }
                /* fall through */
            default:
                switch (v2->val->obj->type) {
                case T_COLONLIST:
                    if (v2->val->refcount == 1) {
                        Colonlist *l2 = (Colonlist *)v2->val;
                        Colonlist *list = new_colonlist();
                        list->len = l2->len + 1;
                        if (list->len < 1) err_msg_out_of_memory(); /* overflow */
                        list->data = list_create_elements(list, list->len);
                        list->data[0] = v1->val;
                        memcpy(&list->data[1], l2->data, l2->len * sizeof *list->data);
                        v1->val = (Obj *)list;
                        l2->len = 0;
                        continue;
                    }
                    /* fall through */
                default: 
                    {
                        Colonlist *list = new_colonlist();
                        list->len = 2;
                        list->data = list_create_elements(list, 2);
                        list->data[0] = v1->val;
                        list->data[1] = v2->val;
                        v1->val = (Obj *)list;
                        v2->val = NULL;
                        continue;
                    }
                case T_ERROR:
                    val_replace(&v1->val, v2->val);
                    continue;
                }
            case T_ERROR:continue;
            }
        case O_WORD:    /* <> */
        case O_HWORD:   /* >` */
        case O_BSWORD:  /* >< */
        case O_LOWER:   /* <  */
        case O_HIGHER:  /* >  */
        case O_BANK:    /* `  */
        case O_STRING:  /* ^  */
        case O_INV:     /* ~  */
        case O_NEG:     /* -  */
        case O_POS:     /* +  */
        case O_LNOT:    /* !  */
            oper.op = op2;
            oper.v1 = v1->val;
            oper.v2 = NULL;
            oper.epoint = &v1->epoint;
            oper.epoint3 = &o_out->epoint;
            val = oper.v1->obj->calc1(&oper);
            val_destroy(v1->val); v1->val = val;
            v1->epoint = o_out->epoint;
            continue;
        case O_COMMAS: am = A_SR; goto addr;                    /* ,s */
        case O_COMMAR: am = A_RR; goto addr;                    /* ,r */
        case O_COMMAZ: am = A_ZR; goto addr;                    /* ,z */
        case O_COMMAY: am = A_YR; goto addr;                    /* ,y */
        case O_COMMAX: am = A_XR; goto addr;                    /* ,x */
        case O_COMMAD: am = A_DR; goto addr;                    /* ,d */
        case O_COMMAB: am = A_BR; goto addr;                    /* ,b */
        case O_COMMAK: am = A_KR; goto addr;                    /* ,k */
        case O_HASH_SIGNED: am = A_IMMEDIATE_SIGNED; goto addr; /* #+ */
        case O_HASH: am = A_IMMEDIATE;                          /* #  */
        addr:
            val = apply_addressing(v1->val, am);
            val_destroy(v1->val); v1->val = val;
            if (op == O_HASH || op == O_HASH_SIGNED) v1->epoint = o_out->epoint;
            continue;
        case O_SPLAT:   /* *  */
            if (i + 1 < ev->outp) {
                Obj *o = ev->o_out[i + 1].val;
                if (o != &o_RPARENT.v && o != &o_RBRACKET.v && o != &o_RBRACE.v && o != &o_FUNC.v && o != &o_INDEX.v && o != &o_COMMA.v) {
                    err_msg2(ERROR_EXPRES_SYNTAX, NULL, &o_out->epoint);
                    val_replace(&v1->val, (Obj *)none_value);
                    continue;
                }
            }
            if (v1->val->obj == TUPLE_OBJ || v1->val->obj == LIST_OBJ || v1->val->obj == ADDRLIST_OBJ) {
                List *tmp = (List *)v1->val;
                size_t k, len = tmp->len;
                size_t len2 = vsp + len;
                if (len2 < len) err_msg_out_of_memory(); /* overflow */
                v1->val = NULL;
                vsp--;
                if (len2 >= ev->values_size) values = extend_values(ev, len);
                for (k = 0; k < len; k++) {
                    if (values[vsp].val != NULL) val_destroy(values[vsp].val);
                    values[vsp].val = (tmp->v.refcount == 1) ? tmp->data[k] : val_reference(tmp->data[k]);
                    values[vsp++].epoint = o_out->epoint;
                }
                if (tmp->v.refcount == 1) tmp->len = 0;
                val_destroy(&tmp->v);
                continue;
            }
            if (v1->val->obj == DICT_OBJ) {
                Dict *tmp = (Dict *)v1->val;
                const struct avltree_node *n;
                size_t len = (tmp->def == NULL) ? tmp->len : tmp->len + 1;
                size_t len2 = vsp + len;
                if (len < tmp->len || len2 < len) err_msg_out_of_memory(); /* overflow */
                v1->val = NULL;
                vsp--;
                if (len2 >= ev->values_size) values = extend_values(ev, len);
                for (n = avltree_first(&tmp->members); n != NULL; n = avltree_next(n)) {
                    const struct pair_s *p = cavltree_container_of(n, struct pair_s, node);
                    if (p->data == NULL) values[vsp].val = val_reference(p->key);
                    else {
                        Colonlist *list = new_colonlist();
                        list->len = 2;
                        list->data = list_create_elements(list, 2);
                        list->data[0] = val_reference(p->key);
                        list->data[1] = val_reference(p->data);

                        if (values[vsp].val != NULL) val_destroy(values[vsp].val);
                        values[vsp].val = (Obj *)list;
                    }
                    values[vsp++].epoint = o_out->epoint;
                }
                if (tmp->def != NULL) {
                    Colonlist *list = new_colonlist();
                    list->len = 2;
                    list->data = list_create_elements(list, 2);
                    list->data[0] = (Obj *)ref_default();
                    list->data[1] = val_reference(tmp->def);

                    if (values[vsp].val != NULL) val_destroy(values[vsp].val);
                    values[vsp].val = (Obj *)list;
                    values[vsp++].epoint = o_out->epoint;
                }
                val_destroy(&tmp->v);
                continue;
            }
            v1->epoint = o_out->epoint;
            continue;
        case O_LXOR: /* ^^ */
            v2 = v1; v1 = &values[--vsp - 1];
            if (vsp == 0) goto syntaxe;
            val = v1->val->obj->truth(v1->val, TRUTH_BOOL, &v1->epoint);
            if (val->obj != BOOL_OBJ) {
                val_destroy(v1->val); v1->val = val;
                continue;
            }
            if (diagnostics.strict_bool && v1->val->obj != BOOL_OBJ) err_msg_bool(ERROR_____CANT_BOOL, v1->val, &v1->epoint); /* TODO */
            {
                Obj *val2 = v2->val->obj->truth(v2->val, TRUTH_BOOL, &v2->epoint);
                if (val2->obj != BOOL_OBJ) {
                    val_destroy(v1->val); v1->val = val2;
                    val_destroy(val);
                    continue;
                }
                if (diagnostics.strict_bool && v2->val->obj != BOOL_OBJ) err_msg_bool(ERROR_____CANT_BOOL, v2->val, &v2->epoint); /* TODO */
                if ((Bool *)val == true_value) {
                    if ((Bool *)val2 == true_value) val_replace(&v1->val, &false_value->v);
                } else {
                    val_replace(&v1->val, (Bool *)val2 == true_value ? v2->val : &false_value->v);
                }
                val_destroy(val2);
            }
            val_destroy(val);
            continue;
        case O_MIN: /* <? */
        case O_MAX: /* >? */
            v2 = v1; v1 = &values[--vsp - 1];
            if (vsp == 0) goto syntaxe;
            oper.op = op2;
            oper.v1 = v1->val;
            oper.v2 = v2->val;
            oper.epoint = &v1->epoint;
            oper.epoint2 = &v2->epoint;
            oper.epoint3 = &o_out->epoint;
            val = oper.v1->obj->calc2(&oper);
            if (val->obj != BOOL_OBJ) {
                val_destroy(v1->val); v1->val = val;
                continue;
            }
            if (val != &true_value->v) {
                val_replace(&v1->val, v2->val);
            }
            val_destroy(val);
            continue;
        default: break;
        }
        v2 = v1; v1 = &values[--vsp - 1];
        if (vsp == 0) {
        syntaxe:
            err_msg(ERROR_EXPRES_SYNTAX, NULL);
            ev->outp2 = ev->outp;
            ev->values_len = 0;
            return false;
        }

        oper.op = op2;
        oper.v1 = v1->val;
        oper.v2 = v2->val;
        oper.epoint = &v1->epoint;
        oper.epoint2 = &v2->epoint;
        oper.epoint3 = &o_out->epoint;
        val = oper.v1->obj->calc2(&oper);
        val_destroy(v1->val); v1->val = val;
    }
    ev->outp2 = i;
    ev->values_len = vsp;
    return true;
}

struct values_s *get_val(void) {
    if (eval->values_p >= eval->values_len) return NULL;
    return &eval->values[eval->values_p++];
}

Obj *pull_val(struct linepos_s *epoint) {
    Obj *val;
    struct values_s *value;

    if (eval->values_p >= eval->values_len) return NULL;

    value = &eval->values[eval->values_p];
    if (epoint != NULL) *epoint = value->epoint;
    val = value->val;
    eval->values[eval->values_p++].val = NULL;
    return val;
}

size_t get_val_remaining(void) {
    return eval->values_len - eval->values_p;
}


/* 0 - normal */
/* 1 - 1 only, till comma */
/* 2 - 1 only, till space  */
/* 3 - opcode */
/* 4 - opcode, with defaults */

static bool get_exp2(int stop, struct file_s *cfile) {
    uint8_t ch;

    Oper *op;
    struct {
        Oper *val;
        struct linepos_s epoint;
    } o_oper[256];
    uint8_t operp = 0, db;
    unsigned int prec;
    struct linepos_s epoint;
    size_t llen;
    size_t openclose, identlist;

    eval->gstop = stop;
    eval->outp2 = 0;
    eval->values_p = eval->values_len = 0;

    if (arguments.tasmcomp) {
        if (get_exp_compat(stop)) return get_val2_compat(eval);
        return false;
    }
    eval->outp = 0;
    o_oper[0].val = &o_COMMA;

    openclose = identlist = 0;

    ignore();
    ch = here();
    if (ch == 0 || ch == ';') return true;
    for (;;) {
        ignore(); ch = here(); epoint = lpoint;
        switch (ch) {
        case ',':
            if (stop != 4 || operp != 0) goto tryanon;
            lpoint.pos++;push_oper((Obj *)ref_default(), &epoint);
            continue;
        case ')':
            if (operp != 0) {
                const Oper *o = o_oper[operp - 1].val;
                if (o == &o_COMMA) {operp--;op = &o_TUPLE;goto tphack;}
                else if (o == &o_PARENT || o == &o_FUNC) goto other;
            }
            goto tryanon;
        case ']':
            if (operp != 0) { 
                const Oper *o = o_oper[operp - 1].val;
                if (o == &o_COMMA) {operp--;op = &o_LIST;goto lshack;}
                else if (o == &o_BRACKET || o == &o_INDEX) goto other;
            }
            goto tryanon;
        case '}':
            if (operp != 0) { 
                const Oper *o = o_oper[operp - 1].val;
                if (o == &o_COMMA) {operp--;op = &o_DICT;goto brhack;}
                else if (o == &o_BRACE) goto other;
            }
            goto tryanon;
        case ':':
            if (operp != 0) { 
                const Oper *o = o_oper[operp - 1].val;
                if (o != &o_PARENT && o != &o_BRACKET && o != &o_BRACE && o != &o_FUNC && o != &o_INDEX && o != &o_COMMA) goto tryanon;
            }
            push_oper((Obj *)ref_default(), &epoint);
            goto other;
        case '(': 
            if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) identlist++;
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_PARENT; lpoint.pos++;
            push_oper(&o_PARENT.v, &epoint);
            openclose++;
            continue;
        case '[':
            if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) identlist++;
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_BRACKET; lpoint.pos++;
            push_oper(&o_BRACKET.v, &epoint);
            openclose++;
            continue;
        case '{':
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_BRACE; lpoint.pos++;
            push_oper(&o_BRACE.v, &epoint);
            openclose++;
            continue;
        case '+': op = &o_POS; break;
        case '-': op = &o_NEG; break;
        case '*': op = &o_SPLAT; break;
        case '!': op = &o_LNOT;break;
        case '~': op = &o_INV; break;
        case '<': if (pline[lpoint.pos + 1] == '>') {lpoint.pos++;op = &o_WORD;} else op = &o_LOWER; break;
        case '>': if (pline[lpoint.pos + 1] == '`') {lpoint.pos++;op = &o_HWORD;} else if (pline[lpoint.pos + 1] == '<') {lpoint.pos++;op = &o_BSWORD;} else op = &o_HIGHER; break;
        case '#': op = (pline[lpoint.pos + 1] == '+' || pline[lpoint.pos + 1] == '-') ? &o_HASH_SIGNED : &o_HASH; break;
        case '`': op = &o_BANK; break;
        case '^': op = &o_STRING; if (diagnostics.deprecated) err_msg2(ERROR____OLD_STRING, NULL, &lpoint); break;
        case '$': push_oper(get_hex(&epoint), &epoint);goto other;
        case '%': if ((pline[lpoint.pos + 1] & 0xfe) == 0x30 || (pline[lpoint.pos + 1] == '.' && (pline[lpoint.pos + 2] & 0xfe) == 0x30)) { push_oper(get_bin(&epoint), &epoint);goto other; }
                  goto tryanon;
        case '"':
        case '\'': push_oper(get_string(&epoint), &epoint);goto other;
        case '?': 
            if (operp != 0) {
                const Oper *o = o_oper[operp - 1].val;
                if (o == &o_SPLAT || o == &o_POS || o == &o_NEG) goto tryanon;
            }
            lpoint.pos++;push_oper((Obj *)ref_gap(), &epoint);goto other;
        case '.': if ((pline[lpoint.pos + 1] ^ 0x30) >= 10) goto tryanon; /* fall through */
        case '0': 
            if (diagnostics.leading_zeros && pline[lpoint.pos + 1] >= '0' && pline[lpoint.pos + 1] <= '9') err_msg2(ERROR_LEADING_ZEROS, NULL, &lpoint);
            /* fall through */
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            push_oper(get_float(&epoint), &epoint);
            goto other;
        case 0:
        case ';': 
            if (openclose != 0) {
                listing_line(listing, 0);
                if (!mtranslate(cfile)) { /* expand macro parameters, if any */
                    continue;
                }
            }
            goto tryanon;
        default: 
            if (get_label() != 0) {
                bool down;
                Label *l;
                Error *err;
                str_t ident;
            as_ident:
                if (pline[epoint.pos + 1] == '"' || pline[epoint.pos + 1] == '\'') {
                    Textconv_types mode;
                    switch (pline[epoint.pos] | arguments.caseinsensitive) {
                    case 'n': mode = BYTES_MODE_NULL; break;
                    case 's': mode = BYTES_MODE_SHIFT; break;
                    case 'p': mode = BYTES_MODE_PTEXT; break;
                    case 'l': mode = BYTES_MODE_SHIFTL; break;
                    case 'b': mode = BYTES_MODE_TEXT; break;
                    default: mode = BYTES_MODE_NULL_CHECK; break;
                    }
                    if (mode != BYTES_MODE_NULL_CHECK) {
                        Obj *str = get_string(&epoint), *val;
                        if (str->obj == STR_OBJ) {
                            epoint.pos++;
                            val = bytes_from_str((Str *)str, &epoint, mode);
                            epoint.pos--;
                            push_oper(val, &epoint);
                            val_destroy(str);
                        } else push_oper(str, &epoint);
                        goto other;
                    }
                }
                if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) {
                    Ident *idn = (Ident *)val_alloc(IDENT_OBJ);
                    idn->name.data = pline + epoint.pos;
                    idn->name.len = lpoint.pos - epoint.pos;
                    idn->epoint = epoint;
                    push_oper(&idn->v, &epoint);
                    goto other;
                } 
                ident.data = pline + epoint.pos;
                ident.len = lpoint.pos - epoint.pos;
                down = (ident.data[0] != '_');
                l = down ? find_label(&ident, NULL) : find_label2(&ident, cheap_context);
                if (l != NULL) {
                    if (diagnostics.case_symbol && str_cmp(&ident, &l->name) != 0) err_msg_symbol_case(&ident, l, &epoint);
                    touch_label(l);
                    if (down) l->shadowcheck = true;
                    push_oper(val_reference(l->value), &epoint);
                    goto other;
                }
                err = new_error(ERROR___NOT_DEFINED, &epoint);
                err->u.notdef.ident = ident;
                err->u.notdef.names = ref_namespace(down ? current_context : cheap_context);
                err->u.notdef.down = down;
                push_oper(&err->v, &epoint);
                goto other;
            }
        tryanon:
            db = operp;
            while (operp != 0 && o_oper[operp - 1].val == &o_POS) operp--;
            if (db != operp) {
                Label *l;
                Error *err;
                if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) {
                    Anonident *anonident = (Anonident *)val_alloc(ANONIDENT_OBJ);
                    anonident->count = db - operp - 1;
                    anonident->epoint = o_oper[operp].epoint;
                    push_oper(&anonident->v, &o_oper[operp].epoint);
                    goto other;
                }
                l = find_anonlabel(db - operp -1);
                if (l != NULL) {
                    touch_label(l);
                    push_oper(val_reference(l->value), &o_oper[operp].epoint);
                    goto other;
                }
                err = new_error(ERROR___NOT_DEFINED, &o_oper[operp].epoint);
                err->u.notdef.ident.len = (size_t)((ssize_t)db - (ssize_t)operp);
                err->u.notdef.ident.data = NULL;
                err->u.notdef.names = ref_namespace(current_context);
                err->u.notdef.down = true;
                push_oper(&err->v, &o_oper[operp].epoint);
                goto other;
            }
            while (operp != 0 && o_oper[operp - 1].val == &o_NEG) operp--;
            if (db != operp) {
                Label *l;
                Error *err;
                if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) {
                    Anonident *anonident = (Anonident *)val_alloc(ANONIDENT_OBJ);
                    anonident->count = operp - db;
                    anonident->epoint = o_oper[operp].epoint;
                    push_oper(&anonident->v, &o_oper[operp].epoint);
                    goto other;
                }
                l = find_anonlabel(operp - db);
                if (l != NULL) {
                    touch_label(l);
                    push_oper(val_reference(l->value), &o_oper[operp].epoint);
                    goto other;
                }
                err = new_error(ERROR___NOT_DEFINED, &o_oper[operp].epoint);
                err->u.notdef.ident.len = (size_t)((ssize_t)operp - (ssize_t)db);
                err->u.notdef.ident.data = NULL;
                err->u.notdef.names = ref_namespace(current_context);
                err->u.notdef.down = true;
                push_oper(&err->v, &o_oper[operp].epoint);
                goto other;
            }
            if (operp != 0) {
                if (o_oper[operp - 1].val == &o_COLON) {
                    push_oper((Obj *)ref_default(), &epoint);
                    goto other;
                }
                if (o_oper[operp - 1].val == &o_SPLAT) {
                    operp--;
                    if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) {
                        Ident *idn = (Ident *)val_alloc(IDENT_OBJ);
                        idn->name.data = pline + o_oper[operp].epoint.pos;
                        idn->name.len = 1;
                        idn->epoint = o_oper[operp].epoint;
                        push_oper(&idn->v, &o_oper[operp].epoint);
                        goto other;
                    } 
                    push_oper(get_star(&o_oper[operp].epoint), &o_oper[operp].epoint);
                    goto other;
                }
                epoint = o_oper[operp - 1].epoint;
            }
            goto syntaxe;
        }
        if (operp != 0 && o_oper[operp - 1].val == &o_SPLAT) {
            operp--;
            lpoint.pos = epoint.pos;
            if ((operp != 0 && o_oper[operp - 1].val == &o_MEMBER) || identlist != 0) {
                Ident *idn = (Ident *)val_alloc(IDENT_OBJ);
                idn->name.data = pline + o_oper[operp].epoint.pos;
                idn->name.len = 1;
                idn->epoint = o_oper[operp].epoint;
                push_oper(&idn->v, &o_oper[operp].epoint);
                goto other;
            } 
            push_oper(get_star(&o_oper[operp].epoint), &o_oper[operp].epoint);
            goto other;
        }
        lpoint.pos++;
    rtl:
        o_oper[operp].epoint = epoint;
        o_oper[operp++].val = op;
        continue;
    other:
        if (stop != 2 || openclose != 0) ignore();
        ch = here();epoint = lpoint;
        switch (ch) {
        case ',':
            lpoint.pos++;
            llen = get_label();
            if (llen == 1) {
                switch (pline[epoint.pos + 1] | arguments.caseinsensitive) {
                case 'x': op = &o_COMMAX; break;
                case 'y': op = &o_COMMAY; break;
                case 'z': op = &o_COMMAZ; break;
                case 'r': op = &o_COMMAR; break;
                case 's': op = &o_COMMAS; break;
                case 'd': op = &o_COMMAD; break;
                case 'b': op = &o_COMMAB; break;
                case 'k': op = &o_COMMAK; break;
                default: op = &o_COMMA; break;
                }
            } else op = &o_COMMA;
            prec = op->prio;
            if (op == &o_COMMA) {
                while (operp != 0 && prec <= o_oper[operp - 1].val->prio) {
                    operp--;
                    push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
                }
            } else {
                while (operp != 0 && prec <= o_oper[operp - 1].val->prio && o_oper[operp - 1].val != &o_COLON2 && o_oper[operp - 1].val != &o_COND) {
                    operp--;
                    push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
                }
                o_oper[operp].epoint = epoint;
                o_oper[operp++].val = op;
                goto other;
            }
            if (operp == 0) {
                if (stop == 1) {lpoint = epoint;break;}
            }
            push_oper(&o_COMMA.v, &epoint);
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = op;
            if (llen != 0) {
                epoint.pos++;
                goto as_ident;
            }
            continue;
        case '(':
            prec = o_MEMBER.prio;
            while (operp != 0 && prec <= o_oper[operp - 1].val->prio) {
                operp--;
                push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
            }
            push_oper(&o_PARENT.v, &epoint);
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_FUNC; lpoint.pos++;
            if (identlist != 0) identlist++;
            openclose++;
            continue;
        case '[':
            prec = o_MEMBER.prio;
            while (operp != 0 && prec <= o_oper[operp - 1].val->prio) {
                operp--;
                push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
            }
            push_oper(&o_BRACKET.v, &epoint);
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = &o_INDEX; lpoint.pos++;
            if (identlist != 0) identlist++;
            openclose++;
            continue;
        case '&': op = pline[lpoint.pos + 1] == '&' ? (pline[lpoint.pos + 2] == '=' ? &o_LAND_ASSIGN : &o_LAND) : (pline[lpoint.pos + 1] == '=' ? &o_AND_ASSIGN : &o_AND); goto push2;
        case '|': op = pline[lpoint.pos + 1] == '|' ? (pline[lpoint.pos + 2] == '=' ? &o_LOR_ASSIGN : &o_LOR) : (pline[lpoint.pos + 1] == '=' ? &o_OR_ASSIGN : &o_OR); goto push2;
        case '^': op = pline[lpoint.pos + 1] == '^' ? &o_LXOR : (pline[lpoint.pos + 1] == '=' ? &o_XOR_ASSIGN : &o_XOR); goto push2;
        case '*': op = pline[lpoint.pos + 1] == '*' ? (pline[lpoint.pos + 2] == '=' ? &o_EXP_ASSIGN : &o_EXP) : (pline[lpoint.pos + 1] == '=' ? &o_MUL_ASSIGN : &o_MUL); if (op == &o_EXP) {lpoint.pos+=2; goto rtl;} goto push2;
        case '%': op = pline[lpoint.pos + 1] == '=' ? &o_MOD_ASSIGN : &o_MOD; goto push2;
        case '/': if (pline[lpoint.pos + 1] == '/') {if (diagnostics.deprecated) err_msg2(ERROR____OLD_MODULO, NULL, &lpoint);lpoint.pos++;op = &o_MOD;} else op = pline[lpoint.pos + 1] == '=' ? &o_DIV_ASSIGN : &o_DIV; goto push2;
        case '+': op = pline[lpoint.pos + 1] == '=' ? &o_ADD_ASSIGN : &o_ADD; goto push2;
        case '-': op = pline[lpoint.pos + 1] == '=' ? &o_SUB_ASSIGN : &o_SUB; goto push2;
        case '.': op = pline[lpoint.pos + 1] == '.' ? (pline[lpoint.pos + 2] == '=' ? &o_CONCAT_ASSIGN : &o_CONCAT) : (pline[lpoint.pos + 1] == '=' ? &o_MEMBER_ASSIGN : &o_MEMBER); goto push2;
        case '?': lpoint.pos++;op = &o_QUEST; prec = o_COND.prio + 1; goto push3;
        case ':': if (pline[lpoint.pos + 1] == '=') {op = &o_COLON_ASSIGN;goto push2;}
            op = &o_COLON;
            prec = op->prio + 1;
            while (operp != 0 && prec <= o_oper[operp - 1].val->prio) {
                operp--;
                push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
            }
            if (operp != 0 && o_oper[operp - 1].val == &o_QUEST) { o_oper[operp - 1].val = &o_COND; op = &o_COLON2;}
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = op;
            lpoint.pos++;
            continue;
        case '=': op = &o_EQ; if (pline[lpoint.pos + 1] != '=') {if (diagnostics.old_equal) err_msg2(ERROR_____OLD_EQUAL, NULL, &lpoint);lpoint.pos--;}
        push2:
            lpoint.pos += op->len;
        push2a:
            prec = op->prio;
        push3:
            while (operp != 0 && prec <= o_oper[operp - 1].val->prio) {
                operp--;
                push_oper((Obj *)o_oper[operp].val, &o_oper[operp].epoint);
            }
            o_oper[operp].epoint = epoint;
            o_oper[operp++].val = op;
            continue;
        case '<': 
            switch (pline[lpoint.pos + 1]) {
            case '>': if (diagnostics.deprecated) err_msg2(ERROR_______OLD_NEQ, NULL, &lpoint); op = &o_NE; break;
            case '<': op = pline[lpoint.pos + 2] == '=' ? &o_BLS_ASSIGN : &o_LSHIFT; break;
            case '?': op = pline[lpoint.pos + 2] == '=' ? &o_MIN_ASSIGN : &o_MIN; break;
            case '=': op = pline[lpoint.pos + 2] == '>' ? &o_CMP : &o_LE; break;
            default: op = &o_LT; break;
            }
            goto push2;
        case '>':
            switch (pline[lpoint.pos + 1]) {
            case '<': if (diagnostics.deprecated) err_msg2(ERROR_______OLD_NEQ, NULL, &lpoint); op = &o_NE; break;
            case '>': op = pline[lpoint.pos + 2] == '=' ? &o_BRS_ASSIGN : &o_RSHIFT; break;
            case '?': op = pline[lpoint.pos + 2] == '=' ? &o_MAX_ASSIGN : &o_MAX; break;
            case '=': op = &o_GE; break;
            default: op = &o_GT; break;
            }
            goto push2;
        case '!':
            if (pline[lpoint.pos + 1] == '=') {op = &o_NE;goto push2;}
            goto syntaxe;
        case ')':
            op = &o_RPARENT;
        tphack:
            openclose--;
            if (identlist != 0) identlist--;
            while (operp != 0) {
                Oper *o = o_oper[operp - 1].val;
                if (o == &o_PARENT || o == &o_FUNC) break;
                if (o == &o_BRACKET || o == &o_INDEX) {err_msg2(ERROR______EXPECTED, "']'", &lpoint); goto error;}
                if (o == &o_BRACE) {err_msg2(ERROR______EXPECTED, "'}'", &lpoint); goto error;}
                push_oper(&o->v, &o_oper[--operp].epoint);
            }
            if (operp == 0) {err_msg2(ERROR______EXPECTED, "')' not", &lpoint); goto error;}
            lpoint.pos++;
            operp--;
            push_oper((Obj *)((o_oper[operp].val == &o_PARENT) ? op : o_oper[operp].val), &o_oper[operp].epoint);
            goto other;
        case ']':
            op = &o_RBRACKET;
        lshack:
            openclose--;
            if (identlist != 0) identlist--;
            while (operp != 0) {
                Oper *o = o_oper[operp - 1].val;
                if (o == &o_BRACKET || o == &o_INDEX) break;
                if (o == &o_PARENT || o == &o_FUNC) {err_msg2(ERROR______EXPECTED, "')'", &lpoint); goto error;}
                if (o == &o_BRACE) {err_msg2(ERROR______EXPECTED, "'}'", &lpoint); goto error;}
                push_oper(&o->v, &o_oper[--operp].epoint);
            }
            if (operp == 0) {err_msg2(ERROR______EXPECTED, "']' not", &lpoint); goto error;}
            lpoint.pos++;
            operp--;
            push_oper((Obj *)((o_oper[operp].val == &o_BRACKET) ? op : o_oper[operp].val), &o_oper[operp].epoint);
            goto other;
        case '}':
            op = &o_RBRACE;
        brhack:
            openclose--;
            while (operp != 0) {
                Oper *o = o_oper[operp - 1].val;
                if (o == &o_BRACE) break;
                if (o == &o_BRACKET || o == &o_INDEX) {err_msg2(ERROR______EXPECTED, "']'", &lpoint); goto error;}
                if (o == &o_PARENT || o == &o_FUNC) {err_msg2(ERROR______EXPECTED, "')'", &lpoint); goto error;}
                push_oper(&o->v, &o_oper[--operp].epoint);
            }
            if (operp == 0) {err_msg2(ERROR______EXPECTED, "'}' not", &lpoint); goto error;}
            lpoint.pos++;
            operp--;
            push_oper((Obj *)((o_oper[operp].val == &o_BRACE) ? op : o_oper[operp].val), &o_oper[operp].epoint);
            goto other;
        case 0:
        case ';': 
            if (openclose != 0) {
                listing_line(listing, 0);
                if (!mtranslate(cfile)) { /* expand macro parameters, if any */
                    goto other;
                }
            }
            break;
        case '\t':
        case ' ': break;
        default: 
            switch (get_label()) {
            case 1: if ((pline[epoint.pos] | arguments.caseinsensitive) == 'x') {if (pline[lpoint.pos] == '=') {lpoint.pos++; op = &o_X_ASSIGN;} else op = &o_X;goto push2a;} break;
            case 2: if ((pline[epoint.pos] | arguments.caseinsensitive) == 'i' && 
                        (pline[epoint.pos + 1] | arguments.caseinsensitive) == 'n') {op = &o_IN;goto push2a;} break;
            }
            err_msg2(ERROR______EXPECTED, "an operator", &epoint);
            goto error;
        }
        while (operp != 0) {
            Oper *o = o_oper[operp - 1].val;
            if (o == &o_PARENT || o == &o_FUNC) {err_msg2(ERROR______EXPECTED, "')'", &epoint); goto error;}
            if (o == &o_BRACKET || o == &o_INDEX) {err_msg2(ERROR______EXPECTED,"']'", &epoint); goto error;}
            if (o == &o_BRACE) {err_msg2(ERROR______EXPECTED, "'}'", &epoint); goto error;}
            push_oper(&o->v, &o_oper[--operp].epoint);
        }
        if (operp == 0) return get_val2(eval);
    syntaxe:
        err_msg2(ERROR_EXPRES_SYNTAX, NULL, &epoint);
    error:
        break;
    }
    return false;
}

bool get_exp(int stop, struct file_s *cfile, unsigned int min, unsigned int max, linepos_t epoint) {/* length in bytes, defined */
    if (!get_exp2(stop, cfile)) {
        return false;
    }
    if (eval->values_len < min || (max != 0 && eval->values_len > max)) {
        err_msg_argnum(eval->values_len, min, max, epoint);
        return false;
    }
    return true;
}


Obj *get_vals_tuple(void) {
    size_t i, len = get_val_remaining();
    Tuple *list;

    switch (len) {
    case 0:
        return (Obj *)ref_tuple(null_tuple);
    case 1:
        {
            Obj *val = pull_val(NULL);
            if (val->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)val); val = (Obj *)ref_none(); }
            return val;
        }
    default:
        break;
    }
    list = new_tuple();
    list->len = len;
    list->data = list_create_elements(list, len);
    for (i = 0; i < len; i++) {
        Obj *val2 = pull_val(NULL);
        if (val2->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)val2); val2 = (Obj *)ref_none(); }
        list->data[i] = val2;
    }
    return (Obj *)list;
}

Obj *get_vals_addrlist(struct linepos_s *epoints) {
    size_t i, j, len = get_val_remaining();
    Addrlist *list;
    struct linepos_s epoint;

    switch (len) {
    case 0: 
        return (Obj *)ref_addrlist(null_addrlist);
    case 1: 
        {
            Obj *val = pull_val(&epoints[0]);
            if (val->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)val); val = (Obj *)ref_none(); }
            return val;
        }
    default:
        break;
    }
    list = new_addrlist();
    list->data = list_create_elements(list, len);
    for (i = j = 0; j < len; j++) {
        Obj *val2 = pull_val((i < 3) ? &epoints[i] : &epoint);
        if (val2->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)val2); val2 = (Obj *)ref_none(); }
        else if (val2->obj == REGISTER_OBJ && ((Register *)val2)->len == 1 && i != 0) {
            Address_types am;
            switch (((Register *)val2)->data[0]) {
            case 's': am = A_SR; break;
            case 'r': am = A_RR; break;
            case 'z': am = A_ZR; break;
            case 'y': am = A_YR; break;
            case 'x': am = A_XR; break;
            case 'd': am = A_DR; break;
            case 'b': am = A_BR; break;
            case 'k': am = A_KR; break;
            default: am = A_NONE; break;
            }
            if (am != A_NONE) {
                val_destroy(val2);
                val2 = apply_addressing(list->data[i - 1], am);
                val_destroy(list->data[i - 1]);
                list->data[i - 1] = val2;
                continue;
            }
        }
        list->data[i++] = val2;
    }
    if (i == 1) {
        Obj *val2 = list->data[0];
        list->len = 0;
        val_destroy(&list->v);
        return val2;
    }
    list->len = i;
    return &list->v;
}

void eval_enter(void) {
    evx_p++;
    if (evx_p >= evxnum) {
        evxnum++;
        if (/*evxnum < 1 ||*/ evxnum > SIZE_MAX / sizeof *evx) err_msg_out_of_memory(); /* overflow */
        evx = (struct eval_context_s **)reallocx(evx, evxnum * sizeof *evx);
        eval = (struct eval_context_s *)mallocx(sizeof *eval);
        eval->values = NULL;
        eval->values_size = 0;
        eval->o_out = NULL;
        eval->out_size = 0;
        evx[evx_p] = eval;
        return;
    }
    eval = evx[evx_p];
}

void eval_leave(void) {
    if (evx_p != 0) evx_p--;
    eval = evx[evx_p];
}

void init_eval(void) {
    evxnum = 0;
    evx_p = (size_t)-1;
    eval_enter();
}

void destroy_eval(void) {
    while ((evxnum--) != 0) {
        struct values_s *v;
        eval = evx[evxnum];
        v = eval->o_out;
        while ((eval->out_size--) != 0) {
            if (v->val != NULL) val_destroy(v->val);
            v++;
        }
        free(eval->o_out);
        v = eval->values;
        while ((eval->values_size--) != 0) {
            if (v->val != NULL) val_destroy(v->val);
            v++;
        }
        free(eval->values);
        free(eval);
    }
    free(evx);
}
