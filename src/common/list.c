/*****************************************************************************
 *  list.c
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Copyright (C) 2021 NVIDIA Corporation.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *
 *  This file is from LSD-Tools, the LLNL Software Development Toolbox.
 *
 *  LSD-Tools is free software; you can redistribute it and/or modify it under
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
 *  LSD-Tools is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with LSD-Tools; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *****************************************************************************
 *  Refer to "list.h" for documentation on public functions.
 *****************************************************************************/

#include "config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "log.h"
#include "macros.h"
#include "xassert.h"
#include "xmalloc.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
*/
strong_alias(list_create,	slurm_list_create);
strong_alias(list_destroy,	slurm_list_destroy);
strong_alias(list_is_empty,	slurm_list_is_empty);
strong_alias(list_count,	slurm_list_count);
strong_alias(list_shallow_copy,	slurm_list_shallow_copy);
strong_alias(list_append,	slurm_list_append);
strong_alias(list_append_list,	slurm_list_append_list);
strong_alias(list_transfer,	slurm_list_transfer);
strong_alias(list_transfer_max,	slurm_list_transfer_max);
strong_alias(list_transfer_unique,	slurm_list_transfer_unique);
strong_alias(list_push,		list_prepend);
strong_alias(list_push,		slurm_list_prepend);
strong_alias(list_find_first,	slurm_list_find_first);
strong_alias(list_find_first_ro, slurm_list_find_first_ro);
strong_alias(list_delete_all,	slurm_list_delete_all);
strong_alias(list_delete_first,	slurm_list_delete_first);
strong_alias(list_delete_ptr,	slurm_list_delete_ptr);
strong_alias(list_for_each,	slurm_list_for_each);
strong_alias(list_for_each_ro,	slurm_list_for_each_ro);
strong_alias(list_for_each_max,	slurm_list_for_each_max);
strong_alias(list_flush,	slurm_list_flush);
strong_alias(list_flush_max,	slurm_list_flush_max);
strong_alias(list_sort,		slurm_list_sort);
strong_alias(list_flip,		slurm_list_flip);
strong_alias(list_push,		slurm_list_push);
strong_alias(list_pop,		slurm_list_pop);
strong_alias(list_peek,		slurm_list_peek);
strong_alias(list_append,	list_enqueue);
strong_alias(list_append,	slurm_list_enqueue);
strong_alias(list_dequeue,	slurm_list_dequeue);
strong_alias(list_iterator_create,	slurm_list_iterator_create);
strong_alias(list_iterator_reset,	slurm_list_iterator_reset);
strong_alias(list_iterator_destroy,	slurm_list_iterator_destroy);
strong_alias(list_next,		slurm_list_next);
strong_alias(list_insert,	slurm_list_insert);
strong_alias(list_find,		slurm_list_find);
strong_alias(list_remove,	slurm_list_remove);
strong_alias(list_delete_item,	slurm_list_delete_item);

/***************
 *  Constants  *
 ***************/
#define LIST_MAGIC 0xDEADBEEF
#define LIST_ITR_MAGIC 0xDEADBEFF

/****************
 *  Data Types  *
 ****************/

typedef struct listNode {
	void                 *data;         /* node's data                       */
	struct listNode      *next;         /* next node in list                 */
} list_node_t;

struct listIterator {
	unsigned int          magic;        /* sentinel for asserting validity   */
	struct xlist         *list;         /* the list being iterated           */
	struct listNode      *pos;          /* the next node to be iterated      */
	struct listNode     **prev;         /* addr of 'next' ptr to prv It node */
	struct listIterator  *iNext;        /* iterator chain for list_destroy() */
};

struct xlist {
	unsigned int          magic;        /* sentinel for asserting validity   */
	struct listNode      *head;         /* head of the list                  */
	struct listNode     **tail;         /* addr of last node's 'next' ptr    */
	struct listIterator  *iNext;        /* iterator chain for list_destroy() */
	ListDelF              fDel;         /* function to delete node data      */
	int                   count;        /* number of nodes in list           */
	pthread_rwlock_t      mutex;        /* mutex to protect access to list   */
};

/****************
 *  Prototypes  *
 ****************/

