/*****************************************************************************\
 *  hostlist.h - hostname list manipulation routines
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _HOSTLIST_H
#define _HOSTLIST_H

/* The hostlist opaque data type 
 *
 * A hostlist is a list of hostnames optimized for a prefixXXXX style 
 * naming convention, where XXXX  is a decimal, numeric suffix.
 */
typedef struct hostlist * hostlist_t;

/* A hostset is a special case of a hostlist. It:
 *
 * 1. never contains duplicates
 * 2. is always sorted 
 *    (Note: sort occurs first on alphanumeric prefix -- where prefix
 *     matches, numeric suffixes will be sorted *by value*)
 */
typedef struct hostset * hostset_t;

/* The hostlist iterator type (may be used with a hostset as well)
 * used for non-destructive access to hostlist members.
 * 
 */
typedef struct hostlist_iterator * hostlist_iterator_t;

/* ----[ general node name functions: ]---- */

/* getnodename - equivalent to gethostname, but return only the first component of the fully 
 *	qualified name (e.g. "linux123.foo.bar" becomes "linux123") */
extern int getnodename (char *name, size_t len);

/* ----[ hostlist_t functions: ]---- */

/* ----[ hostlist creation and destruction ]---- */

/*
 * hostlist_create(): 
 *
 * Create a new hostlist from a string representation. 
 *
 * The string representation (str) may contain one or more hostnames or
 * bracketed hostlists separated by either `,' or whitespace. A bracketed 
 * hostlist is denoted by a common prefix followed by a list of numeric 
 * ranges contained within brackets: e.g. "tux[0-5,12,20-25]" 
 *
 * Note: if this module is compiled with WANT_RECKLESS_HOSTRANGE_EXPANSION
 * defined, a much more loose interpretation of host ranges is used. 
 * Reckless hostrange expansion allows all of the following (in addition to 
 * bracketed hostlists):
 *
 *  o tux0-5,tux12,tux20-25
 *  o tux0-tux5,tux12,tux20-tux25
 *  o tux0-5,12,20-25
 *
 * If str is NULL, and empty hostlist is created and returned. 
 *
 * If the create fails, hostlist_create() returns NULL.
 *
 * The returned hostlist must be freed with hostlist_destroy()
 *
 */
hostlist_t hostlist_create(char *str);

/* hostlist_copy(): 
 *
 * Allocate a copy of a hostlist object. Returned hostlist must be freed
 * with hostlist_destroy.
 */
hostlist_t hostlist_copy(hostlist_t hl);

/* hostlist_destroy():
 *
 * Destroy a hostlist object. Frees all memory allocated to the hostlist.
 */
void hostlist_destroy(hostlist_t hl);


/* ----[ hostlist list operations ]---- */

/* hostlist_push():
 *
 * push a string representation of hostnames onto a hostlist.
 *
 * The hosts argument may take the same form as in hostlist_create()
 *
 * Returns the number of hostnames inserted into the list, 
 * or 0 on failure.
 */
int hostlist_push(hostlist_t hl, char *hosts);


/* hostlist_push_host():
 *
 * Push a single host onto the hostlist hl. 
 * This function is more efficient than hostlist_push() for a single
 * hostname, since the argument does not need to be checked for ranges.
 *
 * return value is 1 for success, 0 for failure.
 */
int hostlist_push_host(hostlist_t hl, char *host);


/* hostlist_push_list():
 *
 * Push a hostlist (hl2) onto another list (hl1)
 *
 * Returns 1 for success, 0 for failure.
 *
 */
int hostlist_push_list(hostlist_t hl1, hostlist_t hl2);


/* hostlist_pop():
 *
 * Returns the string representation of the last host pushed onto the list
 * or NULL if hostlist is empty or there was an error allocating memory.
 * The host is removed from the hostlist.
 *
 * Note: Caller is responsible for freeing the returned memory.
 */
char * hostlist_pop(hostlist_t hl);


/* hostlist_shift():
 *
 * Returns the string representation of the first host in the hostlist
 * or NULL if the hostlist is empty or there was an error allocating memory.
 * The host is removed from the hostlist.
 *
 * Note: Caller is responsible for freeing the returned memory.
 */
char * hostlist_shift(hostlist_t hl);


/* hostlist_pop_range():
 *
 * Pop the last bracketed list of hosts of the hostlist hl.
 * Returns the string representation in bracketed list form.
 * All hosts associated with the returned list are removed
 * from hl.
 *
 * Caller is responsible for freeing returned memory
 */
char * hostlist_pop_range(hostlist_t hl);

/* hostlist_shift_range():
 *
 * Shift the first bracketed hostlist (improperly: range) off the
 * hostlist hl. Returns the string representation in bracketed list
 * form. All hosts associated with the list are removed from the
 * hostlist.
 *
 * Caller is responsible for freeing returned memory.
 */
char * hostlist_shift_range(hostlist_t hl);


/* hostlist_find():
 *
 * Searches hostlist hl for the first host matching hostname 
 * and returns position in list if found. 
 *
 * Returns -1 if host is not found.
 *
 */
int hostlist_find(hostlist_t hl, char *hostname);

/* hostlist_delete():
 *
 * Deletes all hosts in the list represented by `hosts'
 *
 * Returns the number of hosts successfully deleted
 */
int hostlist_delete(hostlist_t hl, char *hosts);


