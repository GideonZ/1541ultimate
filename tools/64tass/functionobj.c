/*
    $Id: functionobj.c 1506 2017-04-30 13:52:04Z soci $

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
#include "functionobj.h"
#include <string.h>
#include "math.h"
#include "isnprintf.h"
#include "eval.h"
#include "variables.h"
#include "error.h"

#include "floatobj.h"
#include "strobj.h"
#include "listobj.h"
#include "intobj.h"
#include "boolobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"

static Type obj;

Type *FUNCTION_OBJ = &obj;

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Function *v1 = (const Function *)o1, *v2 = (const Function *)o2;
    return o2->obj == FUNCTION_OBJ && v1->func == v2->func;
}

static MUST_CHECK Error *hash(Obj *o1, int *hs, linepos_t UNUSED(epoint)) {
    Function *v1 = (Function *)o1;
    *hs = v1->name_hash;
    return NULL;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    const Function *v1 = (const Function *)o1;
    uint8_t *s;
    size_t len;
    Str *v;
    if (epoint == NULL) return NULL;
    len = v1->name.len + 20;
    if (len < 20) return (Obj *)new_error_mem(epoint); /* overflow */
    if (len > maxsize) return NULL;
    v = new_str(len);
    v->chars = len;
    s = v->data;
    memcpy(s, "<native_function '", 18);
    s += 18;
    memcpy(s, v1->name.data, v1->name.len);
    s += v1->name.len;
    *s = '\'';
    s[1] = '>';
    return &v->v;
}

typedef MUST_CHECK Obj *(*func_t)(Funcargs *, linepos_t);

static MUST_CHECK Obj *gen_broadcast(Funcargs *vals, linepos_t epoint, func_t f) {
    struct values_s *v = vals->val;
    size_t args = vals->len;
    size_t j;
    for (j = 0; j < args; j++) {
        Type *objt = v[j].val->obj;
        if (objt == TUPLE_OBJ || objt == LIST_OBJ) {
            List *v1 = (List *)v[j].val, *vv;
            Obj *oval[3];
            size_t k;
            for (k = j + 1; k < args; k++) {
                oval[k] = v[k].val;
                if (v[k].val->obj == objt) {
                   List *v2 = (List *)v[k].val;
                   if (v2->len != 1 && v2->len != v1->len) {
                       Error *err = new_error(ERROR_CANT_BROADCAS, &v[j].epoint);
                       err->u.broadcast.v1 = v1->len;
                       err->u.broadcast.v2 = v2->len;
                       return &err->v;
                   }
                }
            }
            if (v1->len != 0) {
                bool error = true;
                size_t i;
                Obj **vals2, *o1 = v[j].val;
                vv = (List *)val_alloc(objt);
                vals2 = list_create_elements(vv, v1->len);
                for (i = 0; i < v1->len; i++) {
                    Obj *val;
                    v[j].val = v1->data[i];
                    for (k = j + 1; k < args; k++) {
                        if (oval[k]->obj == objt) {
                            List *v2 = (List *)oval[k];
                            v[k].val = v2->data[(v2->len == 1) ? 0 : i];
                        }
                    }
                    val = gen_broadcast(vals, epoint, f);
                    if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
                    vals2[i] = val;
                }
                vv->len = i;
                vv->data = vals2;
                v[j].val = o1;
                for (i = j + 1; i < args; i++) {
                    v[i].val = oval[i];
                }
                return &vv->v;
            }
            return val_reference(&v1->v);
        }
    }
    return f(vals, epoint);
}

