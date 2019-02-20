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

#ifndef _UTIL_H
#define _UTIL_H

#include <time.h>

#define container_of(p, type, member)					\
	((type *)((char *)(p) - __builtin_offsetof(type, member)))

static inline void refresh_page(void *map, size_t i_page, size_t page_size)
{
	char *p = &((char*)map)[i_page * page_size];
	/* asm volatile("prefetcht0 %0" : : "m" (p[page_size * 8])); */
	asm volatile("movq %0, %%r11" : :
		"m" (*p) : "r11");
	/* asm volatile("movntdqa %0, %%xmm0" : : */
	/* 	     "m" (((char*)map)[i_page * page_size]) : "xmm0"); */
}

unsigned long ts_diff_ms(struct timespec *start, struct timespec *end);

#endif
