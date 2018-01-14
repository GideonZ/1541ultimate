/*
    $Id: mem.h 1407 2017-03-30 15:10:21Z soci $

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
#ifndef MEM_H
#define MEM_H
#include "attributes.h"
#include "stdbool.h"
#include "inttypes.h"

struct memblock_s;
struct output_s;

struct memblocks_s {
    struct {       /* Linear memory dump */
        size_t p, len;
        uint8_t *data;
    } mem;
    bool compressed;
    size_t p, len;
    size_t lastp;
    address_t lastaddr;
    struct memblock_s *data;
};

extern void mark_mem(const struct memblocks_s *, address_t, address_t);
extern void write_mark_mem(struct memblocks_s *, unsigned int);
extern void list_mem(const struct memblocks_s *, bool);
extern void memjmp(struct memblocks_s *, address_t);
extern void memref(struct memblocks_s *, struct memblocks_s *);
extern void memprint(struct memblocks_s *);
extern void output_mem(struct memblocks_s *, const struct output_s *);
extern FAST_CALL void write_mem(struct memblocks_s *, unsigned int);
extern int read_mem(const struct memblocks_s *, size_t, size_t, size_t);
extern void get_mem(const struct memblocks_s *, size_t *, size_t *);
extern void restart_memblocks(struct memblocks_s *, address_t);
extern void init_memblocks(struct memblocks_s *);
extern void destroy_memblocks(struct memblocks_s *);
#endif
