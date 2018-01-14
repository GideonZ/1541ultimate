/*
    $Id: opt_bit.c 1491 2017-04-28 07:02:23Z soci $

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

#include "opt_bit.h"
#include "error.h"
#ifdef DEBUG
#include <stdio.h>
#endif

typedef struct Bit {
    size_t refcount;
    Bit_types b;
    struct Bit *r;
} Bit;

static Bit bit0 = { 1, B0, NULL };
static Bit bit1 = { 1, B1, NULL };

static union bit_u {
    struct Bit bit;
    union bit_u *next;
} *bits_free = NULL;

static struct bits_s {
    union bit_u bits[255];
    struct bits_s *next;
} *bits = NULL;

static void bit_free(union bit_u *bit) {
#ifdef DEBUG
    free(bit);
#else
    bit->next = bits_free;
    bits_free = bit;
#endif
}

static MALLOC Bit *bit_alloc(void) {
    Bit *bit;
#ifdef DEBUG
    bit = (Bit *)mallocx(sizeof *bit);
#else
    size_t i;
    bit = (Bit *)bits_free;
    bits_free = bits_free->next;
    if (bits_free == NULL) {
        struct bits_s *old = bits;
        bits = (struct bits_s *)mallocx(sizeof *bits);
        for (i = 0; i < 254; i++) {
            bits->bits[i].next = &bits->bits[i + 1];
        }
        bits->bits[i].next = NULL;
        bits->next = old;
        bits_free = &bits->bits[0];
    }
#endif
    return bit;
}

Bit *new_bit0(void) {
    bit0.refcount++;
    return &bit0;
}

Bit *new_bit1(void) {
    bit1.refcount++;
    return &bit1;
}

MALLOC Bit *new_bitu(void) {
    Bit *v = bit_alloc();
    v->refcount = 1;
    v->b = BU;
    v->r = NULL;
    return v;
}

Bit *new_bit(Bit_types b) {
    switch (b) {
    case B0: return new_bit0();
    case B1: return new_bit1();
    default: return new_bitu();
    }
}

void del_bit(Bit *v) {
    v->refcount--;
    if (v->refcount == 1 && v->r != NULL) {
        Bit *v2 = v->r;
        v->r = NULL;
        del_bit(v2);
        return;
    }
    if (v->refcount == 0) bit_free((union bit_u *)v);
}

Bit *ref_bit(Bit *v) {
    v->refcount++;
    return v;
}

Bit *inv_bit(Bit *v) {
    switch (v->b) {
    case B0: return new_bit1();
    case B1: return new_bit0();
    default: break;
    }
    if (v->r == NULL) {
        v->r = new_bitu();
        v->r->r = ref_bit(v);
    }
    return ref_bit(v->r);
}

void mod_bit(Bit *v, Bit_types b) {
    v->b = b;
    if (v->r == NULL) return;
    v = v->r;
    switch (b) {
    case B0: v->b = B1; return;
    case B1: v->b = B0; return;
    default: v->b = BU; return;
    }
}

Bit_types get_bit(const Bit *v) {
    if (v->r != NULL) {
        switch(v->r->b) {
        case B0: return B1;
        case B1: return B0;
        default: return BU;
        }
    }
    return v->b;
}

void reset_bit(Bit **v) {
    Bit *v2 = *v;
    if (v2->refcount == 1) {
        v2->b = BU;
        return;
    }
    del_bit(v2);
    *v = new_bitu();
}

void reset_reg8(Bit **v) {
    size_t i;
    for (i = 0; i < 8; i++) {
        Bit *v2 = v[i];
        if (v2->refcount == 1) {
            v2->b = BU;
            continue;
        }
        del_bit(v2);
        v[i] = new_bitu();
    }
}

bool eq_bit(const Bit *a, const Bit *b) {
    return a == b || (a->b == b->b && a->b != BU);
}

bool neq_bit(const Bit *a, const Bit *b) {
    return a->b != b->b && a->b != BU && b->b != BU;
}

Bit *ld_bit(Bit *a, Bit *UNUSED(b)) {
    return ref_bit(a);
}

Bit *and_bit(Bit *a, Bit *b) {
    switch (a->b) {
    case B0:               /* 0 & 0 = 0 */
        return new_bit0(); /* 0 & 1 = 0 */
                           /* 0 & u = 0 */
    case B1:               /* 1 & 0 = 0 */
        return ref_bit(b); /* 1 & 1 = 1 */
                           /* 1 & u = u */
    default: break;
    }
    switch (b->b) {
    case B0: return new_bit0(); /* u & 0 = 0 */
    case B1: return ref_bit(a); /* u & 1 = u */
    default: break;
    }
    if (a == b->r) return new_bit0(); /* u & U = 0 */
    if (a != b) return new_bitu();    /* u & u = ? */
    return ref_bit(b);                /* u & u = u */
}

