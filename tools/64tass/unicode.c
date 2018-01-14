/*
    $Id: unicode.c 1503 2017-04-30 11:44:39Z soci $

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
#include "unicode.h"
#include "wchar.h"
#include "wctype.h"
#include <ctype.h>
#include <string.h>
#include "error.h"

#define U_CASEFOLD 1
#define U_COMPAT 2

FAST_CALL unsigned int utf8in(const uint8_t *c, uchar_t *out) { /* only for internal use with validated utf-8! */
    unsigned int i, j;
    uchar_t ch = c[0];

    if (ch < 0xe0) {
        *out = (ch << 6) ^ c[1] ^ 0x3080;
        return 2;
    }
    if (ch < 0xf0) {
        ch ^= 0xe0;i = 3;
    } else if (ch < 0xf8) {
        ch ^= 0xf0;i = 4;
    } else if (ch < 0xfc) {
        ch ^= 0xf8;i = 5;
    } else {
        ch ^= 0xfc;i = 6;
    }

    for (j = 1;j < i; j++) {
        ch = (ch << 6) ^ c[j] ^ 0x80;
    }
    *out = ch;
    return i;
}

FAST_CALL unsigned int utf8rin(const uint8_t *c, uchar_t *out) { /* only for internal use with validated utf-8! */
    uchar_t ch;
    unsigned int i, j;

    if (c[-2] < 0xe0) {
        ch = c[-2] ^ 0xc0;i = 2;
    } else if (c[-3] < 0xf0) {
        ch = c[-3] ^ 0xe0;i = 3;
    } else if (c[-4] < 0xf8) {
        ch = c[-4] ^ 0xf0;i = 4;
    } else if (c[-5] < 0xfc) {
        ch = c[-5] ^ 0xf8;i = 5;
    } else {
        ch = c[-6] ^ 0xfc;i = 6;
    }

    c -= i;
    for (j = 1;j < i; j++) {
        ch = (ch << 6) ^ c[j] ^ 0x80;
    }
    *out = ch;
    return i;
}

FAST_CALL uint8_t *utf8out(uchar_t i, uint8_t *c) {
    if (i < 0x800) {
        *c++=0xc0 | (uint8_t)(i >> 6);
        *c++=0x80 | (i & 0x3f);
	return c;
    }
    if (i < 0x10000) {
        *c++=0xe0 | (uint8_t)(i >> 12);
        *c++=0x80 | ((i >> 6) & 0x3f);
        *c++=0x80 | (i & 0x3f);
	return c;
    }
    if (i < 0x200000) {
        *c++=0xf0 | (uint8_t)(i >> 18);
        *c++=0x80 | ((i >> 12) & 0x3f);
        *c++=0x80 | ((i >> 6) & 0x3f);
        *c++=0x80 | (i & 0x3f);
	return c;
    }
    if (i < 0x4000000) {
        *c++=0xf8 | (i >> 24);
        *c++=0x80 | ((i >> 18) & 0x3f);
        *c++=0x80 | ((i >> 12) & 0x3f);
        *c++=0x80 | ((i >> 6) & 0x3f);
        *c++=0x80 | (i & 0x3f);
	return c;
    }
    if ((i & ~(uchar_t)0x7fffffff) != 0) return c;
    *c++=0xfc | (i >> 30);
    *c++=0x80 | ((i >> 24) & 0x3f);
    *c++=0x80 | ((i >> 18) & 0x3f);
    *c++=0x80 | ((i >> 12) & 0x3f);
    *c++=0x80 | ((i >> 6) & 0x3f);
    *c++=0x80 | (i & 0x3f);
    return c;
}

static inline unsigned int utf8outlen(uchar_t i) {
    if (i < 0x800) return 2;
    if (i < 0x10000) return 3;
    if (i < 0x200000) return 4;
    if (i < 0x4000000) return 5;
    return 6;
}

static void extbuff(struct ubuff_s *d) {
    d->len += 16;
    if (/*d->len < 16 ||*/ d->len > SIZE_MAX / sizeof *d->data) err_msg_out_of_memory(); /* overflow */
    d->data = (uchar_t *)reallocx(d->data, d->len * sizeof *d->data);
}

