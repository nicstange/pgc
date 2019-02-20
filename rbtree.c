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

#include <assert.h>
#include "rbtree.h"

enum color
{
	BLACK = 0x0,
	RED   = 0x1
};

#define PARENT_MASK ~1UL
#define COLOR_MASK  1UL

static inline void set_color(struct rb_node *node, enum color color)
{
	if(!node) {
		assert(color == BLACK);
		return;
	}

	node->__parent_and_color = ((node->__parent_and_color & PARENT_MASK) |
				     color);
}

static inline enum color get_color(struct rb_node *node)
{
	if (!node)
		return BLACK;

	return (enum color)(node->__parent_and_color & COLOR_MASK);
}

static inline struct rb_node* get_parent(struct rb_node *node)
{
	return (struct rb_node*)(node->__parent_and_color & PARENT_MASK);
}

static inline void set_parent_and_color(struct rb_node *node,
					struct rb_node *parent,
					enum color color)
{
	node->__parent_and_color = ((unsigned long)parent | color);
}

static struct rb_node* get_uncle(struct rb_node *parent,
				 struct rb_node *grandparent)
{
	assert(grandparent != 0);
	assert(parent != 0);
	assert(parent == grandparent->left || parent == grandparent->right);

	return ((parent == grandparent->left) ?
		grandparent->right : grandparent->left);
}

static void rotate_left(struct rb_node *node)
{
	struct rb_node *right = node->right;
	assert(right != NULL);

	if (right->left) {
		set_parent_and_color(right->left, node, get_color(right->left));
	}
	node->right = right->left;

	right->left = node;
	set_parent_and_color(node, right, RED);
}

static void rotate_right(struct rb_node *node)
{
	struct rb_node *left = node->left;
	assert(left != NULL);

	if (left->right) {
		set_parent_and_color(left->right, node, get_color(left->right));
	}
	node->left = left->right;


	left->right = node;
	set_parent_and_color(node, left, RED);
}

void rb_insert(struct rb_node *node, struct rb_node *parent,
	       struct rb_node **where, struct rb_node **tree)
{
	struct rb_node *g, *u, *gg;

	assert(!parent || where == &parent->left || where == &parent->right);

	node->left = NULL;
	node->right = NULL;
	set_parent_and_color(node, parent, RED);

	*where = node;

	while (1) {
		if (!parent) {
			set_color(node, BLACK);
			return;
		}

		if (get_color(parent) == BLACK)
			return;

		g = get_parent(parent);
		assert(g);
		u = get_uncle(parent, g);

		if (u && get_color(u) == RED) {
			set_color(parent, BLACK);
			set_color(u, BLACK);
			set_color(g, RED);

			node = g;
			parent = get_parent(node);
			continue;
		}


		if (node == parent->right && parent == g->left) {
			rotate_left(parent);
			assert(get_color(node) == RED);
			set_parent_and_color(node, g, RED);
			g->left = node;

			node = parent;
			parent = g->left;
		} else if (node == parent->left && parent == g->right) {
			rotate_right(parent);
			assert(get_color(node) == RED);
			set_parent_and_color(node, g, RED);
			g->right = node;

			node = parent;
			parent = g->right;
		}


		gg = get_parent(g);
		if (node == parent->left && parent == g->left) {
			rotate_right(g);
		} else {
			assert(node == parent->right && parent == g->right);
			rotate_left(g);
		}

		set_parent_and_color(parent, gg, BLACK);
		if (gg) {
			if (gg->left == g) {
				gg->left = parent;
			} else {
				assert(gg->right == g);
				gg->right = parent;
			}
		} else {
			assert(*tree == g);
			*tree = parent;
		}

		return;
	}
}

static void replace_node(struct rb_node *where, struct rb_node *replacement,
			 struct rb_node **tree)
{
	struct rb_node *p;

	assert(where != NULL);

	if (replacement) {
		p = get_parent(replacement);
		if (p) {
			if (p->left == replacement) {
				p->left = NULL;
			} else {
				assert(p->right == replacement);
				p->right = NULL;
			}
		}

		replacement->__parent_and_color = where->__parent_and_color;
	}

	p = get_parent(where);
	if (p) {
		if (p->left == where) {
			p->left = replacement;
		} else {
			assert(p->right == where);
			p->right = replacement;
		}
	} else {
		assert(*tree == where);
		*tree = replacement;
	}
	set_parent_and_color(where, NULL, get_color(where));
}

static struct rb_node* sibling(struct rb_node *n, struct rb_node *p)
{
	assert(p != NULL);
	assert(p->left == n || p->right == n);
	return (p->left == n) ? p->right : p->left;
}

static void __delete_case1(struct rb_node *n, struct rb_node **tree);
static void __delete_case2(struct rb_node *n, struct rb_node **tree);
static void __delete_case3(struct rb_node *n, struct rb_node **tree);
static void __delete_case4(struct rb_node *n, struct rb_node **tree);
static void __delete_case5(struct rb_node *n, struct rb_node **tree);
static void __delete_case6(struct rb_node *n, struct rb_node **tree);

void __delete_case1(struct rb_node *n, struct rb_node **tree)
{
	assert(n);
	assert(get_color(n) == BLACK);

	if (get_parent(n))
		__delete_case2(n, tree);
}

