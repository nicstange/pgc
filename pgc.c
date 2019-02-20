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
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/mman.h>
#include "resident-keeper.h"
#include "sigbus-fixup.h"
#include "transient-pager.h"
#include "victim-checker.h"
#include "meminfo-stats.h"

#define ME "pgc"

static const struct option longopts[] = {
	{ "resident-set-size", 1, NULL, 'r' },
	{ "resident-set-directory", 1, NULL, 'd' },
	{ "resident-set-fillup-file", 1, NULL, 'f' },
	{ "map-resident-executable", 0, NULL, 'R' },
	{ "refresh-only-resident", 0, NULL, 'q' },
	{ "launch-resident-rewarmer", 0, NULL, 'w' },
	{ "rt-sched-refresher", 0, NULL, 'c' },

	{ "transient-refill-period", 1, NULL, 't' },
	{ "transient-pool-file", 1, NULL, 'p' },
	{ "map-transient-executable", 0, NULL, 'T' },

	{ "non-evictable-set-size", 1, NULL, 'a' },

	{ "victim-file", 1, NULL, 'v' },
	{ "map-victim-executable", 0, NULL, 'V' },

	{ "help", 0, NULL, 'h' },

	{ NULL, 0, NULL, 0}
};

static const char optstring[] = ":r:d:f:Rqwca:t:p:Tv:Vh";

static int parse_set_size(const char *s, size_t *res)
{
	unsigned long long size;
	char *unit;
	int shift = 0;

	size = strtoull(s, &unit, 10);
	if (size == ULLONG_MAX && errno == ERANGE)
		return -1;

	switch (*unit) {
	case '\0':
		break;

	case 'k':
		/* fall through */
	case 'K':
		/* fall through */
		shift = 10;
		break;

	case 'm':
		/* fall through */
	case 'M':
		/* fall through */
		shift = 20;
		break;

	case 'g':
		/* fall through */
	case 'G':
		/* fall through */
		shift = 30;
		break;

	case 't':
		/* fall through */
	case 'T':
		/* fall through */
		shift = 40;
		break;

	default:
		errno = EINVAL;
		return -1;
	};

	if (shift && strcmp(unit + 1, "B") && strcmp(unit + 1, "iB")) {
		errno = EINVAL;
		return -1;
	}

	if (((size << shift) >> shift) != size) {
		errno = ERANGE;
		return -1;
	}

	size <<= shift;
	if (size > SIZE_MAX) {
		errno = ERANGE;
		return -1;
	}

	*res = (size_t)size;
	return 0;
}

static int parse_time_period(const char *s, unsigned long *res_usec)
{
	unsigned long period;
	char *unit;
	unsigned int mul = 0;

	period = strtoul(s, &unit, 10);
	if (period == ULONG_MAX && errno == ERANGE)
		return -1;

	switch (*unit) {
	case '\0':
		break;

	case 's':
		mul = 1000 * 1000; /* 1s = 10^6us */
		break;

	case 'm':
		++unit;
		mul = 1000; /* 1ms = 1000us */
		break;

	case 'u':
		++unit;
		mul = 1;
		break;

	default:
		errno = EINVAL;
		return -1;
	};

	if (mul && strcmp(unit, "s")) {
		errno = EINVAL;
		return -1;
	}

	if (!mul) {
		/* default to seconds */
		mul = 1000 * 1000;
	}

	if ((period * mul / mul) != period) {
		errno = ERANGE;
		return -1;
	}

	*res_usec = period * mul;
	return 0;
}


static int set_err_msg(char **msg, const char * const fmt, ...)
{
	va_list ap;
	int len;

	if (*msg)
		return 0;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (len <= 0)
		abort();

	*msg = malloc(len + 1);
	if (!*msg) {
		perror(ME);
		return -1;
	}

	va_start(ap, fmt);
	len = vsnprintf(*msg, len + 1, fmt, ap);
	va_end(ap);

	if (len <= 0)
		abort();

	return 0;
}

static int set_size_fmt_err(char **err_msg, char opt)
{
	return set_err_msg(err_msg,
			   "invalid size specification for \"-%c\"", opt);
}

static int set_size_overflow_err(char **err_msg, char opt)
{
	return set_err_msg(err_msg,
			   "argument of \"-%c\" is too large", opt);
}

