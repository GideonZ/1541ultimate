/*
    $Id: mfuncobj.h 1362 2017-03-19 19:45:45Z soci $

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
#ifndef MFUNCOBJ_H
#define MFUNCOBJ_H
#include "obj.h"
#include "str.h"

extern struct Type *MFUNC_OBJ;

struct Namespace;

struct mfunc_param_s {
    str_t name;
    str_t cfname;
    Obj *init;
    struct linepos_s epoint;
};

typedef struct Mfunc {
    Obj v;
    size_t argc;
    struct mfunc_param_s *param; 
    struct file_list_s *file_list;
    line_t line;
    size_t nslen;
    struct Namespace **namespaces;
} Mfunc;

extern void mfuncobj_init(void);
#endif
