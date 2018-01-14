/*
    $Id: listobj.c 1506 2017-04-30 13:52:04Z soci $

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
#include "listobj.h"
#include <string.h>
#include "eval.h"
#include "variables.h"
#include "error.h"
#include "arguments.h"

#include "boolobj.h"
#include "codeobj.h"
#include "strobj.h"
#include "intobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"

static Type list_obj;
static Type tuple_obj;
static Type addrlist_obj;
static Type colonlist_obj;

Type *LIST_OBJ = &list_obj;
Type *TUPLE_OBJ = &tuple_obj;
Type *ADDRLIST_OBJ = &addrlist_obj;
Type *COLONLIST_OBJ = &colonlist_obj;
Tuple *null_tuple;
List *null_list;
Addrlist *null_addrlist;

static FAST_CALL void destroy(Obj *o1) {
    List *v1 = (List *)o1;
    size_t i;
    for (i = 0; i < v1->len; i++) {
        val_destroy(v1->data[i]);
    }
    if (v1->val != v1->data) free(v1->data);
}

static FAST_CALL void garbage(Obj *o1, int j) {
    List *v1 = (List *)o1;
    size_t i;
    Obj *v;
    switch (j) {
    case -1:
        for (i = 0; i < v1->len; i++) {
            v1->data[i]->refcount--;
        }
        return;
    case 0:
        if (v1->val != v1->data) free(v1->data);
        return;
    case 1:
        for (i = 0; i < v1->len; i++) {
            v = v1->data[i];
            if ((v->refcount & SIZE_MSB) != 0) {
                v->refcount -= SIZE_MSB - 1;
                v->obj->garbage(v, 1);
            } else v->refcount++;
        }
        return;
    }
}

static Obj **lnew(List *v, size_t len) {
    v->len = len;
    if (len <= lenof(v->val)) {
        v->data = v->val;
        return v->val;
    }
    if (len <= SIZE_MAX / sizeof *v->data) { /* overflow */
        Obj **n = (Obj **)malloc(len * sizeof *v->data);
        if (n != NULL) {
            v->data = n;
            return n;
        }
    }
    v->len = 0;
    val_destroy(&v->v);
    return NULL;
}

static MUST_CHECK Obj *tuple_from_list(List *v1, Type *typ, linepos_t epoint) {
    size_t i, ln;
    Obj **vals, **data = v1->data;
    Tuple *v = (Tuple *)val_alloc(typ);

    ln = v1->len;
    vals = lnew(v, ln);
    if (vals == NULL) return (Obj *)new_error_mem(epoint);

    for (i = 0; i < ln; i++) {
        vals[i] = val_reference(data[i]);
    }
    return &v->v;
}

static MUST_CHECK Obj *list_create(Obj *o1, linepos_t epoint) {
    switch (o1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_LIST: return val_reference(o1);
    case T_TUPLE: return tuple_from_list((List *)o1, LIST_OBJ, epoint);
    case T_CODE: return tuple_from_code((Code *)o1, LIST_OBJ, epoint);
    default: break;
    }
    err_msg_wrong_type(o1, NULL, epoint);
    return (Obj *)ref_none();
}

static MUST_CHECK Obj *tuple_create(Obj *o1, linepos_t epoint) {
    switch (o1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_TUPLE: return val_reference(o1);
    case T_LIST: return tuple_from_list((List *)o1, TUPLE_OBJ, epoint);
    case T_CODE: return tuple_from_code((Code *)o1, TUPLE_OBJ, epoint);
    default: break;
    }
    err_msg_wrong_type(o1, NULL, epoint);
    return (Obj *)ref_none();
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const List *v1 = (const List *)o1, *v2 = (const List *)o2;
    size_t i;
    if (o1->obj != o2->obj || v1->len != v2->len) return false;
    for (i = 0; i < v2->len; i++) {
        Obj *val = v1->data[i];
        if (!val->obj->same(val, v2->data[i])) return false;
    }
    return true;
}

