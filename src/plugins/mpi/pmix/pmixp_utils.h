/*****************************************************************************\
 ** pmix_utils.h - Various PMIx utility functions
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 \*****************************************************************************/

#ifndef PMIXP_UTILS_H
#define PMIXP_UTILS_H

#include "pmixp_common.h"

extern int pmixp_count_digits_base10(uint32_t val);

void pmixp_free_buf(void *x);
int pmixp_usock_create_srv(char *path);
size_t pmixp_read_buf(int fd, void *buf, size_t count, int *shutdown,
		      bool blocking);
size_t pmixp_write_buf(int fd, void *buf, size_t count, int *shutdown,
		       bool blocking);
size_t pmixp_writev_buf(int sd, struct iovec *iov, size_t iovcnt,
			size_t offset, int *shutdown);

int pmixp_fd_set_nodelay(int fd);
bool pmixp_fd_read_ready(int fd, int *shutdown);
bool pmixp_fd_write_ready(int fd, int *shutdown);
int pmixp_srun_send(slurm_addr_t *addr, uint32_t len, char *data);
int pmixp_stepd_send(const char *nodelist, const char *address,
		     const char *data, uint32_t len, unsigned int start_delay,
		     unsigned int retry_cnt, int silent);
int pmixp_p2p_send(const char *nodename, const char *address, const char *data,
		   uint32_t len, unsigned int start_delay,
		   unsigned int retry_cnt, int silent);
int pmixp_rmdir_recursively(char *path);
int pmixp_fixrights(char *path, uid_t uid, mode_t mode);
int pmixp_mkdir(char *path, mode_t rights);

/* lightweight pmix list of pointers */
#define PMIXP_LIST_DEBUG 0
#define PMIXP_LIST_VAL(elem) (elem->data)

typedef struct pmixp_list_elem_s {
#ifndef NDEBUG
	void *lptr;
#endif
	void *data;
	struct pmixp_list_elem_s *next, *prev;
} pmixp_list_elem_t;

typedef struct pmixp_list_s {
	pmixp_list_elem_t *head, *tail;
	size_t count;
} pmixp_list_t;

/* PMIx list of pointers with element reuse */
typedef struct pmixp_rlist_s {
	pmixp_list_t list;
	pmixp_list_t *src_list;
	size_t pre_alloc;
} pmixp_rlist_t;

static inline pmixp_list_elem_t *pmixp_list_elem_new(void)
{
	return xmalloc(sizeof(pmixp_list_elem_t));
}

static inline void pmixp_list_elem_free(pmixp_list_elem_t *elem)
{
	xfree(elem);
}

static inline bool pmixp_list_empty(pmixp_list_t *l)
{
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
#endif
	return !(l->count);
}

static inline size_t pmixp_list_count(pmixp_list_t *l)
{
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
#endif
	return l->count;
}

static inline void pmixp_list_init_pre(pmixp_list_t *l,
				       pmixp_list_elem_t *h,
				       pmixp_list_elem_t *t)
{
	xassert(l && h && t);
	l->head = h;
	l->tail = t;

	l->head->data = NULL;
	l->head->next = l->tail;
	l->head->prev = NULL;

	l->tail->data = NULL;
	l->tail->prev = l->head;
	l->tail->next = NULL;

	l->count = 0;
}

static inline void pmixp_list_fini_pre(pmixp_list_t *l,
				       pmixp_list_elem_t **h,
				       pmixp_list_elem_t **t)
{
	/* list supposed to be empty */
	xassert(l->head && l->tail);
	xassert(l->head->next == l->tail);
	xassert(l->head == l->tail->prev);
	xassert(!l->count);

	*h = l->head;
	*t = l->tail;

	l->head = NULL;
	l->tail = NULL;
	l->count = 0;
}


static inline void pmixp_list_init(pmixp_list_t *l)
{
	pmixp_list_init_pre(l, pmixp_list_elem_new(), pmixp_list_elem_new());
}

