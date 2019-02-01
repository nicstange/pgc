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