/* range([start],end,[step]) */
static MUST_CHECK Obj *function_range(Funcargs *vals, linepos_t UNUSED(epoint)) {
    struct values_s *v = vals->val;
    List *new_value;
    Error *err = NULL;
    ival_t start = 0, end, step = 1;
    size_t len2;
    Obj **val;

    switch (vals->len) {
    default: end = 0; break; /* impossible */
    case 1: 
        err = v[0].val->obj->ival(v[0].val, &end, 8 * sizeof end, &v[0].epoint);
        break;
    case 3: 
        err = v[2].val->obj->ival(v[2].val, &step, 8 * sizeof step, &v[2].epoint);
        if (err != NULL) return &err->v; 
        /* fall through */
    case 2: 
        err = v[0].val->obj->ival(v[0].val, &start, 8 * sizeof start, &v[0].epoint);
        if (err != NULL) return &err->v; 
        err = v[1].val->obj->ival(v[1].val, &end, 8 * sizeof end, &v[1].epoint);
        break;
    }
    if (err != NULL) return &err->v;
    if (step == 0) {
        return (Obj *)new_error(ERROR_NO_ZERO_VALUE, &v[2].epoint);
    }
    if (step > 0) {
        if (end < start) end = start;
        len2 = (uval_t)(end - start + step - 1) / (uval_t)step;
    } else {
        if (end > start) end = start;
        len2 = (uval_t)(start - end - step - 1) / (uval_t)-step;
    }
    new_value = new_list();
    val = list_create_elements(new_value, len2);
    if (len2 != 0) {
        size_t i = 0;
        while ((end > start && step > 0) || (end < start && step < 0)) {
            val[i] = (Obj *)int_from_ival(start);
            i++; start += step;
        }
    }
    new_value->len = len2;
    new_value->data = val;
    return &new_value->v;
}

static uint64_t state[2];
 
static uint64_t random64(void) {
    uint64_t a = state[0];
    const uint64_t b = state[1];
    state[0] = b;
    a ^= a << 23;
    a ^= a >> 17;
    a ^= b ^ (b >> 26);
    state[1] = a;
    return a + b;
}

void random_reseed(Obj *o1, linepos_t epoint) {
    Obj *v = INT_OBJ->create(o1, epoint);
    if (v->obj != INT_OBJ) {
        if (v->obj == ERROR_OBJ) err_msg_output((Error *)v);
    } else {
        Int *v1 = (Int *)v;
        Error *err;

        state[0] = (((uint64_t)0x5229a30f) << 32) | (uint64_t)0x996ad7eb;
        state[1] = (((uint64_t)0xc03bbc75) << 32) | (uint64_t)0x3f671f6f;

        switch (v1->len) {
        case 4: state[1] ^= ((uint64_t)v1->data[3] << (8 * sizeof *v1->data)); /* fall through */
        case 3: state[1] ^= v1->data[2]; /* fall through */
        case 2: state[0] ^= ((uint64_t)v1->data[1] << (8 * sizeof *v1->data)); /* fall through */
        case 1: state[0] ^= v1->data[0]; /* fall through */
        case 0: break;
        default:
            err = new_error(v1->len < 0 ? ERROR______NOT_UVAL : ERROR_____CANT_UVAL, epoint);
            err->u.intconv.bits = 128;
            err->u.intconv.val = val_reference(o1);
            err_msg_output_and_destroy(err);
        }
    }
    val_destroy(v);
}