static inline void pmixp_list_fini(pmixp_list_t *l)
{
	pmixp_list_elem_t *elem1, *elem2;
	pmixp_list_fini_pre(l, &elem1, &elem2);
	pmixp_list_elem_free(elem1);
	pmixp_list_elem_free(elem2);
}

static inline void pmixp_list_enq(pmixp_list_t *l, pmixp_list_elem_t *elem)
{
#if PMIXP_LIST_DEBUG
	xassert(elem);
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
#ifndef NDEBUG
	elem->lptr = l;
#endif /* NDEBUG */
#endif /* PMIXP_LIST_DEBUG */

	/* setup connection to the previous elem */
	elem->prev = l->tail->prev;
	elem->prev->next = elem;

	/* setup connection to the dummy tail elem */
	elem->next = l->tail;
	l->tail->prev = elem;

	/* reduce element count */
	l->count++;
}

static inline pmixp_list_elem_t *pmixp_list_deq(pmixp_list_t *l)
{
	pmixp_list_elem_t *ret;
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
	xassert (!pmixp_list_empty(l));
#endif
	/* user is responsible to ensure that
	 * list is not empty */
	ret = l->head->next;

#if PMIXP_LIST_DEBUG
	xassert(ret->lptr == l);
#endif

	/* reconnect the list, removing element */
	l->head->next = ret->next;
	ret->next->prev = l->head;

	/* reduce element count */
	l->count--;

	return ret;
}

static inline void pmixp_list_push(pmixp_list_t *l, pmixp_list_elem_t *elem)
{
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
#ifndef NDEBUG
	elem->lptr = l;
#endif
#endif /* PMIXP_LIST_DEBUG */

	/* setup connection with ex-first element */
	elem->next = l->head->next;
	elem->next->prev = elem;

	/* setup connection with dummy head element */
	l->head->next = elem;
	elem->prev = l->head;

	/* reduce element count */
	l->count++;
}

static inline pmixp_list_elem_t *pmixp_list_pop(pmixp_list_t *l)
{
	pmixp_list_elem_t *ret = NULL;

#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
	xassert (!pmixp_list_empty(l));
#endif

	/* user is responsible to ensure that
	 * list is not empty */
	ret = l->tail->prev;

#if PMIXP_LIST_DEBUG
	xassert(ret->lptr == l);
#endif

	l->tail->prev = ret->prev;
	ret->prev->next = l->tail;
	l->count--;

	return ret;
}

static inline pmixp_list_elem_t *pmixp_list_rem(
	pmixp_list_t *l, pmixp_list_elem_t *elem)
{
	pmixp_list_elem_t *next;

#if PMIXP_LIST_DEBUG
	xassert(elem && l);
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
	xassert(elem->next && elem->prev);
	xassert((elem != l->head) && (elem != l->tail));
	xassert(elem->lptr == l);
#endif

	next = elem->next;
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	/* protect the list */
	elem->next = elem->prev = NULL;

	l->count--;
	return next;
}

static inline pmixp_list_elem_t *pmixp_list_begin(pmixp_list_t *l)
{
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
#endif
	return l->head->next;
}

static inline pmixp_list_elem_t *pmixp_list_end(pmixp_list_t *l)
{
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
#endif
	return l->tail;
}

static inline pmixp_list_elem_t *pmixp_list_next(
	pmixp_list_t *l, pmixp_list_elem_t *cur)
{
#if PMIXP_LIST_DEBUG
	xassert(l->head && l->tail);
	xassert(!l->head->data && !l->tail->data);
	xassert(!l->tail->next && !l->head->prev);
	xassert(cur);
	xassert(cur->lptr == l);
#endif
	return cur->next;
}

static inline pmixp_list_elem_t *__pmixp_rlist_get_free(
	pmixp_list_t *l, size_t pre_alloc)
{
	if (pmixp_list_empty(l)) {
		/* add l->pre_alloc elements to the source list */
		int i;
		for (i=0; i<pre_alloc-1; i++) {
			pmixp_list_enq(l, pmixp_list_elem_new());
		}
	}
	return pmixp_list_deq(l);
}

