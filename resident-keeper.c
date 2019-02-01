#define _GNU_SOURCE
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <sched.h>
#include "resident-keeper.h"
#include "util.h"
#include "sigbus-fixup.h"

struct dir_stack_entry
{
	DIR *d;
	struct dirent de[];
};

struct dir_stack
{
	size_t n_entries;
	size_t allocated_entries;
	struct dir_stack_entry **entries;
};

static struct dir_stack_entry* dir_stack_push(struct dir_stack *ds, DIR *d)
{
	long name_max;
	size_t dse_size;
	int fd;
	struct dir_stack_entry *dse;

	if (ds->n_entries == ds->allocated_entries) {
		struct dir_stack_entry **entries;
		size_t n;

		n = !ds->n_entries ? 16 : 2 * ds->n_entries;
		entries = realloc(ds->entries, n * sizeof(*entries));
		if (!entries)
			return NULL;

		ds->allocated_entries = n;
		ds->entries = entries;
	}

	fd = dirfd(d);
	if (fd < 0)
		return NULL;

	name_max = fpathconf(fd, _PC_NAME_MAX);
	if (name_max == -1)
		name_max = 255;
	dse_size = (sizeof(struct dir_stack_entry) +
		    __builtin_offsetof(struct dirent, d_name) + name_max + 1);

	dse = malloc(dse_size);
	if (!dse)
		return NULL;

	dse->d = d;
	ds->entries[ds->n_entries++] = dse;

	return dse;
}

static struct dir_stack_entry* dir_stack_pop(struct dir_stack *ds)
{
	--ds->n_entries;
	closedir(ds->entries[ds->n_entries]->d);
	free(ds->entries[ds->n_entries]);
	if (!ds->n_entries)
		return NULL;

	return ds->entries[ds->n_entries - 1];
}

static void dir_stack_destroy(struct dir_stack *ds)
{
	size_t i;

	for (i = 0; i < ds->n_entries; ++i) {
		closedir(ds->entries[i]->d);
		free(ds->entries[i]);
	}
	free(ds->entries);
}


typedef int (*walk_dir_proc)(int parentfd, const char *name,
			     struct stat *s, void *arg);

static int walk_dir(const char *path, walk_dir_proc proc, void *arg)
{
	struct dir_stack ds = {};
	struct dir_stack_entry *dse_top;
	int r;
	DIR *d;

	d = opendir(path);
	if (!d)
		return -1;

	dse_top = dir_stack_push(&ds, d);
	if (!dse_top) {
		closedir(d);
		dir_stack_destroy(&ds);
		return -1;
	}

	while(dse_top) {
		struct dirent *res;
		int fd;
		struct stat s;
		const char *name;

		r = readdir_r(dse_top->d, &dse_top->de[0], &res);
		if (r) {
			dir_stack_destroy(&ds);
			errno = r;
			return -1;
		}

		if (!res) {
			dse_top = dir_stack_pop(&ds);
			continue;
		}

		name = dse_top->de[0].d_name;
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		fd = dirfd(dse_top->d);
		if (fd < 0) {
			dir_stack_destroy(&ds);
			return -1;
		}

		r = fstatat(fd, name, &s, AT_SYMLINK_NOFOLLOW);
		if (r)
			continue;

		r = proc(fd, name, &s, arg);
		if (r <= 0) {
			dir_stack_destroy(&ds);
			return r;
		}

		if (S_ISDIR(s.st_mode)) {
			fd = openat(fd, name, O_RDONLY | O_DIRECTORY);
			if (fd < 0)
				continue;
			d = fdopendir(fd);
			if (!d) {
				close(fd);
				continue;
			}

			dse_top = dir_stack_push(&ds, d);
			if (!dse_top) {
				closedir(d);
				dir_stack_destroy(&ds);
				return -1;
			}
		}
	}

	return 0;
}

static void resident_mapping_init(struct resident_mapping *m)
{
	memset(m, 0, sizeof(*m));
}

static void resident_mapping_destroy(struct resident_mapping *m)
{
	free(m->__extern_ranges);
}

