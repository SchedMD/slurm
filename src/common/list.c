/*****************************************************************************
 *  list.c
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
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

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "log.h"
#include "macros.h"
#include "xmalloc.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
*/
strong_alias(list_create,	slurm_list_create);
strong_alias(list_destroy,	slurm_list_destroy);
strong_alias(list_is_empty,	slurm_list_is_empty);
strong_alias(list_count,	slurm_list_count);
strong_alias(list_append,	slurm_list_append);
strong_alias(list_append_list,	slurm_list_append_list);
strong_alias(list_transfer,	slurm_list_transfer);
strong_alias(list_prepend,	slurm_list_prepend);
strong_alias(list_find_first,	slurm_list_find_first);
strong_alias(list_delete_all,	slurm_list_delete_all);
strong_alias(list_for_each,	slurm_list_for_each);
strong_alias(list_flush,	slurm_list_flush);
strong_alias(list_sort,		slurm_list_sort);
strong_alias(list_push,		slurm_list_push);
strong_alias(list_pop,		slurm_list_pop);
strong_alias(list_peek,		slurm_list_peek);
strong_alias(list_enqueue,	slurm_list_enqueue);
strong_alias(list_dequeue,	slurm_list_dequeue);
strong_alias(list_iterator_create,	slurm_list_iterator_create);
strong_alias(list_iterator_reset,	slurm_list_iterator_reset);
strong_alias(list_iterator_destroy,	slurm_list_iterator_destroy);
strong_alias(list_next,		slurm_list_next);
strong_alias(list_insert,	slurm_list_insert);
strong_alias(list_find,		slurm_list_find);
strong_alias(list_remove,	slurm_list_remove);
strong_alias(list_delete_item,	slurm_list_delete_item);
strong_alias(list_install_fork_handlers, slurm_list_install_fork_handlers);


/***************
 *  Constants  *
 ***************/

/**************************************************************************\
 * To test for memory leaks associated with the use of list functions (not
 * necessarily within the list module), set MEMORY_LEAK_DEBUG to 1 using
 * "configure --enable-memory-leak" then execute
 * > valgrind --tool=memcheck --leak-check=yes --num-callers=6
 *    --leak-resolution=med [slurmctld | slurmd] -D
 *
 * Do not leave MEMORY_LEAK_DEBUG set for production use
 *
 * When MEMORY_LEAK_DEBUG is set to 1, the cache is disabled. Each memory
 * request will be satisified with a separate xmalloc request. When the
 * memory is no longer required, it is immeditately freed. This means
 * valgrind can identify where exactly any leak associated with the use
 * of the list functions originates.
\**************************************************************************/
#ifdef MEMORY_LEAK_DEBUG
#  define LIST_ALLOC 1
#else
#  define LIST_ALLOC 128
#endif
#define LIST_MAGIC 0xDEADBEEF


/****************
 *  Data Types  *
 ****************/

struct listNode {
	void                 *data;         /* node's data                       */
	struct listNode      *next;         /* next node in list                 */
};

struct listIterator {
	struct xlist         *list;         /* the list being iterated           */
	struct listNode      *pos;          /* the next node to be iterated      */
	struct listNode     **prev;         /* addr of 'next' ptr to prv It node */
	struct listIterator  *iNext;        /* iterator chain for list_destroy() */
#ifndef NDEBUG
	unsigned int          magic;        /* sentinel for asserting validity   */
#endif /* !NDEBUG */
};

struct xlist {
	struct listNode      *head;         /* head of the list                  */
	struct listNode     **tail;         /* addr of last node's 'next' ptr    */
	struct listIterator  *iNext;        /* iterator chain for list_destroy() */
	ListDelF              fDel;         /* function to delete node data      */
	int                   count;        /* number of nodes in list           */
	pthread_mutex_t       mutex;        /* mutex to protect access to list   */
#ifndef NDEBUG
	unsigned int          magic;        /* sentinel for asserting validity   */
#endif /* !NDEBUG */
};

typedef struct listNode * ListNode;


/****************
 *  Prototypes  *
 ****************/

static void * list_node_create (List l, ListNode *pp, void *x);
static void * list_node_destroy (List l, ListNode *pp);
static List list_alloc (void);
static void list_free (List l);
static ListNode list_node_alloc (void);
static void list_node_free (ListNode p);
static ListIterator list_iterator_alloc (void);
static void list_iterator_free (ListIterator i);
static void * list_alloc_aux (int size, void *pfreelist);
static void list_free_aux (void *x, void *pfreelist);
static void *_list_pop_locked(List l);
static void *_list_append_locked(List l, void *x);

