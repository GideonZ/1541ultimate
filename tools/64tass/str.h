/*
    $Id: str.h 1442 2017-04-02 22:43:30Z soci $

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
#ifndef STR_H
#define STR_H
#include "inttypes.h"

typedef struct str_t {
    size_t len;
    const uint8_t *data;
} str_t;

extern int str_hash(const str_t *);
extern int str_cmp(const str_t *, const str_t *);
extern void str_cfcpy(str_t *, const str_t *);
extern void str_cpy(str_t *, const str_t *);

#endif