static void udecompose(uchar_t ch, struct ubuff_s *d, int options) {
    const struct properties_s *prop;
    if (ch >= 0xac00 && ch <= 0xd7a3) {
        uchar_t ht, hs = ch - 0xac00;
        if (d->p >= d->len) extbuff(d);
        d->data[d->p++] = 0x1100 + hs / 588;
        if (d->p >= d->len) extbuff(d);
        d->data[d->p++] = 0x1161 + (hs % 588) / 28;
        ht = hs % 28;
        if (ht != 0) {
            if (d->p >= d->len) extbuff(d);
            d->data[d->p++] = 0x11a7 + ht;
        }
        return;
    }
    prop = uget_property(ch);
    if ((options & U_CASEFOLD) != 0 && prop->casefold != 0) {
        if (prop->casefold > 0) {
            if (d->p >= d->len) extbuff(d);
            d->data[d->p++] = (uint16_t)prop->casefold;
            return;
        }
        if (prop->casefold > -16384) {
            const int16_t *p = &usequences[-prop->casefold];
            for (;;) {
                if (d->p >= d->len) extbuff(d);
                d->data[d->p++] = (uint16_t)abs(*p);
                if (*p < 0) return;
                p++;
            }
        } else {
            const int32_t *p = &usequences2[-prop->casefold - 16384];
            for (;;) {
                if (d->p >= d->len) extbuff(d);
                d->data[d->p++] = (uint32_t)abs(*p);
                if (*p < 0) return;
                p++;
            }
        }
    }
    if (prop->decompose != 0) {
        if ((prop->property & pr_compat) == 0 || (options & U_COMPAT) != 0) {
            if (prop->decompose > 0) {
                udecompose((uint16_t)prop->decompose, d, options);
                return;
            } 
            if (prop->decompose > -16384) {
                const int16_t *p = &usequences[-prop->decompose];
                for (;;) {
                    udecompose((uint16_t)abs(*p), d, options);
                    if (*p < 0) return;
                    p++;
                }
            } else {
                const int32_t *p = &usequences2[-prop->decompose - 16384];
                for (;;) {
                    udecompose((uint32_t)abs(*p), d, options);
                    if (*p < 0) return;
                    p++;
                }
            }
        }
    }
    if (d->p >= d->len) extbuff(d);
    d->data[d->p++] = ch;
}

static void unormalize(struct ubuff_s *d) {
    size_t pos, max;
    if (d->p < 2) return;
    pos = 0;
    max = d->p - 1;
    while (pos < max) {
        uchar_t ch1, ch2;
        uint8_t cc1, cc2;
        ch2 = d->data[pos + 1];
        cc2 = uget_property(ch2)->combclass;
        if (cc2 != 0) {
            ch1 = d->data[pos];
            cc1 = uget_property(ch1)->combclass;
            if (cc1 > cc2) {
                d->data[pos] = ch2;
                d->data[pos + 1] = ch1;
                if (pos != 0) {
                    pos--;
                    continue;
                }
            }
        } 
        pos++;
    }
}

static void ucompose(const struct ubuff_s *buff, struct ubuff_s *d) {
    const struct properties_s *prop, *sprop = NULL;
    uchar_t ch;
    int mclass = -1;
    size_t i, sp = (size_t)-1;
    d->p = 0;
    for (i = 0; i < buff->p; i++) {
        ch = buff->data[i];
        prop = uget_property(ch);
        if (sp != (size_t)-1 && prop->combclass > mclass) {
            uchar_t sc = d->data[sp];
            if (sc >= 0xac00) {
                uchar_t hs = sc - 0xac00;
                if (hs < 588*19 && (hs % 28) == 0) {
                    if (ch >= 0x11a7 && ch < 0x11a7 + 28) {
                        d->data[sp] = sc + ch - 0x11a7;
                        sprop = NULL;
                        continue;
                    }
                }
            } else if (sc >= 0x1100 && sc < 0x1100 + 19 && ch >= 0x1161 && ch < 0x1161 + 21) {
                d->data[sp] = 0xac00 + (ch - 0x1161 + (sc - 0x1100) * 21) * 28;
                sprop = NULL;
                continue;
            }
            if (sprop == NULL) sprop = uget_property(sc);
            if (sprop->base >= 0 && prop->diar >= 0) {
                int16_t comp = ucomposing[sprop->base + prop->diar];
                if (comp != 0) {
                    d->data[sp] = (comp > 0) ? (uint16_t)comp : ucomposed[-comp];
                    sprop = NULL;
                    continue;
                }
            }
        }
        if (prop->combclass != 0) {
            if (prop->combclass > mclass) {
                mclass = prop->combclass;
            }
        } else {
            sp = d->p;
            sprop = prop;
            mclass = -1;
        }
        if (d->p >= d->len) extbuff(d);
        d->data[d->p++] = ch;
    }
}