#ifndef NDEBUG
static int _list_mutex_is_locked (pthread_mutex_t *mutex);
#endif

/***************
 *  Variables  *
 ***************/

static List list_free_lists = NULL;
static ListNode list_free_nodes = NULL;
static ListIterator list_free_iterators = NULL;

static pthread_mutex_t list_free_lock = PTHREAD_MUTEX_INITIALIZER;

/***************
 *  Functions  *
 ***************/

/* list_create()
 */
List
list_create (ListDelF f)
{
	List l = list_alloc();

	l->head = NULL;
	l->tail = &l->head;
	l->iNext = NULL;
	l->fDel = f;
	l->count = 0;
	slurm_mutex_init(&l->mutex);
	assert((l->magic = LIST_MAGIC));      /* set magic via assert abuse */

	return l;
}

/* list_destroy()
 */
void
list_destroy (List l)
{
	ListIterator i, iTmp;
	ListNode p, pTmp;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	i = l->iNext;
	while (i) {
		assert(i->magic == LIST_MAGIC);
		iTmp = i->iNext;
		assert((i->magic = ~LIST_MAGIC)); /* clear magic via assert abuse */
		list_iterator_free(i);
		i = iTmp;
	}
	p = l->head;
	while (p) {
		pTmp = p->next;
		if (p->data && l->fDel)
			l->fDel(p->data);
		list_node_free(p);
		p = pTmp;
	}
	assert((l->magic = ~LIST_MAGIC));     /* clear magic via assert abuse */
	slurm_mutex_unlock(&l->mutex);
	slurm_mutex_destroy(&l->mutex);
	list_free(l);
}

/* list_is_empty()
 */
int
list_is_empty (List l)
{
	int n;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);
	n = l->count;
	slurm_mutex_unlock(&l->mutex);

	return (n == 0);
}

/* list_count()
 */
int
list_count (List l)
{
	int n;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);
	n = l->count;
	slurm_mutex_unlock(&l->mutex);

	return n;
}

/* list_append()
 */
