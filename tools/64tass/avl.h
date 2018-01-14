/*
 * avl.h - this file is part of Libtree.
 * $Id: avl.h 1093 2016-05-12 10:50:04Z soci $
 *
 * Copyright (C) 2010 Franck Bui-Huu <fbuihuu@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */
#ifndef AVL_H
#define AVL_H

#include <stddef.h>

/*
 * The definition has been stolen from the Linux kernel.
 */
#ifdef __GNUC__
#  define avltree_container_of(node, type, member) ({			\
	struct avltree_node *__mptr = (node);			\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#  define cavltree_container_of(node, type, member) ({			\
	const struct avltree_node *__mptr = (node);			\
	(const type *)( (const char *)__mptr - offsetof(type,member) );})
#else
#  define avltree_container_of(node, type, member)			\
	((type *)((char *)(node) - offsetof(type, member)))
#  define cavltree_container_of(node, type, member)			\
	((const type *)((const char *)(node) - offsetof(type, member)))
#endif	/* __GNUC__ */

/*
 * AVL tree
 */

struct avltree_node {
	struct avltree_node *left, *right;
	struct avltree_node *parent;
	int balance;		/* balance factor [-2:+2] */
};

typedef int (*avltree_cmp_fn_t)(const struct avltree_node *, const struct avltree_node *);
typedef void (*avltree_free_fn_t)(struct avltree_node *);

struct avltree {
	struct avltree_node *root;
	int height;
	struct avltree_node *first, *last;
};

struct avltree_node *avltree_first(const struct avltree *);
struct avltree_node *avltree_next(const struct avltree_node *);
struct avltree_node *avltree_prev(const struct avltree_node *);

struct avltree_node *avltree_lookup(const struct avltree_node *, const struct avltree *, avltree_cmp_fn_t);
struct avltree_node *avltree_insert(struct avltree_node *, struct avltree *, avltree_cmp_fn_t);
void avltree_init(struct avltree *);
void avltree_destroy(struct avltree *, avltree_free_fn_t);

#endif