void unfc(struct ubuff_s *b) {
    size_t i;
    static struct ubuff_s dbuf;
    if (b == NULL) {
        free(dbuf.data);
        return;
    }
    for (dbuf.p = i = 0; i < b->p; i++) {
        udecompose(b->data[i], &dbuf, 0);
    }
    unormalize(&dbuf);
    ucompose(&dbuf, b);
}

void unfkc(str_t *s1, const str_t *s2, int mode) {
    const uint8_t *d, *m;
    uint8_t *s, *dd;
    size_t i, l;
    static struct ubuff_s dbuf, dbuf2;
    if (s2 == NULL) {
        free(dbuf.data);
        free(dbuf2.data);
        return;
    }
    mode = ((mode != 0) ? U_CASEFOLD : 0) | U_COMPAT;
    d = s2->data;
    for (dbuf.p = i = 0; i < s2->len;) {
        uchar_t ch;
        ch = d[i];
        if ((ch & 0x80) != 0) {
            i += utf8in(d + i, &ch);
            udecompose(ch, &dbuf, mode);
            continue;
        }
        if ((mode & U_CASEFOLD) != 0 && ch >= 'A' && ch <= 'Z') ch |= 0x20;
        if (dbuf.p >= dbuf.len) extbuff(&dbuf);
        dbuf.data[dbuf.p++] = ch;
        i++;
    }
    unormalize(&dbuf);
    ucompose(&dbuf, &dbuf2);
    l = s2->len;
    if (l > s1->len) {
        s1->data = (uint8_t *)reallocx((uint8_t *)s1->data, l);
    }
    dd = s = (uint8_t *)s1->data;
    m = dd + l;
    for (i = 0; i < dbuf2.p; i++) {
        uchar_t ch;
        ch = dbuf2.data[i];
        if (ch != 0 && ch < 0x80) {
            if (s >= m) {
                size_t o = (size_t)(s - dd);
                l += 16;
                if (l < 16) err_msg_out_of_memory();
                dd = (uint8_t *)reallocx(dd, l);
                s = dd + o;
                m = dd + l;
            }
            *s++ = (uint8_t)ch;
            continue;
        }
        if (s + utf8outlen(ch) > m) {
            size_t o = (size_t)(s - dd);
            l += 16;
            if (l < 16) err_msg_out_of_memory();
            dd = (uint8_t *)reallocx(dd, l);
            s = dd + o;
            m = dd + l;
        }
        s = utf8out(ch, s);
    }
    s1->len = (size_t)(s - dd);
    s1->data = dd;
}

