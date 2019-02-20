/*
 * Copyright (C) 2019 SUSE LLC
 *
 * This file is part of pgc.
 *
 * pgc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * pgc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pgc.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _RBTREE_H
#define _RBTREE_H

#include <stddef.h>

struct rb_node
{
	unsigned long __parent_and_color;
	struct rb_node *left;
	struct rb_node *right;
};

typedef int (*rb_compare) (struct rb_node *node, void *data);

static inline void
rb_lookup(struct rb_node ***root, rb_compare compare, void *data,
	  struct rb_node **parent)
{
	struct rb_node *p = NULL;
	while (**root != NULL) {
		int r = compare(**root, data);

		if (!r)
			break;

		p = **root;
		*root = r < 0 ? &(**root)->left : &(**root)->right;
	}

	if (parent)
		*parent = p;
}


void rb_insert(struct rb_node *node, struct rb_node *parent,
	       struct rb_node **where, struct rb_node **tree);

void rb_delete(struct rb_node *n, struct rb_node **tree);

void rb_relocate_node(struct rb_node *new, struct rb_node *old,
		      struct rb_node **tree);

#endif
