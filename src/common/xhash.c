/*****************************************************************************\
 *  xtree.c - functions used for hash table manament
 *****************************************************************************
 *  Copyright (C) 2012 CEA/DAM/DIF
 *  Copyright (C) 2013 SchedMD LLC. Written by David Bigagli
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/xmalloc.h"
#include "src/common/uthash/uthash.h"
#include "src/common/xstring.h"
#include "src/common/xhash.h"

#if 0
/* undefine default allocators */
#undef uthash_malloc
#undef uthash_free

/* re-define them using slurm's ones */
#define uthash_malloc(sz) xmalloc(sz)
#define uthash_free(ptr, sz) xfree(ptr)
#endif

/*
 * FIXME:
 * A pre-malloced array of xhash_item_t could be associated to
 * the xhash_table in order to speed up the xhash_add function.
 * The default array size could be something like 10% of the
 * provided table_size (hash table are commonly defined larger
 * than necessary to avoid shared keys and usage of linked list)
 */

typedef struct xhash_item_st {
	void*		item;    /* user item                               */
	const char*	key;     /* cached key calculated by user function, */
                                 /* needed by uthash                        */
	uint32_t	keysize; /* cached key size                         */
	UT_hash_handle	hh;      /* make this structure hashable by uthash  */
} xhash_item_t;

struct xhash_st {
	uint32_t		count;    /* user items count                */
	xhash_freefunc_t	freefunc; /* function used to free items     */
	xhash_item_t*		ht;       /* hash table                      */
	xhash_idfunc_t		identify; /* function returning a unique str
					     key */
};

xhash_t* xhash_init(xhash_idfunc_t idfunc,
		    xhash_freefunc_t freefunc,
		    xhash_hashfunc_t hashfunc,
		    uint32_t table_size)
{
	xhash_t* table = NULL;
	if (!idfunc)
		return NULL;
	table = (xhash_t*)xmalloc(sizeof(xhash_t));
	table->ht = NULL; /* required by uthash */
	table->count = 0;
	table->identify = idfunc;
	table->freefunc = freefunc;
	return table;
}

static xhash_item_t* xhash_find(xhash_t* table, const char* key)
{
	xhash_item_t* hash_item = NULL;
	uint32_t      key_size  = 0;

	if (!table || !key)
		return NULL;
	key_size = strlen(key);
	HASH_FIND(hh, table->ht, key, key_size, hash_item);
	return hash_item;
}

void* xhash_get(xhash_t* table, const char* key)
{
	xhash_item_t* item = xhash_find(table, key);
	if (!item)
		return NULL;
	return item->item;
}

void* xhash_add(xhash_t* table, void* item)
{
	xhash_item_t* hash_item = NULL;
	if (!table || !item)
		return NULL;
	hash_item          = (xhash_item_t*)xmalloc(sizeof(xhash_item_t));
	hash_item->item    = item;
	hash_item->key     = table->identify(item);
	hash_item->keysize = strlen(hash_item->key);
	HASH_ADD_KEYPTR(hh, table->ht, hash_item->key,
			hash_item->keysize, hash_item);
	++table->count;
	return hash_item->item;
}

void* xhash_pop(xhash_t* table, const char* key)
{
	void* item_item;
	xhash_item_t* item = xhash_find(table, key);
	if (!item)
		return NULL;
	item_item = item->item;
	HASH_DELETE(hh, table->ht, item);
	xfree(item);
	--table->count;
	return item_item;
}

void xhash_delete(xhash_t* table, const char* key)
{
	if (!table || !key)
		return;
	void* item_item = xhash_pop(table, key);
	if (table->freefunc)
		table->freefunc(item_item);
}

uint32_t xhash_count(xhash_t* table)
{
	if (!table)
		return 0;
	return table->count;
}