static MUST_CHECK Obj *truth(Obj *o1, Truth_types type, linepos_t epoint) {
    List *v1 = (List *)o1;
    size_t i;
    Obj *val;
    switch (type) {
    case TRUTH_ALL:
        for (i = 0; i < v1->len; i++) {
            val = v1->data[i]->obj->truth(v1->data[i], type, epoint);
            if (val->obj != BOOL_OBJ) return val;
            if ((Bool *)val != true_value) return val;
            val_destroy(val);
        }
        return (Obj *)ref_bool(true_value);
    case TRUTH_ANY:
        for (i = 0; i < v1->len; i++) {
            val = v1->data[i]->obj->truth(v1->data[i], type, epoint);
            if (val->obj != BOOL_OBJ) return val;
            if ((Bool *)val == true_value) return val;
            val_destroy(val);
        }
        return (Obj *)ref_bool(false_value);
    default: 
        return DEFAULT_OBJ->truth(o1, type, epoint);
    }
}

static MUST_CHECK Obj *repr_listtuple(Obj *o1, linepos_t epoint, size_t maxsize) {
    Tuple *v1 = (List *)o1;
    bool tupleorlist = (o1->obj != ADDRLIST_OBJ && o1->obj != COLONLIST_OBJ);
    size_t i, len = tupleorlist ? 2 : 0, chars = len;
    List *list = NULL;
    Obj **vals = NULL, *val;
    Str *v;
    uint8_t *s;
    size_t llen = v1->len;
    if (llen != 0) {
        list = new_tuple();
        vals = lnew(list, llen);
        if (vals == NULL) return (Obj *)new_error_mem(epoint);
        i = (llen != 1 || o1->obj != TUPLE_OBJ) ? (llen - 1) : llen;
        len += i;
        if (len < i) {
            list->len = 0;
            val_destroy(&list->v);
            return (Obj *)new_error_mem(epoint); /* overflow */
        }
        chars = len;
        if (chars > maxsize) {
            list->len = 0;
            val_destroy(&list->v);
            return NULL;
        }
        for (i = 0;i < llen; i++) {
            Obj *o2 = v1->data[i];
            if (o2 == &default_value->v && o1->obj == COLONLIST_OBJ) {
                val = (Obj *)ref_str(null_str);
            } else {
                val = o2->obj->repr(o2, epoint, maxsize - chars);
                if (val == NULL || val->obj != STR_OBJ) {
                    list->len = i;
                    val_destroy(&list->v);
                    return val;
                }
            }
            v = (Str *)val;
            len += v->len;
            if (len < v->len) {
                list->len = i;
                val_destroy(&list->v);
                val_destroy(val);
                return (Obj *)new_error_mem(epoint); /* overflow */
            }
            chars += v->chars;
            if (chars > maxsize) {
                list->len = i;
                val_destroy(&list->v);
                val_destroy(val);
                return NULL;
            }
            vals[i] = val;
        }
        list->len = i;
    } else if (chars > maxsize) return NULL;
    v = new_str(len);
    v->chars = chars;
    s = v->data;
    if (tupleorlist) *s++ = (o1->obj == LIST_OBJ) ? '[' : '(';
    for (i = 0; i < llen; i++) {
        Str *str = (Str *)vals[i];
        if (i != 0) *s++ = (o1->obj == COLONLIST_OBJ) ? ':' : ',';
        if (str->len != 0) {
            memcpy(s, str->data, str->len);
            s += str->len;
        }
    }
    if (i == 1 && o1->obj == TUPLE_OBJ) *s++ = ',';
    if (tupleorlist) *s = (o1->obj == LIST_OBJ) ? ']' : ')';
    if (list != NULL) val_destroy(&list->v);
    return &v->v;
}

static MUST_CHECK Obj *len(Obj *o1, linepos_t UNUSED(epoint)) {
    List *v1 = (List *)o1;
    return (Obj *)int_from_size(v1->len);
}

static MUST_CHECK Iter *getiter(Obj *v1) {
    Iter *v = (Iter *)val_alloc(ITER_OBJ);
    v->val = 0;
    v->iter = &v->val;
    v->data = val_reference(v1);
    return v;
}

static MUST_CHECK Obj *next(Iter *v1) {
    const List *vv1 = (List *)v1->data;
    if (v1->val >= vv1->len) return NULL;
    return val_reference(vv1->data[v1->val++]);
}

Obj **list_create_elements(List *v, size_t n) {
    if (n <= lenof(v->val)) return v->val;
    if (n > SIZE_MAX / sizeof *v->data) err_msg_out_of_memory(); /* overflow */
    return (Obj **)mallocx(n * sizeof *v->data);
}

