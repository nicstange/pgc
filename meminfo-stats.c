#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include "meminfo-stats.h"

static int parse_meminfo_field(const char *line, unsigned long *result)
{
	const char *p;
	char *unit;

	p = strchr(line, ':');
	if (!p) {
		errno = EINVAL;
		return -1;
	}
	++p;

	*result = strtoul(p, &unit, 10);

	if (p == unit) {
		errno = EINVAL;
		return -1;
	}

	while (isspace(*unit))
		++unit;

	if (strcmp(unit, "kB")) {
		if (*result * 1024 / 1024 != *result) {
			errno = ERANGE;
			return -1;
		}
		*result *= 1024;

	} else if (*unit != '\0') {
		errno = EINVAL;
		return -1;
	}

	return 0;
}

int meminfo_read_stats(unsigned long *total,
		       unsigned long *free,
		       unsigned long *pc_a_active,
		       unsigned long *pc_a_inactive,
		       unsigned long *pc_f_active,
		       unsigned long *pc_f_inactive)
{
	struct field {
		const char *prefix;
		const size_t prefix_len;
		unsigned long *result;
		bool found;
	} fields[] = {
		{"MemTotal:", sizeof("MemTotal:") - 1, total},
		{"MemFree:", sizeof("MemFree:") - 1, free},
		{"Active(anon):", sizeof("Active(anon):") - 1, pc_a_active},
		{"Inactive(anon):", sizeof("Inactive(anon):") - 1,
			pc_a_inactive},
		{"Active(file):", sizeof("Active(file):") - 1,
			pc_f_active},
		{"Inactive(file):", sizeof("Inactive(file):") - 1,
			pc_f_inactive},
		{ NULL },
	};
	bool any_not_found, line_consumed;
	FILE *f_meminfo;
	char line[256];
	int r;

	f_meminfo = fopen("/proc/meminfo", "r");
	if (!f_meminfo)
		return -1;

	any_not_found = true;
	while (any_not_found && fgets(line, sizeof(line), f_meminfo)) {
		struct field *f;

		any_not_found = false;
		line_consumed = false;
		for (f = fields; f->prefix; ++f) {
			if (f->found || !f->result)
				continue;

			if (line_consumed ||
			    strncmp(line, f->prefix, f->prefix_len)) {
				any_not_found = true;
				continue;
			}

			line_consumed = true;
			f->found = true;
			r = parse_meminfo_field(line, f->result);
			if (r) {
				fclose(f_meminfo);
				return r;
			}
		}
	}

	fclose(f_meminfo);

	if (any_not_found) {
		errno = ENOENT;
		return -1;
	}

	return 0;
}

static void* meminfo_reporter_proc(void *arg)
{
	struct meminfo_reporter_state *s = (struct meminfo_reporter_state *)arg;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (1) {
		unsigned long mi_pc_f_active = 0, mi_pc_f_inactive = 0,
			      mi_free = 0;

		meminfo_read_stats(NULL, &mi_free, NULL, NULL,
				   &mi_pc_f_active, &mi_pc_f_inactive);
		printf("meminfo: active file %lu, inactive file %lu, free %lu\n",
		       mi_pc_f_active / s->page_size,
		       mi_pc_f_inactive / s->page_size,
		       mi_free / s->page_size);
		usleep(s->interval_us);
	}

	return NULL;
}


int meminfo_reporter_state_init(struct meminfo_reporter_state *s,
				unsigned long interval_ms)
{
	long page_size;

	memset(s, 0, sizeof(*s));

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return -1;

	s->page_size = page_size;
	s->interval_us = interval_ms * 1000;

	return 0;
}

void meminfo_reporter_state_cleanup(struct meminfo_reporter_state *s)
{
	return;
}

int meminfo_reporter_start(struct meminfo_reporter_state *s)
{
	int r;

	r = pthread_create(&s->reporter, NULL,
			   meminfo_reporter_proc, s);
	if (r) {
		errno = r;
		return -1;
	}

	return 0;
}

void meminfo_reporter_stop(struct meminfo_reporter_state *s)
{
	pthread_cancel(s->reporter);
	pthread_join(s->reporter, NULL);
}