size_t argv_print(const char *line, FILE *f) {
    size_t len = 0;
#ifdef _WIN32
    size_t i = 0, back;
    bool quote = false, space = false;

    for (;;i++) {
        switch (line[i]) {
        case '%':
        case '"': quote = true; if (!space) continue; break;
        case ' ': space = true; if (!quote) continue; break;
        case 0: break;
        default: continue;
        }
        break;
    }

    if (space) {
        if (quote) {len++;putc('^', f);}
        len++;putc('"', f);
    }
    i = 0; back = 0;
    for (;;) {
        uchar_t ch = (uint8_t)line[i];
        if ((ch & 0x80) != 0) {
            unsigned int ln = utf8in((const uint8_t *)line + i, &ch);
            if (iswprint(ch) != 0) {
                int ln2;
                char tmp[64];
                memcpy(tmp, line + i, ln);
                tmp[ln] = 0;
                ln2 = fwprintf(f, L"%S", tmp);
                if (ln2 > 0) {
                    i += ln;
                    len += ln2;
                    continue;
                }
            }
            i += ln;
            len++;putc('?', f);
            continue;
        }
        if (ch == 0) break;

        if (ch == '\\') {
            back++;
            i++;
            len++;putc('\\', f);
            continue;
        }
        if (!space || quote) {
            if (strchr("()%!^<>&|\"", ch) != NULL) {
                if (ch == '"') {
                    while ((back--) != 0) {len++;putc('\\', f);}
                    len++;putc('\\', f);
                }
                len++;putc('^', f);
            }
        } else {
            if (ch == '%') {
                len++;putc('^', f);
            }
        }
        back = 0;

        i++;
        if (isprint(ch) == 0) {
            len++;putc('?', f);
            continue;
        }
        len++;putc(ch, f);
    }
    if (space) {
        while ((back--) != 0) {len++;putc('\\', f);}
        if (quote) {len++;putc('^', f);}
        len++;putc('"', f);
    }
#else
    size_t i;
    bool quote = false;

    for (i = 0;line[i] != 0;i++) {
        if (line[i] == '!') break;
        if (strchr(" \"$&()*;<>'?[\\]`{|}", line[i]) != NULL) quote = true;
    }
    if (line[i] != 0) quote = false;
    if (quote) {len++;putc('"', f);}
    else {
        switch (line[0]) {
        case '~':
        case '#': len++;putc('\\', f); break;
        }
    }
    i = 0;
    for (;;) {
        uchar_t ch = (uint8_t)line[i];
        if ((ch & 0x80) != 0) {
            int ln2;
            i += utf8in((const uint8_t *)line + i, &ch);
            if (iswprint(ch) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch, &ps);
                if (ln != (size_t)-1) {
                    len += fwrite(temp, ln, 1, f);
                    continue;
                }
            }
            ln2 = fprintf(f, ch < 0x10000 ? "$'\\u%" PRIx32 "'" : "$'\\U%" PRIx32 "'", ch);
            if (ln2 > 0) len += (size_t)ln2;
            continue;
        }
        if (ch == 0) break;

        if (quote) {
            if (strchr("$`\"\\", ch) != NULL) {len++;putc('\\', f);}
        } else {
            if (strchr(" !\"$&()*;<>'?[\\]`{|}", ch) != NULL) {
                len++;putc('\\', f);
            }
        }

        i++;
        if (isprint(ch) == 0) {
            int ln = fprintf(f, "$'\\x%" PRIx32 "'", ch);
            if (ln > 0) len += (size_t)ln;
            continue;
        }
        len++;putc(ch, f);
    }
    if (quote) {len++;putc('"', f);}
#endif
    return len;
}

static int unknown_print(FILE *f, uchar_t ch) {
    char temp[64];
    const char *format = (ch >= 256) ? "<U+%" PRIX32 ">" : "<%02" PRIX32 ">";
    if (f != NULL) {
        int ln;
        if (print_use_color) fputs("\33[7m", f);
        ln = fprintf(f, format, ch);
        if (print_use_color) fputs("\33[m", f);
        if (print_use_bold) fputs("\33[1m", f);
        return ln;
    }
    return sprintf(temp, format, ch);
}

void printable_print(const uint8_t *line, FILE *f) {
#ifdef _WIN32
    size_t i = 0, l = 0;
    for (;;) {
        uchar_t ch = line[i];
        if ((ch >= 0x20 && ch <= 0x7e) || ch == 0x09) {
            i++;
            continue;
        }
        if (l != i) fwrite(line + l, 1, i - l, f);
        if (ch == 0) break;
        if ((ch & 0x80) != 0) {
            unsigned int ln = utf8in(line + i, &ch);
            if (iswprint(ch) != 0) {
                char tmp[64];
                memcpy(tmp, line + i, ln);
                tmp[ln] = 0;
                if (fwprintf(f, L"%S", tmp) > 0) {
                    i += ln;
                    l = i;
                    continue;
                }
            }
            i += ln;
        } else i++;
        l = i;
        unknown_print(f, ch);
    }
#else
    size_t i = 0, l = 0;
    for (;;) {
        uchar_t ch = line[i];
        if ((ch >= 0x20 && ch <= 0x7e) || ch == 0x09) {
            i++;
            continue;
        }
        if (l != i) fwrite(line + l, 1, i - l, f);
        if (ch == 0) break;
        if ((ch & 0x80) != 0) {
            i += utf8in(line + i, &ch);
            if (iswprint(ch) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch, &ps);
                if (ln != (size_t)-1) {
                    fwrite(temp, ln, 1, f);
                    l = i;
                    continue;
                }
            }
        } else i++;
        unknown_print(f, ch);
        l = i;
    }
