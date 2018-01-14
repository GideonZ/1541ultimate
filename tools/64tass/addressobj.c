/*
    $Id: addressobj.c 1511 2017-05-01 08:08:36Z soci $

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
#include "addressobj.h"
#include <string.h>
#include "error.h"
#include "eval.h"
#include "variables.h"
#include "arguments.h"

#include "boolobj.h"
#include "strobj.h"
#include "intobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"
#include "floatobj.h"
#include "bitsobj.h"
#include "bytesobj.h"

static Type obj;

Type *ADDRESS_OBJ = &obj;

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_BYTES:
    case T_STR:
        return (Obj *)new_address(val_reference(v1), A_NONE);
    case T_NONE:
    case T_ERROR:
    case T_ADDRESS: return val_reference(v1);
    default: break;
    }
    err_msg_wrong_type(v1, NULL, epoint);
    return (Obj *)ref_none();
}

static FAST_CALL void destroy(Obj *o1) {
    Address *v1 = (Address *)o1;
    val_destroy(v1->val);
}

static FAST_CALL void garbage(Obj *o1, int i) {
    Address *v1 = (Address *)o1;
    Obj *v;
    switch (i) {
    case -1:
        v1->val->refcount--;
        return;
    case 0:
        return;
    case 1:
        v = v1->val;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Address *v1 = (const Address *)o1, *v2 = (const Address *)o2;
    return o2->obj == ADDRESS_OBJ && v1->type == v2->type && v1->val->obj->same(v1->val, v2->val);
}

static MUST_CHECK Obj *truth(Obj *o1, Truth_types type, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v;
    if (v1->type != A_NONE) {
        return DEFAULT_OBJ->truth(o1, type, epoint);
    }
    v = v1->val;
    return v->obj->truth(v, type, epoint);
}

static MUST_CHECK Error *hash(Obj *o1, int *hs, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v = v1->val;
    Error *err = v->obj->hash(v, hs, epoint);
    if (err == NULL) {
        *hs = ((unsigned int)*hs + v1->type) & ((~0U) >> 1);
    }
    return err;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    Address *v1 = (Address *)o1;
    uint8_t *s;
    size_t len, chars;
    char buffer[100], buffer2[100], *b2;
    atype_t addrtype;
    unsigned int ind, ind2;
    Obj *tmp;
    Str *v, *str;

    ind2 = 0;
    addrtype = v1->type;
    ind = 99;
    buffer2[ind] = '\0';
    if (addrtype == A_NONE) {
        ind -= 8;
        memcpy(&buffer2[ind], "address(", 8);
        buffer[ind2++] = ')';
    } else {
        while ((addrtype & MAX_ADDRESS_MASK) != 0) {
            uint32_t mode;
            switch ((Address_types)((addrtype & 0xf000) >> 12)) {
            case A_XR: mode = (',' << 16) + ('x' << 24);break;
            case A_YR: mode = (',' << 16) + ('y' << 24);break;
            case A_ZR: mode = (',' << 16) + ('z' << 24);break;
            case A_SR: mode = (',' << 16) + ('s' << 24);break;
            case A_RR: mode = (',' << 16) + ('r' << 24);break;
            case A_DR: mode = (',' << 16) + ('d' << 24);break;
            case A_BR: mode = (',' << 16) + ('b' << 24);break;
            case A_KR: mode = (',' << 16) + ('k' << 24);break;
            case A_I:  mode = ('(' << 8) + (')' << 16);break;
            case A_LI: mode = ('[' << 8) + (']' << 16);break;
            case A_IMMEDIATE_SIGNED: mode = ('#' << 8) + '+';break;
            case A_IMMEDIATE: mode = '#' << 8;break;
            default: mode = 0;
            }
            if ((char)mode != '\0') buffer2[--ind] = (char)mode;
            mode >>= 8;
            if ((char)mode != '\0') buffer2[--ind] = (char)mode;
            mode >>= 8;
            if ((char)mode != '\0') buffer[ind2++] = (char)mode;
            mode >>= 8;
            if ((char)mode != '\0') buffer[ind2++] = (char)mode;
            addrtype <<= 4;
        }
    }
    b2 = buffer2 + ind;
    ind = 99 - ind;

    chars = ind + ind2;
    if (chars > maxsize) return NULL;
    tmp = v1->val->obj->repr(v1->val, epoint, maxsize - chars);
    if (tmp == NULL || tmp->obj != STR_OBJ) return tmp;
    str = (Str *)tmp;
    len = chars + str->len;
    if (len < chars) err_msg_out_of_memory(); /* overflow */
    chars += str->chars;
    if (chars > maxsize) {
        val_destroy(tmp);
        return NULL;
    }

    v = new_str(len);
    v->chars = chars;
    s = v->data;
    if (ind != 0) {
        memcpy(s, b2, ind);
        s += ind;
    }
    if (str->len != 0) {
        memcpy(s, str->data, str->len);
        s += str->len;
    }
    if (ind2 != 0) memcpy(s, buffer, ind2);
    val_destroy(tmp);
    return &v->v;
}

