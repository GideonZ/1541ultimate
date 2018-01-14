/*
    $Id: addressobj.h 1390 2017-03-27 02:28:33Z soci $

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
#ifndef ADDRESSOBJ_H
#define ADDRESSOBJ_H
#include "obj.h"
#include "values.h"
#include "stdbool.h"

#define MAX_ADDRESS_MASK 0xffff

extern struct Type *ADDRESS_OBJ;

typedef enum Address_types {
    A_NONE,             /*       */
    A_IMMEDIATE,        /* #     */
    A_IMMEDIATE_SIGNED, /* #+    */
    A_XR,               /* ,x    */
    A_YR,               /* ,y    */
    A_ZR,               /* ,z    */
    A_RR,               /* ,r    */
    A_SR,               /* ,s    */
    A_DR,               /* ,d    */
    A_BR,               /* ,b    */
    A_KR,               /* ,k    */
    A_I,                /* )     */
    A_LI                /* ]     */
} Address_types;

typedef uint32_t atype_t;
typedef struct Address {
    Obj v;
    atype_t type;
    Obj *val;
} Address; 

extern void addressobj_init(void);
extern void addressobj_names(void);

static inline MUST_CHECK Address *new_address(Obj *val, atype_t type) {
    Address *v = (Address *)val_alloc(ADDRESS_OBJ);
    v->val = val;
    v->type = type;
    return v;
}

extern MUST_CHECK Obj *int_from_address(Address *, linepos_t);
extern MUST_CHECK Obj *float_from_address(Address *, linepos_t);
extern MUST_CHECK Obj *bits_from_address(Address *, linepos_t);
extern MUST_CHECK Obj *bytes_from_address(Address *, linepos_t);
extern bool check_addr(atype_t);
#endif
