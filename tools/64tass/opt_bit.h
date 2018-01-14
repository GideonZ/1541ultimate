/*
    $Id: opt_bit.h 1490 2017-04-27 22:28:27Z soci $

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
#ifndef OPT_BIT_H
#define OPT_BIT_H
#include "stdbool.h"
#include "attributes.h"

struct Bit;

typedef enum Bit_types {
    BU, B0, B1
} Bit_types;

extern struct Bit *new_bit0(void);
extern struct Bit *new_bit1(void);
extern MALLOC struct Bit *new_bitu(void);
extern struct Bit *new_bit(Bit_types);
extern void del_bit(struct Bit *);
extern struct Bit *ref_bit(struct Bit *);
extern struct Bit *inv_bit(struct Bit *);
extern void mod_bit(struct Bit *, Bit_types);
extern Bit_types get_bit(const struct Bit *);
extern void reset_bit(struct Bit **);
extern void reset_reg8(struct Bit **);
extern bool eq_bit(const struct Bit *, const struct Bit *);
extern bool neq_bit(const struct Bit *, const struct Bit *);
extern struct Bit *add_bit(struct Bit *, struct Bit *, struct Bit *, struct Bit **);
extern struct Bit *v_bit(struct Bit *, struct Bit *, struct Bit *);
extern struct Bit *ld_bit(struct Bit *, struct Bit *);
extern struct Bit *and_bit(struct Bit *, struct Bit *);
extern struct Bit *or_bit(struct Bit *, struct Bit *);
extern struct Bit *xor_bit(struct Bit *, struct Bit *);
extern void init_opt_bit(void);
extern void destroy_opt_bit(void);

#endif