static inline void pmixp_rlist_init(
	pmixp_rlist_t *l, pmixp_list_t *elem_src, size_t pre_alloc)
{
	pmixp_list_elem_t *h, *t;
	xassert(l && elem_src && pre_alloc);
	l->src_list = elem_src;
	l->pre_alloc = pre_alloc;

	/* initialize local list */
	h = __pmixp_rlist_get_free(elem_src, pre_alloc);
	t = __pmixp_rlist_get_free(elem_src, pre_alloc);
	xassert(h && t);
	pmixp_list_init_pre(&l->list,h, t);
}

static inline void pmixp_rlist_fini(pmixp_rlist_t *l)
{
	pmixp_list_elem_t *h, *t;
	xassert(l);
	pmixp_list_fini_pre(&l->list, &h, &t);
	xassert(h && t);
	pmixp_list_enq(l->src_list, h);
	pmixp_list_enq(l->src_list, t);
}

static inline bool pmixp_rlist_empty(pmixp_rlist_t *l)
{
	return pmixp_list_empty(&l->list);
}

static inline size_t pmixp_rlist_count(pmixp_rlist_t *l)
{
	return pmixp_list_count(&l->list);
}

static inline void pmixp_rlist_enq(pmixp_rlist_t *l, void *ptr)
{
	pmixp_list_elem_t *elem = NULL;
	elem = __pmixp_rlist_get_free(l->src_list, l->pre_alloc);
	PMIXP_LIST_VAL(elem) = ptr;
	pmixp_list_enq(&l->list, elem);
}

static inline void *pmixp_rlist_deq(pmixp_rlist_t *l)
{
	pmixp_list_elem_t *elem = NULL;
	void *val = NULL;

	/* user is responsible to ensure that
	 * list is not empty
	 */
	elem = pmixp_list_deq(&l->list);
	val = PMIXP_LIST_VAL(elem);
	pmixp_list_enq(l->src_list, elem);
	return val;
}

static inline void pmixp_rlist_push(pmixp_rlist_t *l, void *ptr)
{
	pmixp_list_elem_t *elem = NULL;
	elem = __pmixp_rlist_get_free(l->src_list, l->pre_alloc);
	PMIXP_LIST_VAL(elem) = ptr;
	pmixp_list_push(&l->list, elem);
}

static inline void *pmixp_rlist_pop(pmixp_rlist_t *l)
{
	pmixp_list_elem_t *elem = NULL;
	void *val = NULL;
#if PMIXP_LIST_DEBUG
	xassert(l);
#endif
	/* user is responsible to ensure that
	 * list is not empty
	 */
	elem = pmixp_list_pop(&l->list);
	val = PMIXP_LIST_VAL(elem);
	pmixp_list_enq(l->src_list, elem);
	return val;
}

static inline pmixp_list_elem_t *pmixp_rlist_begin(pmixp_rlist_t *l)
{
#if PMIXP_LIST_DEBUG
	xassert(l);
#endif
	return pmixp_list_begin(&l->list);
}

static inline pmixp_list_elem_t *pmixp_rlist_end(pmixp_rlist_t *l)
{
#if PMIXP_LIST_DEBUG
	xassert(l);
#endif
	return pmixp_list_end(&l->list);
}

static inline pmixp_list_elem_t *pmixp_rlist_next(
	pmixp_rlist_t *l, pmixp_list_elem_t *cur)
{
#if PMIXP_LIST_DEBUG
	xassert(l && cur);
#endif
	return pmixp_list_next(&l->list, cur);
}

static inline pmixp_list_elem_t *pmixp_rlist_rem(
	pmixp_rlist_t *l, pmixp_list_elem_t *elem)
{
	pmixp_list_elem_t *ret = NULL;
#if PMIXP_LIST_DEBUG
	xassert(l && elem);
#endif
	ret = pmixp_list_rem(&l->list, elem);
	pmixp_list_enq(l->src_list, elem);
	return ret;
}

#endif /* PMIXP_UTILS_H*/