static void _list_node_create(list_t *l, list_node_t **pp, void *x);
static void *_list_node_destroy(list_t *l, list_node_t **pp);
static void *_list_pop_locked(list_t *l);
static void *_list_find_first_locked(list_t *l, ListFindF f, void *key);

#ifndef NDEBUG
static int _list_mutex_is_locked(pthread_rwlock_t *mutex);
#endif

/***************
 *  Functions  *
 ***************/

/* list_create()
 */
extern list_t *list_create(ListDelF f)
{
	list_t *l = xmalloc(sizeof(*l));

	l->magic = LIST_MAGIC;
	l->head = NULL;
	l->tail = &l->head;
	l->iNext = NULL;
	l->fDel = f;
	l->count = 0;
	slurm_rwlock_init(&l->mutex);

	return l;
}

/* list_destroy()
 */
extern void list_destroy(list_t *l)
{
	list_itr_t *i, *iTmp;
	list_node_t *p, *pTmp;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	i = l->iNext;
	while (i) {
		xassert(i->magic == LIST_ITR_MAGIC);
		i->magic = ~LIST_ITR_MAGIC;
		iTmp = i->iNext;
		xfree(i);
		i = iTmp;
	}
	p = l->head;
	while (p) {
		pTmp = p->next;
		if (p->data && l->fDel)
			l->fDel(p->data);
		xfree(p);
		p = pTmp;
	}
	l->magic = ~LIST_MAGIC;
	slurm_rwlock_unlock(&l->mutex);
	slurm_rwlock_destroy(&l->mutex);
	xfree(l);
}

/* list_is_empty()
 */
extern int list_is_empty(list_t *l)
{
	int n;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_rdlock(&l->mutex);
	n = l->count;
	slurm_rwlock_unlock(&l->mutex);

	return (n == 0);
}

/*
 * Return the number of items in list [l].
 * If [l] is NULL, return 0.
 */
