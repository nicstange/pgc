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

#define _GNU_SOURCE
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include "util.h"
#include "transient-pager.h"

#define PAGEIN_BATCH_SIZE 32

static void pagein_batch(struct transient_pager_state *s)
{
	unsigned int i;

	for (i = 0; i < PAGEIN_BATCH_SIZE; ++i, ++s->i_page) {
		if (s->i_page == s->n_pages)
			s->i_page = 0;
		refresh_page(s->map, s->i_page, s->page_size);
	}
}

static unsigned long ts_diff_us(struct timespec *start, struct timespec *end)
{
	unsigned long us;
	unsigned long ns_end;

	us = (unsigned long)(end->tv_sec - start->tv_sec) * 1000000;

	if (end->tv_nsec >= start->tv_nsec) {
		ns_end = end->tv_nsec;
	} else {
		ns_end = (unsigned long)(end->tv_nsec) + 1000000000UL;
		us -= 1000000;
	}

	us += (ns_end - start->tv_nsec + 500UL) / 1000UL;
	return us;
}

static void do_sleep(unsigned long usec)
{
	unsigned long sec;

	sec = usec / 1000000UL;
	usec -= sec *1000000UL;

	if (sec && sleep(sec))
		return;

	if (!usec)
		return;

	usleep(usec);
}

static void* transient_pager_proc(void *arg)
{
	struct transient_pager_state *s = (struct transient_pager_state *)arg;
	const unsigned long target_period_usec =
		s->target_period_usec * PAGEIN_BATCH_SIZE;
	long acc_period_err_usec = 0;
	struct timespec ts_start, ts_end;

	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	while (1) {
		unsigned long actual_period_usec;

		pagein_batch(s);

		pthread_testcancel();

		if (acc_period_err_usec >= 0 ||
		    -acc_period_err_usec <= target_period_usec) {
			unsigned long sleep_duration_usec;
			sleep_duration_usec = ((long)target_period_usec +
					       acc_period_err_usec);
			do_sleep(sleep_duration_usec);
		}
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		actual_period_usec = ts_diff_us(&ts_start, &ts_end);
		ts_start = ts_end;
		acc_period_err_usec += ((long)target_period_usec -
					(long)actual_period_usec);
	}

	return NULL;
}


int transient_pager_state_init(struct transient_pager_state *s,
			       const char *pool_file,
			       unsigned long pagein_period_usec,
			       bool map_exec)
{
	long page_size;
	int fd;
	struct stat st;
	size_t size;

	memset(s, 0, sizeof(*s));

	s->target_period_usec = pagein_period_usec;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return -1;

	s->page_size = page_size;

	fd = open(pool_file, O_RDONLY);
	if (fd < 0)
		return -1;

	if(fstat(fd, &st)) {
		close(fd);
		return -1;
	}

	if (!S_ISREG(st.st_mode)) {
		close(fd);
		errno = ESPIPE;
		return -1;
	}

	if (!st.st_size) {
		close(fd);
		errno = ESPIPE;
		return -1;
	}

	size = st.st_size <= SIZE_MAX ? st.st_size : SIZE_MAX;
	if (size <= SIZE_MAX - (s->page_size - 1))
		size += s->page_size - 1;
	size &= ~((size_t)s->page_size - 1);

	s->size = size;
	s->n_pages = size / s->page_size;

	s->map = mmap(NULL, size,
		      PROT_READ | (map_exec ? PROT_EXEC : 0),
		      MAP_PRIVATE, fd, 0);
	if (s->map == MAP_FAILED) {
		close(fd);
		return -1;
	}

	close(fd);

	if (madvise(s->map, s->size, MADV_RANDOM | MADV_DONTDUMP)) {
		munmap(s->map, s->size);
		s->map = NULL;
		return -1;
	}



	return 0;
}

void transient_pager_state_cleanup(struct transient_pager_state *s)
{
	munmap(s->map, s->size);
}

int transient_pager_start(struct transient_pager_state *s)
{
	int r;
	r = pthread_create(&s->pager, NULL,
			   transient_pager_proc, s);
	if (r) {
		errno = r;
		return -1;
	}

	return 0;
}

void transient_pager_stop(struct transient_pager_state *s)
{
	pthread_cancel(s->pager);
	pthread_join(s->pager, NULL);
}