/* random() */
static MUST_CHECK Obj *function_random(Funcargs *vals, linepos_t epoint) {
    struct values_s *v = vals->val;
    Error *err = NULL;
    ival_t start = 0, end, step = 1;
    uval_t len2;

    switch (vals->len) {
    case 0:
        return (Obj *)new_float((random64() & (((uint64_t)1 << 53) - 1)) * ldexp(1, -53));
    case 1: 
        err = v[0].val->obj->ival(v[0].val, &end, 8 * sizeof end, &v[0].epoint);
        break;
    case 3: 
        err = v[2].val->obj->ival(v[2].val, &step, 8 * sizeof step, &v[2].epoint);
        if (err != NULL) return &err->v; 
        /* fall through */
    case 2: 
        err = v[0].val->obj->ival(v[0].val, &start, 8 * sizeof start, &v[0].epoint);
        if (err != NULL) return &err->v; 
        err = v[1].val->obj->ival(v[1].val, &end, 8 * sizeof end, &v[1].epoint);
        break;
    }
    if (err != NULL) return &err->v;
    if (step == 0) {
        return (Obj *)new_error(ERROR_NO_ZERO_VALUE, &v[2].epoint);
    }
    if (step > 0) {
        if (end < start) end = start;
        len2 = (uval_t)(end - start + step - 1) / (uval_t)step;
    } else {
        if (end > start) end = start;
        len2 = (uval_t)(start - end - step - 1) / (uval_t)-step;
    }
    if (len2 != 0) {
        if (step != 1 || (len2 & (len2 - 1)) != 0) {
            uval_t a = (~(uval_t)0) / len2;
            uval_t b = a * len2;
            uval_t r;
            do {
                r = (uval_t)random64();
            } while (r >= b);
            return (Obj *)int_from_ival(start + (ival_t)(r / a) * step);
        }
        return (Obj *)int_from_ival(start + (ival_t)(random64() & (len2 - 1)));
    }
    return (Obj *)new_error(ERROR___EMPTY_RANGE, epoint);
}

static struct oper_s sort_tmp;
static Obj *sort_error;
static Obj **sort_vals;

static int sortcomp(const void *a, const void *b) {
    int ret;
    Obj *result;
    size_t aa = *(const size_t *)a, bb = *(const size_t *)b;
    Obj *o1 = sort_tmp.v1 = sort_vals[aa];
    Obj *o2 = sort_tmp.v2 = sort_vals[bb];
    result = sort_tmp.v1->obj->calc2(&sort_tmp);
    if (result->obj == INT_OBJ) ret = (int)((Int *)result)->len;
    else {
        ret = 0;
        if (sort_error == NULL) {
            if (result->obj == ERROR_OBJ) sort_error = val_reference(result);
            else {
                if (result->obj == TUPLE_OBJ || result->obj == LIST_OBJ) {
                    List *v1 = (List *)result;
                    size_t i;
                    for (i = 0; i < v1->len; i++) {
                        Obj *v2 = v1->data[i];
                        if (v2->obj == INT_OBJ) {
                            ret = (int)((Int *)v2)->len;
                            if (ret == 0) continue;
                            val_destroy(result);
                            return ret;
                        }
                        break;
                    }
                    if (i == v1->len) {
                        val_destroy(result);
                        return (aa > bb) ? 1 : -1;
                    }
                }
                sort_tmp.v1 = o1;
                sort_tmp.v2 = o2;
                sort_error = obj_oper_error(&sort_tmp);
            } 
        }
    }
    val_destroy(result);
    if (ret == 0) return (aa > bb) ? 1 : -1;
    return ret;
}

/* sort() */
static MUST_CHECK Obj *function_sort(Obj *o1, linepos_t epoint) {
    if (o1->obj == TUPLE_OBJ || o1->obj == LIST_OBJ) {
        List *v1 = (List *)o1, *v;
        size_t ln = v1->len;
        if (ln > 0) {
            size_t i;
            Obj **vals;
            size_t *sort_index;
            if (ln > SIZE_MAX / sizeof *sort_index) goto failed; /* overflow */
            sort_index = (size_t *)malloc(ln * sizeof *sort_index);
            if (sort_index == NULL) goto failed;
            for (i = 0; i < ln; i++) sort_index[i] = i;
            sort_vals = v1->data;
            sort_error = NULL;
            sort_tmp.op = &o_CMP;
            sort_tmp.epoint = sort_tmp.epoint2 = sort_tmp.epoint3 = epoint;
            qsort(sort_index, ln, sizeof *sort_index, sortcomp);
            if (sort_error != NULL) {
                free(sort_index);
                return sort_error;
            }
            v = (List *)val_alloc(o1->obj);
            v->data = vals = list_create_elements(v, ln);
            v->len = ln;
            for (i = 0; i < ln; i++) vals[i] = val_reference(v1->data[sort_index[i]]);
            free(sort_index);
            return &v->v;
        }
    }
    return val_reference(o1);
failed:
    return (Obj *)new_error_mem(epoint);
}

