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

#ifndef _VICTIM_CHECKER_H
#define _VICTIM_CHECKER_H

#include <stdbool.h>

struct victim_checker_state
{
	void *map;
	size_t page_size;
};

int victim_checker_state_init(struct victim_checker_state *s,
			      const char *victim_file,
			      bool map_executable);

void victim_checker_state_cleanup(struct victim_checker_state *s);

int victim_checker_measure_one(struct victim_checker_state *s);

#endif