#endif
}

size_t printable_print2(const uint8_t *line, FILE *f, size_t max) {
#ifdef _WIN32
    size_t i, l = 0, len = 0;
    int err;
    for (i = 0; i < max;) {
        uchar_t ch = line[i];
        if ((ch & 0x80) != 0) {
            unsigned int ln;
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            ln = utf8in(line + i, &ch);
            if (iswprint(ch) != 0) {
                char tmp[64];
                memcpy(tmp, line + i, ln);
                tmp[ln] = 0;
                if (fwprintf(f, L"%S", tmp) >= 0) {
                    i += ln;
                    l = i;
                    len++;
                    continue;
                }
            }
            i += ln;
            l = i;
            err = unknown_print(f, ch);
            if (err > 0) len += (size_t)err;
            continue;
        }
        if ((ch < 0x20 && ch != 0x09) || ch > 0x7e) {
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            i++;
            l = i;
            err = unknown_print(f, ch);
            if (err > 0) len += (size_t)err;
            continue;
        }
        i++;
    }
    if (i != l) len += fwrite(line + l, 1, i - l, f);
    return len;
#else
    size_t i, l = 0, len = 0;
    int err;
    for (i = 0; i < max;) {
        uchar_t ch = line[i];
        if ((ch & 0x80) != 0) {
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            i += utf8in(line + i, &ch);
            l = i;
            if (iswprint(ch) != 0) {
                mbstate_t ps;
                char temp[64];
                size_t ln;
                memset(&ps, 0, sizeof ps);
                ln = wcrtomb(temp, (wchar_t)ch, &ps);
                if (ln != (size_t)-1) {
                    len += fwrite(temp, ln, 1, f); /* 1 character */
                    continue;
                }
            }
            err = unknown_print(f, ch);
            if (err > 0) len += (size_t)err;
            continue;
        }
        if ((ch < 0x20 && ch != 0x09) || ch > 0x7e) {
            if (l != i) len += fwrite(line + l, 1, i - l, f);
            i++;
            l = i;
            err = unknown_print(f, ch);
            if (err > 0) len += (size_t)err;
            continue;
        }
        i++;
    }
    if (i != l) len += fwrite(line + l, 1, i - l, f);
    return len;
#endif
}

void caret_print(const uint8_t *line, FILE *f, size_t max) {
    size_t i, l = 0;
    int err;
    for (i = 0; i < max;) {
        uchar_t ch = line[i];
        if ((ch & 0x80) != 0) {
#ifdef _WIN32
            unsigned int ln = utf8in(line + i, &ch);
            if (iswprint(ch) != 0) {
                char tmp[64];
                wchar_t tmp2[64];
                memcpy(tmp, line + i, ln);
                tmp[ln] = 0;
                if (swprintf(tmp2, lenof(tmp2), L"%S", tmp) > 0) {
                    int width = wcwidth(ch);
                    if (width > 0) l += (unsigned int)width;
                    i += ln;
                    continue;
                }
            }
            i += ln;
#else
            i += utf8in(line + i, &ch);
            if (iswprint(ch) != 0) {
                char temp[64];
                mbstate_t ps;
                memset(&ps, 0, sizeof ps);
                if (wcrtomb(temp, (wchar_t)ch, &ps) != (size_t)-1) {
                    int width = wcwidth(ch);
                    if (width > 0) l += (unsigned int)width;
                    continue;
                }
            }
#endif
            err = unknown_print(NULL, ch);
            if (err > 0) l += (size_t)err;
            continue;
        }
        if (ch == 0) break;
        if (ch == '\t') {
            while (l != 0) { 
                putc(' ', f); 
                l--; 
            }
            putc('\t', f);
            i++;
            continue;
        }
        if (ch < 0x20 || ch > 0x7e) {
            err = unknown_print(NULL, ch); 
            if (err > 0) l += (size_t)err;
        } else l++;
        i++;
    }
    while (l != 0) { 
        putc(' ', f); 
        l--; 
    }
}

size_t calcpos(const uint8_t *line, size_t pos) {
    size_t s, l;
    s = l = 0;
    while (s < pos) {
        if (line[s] == 0) return l;
        s += utf8len(line[s]);
        l++;
    }
    return l;
}
