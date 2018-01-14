/*
    $Id: longjump.c 1389 2017-03-27 01:13:30Z soci $

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
#include "longjump.h"
#include "section.h"
#include "error.h"

static int longjump_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct longjump_s *a = cavltree_container_of(aa, struct longjump_s, node);
    const struct longjump_s *b = cavltree_container_of(bb, struct longjump_s, node);
    if (a->address != b->address) return a->address > b->address ? 1 : -1;
    return 0;
}

static void longjump_free(struct avltree_node *aa)
{
    struct longjump_s *a = avltree_container_of(aa, struct longjump_s, node);
    free(a);
}

static struct longjump_s *lastlj = NULL;
struct longjump_s *new_longjump(address_t address, bool *exists) {
    struct avltree_node *b;
    struct longjump_s *tmp;

    if (lastlj == NULL) {
        lastlj = (struct longjump_s *)mallocx(sizeof *lastlj);
    }
    lastlj->address = address;
    b = avltree_insert(&lastlj->node, &current_section->longjump, longjump_compare);
    if (b == NULL) { /* new longjump */
        lastlj->defpass = 0;
        *exists = false;
        tmp = lastlj;
        lastlj = NULL;
        return tmp;
    }
    *exists = true;
    return avltree_container_of(b, struct longjump_s, node);            /* already exists */
}

void destroy_longjump(void) {
    free(lastlj);
}

void longjump_destroy(struct avltree *t) {
    avltree_destroy(t, longjump_free);
}
