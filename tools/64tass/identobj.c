/*
    $Id: identobj.c 1494 2017-04-28 11:01:15Z soci $

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
#include "identobj.h"
#include "eval.h"

#include "typeobj.h"
#include "operobj.h"

static Type ident_obj;
static Type anonident_obj;

Type *IDENT_OBJ = &ident_obj;
Type *ANONIDENT_OBJ = &anonident_obj;

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *o2 = op->v2;
    switch (o2->obj->type) {
    case T_TUPLE:
    case T_LIST:
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

static MUST_CHECK Obj *rcalc2(oper_t op) {
    if (op->op == &o_MEMBER) {
        return op->v1->obj->calc2(op);
    }
    return obj_oper_error(op);
}

void identobj_init(void) {
    new_type(&ident_obj, T_IDENT, "ident", sizeof(Ident));
    ident_obj.calc2 = calc2;
    ident_obj.rcalc2 = rcalc2;
    new_type(&anonident_obj, T_ANONIDENT, "anonident", sizeof(Anonident));
    anonident_obj.calc2 = calc2;
    anonident_obj.rcalc2 = rcalc2;
}
