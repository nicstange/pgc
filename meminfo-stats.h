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

#ifndef _MEMINFO_STATS_H
#define _MEMINFO_STATS_H

#include <pthread.h>

int meminfo_read_stats(unsigned long *total,
		       unsigned long *free,
		       unsigned long *pc_a_active,
		       unsigned long *pc_a_inactive,
		       unsigned long *pc_f_active,
		       unsigned long *pc_f_inactive);

struct meminfo_reporter_state
{
	size_t page_size;
	unsigned long interval_us;
	pthread_t reporter;
};


int meminfo_reporter_state_init(struct meminfo_reporter_state *s,
				unsigned long interval_ms);
void meminfo_reporter_state_cleanup(struct meminfo_reporter_state *s);
int meminfo_reporter_start(struct meminfo_reporter_state *s);
void meminfo_reporter_stop(struct meminfo_reporter_state *s);


#endif
