/*****************************************************************************
 *  list.h
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
 *****************************************************************************/

#ifndef LSD_LIST_H
#define LSD_LIST_H

#define FREE_NULL_LIST(_X)			\
	do {					\
		if (_X) list_destroy (_X);	\
		_X	= NULL; 		\
	} while (0)

/****************
 *  Data Types  *
 ****************/

#ifndef   __list_datatypes_defined
#  define __list_datatypes_defined
typedef struct xlist *List;
typedef struct xlist list_t;

/*
 *  List opaque data type.
 */

/*
 *  List Iterator opaque data type.
 */
typedef struct listIterator *ListIterator;
typedef struct listIterator list_itr_t;

/*
 *  Function prototype to deallocate data stored in a list.
 *    This function is responsible for freeing all memory associated
 *    with an item, including all subordinate items (if applicable).
 */
typedef void (*ListDelF) (void *x);

/*
 *  Function prototype for comparing two items in a list.
 *  Returns less-than-zero if (x<y), zero if (x==y), and
 *    greather-than-zero if (x>y).
 */
typedef int (*ListCmpF) (void *x, void *y);

/*
 *  Function prototype for matching items in a list.
 *  Returns non-zero if (x==key); o/w returns zero.
 */
typedef int (*ListFindF) (void *x, void *key);

/*
 *  Function prototype for operating on each item in a list.
 *  Returns less-than-zero on error.
 */
typedef int (*ListForF) (void *x, void *arg);

#endif

/*******************************
 *  General-Purpose Functions  *
 *******************************/

/*
 *  Creates and returns a new empty list.
 *  The deletion function [f] is used to deallocate memory used by items
 *    in the list; if this is NULL, memory associated with these items
 *    will not be freed when the list is destroyed.
 *  Note: Abandoning a list without calling list_destroy() will result
 *    in a memory leak.
 */
extern list_t *list_create(ListDelF f);

/*
 *  Destroys list [l], freeing memory used for list iterators and the
 *    list itself; if a deletion function was specified when the list
 *    was created, it will be called for each item in the list.
 */
extern void list_destroy(list_t *l);

/*
 *  Returns non-zero if list [l] is empty; o/w returns zero.
 */
extern int list_is_empty(list_t *l);

/*
 * Return the number of items in list [l].
 * If [l] is NULL, return 0.
 */
extern int list_count(list_t *l);

/*
 *  Create new shallow copy of list [l] pointers, without destructor.
 *
 *  The list created is intended to allow manipulation of the list without
 *  affecting the real list (such as sorting).
 *
 *  Warning: destruction of this list will not free members of [l].
 *  Warning: This list is only valid while [l] is unchanged.
 */
extern list_t *list_shallow_copy(list_t *l);

/***************************
 *  List Access Functions  *
 ***************************/

/*
 *  Inserts data [x] at the end of list [l].
 */
extern void list_append(list_t *l, void *x);

/*
 *  Inserts list [sub] at the end of list [l].
 *  Note: list [l] must have a destroy function of NULL.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_append_list(list_t *l, List sub);

/*
 *  Pops off list [sub] and appends data at the end of list [l].
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [sub] will be returned empty, but not destroyed.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_transfer(list_t *l, List sub);

/*
 *  Pop off elements in list [sub] to [l], unless already in [l].
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [l] could contain repeated elements, but those aren't removed.
 *  Note: list [sub] will be returned with repeated elements or empty,
 *        but never destroyed.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_transfer_unique(list_t *l, ListFindF f, list_t *sub);

/*
 *  Pops off list [sub] to [l] with maximum number of entries.
 *  Set max = -1 to transfer all entries.
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [sub] may be returned empty, but not destroyed.
 *  Returns a count of the number of items added to list [l].
 */
extern int list_transfer_max(list_t *l, list_t *sub, int max);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Matching items are then transfered to [sub].
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Returns a count of the number of items moved to list [sub] from list [l].
 */
extern int list_transfer_match(list_t *l, list_t *sub, ListFindF f, void *key);

/*
 *  Inserts data [x] at the beginning of list [l].
 */
extern void list_prepend(list_t *l, void *x);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Returns a ptr to the first item for which the function [f]
 *    returns non-zero, or NULL if no such item is found.
 *  Note: This function differs from list_find() in that it does not require
 *    a list iterator; it should only be used when all list items are known
 *    to be unique (according to the function [f]).
 */
extern void *list_find_first(list_t *l, ListFindF f, void *key);

/*
 * Same as list_find_first, but use rdlock instead of wrlock
 */
extern void *list_find_first_ro(list_t *l, ListFindF f, void *key);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Returns a ptr to the first item for which the function [f]
 *    returns non-zero and removes it from the list, or NULL if no such item is
 *    found.
 *  Note: This function differs from list_remove() in that it does not require
 *    a list iterator; it should only be used when all list items are known
 *    to be unique (according to the function [f]).
 */
extern void *list_remove_first(list_t *l, ListFindF f, void *key);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Removes all items from the list for which the function [f] returns
 *    non-zero; if a deletion function was specified when the list was
 *    created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */
extern int list_delete_all(list_t *l, ListFindF f, void *key);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Removes the first item from the list for which the function [f] returns
 *    a positive value; if a deletion function was specified when the list was
 *    created, it will be called to deallocate each item being removed.
 *  If [f] returns a negative value, processing is stopped without removing
 *    any items.
 *  Returns 0 if no item was found, 1 if an item was removed, -1 if processing
 *    was stopped.
 */
extern int list_delete_first(list_t *l, ListFindF f, void *key);