static struct resident_range*
resident_mapping_get_range(struct resident_mapping *m, size_t i)
{
	if (i < RESIDENT_RANGE_INLINE_COUNT)
		return &m->__inlined_ranges[i];
	else
		return &m->__extern_ranges[i - RESIDENT_RANGE_INLINE_COUNT];
}

static struct resident_range*
resident_mapping_add_range(struct resident_mapping *m)
{
	size_t n_extern_ranges;
	struct resident_range *extern_ranges;

	if (m->n_resident_ranges < RESIDENT_RANGE_INLINE_COUNT) {
		m->n_resident_ranges++;
		return &m->__inlined_ranges[m->n_resident_ranges - 1];
	}

	n_extern_ranges =
		m->n_resident_ranges - RESIDENT_RANGE_INLINE_COUNT + 1;
	extern_ranges = realloc(m->__extern_ranges,
				n_extern_ranges * sizeof(*m->__extern_ranges));
	if (!extern_ranges)
		return NULL;

	m->__extern_ranges = extern_ranges;
	m->n_resident_ranges++;
	return &m->__extern_ranges[n_extern_ranges - 1];
}

static int resident_mapping_find_ranges(struct resident_mapping *m,
					unsigned char *buf, size_t buf_size,
					size_t page_size)
{
	int r;
	size_t mincore_offset = 0;
	const size_t mincore_size = buf_size * page_size;
	size_t remaining = m->size;
	size_t n_pages = 0;

	if (!remaining)
		return 0;

	size_t range_begin;
	bool in_resident_range = false;
	do {
		size_t cur_size =
			mincore_size > remaining ? remaining : mincore_size;
		size_t cur_index, cur_offset;

		r = mincore((char *)m->map + mincore_offset, cur_size, buf);
		if (r)
			return -1;

		cur_index = 0;
		cur_offset = 0;
		while (cur_offset < cur_size) {
			if (!in_resident_range) {
				/*
				 * Search for the beginning of a
				 * contiguous range of resident pages.
				 */
				while (cur_offset < cur_size) {
					if (buf[cur_index] & 0x01) {
						range_begin = (mincore_offset +
							       cur_offset);
						in_resident_range = true;
						break;
					}
					cur_index++;
					cur_offset += page_size;
				}
			}

			if (in_resident_range) {
				/*
				 * Search for the end of a contiguous
				 * range of resident pages.
				 */
				while (cur_offset < cur_size) {
					if (!(buf[cur_index] & 0x01)) {
						struct resident_range *rr;
						size_t range_end;

						range_end = (mincore_offset +
							     cur_offset);
						in_resident_range = false;

						rr = resident_mapping_add_range(m);
						if (!rr)
							return -1;

						rr->offset = range_begin;
						rr->n_pages = ((range_end -
								 range_begin) /
							       page_size);
						n_pages += rr->n_pages;
						break;
					}
					cur_index++;
					cur_offset += page_size;
				}
			}
		}

		mincore_offset += cur_size;
		remaining -= cur_size;
	} while (remaining);

	if (in_resident_range) {
		struct resident_range *rr;
		size_t range_end;

		range_end = m->size;

		rr = resident_mapping_add_range(m);
		if (!rr)
			return -1;

		rr->offset = range_begin;
		rr->n_pages = (range_end - range_begin) / page_size;
		n_pages += rr->n_pages;
	}

	m->n_pages = n_pages;

	return 0;
}

static int resident_mapping_comp(void *p1, void *p2, void *arg)
{
	struct resident_mapping *m1 = (struct resident_mapping *)p1;
	struct resident_mapping *m2 = (struct resident_mapping *)p2;

	if (m1->mapped_executable && !m2->mapped_executable)
		return 1;
	else if (!m1->mapped_executable && m2->mapped_executable)
		return -1;

	if (m1->n_pages < m2->n_pages)
		return -1;
	else if (m1->n_pages > m2->n_pages)
		return 1;
	else
		return 0;
}