static MUST_CHECK Obj *apply_func(Obj *o1, Function_types func, linepos_t epoint) {
    Obj *err;
    double real;
    if (o1->obj == TUPLE_OBJ || o1->obj == LIST_OBJ) {
        List *v1 = (List *)o1, *v;
        if (v1->len != 0) {
            bool error = true;
            size_t i;
            Obj **vals;
            v = (List *)val_alloc(o1->obj);
            vals = list_create_elements(v, v1->len);
            for (i = 0; i < v1->len; i++) {
                Obj *val = apply_func(v1->data[i], func, epoint);
                if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
                vals[i] = val;
            }
            v->len = i;
            v->data = vals;
            return &v->v;
        }
        return val_reference(&v1->v);
    }
    switch (func) {
    case F_SIZE: return o1->obj->size(o1, epoint);
    case F_SIGN: return o1->obj->sign(o1, epoint);
    case F_CEIL: return o1->obj->function(o1, TF_CEIL, epoint);
    case F_FLOOR: return o1->obj->function(o1, TF_FLOOR, epoint);
    case F_ROUND: return o1->obj->function(o1, TF_ROUND, epoint);
    case F_TRUNC: return o1->obj->function(o1, TF_TRUNC, epoint);
    case F_ABS: return o1->obj->function(o1, TF_ABS, epoint);
    case F_REPR: return o1->obj->repr(o1, epoint, SIZE_MAX);
    default: break;
    }
    if (o1->obj == FLOAT_OBJ) {
        real = ((Float *)o1)->real;
    } else {
        err = FLOAT_OBJ->create(o1, epoint);
        if (err->obj != FLOAT_OBJ) return err;
        real = ((Float *)err)->real;
        val_destroy(err);
    }
    switch (func) {
    case F_SQRT: 
        if (real < 0.0) {
            return (Obj *)new_error_key(ERROR_SQUARE_ROOT_N, o1, epoint);
        }
        real = sqrt(real);
        break;
    case F_LOG10: 
        if (real <= 0.0) {
            return (Obj *)new_error_key(ERROR_LOG_NON_POSIT, o1, epoint);
        }
        real = log10(real);
        break;
    case F_LOG: 
        if (real <= 0.0) {
            return (Obj *)new_error_key(ERROR_LOG_NON_POSIT, o1, epoint);
        }
        real = log(real);
        break;
    case F_EXP: real = exp(real);break;
    case F_SIN: real = sin(real);break;
    case F_COS: real = cos(real);break;
    case F_TAN: real = tan(real);break;
    case F_ACOS: 
        if (real < -1.0 || real > 1.0) {
            return (Obj *)new_error_key(ERROR___MATH_DOMAIN, o1, epoint);
        }
        real = acos(real);
        break;
    case F_ASIN: 
        if (real < -1.0 || real > 1.0) {
            return (Obj *)new_error_key(ERROR___MATH_DOMAIN, o1, epoint);
        }
        real = asin(real);
        break;
    case F_ATAN: real = atan(real);break;
    case F_CBRT: real = cbrt(real);break;
    case F_FRAC: real -= trunc(real);break;
    case F_RAD: real = real * M_PI / 180.0;break;
    case F_DEG: real = real * 180.0 / M_PI;break;
    case F_COSH: real = cosh(real);break;
    case F_SINH: real = sinh(real);break;
    case F_TANH: real = tanh(real);break;
    default: real = HUGE_VAL; break; /* can't happen */
    }
    return float_from_double(real, epoint);
}

