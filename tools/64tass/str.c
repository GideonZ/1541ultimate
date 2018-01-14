/*
    $Id: str.c 1389 2017-03-27 01:13:30Z soci $

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

#include "str.h"
#include <string.h>
#include "unicode.h"
#include "error.h"
#include "arguments.h"

int str_hash(const str_t *s) {
    size_t l = s->len;
    const uint8_t *s2 = s->data;
    unsigned int h;
    if (l == 0) return 0;
    h = (unsigned int)*s2 << 7;
    while ((l--) != 0) h = (1000003 * h) ^ *s2++;
    h ^= s->len;
    return h & ((~0U) >> 1);
}

int str_cmp(const str_t *s1, const str_t *s2) {
    if (s1->len != s2->len) return s1->len > s2->len ? 1 : -1;
    if (s1->data == s2->data) return 0;
    return memcmp(s1->data, s2->data, s1->len);
}

void str_cfcpy(str_t *s1, const str_t *s2) {
    size_t i, l;
    const uint8_t *d;
    static str_t cache;
    if (s2 == NULL) {
        if (s1 != NULL) {
            if (s1->len != cache.len) {
                s1->data = (uint8_t *)reallocx((uint8_t *)s1->data, s1->len);
            }
        } else free((uint8_t *)cache.data);
        memset(&cache, 0, sizeof cache);
        return;
    }
    l = s2->len; d = s2->data;
    if (arguments.caseinsensitive == 0) {
        for (i = 0; i < l; i++) {
            if ((d[i] & 0x80) != 0) {
                unfkc(&cache, s2, 0);
                s1->len = cache.len;
                s1->data = cache.data;
                return;
            }
        }
        s1->len = l;
        s1->data = d;
        return;
    }
    for (i = 0; i < l; i++) {
        uint8_t *s, ch = d[i];
        if (ch < 'A' || (ch > 'Z' && ch < 0x80)) continue;
        if ((ch & 0x80) != 0) {
            unfkc(&cache, s2, 1);
            s1->len = cache.len;
            s1->data = cache.data;
            return;
        }
        if (l > cache.len) {
            cache.data = (uint8_t *)reallocx((uint8_t *)cache.data, l);
            cache.len = l;
        }
        s = (uint8_t *)cache.data;
        if (i != 0) memcpy(s, d, i);
        s1->data = s;
        for (; i < l; i++) {
            ch = d[i];
            if (ch < 'A') {
                s[i] = ch;
                continue;
            }
            if (ch <= 'Z') {
                s[i] = ch | 0x20;
                continue;
            }
            if ((ch & 0x80) != 0) {
                unfkc(&cache, s2, 1);
                s1->len = cache.len;
                s1->data = cache.data;
                return;
            }
            s[i] = ch;
        }
        s1->len = l;
        return;
    }
    s1->len = l;
    s1->data = d;
}

void str_cpy(str_t *s1, const str_t *s2) {
    s1->len = s2->len;
    if (s2->data != NULL) {
        uint8_t *s = (uint8_t *)mallocx(s2->len);
        memcpy(s, s2->data, s2->len);
        s1->data = s;
    } else s1->data = NULL;
}
