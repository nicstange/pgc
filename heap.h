#ifndef _HEAP_H
#define _HEAP_H

struct heap;

typedef int (*heap_node_compare)(void *node1, void *node2, void *arg);
typedef void (*heap_node_move)(void *to, void *from, void *arg);

struct heap
{
	size_t n_nodes;
	size_t n_chunks;
	void **chunks;
	heap_node_compare comp_node;
	void *comp_node_arg;
	heap_node_move move_node;
	void *move_node_arg;
	size_t node_size;
	size_t nodes_per_chunk;
	size_t chunk_size;
	void *swap_area;
};

int heap_init(struct heap *h, size_t node_size,
	      heap_node_compare comp,
	      void *comp_node_arg,
	      heap_node_move move_node,
	      void *move_node_arg,
	      unsigned int node_alloc_batch);

void heap_destroy(struct heap *h);

int heap_insert_node(struct heap *h, void *p);

static inline int heap_empty(struct heap *h)
{
	return !h->n_nodes;
}

static inline void* heap_min_node(struct heap *h)
{
	if (heap_empty(h))
		return NULL;

	return h->chunks[0];
}

void heap_pop_min_node(struct heap *h);

void heap_replace_min_node(struct heap *h, void *p);

static inline int heap_for_each(struct heap *h,
				int (*f)(void *p, void *arg), void *arg)
{
	size_t node, node_in_chunk;
	void *p;
	void **chunk;
	int r;

	node = 0;
	node_in_chunk = 0;
	chunk = h->chunks;
	while (node < h->n_nodes) {
		if (node_in_chunk) {
			p = (char *)p + h->node_size;
		} else {
			p = *chunk;
			++chunk;
		}

		r = f(p, arg);
		if (r <= 0)
			return r;

		++node;
		if (++node_in_chunk == h->nodes_per_chunk)
			node_in_chunk = 0;
	}
	return 0;
}

#endif
