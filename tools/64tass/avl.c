/*
 * avltree - Implements an AVL tree with parent pointers.
 * $Id: avl.c 1407 2017-03-30 15:10:21Z soci $
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

#include "avl.h"
#include "attributes.h"
#include "stdbool.h"

static avltree_cmp_fn_t cmp_fn;
/*
 * Iterators
 */
static inline struct avltree_node *get_first(struct avltree_node *node)
{
	while (node->left != NULL)
		node = node->left;
	return node;
}

static inline struct avltree_node *get_last(struct avltree_node *node)
{
	while (node->right != NULL)
		node = node->right;
	return node;
}

struct avltree_node *avltree_first(const struct avltree *tree)
{
	return tree->first;
}

struct avltree_node *avltree_next(const struct avltree_node *node)
{
	struct avltree_node *parent;

	if (node->right != NULL)
		return get_first(node->right);

	while ((parent = node->parent) != NULL && parent->right == node)
		node = parent;
	return parent;
}

struct avltree_node *avltree_prev(const struct avltree_node *node)
{
	struct avltree_node *parent;

	if (node->left != NULL)
		return get_last(node->left);

	while ((parent = node->parent) != NULL && parent->left == node)
		node = parent;
	return parent;
}

/*
 * The AVL tree is more rigidly balanced than Red-Black trees, leading
 * to slower insertion and removal but faster retrieval.
 */

/* node->balance = height(node->right) - height(node->left); */
static void rotate_left(struct avltree_node *node, struct avltree *tree)
{
	struct avltree_node *p = node;
	struct avltree_node *q = node->right; /* can't be NULL */
	struct avltree_node *parent = p->parent;

	if (parent != NULL) {
		if (parent->left == p)
			parent->left = q;
		else
			parent->right = q;
	} else
		tree->root = q;
	q->parent = parent;
	p->parent = q;

	p->right = q->left;
	if (p->right != NULL)
		p->right->parent = p;
	q->left = p;
}

static void rotate_right(struct avltree_node *node, struct avltree *tree)
{
	struct avltree_node *p = node;
	struct avltree_node *q = node->left; /* can't be NULL */
	struct avltree_node *parent = p->parent;

	if (parent != NULL) {
		if (parent->left == p)
			parent->left = q;
		else
			parent->right = q;
	} else
		tree->root = q;
	q->parent = parent;
	p->parent = q;

	p->left = q->right;
	if (p->left != NULL)
		p->left->parent = p;
	q->right = p;
}

/*
 * 'pparent', 'unbalanced' and 'is_left' are only used for
 * insertions. Normally GCC will notice this and get rid of them for
 * lookups.
 */
static inline struct avltree_node *do_lookup(const struct avltree_node *key,
					     const struct avltree *tree,
					     struct avltree_node **pparent,
					     struct avltree_node **unbalanced,
					     bool *is_left)
{
	struct avltree_node *node = tree->root;
	int res = 0;

	*pparent = NULL;
	*unbalanced = node;
	*is_left = false;

	while (node != NULL) {
		if (node->balance != 0)
			*unbalanced = node;

		res = cmp_fn(node, key);
		if (res == 0)
			return node;
		*pparent = node;
		if ((*is_left = (res > 0)))
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

struct avltree_node *avltree_lookup(const struct avltree_node *key,
				    const struct avltree *tree, avltree_cmp_fn_t cmp)
{
	struct avltree_node *parent, *unbalanced;
	bool is_left;

        cmp_fn = cmp;
	return do_lookup(key, tree, &parent, &unbalanced, &is_left);
}

static inline void set_child(struct avltree_node *child,
		      struct avltree_node *node, bool left)
{
	if (left)
		node->left = child;
	else
		node->right = child;
}

/* Insertion never needs more than 2 rotations */
struct avltree_node *avltree_insert(struct avltree_node *node, struct avltree *tree, avltree_cmp_fn_t cmp)
{
	struct avltree_node *key, *parent, *unbalanced;
	bool is_left;

        cmp_fn = cmp;

	key = do_lookup(node, tree, &parent, &unbalanced, &is_left);
	if (key != NULL)
		return key;

	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
	node->balance = 0;

	if (parent == NULL) {
		tree->root = node;
		tree->first = tree->last = node;
		tree->height++;
		return NULL;
	}
	if (is_left) {
		if (parent == tree->first)
			tree->first = node;
	} else {
		if (parent == tree->last)
			tree->last = node;
	}
	node->parent = parent;
	set_child(node, parent, is_left);

	for (;;) {
		if (parent->left == node)
			parent->balance--;
		else
			parent->balance++;

		if (parent == unbalanced)
			break;
		node = parent;
		parent = parent->parent;
	}

	switch (unbalanced->balance) {
	case  1: case -1:
		tree->height++;
		break;
	case 0:
		break;
	case 2: {
		struct avltree_node *right = unbalanced->right;

		if (right->balance == 1) {
			unbalanced->balance = 0;
			right->balance = 0;
		} else {
			switch (right->left->balance) {
			case 1:
				unbalanced->balance = -1;
				right->balance = 0;
				break;
			case 0:
				unbalanced->balance = 0;
				right->balance = 0;
				break;
			case -1:
				unbalanced->balance = 0;
				right->balance = 1;
				break;
			}
			right->left->balance = 0;

			rotate_right(right, tree);
		}
		rotate_left(unbalanced, tree);
		break;
	}
	case -2: {
		struct avltree_node *left = unbalanced->left;

		if (left->balance == -1) {
			unbalanced->balance = 0;
			left->balance = 0;
		} else {
			switch (left->right->balance) {
			case 1:
				unbalanced->balance =  0;
				left->balance = -1;
				break;
			case 0:
				unbalanced->balance = 0;
				left->balance = 0;
				break;
			case -1:
				unbalanced->balance = 1;
				left->balance = 0;
				break;
			}
			left->right->balance = 0;

			rotate_left(left, tree);
		}
		rotate_right(unbalanced, tree);
		break;
	}
	}
	return NULL;
}

void avltree_init(struct avltree *tree)
{
	tree->root = NULL;
	tree->height = -1;
	tree->first = NULL;
	tree->last = NULL;
}

static void destroy(struct avltree *tree, struct avltree_node *node, avltree_free_fn_t free_fn)
{
        struct avltree_node *tmp;
	while (node != NULL) {
		destroy(tree, node->left, free_fn);
		tmp = node;
		node = node->right;
		free_fn(tmp);
	}
}

void avltree_destroy(struct avltree *tree, avltree_free_fn_t free_fn)
{
	destroy(tree, tree->root, free_fn);
}