static MUST_CHECK Obj *calc1(oper_t op) {
    Obj *o1 = op->v1;
    List *v1 = (List *)o1, *v;
    if (v1->len != 0) {
        Obj **vals;
        bool error = true;
        size_t i;
        v = (List *)val_alloc(o1->obj);
        vals = lnew(v, v1->len);
        if (vals == NULL) return (Obj *)new_error_mem(op->epoint3);
        for (i = 0; i < v1->len; i++) {
            Obj *val;
            op->v1 = v1->data[i];
            val = op->v1->obj->calc1(op);
            if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
            vals[i] = val;
        }
        return &v->v;
    }
    return val_reference((o1->obj == TUPLE_OBJ) ? &null_tuple->v : &null_list->v);
}

static MUST_CHECK Obj *calc2_list(oper_t op) {
    Obj *o1 = op->v1, *o2 = op->v2;
    List *v1 = (List *)o1, *v2 = (List *)o2;
    size_t i;
    Error *err;

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
    case O_MUL:
    case O_DIV:
    case O_MOD:
    case O_ADD:
    case O_SUB:
    case O_AND:
    case O_OR:
    case O_XOR:
    case O_LSHIFT:
    case O_RSHIFT:
    case O_EXP:
    case O_MEMBER:
    case O_LAND:
    case O_LOR:
        {
            if (v1->len == v2->len) {
                if (v1->len != 0) {
                    Obj **vals;
                    bool error = true;
                    List *v = (List *)val_alloc(o1->obj);
                    vals = lnew(v, v1->len);
                    if (vals == NULL) goto failed;
                    for (i = 0; i < v1->len; i++) {
                        Obj *oo1 = op->v1 = v1->data[i];
                        Obj *oo2 = op->v2 = v2->data[i];
                        Obj *val = op->v1->obj->calc2(op);
                        if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
                        else if (op->op == &o_MIN || op->op == &o_MAX) {
                            if (val == &true_value->v) val_replace(&val, oo1);
                            else if (val == &false_value->v) val_replace(&val, oo2);
                        }
                        vals[i] = val;
                    }
                    return &v->v;
                } 
                return val_reference(&v1->v);
            } 
            if (v1->len == 1) {
                op->v1 = v1->data[0];
                return op->v2->obj->rcalc2(op);
            } 
            if (v2->len == 1) {
                op->v2 = v2->data[0];
                return op->v1->obj->calc2(op);
            } 
            err = new_error(ERROR_CANT_BROADCAS, op->epoint3);
            err->u.broadcast.v1 = v1->len;
            err->u.broadcast.v2 = v2->len;
            return &err->v;
        }
    case O_CONCAT:
        {
            Obj **vals;
            size_t ln;
            List *v;

            if (v1->len == 0) {
                return val_reference(o2);
            }
            if (v2->len == 0) {
                return val_reference(o1);
            }
            ln = v1->len + v2->len;
            if (ln < v2->len) goto failed; /* overflow */
            v = (List *)val_alloc(o1->obj);
            vals = lnew(v, ln);
            if (vals == NULL) goto failed;
            for (i = 0; i < v1->len; i++) {
                vals[i] = val_reference(v1->data[i]);
            }
            for (; i < ln; i++) {
                vals[i] = val_reference(v2->data[i - v1->len]);
            }
            return &v->v;
        }
    default: break;
    }
    return obj_oper_error(op);
failed:
    return (Obj *)new_error_mem(op->epoint3);
}

static inline MUST_CHECK Obj *repeat(oper_t op) {
    Obj **vals, *o1 = op->v1;
    List *v1 = (List *)o1, *v;
    uval_t rep;
    Error *err;

    err = op->v2->obj->uval(op->v2, &rep, 8 * sizeof rep, op->epoint2);
    if (err != NULL) return &err->v;

    if (v1->len == 0 || rep == 0) return val_reference((o1->obj == TUPLE_OBJ) ? &null_tuple->v : &null_list->v);
    do {
        size_t i = 0, j, ln;
        if (rep == 1) {
            return val_reference(o1);
        }
        if (v1->len > SIZE_MAX / rep) break; /* overflow */
        v = (List *)val_alloc(o1->obj);
        ln = v1->len * rep;
        vals = lnew(v, ln);
        if (vals == NULL) break;
        while ((rep--) != 0) {
            for (j = 0;j < v1->len; j++, i++) {
                vals[i] = val_reference(v1->data[j]);
            }
        }
        return &v->v;
    } while (false);
    return (Obj *)new_error_mem(op->epoint3);
}