static void resident_mapping_move(void *to, void *from, void *arg)
{
	struct resident_mapping *m_to = (struct resident_mapping *)to;
	struct resident_mapping *m_from = (struct resident_mapping *)from;
	struct resident_keeper_state *s = (struct resident_keeper_state *)arg;

	*m_to = *m_from;
	rb_relocate_node(&m_to->id_tree_node, &m_from->id_tree_node,
			 &s->id_tree_root);
}

static int resident_mapping_comp_id(struct rb_node *node, void *data)
{
	struct resident_mapping *m =
		container_of(node, struct resident_mapping, id_tree_node);
	struct resident_mapping_id *node_id = &m->id;
	struct resident_mapping_id *lookup_id =
		(struct resident_mapping_id *)data;
	unsigned long lookup_dev;
	unsigned long node_dev;

	if (lookup_id->dev == node_id->dev) {
		unsigned long lookup_ino = (unsigned long)lookup_id->ino;
		unsigned long node_ino = (unsigned long)node_id->ino;

		if (lookup_ino < node_ino)
			return -1;
		else if (lookup_ino > node_ino)
			return 1;
		else
			return 0;

	}

	lookup_dev = (unsigned long)lookup_id->dev;
	node_dev = (unsigned long)node_id->dev;
	if (lookup_dev < node_dev)
		return -1;
	else if (lookup_dev > node_dev)
		return 1;
	else
		return 0;
}


