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

/*
 *  List opaque data type.
 */

/*
 *  List Iterator opaque data type.
 */
typedef struct listIterator *ListIterator;

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
List list_create(ListDelF f);

/*
 *  Destroys list [l], freeing memory used for list iterators and the
 *    list itself; if a deletion function was specified when the list
 *    was created, it will be called for each item in the list.
 */
void list_destroy(List l);

/*
 *  Returns non-zero if list [l] is empty; o/w returns zero.
 */
int list_is_empty(List l);

/*
 * Return the number of items in list [l].
 * If [l] is NULL, return 0.
 */
int list_count(List l);

/*
 *  Create new shallow copy of list [l] pointers, without destructor.
 *
 *  The list created is intended to allow manipulation of the list without
 *  affecting the real list (such as sorting).
 *
 *  Warning: destruction of this list will not free members of [l].
 *  Warning: This list is only valid while [l] is unchanged.
 */
List list_shallow_copy(List l);

/***************************
 *  List Access Functions  *
 ***************************/

/*
 *  Inserts data [x] at the end of list [l].
 *  Returns the data's ptr.
 */
void *list_append(List l, void *x);

/*
 *  Inserts list [sub] at the end of list [l].
 *  Note: list [l] must have a destroy function of NULL.
 *  Returns a count of the number of items added to list [l].
 */
int list_append_list(List l, List sub);

/*
 *  Pops off list [sub] and appends data at the end of list [l].
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [sub] will be returned empty, but not destroyed.
 *  Returns a count of the number of items added to list [l].
 */
int list_transfer(List l, List sub);

/*
 *  Pop off elements in list [sub] to [l], unless already in [l].
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [l] could contain repeated elements, but those aren't removed.
 *  Note: list [sub] will be returned with repeated elements or empty,
 *        but never destroyed.
 *  Returns a count of the number of items added to list [l].
 */
int list_transfer_unique(List l, ListFindF f, List sub);

/*
 *  Pops off list [sub] to [l] with maximum number of entries.
 *  Set max = -1 to transfer all entries.
 *  Note: list [l] must have the same destroy function as list [sub].
 *  Note: list [sub] may be returned empty, but not destroyed.
 *  Returns a count of the number of items added to list [l].
 */
int list_transfer_max(List l, List sub, int max);

/*
 *  Inserts data [x] at the beginning of list [l].
 *  Returns the data's ptr.
 */
void *list_prepend(List l, void *x);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Returns a ptr to the first item for which the function [f]
 *    returns non-zero, or NULL if no such item is found.
 *  Note: This function differs from list_find() in that it does not require
 *    a list iterator; it should only be used when all list items are known
 *    to be unique (according to the function [f]).
 */
void *list_find_first(List l, ListFindF f, void *key);

/*
 * Same as list_find_first, but use rdlock instead of wrlock
 */
void *list_find_first_ro(List l, ListFindF f, void *key);


/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Returns a ptr to the first item for which the function [f]
 *    returns non-zero and removes it from the list, or NULL if no such item is
 *    found.
 *  Note: This function differs from list_remove() in that it does not require
 *    a list iterator; it should only be used when all list items are known
 *    to be unique (according to the function [f]).
 */
void *list_remove_first(List l, ListFindF f, void *key);

/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Removes all items from the list for which the function [f] returns
 *    non-zero; if a deletion function was specified when the list was
 *    created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */
int list_delete_all(List l, ListFindF f, void *key);

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
int list_delete_first(List l, ListFindF f, void *key);

/*
 *  Traverses list [l] and deletes 'key' from it.
 *  Removes this ptr from the list; if a deletion function was specified when
 *  the list was created, it will be called to deallocate each item being
 *  removed.
 *  Returns 1 if found and 0 if not.
 */
int list_delete_ptr(List l, void *key);

/*
 *  For each item in list [l], invokes the function [f] with [arg].
 *  Returns a count of the number of items on which [f] was invoked.
 *  If [f] returns <0 for a given item, the iteration is aborted and the
 *    function returns the negative of that item's position in the list.
 */
int list_for_each(List l, ListForF f, void *arg);
int list_for_each_ro(List l, ListForF f, void *arg);

