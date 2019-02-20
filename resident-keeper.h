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

#ifndef _RESIDENT_KEEPER_H
#define _RESIDENT_KEEPER_H

#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h>
#include "heap.h"
#include "rbtree.h"

struct resident_mapping_id
{
	dev_t dev;
	ino_t ino;
};

struct resident_range
{
	size_t offset;
	size_t n_pages;
};

#define RESIDENT_RANGE_INLINE_COUNT	1

struct resident_mapping
{
	void *map;
	size_t size;

	struct resident_mapping_id id;
	struct rb_node id_tree_node;

	size_t n_pages;

	size_t n_resident_ranges;
	struct resident_range __inlined_ranges[RESIDENT_RANGE_INLINE_COUNT];
	struct resident_range *__extern_ranges;

	bool mapped_executable;
	bool dead;
};

struct resident_keeper_state
{
	struct heap mappings;
	struct resident_mapping fillup_mapping;

	size_t i_page;
	size_t active_n_pages;
	size_t n_pages_found_resident;

	size_t page_size;

	struct rb_node *id_tree_root;
	size_t target_n_pages;
	size_t n_pages;
	size_t n_pages_executable;

	bool map_executable;
	bool refresh_only_resident;
	bool launch_rewarmer;
	bool rt_sched_refresher;

	unsigned char *mincore_buf;
	size_t mincore_buf_size;

	pthread_t refresher;

	void **rewarm_ring;
	size_t rewarm_ring_size;
	pthread_spinlock_t rewarm_ring_lock;
	size_t rewarm_ring_used;
	size_t rewarm_ring_pos;
	pthread_mutex_t rewarmer_wake_mtx;
	pthread_cond_t rewarmer_wake_cond;
	bool quit_rewarmer;
	pthread_t rewarmer;
};


int resident_keeper_state_init(struct resident_keeper_state *s,
			       size_t target_n_pages, bool map_executable,
			       bool refresh_only_resident,
			       bool launch_rewarmer,
			       bool rt_sched_refresher);
void resident_keeper_state_cleanup(struct resident_keeper_state *s);
int resident_keeper_scan_directory(struct resident_keeper_state *s,
				   const char *path);
int resident_keeper_set_fillup_file(struct resident_keeper_state *s,
				    const char *name);
int resident_keeper_start(struct resident_keeper_state *s);
void resident_keeper_stop(struct resident_keeper_state *s);

#endif