extern int list_count(list_t *l)
{
	int n;

	if (!l)
		return 0;

	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_rdlock(&l->mutex);
	n = l->count;
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

extern list_t *list_shallow_copy(list_t *l)
{
	list_t *m = list_create(NULL);

	(void) list_append_list(m, l);

	return m;
}

/* list_append()
 */
extern void list_append(list_t *l, void *x)
{
	xassert(l != NULL);
	xassert(x != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);
	_list_node_create(l, l->tail, x);
	slurm_rwlock_unlock(&l->mutex);
}

/* list_append_list()
 */
extern int list_append_list(list_t *l, list_t *sub)
{
	int n = 0;
	list_node_t *p;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	xassert(l->fDel == NULL);
	xassert(sub != NULL);
	xassert(sub->magic == LIST_MAGIC);

	slurm_rwlock_wrlock(&l->mutex);
	slurm_rwlock_wrlock(&sub->mutex);
	p = sub->head;
	while (p) {
		_list_node_create(l, l->tail, p->data);
		n++;
		p = p->next;
	}

	slurm_rwlock_unlock(&sub->mutex);
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

/*
 *  Pops off list [sub] to [l] with maximum number of entries.
 *  Set max = 0 to transfer all entries.
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [sub] may be returned empty, but not destroyed.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_transfer_max(list_t *l, list_t *sub, int max)
{
	void *v;
	int n = 0;

	xassert(l);
	xassert(sub);
	xassert(l->magic == LIST_MAGIC);
	xassert(sub->magic == LIST_MAGIC);
	xassert(l->fDel == sub->fDel);

	slurm_rwlock_wrlock(&l->mutex);
	slurm_rwlock_wrlock(&sub->mutex);
	while ((!max || n <= max) && (v = _list_pop_locked(sub))) {
		_list_node_create(l, l->tail, v);
		n++;
	}
	slurm_rwlock_unlock(&sub->mutex);
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

extern int list_transfer_match(list_t *l, list_t *sub, ListFindF f, void *key)
{
	list_node_t **pp;
	void *v;
	int n = 0;

	xassert(l);
	xassert(sub);
	xassert(l != sub);
	xassert(l->magic == LIST_MAGIC);
	xassert(sub->magic == LIST_MAGIC);
	xassert(l->fDel == sub->fDel);

	slurm_rwlock_wrlock(&l->mutex);
	slurm_rwlock_wrlock(&sub->mutex);

	pp = &l->head;
	while (*pp) {
		if (f((*pp)->data, key)) {
			if ((v = _list_node_destroy(l, pp)))
				n++;

			_list_node_create(sub, sub->tail, v);
		} else {
			pp = &(*pp)->next;
		}
	}

	slurm_rwlock_unlock(&sub->mutex);
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

/*
 *  Pops off list [sub] to [l].
 *  Set max = 0 to transfer all entries.
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [sub] will be returned empty, but not destroyed.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_transfer(list_t *l, list_t *sub)
{
	return list_transfer_max(l, sub, 0);
}

/*
 *  Pop off elements in list [sub] to [l], unless already in [l].
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [l] could contain repeated elements, but those aren't removed.
 *  Note: list [sub] will be returned with repeated elements or empty,
 *        but never destroyed.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_transfer_unique(list_t *l, ListFindF f, list_t *sub)
{
	list_node_t **pp;
	void *v;
	int n = 0;

	xassert(l);
	xassert(f);
	xassert(sub);
	xassert(l->magic == LIST_MAGIC);
	xassert(sub->magic == LIST_MAGIC);
	xassert(l->fDel == sub->fDel);

	slurm_rwlock_wrlock(&l->mutex);
	slurm_rwlock_wrlock(&sub->mutex);

	pp = &sub->head;
	while (*pp) {
		v = (*pp)->data;

		/* Is this element already in destination list? */
		if (!_list_find_first_locked(l, f, v)) {
			/* Not found: Transfer the element */
			_list_node_create(l, l->tail, v);
			/* Destroy increases index */
			_list_node_destroy(sub, pp);
			n++;
		} else
			/* Found: Just increase index */
			pp = &(*pp)->next;
	}

	slurm_rwlock_unlock(&sub->mutex);
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

static void *_list_find_first_locked(list_t *l, ListFindF f, void *key)
{
	for (list_node_t *p = l->head; p; p = p->next) {
		if (f(p->data, key))
			return p->data;
	}

	return NULL;
}

static void *_list_find_first_lock(list_t *l, ListFindF f, void *key,
				   bool write_lock)
{
	void *v = NULL;

	xassert(l != NULL);
	xassert(f != NULL);
	xassert(l->magic == LIST_MAGIC);
	if (write_lock)
		slurm_rwlock_wrlock(&l->mutex);
	else
		slurm_rwlock_rdlock(&l->mutex);

	v = _list_find_first_locked(l, f, key);

	slurm_rwlock_unlock(&l->mutex);

	return v;
}

/*
 * list_find_first()
 */
extern void *list_find_first(list_t *l, ListFindF f, void *key)
{
	return _list_find_first_lock(l, f, key, true);
}

/*
 * list_find_first_ro()
 * Same as list_find_first, but use a rdlock instead of wrlock
 */
extern void *list_find_first_ro(list_t *l, ListFindF f, void *key)
{
	return _list_find_first_lock(l, f, key, false);
}

/* list_remove_first()
 */
extern void *list_remove_first(list_t *l, ListFindF f, void *key)
{
	list_node_t **pp;
	void *v = NULL;

	xassert(l != NULL);
	xassert(f != NULL);
	xassert(key != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	pp = &l->head;
	while (*pp) {
		if (f((*pp)->data, key)) {
			v = _list_node_destroy(l, pp);
			break;
		} else {
			pp = &(*pp)->next;
		}
	}
	slurm_rwlock_unlock(&l->mutex);

	return v;
}

/* list_delete_all()
 */
extern int list_delete_all(list_t *l, ListFindF f, void *key)
{
	list_node_t **pp;
	void *v;
	int n = 0;

	xassert(l != NULL);
	xassert(f != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	pp = &l->head;
	while (*pp) {
		if (f((*pp)->data, key)) {
			if ((v = _list_node_destroy(l, pp))) {
				if (l->fDel)
					l->fDel(v);
				n++;
			}
		}
		else {
			pp = &(*pp)->next;
		}
	}
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

extern int list_delete_first(list_t *l, ListFindF f, void *key)
{
	list_node_t **pp;
	void *v;
	int n = 0;

	xassert(l != NULL);
	xassert(f != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	pp = &l->head;
	while (*pp) {
		int rc = f((*pp)->data, key);

		if (rc > 0) {
			if ((v = _list_node_destroy(l, pp))) {
				if (l->fDel)
					l->fDel(v);
			}
			n = 1;
			break;
		} else if (rc < 0) {
			n = -1;
			break;
		} else {
			pp = &(*pp)->next;
		}
	}
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

/* list_delete_ptr()
 */
extern int list_delete_ptr(list_t *l, void *key)
{
	list_node_t **pp;
	void *v;
	int n = 0;

	xassert(l);
	xassert(key);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	pp = &l->head;
	while (*pp) {
		if ((*pp)->data == key) {
			if ((v = _list_node_destroy(l, pp))) {
				if (l->fDel)
					l->fDel(v);
				n = 1;
				break;
			}
		} else
			pp = &(*pp)->next;
	}
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

/* list_for_each()
 */
extern int list_for_each(list_t *l, ListForF f, void *arg)
{
	int max = -1;	/* all values */
	return list_for_each_max(l, &max, f, arg, 1, true);
}

extern int list_for_each_ro(list_t *l, ListForF f, void *arg)
{
	int max = -1;	/* all values */
	return list_for_each_max(l, &max, f, arg, 1, false);
}

extern int list_for_each_nobreak(list_t *l, ListForF f, void *arg)
{
	int max = -1;	/* all values */
	return list_for_each_max(l, &max, f, arg, 0, true);
}

extern int list_for_each_max(list_t *l, int *max, ListForF f, void *arg,
			     int break_on_fail, int write_lock)
{
	list_node_t *p;
	int n = 0;
	bool failed = false;

	xassert(l != NULL);
	xassert(f != NULL);
	xassert(l->magic == LIST_MAGIC);

	if (write_lock)
		slurm_rwlock_wrlock(&l->mutex);
	else
		slurm_rwlock_rdlock(&l->mutex);

	for (p = l->head; (*max == -1 || n < *max) && p; p = p->next) {
		n++;
		if (f(p->data, arg) < 0) {
			failed = true;
			if (break_on_fail)
				break;
		}
	}
	*max = l->count - n;
	slurm_rwlock_unlock(&l->mutex);

	if (failed)
		n = -n;

	return n;
}

extern int list_flush(list_t *l)
{
	return list_flush_max(l, -1);
}

extern int list_flush_max(list_t *l, int max)
{
	list_node_t **pp;
	void *v;
	int n = 0;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	pp = &l->head;
	for (int i = 0; (max < 0 || i < max) && *pp; i++) {
		if ((v = _list_node_destroy(l, pp))) {
			if (l->fDel)
				l->fDel(v);
			n++;
		}
	}
	slurm_rwlock_unlock(&l->mutex);

	return n;
}

/* list_push()
 */
extern void list_push(list_t *l, void *x)
{
	xassert(l != NULL);
	xassert(x != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);
	_list_node_create(l, &l->head, x);
	slurm_rwlock_unlock(&l->mutex);
}

/*
 * Handle translation between ListCmpF and signature required by qsort.
 * glibc has this as __compar_fn_t, but that's non-standard so we define
 * our own instead.
 */
typedef int (*ConstListCmpF) (__const void *, __const void *);

/* list_sort()
 *
 * This function uses the libC qsort().
 *
 */
extern void list_sort(list_t *l, ListCmpF f)
{
	char **v;
	int n;
	int lsize;
	void *e;
	list_itr_t *i;

	xassert(l != NULL);
	xassert(f != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	if (l->count <= 1) {
		slurm_rwlock_unlock(&l->mutex);
		return;
	}

	lsize = l->count;
	v = xmalloc(lsize * sizeof(char *));

	n = 0;
	while ((e = _list_pop_locked(l))) {
		v[n] = e;
		++n;
	}

	qsort(v, n, sizeof(char *), (ConstListCmpF)f);

	for (n = 0; n < lsize; n++) {
		 _list_node_create(l, l->tail, v[n]);
	}

	xfree(v);

	/* Reset all iterators on the list to point
	 * to the head of the list.
	 */
	for (i = l->iNext; i; i = i->iNext) {
		xassert(i->magic == LIST_ITR_MAGIC);
		i->pos = i->list->head;
		i->prev = &i->list->head;
	}

	slurm_rwlock_unlock(&l->mutex);
}

/*
 * list_flip - not called list_reverse due to collision with MariaDB
 */
extern void list_flip(list_t *l)
{
	list_node_t *old_head, *prev = NULL, *curr, *next = NULL;
	list_itr_t *i;

	xassert(l);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	if (l->count <= 1) {
		slurm_rwlock_unlock(&l->mutex);
		return;
	}

	old_head = curr = l->head;
	while (curr) {
		next = curr->next;
		curr->next = prev;
		prev = curr;
		curr = next;
	}
	l->head = prev;
	l->tail = &old_head->next;

	/*
	 * Reset all iterators on the list to point
	 * to the head of the list.
	 */
	for (i = l->iNext; i; i = i->iNext) {
		xassert(i->magic == LIST_ITR_MAGIC);
		i->pos = i->list->head;
		i->prev = &i->list->head;
	}

	slurm_rwlock_unlock(&l->mutex);
}

/* list_pop()
 */
extern void *list_pop(list_t *l)
{
	void *v;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	v = _list_pop_locked(l);
	slurm_rwlock_unlock(&l->mutex);

	return v;
}

/* list_peek()
 */
extern void *list_peek(list_t *l)
{
	void *v;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_rdlock(&l->mutex);

	v = (l->head) ? l->head->data : NULL;
	slurm_rwlock_unlock(&l->mutex);

	return v;
}

/* list_enqueue() is aliased to list_append() */

/* list_dequeue()
 */
extern void *list_dequeue(list_t *l)
{
	void *v;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	v = _list_node_destroy(l, &l->head);
	slurm_rwlock_unlock(&l->mutex);

	return v;
}

/* list_iterator_create()
 */
extern list_itr_t *list_iterator_create(list_t *l)
{
	list_itr_t *i = xmalloc(sizeof(*i));

	xassert(l != NULL);

	i->magic = LIST_ITR_MAGIC;
	i->list = l;
	xassert(l->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&l->mutex);

	i->pos = l->head;
	i->prev = &l->head;
	i->iNext = l->iNext;
	l->iNext = i;

	slurm_rwlock_unlock(&l->mutex);

	return i;
}

/* list_iterator_reset()
 */
extern void list_iterator_reset(list_itr_t *i)
{
	xassert(i != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);
	xassert(i->list->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&i->list->mutex);

	i->pos = i->list->head;
	i->prev = &i->list->head;

	slurm_rwlock_unlock(&i->list->mutex);
}

/* list_iterator_destroy()
 */
extern void list_iterator_destroy(list_itr_t *i)
{
	list_itr_t **pi;

	xassert(i != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);
	xassert(i->list->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&i->list->mutex);

	for (pi = &i->list->iNext; *pi; pi = &(*pi)->iNext) {
		xassert((*pi)->magic == LIST_ITR_MAGIC);
		if (*pi == i) {
			*pi = (*pi)->iNext;
			break;
		}
	}
	slurm_rwlock_unlock(&i->list->mutex);

	i->magic = ~LIST_ITR_MAGIC;
	xfree(i);
}

static void *_list_next_locked(list_itr_t *i)
{
	list_node_t *p;

	if ((p = i->pos))
		i->pos = p->next;
	if (*i->prev != p)
		i->prev = &(*i->prev)->next;

	return (p ? p->data : NULL);
}

/* list_next()
 */
extern void *list_next(list_itr_t *i)
{
	void *rc;

	xassert(i != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);
	xassert(i->list->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&i->list->mutex);

	rc = _list_next_locked(i);

	slurm_rwlock_unlock(&i->list->mutex);

	return rc;
}

/* list_peek_next()
 */
extern void *list_peek_next(list_itr_t *i)
{
	list_node_t *p;

	xassert(i != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);
	xassert(i->list->magic == LIST_MAGIC);
	slurm_rwlock_rdlock(&i->list->mutex);

	p = i->pos;

	slurm_rwlock_unlock(&i->list->mutex);

	return (p ? p->data : NULL);
}

/* list_insert()
 */
extern void list_insert(list_itr_t *i, void *x)
{
	xassert(i != NULL);
	xassert(x != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);
	xassert(i->list->magic == LIST_MAGIC);

	slurm_rwlock_wrlock(&i->list->mutex);
	_list_node_create(i->list, i->prev, x);
	slurm_rwlock_unlock(&i->list->mutex);
}

/* list_find()
 */
extern void *list_find(list_itr_t *i, ListFindF f, void *key)
{
	void *v;

	xassert(i != NULL);
	xassert(f != NULL);
	xassert(key != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);

	slurm_rwlock_wrlock(&i->list->mutex);
	xassert(i->list->magic == LIST_MAGIC);

	while ((v = _list_next_locked(i)) && !f(v, key)) {;}

	slurm_rwlock_unlock(&i->list->mutex);

	return v;
}

/* list_remove()
 */
extern void *list_remove(list_itr_t *i)
{
	void *v = NULL;

	xassert(i != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);
	xassert(i->list->magic == LIST_MAGIC);
	slurm_rwlock_wrlock(&i->list->mutex);

	if (*i->prev != i->pos)
		v = _list_node_destroy(i->list, i->prev);
	slurm_rwlock_unlock(&i->list->mutex);

	return v;
}

/* list_delete_item()
 */
extern int list_delete_item(list_itr_t *i)
{
	void *v;

	xassert(i != NULL);
	xassert(i->magic == LIST_ITR_MAGIC);

	if ((v = list_remove(i))) {
		if (i->list->fDel)
			i->list->fDel(v);
		return 1;
	}

	return 0;
}

/*
 * Inserts data pointed to by [x] into list [l] after [pp],
 * the address of the previous node's "next" ptr.
 * Returns a ptr to data [x], or NULL if insertion fails.
 * This routine assumes the list is already locked upon entry.
 */
static void _list_node_create(list_t *l, list_node_t **pp, void *x)
{
	list_node_t *p;
	list_itr_t *i;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	xassert(_list_mutex_is_locked(&l->mutex));
	xassert(pp != NULL);
	xassert(x != NULL);

	p = xmalloc(sizeof(list_node_t));

	p->data = x;
	if (!(p->next = *pp))
		l->tail = &p->next;
	*pp = p;
	l->count++;

	for (i = l->iNext; i; i = i->iNext) {
		xassert(i->magic == LIST_ITR_MAGIC);
		if (i->prev == pp)
			i->prev = &p->next;
		else if (i->pos == p->next)
			i->pos = p;
		xassert((i->pos == *i->prev) ||
		       ((*i->prev) && (i->pos == (*i->prev)->next)));
	}
}

/*
 * Removes the node pointed to by [*pp] from from list [l],
 * where [pp] is the address of the previous node's "next" ptr.
 * Returns the data ptr associated with list item being removed,
 * or NULL if [*pp] points to the NULL element.
 * This routine assumes the list is already locked upon entry.
 */
static void *_list_node_destroy(list_t *l, list_node_t **pp)
{
	void *v;
	list_node_t *p;
	list_itr_t *i;

	xassert(l != NULL);
	xassert(l->magic == LIST_MAGIC);
	xassert(_list_mutex_is_locked(&l->mutex));
	xassert(pp != NULL);

	if (!(p = *pp))
		return NULL;

	v = p->data;
	if (!(*pp = p->next))
		l->tail = pp;
	l->count--;

	for (i = l->iNext; i; i = i->iNext) {
		xassert(i->magic == LIST_ITR_MAGIC);
		if (i->pos == p)
			i->pos = p->next, i->prev = pp;
		else if (i->prev == &p->next)
			i->prev = pp;
		xassert((i->pos == *i->prev) ||
		       ((*i->prev) && (i->pos == (*i->prev)->next)));
	}
	xfree(p);

	return v;
}

#ifndef NDEBUG
static int _list_mutex_is_locked(pthread_rwlock_t *mutex)
{
/*  Returns true if the mutex is locked; o/w, returns false.
 */
	int rc;

	xassert(mutex != NULL);
	rc = slurm_rwlock_trywrlock(mutex);
	return(rc == EBUSY ? 1 : 0);
}
#endif /* !NDEBUG */

/* _list_pop_locked
 *
 * Pop an item from the list assuming the
 * the list is already locked.
 */
static void *_list_pop_locked(list_t *l)
{
	void *v;

	v = _list_node_destroy(l, &l->head);

	return v;
}
