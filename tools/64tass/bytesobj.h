/*
    $Id: bytesobj.h 1407 2017-03-30 15:10:21Z soci $

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
#ifndef BYTESOBJ_H
#define BYTESOBJ_H
#include "obj.h"

extern struct Type *BYTES_OBJ;

typedef struct Bytes {
    Obj v;
    ssize_t len;
    uint8_t *data;
    uint8_t val[16];
} Bytes;

extern Bytes *null_bytes;
extern Bytes *inv_bytes;

extern void bytesobj_init(void);
extern void bytesobj_names(void);
extern void bytesobj_destroy(void);

typedef enum Textconv_types {
    BYTES_MODE_TEXT,
    BYTES_MODE_SHIFT_CHECK,
    BYTES_MODE_SHIFT,
    BYTES_MODE_SHIFTL,
    BYTES_MODE_NULL_CHECK,
    BYTES_MODE_NULL,
    BYTES_MODE_PTEXT
} Textconv_types;

static inline Bytes *ref_bytes(Bytes *v1) {
    v1->v.refcount++; return v1;
}

extern MALLOC Bytes *new_bytes(size_t);

struct Str;

extern MUST_CHECK Bytes *bytes_from_u8(unsigned int);
extern MUST_CHECK Bytes *bytes_from_u16(unsigned int);
extern MUST_CHECK Bytes *bytes_from_uval(uval_t, unsigned int);
extern MUST_CHECK Obj *bytes_from_str(const struct Str *, linepos_t, Textconv_types);
extern MUST_CHECK Obj *float_from_bytes(const Bytes *, linepos_t);
#endif
