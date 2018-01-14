/*
    $Id: macroobj.c 1494 2017-04-28 11:01:15Z soci $

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
#include "macroobj.h"
#include "values.h"
#include "eval.h"

#include "operobj.h"
#include "typeobj.h"
#include "namespaceobj.h"
#include "intobj.h"
#include "noneobj.h"
#include "errorobj.h"

static Type macro_obj;
static Type segment_obj;
static Type struct_obj;
static Type union_obj;

Type *MACRO_OBJ = &macro_obj;
Type *SEGMENT_OBJ = &segment_obj;
Type *STRUCT_OBJ = &struct_obj;
Type *UNION_OBJ = &union_obj;

static FAST_CALL void macro_destroy(Obj *o1) {
    Macro *v1 = (Macro *)o1;
    while (v1->argc != 0) {
        --v1->argc;
        free((char *)v1->param[v1->argc].cfname.data);
        free((char *)v1->param[v1->argc].init.data);
    }
    free(v1->param);
}

static FAST_CALL bool macro_same(const Obj *o1, const Obj *o2) {
    const Macro *v1 = (const Macro *)o1, *v2 = (const Macro *)o2;
    size_t i;
    if (o1->obj != o2->obj || v1->file_list != v2->file_list || v1->line != v2->line || v1->retval != v2->retval || v1->argc != v2->argc) return false;
    for (i = 0; i < v1->argc; i++) {
        if (str_cmp(&v1->param[i].cfname, &v2->param[i].cfname) != 0) return false;
        if (str_cmp(&v1->param[i].init, &v2->param[i].init) != 0) return false;
    }
    return true;
}

static FAST_CALL void struct_destroy(Obj *o1) {
    Struct *v1 = (Struct *)o1;
    while (v1->argc != 0) {
        --v1->argc;
        free((char *)v1->param[v1->argc].cfname.data);
        free((char *)v1->param[v1->argc].init.data);
    }
    free(v1->param);
    val_destroy((Obj *)v1->names);
}

static FAST_CALL void struct_garbage(Obj *o1, int i) {
    Struct *v1 = (Struct *)o1;
    Obj *v;
    switch (i) {
    case -1:
        ((Obj *)v1->names)->refcount--;
        return;
    case 0:
        while (v1->argc != 0) {
            --v1->argc;
            free((char *)v1->param[v1->argc].cfname.data);
            free((char *)v1->param[v1->argc].init.data);
        }
        free(v1->param);
        return;
    case 1:
        v = (Obj *)v1->names;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static FAST_CALL bool struct_same(const Obj *o1, const Obj *o2) {
    const Struct *v1 = (const Struct *)o1, *v2 = (const Struct *)o2;
    size_t i;
    if (o1->obj != o2->obj || v1->size != v2->size || v1->file_list != v2->file_list || v1->line != v2->line || v1->retval != v2->retval || v1->argc != v2->argc) return false;
    if (v1->names != v2->names && !v1->names->v.obj->same(&v1->names->v, &v2->names->v)) return false;
    for (i = 0; i < v1->argc; i++) {
        if (str_cmp(&v1->param[i].cfname, &v2->param[i].cfname) != 0) return false;
        if (str_cmp(&v1->param[i].init, &v2->param[i].init) != 0) return false;
    }
    return true;
}

static MUST_CHECK Obj *struct_size(Obj *o1, linepos_t UNUSED(epoint)) {
    Struct *v1 = (Struct *)o1;
    return (Obj *)int_from_size(v1->size);
}

static MUST_CHECK Obj *struct_calc2(oper_t op) {
    if (op->op == &o_MEMBER) {
        return namespace_member(op, ((Struct *)op->v1)->names);
    }
    if (op->v2 == &none_value->v || op->v2->obj == ERROR_OBJ) return val_reference(op->v2);
    return obj_oper_error(op);
}

void macroobj_init(void) {
    new_type(&macro_obj, T_MACRO, "macro", sizeof(Macro));
    macro_obj.destroy = macro_destroy;
    macro_obj.same = macro_same;
    new_type(&segment_obj, T_SEGMENT, "segment", sizeof(Segment));
    segment_obj.destroy = macro_destroy;
    segment_obj.same = macro_same;
    new_type(&struct_obj, T_STRUCT, "struct", sizeof(Struct));
    struct_obj.destroy = struct_destroy;
    struct_obj.garbage = struct_garbage;
    struct_obj.same = struct_same;
    struct_obj.size = struct_size;
    struct_obj.calc2 = struct_calc2;
    new_type(&union_obj, T_UNION, "union", sizeof(Union));
    union_obj.destroy = struct_destroy;
    union_obj.garbage = struct_garbage;
    union_obj.same = struct_same;
    union_obj.size = struct_size;
    union_obj.calc2 = struct_calc2;
}
