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
