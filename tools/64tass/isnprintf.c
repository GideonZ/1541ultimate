/*
   Unix snprintf implementation.
   Version 1.3

   Adapted for use in 64tass by Soci/Singular
   $Id: isnprintf.c 1507 2017-04-30 14:20:11Z soci $

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Library General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

   Revision History:

   1.3:
      *  add #include <config.h> ifdef HAVE_CONFIG_H
      *  cosmetic change, when exponent is 0 print xxxE+00
         instead of xxxE-00
   1.2:
      *  put the program under LGPL.
   1.1:
      *  added changes from Miles Bader
      *  corrected a bug with %f
      *  added support for %#g
      *  added more comments :-)
   1.0:
      *  supporting must ANSI syntaxic_sugars
   0.0:
      *  suppot %s %c %d

 THANKS(for the patches and ideas):
     Miles Bader
     Cyrille Rustom
     Jacek Slabocewiz
     Mike Parker(mouse)

 Alain Magloire: alainm@rcsm.ee.mcgill.ca
*/

#include "isnprintf.h"
#include <string.h>
#include "unicode.h"
#include "eval.h"
#include "error.h"

#include "floatobj.h"
#include "strobj.h"
#include "intobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"

#if _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
#else
#ifndef snprintf
#define snprintf(str, size, format, var, var2) sprintf(str, format, var, var2)
#endif
#endif

static Str return_value;
static size_t returnsize = 0;
static size_t none;

/* this struct holds everything we need */
struct DATA {
  const uint8_t *pf;
  const uint8_t *pfend;
/* FLAGS */
  int width, precision;
  uchar_t pad;
  bool left, square, space, plus, star_w, star_p, dot;
};

/* those are defines specific to snprintf to hopefully
 * make the code clearer :-)
 */
#define NOT_FOUND (-1)

static size_t listp;
static const struct values_s *list;
static size_t largs;
static struct values_s dummy = {NULL, {0, 0}};

static const struct values_s *next_arg(void) {
    if (none == 0 && largs > listp) return &list[listp++];
    listp++;
    dummy.val = &none_value->v;
    return &dummy;
}

static void PUT_CHAR(uchar_t c) {
    uint8_t *p;
    return_value.chars++;
    p = return_value.data;
    if (return_value.len + 6 >= returnsize) {
        returnsize += 256;
        if (returnsize < 256) err_msg_out_of_memory(); /* overflow */
        p = (uint8_t *)reallocx(p, returnsize);
        return_value.data = p;
    }
    if (c != 0 && c < 0x80) {
        p[return_value.len++] = (uint8_t)c;
        return;
    }
    p = utf8out(c, p + return_value.len);
    return_value.len = (size_t)(p - return_value.data);
}

/* pad right */
static inline void PAD_RIGHT(struct DATA *p)
{
    if (p->width > 0 && !p->left) {
        for (; p->width > 0; p->width--) PUT_CHAR(p->pad);
    }
}

static void PAD_RIGHT2(struct DATA *p, uint8_t c, bool minus, size_t ln)
{
    size_t n = 0;
    p->width -= ln;
    if (p->precision > 0 && (size_t)p->precision > ln) {
        n = (size_t)p->precision - ln;
        p->width -= n;
    }
    if (minus || p->plus || p->space) p->width--;
    if (c != 0 && p->square) p->width--;
    if (p->pad != '0') PAD_RIGHT(p);
    if (minus) PUT_CHAR('-');
    else if (p->plus) PUT_CHAR('+');
    else if (p->space) PUT_CHAR(' ');
    if (c != 0 && p->square) PUT_CHAR(c);
    if (p->pad == '0') PAD_RIGHT(p);
    for (;n > 0; n--) PUT_CHAR('0');
}

/* pad left */
static MUST_CHECK Obj *PAD_LEFT(struct DATA *p)
{
    if (p->width > 0 && p->left) {
        for (; p->width > 0; p->width--) PUT_CHAR(p->pad);
    }
    return NULL;
}