static void show_help(void)
{
	printf("Usage: " ME " OPTION...\n\n");

	printf("General options:\n");
	printf("-h, --help\t\t\t\tdisplay this help and exit\n");
	printf("\n");
	printf("Victim page eviction checker:\n");
	printf("-v, --victim-file=FILE\t\t\t"
	       "file whose first page to monitor for\n"
	       "\t\t\t\t\tevictions\n");
	printf("-V, --map-victim-executable\t\t"
	       "map victim page executable\n");
	printf("\n");
	printf("Resident set keeper:\n");
	printf("-r, --resident-set-size=SIZE\t\t"
	       "target size for total of resident set\n"
	       "\t\t\t\t\tcandidates\n");
	printf("-d, --resident-set-direcory=DIR\t\t"
	       "directory to scan for files with\n"
	       "\t\t\t\t\tresident pages to use as resident set\n"
	       "\t\t\t\t\tcandidates\n");
	printf("-f, --resident-set-fillup-file=FILE\t"
	       "file to fill up the resident set\n"
	       "\t\t\t\t\tcandidates from\n");
	printf("-q, --refresh-only-resident\t\t"
	       "don't refresh non-resident pages\n");
	printf("-w, --launch-resident-rewarmer\t\t"
	       "schedule background IO to read\n"
	       "\t\t\t\t\tnon-resident pages back in\n");
	printf("-c, --rt-sched-refresher\t\t"
	       "schedule residency refresher thread with\n"
	       "\t\t\t\t\treal time priority\n");
	printf("\n");
	printf("Transient set pager:\n");
	printf("-t, --transient-refill-period=TIME\t"
	       "time interval to read one transient page\n"
	       "\t\t\t\t\tin, i.e. inverse of read frequency\n");
	printf("-p, --transient-pool-file=FILE\t\t"
	       "file to read transient pages from\n");
	printf("-T, --map-transient-executable\t\t"
	       "map transient pages executable\n");
	printf("\n");
	printf("Anonymous memory hogger:\n");
	printf("-a, --non-evictable-set-size=SIZE\t"
	       "amount of anonymous memory to allocate\n");
}

static void non_evictable_fill(void *map, size_t size, size_t page_size)
{
	size_t i;

	for (i = 0; i < size; i += page_size) {
		int *p = (int*)((char *)map + i);
		p[0] = rand();
		p[1] = rand();
		p[3] = rand();
	}
}