void xhash_walk(xhash_t* table,
		void (*callback)(void* item, void* arg),
		void* arg)
{
	xhash_item_t* current_item = NULL;
	xhash_item_t* tmp = NULL;
	if (!table || !callback)
		return;
	HASH_ITER(hh, table->ht, current_item, tmp) {
		  callback(current_item->item, arg);
	}
}

void xhash_clear(xhash_t* table)
{
	xhash_item_t* current_item = NULL;
	xhash_item_t* tmp = NULL;

	if (!table)
		return;
	HASH_ITER(hh, table->ht, current_item, tmp) {
		  HASH_DEL(table->ht, current_item);
		  if (table->freefunc)
			  table->freefunc(current_item->item);
		  xfree(current_item);
	}

	table->count = 0;
}

void xhash_free(xhash_t* table)
{
	if (!table)
		return;
	xhash_clear(table);
	xfree(table);
}

/* String hash table using the pjw hashing algorithm
 * and chaining conflict resolution.
 * Includes a double linked list implementation.
 */

static pthread_mutex_t hash_mutex = PTHREAD_MUTEX_INITIALIZER;

static int _hash_install(struct hash_tab *, const char *, void *);
static struct hash_entry *_hash_lookup(struct hash_tab *, const char *);
static void _rehash(struct hash_tab *, int);
static int _find_closest_prime(int);
static int _is_prime(int);
static int _pjw_hash(const char *, uint32_t);

static int primes[] = {
	293,
	941,
	1427,
	1619,
	2153,
	5483,
	10891,		 /* 10K */
	24571,
	69857,
	111697,
	200003,
	1000003,	 /* 1MB */
	2000003,
	8000099,
	16000097,
	50000063,	 /* 50 MB */
	100000081,	 /* 100 MB */
	150999103,
	250000103,	 /* 250MB */
	500000101,	 /* 500MB */
	750003379,	 /* 750MB */
	1000004897,	 /* 1GB */
	2002950673	 /* 2GB that's one mother */
};

/* hash_make()
 */
struct hash_tab *
hash_make(uint32_t size)
{
	struct hash_tab *t;
	int cc;

	cc = _find_closest_prime(size);

	t = xmalloc(1 * sizeof(struct hash_tab));
	t->num_ents = 0;
	t->size = cc;
	t->lists = xmalloc(cc * sizeof(struct list_ *));

	return t;
}

/* hash_install()
 */
int
hash_install(struct hash_tab *t, const char *key, void *data)
{
	int cc;

	slurm_mutex_lock(&hash_mutex);
	cc = _hash_install(t, key, data);
	slurm_mutex_unlock(&hash_mutex);

	return cc;
}

/* hash_lookup()
 */
void *
hash_lookup(struct hash_tab *t, const char *key)
{
	struct hash_entry *e;

	slurm_mutex_lock(&hash_mutex);
	e = _hash_lookup(t, key);
	if (e) {
		slurm_mutex_unlock(&hash_mutex);
		return e->data;
	}

	slurm_mutex_unlock(&hash_mutex);
	return NULL;
}

/* hash_remove()
 */
void *
hash_remove(struct hash_tab *t, const char *key)
{
	struct hash_entry *e;
	int cc;
	void *v;

	if (t == NULL
	    || key == NULL)
		return NULL;

	slurm_mutex_lock(&hash_mutex);

	cc = _pjw_hash(key, t->size);
	if (t->lists[cc] == NULL) {
		slurm_mutex_unlock(&hash_mutex);
		return NULL;
	}

	for (e = (struct hash_entry *)t->lists[cc]->forw;
	     e!= (void *)t->lists[cc];
	     e = e->forw) {

		if (strcmp(e->key, key) == 0) {
			list_rm_(t->lists[cc], (struct list_ *)e);
			t->num_ents--;
			v = e->data;
			xfree(e->key);
			xfree(e);
			slurm_mutex_unlock(&hash_mutex);
			return v;
		}
	}

	slurm_mutex_unlock(&hash_mutex);
	return NULL;
}

