/*
    $Id: values.h 1407 2017-03-30 15:10:21Z soci $

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
#ifndef VALUES_H
#define VALUES_H
#include <stdio.h>
#include "attributes.h"

struct Type;
struct Obj;

extern FAST_CALL MALLOC struct Obj *val_alloc(struct Type *);
extern FAST_CALL void val_destroy(struct Obj *);
extern FAST_CALL void val_replace(struct Obj **, struct Obj *);
extern size_t val_print(struct Obj *, FILE *);

extern void destroy_values(void);
extern void garbage_collect(void);
#endif