static MUST_CHECK Obj *slice(Obj *o1, oper_t op, size_t indx) {
    Obj **vals;
    Obj *o2 = op->v2;
    size_t offs2;
    List *v, *v1 = (List *)o1;
    Funcargs *args = (Funcargs *)o2;
    size_t i, ln;
    Error *err;
    bool more = args->len > indx + 1;
    linepos_t epoint2;

    if (args->len < 1) {
        err_msg_argnum(args->len, 1, 0, op->epoint2);
        return (Obj *)ref_none();
    }

    o2 = args->val[indx].val;
    epoint2 = &args->val[indx].epoint;

    ln = v1->len;

    if (o2->obj == LIST_OBJ) {
        List *v2 = (List *)o2;
        bool error;
        if (v2->len == 0) {
            return val_reference((o1->obj == TUPLE_OBJ) ? &null_tuple->v : &null_list->v);
        }
        v = (List *)val_alloc(o1->obj);
        vals = lnew(v, v2->len);
        if (vals == NULL) goto failed;
        error = true;
        for (i = 0; i < v2->len; i++) {
            err = indexoffs(v2->data[i], ln, &offs2, epoint2);
            if (err != NULL) {
                if (error) {err_msg_output(err); error = false;} 
                val_destroy(&err->v);
                vals[i] = (Obj *)ref_none();
                continue;
            }
            if (more) {
                Obj *vv = v1->data[offs2];
                vals[i] = vv->obj->slice(vv, op, indx + 1);
            } else {
                vals[i] = val_reference(v1->data[offs2]);
            }
        }
        return &v->v;
    }
    if (o2->obj == COLONLIST_OBJ) {
        uval_t length;
        ival_t offs, end, step;

        err = (Error *)sliceparams((Colonlist *)o2, ln, &length, &offs, &end, &step, epoint2);
        if (err != NULL) return &err->v;

        if (length == 0) {
            return val_reference((o1->obj == TUPLE_OBJ) ? &null_tuple->v : &null_list->v);
        }

        if (step == 1 && length == v1->len && !more) {
            return val_reference(o1); /* original tuple */
        }
        v = (List *)val_alloc(o1->obj);
        vals = lnew(v, length);
        if (vals == NULL) goto failed;
        i = 0;
        while ((end > offs && step > 0) || (end < offs && step < 0)) {
            if (more) {
                Obj *vv = v1->data[offs];
                vals[i] = vv->obj->slice(vv, op, indx + 1);
            } else {
                vals[i] = val_reference(v1->data[offs]);
            }
            i++; offs += step;
        }
        v->len = i;
        return &v->v;
    }
    err = indexoffs(o2, ln, &offs2, epoint2);
    if (err != NULL) return &err->v;
    if (more) {
        Obj *vv = v1->data[offs2];
        return vv->obj->slice(vv, op, indx + 1);
    }
    return val_reference(v1->data[offs2]);
failed:
    return (Obj *)new_error_mem(op->epoint3);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o1 = op->v1, *o2 = op->v2;
    List *v1 = (List *)o1;
    size_t i = 0;
    Obj **vals;

    if (op->op == &o_X) {
        return repeat(op); 
    }
    if (op->op == &o_IN) {
        return o2->obj->rcalc2(op);
    }
    if (o1->obj == o2->obj && (o1->obj == TUPLE_OBJ || o1->obj == LIST_OBJ)) {
        return calc2_list(op);
    }
    if (o2 == &none_value->v || o2->obj == ERROR_OBJ) {
        return val_reference(o2);
    }
    if (v1->len != 0) {
        bool error = true;
        List *list = (List *)val_alloc(o1->obj);
        vals = lnew(list, v1->len);
        if (vals == NULL) return (Obj *)new_error_mem(op->epoint3);
        for (;i < v1->len; i++) {
            Obj *val;
            Obj *oo1 = v1->data[i];
            op->v1 = oo1;
            op->v2 = o2;
            val = op->v1->obj->calc2(op);
            if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
            else if (op->op == &o_MIN || op->op == &o_MAX) {
                if (val == &true_value->v) val_replace(&val, oo1);
                else if (val == &false_value->v) val_replace(&val, o2);
            }
            vals[i] = val;
        }
        return &list->v;
    }
    return val_reference(o1);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    Obj *o1 = op->v1, *o2 = op->v2;
    List *v2 = (List *)o2;
    size_t i = 0;
    Obj **vals;

    if (op->op == &o_IN) {
        op->op = &o_EQ;
        for (;i < v2->len; i++) {
            Obj *result;
            op->v1 = o1;
            op->v2 = v2->data[i];
            result = o1->obj->calc2(op);
            if ((Bool *)result == true_value) {
                op->op = &o_IN;
                return result;
            }
            val_destroy(result);
        }
        op->op = &o_IN;
        return (Obj *)ref_bool(false_value);
    }
    if (o1->obj == o2->obj && (o1->obj == TUPLE_OBJ || o1->obj == LIST_OBJ)) {
        return calc2_list(op);
    }
    if (o1 == &none_value->v || o1->obj == ERROR_OBJ) {
        return o1->obj->calc2(op);
    }
    if (v2->len != 0) {
        bool error = true;
        List *v = (List *)val_alloc(o2->obj);
        vals = lnew(v, v2->len);
        if (vals == NULL) return (Obj *)new_error_mem(op->epoint3);
        for (;i < v2->len; i++) {
            Obj *val;
            Obj *oo2 = v2->data[i];
            op->v2 = oo2;
            op->v1 = o1;
            val = o1->obj->calc2(op);
            if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
            else if (op->op == &o_MIN || op->op == &o_MAX) {
                if (val == &true_value->v) val_replace(&val, o1);
                else if (val == &false_value->v) val_replace(&val, oo2);
            }
            vals[i] = val;
        }
        return &v->v;
    }
    return val_reference(o2);
}