void *
list_append (List l, void *x)
{
	void *v;

	assert(l != NULL);
	assert(x != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);
	v = _list_append_locked(l, x);
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_append_list()
 */
int
list_append_list (List l, List sub)
{
	ListIterator itr;
	void *v;
	int n = 0;

	assert(l != NULL);
	assert(l->fDel == NULL);
	assert(sub != NULL);
	itr = list_iterator_create(sub);
	while((v = list_next(itr))) {
		if (list_append(l, v))
			n++;
		else
			break;
	}
	list_iterator_destroy(itr);

	return n;
}

/* list_transfer()
 */
int
list_transfer (List l, List sub)
{
	void *v;
	int n = 0;

	assert(l != NULL);
	assert(sub != NULL);
	assert(l->fDel == sub->fDel);
	while((v = list_pop(sub))) {
		if (list_append(l, v))
			n++;
		else {
			if (l->fDel)
				l->fDel(v);
			break;
		}
	}

	return n;
}

/* list_prepend()
 */
void *
list_prepend (List l, void *x)
{
	void *v;

	assert(l != NULL);
	assert(x != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	v = list_node_create(l, &l->head, x);
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_find_first()
 */
void *
list_find_first (List l, ListFindF f, void *key)
{
	ListNode p;
	void *v = NULL;

	assert(l != NULL);
	assert(f != NULL);
	assert(key != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	for (p = l->head; p; p = p->next) {
		if (f(p->data, key)) {
			v = p->data;
			break;
		}
	}
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_remove_first()
 */
void *
list_remove_first (List l, ListFindF f, void *key)
{
	ListNode *pp;
	void *v = NULL;

	assert(l != NULL);
	assert(f != NULL);
	assert(key != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	pp = &l->head;
	while (*pp) {
		if (f((*pp)->data, key)) {
			v = list_node_destroy(l, pp);
			break;
		} else {
			pp = &(*pp)->next;
		}
	}
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_delete_all()
 */
int
list_delete_all (List l, ListFindF f, void *key)
{
	ListNode *pp;
	void *v;
	int n = 0;

	assert(l != NULL);
	assert(f != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	pp = &l->head;
	while (*pp) {
		if (f((*pp)->data, key)) {
			if ((v = list_node_destroy(l, pp))) {
				if (l->fDel)
					l->fDel(v);
				n++;
			}
		}
		else {
			pp = &(*pp)->next;
		}
	}
	slurm_mutex_unlock(&l->mutex);

	return n;
}

/* list_for_each()
 */
int
list_for_each (List l, ListForF f, void *arg)
{
	ListNode p;
	int n = 0;

	assert(l != NULL);
	assert(f != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	for (p = l->head; p; p = p->next) {
		n++;
		if (f(p->data, arg) < 0) {
			n = -n;
			break;
		}
	}
	slurm_mutex_unlock(&l->mutex);

	return n;
}

/* list_flush()
 */
int
list_flush (List l)
{
	ListNode *pp;
	void *v;
	int n = 0;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	pp = &l->head;
	while (*pp) {
		if ((v = list_node_destroy(l, pp))) {
			if (l->fDel)
				l->fDel(v);
			n++;
		}
	}
	slurm_mutex_unlock(&l->mutex);

	return n;
}

/* list_push()
 */
void *
list_push (List l, void *x)
{
	void *v;

	assert(l != NULL);
	assert(x != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	v = list_node_create(l, &l->head, x);
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_sort()
 *
 * This function uses the libC qsort().
 *
 */
void
list_sort(List l, ListCmpF f)
{
	char **v;
	int n;
	int lsize;
	void *e;
	ListIterator i;

	assert(l != NULL);
	assert(f != NULL);
	assert(l->magic == LIST_MAGIC);
	slurm_mutex_lock(&l->mutex);

	if (l->count <= 1) {
		slurm_mutex_unlock(&l->mutex);
		return;
	}

	lsize = l->count;
	v = xmalloc(lsize * sizeof(char *));

	n = 0;
	while ((e = _list_pop_locked(l))) {
		v[n] = e;
		++n;
	}

	qsort(v, n, sizeof(char *), (__compar_fn_t)f);

	for (n = 0; n < lsize; n++) {
		_list_append_locked(l, v[n]);
	}

	xfree(v);

	/* Reset all iterators on the list to point
	 * to the head of the list.
	 */
	for (i = l->iNext; i; i = i->iNext) {
		assert(i->magic == LIST_MAGIC);
		i->pos = i->list->head;
		i->prev = &i->list->head;
	}

	slurm_mutex_unlock(&l->mutex);
}

/* list_pop()
 */
void *
list_pop (List l)
{
	void *v;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	v = _list_pop_locked(l);
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_peek()
 */
void *
list_peek (List l)
{
	void *v;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	v = (l->head) ? l->head->data : NULL;
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_enqueue()
 */
void *
list_enqueue (List l, void *x)
{
	void *v;

	assert(l != NULL);
	assert(x != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	v = list_node_create(l, l->tail, x);
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_dequeue()
 */
void *
list_dequeue (List l)
{
	void *v;

	assert(l != NULL);
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	v = list_node_destroy(l, &l->head);
	slurm_mutex_unlock(&l->mutex);

	return v;
}

/* list_iterator_create()
 */
ListIterator
list_iterator_create (List l)
{
	ListIterator i;

	assert(l != NULL);
	i = list_iterator_alloc();

	i->list = l;
	slurm_mutex_lock(&l->mutex);
	assert(l->magic == LIST_MAGIC);

	i->pos = l->head;
	i->prev = &l->head;
	i->iNext = l->iNext;
	l->iNext = i;
	assert((i->magic = LIST_MAGIC));      /* set magic via assert abuse */

	slurm_mutex_unlock(&l->mutex);

	return i;
}

/* list_iterator_reset()
 */
void
list_iterator_reset (ListIterator i)
{
	assert(i != NULL);
	assert(i->magic == LIST_MAGIC);
	slurm_mutex_lock(&i->list->mutex);
	assert(i->list->magic == LIST_MAGIC);

	i->pos = i->list->head;
	i->prev = &i->list->head;

	slurm_mutex_unlock(&i->list->mutex);
}

/* list_iterator_destroy()
 */
void
list_iterator_destroy (ListIterator i)
{
	ListIterator *pi;

	assert(i != NULL);
	assert(i->magic == LIST_MAGIC);
	slurm_mutex_lock(&i->list->mutex);
	assert(i->list->magic == LIST_MAGIC);

	for (pi = &i->list->iNext; *pi; pi = &(*pi)->iNext) {
		assert((*pi)->magic == LIST_MAGIC);
		if (*pi == i) {
			*pi = (*pi)->iNext;
			break;
		}
	}
	slurm_mutex_unlock(&i->list->mutex);

	assert((i->magic = ~LIST_MAGIC));     /* clear magic via assert abuse */
	list_iterator_free(i);
}

/* list_next()
 */
void *
list_next (ListIterator i)
{
	ListNode p;

	assert(i != NULL);
	assert(i->magic == LIST_MAGIC);
	slurm_mutex_lock(&i->list->mutex);
	assert(i->list->magic == LIST_MAGIC);

	if ((p = i->pos))
		i->pos = p->next;
	if (*i->prev != p)
		i->prev = &(*i->prev)->next;

	slurm_mutex_unlock(&i->list->mutex);

	return (p ? p->data : NULL);
}

/* list_peek_next()
 */
void *
list_peek_next (ListIterator i)
{
	ListNode p;

	assert(i != NULL);
	assert(i->magic == LIST_MAGIC);
	slurm_mutex_lock(&i->list->mutex);
	assert(i->list->magic == LIST_MAGIC);

	p = i->pos;

	slurm_mutex_unlock(&i->list->mutex);

	return (p ? p->data : NULL);
}

/* list_insert()
 */
void *
list_insert (ListIterator i, void *x)
{
	void *v;

	assert(i != NULL);
	assert(x != NULL);
	assert(i->magic == LIST_MAGIC);
	slurm_mutex_lock(&i->list->mutex);
	assert(i->list->magic == LIST_MAGIC);

	v = list_node_create(i->list, i->prev, x);
	slurm_mutex_unlock(&i->list->mutex);

	return v;
}

/* list_find()
 */
void *
list_find (ListIterator i, ListFindF f, void *key)
{
	void *v;

	assert(i != NULL);
	assert(f != NULL);
	assert(key != NULL);
	assert(i->magic == LIST_MAGIC);

	while ((v = list_next(i)) && !f(v,key)) {;}

	return v;
}

/* list_remove()
 */
void *
list_remove (ListIterator i)
{
	void *v = NULL;

	assert(i != NULL);
	assert(i->magic == LIST_MAGIC);
	slurm_mutex_lock(&i->list->mutex);
	assert(i->list->magic == LIST_MAGIC);

	if (*i->prev != i->pos)
		v = list_node_destroy(i->list, i->prev);
	slurm_mutex_unlock(&i->list->mutex);

	return v;
}

/* list_delete_item()
 */
int
list_delete_item (ListIterator i)
{
	void *v;

	assert(i != NULL);
	assert(i->magic == LIST_MAGIC);

	if ((v = list_remove(i))) {
		if (i->list->fDel)
			i->list->fDel(v);
		return 1;
	}

	return 0;
}

/* list_node_create()
 */
static void *
list_node_create (List l, ListNode *pp, void *x)
{
/*  Inserts data pointed to by [x] into list [l] after [pp],
 *    the address of the previous node's "next" ptr.
 *  Returns a ptr to data [x], or NULL if insertion fails.
 *  This routine assumes the list is already locked upon entry.
 */
	ListNode p;
	ListIterator i;

	assert(l != NULL);
	assert(l->magic == LIST_MAGIC);
	assert(_list_mutex_is_locked(&l->mutex));
	assert(pp != NULL);
	assert(x != NULL);

	p = list_node_alloc();

	p->data = x;
	if (!(p->next = *pp))
		l->tail = &p->next;
	*pp = p;
	l->count++;

	for (i = l->iNext; i; i = i->iNext) {
		assert(i->magic == LIST_MAGIC);
		if (i->prev == pp)
			i->prev = &p->next;
		else if (i->pos == p->next)
			i->pos = p;
		assert((i->pos == *i->prev) ||
		       ((*i->prev) && (i->pos == (*i->prev)->next)));
	}

	return x;
}

/* list_node_destroy()
 *
 * Removes the node pointed to by [*pp] from from list [l],
 * where [pp] is the address of the previous node's "next" ptr.
 * Returns the data ptr associated with list item being removed,
 * or NULL if [*pp] points to the NULL element.
 * This routine assumes the list is already locked upon entry.
 */
static void *
list_node_destroy (List l, ListNode *pp)
{
	void *v;
	ListNode p;
	ListIterator i;

	assert(l != NULL);
	assert(l->magic == LIST_MAGIC);
	assert(_list_mutex_is_locked(&l->mutex));
	assert(pp != NULL);

	if (!(p = *pp))
		return NULL;

	v = p->data;
	if (!(*pp = p->next))
		l->tail = pp;
	l->count--;

	for (i = l->iNext; i; i = i->iNext) {
		assert(i->magic == LIST_MAGIC);
		if (i->pos == p)
			i->pos = p->next, i->prev = pp;
		else if (i->prev == &p->next)
			i->prev = pp;
		assert((i->pos == *i->prev) ||
		       ((*i->prev) && (i->pos == (*i->prev)->next)));
	}
	list_node_free(p);

	return v;
}

/* list_alloc()
 */
static List
list_alloc (void)
{
	return(list_alloc_aux(sizeof(struct xlist), &list_free_lists));
}

/* list_free()
 */
static void
list_free (List l)
{
	list_free_aux(l, &list_free_lists);
}

/* list_node_alloc()
 */
static ListNode
list_node_alloc (void)
{
	return(list_alloc_aux(sizeof(struct listNode), &list_free_nodes));
}

/* list_node_free()
 */
static void
list_node_free (ListNode p)
{
	list_free_aux(p, &list_free_nodes);
}

/* list_iterator_alloc()
 */
static ListIterator
list_iterator_alloc (void)
{
	return(list_alloc_aux(sizeof(struct listIterator), &list_free_iterators));
}

/* list_iterator_free()
 */
static void
list_iterator_free (ListIterator i)
{
	list_free_aux(i, &list_free_iterators);
}

/* list_alloc_aux()
 */
static void *
list_alloc_aux (int size, void *pfreelist)
{
/*  Allocates an object of [size] bytes from the freelist [*pfreelist].
 *  Memory is added to the freelist in chunks of size LIST_ALLOC.
 *  Returns a ptr to the object, or NULL if the memory request fails.
 */
	void **px;
	void **pfree = pfreelist;
	void **plast;

	assert(sizeof(char) == 1);
	assert(size >= sizeof(void *));
	assert(pfreelist != NULL);
	assert(LIST_ALLOC > 0);
	slurm_mutex_lock(&list_free_lock);

	if (!*pfree) {
		if ((*pfree = xmalloc(LIST_ALLOC * size))) {
			px = *pfree;
			plast = (void **) ((char *) *pfree + ((LIST_ALLOC - 1) * size));
			while (px < plast)
				*px = (char *) px + size, px = *px;
			*plast = NULL;
		}
	}
	if ((px = *pfree))
		*pfree = *px;
	else
		errno = ENOMEM;
	slurm_mutex_unlock(&list_free_lock);

	return px;
}

/* list_free_aux()
 */
static void
list_free_aux (void *x, void *pfreelist)
{
/*  Frees the object [x], returning it to the freelist [*pfreelist].
 */
#ifdef MEMORY_LEAK_DEBUG
	xfree(x);
#else
	void **px = x;
	void **pfree = pfreelist;

	assert(x != NULL);
	assert(pfreelist != NULL);
	slurm_mutex_lock(&list_free_lock);

	*px = *pfree;
	*pfree = px;

	slurm_mutex_unlock(&list_free_lock);
#endif
}

static void
list_reinit_mutexes (void)
{
	slurm_mutex_init(&list_free_lock);
}

void list_install_fork_handlers (void)
{
	if (pthread_atfork(NULL, NULL, &list_reinit_mutexes))
		fatal("cannot install list atfork handler");
}

#ifndef NDEBUG
static int
_list_mutex_is_locked (pthread_mutex_t *mutex)
{
/*  Returns true if the mutex is locked; o/w, returns false.
 */
	int rc;

	assert(mutex != NULL);
	rc = pthread_mutex_trylock(mutex);
	return(rc == EBUSY ? 1 : 0);
}
#endif /* !NDEBUG */

/* _list_pop_locked
 *
 * Pop an item from the list assuming the
 * the list is already locked.
 */
static void *
_list_pop_locked(List l)
{
	void *v;

	v = list_node_destroy(l, &l->head);

	return v;
}

/* _list_append_locked()
 *
 * Append an item to the list. The function assumes
 * the list is already locked.
 */
static void *
_list_append_locked(List l, void *x)
{
	void *v;

	v = list_node_create(l, l->tail, x);

	return v;
}