/*
 *  Traverses list [l] and deletes 'key' from it.
 *  Removes this ptr from the list; if a deletion function was specified when
 *  the list was created, it will be called to deallocate each item being
 *  removed.
 *  Returns 1 if found and 0 if not.
 */
extern int list_delete_ptr(list_t *l, void *key);

/*
 *  For each item in list [l], invokes the function [f] with [arg].
 *  Returns a count of the number of items on which [f] was invoked.
 *  If [f] returns <0 for a given item, the iteration is aborted and the
 *    function returns the negative of that item's position in the list.
 */
extern int list_for_each(list_t *l, ListForF f, void *arg);
extern int list_for_each_ro(list_t *l, ListForF f, void *arg);

/*
 *  For each item in list [l], invokes the function [f] with [arg].
 *  Returns a count of the number of items on which [f] was invoked.
 *  If [f] returns <0 for a given item, the iteration is NOT aborted but the
 *  return value (count of items processed) will be negated.
 */
extern int list_for_each_nobreak(list_t *l, ListForF f, void *arg);

/*
 *  For each item in list [l], invokes the function [f] with [arg].
 *  Will process up to [max] number of list items, or set [max] to -1 for all.
 *  [max] will be return to the number of unprocessed items remaining.
 *  [write_lock] controls whether a read-lock or write-lock is used to access
 *  the list.
 *  Returns a count of the number of items on which [f] was invoked.
 *  If [f] returns <0 for a given item, the iteration is aborted and the
 *    function returns the negative of that item's position in the list.
 */
extern int list_for_each_max(list_t *l, int *max, ListForF f, void *arg,
			     int break_on_fail, int write_lock);

/*
 *  Traverses list [l] and removes all items in list
 *  If a deletion function was specified when the list was
 *  created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */
extern int list_flush(list_t *l);

/*
 *  Traverses list [l] and removes items.
 *  Will process up to [max] number of list items, or set [max] to -1 for all.
 *  If a deletion function was specified when the list was
 *  created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */
extern int list_flush_max(list_t *l, int max);

/*
 *  Sorts list [l] into ascending order according to the function [f].
 *  Note: Sorting a list resets all iterators associated with the list.
 *  This function uses the libC qsort() algorithm.
 */
extern void list_sort(list_t *l, ListCmpF f);

/*
 * Reverses the order of the items in list [l].
 * Note: Reversing a list resets all iterators associated with the list.
 */
extern void list_flip(list_t *l);

/****************************
 *  Stack Access Functions  *
 ****************************/

/*
 *  Pushes data [x] onto the top of stack [l].
 */
extern void list_push(list_t *l, void *x);

/*
 *  Pops the data item at the top of the stack [l].
 *  Returns the data's ptr, or NULL if the stack is empty.
 */
extern void *list_pop(list_t *l);

/*
 *  Peeks at the data item at the top of the stack (or head of the queue) [l].
 *  Returns the data's ptr, or NULL if the stack (or queue) is empty.
 *  Note: The item is not removed from the list.
 */
extern void *list_peek(list_t *l);

/****************************
 *  Queue Access Functions  *
 ****************************/

/*
 *  Enqueues data [x] at the tail of queue [l].
 */
extern void list_enqueue(list_t *l, void *x);

/*
 *  Dequeues the data item at the head of the queue [l].
 *  Returns the data's ptr, or NULL if the queue is empty.
 */
extern void *list_dequeue(list_t *l);


/*****************************
 *  List Iterator Functions  *
 *****************************/

/*
 *  Creates and returns a list iterator for non-destructively traversing
 *    list [l].
 */
extern list_itr_t *list_iterator_create(list_t *l);

/*
 *  Resets the list iterator [i] to start traversal at the beginning
 *    of the list.
 */
extern void list_iterator_reset(list_itr_t *i);

/*
 *  Destroys the list iterator [i]; list iterators not explicitly destroyed
 *    in this manner will be destroyed when the list is deallocated via
 *    list_destroy().
 */
extern void list_iterator_destroy(list_itr_t *i);

/*
 *  Returns a ptr to the next item's data,
 *    or NULL once the end of the list is reached.
 *  Example: i=list_iterator_create(i); while ((x=list_next(i))) {...}
 */
extern void *list_next(list_itr_t *i);

/*
 *  Returns a ptr to the next item's data WITHOUT advancing the pointer,
 *    or NULL once the end of the list is reached.
 */
extern void *list_peek_next(list_itr_t *i);

/*
 *  Inserts data [x] immediately before the last item returned via list
 *    iterator [i]; once the list iterator reaches the end of the list,
 *    insertion is made at the list's end.
 */
extern void list_insert(list_itr_t *i, void *x);

/*
 *  Traverses the list from the point of the list iterator [i]
 *    using [f] to match each item with [key].
 *  Returns a ptr to the next item for which the function [f]
 *    returns non-zero, or NULL once the end of the list is reached.
 *  Example: i=list_iterator_reset(i); while ((x=list_find(i,f,k))) {...}
 */
extern void *list_find(list_itr_t *i, ListFindF f, void *key);

/*
 *  Removes from the list the last item returned via list iterator [i]
 *    and returns the data's ptr.
 *  Note: The client is responsible for freeing the returned data.
 */
extern void *list_remove(list_itr_t *i);

/*
 *  Removes from the list the last item returned via list iterator [i];
 *    if a deletion function was specified when the list was created,
 *    it will be called to deallocate the item being removed.
 *  Returns a count of the number of items removed from the list
 *    (ie, '1' if the item was removed, and '0' otherwise).
 */
extern int list_delete_item(list_itr_t *i);

#endif /* !LSD_LIST_H */