/* hash_free()
 */
void
hash_free(struct hash_tab *t,
	  void (*f)(char *key, void *data))
{
	int cc;
	struct hash_entry *e;

	if (t == NULL)
		return;

	slurm_mutex_lock(&hash_mutex);
	for (cc = 0; cc < t->size; cc++) {

		if (t->lists[cc] == NULL)
			continue;

		while ((e = (struct hash_entry *)list_pop_(t->lists[cc]))) {
			if (f) {
				(*f)(e->key, e->data);
			} else {
				xfree(e->key);
				xfree(e);
			}
		}
		list_free_(t->lists[cc], NULL);
	}
	xfree(t->lists);
	xfree(t);

	slurm_mutex_unlock(&hash_mutex);
}

/* _rehash()
 */
static void
_rehash(struct hash_tab *t,
       int size)
{
    struct list_ **list;
    int cc;
    struct hash_tab t2;

    memset(&t2, 0, sizeof(t2));

    cc = _find_closest_prime(size);
    t2.size = cc;

    list = xmalloc(cc * sizeof(struct list_ *));
    t2.lists = list;

    for (cc = 0; cc < t->size; cc++) {
        struct hash_entry *e;

        if (t->lists[cc] == NULL)
            continue;

        while ((e = (struct hash_entry *)list_pop_(t->lists[cc]))) {
		_hash_install(&t2, e->key, e->data);
		xfree(e->key);
		xfree(e);
        }
        list_free_(t->lists[cc], NULL);
    }

    xfree(t->lists);
    t->lists = list;
    t->size = t2.size;
    t->num_ents  = t2.num_ents;

} /* rehash() */

/* _find_closest_prime()
 */
static int
_find_closest_prime(int s)
{
	int n;
	int cc;

	if (_is_prime(s))
		return s;

	n = sizeof(primes)/sizeof(primes[0]);

	for (cc = 0; cc < n; cc++) {
		if (s < primes[cc])
			return primes[cc];
	}

	return primes[n - 1];
}

/* _is_prime()
 */
int
_is_prime(int s)
{
	int cc;

	/* Try all divisors upto square
	 * root of s;
	 */
	for (cc = 2; cc*cc <= s; cc++) {
		if ((s % cc) == 0)
			return 0;
	}
	return 1;
}

/* _pjw_hash()
 *
 * Hash a string using an algorithm taken from Aho, Sethi, and Ullman,
 * "Compilers: Principles, Techniques, and Tools," Addison-Wesley,
 * 1985, p. 436.  PJW stands for Peter J. Weinberger, who apparently
 * originally suggested the function.
 */
static int
_pjw_hash(const char *x, uint32_t size)
{
	const char *s = x;
	unsigned int h = 0;
	unsigned int g;

	while (*s != 0)	 {
		h = (h << 4) + *s++;
		if ((g = h & (unsigned int) 0xf0000000) != 0)
			h = (h ^ (g >> 24)) ^ g;
	}

	return h % size;
}

/* _hash_install()
 */
static int
_hash_install(struct hash_tab *t, const char *key, void *data)
{
	int cc;
	struct hash_entry *e;

	if (t == NULL
	    || key == NULL)
		return -1;

	/* FIXME rehash the table if
	 * t->num >= 0.9 * t->size
	 */
	if (t->num_ents >= 0.9 * t->size)
		_rehash(t, 3 * t->size);

	if ((e = hash_lookup(t, key)) == NULL) {
		e = xmalloc(1 * sizeof(struct hash_entry));
		e->key = xstrdup(key);
	}
	e->data = data;

	cc = _pjw_hash(key, t->size);
	if (t->lists[cc] == NULL)
		t->lists[cc] = list_make_("");
	list_push_(t->lists[cc], (struct list_ *)e);
	t->num_ents++;

	return 0;
}

/* _hash_lookup()
 */