int resident_keeper_state_init(struct resident_keeper_state *s,
			       size_t target_n_pages, bool map_executable)
{
	int r;
	long page_size;
	unsigned int node_alloc_batch;

	memset(s, 0, sizeof(*s));

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return -1;

	resident_mapping_init(&s->fillup_mapping);

	/*
	 * Make the heap's node allocation batch size a multiple of
	 * the page size.
	 */
	node_alloc_batch = (page_size * sizeof(struct resident_mapping) /
			    sizeof(unsigned long) /
			    sizeof(struct resident_mapping));
	r = heap_init(&s->mappings, sizeof(struct resident_mapping),
		      resident_mapping_comp, NULL,
		      resident_mapping_move, s,
		      node_alloc_batch);
	if (r)
		return -1;

	s->mincore_buf =
		(unsigned char *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (s->mincore_buf == MAP_FAILED) {
		heap_destroy(&s->mappings);
		return -1;
	}
	s->mincore_buf_size = page_size;

	s->page_size = page_size;
	s->target_n_pages = target_n_pages;
	s->map_executable = map_executable;

	return 0;
}

static int resident_keeper_state_cleanup_one(void *p, void *arg)
{
	struct resident_mapping *m = (struct resident_mapping *)p;

	munmap(m->map, m->size);
	resident_mapping_destroy(m);

	return 1;
}

void resident_keeper_state_cleanup(struct resident_keeper_state *s)
{
	munmap(s->mincore_buf, s->mincore_buf_size);
	heap_for_each(&s->mappings, resident_keeper_state_cleanup_one,
		      NULL);
	heap_destroy(&s->mappings);
	if (s->fillup_mapping.map)
		resident_keeper_state_cleanup_one(&s->fillup_mapping, s);
}


static int map_one_file(struct resident_mapping *m,
			int fd, off_t st_size,
			struct resident_keeper_state *s)
{
	size_t size;

	size = st_size <= SIZE_MAX ? st_size : SIZE_MAX;
	if (size <= SIZE_MAX - (s->page_size - 1))
		size += s->page_size - 1;
	size &= ~((size_t)s->page_size - 1);

	m->size = size;

	if (s->map_executable) {
		m->map = mmap(NULL, size, PROT_READ | PROT_EXEC,
			     MAP_PRIVATE, fd, 0);
		if (m->map != MAP_FAILED) {
			m->mapped_executable = true;

		} else {
			m->map = NULL;
			if (errno == EPERM) {
				errno = 0;
			} else {
				return -1;
			}
		}
	}

	if (!m->mapped_executable) {
		m->map = mmap(NULL, size, PROT_READ | PROT_EXEC,
			     MAP_PRIVATE, fd, 0);
		if (m->map == MAP_FAILED) {
			m->map = NULL;
			return -1;
		}
	}

	/*
	 * Suppress readahead into the "holes" which currently aren't
	 * resident.
	 */
	if (madvise(m->map, m->size, MADV_RANDOM | MADV_DONTDUMP)) {
		munmap(m->map, m->size);
		m->map = NULL;
		return -1;
	}

	return 0;
}

static int scan_residency_one(int parentfd, const char *name,
			      struct stat *st, void *arg)
{
	struct resident_keeper_state *s = (struct resident_keeper_state *)arg;
	struct resident_mapping m;
	struct rb_node **id_lookup, *id_lookup_parent;
	int fd;
	struct resident_mapping *smallest_m;

	if (!S_ISREG(st->st_mode))
		return 1;

	if (!st->st_size)
		return 1;

	resident_mapping_init(&m);
	m.id.dev = st->st_dev;
	m.id.ino = st->st_ino;

	id_lookup = &s->id_tree_root;
	rb_lookup(&id_lookup, resident_mapping_comp_id, &m.id,
		  &id_lookup_parent);

	if (*id_lookup) {
		resident_mapping_destroy(&m);
		return 1;
	}

	fd = openat(parentfd, name, O_RDONLY);
	if (fd < 0) {
		resident_mapping_destroy(&m);
		return 1;
	}

	if (map_one_file(&m, fd, st->st_size, s)) {
		close(fd);
		resident_mapping_destroy(&m);
		return 1;
	}
	close(fd);

	if (resident_mapping_find_ranges(&m, s->mincore_buf,
					 s->mincore_buf_size,
					 s->page_size)) {
		munmap(m.map, m.size);
		resident_mapping_destroy(&m);
		return -1;
	}
	if (!m.n_pages) {
		munmap(m.map, m.size);
		resident_mapping_destroy(&m);
		return 1;
	}

	smallest_m = (struct resident_mapping *)heap_min_node(&s->mappings);
	if (smallest_m && s->n_pages >= s->target_n_pages &&
	    resident_mapping_comp(&m, smallest_m, s) <= 0) {
		/*
		 * We have already found enough pages and the current
		 * mapping is not better than any other. Drop it.
		 */
		munmap(m.map, m.size);
		resident_mapping_destroy(&m);
		return 1;
	}

	s->n_pages += m.n_pages;
	if (m.mapped_executable)
		s->n_pages_executable += m.n_pages;
	rb_insert(&m.id_tree_node, id_lookup_parent, id_lookup,
		  &s->id_tree_root);
	if (heap_insert_node(&s->mappings, &m)) {
		s->n_pages -= m.n_pages;
		if (m.mapped_executable)
			s->n_pages_executable -= m.n_pages;
		rb_delete(&m.id_tree_node, &s->id_tree_root);
		munmap(m.map, m.size);
		resident_mapping_destroy(&m);
		return -1;
	}

	/* Finally, remove less optimal mappings if possible. */
	while (smallest_m &&
	       s->n_pages - smallest_m->n_pages >= s->target_n_pages) {

		s->n_pages -= smallest_m->n_pages;
		if (smallest_m->mapped_executable)
			s->n_pages_executable -= smallest_m->n_pages;
		rb_delete(&smallest_m->id_tree_node, &s->id_tree_root);
		munmap(smallest_m->map, smallest_m->size);
		resident_mapping_destroy(smallest_m);
		heap_pop_min_node(&s->mappings);

		smallest_m =
			(struct resident_mapping *)heap_min_node(&s->mappings);
	}

	return 1;
}

int resident_keeper_scan_directory(struct resident_keeper_state *s,
				   const char *path)
{
	return walk_dir(path, scan_residency_one, s);
}

int resident_keeper_set_fillup_file(struct resident_keeper_state *s,
				    const char *name)
{
	struct resident_mapping *m = &s->fillup_mapping;
	int fd;
	struct stat st;
	struct rb_node **id_lookup, *id_lookup_parent;
	struct resident_range *rr;

	if (m->map) {
		errno = EINVAL;
		return -1;
	}

	fd = open(name, O_RDONLY);
	if (fd < 0)
		return -1;

	if (fstat(fd, &st)) {
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

	m->id.dev = st.st_dev;
	m->id.ino = st.st_ino;

	id_lookup = &s->id_tree_root;
	rb_lookup(&id_lookup, resident_mapping_comp_id, &m->id,
		  &id_lookup_parent);

	if (*id_lookup) {
		close(fd);
		errno = EEXIST;
		return -1;
	}

	if (map_one_file(m, fd, st.st_size, s)) {
		close(fd);
		return -1;
	}
	close(fd);

	rb_insert(&m->id_tree_node, id_lookup_parent, id_lookup,
		  &s->id_tree_root);

	m->n_pages = m->size / s->page_size;

	rr = resident_mapping_add_range(m);
	rr->offset = 0;
	rr->n_pages = m->n_pages;

	return 0;
}

static int resident_keeper_refresh_one(void *p, void *arg)
{
	int r;
	struct resident_mapping *m = (struct resident_mapping *)p;
	struct resident_keeper_state *s = (struct resident_keeper_state *)arg;
	size_t active_n_pages = __atomic_load_n(&s->active_n_pages,
						__ATOMIC_ACQUIRE);

	size_t n_pages = (active_n_pages - s->i_page > m->n_pages ?
			  m->n_pages : active_n_pages - s->i_page);
	size_t i_page;
	size_t i_resident_range;

	r = 1;
	s->i_page += n_pages;
	if (s->i_page == active_n_pages) {
		r = 0;
		s->i_page = 0;
	}

	if (m->dead) {
		pthread_testcancel();
		return r;
	}

	sigbus_fixup.active = true;
	if (setjmp(sigbus_fixup.jmp_buf)) {
		m->dead = true;
		pthread_testcancel();
		return r;
	}

	for (i_resident_range = 0, i_page = 0;
	     (i_resident_range < m->n_resident_ranges &&
	      i_page < n_pages);
	     ++i_resident_range) {
		struct resident_range *r =
			resident_mapping_get_range(m, i_resident_range);
		size_t i_page_in_range;

		for (i_page_in_range = 0;
		     i_page_in_range < r->n_pages && i_page < n_pages;
		     (++i_page_in_range, ++i_page,
		      ++s->n_pages_refreshed_since_yield)) {
			if (s->n_pages_refreshed_since_yield == 1u << 18) {
				pthread_testcancel();
				/* pthread_yield(); */
				s->n_pages_refreshed_since_yield = 0;
			}

			refresh_page((char*)m->map + r->offset, i_page_in_range,
				     s->page_size);
		}
	}

	sigbus_fixup.active = false;

	return r;
}

static void *resident_keeper_refresh_proc(void *arg)
{
	struct resident_keeper_state *s = (struct resident_keeper_state *)arg;
	unsigned long avg_duration_ms; /* moving average */
	const unsigned int avg_window_len = 32;
	unsigned int n_since_last_printed;

	s->n_pages_refreshed_since_yield = 0;

	avg_duration_ms = 0;
	n_since_last_printed = 0;
	while (true) {
		struct timespec ts_start, ts_end;
		unsigned long duration_ms;

		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		s->i_page = 0;
		heap_for_each(&s->mappings, resident_keeper_refresh_one, s);
		if (s->fillup_mapping.map && s->i_page < __atomic_load_n(&s->active_n_pages, __ATOMIC_RELAXED))
			resident_keeper_refresh_one(&s->fillup_mapping, s);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);

		duration_ms = ts_diff_ms(&ts_start, &ts_end);
		avg_duration_ms = ((avg_duration_ms >> 1) +
				   (duration_ms << (avg_window_len - 1)));
		if (++n_since_last_printed == avg_window_len) {
			n_since_last_printed = 0;
			printf("Refreshing %lu resident pages takes %lums\n",
				__atomic_load_n(&s->active_n_pages, __ATOMIC_RELAXED),
			       ((avg_duration_ms +
				 ((1ul << avg_window_len) - 1) / 2) /
				((1ul << avg_window_len) - 1)));
		}
	}

	return NULL;
}

static int resident_keeper_warmup_one(void *p, void *arg)
{
	struct resident_mapping *m = (struct resident_mapping *)p;
	struct resident_keeper_state *s = (struct resident_keeper_state *)arg;
	size_t active_n_pages = s->active_n_pages;

	size_t n_pages = (s->target_n_pages - active_n_pages > m->n_pages ?
			  m->n_pages : s->target_n_pages - active_n_pages);
	size_t i_page;
	size_t i_resident_range;

	sigbus_fixup.active = true;
	if (setjmp(sigbus_fixup.jmp_buf)) {
		m->dead = true;
		m->n_pages = 0;
		pthread_testcancel();
		return 1;
	}

	for (i_resident_range = 0, i_page = 0;
	     (i_resident_range < m->n_resident_ranges &&
	      i_page < n_pages);
	     ++i_resident_range) {
		struct resident_range *r =
			resident_mapping_get_range(m, i_resident_range);
		size_t i_page_in_range;

		for (i_page_in_range = 0;
		     i_page_in_range < r->n_pages && i_page < n_pages;
		     (++i_page_in_range, ++i_page,
		      ++s->n_pages_warmed_up_since_yield)) {
			if (s->n_pages_warmed_up_since_yield == 1u << 8) {
				pthread_testcancel();
				/* pthread_yield(); */
				s->n_pages_warmed_up_since_yield = 0;
			}

			usleep(120);
			refresh_page((char*)m->map + r->offset, i_page_in_range,
				     s->page_size);

			active_n_pages++;
			__atomic_store_n(&s->active_n_pages, active_n_pages,
					__ATOMIC_RELEASE);
		}
	}

	sigbus_fixup.active = false;


	return (active_n_pages != s->target_n_pages ? 1 : 0);
}

static void resident_keeper_warmup(struct resident_keeper_state *s)
{
	struct timespec ts_start, ts_end;
	unsigned long duration_ms;

	s->n_pages_warmed_up_since_yield = 0;


	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	s->i_page = 0;
	heap_for_each(&s->mappings, resident_keeper_warmup_one, s);
	if (s->fillup_mapping.map && s->active_n_pages != s->target_n_pages)
		resident_keeper_warmup_one(&s->fillup_mapping, s);
	clock_gettime(CLOCK_MONOTONIC, &ts_end);

	duration_ms = ts_diff_ms(&ts_start, &ts_end);
	printf("Warming up resident pages took %lums\n", duration_ms);
}

int resident_keeper_start(struct resident_keeper_state *s)
{
	int r;

	/* pthread_attr_t attr; */
	/* struct sched_param sp = { .sched_priority = sched_get_priority_max(SCHED_FIFO) }; */

	/* r = pthread_attr_init(&attr); */
	/* if (r) { */
	/* 	errno = r; */
	/* 	return -1; */
	/* } */

	/* r = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED); */
	/* if (r) { */
	/* 	errno = r; */
	/* 	return -1; */
	/* } */

	/* r = pthread_attr_setschedpolicy(&attr, SCHED_FIFO); */
	/* if (r) { */
	/* 	errno = r; */
	/* 	return -1; */
	/* } */

	/* r = pthread_attr_setschedparam(&attr, &sp); */
	/* if (r) { */
	/* 	errno = r; */
	/* 	return -1; */
	/* } */

	/* r = pthread_create(&s->refresher, &attr, */
	/* 		   resident_keeper_refresh_proc, s); */
	r = pthread_create(&s->refresher, NULL,
			   resident_keeper_refresh_proc, s);
	if (r) {
		errno = r;
		return -1;
	}

	resident_keeper_warmup(s);

	return 0;
}

void resident_keeper_stop(struct resident_keeper_state *s)
{
	pthread_cancel(s->refresher);
	pthread_join(s->refresher, NULL);
}
