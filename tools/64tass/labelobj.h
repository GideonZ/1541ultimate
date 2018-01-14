/*
    $Id: labelobj.h 1425 2017-04-01 00:55:33Z soci $

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
#ifndef LABELOBJ_H
#define LABELOBJ_H
#include "obj.h"
#include "str.h"
#include "stdbool.h"

extern struct Type *LABEL_OBJ;

typedef struct Label {
    Obj v;
    str_t name;
    str_t cfname;

    Obj *value;
    struct file_list_s *file_list;
    struct linepos_s epoint;
    bool ref;
    bool unused;
    bool shadowcheck;
    bool update_after;
    bool constant;
    bool owner;
    uint8_t usepass;
    uint8_t defpass;
    uint8_t strength;
} Label;

extern void labelobj_init(void);

#endif