static struct hash_entry *
_hash_lookup(struct hash_tab *t, const char *key)
{
	struct hash_entry *e;
	int cc;

	if (t == NULL
	    || key == NULL)
		return NULL;

	cc = _pjw_hash(key, t->size);
	if (t->lists[cc] == NULL)
		return NULL;

	for (e = (struct hash_entry *)t->lists[cc]->forw;
	     e!= (void *)t->lists[cc];
	     e = e->forw) {
		if (strcmp(e->key, key) == 0)
			return e;
	}

	return NULL;
}

/* Simple double linked list.
 *
 * The idea is very simple we have 2 insertion methods
 * enqueue which adds at the end of the queue and push
 * which adds at the front. Then we simply pick the first
 * element in the queue. If you have inserted by enqueue
 * you get a FCFS policy if you pushed you get a stack policy.
 *
 *
 * FCFS
 *
 *	 H->1->2->3->4
 *
 * you retrive elements as 1, 2 etc
 *
 * Stack:
 *
 * H->4->3->2->1
 *
 * you retrieve the elements as 4,3, etc
 *
 * The trailing underscore in the function name avoid
 * naming conflict with other list implementation.
 *
 */

struct list_ *
list_make_(const char *name)
{
	struct list_ *list;

	list = xmalloc(1 * sizeof(struct list_));
	list->forw = list->back = list;

	list->name = xstrdup(name);

	return list;

}

/* list_inisert_()
 *
 * Using cartesian coordinates the head h is at
 * zero and elemets are pushed along x axes.
 *
 *		 <-- back ---
 *		/			  \
 *	   h <--> e2 <--> e
 *		\			  /
 *		  --- forw -->
 *
 * The h points the front, the first element of the list,
 * elements can be pushed in front or enqueued at the back.
 *
 */
int
list_insert_(struct list_ *h,
	     struct list_ *e,
	     struct list_ *e2)
{
	/*	before: h->e
	 */

	e->back->forw = e2;
	e2->back = e->back;
	e->back = e2;
	e2->forw = e;

	/* after h->e2->e
	 */

	h->num_ents++;

	return h->num_ents;

}

/*
 * list_enqueue_()
 *
 * Enqueue a new element at the end
 * of the list.
 *
 * listenque()/listdeque()
 * implements FCFS policy.
 *
 */
int
list_enque_(struct list_ *h,
	    struct list_ *e2)
{
	/* before: h->e
	 */
	list_insert_(h, h, e2);
	/* after: h->e->e2
	 */
	return 0;
}

/* list_deque_()
 */
struct list_ *
list_deque_(struct list_ *h)
{
	struct list_   *e;

	if (h->forw == h)
		return NULL;

	/* before: h->e->e2
	 */

	e = list_rm_(h, h->forw);

	/* after: h->e2
	 */

	return e;
}

/*
 * list_push_()
 *
 * Push e at the front of the list
 *
 * H --> e --> e2
 *
 */
int
list_push_(struct list_ *h,
	   struct list_ *e2)
{
	/* before: h->e
	 */
	list_insert_(h, h->forw, e2);

	/* after: h->e2->e
	 */

	return 0;
}

/* list_pop_()
 */
struct list_ *
list_pop_(struct list_ *h)
{
	struct list_ *e;

	e = list_deque_(h);

	return e;
}

/* list_pop()
 */
struct list_ *
list_rm_(struct list_ *h,
	 struct list_ *e)
{
	if (h->num_ents == 0)
		return NULL;

	e->back->forw = e->forw;
	e->forw->back = e->back;
	h->num_ents--;

	return e;

}


/* list_free_()
 */
void
list_free_(struct list_ *list,
	   void (*f)(void *))
{
	struct list_ *l;

	if (list == NULL)
		return;

	while ((l = list_pop_(list))) {
		if (f == NULL)
			xfree(l);
		else
			(*f)(l);
	}

	list->num_ents = 0;
	xfree(list->name);
	xfree(list);
}
