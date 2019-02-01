#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include "heap.h"

int heap_init(struct heap *h, size_t node_size,
	      heap_node_compare comp_node,
	      void *comp_node_arg,
	      heap_node_move move_node,
	      void *move_node_arg,
	      unsigned int node_alloc_batch)
{
	memset(h, 0, sizeof(*h));

	h->comp_node = comp_node;
	h->comp_node_arg = comp_node_arg;
	h->move_node = move_node;
	h->move_node_arg = move_node_arg;
	h->node_size = node_size;

	h->chunk_size = node_size * node_alloc_batch;
	h->nodes_per_chunk = node_alloc_batch;

	h->swap_area = malloc(node_size);
	if (!h->swap_area)
		return -ENOMEM;

	return 0;
}

void heap_destroy(struct heap *h)
{
	size_t i;

	for (i = 0; i < h->n_chunks; ++i)
		free(h->chunks[i]);
	free(h->chunks);
	free(h->swap_area);
}

static inline size_t node_to_chunk(struct heap *h, size_t node)
{
	return node / h->nodes_per_chunk;
}

static inline size_t node_to_pos_in_chunk(struct heap *h, size_t node)
{
	return node % h->nodes_per_chunk;
}

static inline void* node_ptr(struct heap *h, size_t node)
{
	return ((char*)h->chunks[node_to_chunk(h, node)] +
		node_to_pos_in_chunk(h, node) * h->node_size);
}

static int add_chunk(struct heap *h)
{
	void **chunks;
	void *chunk;

	chunks = realloc(h->chunks, sizeof(*chunks) * (h->n_chunks + 1));
	if (!chunks)
		return -ENOMEM;
	h->chunks = chunks;

	chunk = malloc(h->chunk_size);
	if (!chunk)
		return -ENOMEM;

	h->chunks[h->n_chunks++] = chunk;
	return 0;
}

static int alloc_tail_node(struct heap *h)
{
	if (h->n_nodes == h->n_chunks * h->nodes_per_chunk) {
		int r;

		r = add_chunk(h);
		if (r)
			return r;
	}

	h->n_nodes++;
	return 0;
}

static void shrink_chunks(struct heap *h)
{
	size_t allocated_nodes, free_nodes;
	size_t chunks_to_free;
	size_t i;
	void **chunks;

	allocated_nodes = h->n_chunks * h->nodes_per_chunk;
	free_nodes = allocated_nodes - h->n_nodes;
	if (free_nodes < 2 * h->nodes_per_chunk)
		return;
	chunks_to_free = free_nodes / h->nodes_per_chunk;

	for (i = 0; i < chunks_to_free; ++i)
		free(h->chunks[h->n_chunks - i - 1]);
	h->n_chunks -= chunks_to_free;

	chunks = realloc(h->chunks, sizeof(*chunks) * h->n_chunks);
	if (!chunks)
		return;
	h->chunks = chunks;
}

static void free_tail_node(struct heap *h)
{
	h->n_nodes--;
	shrink_chunks(h);
}

static inline size_t node_parent(size_t node)
{
	return (node - 1) / 2;
}

static inline size_t node_first_child(size_t node)
{
	return 2 * node + 1;
}

static inline void node_swap(struct heap *h, void *p1, void *p2)
{
	h->move_node(h->swap_area, p1, h->move_node_arg);
	h->move_node(p1, p2, h->move_node_arg);
	h->move_node(p2, h->swap_area, h->move_node_arg);
}

static void node_trickle_up(struct heap *h, size_t node)
{
	void *p = node_ptr(h, node);

	while (node) {
		size_t parent = node_parent(node);
		void *p_parent = node_ptr(h, parent);
		if (h->comp_node(p_parent, p, h->comp_node_arg) <= 0)
			break;

		node_swap(h, p_parent, p);
		node = parent;
		p = p_parent;
	}
}

static void node_trickle_down(struct heap *h, size_t node)
{
	void *p = node_ptr(h, node);

	while (1) {
		bool first_child_is_less = false;
		bool second_child_is_less = false;

		size_t first_child, second_child, swap_child;
		void *p_first_child, *p_second_child, *p_swap_child;

		first_child = node_first_child(node);
		second_child = first_child + 1;

		if (first_child >= h->n_nodes)
			break;

		p_first_child = node_ptr(h, first_child);
		if (h->comp_node(p, p_first_child, h->comp_node_arg) > 0)
			first_child_is_less = true;

		if (second_child < h->n_nodes) {
			p_second_child = node_ptr(h, second_child);
			if (h->comp_node(p, p_second_child, h->comp_node_arg)
			    > 0) {
				second_child_is_less = true;
			}
		}

		if (!first_child_is_less && !second_child_is_less)
			break;

		if (first_child_is_less && second_child_is_less) {
			if (h->comp_node(p_first_child, p_second_child,
					 h->comp_node_arg) <= 0) {
				swap_child = first_child;
				p_swap_child = p_first_child;

			} else {
				swap_child = second_child;
				p_swap_child = p_second_child;
			}

		} else if (first_child_is_less) {
			swap_child = first_child;
			p_swap_child = p_first_child;

		} else {
			swap_child = second_child;
			p_swap_child = p_second_child;

		}

		node_swap(h, p, p_swap_child);
		node = swap_child;
		p = p_swap_child;
	}
}


int heap_insert_node(struct heap *h, void *p)
{
	int r;
	size_t tail_node;
	void *p_tail;

	r = alloc_tail_node(h);
	if (r)
		return r;

	tail_node = h->n_nodes - 1;
	p_tail = node_ptr(h, tail_node);
	h->move_node(p_tail, p, h->move_node_arg);
	node_trickle_up(h, tail_node);

	return 0;
}


void heap_pop_min_node(struct heap *h)
{
	if (h->n_nodes > 1) {
		h->move_node(node_ptr(h, 0), node_ptr(h, h->n_nodes - 1),
			     h->move_node_arg);
		free_tail_node(h);
		node_trickle_down(h, 0);
	} else {
		free_tail_node(h);
	}
}


void heap_replace_min_node(struct heap *h, void *p)
{
	h->move_node(node_ptr(h, 0), p, h->move_node_arg);
	node_trickle_down(h, 0);
}
