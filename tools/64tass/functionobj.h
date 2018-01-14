/*
    $Id: functionobj.h 1390 2017-03-27 02:28:33Z soci $

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
#ifndef FUNCTIONOBJ_H
#define FUNCTIONOBJ_H
#include "obj.h"
#include "str.h"

extern struct Type *FUNCTION_OBJ;

typedef enum Function_types {
    F_NONE, F_FLOOR, F_CEIL, F_ROUND, F_TRUNC, F_FRAC, F_SQRT, F_CBRT, F_LOG,
    F_LOG10, F_EXP, F_SIN, F_COS, F_TAN, F_ACOS, F_ASIN, F_ATAN, F_RAD, F_DEG,
    F_COSH, F_SINH, F_TANH, F_HYPOT, F_ATAN2, F_POW, F_SIGN, F_ABS, F_ALL,
    F_ANY, F_SIZE, F_LEN, F_RANGE, F_REPR, F_FORMAT, F_RANDOM, F_SORT
} Function_types;

typedef struct Function {
    Obj v;
    str_t name;
    int name_hash;
    Function_types func;
} Function;

extern void functionobj_init(void);
extern void functionobj_names(void);
extern void random_reseed(Obj *, linepos_t);

#endif