/* hostlist_delete_host():
 *
 * Deletes the first host that matches `hostname' from the hostlist hl.
 *
 * Returns 1 if successful, 0 if hostname is not found in list.
 */
int hostlist_delete_host(hostlist_t hl, char *hostname);


/* hostlist_delete_nth():
 *
 * Deletes the host from position n in the hostlist.
 *
 * Returns 1 if successful 0 on error.
 *
 */
int hostlist_delete_nth(hostlist_t hl, int n);


/* hostlist_count():
 *
 * Return the number of hosts in hostlist hl.
 */ 
int hostlist_count(hostlist_t hl);

/* hostlist_is_empty(): return true if hostlist is empty. */
#define hostlist_is_empty(__hl) ( hostlist_count(__hl) == 0 )

/* ----[ Other hostlist operations ]---- */

/* hostlist_sort():
 * 
 * Sort the hostlist hl.
 *
 */
void hostlist_sort(hostlist_t hl);

/* hostlist_uniq():
 *
 * Sort the hostlist hl and remove duplicate entries.
 * 
 */
void hostlist_uniq(hostlist_t hl);


/* ----[ hostlist print functions ]---- */

/* hostlist_to_string():
 *
 * Write the string representation of the hostlist hl into buf,
 * writing at most n chars. Returns the number of bytes that would have
 * been written (including the terminating '\0') had there been enough
 * space.
 *
 * The result will be NULL terminated.
 * 
 * hostlist_to_string() will write a bracketed hostlist representation
 * where possible.
 */
size_t hostlist_ranged_string(hostlist_t hl, size_t n, char *buf);

/* hostlist_to_long_string():
 *
 * Writes the string representation of the hostlist hl into buf,
 * writing at most n chars. Returns the number of bytes that would have
 * been written (including the terminating '\0'). 
 *
 * hostlist_to_long_string() will not attempt to write a bracketed
 * hostlist representation. Every hostname will be explicitly written.
 */
size_t hostlist_deranged_string(hostlist_t hl, size_t n, char *buf);


/* ----[ hostlist utility functions ]---- */


/* hostlist_nranges():
 *
 * Return the number of ranges currently held in hostlist hl.
 */
int hostlist_nranges(hostlist_t hl);


/* ----[ hostlist iterator functions ]---- */

/* hostlist_iterator_create():
 *
 * Creates and returns a hostlist iterator used for non destructive
 * access to a hostlist or hostset. Returns NULL on failure.
 */
hostlist_iterator_t hostlist_iterator_create(hostlist_t hl);

/* hostset_iterator_create():
 *
 * Same as hostlist_iterator_create(), but creates a hostlist_iterator
 * from a hostset.
 */
hostlist_iterator_t hostset_iterator_create(hostset_t set);

/* hostlist_iterator_destroy():
 *
 * Destroys a hostlist iterator.
 */
void hostlist_iterator_destroy(hostlist_iterator_t i);

/* hostlist_iterator_reset():
 *
 * Reset an iterator to the beginning of the list.
 */
void hostlist_iterator_reset(hostlist_iterator_t i);

/* hostlist_next():
 *
 * Returns a pointer to the  next hostname on the hostlist 
 * or NULL at the end of the list
 *
 * The caller is responsible for freeing the returned memory.
 */ 
char * hostlist_next(hostlist_iterator_t i);


/* hostlist_next_range():
 *
 * Returns the next bracketed hostlist or NULL if the iterator i is
 * at the end of the list.
 *
 * The caller is responsible for freeing the returned memory.
 *
 */
char * hostlist_next_range(hostlist_iterator_t i);


/* hostlist_remove():
 * Removes the last host returned by hostlist iterator i
 *
 * Returns 1 for success, 0 for failure.
 */
int hostlist_remove(hostlist_iterator_t i);


/* ----[ hostset operations ]---- */

/* hostset_create():
 *
 * Create a new hostset object from a string representation of a list of
 * hosts. See hostlist_create() for valid hostlist forms.
 */
hostset_t hostset_create(char *hostlist);

/* hostset_copy():
 *
 * Copy a hostset object. Returned set must be freed with hostset_destroy().
 */
hostset_t hostset_copy(hostset_t set);

/* hostset_destroy():
 */
void hostset_destroy(hostset_t set);

/* hostset_insert():
 * Add a host or list of hosts into hostset "set."
 *
 * Returns number of hosts successfully added to "set"
 * (insertion of a duplicate is not considered successful)
 */
int hostset_insert(hostset_t set, char *hosts);

/* hostset_delete():
 * Delete a host or list of hosts from hostset "set."
 * Returns number of hosts deleted from set.
 */
int hostset_delete(hostset_t set, char *hosts);

/* hostset_within():
 * Return 1 if all hosts specified by "hosts" are within the hostset "set"
 * Retrun 0 if every host in "hosts" is not in the hostset "set"
 */
int hostset_within(hostset_t set, char *hosts);

/* hostset_shift():
 * hostset equivalent to hostlist_shift()
 */
char * hostset_shift(hostset_t set);

/* hostset_shift_range():
 * hostset eqivalent to hostlist_shift_range()
 */
char * hostset_shift_range(hostset_t set);

/* hostset_count():
 * Count the number of hosts currently in hostset
 */
int hostset_count(hostset_t set);


#endif /* !_HOSTLIST_H */
