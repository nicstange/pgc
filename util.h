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
