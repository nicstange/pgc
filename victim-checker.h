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