/*
 *  For each item in list [l], invokes the function [f] with [arg].
 *  Returns a count of the number of items on which [f] was invoked.
 *  If [f] returns <0 for a given item, the iteration is NOT aborted but the
 *  return value (count of items processed) will be negated.
 */
int list_for_each_nobreak(List l, ListForF f, void *arg);

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
int list_for_each_max(List l, int *max, ListForF f, void *arg,
		      int break_on_fail, int write_lock);

/*
 *  Traverses list [l] and removes all items in list
 *  If a deletion function was specified when the list was
 *  created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */
int list_flush(List l);

/*
 *  Traverses list [l] and removes items.
 *  Will process up to [max] number of list items, or set [max] to -1 for all.
 *  If a deletion function was specified when the list was
 *  created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */
int list_flush_max(List l, int max);

/*
 *  Sorts list [l] into ascending order according to the function [f].
 *  Note: Sorting a list resets all iterators associated with the list.
 *  This function uses the libC qsort() algorithm.
 */
void list_sort(List l, ListCmpF f);

/*
 * Reverses the order of the items in list [l].
 * Note: Reversing a list resets all iterators associated with the list.
 */
void list_flip(List l);

/****************************
 *  Stack Access Functions  *
 ****************************/

/*
 *  Pushes data [x] onto the top of stack [l].
 *  Returns the data's ptr.
 */
void *list_push(List l, void *x);

/*
 *  Pops the data item at the top of the stack [l].
 *  Returns the data's ptr, or NULL if the stack is empty.
 */
void *list_pop(List l);

/*
 *  Peeks at the data item at the top of the stack (or head of the queue) [l].
 *  Returns the data's ptr, or NULL if the stack (or queue) is empty.
 *  Note: The item is not removed from the list.
 */
void *list_peek(List l);

/****************************
 *  Queue Access Functions  *
 ****************************/

/*
 *  Enqueues data [x] at the tail of queue [l].
 *  Returns the data's ptr.
 */
void *list_enqueue(List l, void *x);

/*
 *  Dequeues the data item at the head of the queue [l].
 *  Returns the data's ptr, or NULL if the queue is empty.
 */
void *list_dequeue(List l);


/*****************************
 *  List Iterator Functions  *
 *****************************/

/*
 *  Creates and returns a list iterator for non-destructively traversing
 *    list [l].
 */
ListIterator list_iterator_create(List l);

/*
 *  Resets the list iterator [i] to start traversal at the beginning
 *    of the list.
 */
void list_iterator_reset(ListIterator i);

/*
 *  Destroys the list iterator [i]; list iterators not explicitly destroyed
 *    in this manner will be destroyed when the list is deallocated via
 *    list_destroy().
 */
void list_iterator_destroy(ListIterator i);

/*
 *  Returns a ptr to the next item's data,
 *    or NULL once the end of the list is reached.
 *  Example: i=list_iterator_create(i); while ((x=list_next(i))) {...}
 */
void *list_next(ListIterator i);

/*
 *  Returns a ptr to the next item's data WITHOUT advancing the pointer,
 *    or NULL once the end of the list is reached.
 */
void *list_peek_next(ListIterator i);

/*
 *  Inserts data [x] immediately before the last item returned via list
 *    iterator [i]; once the list iterator reaches the end of the list,
 *    insertion is made at the list's end.
 *  Returns the data's ptr.
 */
void *list_insert(ListIterator i, void *x);

/*
 *  Traverses the list from the point of the list iterator [i]
 *    using [f] to match each item with [key].
 *  Returns a ptr to the next item for which the function [f]
 *    returns non-zero, or NULL once the end of the list is reached.
 *  Example: i=list_iterator_reset(i); while ((x=list_find(i,f,k))) {...}
 */
void *list_find(ListIterator i, ListFindF f, void *key);

/*
 *  Removes from the list the last item returned via list iterator [i]
 *    and returns the data's ptr.
 *  Note: The client is responsible for freeing the returned data.
 */
void *list_remove(ListIterator i);

/*
 *  Removes from the list the last item returned via list iterator [i];
 *    if a deletion function was specified when the list was created,
 *    it will be called to deallocate the item being removed.
 *  Returns a count of the number of items removed from the list
 *    (ie, '1' if the item was removed, and '0' otherwise).
 */
int list_delete_item(ListIterator i);

#endif /* !LSD_LIST_H */