bool check_addr(atype_t type) {
    while (type != A_NONE) {
        switch ((Address_types)(type & 0xf)) {
        case A_I:
        case A_LI: return true;
        case A_IMMEDIATE:
        case A_IMMEDIATE_SIGNED:
        case A_KR:
        case A_DR:
        case A_BR:
        case A_XR:
        case A_YR:
        case A_ZR:
        case A_RR:
        case A_SR:
        case A_NONE: break;
        }
        type >>= 4;
    }
    return false;
}

static inline bool check_addr2(atype_t type) {
    while (type != A_NONE) {
        switch ((Address_types)(type & 0xf)) {
        case A_KR:
        case A_DR:
        case A_BR:
        case A_XR:
        case A_YR:
        case A_ZR:
        case A_RR:
        case A_SR:
        case A_I:
        case A_LI: return true;
        case A_IMMEDIATE:
        case A_IMMEDIATE_SIGNED:
        case A_NONE: break;
        }
        type >>= 4;
    }
    return false;
}

static FAST_CALL Obj *address(Obj *o1, uint32_t *am) {
    const Address *v1 = (Address *)o1;
    atype_t type;
    Obj *v = v1->val;
    v = v->obj->address(v, am);
    type = v1->type;
    while (type != A_NONE) {
        *am <<= 4;
        type >>= 4;
    }
    *am |= v1->type;
    return v;
}

static MUST_CHECK Error *ival(Obj *o1, ival_t *iv, unsigned int bits, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v;
    if (v1->type != A_NONE) {
        return DEFAULT_OBJ->ival(o1, iv, bits, epoint);
    }
    v = v1->val;
    return v->obj->ival(v, iv, bits, epoint);
}

static MUST_CHECK Error *uval(Obj *o1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v;
    if (v1->type != A_NONE) {
        return DEFAULT_OBJ->uval(o1, uv, bits, epoint);
    }
    v = v1->val;
    return v->obj->uval(v, uv, bits, epoint);
}

static MUST_CHECK Error *uval2(Obj *o1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v;
    if (v1->type != A_NONE) {
        return DEFAULT_OBJ->uval2(o1, uv, bits, epoint);
    }
    v = v1->val;
    return v->obj->uval2(v, uv, bits, epoint);
}

MUST_CHECK Obj *float_from_address(Address *v1, linepos_t epoint) {
    if (v1->type != A_NONE) {
        err_msg_wrong_type(&v1->v, NULL, epoint);
        return (Obj *)ref_none();
    }
    return FLOAT_OBJ->create(v1->val, epoint);
}

MUST_CHECK Obj *int_from_address(Address *v1, linepos_t epoint) {
    if (v1->type != A_NONE) {
        err_msg_wrong_type(&v1->v, NULL, epoint);
        return (Obj *)ref_none();
    }
    return INT_OBJ->create(v1->val, epoint);
}

MUST_CHECK Obj *bits_from_address(Address *v1, linepos_t epoint) {
    if (v1->type != A_NONE) {
        err_msg_wrong_type(&v1->v, NULL, epoint);
        return (Obj *)ref_none();
    }
    return BITS_OBJ->create(v1->val, epoint);
}

MUST_CHECK Obj *bytes_from_address(Address *v1, linepos_t epoint) {
    if (v1->type != A_NONE) {
        err_msg_wrong_type(&v1->v, NULL, epoint);
        return (Obj *)ref_none();
    }
    return BYTES_OBJ->create(v1->val, epoint);
}

static MUST_CHECK Obj *sign(Obj *o1, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v;
    if (v1->type != A_NONE) {
        return DEFAULT_OBJ->sign(o1, epoint);
    }
    v = v1->val;
    return v->obj->sign(v, epoint);
}

static MUST_CHECK Obj *function(Obj *o1, Func_types f, linepos_t epoint) {
    Address *v1 = (Address *)o1;
    Obj *v;
    if (v1->type != A_NONE) {
        return DEFAULT_OBJ->function(o1, f, epoint);
    }
    v = v1->val;
    return v->obj->function(v, f, epoint);
}