int main(int argc, char *argv[])
{
	int r = 0;
	int opt;
	char *err_msg = NULL;

	size_t resident_set_size = 0;
	bool resident_set_size_given = false;
	const char **resident_set_directories = NULL;
	const char *resident_set_fillup_file = NULL;
	size_t n_resident_set_directories = 0;
	bool map_resident_exec = false;
	bool refresh_only_resident = false;
	bool launch_resident_rewarmer = false;
	bool rt_sched_refresher = false;

	const char *transient_pool_file = NULL;
	unsigned long transient_refill_period_usec = 0;
	bool transient_refill_period_given = false;
	bool map_transient_exec = false;

	size_t non_evictable_set_size = 0;
	bool non_evictable_set_size_given = false;

	const char *victim_file = NULL;
	bool map_victim_exec = 0;

	bool launch_meminfo_reporter = false;

	unsigned long page_size;

	struct victim_checker_state vcs;
	struct transient_pager_state tps;
	struct resident_keeper_state rks;
	struct meminfo_reporter_state mrs;
	void *non_evictable_map = NULL;

	opterr = 0;
	while ((opt = getopt_long(argc, argv, optstring, longopts, NULL))
	       != -1) {

		switch (opt) {
		case '?':
			r = set_err_msg(&err_msg,
					"unknown command line option");
			if (r) {
				free(resident_set_directories);
				return 2;
			}
			break;

		case ':':
			r = set_err_msg(&err_msg,
					"\"-%c\" expects an argument",
					optopt);
			if (r) {
				free(resident_set_directories);
				return 2;
			}
			break;

		case 'r':
			if (resident_set_size_given) {
				r = set_err_msg(&err_msg,
						"\"-r\" may be specified only once");
				if (r) {
					free(resident_set_directories);
					return 2;
				}
			}
			resident_set_size_given = true;

			if (parse_set_size(optarg, &resident_set_size)) {
				if (errno == EINVAL) {
					r = set_size_fmt_err(&err_msg, 'r');
					if (r) {
						free(resident_set_directories);
						return 2;
					}
				} else {
					/* overflow will be handled below */
					resident_set_size = SIZE_MAX;
				}
			}
			break;

		case 'd':
			resident_set_directories =
				realloc(resident_set_directories,
					((n_resident_set_directories + 1)*
					 sizeof(*resident_set_directories)));
			if (!resident_set_directories) {
				perror(ME);
				free(resident_set_directories);
				free(err_msg);
				return 2;
			}
			resident_set_directories[n_resident_set_directories] =
				optarg;
			++n_resident_set_directories;
			break;

		case 'f':
			if (resident_set_fillup_file) {
				r = set_err_msg(&err_msg,
						"\"-f\" may be specified only once");
				if (r) {
					free(resident_set_directories);
					return 2;
				}
			}
			resident_set_fillup_file = optarg;

			break;

		case 'R':
			map_resident_exec = true;
			break;

		case 'q':
			refresh_only_resident = true;
			break;

		case 'w':
			launch_resident_rewarmer = true;
			break;

		case 'c':
			rt_sched_refresher = true;
			break;

		case 't':
			if (transient_refill_period_given) {
				r = set_err_msg(&err_msg,
						"\"-t\" may be specified only once");
				if (r) {
					free(resident_set_directories);
					return 2;
				}
			}
			transient_refill_period_given = true;

			if (parse_time_period(optarg,
					      &transient_refill_period_usec)) {
				if (errno == EINVAL) {
					r = set_err_msg(&err_msg,
							"invalid time specification for \"-t\"");

				} else {
					/* ERANGE */
					r = set_err_msg(&err_msg,
							"argument of \"-t\" is too large");
				}

				if (r) {
					free(resident_set_directories);
					return 2;
				}
			}
			break;

		case 'p':
			if (transient_pool_file) {
				r = set_err_msg(&err_msg,
						"\"-p\" may be specified only once");
				if (r) {
					free(resident_set_directories);
					return 2;
				}
			}
			transient_pool_file = optarg;

			break;

		case 'T':
			map_transient_exec = true;
			break;

		case 'a':
			if (non_evictable_set_size_given) {
				r = set_err_msg(&err_msg,
						"\"-a\" may be specified only once");
				if (r) {
					free(resident_set_directories);
					return 2;
				}
			}
			non_evictable_set_size_given = true;

			if (parse_set_size(optarg, &non_evictable_set_size)) {
				if (errno == EINVAL) {
					r = set_size_fmt_err(&err_msg, 'a');
					if (r) {
						free(resident_set_directories);
						return 2;
					}
				} else {
					/* overflow will be handled below */
					non_evictable_set_size = SIZE_MAX;
				}
			}
			break;

		case 'v':
			if (victim_file) {
				r = set_err_msg(&err_msg,
						"\"-v\" may be specified only once");
			}
			victim_file = optarg;
			break;

		case 'V':
			map_victim_exec = true;
			break;

		case 'h':
			show_help();
			if (err_msg)
				free(err_msg);
			return 0;
		};
	}

	if (!resident_set_size_given &&
	    !transient_refill_period_given &&
	    !non_evictable_set_size_given &&
	    !victim_file) {
		r = set_err_msg(&err_msg,
				"at least one of \"-r\", \"-t\", \"-a\" or \"-v\" is required");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}

	if (resident_set_size_given && !n_resident_set_directories
	    && !resident_set_fillup_file) {
		r = set_err_msg(&err_msg,
				"\"-r\" requires \"-d\" or \"-f\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}

	if (n_resident_set_directories && !resident_set_size_given) {
		r = set_err_msg(&err_msg,
				"\"-d\" requires \"-r\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (resident_set_fillup_file && !resident_set_size_given) {
		r = set_err_msg(&err_msg,
				"\"-f\" requires \"-r\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (map_resident_exec && !resident_set_size_given) {
		r = set_err_msg(&err_msg,
				"\"-R\" requires \"-r\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (refresh_only_resident && !resident_set_size_given) {
		r = set_err_msg(&err_msg,
				"\"-q\" requires \"-r\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (launch_resident_rewarmer && !resident_set_size_given) {
		r = set_err_msg(&err_msg,
				"\"-w\" requires \"-r\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (rt_sched_refresher && !resident_set_size_given) {
		r = set_err_msg(&err_msg,
				"\"-c\" requires \"-r\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (launch_resident_rewarmer && !refresh_only_resident) {
		r = set_err_msg(&err_msg,
				"\"-w\" requires \"-q\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}

	if ((transient_refill_period_given && !transient_pool_file) ||
	    (!transient_refill_period_given && transient_pool_file)) {
		r = set_err_msg(&err_msg,
				"either both or none of \"-t\" and \"-p\" must be given");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}
	if (map_transient_exec && !transient_refill_period_given) {
		r = set_err_msg(&err_msg,
				"\"-T\" requires \"-t\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}

	if (map_victim_exec && !victim_file) {
		r = set_err_msg(&err_msg,
				"\"-V\" requires \"-v\"");
		if (r) {
			free(resident_set_directories);
			return 2;
		}
	}

	if (!resident_set_size_given && transient_refill_period_given) {
		/*
		 * The resident set refresher will report meminfo
		 * stats already.
		 */
		launch_meminfo_reporter = true;
	}

	/*
	 * Round up resident_set_size and non_evictable_set_size to
	 * the next multiple of the page size.
	 */
	page_size = sysconf(_SC_PAGESIZE);

	if (resident_set_size > SIZE_MAX - (page_size - 1)) {
		if (set_size_overflow_err(&err_msg, 'r')) {
			free(resident_set_directories);
			return 2;
		}
	}
	resident_set_size = ((resident_set_size + (page_size - 1)) &
			      ~((size_t)page_size - 1));

	if (non_evictable_set_size > SIZE_MAX - (page_size - 1)) {
		if (set_size_overflow_err(&err_msg, 'r')) {
			free(resident_set_directories);
			return 2;
		}
	}
	non_evictable_set_size = ((non_evictable_set_size + (page_size - 1)) &
				  ~((size_t)page_size - 1));

	if (err_msg) {
		fputs("command line error: ", stderr);
		fputs(err_msg, stderr);
		fputc('\n', stderr);
		fputc('\n', stderr);
		fputs("See \"" ME "-h\" for help.\n", stderr);
		free(err_msg);
		free(resident_set_directories);
		return 1;
	}

	if (sigbus_fixup_init()) {
		perror(ME);
		free(resident_set_directories);
		return 2;
	}

	if (launch_meminfo_reporter) {
		r = meminfo_reporter_state_init(&mrs, 500);
		if (r) {
			perror(ME);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}
	}

	if (victim_file) {
		r = victim_checker_state_init(&vcs, victim_file,
					      map_victim_exec);
		if (r) {
			perror(ME);
			if (launch_meminfo_reporter)
				meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}
	}

	if (transient_pool_file) {
		r = transient_pager_state_init(&tps, transient_pool_file,
					       transient_refill_period_usec,
					       map_transient_exec);
		if (r) {
			perror(ME);
			if (victim_file)
				victim_checker_state_cleanup(&vcs);
			if (launch_meminfo_reporter)
				meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}
	}


	if (resident_set_size) {
		size_t n_target_pages;
		size_t i;

		n_target_pages = resident_set_size / page_size;
		if (resident_keeper_state_init(&rks, n_target_pages,
					       map_resident_exec,
					       refresh_only_resident,
					       launch_resident_rewarmer,
					       rt_sched_refresher)) {
			perror(ME);
			if (transient_pool_file)
				transient_pager_state_cleanup(&tps);
			if (victim_file)
				victim_checker_state_cleanup(&vcs);
			if (launch_meminfo_reporter)
				meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}

		if (resident_set_fillup_file) {
			if (resident_keeper_set_fillup_file
				(&rks, resident_set_fillup_file)) {
				perror(ME);
				resident_keeper_state_cleanup(&rks);
				if (transient_pool_file)
					transient_pager_state_cleanup(&tps);
				if (victim_file)
					victim_checker_state_cleanup(&vcs);
				if (launch_meminfo_reporter)
					meminfo_reporter_state_cleanup(&mrs);
				sigbus_fixup_cleanup();
				free(resident_set_directories);
				return 2;
			}
		}

		if (n_resident_set_directories) {
			printf("Searching for resident files...\n");
			fflush(stdout);
		}
		for (i = 0; i < n_resident_set_directories; ++i) {
			const char *d = resident_set_directories[i];

			if (resident_keeper_scan_directory(&rks, d)) {
				perror(ME);
				resident_keeper_state_cleanup(&rks);
				if (transient_pool_file)
					transient_pager_state_cleanup(&tps);
				if (victim_file)
					victim_checker_state_cleanup(&vcs);
				if (launch_meminfo_reporter)
					meminfo_reporter_state_cleanup(&mrs);
				sigbus_fixup_cleanup();
				free(resident_set_directories);
				return 2;
			}
		}
		if (n_resident_set_directories) {
			printf("Found %lu resident pages in %lu files\n",
				rks.n_pages, rks.mappings.n_nodes);
			fflush(stdout);
		}
	}

	if (non_evictable_set_size) {
		non_evictable_map = mmap(NULL, non_evictable_set_size,
					 PROT_WRITE | PROT_READ,
					 MAP_PRIVATE | MAP_ANONYMOUS,
					 -1, 0);
		if (non_evictable_map == MAP_FAILED) {
			perror(ME);
			if (resident_set_size)
				resident_keeper_state_cleanup(&rks);
			if (transient_pool_file)
				transient_pager_state_cleanup(&tps);
			if (victim_file)
				victim_checker_state_cleanup(&vcs);
			if (launch_meminfo_reporter)
				meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}

		non_evictable_fill(non_evictable_map, non_evictable_set_size,
				   (size_t)page_size);
	}

	if (resident_set_size) {
		if (resident_keeper_start(&rks)) {
			perror(ME);
			if (non_evictable_map) {
				munmap(non_evictable_map,
				       non_evictable_set_size);
			}
			resident_keeper_state_cleanup(&rks);
			if (transient_pool_file)
				transient_pager_state_cleanup(&tps);
			if (victim_file)
				victim_checker_state_cleanup(&vcs);
			if (launch_meminfo_reporter)
				meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}

		sleep(10);
	}

	if (transient_pool_file) {
		if (transient_pager_start(&tps)) {
			perror(ME);
			if (non_evictable_map) {
				munmap(non_evictable_map,
				       non_evictable_set_size);
			}
			if (resident_set_size) {
				resident_keeper_stop(&rks);
				resident_keeper_state_cleanup(&rks);
			}
			transient_pager_state_cleanup(&tps);
			if (victim_file)
				victim_checker_state_cleanup(&vcs);
			if (launch_meminfo_reporter)
				meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}
	}

	if (launch_meminfo_reporter) {
		if (meminfo_reporter_start(&mrs)) {
			perror(ME);
			if (non_evictable_map) {
				munmap(non_evictable_map,
				       non_evictable_set_size);
			}
			if (resident_set_size) {
				resident_keeper_stop(&rks);
				resident_keeper_state_cleanup(&rks);
			}
			if (transient_pool_file) {
				transient_pager_stop(&tps);
				transient_pager_state_cleanup(&tps);
			}
			if (victim_file)
				victim_checker_state_cleanup(&vcs);
			meminfo_reporter_state_cleanup(&mrs);
			sigbus_fixup_cleanup();
			free(resident_set_directories);
			return 2;
		}
	}

	r = 0;
	if (victim_file) {
		while (1) {
			printf("Making measurement\n");
			fflush(stdout);
			if (victim_checker_measure_one(&vcs)) {
				r = 2;
				goto out;
			}
			sleep(1);
		}
	} else {
		while (1) {
			getchar();
		}
	}

out:
	if (launch_meminfo_reporter) {
		meminfo_reporter_stop(&mrs);
		meminfo_reporter_state_cleanup(&mrs);
	}
	if (non_evictable_map)
		munmap(non_evictable_map, non_evictable_set_size);
	if (resident_set_size) {
		resident_keeper_stop(&rks);
		resident_keeper_state_cleanup(&rks);
	}
	if (transient_pool_file) {
		transient_pager_stop(&tps);
		transient_pager_state_cleanup(&tps);
	}
	if (victim_file)
		victim_checker_state_cleanup(&vcs);
	sigbus_fixup_cleanup();
	free(resident_set_directories);

	return r;
}
