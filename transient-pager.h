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

#ifndef _TRANSIENT_PAGER_H
#define _TRANSIENT_PAGER_H

#include <pthread.h>
#include <stdbool.h>

struct transient_pager_state
{
	unsigned long target_period_usec;
	void *map;
	size_t i_page;
	size_t n_pages;
	size_t size;
	size_t page_size;
	pthread_t pager;
};

int transient_pager_state_init(struct transient_pager_state *s,
			       const char *pool_file,
			       unsigned long pagein_period_usec,
			       bool map_exec);

void transient_pager_state_cleanup(struct transient_pager_state *s);

int transient_pager_start(struct transient_pager_state *s);

void transient_pager_stop(struct transient_pager_state *s);

#endif
