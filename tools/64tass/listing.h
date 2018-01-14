/*
    $Id: listing.h 1407 2017-03-30 15:10:21Z soci $

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
#ifndef LISTING_H
#define LISTING_H
#include "attributes.h"
#include "inttypes.h"
#include "stdbool.h"
struct cpu_s;
struct Obj;
struct Listing;

extern bool listing_pccolumn;
extern unsigned int nolisting;
extern const uint8_t *llist;
extern MUST_CHECK struct Listing *listing_open(const char *, int, char *[]);
extern void listing_close(struct Listing *);
extern FAST_CALL void listing_equal(struct Listing *, struct Obj *);
extern FAST_CALL void listing_line(struct Listing *, linecpos_t);
extern FAST_CALL void listing_line_cut(struct Listing *, linecpos_t);
extern FAST_CALL void listing_line_cut2(struct Listing *, linecpos_t);
extern FAST_CALL void listing_set_cpumode(struct Listing *, const struct cpu_s *);
extern void listing_instr(struct Listing *, uint8_t, uint32_t, int);
extern void listing_mem(struct Listing *, const uint8_t *, size_t, address_t, address_t);
extern void listing_file(struct Listing *, const char *, const char *);
#endif