static void init(Type *obj) {
    obj->destroy = destroy;
    obj->garbage = garbage;
    obj->same = same;
    obj->truth = truth;
    obj->len = len;
    obj->getiter = getiter;
    obj->next = next;
    obj->calc1 = calc1;
    obj->calc2 = calc2;
    obj->rcalc2 = rcalc2;
    obj->slice = slice;
    obj->repr = repr_listtuple;
}

void listobj_init(void) {
    new_type(&list_obj, T_LIST, "list", sizeof(List));
    init(&list_obj);
    list_obj.create = list_create;
    new_type(&tuple_obj, T_TUPLE, "tuple", sizeof(Tuple));
    init(&tuple_obj);
    tuple_obj.create = tuple_create;
    new_type(&addrlist_obj, T_ADDRLIST, "addresslist", sizeof(Addrlist));
    addrlist_obj.destroy = destroy;
    addrlist_obj.garbage = garbage;
    addrlist_obj.same = same;
    addrlist_obj.repr = repr_listtuple;
    new_type(&colonlist_obj, T_COLONLIST, "colonlist", sizeof(Colonlist));
    colonlist_obj.destroy = destroy;
    colonlist_obj.garbage = garbage;
    colonlist_obj.same = same;
    colonlist_obj.repr = repr_listtuple;

    null_tuple = new_tuple();
    null_tuple->len = 0;
    null_tuple->data = NULL;
    null_list = new_list();
    null_list->len = 0;
    null_list->data = NULL;
    null_addrlist = new_addrlist();
    null_addrlist->len = 0;
    null_addrlist->data = NULL;
}

void listobj_names(void) {
    new_builtin("list", val_reference(&LIST_OBJ->v));
    new_builtin("tuple", val_reference(&TUPLE_OBJ->v));
}

void listobj_destroy(void) {
#ifdef DEBUG
    if (null_tuple->v.refcount != 1) fprintf(stderr, "tuple %" PRIuSIZE "\n", null_tuple->v.refcount - 1);
    if (null_list->v.refcount != 1) fprintf(stderr, "list %" PRIuSIZE "\n", null_list->v.refcount - 1);
    if (null_addrlist->v.refcount != 1) fprintf(stderr, "addrlist %" PRIuSIZE "\n", null_addrlist->v.refcount - 1);
#endif

    val_destroy(&null_tuple->v);
    val_destroy(&null_list->v);
    val_destroy(&null_addrlist->v);
}
