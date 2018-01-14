/*
    $Id: bitsobj.h 1506 2017-04-30 13:52:04Z soci $

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
#ifndef BITSOBJ_H
#define BITSOBJ_H
#include "obj.h"
#include "stdbool.h"

extern struct Type *BITS_OBJ;

typedef uint32_t bdigit_t;
typedef struct Bits {
    Obj v;
    ssize_t len;
    size_t bits;
    bdigit_t val[2];
    bdigit_t *data;
} Bits;

extern Bits *null_bits;
extern Bits *inv_bits;
extern Bits *bits_value[2];

extern void bitsobj_init(void);
extern void bitsobj_names(void);
extern void bitsobj_destroy(void);

struct Str;
struct Bytes;

static inline Bits *ref_bits(Bits *v1) {
    v1->v.refcount++; return v1;
}

extern MUST_CHECK Obj *bits_from_hexstr(const uint8_t *, size_t *, size_t *, linepos_t);
extern MUST_CHECK Obj *bits_from_binstr(const uint8_t *, size_t *, size_t *, linepos_t);
extern MUST_CHECK Obj *bits_from_str(const struct Str *, linepos_t);
extern MUST_CHECK Obj *bits_from_bytes(const struct Bytes *, linepos_t);
extern MUST_CHECK Bits *bits_from_uval(uval_t, unsigned int);
extern MUST_CHECK Bits *ibits_from_bool(bool);
extern MUST_CHECK Bits *bits_from_bools(bool, bool);
extern MUST_CHECK Obj *float_from_bits(const Bits *, linepos_t);

#endif