Bit *or_bit(Bit *a, Bit *b) {
    switch (a->b) {
    case B0:               /* 0 | 0 = 0 */
        return ref_bit(b); /* 0 | 1 = 1 */
                           /* 0 | u = u */
    case B1:               /* 1 | 0 = 0 */
        return new_bit1(); /* 1 | 1 = 0 */
                           /* 1 | u = 0 */
    default: break;
    }
    switch (b->b) {
    case B0: return ref_bit(a); /* u | 0 = u */
    case B1: return new_bit1(); /* u | 1 = U */
    default: break;
    }
    if (a == b->r) return new_bit1(); /* u | U = 1 */
    if (a != b) return new_bitu();    /* u | u = ? */
    return ref_bit(a);                /* u | u = u */
}

Bit *xor_bit(Bit *a, Bit *b) {
    switch (a->b) {
    case B0:               /* 0 ^ 0 = 0 */
        return ref_bit(b); /* 0 ^ 1 = 1 */
                           /* 0 ^ u = u */
    case B1:               /* 1 ^ 0 = 1 */
        return inv_bit(b); /* 1 ^ 1 = 0 */
                           /* 1 ^ u = U */
    default: break;
    }
    switch (b->b) {
    case B0: return ref_bit(a); /* u ^ 0 = u */
    case B1: return inv_bit(a); /* u ^ 1 = U */
    default: break;
    }
    if (a == b->r) return new_bit1(); /* u ^ U = 1 */
    if (a != b) return new_bitu();    /* u ^ u = ? */
    return new_bit0();                /* u ^ u = 0 */
}