void __delete_case2(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *p = get_parent(n);
	struct rb_node *s = sibling(n, p);
	struct rb_node *g = get_parent(p);

	if (get_color(s) == RED) {
		if (p->left == n) {
			rotate_left(p);
		} else {
			assert(p->right == n);
			rotate_right(p);
		}

		assert(get_color(p) == RED);
		set_parent_and_color(s, g, BLACK);

		if (g) {
			if (g->left == p) {
				g->left = s;
			} else {
				assert(g->right == p);
				g->right = s;
			}
		} else {
			assert(*tree == p);
			*tree = s;
		}
	}

	__delete_case3(n, tree);
}

void __delete_case3(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *p = get_parent(n);
	struct rb_node *s = sibling(n, p);

	assert(get_color(n) == BLACK);
	assert(get_color(s) == BLACK);

	if (get_color(p) == BLACK &&
		get_color(s->left) == BLACK &&
		get_color(s->right) == BLACK) {
		set_color(s, RED);
		__delete_case1(p, tree);
	} else {
		__delete_case4(n, tree);
	}
}

void __delete_case4(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *p = get_parent(n);
	struct rb_node *s = sibling(n, p);

	assert(get_color(n) == BLACK);
	assert(get_color(s) == BLACK);

	assert(get_color(p) == RED ||
	       get_color(s->left) == RED ||
	       get_color(s->right) == RED);

	if (get_color(p) == RED &&
	    get_color(s->left) == BLACK &&
	    get_color(s->right) == BLACK) {
		set_color(s, RED);
		set_color(p, BLACK);
	} else {
		assert(get_color(s->left) == RED ||
			get_color(s->right) == RED);
		__delete_case5(n, tree);
	}
}

void __delete_case5(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *p = get_parent(n);
	struct rb_node *s = sibling(n, p);
	struct rb_node *sl = s->left;
	struct rb_node *sr = s->right;

	assert(get_color(n) == BLACK);
	assert(get_color(s) == BLACK);

	assert(get_color(sl) == RED ||
	       get_color(sr) == RED);

	if (n == p->left &&
	    get_color(sr) == BLACK) {
		assert(get_color(sl) == RED);
		rotate_right(s);
		assert(get_color(s) == RED);
		set_parent_and_color(sl, p, BLACK);
		p->right = sl;
	} else if (n == p->right &&
		   get_color(sl) == BLACK) {
		assert(get_color(sr) == RED);
		rotate_left(s);
		assert(get_color(s) == RED);
		set_parent_and_color(sr, p, BLACK);
		p->left = sr;
	}

	__delete_case6(n, tree);
}

void __delete_case6(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *p = get_parent(n);
	enum color p_color = get_color(p);
	struct rb_node *s = sibling(n, p);
	struct rb_node *g = get_parent(p);

	assert(get_color(n) == BLACK);
	assert(get_color(s) == BLACK);

	if (n == p->left) {
		assert(get_color(s->right) == RED);

		rotate_left(p);
		set_color(s->right, BLACK);
	} else {
		assert(n == p->right);
		assert(get_color(s->left) == RED);

		rotate_right(p);
		set_color(s->left, BLACK);
	}

	set_color(p, BLACK);
	set_parent_and_color(s, g, p_color);
	if (g) {
		if (g->left == p) {
			g->left = s;
		} else {
			assert(g->right == p);
			g->right = s;
		}
	} else {
		assert (*tree == p);
		*tree = s;
	}
}

static void __delete(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *child = n->left ? n->left : n->right;
	enum color child_color = get_color(child);

	if (get_color(n) == BLACK) {
		if (child_color == RED) {
			replace_node(n, child, tree);
			assert(get_color(child) == BLACK);
		} else {
			assert(child == NULL);
			__delete_case1(n, tree);
			replace_node(n, NULL, tree);
		}
	} else {
		replace_node(n, child, tree);
		set_color(child, child_color);
	}
}

void rb_delete(struct rb_node *n, struct rb_node **tree)
{
	struct rb_node *e, *p;
	struct rb_node tmp = {0};

	if (n->left && n->right) {
		/* Find symmetric successor of n */
		e = n->right;
		while (e->left)
			e = e->left;

		/*
		 * Move the _value_ of e to RB tree node n, install a
		 * dummy at e's former position.
		 */
		tmp = *e;
		if (tmp.right) {
			set_parent_and_color(tmp.right, &tmp,
					     get_color(tmp.right));
		}
		assert(!tmp.left);
		p = get_parent(&tmp);
		assert(p);
		if (p->left == e) {
			p->left = &tmp;
		} else {
			assert(p->right == e);
			p->right = &tmp;
		}

		*e = *n;
		if (e->left)
			set_parent_and_color(e->left, e, get_color(e->left));
		if (e->right)
			set_parent_and_color(e->right, e, get_color(e->right));
		p = get_parent(e);
		if (p) {
			if (p->left == n) {
				p->left = e;
			} else {
				assert(p->right == n);
				p->right = e;
			}
		} else {
			*tree = e;
		}

		n = &tmp;
	}

	assert(n->left == NULL || n->right == NULL);
	__delete(n, tree);
}

void rb_relocate_node(struct rb_node *new, struct rb_node *old,
		      struct rb_node **tree)

{
	struct rb_node *p;

	if (old->left) {
		new->left = old->left;
		set_parent_and_color(new->left, new, get_color(new->left));
	} else {
		new->left = NULL;
	}

	if (old->right) {
		new->right = old->right;
		set_parent_and_color(new->right, new, get_color(new->right));
	} else {
		new->right = NULL;
	}

	new->__parent_and_color = old->__parent_and_color;
	p = get_parent(old);
	if (p) {
		if (p->left == old) {
			p->left = new;
		} else {
			assert(p->right == old);
			p->right = new;
		}
	} else {
		assert(*tree == old);
		*tree = new;
	}
}
