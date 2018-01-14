/*
    $Id: section.h 1498 2017-04-29 17:49:46Z soci $

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
#ifndef SECTION_H
#define SECTION_H
#include "avl.h"
#include "stdbool.h"
#include "str.h"
#include "mem.h"

struct Obj;
struct optimizer_s;

struct section_s {
    int name_hash;
    str_t name;
    str_t cfname;
    struct avltree_node node;

    uval_t requires;
    uval_t conflicts;
    uval_t provides;
    address_t restart;
    address2_t l_restart;
    address_t address;
    address2_t l_address;
    struct Obj *l_address_val;
    address_t start;
    address_t end;
    address_t unionstart;
    address_t unionend;
    address2_t l_unionstart;
    address2_t l_unionend;
    address_t size;
    struct memblocks_s mem;
    uint8_t usepass;
    uint8_t defpass;
    uint8_t structrecursion;
    uint8_t logicalrecursion;
    bool dooutput;
    bool declared;
    bool unionmode;
    bool moved;
    bool wrapwarn;
    struct section_s *parent;
    struct section_s *next;
    struct file_list_s *file_list;
    struct optimizer_s *optimizer;
    struct linepos_s epoint;
    struct avltree members;
    struct avltree longjump;
};

extern struct section_s *new_section(const str_t *);
extern struct section_s *find_new_section(const str_t *);
extern void init_section(void);
extern void init_section2(struct section_s *);
extern void destroy_section(void);
extern void destroy_section2(struct section_s *);
extern void reset_section(struct section_s *);
extern void sectionprint(void);
extern struct section_s *current_section, root_section;
#endif
