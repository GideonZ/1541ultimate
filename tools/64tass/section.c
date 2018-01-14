/*
    $Id: section.c 1498 2017-04-29 17:49:46Z soci $

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
#include "section.h"
#include "unicode.h"
#include "error.h"
#include "64tass.h"
#include "values.h"
#include "intobj.h"
#include "longjump.h"
#include "optimizer.h"

struct section_s root_section;
struct section_s *current_section = &root_section;
static struct section_s *prev_section = &root_section;

static int section_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct section_s *a = cavltree_container_of(aa, struct section_s, node);
    const struct section_s *b = cavltree_container_of(bb, struct section_s, node);
    int h = a->name_hash - b->name_hash;
    if (h != 0) return h; 
    return str_cmp(&a->cfname, &b->cfname);
}

static void section_free(struct avltree_node *aa)
{
    struct section_s *a = avltree_container_of(aa, struct section_s, node);
    free((uint8_t *)a->name.data);
    if (a->name.data != a->cfname.data) free((uint8_t *)a->cfname.data);
    avltree_destroy(&a->members, section_free);
    longjump_destroy(&a->longjump);
    destroy_memblocks(&a->mem);
    val_destroy(a->l_address_val);
    cpu_opt_destroy(a->optimizer);
    free(a);
}

struct section_s *find_new_section(const str_t *name) {
    struct avltree_node *b;
    struct section_s *context = current_section;
    struct section_s tmp, *tmp2 = NULL;

    if (name->len > 1 && name->data[1] == 0) tmp.cfname = *name;
    else str_cfcpy(&tmp.cfname, name);
    tmp.name_hash = str_hash(&tmp.cfname);

    while (context != NULL) {
        b = avltree_lookup(&tmp.node, &context->members, section_compare);
        if (b != NULL) {
            tmp2 = avltree_container_of(b, struct section_s, node);
            if (tmp2->defpass >= pass - 1) {
                return tmp2;
            }
        }
        context = context->parent;
    }
    if (tmp2 != NULL) return tmp2;
    return new_section(name);
}

static struct section_s *lastsc = NULL;
struct section_s *new_section(const str_t *name) {
    struct avltree_node *b;
    struct section_s *tmp;

    if (lastsc == NULL) {
	lastsc = (struct section_s *)mallocx(sizeof *lastsc);
    }
    if (name->len > 1 && name->data[1] == 0) lastsc->cfname = *name;
    else str_cfcpy(&lastsc->cfname, name);
    lastsc->name_hash = str_hash(&lastsc->cfname);
    b = avltree_insert(&lastsc->node, &current_section->members, section_compare);
    if (b == NULL) { /* new section */
        str_cpy(&lastsc->name, name);
        if (lastsc->cfname.data == name->data) lastsc->cfname = lastsc->name;
        else str_cfcpy(&lastsc->cfname, NULL);
        lastsc->parent = current_section;
        lastsc->provides = ~(uval_t)0;lastsc->requires = lastsc->conflicts = 0;
        lastsc->end = lastsc->address = lastsc->l_address.address = lastsc->l_address.bank = lastsc->size = 0;
        lastsc->l_address_val = (Obj *)ref_int(int_value[0]);
        lastsc->dooutput = true;
        lastsc->defpass = 0;
        lastsc->usepass = 0;
        lastsc->unionmode = false;
        lastsc->structrecursion = 0;
        lastsc->logicalrecursion = 0;
        lastsc->moved = false;
        lastsc->wrapwarn = false;
        lastsc->next = NULL;
        lastsc->optimizer = NULL;
        prev_section->next = lastsc;
        prev_section = lastsc;
        init_memblocks(&lastsc->mem);
        avltree_init(&lastsc->members);
        avltree_init(&lastsc->longjump);
	tmp = lastsc;
	lastsc = NULL;
	return tmp;
    }
    return avltree_container_of(b, struct section_s, node);            /* already exists */
}

void reset_section(struct section_s *section) {
    section->provides = ~(uval_t)0; section->requires = section->conflicts = 0;
    section->end = section->start = section->restart = section->l_restart.address = section->l_restart.bank = section->address = section->l_address.address = section->l_address.bank = 0;
    val_destroy(section->l_address_val);
    section->l_address_val = (Obj *)ref_int(int_value[0]);
    section->dooutput = true;
    section->structrecursion = 0;
    section->logicalrecursion = 0;
    section->moved = false;
    section->wrapwarn = false;
    section->unionmode = false;
}

void init_section2(struct section_s *section) {
    section->parent = NULL;
    section->name.data = NULL;
    section->name.len = 0;
    section->cfname.data = NULL;
    section->cfname.len = 0;
    section->next = NULL;
    section->optimizer = NULL;
    init_memblocks(&section->mem);
    section->l_address_val = (Obj *)ref_int(int_value[0]);
    avltree_init(&section->members);
    avltree_init(&section->longjump);
}

void init_section(void) {
    init_section2(&root_section);
    prev_section = &root_section;
}

void destroy_section2(struct section_s *section) {
    avltree_destroy(&section->members, section_free);
    longjump_destroy(&section->longjump);
    destroy_memblocks(&section->mem);
    val_destroy(section->l_address_val);
    section->l_address_val = NULL;
    cpu_opt_destroy(section->optimizer);
    section->optimizer = NULL;
}

void destroy_section(void) {
    free(lastsc);
    destroy_section2(&root_section);
}

static void sectionprint2(const struct section_s *l) {
    if (l->name.data != NULL) {
        sectionprint2(l->parent);
        printable_print2(l->name.data, stdout, l->name.len);
        putchar('.');
    }
}

static void printrange(const struct section_s *l) {
    char temp[10], temp2[10], temp3[10];
    sprintf(temp, "$%04" PRIaddress, l->start);
    temp2[0] = 0;
    if (l->size != 0) {
        sprintf(temp2, "-$%04" PRIaddress, (address_t)(l->start + l->size - 1));
    } 
    sprintf(temp3, "$%04" PRIaddress, l->size);
    printf("Section: %15s%-8s %-7s", temp, temp2, temp3);
}

void sectionprint(void) {
    struct section_s *l = &root_section;

    if (l->size != 0) {
        printrange(l);
        putchar('\n');
    }
    memprint(&l->mem);
    l = root_section.next;
    while (l != NULL) {
        if (l->defpass == pass) {
            printrange(l);
            putchar(' ');
            sectionprint2(l->parent);
            printable_print2(l->name.data, stdout, l->name.len);
            putchar('\n');
            memprint(&l->mem);
        }
        l = l->next;
    }
}
