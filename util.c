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

#include "util.h"

unsigned long ts_diff_ms(struct timespec *start, struct timespec *end)
{
	unsigned long ms;
	unsigned long ns_end;

	ms = (unsigned long)(end->tv_sec - start->tv_sec) * 1000;

	if (end->tv_nsec >= start->tv_nsec) {
		ns_end = end->tv_nsec;
	} else {
		ns_end = (unsigned long)(end->tv_nsec) + 1000000000UL;
		ms -= 1000;
	}

	ms += (ns_end - start->tv_nsec + 500000UL) / 1000000UL;
	return ms;
}