static MUST_CHECK Obj *to_real(struct values_s *v, double *r) {
    if (v->val->obj == FLOAT_OBJ) {
        *r = ((Float *)v->val)->real;
    } else {
        Obj *val = FLOAT_OBJ->create(v->val, &v->epoint);
        if (val->obj != FLOAT_OBJ) return val;
        *r = ((Float *)val)->real;
        val_destroy(val);
    }
    return NULL;
}

static MUST_CHECK Obj *function_hypot(Funcargs *vals, linepos_t epoint) {
    struct values_s *v = vals->val;
    Obj *val;
    double real, real2;

    val = to_real(&v[0], &real);
    if (val != NULL) return val;
    val = to_real(&v[1], &real2);
    if (val != NULL) return val;
    return float_from_double(hypot(real, real2), epoint);
}

static MUST_CHECK Obj *function_atan2(Funcargs *vals, linepos_t epoint) {
    struct values_s *v = vals->val;
    Obj *val;
    double real, real2;

    val = to_real(&v[0], &real);
    if (val != NULL) return val;
    val = to_real(&v[1], &real2);
    if (val != NULL) return val;
    return float_from_double(atan2(real, real2), epoint);
}

static MUST_CHECK Obj *function_pow(Funcargs *vals, linepos_t epoint) {
    struct values_s *v = vals->val;
    Obj *val;
    double real, real2;

    val = to_real(&v[0], &real);
    if (val != NULL) return val;
    val = to_real(&v[1], &real2);
    if (val != NULL) return val;
    if (real2 < 0.0 && real == 0.0) {
        return (Obj *)new_error(ERROR_DIVISION_BY_Z, epoint);
    }
    if (real < 0.0 && floor(real2) != real2) {
        return (Obj *)new_error(ERROR_NEGFRAC_POWER, epoint);
    }
    return float_from_double(pow(real, real2), epoint);
}

static inline int icmp(const Function *vv1, const Function *vv2) {
    Function_types v1 = vv1->func;
    Function_types v2 = vv2->func;
    if (v1 < v2) return -1;
    return (v1 > v2) ? 1 : 0;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Function *v1 = (Function *)op->v1;
    Obj *o2 = op->v2;
    Function_types func;
    struct values_s *v;
    size_t args;
    switch (o2->obj->type) {
    case T_FUNCTION:
        {
            Function *v2 = (Function *)o2;
            int val = icmp(v1, v2);
            switch (op->op->op) {
            case O_CMP:
                if (val < 0) return (Obj *)ref_int(minus1_value);
                return (Obj *)ref_int(int_value[(val > 0) ? 1 : 0]);
            case O_EQ: return truth_reference(val == 0);
            case O_NE: return truth_reference(val != 0);
            case O_MIN:
            case O_LT: return truth_reference(val < 0);
            case O_LE: return truth_reference(val <= 0);
            case O_MAX:
            case O_GT: return truth_reference(val > 0);
            case O_GE: return truth_reference(val >= 0);
            default: break;
            }
            break;
        }
    case T_FUNCARGS:
        {
            Funcargs *v2 = (Funcargs *)o2;
            v = v2->val;
            args = v2->len;
            switch (op->op->op) {
            case O_FUNC:
                func = v1->func;
                switch (func) {
                case F_HYPOT:
                    if (args != 2) {
                        err_msg_argnum(args, 2, 2, op->epoint2);
                        return (Obj *)ref_none();
                    }
                    return gen_broadcast(v2, op->epoint, function_hypot);
                case F_ATAN2:
                    if (args != 2) {
                        err_msg_argnum(args, 2, 2, op->epoint2);
                        return (Obj *)ref_none();
                    }
                    return gen_broadcast(v2, op->epoint, function_atan2);
                case F_POW: 
                    if (args != 2) {
                        err_msg_argnum(args, 2, 2, op->epoint2);
                        return (Obj *)ref_none();
                    }
                    return gen_broadcast(v2, op->epoint, function_pow);
                case F_RANGE: 
                    if (args < 1 || args > 3) {
                        err_msg_argnum(args, 1, 3, op->epoint2);
                        return (Obj *)ref_none();
                    }
                    return gen_broadcast(v2, op->epoint, function_range);
                case F_FORMAT: 
                    return isnprintf(v2, op->epoint);
                case F_RANDOM: 
                    if (args > 3) {
                        err_msg_argnum(args, 0, 3, op->epoint2);
                        return (Obj *)ref_none();
                    }
                    return gen_broadcast(v2, op->epoint, function_random);
                default:
                    if (args != 1) {
                        err_msg_argnum(args, 1, 1, op->epoint2);
                        return (Obj *)ref_none();
                    }
                    switch (func) {
                    case F_ANY: return v[0].val->obj->truth(v[0].val, TRUTH_ANY, &v[0].epoint);
                    case F_ALL: return v[0].val->obj->truth(v[0].val, TRUTH_ALL, &v[0].epoint);
                    case F_LEN: return v[0].val->obj->len(v[0].val, &v[0].epoint);
                    case F_SORT: return function_sort(v[0].val, &v[0].epoint);
                    default: return apply_func(v[0].val, func, &v[0].epoint);
                    }
                }
            default: break;
            }
            break;
        }
    case T_LIST:
    case T_TUPLE:
    case T_DICT:
        if (op->op != &o_MEMBER && op->op != &o_X) {
            return o2->obj->rcalc2(op);
        }
        break;
    case T_NONE:
    case T_ERROR:
        return val_reference(o2);
    default: break;
    }
    return obj_oper_error(op);
}