static MUST_CHECK Obj *calc1(oper_t op) {
    Obj *result;
    Address *v1 = (Address *)op->v1;
    atype_t am = v1->type;
    switch (op->op->op) {
    case O_LNOT:
    case O_STRING: 
        if (am != A_NONE) break;
        /* fall through */
    case O_BANK:
    case O_HIGHER:
    case O_LOWER:
    case O_HWORD:
    case O_WORD:
    case O_BSWORD:
        if (am == A_NONE) {
            op->v1 = v1->val;
            return op->v1->obj->calc1(op);
        }
        goto ok;
    case O_INV:
    case O_NEG:
    case O_POS:
        if (check_addr2(am)) break;
    ok:
        op->v1 = v1->val;
        result = op->v1->obj->calc1(op);
        if (am == A_NONE) return result;
        if (result->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result); result = (Obj *)ref_none(); }
        return (Obj *)new_address(result, am);
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o2 = op->v2, *result;
    Address *v1 = (Address *)op->v1, *v;
    atype_t am;
    switch (o2->obj->type) {
    case T_ADDRESS:
        {
            Address *v2 = (Address *)o2;
            am = v1->type;
            switch (op->op->op) {
            case O_CMP:
            case O_EQ:
            case O_NE:
            case O_MIN:
            case O_LT:
            case O_LE:
            case O_MAX:
            case O_GT:
            case O_GE:
                if (am == v2->type) {
                    op->v1 = v1->val;
                    op->v2 = v2->val;
                    return op->v1->obj->calc2(op);
                }
                switch (op->op->op) {
                default: /* can't happen */
                case O_CMP: return (Obj *)ref_int((am < v2->type) ? minus1_value : int_value[1]);
                case O_EQ: return (Obj *)ref_bool(false_value);
                case O_NE: return (Obj *)ref_bool(true_value);
                case O_LE:
                case O_MIN:
                case O_LT: return truth_reference(am < v2->type);
                case O_GE:
                case O_MAX:
                case O_GT: return truth_reference(am > v2->type);
                }
            case O_ADD:
                if (check_addr(am)) break;
                if (check_addr(v2->type)) break;
                op->v1 = v1->val;
                op->v2 = v2->val;
                result = op->v1->obj->calc2(op);
                if (am == A_NONE && v2->type == A_NONE) return result;
                if (result->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result); result = (Obj *)ref_none(); }
                v = new_address(result, am);
                am = v2->type;
                while ((Address_types)(am & 0xf) != A_NONE) { v->type <<= 4; am >>= 4; }
                v->type |= v2->type;
                return &v->v;
            case O_SUB:
                if (check_addr(am)) break;
                if (check_addr(v2->type)) break;
                {
                    atype_t am1 = A_NONE, am2 = v2->type;
                    for (; (am & MAX_ADDRESS_MASK) != 0; am <<= 4) { 
                        atype_t amc = (am >> 8) & 0xf;
                        atype_t am3, am4;
                        if (amc == A_NONE) continue;
                        am3 = A_NONE; am4 = am2;
                        while (am4 != A_NONE) { 
                            atype_t am5 = (am4 & 0xf);
                            if (amc == am5) amc = A_NONE;
                            else am3 = (am3 << 4) | am5;
                            am4 >>= 4;
                        }
                        am2 = am3;
                        if (amc == A_NONE) continue;
                        am1 = (am1 << 4) | amc;
                    }
                    if (am2 != A_NONE) break;
                    op->v1 = v1->val;
                    op->v2 = v2->val;
                    result = op->v1->obj->calc2(op);
                    if (am1 == A_NONE) return result;
                    if (result->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result); result = (Obj *)ref_none(); }
                    return (Obj *)new_address(result, am1);
                }
            default:
                break;
            }
            break;
        }
    case T_BOOL:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_BYTES:
    case T_STR:
        switch (op->op->op) {
        default:
            am = v1->type;
            if (am == A_NONE) {
                op->v1 = v1->val;
                return op->v1->obj->calc2(op);
            }
            if (check_addr2(am)) break;
            goto ok;
        case O_ADD:
        case O_SUB:
            am = v1->type;
            if (check_addr(am)) break;
        ok:
            op->v1 = v1->val;
            result = op->v1->obj->calc2(op);
            if (result->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result); result = (Obj *)ref_none(); }
            return (Obj *)new_address(result, am);
        }
        break;
    default:
        if (op->op != &o_MEMBER && op->op != &o_X) {
            return o2->obj->rcalc2(op);
        }
        if (o2 == &none_value->v || o2->obj == ERROR_OBJ) return val_reference(o2);
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    Obj *result;
    Type *t1 = op->v1->obj;
    Address *v2 = (Address *)op->v2;
    atype_t am;
    switch (t1->type) {
    case T_BOOL:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_BYTES:
    case T_STR:
    case T_GAP:
        am = v2->type;
        if (op->op == &o_ADD) {
            if (check_addr(am)) break;
        } else {
            if (am == A_NONE) {
                op->v2 = v2->val;
                return t1->calc2(op);
            }
            if (check_addr2(am)) break;
        }
        op->v2 = v2->val;
        result = t1->calc2(op);
        if (result->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result); result = (Obj *)ref_none(); }
        return (Obj *)new_address(result, am);
    case T_CODE:
        if (op->op != &o_IN) {
            return t1->calc2(op);
        }
        break;
    default: break;
    }
    return obj_oper_error(op);
}

void addressobj_init(void) {
    new_type(&obj, T_ADDRESS, "address", sizeof(Address));
    obj.create = create;
    obj.destroy = destroy;
    obj.garbage = garbage;
    obj.same = same;
    obj.truth = truth;
    obj.hash = hash;
    obj.repr = repr;
    obj.address = address;
    obj.ival = ival;
    obj.uval = uval;
    obj.uval2 = uval2;
    obj.sign = sign;
    obj.function = function;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
}

void addressobj_names(void) {
    new_builtin("address", val_reference(&ADDRESS_OBJ->v));
}