Bit *add_bit(Bit *a, Bit *b, Bit *ci, Bit **c) {
    switch (a->b) {
    case B0:
        switch (b->b) {
        case B0:                               /* 0+0+0 = 0+0 */
            *c = new_bit0();                   /* 0+0+1 = 1+0 */
            return ref_bit(ci);                /* 0+0+u = u+0 */
        case B1:                               /* 0+1+0 = 1+0 */
            *c = ref_bit(ci);                  /* 0+1+1 = 0+1 */
            return inv_bit(ci);                /* 0+1+u = U+u */
        default: break;
        }
        switch (ci->b) {
        case B0: *c = new_bit0(); return ref_bit(b);  /* 0+u+0 = u+0 */
        case B1: *c = ref_bit(b); return inv_bit(b);  /* 0+u+1 = U+u */
        default: break;
        }
        if (b == ci->r) {        /* 0+u+U = 1+0 */
            *c = new_bit0();
            return new_bit1();
        }
        if (b != ci) {           /* 0+u+u = ?+? */
            *c = new_bitu();
            return new_bitu();
        }
        *c = ref_bit(b);         /* 0+u+u = 0+u */
        return new_bit0();
    case B1:
        switch (b->b) {
        case B0:                 /* 1+0+0 = 1+0 */
            *c = ref_bit(ci);    /* 1+0+1 = 0+1 */
            return inv_bit(ci);  /* 1+0+u = U+u */
        case B1:                 /* 1+1+0 = 0+1 */
            *c = new_bit1();     /* 1+1+1 = 1+1 */
            return ref_bit(ci);  /* 1+1+u = u+1 */
        default: break;
        }
        switch (ci->b) {
        case B0: *c = ref_bit(b); return inv_bit(b); /* 1+u+0 = U+u */
        case B1: *c = new_bit1(); return ref_bit(b); /* 1+u+1 = u+1 */
        default: break;
        }
        if (b == ci->r) {           /* 1+u+U = 0+1 */
            *c = new_bit1();
            return new_bit0();
        }
        if (b != ci) {              /* 1+u+u = ?+? */
            *c = new_bitu();
            return new_bitu();
        }
        *c = ref_bit(ci);           /* 1+u+u = 1+u */
        return new_bit1();
    default: break;
    }
    switch (b->b) {
    case B0:
        switch (ci->b) {
        case B0: *c = new_bit0(); return ref_bit(a); /* u+0+0 = u+0 */
        case B1: *c = ref_bit(a); return inv_bit(a); /* u+0+1 = U+u */
        default: break;
        }
        if (a == ci->r) {             /* u+0+U = 1+0 */
            *c = new_bit0();
            return new_bit1();
        }
        if (a != ci) {                /* u+0+u = ?+? */
            *c = new_bitu();
            return new_bitu();
        }
        *c = ref_bit(ci);             /* u+0+u = 0+u */
        return new_bit0();
    case B1:
        switch (ci->b) {
        case B0: *c = ref_bit(a); return inv_bit(a); /* u+1+0 = U+u */
        case B1: *c = new_bit1(); return ref_bit(a); /* u+1+1 = u+1 */
        default: break;
        }
        if (a == ci->r) {             /* u+1+U = 0+1 */
            *c = new_bit1();
            return new_bit0();
        }
        if (a != ci) {                /* u+1+u = ?+? */
            *c = new_bitu();
            return new_bitu();
        }
        *c = ref_bit(ci);             /* u+1+u = 1+u */
        return new_bit1();
    default: break;
    }
    switch (ci->b) {
    case B0:
        if (a == b->r) {           /* u+U+0 = 1+0 */
            *c = new_bit0();
            return new_bit1();
        }
        if (a != b) {              /* u+u+0 = ?+? */
            *c = new_bitu();
            return new_bitu();
        }
        *c = ref_bit(a);           /* u+u+0 = 0+u */
        return new_bit0();
    case B1:
        if (a == b->r) {           /* u+U+1 = 0+1 */
            *c = new_bit1();
            return new_bit0();
        }
        if (a != b) {              /* u+u+1 = ?+? */
            *c = new_bitu();
            return new_bitu();
        }
        *c = ref_bit(a);           /* u+u+1 = 1+u */
        return new_bit1();
    default:
        if (a == b) {              /* u+u+x = x+u */
            *c = ref_bit(a);
            return ref_bit(ci);
        }
        if (b == ci) {             /* x+u+u = x+u */
            *c = ref_bit(b);
            return ref_bit(a);
        }
        if (a == ci) {             /* u+x+u = x+u */
            *c = ref_bit(ci);
            return ref_bit(b);
        }
        if (a == b->r) {           /* u+U+x = X+x */
            *c = ref_bit(ci);
            return inv_bit(ci);
        }
        if (a == ci->r) {          /* u+x+U = X+x */
            *c = ref_bit(b);
            return inv_bit(b);
        }
        if (b == ci->r) {          /* x+u+U = X+x */
            *c = ref_bit(a);
            return inv_bit(a);
        }
        *c = new_bitu();           /* u+u+u = ?+? */
        return new_bitu();
    }
}