/* if width and prec. in the args */
static MUST_CHECK Obj *star_args(struct DATA *p)
{
    ival_t ival;
    Error *err;

    if (p->star_w) {
        const struct values_s *v = next_arg();
        Obj *val = v->val;
        if (val == &none_value->v) none = listp;
        else {
            err = val->obj->ival(val, &ival, 8 * (sizeof ival), &v->epoint);
            if (err != NULL) return &err->v;
            p->width = abs(ival);
            if (ival < 0) p->left = true;
        }
    }
    if (p->star_p) {
        const struct values_s *v = next_arg();
        Obj *val = v->val;
        if (val == &none_value->v) none = listp;
        else {
            err = val->obj->ival(val, &ival, 8 * (sizeof ival), &v->epoint);
            if (err != NULL) return &err->v;
            p->precision = ival > 0 ? ival : 0;
        }
    }
    return NULL;
}

/* for %d and friends, it puts in holder
 * the representation with the right padding
 */
static inline MUST_CHECK Obj *decimal(struct DATA *p, const struct values_s *v)
{
    bool minus;
    Obj *val = v->val, *err, *err2;
    Str *str;
    size_t i;

    if (val == &none_value->v) {
        none = listp;
        err = (Obj *)ref_int(int_value[0]);
    } else {
        err = INT_OBJ->create(val, &v->epoint);
        if (err->obj != INT_OBJ) return err;
    }

    minus = (((Int *)err)->len < 0);
    err2 = err->obj->repr(err, &v->epoint, SIZE_MAX);
    val_destroy(err);
    if (err2->obj != STR_OBJ) return err2;

    str = (Str *)err2;
    i = minus ? 1 : 0;
    PAD_RIGHT2(p, 0, minus, str->len - i);
    for (; i < str->len; i++) PUT_CHAR(str->data[i]);
    val_destroy(&str->v);
    return PAD_LEFT(p);
}

