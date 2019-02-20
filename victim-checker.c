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

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include "victim-checker.h"
#include "util.h"

int victim_checker_state_init(struct victim_checker_state *s,
			      const char *victim_file,
			      bool map_executable)
{
	int fd;
	long page_size;
	struct stat st;

	memset(s, 0, sizeof(*s));

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return -1;

	s->page_size = page_size;

	fd = open(victim_file, O_RDONLY);
	if (fd < 0)
		return -1;

	if(fstat(fd, &st)) {
		close(fd);
		return -1;
	}

	if (!S_ISREG(st.st_mode)) {
		close(fd);
		errno = EINVAL;
		return -1;
	}

	if (!st.st_size) {
		close(fd);
		errno = EINVAL;
		return -1;
	}

	s->map = mmap(NULL, s->page_size,
		      PROT_READ | (map_executable ? PROT_EXEC : 0),
		      MAP_PRIVATE, fd, 0);
	if (s->map == MAP_FAILED) {
		close(fd);
		return -1;
	}

	if (madvise(s->map, s->page_size, MADV_RANDOM | MADV_DONTDUMP)) {
		munmap(s->map, s->page_size);
		return -1;
	}

	return 0;
}

void victim_checker_state_cleanup(struct victim_checker_state *s)
{
	munmap(s->map, s->page_size);
}

int victim_checker_measure_one(struct victim_checker_state *s)
{
	struct timespec ts_start, ts_end;
	unsigned long duration_ms;
	unsigned char mincore_buf[1];

	refresh_page(s->map, 0, s->page_size);
	clock_gettime(CLOCK_MONOTONIC, &ts_start);

	mincore_buf[0] = 0x01;
	while (mincore_buf[0] & 0x01) {
		if (mincore(s->map, s->page_size, mincore_buf)) {
			return -1;
		}

	}

	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	duration_ms = ts_diff_ms(&ts_start, &ts_end);

	printf("Victim page evicted in %lums\n", duration_ms);
	fflush(stdout);
	return 0;
}