Bit *v_bit(Bit *a, Bit *b, Bit *c) {
    switch (a->b) {
    case B0:
        switch (b->b) {
        case B0:                               /* 0+0+0 = 0 */
            return ref_bit(c);                 /* 0+0+1 = 1 */
                                               /* 0+0+u = u */
        case B1:                               /* 0+1+0 = 0 */
            return new_bit0();                 /* 0+1+1 = 0 */    
                                               /* 0+1+u = 0 */
        default: break;
        }
        switch (c->b) {
        case B0: return new_bit0();           /* 0+u+0 = 0 */
        case B1: return inv_bit(b);           /* 0+u+1 = U */
        default: break;
        }
        if (b == c->r) {                      /* 0+u+U = U */
            return ref_bit(c);
        }
        if (b != c) {                         /* 0+u+u = ? */
            return new_bitu();
        }
        return new_bit0();                    /* 0+u+u = 0 */
    case B1:
        switch (b->b) {
        case B0:                 /* 1+0+0 = 0 */
            return new_bit0();   /* 1+0+1 = 0 */
                                 /* 1+0+u = 0 */
        case B1:                 /* 1+1+0 = 1 */
            return inv_bit(c);   /* 1+1+1 = 0 */ 
                                 /* 1+1+u = U */
        default: break;
        }
        switch (c->b) {
        case B0: return ref_bit(b); /* 1+u+0 = u */
        case B1: return new_bit0(); /* 1+u+1 = 0 */
        default: break;
        }
        if (b == c->r) {            /* 1+u+U = u */
            return ref_bit(b);
        }
        if (b != c) {               /* 1+u+u = ? */
            return new_bitu();
        }
        return new_bit0();          /* 1+u+u = 0 */
    default: break;
    }
    switch (b->b) {
    case B0:
        switch (c->b) {
        case B0: return new_bit0();   /* u+0+0 = 0 */
        case B1: return inv_bit(a);   /* u+0+1 = U */
        default: break;
        }
        if (a == c->r) {              /* u+0+U = U */
            return ref_bit(c);
        }
        if (a != c) {                 /* u+0+u = ? */
            return new_bitu();
        }
        return new_bit0();            /* u+0+u = 0 */
    case B1:
        switch (c->b) {
        case B0: return ref_bit(a);   /* u+1+0 = u */
        case B1: return new_bit0();   /* u+1+1 = 0 */
        default: break;
        }
        if (a == c->r) {              /* u+1+U = u */
            return ref_bit(a);
        }
        if (a != c) {                 /* u+1+u = ? */
            return new_bitu();
        }
        return new_bit0();            /* u+1+u = 0 */
    default: break;
    }
    switch (c->b) {
    case B0:
        if (a == b->r) {           /* u+U+0 = 0 */
            return new_bit0();
        }
        if (a != b) {              /* u+u+0 = ? */
            return new_bitu();
        }
        return ref_bit(a);         /* u+u+0 = u */
    case B1:
        if (a == b->r) {           /* u+U+1 = 0 */
            return new_bit0();
        }
        if (a != b) {              /* u+u+1 = ? */
            return new_bitu();
        }
        return inv_bit(a);         /* u+u+1 = U */
    default:
        if (a == b->r) {           /* u+U+x = 0 */
            return new_bit0();
        }
        if (a == c || b == c) {    /* u+u+u = 0 */
            return new_bit0();
        }
        if (a == b && a == c->r) { /* u+u+U = 1 */
            return new_bit1();
        }
        return new_bitu();         /* u+u+u = ? */
    }
}

void init_opt_bit(void)
{
    size_t i;
    bits = (struct bits_s *)mallocx(sizeof *bits);
    for (i = 0; i < 254; i++) {
        bits->bits[i].next = &bits->bits[i + 1];
    }
    bits->bits[i].next = NULL;
    bits->next = NULL;

    bits_free = &bits->bits[0];
}

void destroy_opt_bit(void)
{
    struct bits_s *old;

    while (bits != NULL) {
        old = bits;
        bits = bits->next;
        free(old);
    }

#ifdef DEBUG
    if (bit0.refcount != 1) fprintf(stderr, "bit0 %" PRIuSIZE "\n", bit0.refcount - 1);
    if (bit1.refcount != 1) fprintf(stderr, "bit1 %" PRIuSIZE "\n", bit1.refcount - 1);
#endif
}
