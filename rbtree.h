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
