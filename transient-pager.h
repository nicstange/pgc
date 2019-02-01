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