/* for %x %X hexadecimal representation */
static inline MUST_CHECK Obj *hexa(struct DATA *p, const struct values_s *v)
{
    bool minus;
    Obj *val = v->val, *err;
    Int *integer;
    const char *hex = (*p->pf == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
    unsigned int bp, b;
    size_t bp2;

    if (val == &none_value->v) {
        none = listp;
        err = (Obj *)ref_int(int_value[0]);
    } else {
        err = INT_OBJ->create(val, &v->epoint);
        if (err->obj != INT_OBJ) return err;
    }

    integer = (Int *)err;
    minus = (integer->len < 0);
    bp2 = (size_t)(minus ? -integer->len : integer->len);
    bp = b = 0;
    do {
        if (bp == 0) {
            if (bp2 == 0) break;
            bp2--;
            bp = 8 * sizeof(digit_t) - 4;
        } else bp -= 4;
        b = (integer->data[bp2] >> bp) & 0xf;
    } while (b == 0);

    PAD_RIGHT2(p, '$', minus, bp / 4 + bp2 * (sizeof(digit_t) * 2) + 1);
    for (;;) {
        PUT_CHAR((uint8_t)hex[b]);
        if (bp == 0) {
            if (bp2 == 0) break;
            bp2--;
            bp = 8 * sizeof(digit_t) - 4;
        } else bp -= 4;
        b = (integer->data[bp2] >> bp) & 0xf;
    }
    val_destroy(&integer->v);
    return PAD_LEFT(p);
}

/* for %b binary representation */
static inline MUST_CHECK Obj *bin(struct DATA *p, const struct values_s *v)
{
    bool minus;
    Obj *val = v->val, *err;
    Int *integer;
    unsigned int bp, b;
    size_t bp2;

    if (val == &none_value->v) {
        none = listp;
        err = (Obj *)ref_int(int_value[0]);
    } else {
        err = INT_OBJ->create(val, &v->epoint);
        if (err->obj != INT_OBJ) return err;
    }

    integer = (Int *)err;
    minus = (integer->len < 0);
    bp2 = (size_t)(minus ? -integer->len : integer->len);
    bp = b = 0;
    do {
        if (bp == 0) {
            if (bp2 == 0) break;
            bp2--;
            bp = 8 * sizeof(digit_t) - 1;
        } else bp--;
        b = (integer->data[bp2] >> bp) & 1;
    } while (b == 0);

    PAD_RIGHT2(p, '%', minus, bp + bp2 * (sizeof(digit_t) * 8) + 1);
    for (;;) {
        PUT_CHAR('0' + b);
        if (bp == 0) {
            if (bp2 == 0) break;
            bp2--;
            bp = 8 * sizeof(digit_t) - 1;
        } else bp--;
        b = (integer->data[bp2] >> bp) & 1;
    }
    val_destroy(&integer->v);
    return PAD_LEFT(p);
}

/* %c chars */
static inline MUST_CHECK Obj *chars(const struct values_s *v)
{
    uval_t uval;
    Obj *val = v->val;
    Error *err;

    if (val == &none_value->v) {
        none = listp;
        uval = 0;
    } else {
        err = val->obj->uval(val, &uval, 24, &v->epoint);
        uval &= 0xffffff;
        if (err != NULL) return &err->v;
    }

    PUT_CHAR(uval);
    return NULL;
}

/* %s strings */
static inline MUST_CHECK Obj *strings(struct DATA *p, const struct values_s *v)
{
    int i;
    const uint8_t *tmp;
    uchar_t ch;
    Obj *val = v->val, *err;
    Str *str;

    if (val == &none_value->v) {
        none = listp;
        return NULL;
    }
    if (*p->pf == 'r') err = val->obj->repr(val, &v->epoint, SIZE_MAX);
    else err = STR_OBJ->create(val, &v->epoint);
    if (err->obj != STR_OBJ) {
        if (err == &none_value->v) {
            none = listp;
            val_destroy(err);
            return NULL;
        }
        return err;
    }

    str = (Str *)err;
    tmp = str->data;
    i = (int)str->chars;
    if (p->dot) { /* the smallest number */
        i = (i < p->precision ? i : p->precision);
    }
    if (i < 0) i = 0;
    p->width -= i;
    PAD_RIGHT(p);
    while (i-- > 0) { /* put the string */
        ch = *tmp;
        if ((ch & 0x80) != 0) tmp += utf8in(tmp, &ch); else tmp++;
        PUT_CHAR(ch);
    }
    val_destroy(&str->v);
    return PAD_LEFT(p);
}

/* %f or %g  floating point representation */
static inline MUST_CHECK Obj *floating(struct DATA *p, const struct values_s *v)
{
    char tmp[400], *t, form[10];
    bool minus;
    double d;
    Obj *val = v->val, *err;
    int l;

    if (val == &none_value->v) {
        none = listp;
        d = 0;
    } else {
        err = FLOAT_OBJ->create(val, &v->epoint);
        if (err->obj != FLOAT_OBJ) return err;
        d = ((Float *)err)->real;
        val_destroy(err);
    }
    if (d < 0.0) { d = -d; minus = true;} else minus = false;
    if (!p->dot) p->precision = 6;
    t = form;
    *t++ = '%';
    if (p->square) *t++ = '#';
    *t++ = '.';
    *t++ = '*';
    *t++ = (char)*p->pf;
    *t++ = 0;
    l = snprintf(tmp, sizeof tmp, form, (p->precision < 80) ? (p->precision > 0 ? p->precision : 0) : 80, d);
    t = tmp;

    p->precision = 0;
    PAD_RIGHT2(p, 0, minus, (l > 0) ? (size_t)l : 0);
    while (*t != 0) { /* the integral */
        PUT_CHAR((uint8_t)*t);
        t++;
    }
    return PAD_LEFT(p);
}

MUST_CHECK Obj *isnprintf(Funcargs *vals, linepos_t epoint)
{
    struct values_s *v = vals->val;
    size_t args = vals->len;
    Obj *err;
    Str *str;
    struct DATA data;
    int state;

    if (args < 1) {
        err_msg_argnum(args, 1, 0, epoint);
        return (Obj *)ref_none();
    }
    switch (v[0].val->obj->type) {
    case T_ERROR:
    case T_NONE:
        return val_reference(v[0].val);
    case T_STR: break;
    default:
        err_msg_wrong_type(v[0].val, STR_OBJ, &v[0].epoint);
        return (Obj *)ref_none();
    }
    data.pf = ((Str *)v[0].val)->data;
    data.pfend = data.pf +((Str *)v[0].val)->len;

    listp = 0;
    list = &v[1];
    largs = args - 1;

    return_value.data = NULL;
    return_value.len = 0;
    return_value.chars = 0;
    none = returnsize = 0;

    for (; data.pf < data.pfend; data.pf++) {
        uchar_t c = *data.pf;
        if (c != '%') {
            if ((c & 0x80) != 0) data.pf += utf8in(data.pf, &c) - 1;
            PUT_CHAR(c);  /* add the char the string */
            continue;
        }
        /* reset the flags. */
        data.precision = data.width = NOT_FOUND;
        data.star_w = data.star_p = false;
        data.square = data.plus = data.space = false;
        data.left = false; data.dot = false;
        data.pad = ' ';
        for (state = 1; data.pf < data.pfend - 1 && state != 0;) {
            c = *(++data.pf);
            switch (c) {
            case 'e':
            case 'E':  /* Exponent double */
            case 'f':  /* float, double */
            case 'F':
            case 'a':
            case 'A':
            case 'g':
            case 'G':
                err = star_args(&data);
                if (err != NULL) goto error;
                err = floating(&data, next_arg());
                if (err != NULL) goto error;
                state = 0;
                break;
            case 'd':  /* decimal */
                err = star_args(&data);
                if (err != NULL) goto error;
                err = decimal(&data, next_arg());
                if (err != NULL) goto error;
                state = 0;
                break;
            case 'x':
            case 'X':  /* hexadecimal */
                err = star_args(&data);
                if (err != NULL) goto error;
                err = hexa(&data, next_arg());
                if (err != NULL) goto error;
                state = 0;
                break;
            case 'b':  /* binary */
                err = star_args(&data);
                if (err != NULL) goto error;
                err = bin(&data, next_arg());
                if (err != NULL) goto error;
                state = 0;
                break;
            case 'c': /* character */
                err = chars(next_arg());
                if (err != NULL) goto error;
                state = 0;
                break;
            case 'r':  /* repr */
            case 's':  /* string */
                err = star_args(&data);
                if (err != NULL) goto error;
                err = strings(&data, next_arg());
                if (err != NULL) goto error;
                state = 0;
                break;
            case '%':  /* nothing just % */
                PUT_CHAR('%');
                state = 0;
                break;
            case ' ': 
                data.space = true; 
                break;
            case '#': 
                data.square = true; 
                break;
            case '*': 
                if (data.width == NOT_FOUND) {
                    data.width = 0;
                    data.star_w = true;
                } else if (data.dot && data.precision == NOT_FOUND) {
                    data.precision = 0;
                    data.star_p = true;
                }
                break;
            case '+':
                data.plus = true;
                break;
            case '-': 
                data.left = true; 
                break;
            case '.': 
                data.dot = true; 
                if (data.width == NOT_FOUND) data.width = 0;
                break;
            case '0': 
                if (data.width == NOT_FOUND) {
                    data.pad = '0'; 
                    break;
                }
                /* fall through */
            case '1':
            case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
                c -= '0';
                if (data.dot && !data.star_p) {
                    data.precision = ((data.precision == NOT_FOUND) ? 0 : data.precision * 10) + (int)c;
                    if (data.precision > 100000) goto error2;
                } else if (!data.star_w) {
                    data.width = ((data.width == NOT_FOUND) ? 0 : data.width * 10) + (int)c;
                    if (data.width > 100000) goto error2;
                } else goto error2;
                break;
            default:
                {
                    struct linepos_s epoint2;
                    str_t msg;
                error2:
                    epoint2 = v[0].epoint;
                    epoint2.pos = interstring_position(&epoint2, ((Str *)v[0].val)->data, (size_t)(data.pf - ((Str *)v[0].val)->data));
                    if ((c & 0x80) != 0) msg.len = utf8in(data.pf, &c); else msg.len = 1;
                    err_msg_not_defined(&msg, &epoint2);
                    err = star_args(&data);
                    if (err != NULL) goto error;
                    next_arg();
                    PUT_CHAR('%');
                    PUT_CHAR(c);
                    state = 0;
                    data.pf += msg.len - 1;
                }
            } /* end switch */
        }
    }
    if (listp != largs) {
        err_msg_argnum(args, listp + 1, listp + 1, epoint);
    } else if (none != 0) {
        err_msg_still_none(NULL, (largs >= none) ? &v[none].epoint : epoint);
    }
    str = new_str(0);
    str->len = return_value.len;
    str->chars = return_value.chars;
    if (return_value.len > sizeof str->val) {
        if (returnsize > return_value.len) {
            uint8_t *d = (uint8_t *)realloc(return_value.data, return_value.len);
            if (d != NULL) {
                str->data = d;
                return &str->v;
            }
        } 
        str->data = return_value.data;
        return &str->v;
    }
    memcpy(str->val, return_value.data, return_value.len);
    str->data = str->val;
    free(return_value.data);
    return &str->v;
error:
    free(return_value.data);
    return err;
}