void functionobj_init(void) {
    new_type(&obj, T_FUNCTION, "function", sizeof(Function));
    obj.hash = hash;
    obj.same = same;
    obj.repr = repr;
    obj.calc2 = calc2;
}

struct builtin_functions_s {
    const char *name;
    Function_types func;
};

static struct builtin_functions_s builtin_functions[] = {
    {"abs", F_ABS}, 
    {"acos", F_ACOS}, 
    {"all", F_ALL},
    {"any", F_ANY},
    {"asin", F_ASIN}, 
    {"atan", F_ATAN}, 
    {"atan2", F_ATAN2}, 
    {"cbrt", F_CBRT}, 
    {"ceil", F_CEIL},
    {"cos", F_COS}, 
    {"cosh", F_COSH}, 
    {"deg", F_DEG}, 
    {"exp", F_EXP}, 
    {"floor", F_FLOOR},
    {"format", F_FORMAT}, 
    {"frac", F_FRAC}, 
    {"hypot", F_HYPOT}, 
    {"len", F_LEN},
    {"log", F_LOG},
    {"log10", F_LOG10}, 
    {"pow", F_POW}, 
    {"rad", F_RAD}, 
    {"random", F_RANDOM},
    {"range", F_RANGE},
    {"repr", F_REPR},
    {"round", F_ROUND},
    {"sign", F_SIGN}, 
    {"sin", F_SIN}, 
    {"sinh", F_SINH}, 
    {"size", F_SIZE},
    {"sort", F_SORT}, 
    {"sqrt", F_SQRT}, 
    {"tan", F_TAN}, 
    {"tanh", F_TANH}, 
    {"trunc", F_TRUNC}, 
    {NULL, F_NONE}
};

void functionobj_names(void) {
    int i;

    for (i = 0; builtin_functions[i].name != NULL; i++) {
        Function *func = (Function *)val_alloc(FUNCTION_OBJ);
        func->name.data = (const uint8_t *)builtin_functions[i].name;
        func->name.len = strlen(builtin_functions[i].name);
        func->func = builtin_functions[i].func;
        func->name_hash = str_hash(&func->name);
        new_builtin(builtin_functions[i].name, &func->v);
    }
}
