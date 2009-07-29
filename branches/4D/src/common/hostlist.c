/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  $LSDId: hostlist.c,v 1.14 2003/10/14 20:11:54 grondo Exp $
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STRING_H
#    include <string.h>
#  endif
#  if HAVE_PTHREAD_H
#    include <pthread.h>
#  endif
#else                /* !HAVE_CONFIG_H */
#  include <string.h>
#  include <pthread.h>
#endif                /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <sys/param.h>
#include <unistd.h>
#include <slurm/slurm.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/timers.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
 * for details. 
 */
strong_alias(hostlist_create,		slurm_hostlist_create);
strong_alias(hostlist_copy,		slurm_hostlist_copy);
strong_alias(hostlist_count,		slurm_hostlist_count);
strong_alias(hostlist_delete,		slurm_hostlist_delete);
strong_alias(hostlist_delete_host,	slurm_hostlist_delete_host);
strong_alias(hostlist_delete_nth,	slurm_hostlist_delete_nth);
strong_alias(hostlist_deranged_string,	slurm_hostlist_deranged_string);
strong_alias(hostlist_destroy,		slurm_hostlist_destroy);
strong_alias(hostlist_find,		slurm_hostlist_find);
strong_alias(hostlist_iterator_create,	slurm_hostlist_iterator_create);
strong_alias(hostlist_iterator_destroy,	slurm_hostlist_iterator_destroy);
strong_alias(hostlist_iterator_reset,	slurm_hostlist_iterator_reset);
strong_alias(hostlist_next,		slurm_hostlist_next);
strong_alias(hostlist_next_range,	slurm_hostlist_next_range);
strong_alias(hostlist_nth,		slurm_hostlist_nth);
strong_alias(hostlist_pop,		slurm_hostlist_pop);
strong_alias(hostlist_pop_range,	slurm_hostlist_pop_range);
strong_alias(hostlist_push,		slurm_hostlist_push);
strong_alias(hostlist_push_host,	slurm_hostlist_push_host);
strong_alias(hostlist_push_list,	slurm_hostlist_push_list);
strong_alias(hostlist_ranged_string,	slurm_hostlist_ranged_string);
strong_alias(hostlist_remove,		slurm_hostlist_remove);
strong_alias(hostlist_shift,		slurm_hostlist_shift);
strong_alias(hostlist_shift_range,	slurm_hostlist_shift_range);
strong_alias(hostlist_sort,		slurm_hostlist_soft);
strong_alias(hostlist_uniq,		slurm_hostlist_uniq);
strong_alias(hostset_copy,		slurm_hostset_copy);
strong_alias(hostset_count,		slurm_hostset_count);
strong_alias(hostset_create,		slurm_hostset_create);
strong_alias(hostset_delete,		slurm_hostset_delete);
strong_alias(hostset_destroy,		slurm_hostset_destroy);
strong_alias(hostset_find,		slurm_hostset_find);
strong_alias(hostset_insert,		slurm_hostset_insert);
strong_alias(hostset_shift,		slurm_hostset_shift);
strong_alias(hostset_shift_range,	slurm_hostset_shift_range);
strong_alias(hostset_within,		slurm_hostset_within);
strong_alias(hostset_nth,		slurm_hostset_nth);

/*
 * lsd_fatal_error : fatal error macro
 */
#ifdef WITH_LSD_FATAL_ERROR_FUNC
#  undef lsd_fatal_error
   extern void lsd_fatal_error(char *file, int line, char *mesg);
#else /* !WITH_LSD_FATAL_ERROR_FUNC */
#  ifndef lsd_fatal_error
#    define lsd_fatal_error(file, line, mesg)                                \
       do {                                                                  \
           fprintf(stderr, "ERROR: [%s:%d] %s: %s\n",                        \
           file, line, mesg, strerror(errno));                               \
       } while (0)
#  endif /* !lsd_fatal_error */
#endif /* !WITH_LSD_FATAL_ERROR_FUNC */

/*
 * lsd_nonmem_error
 */
#ifdef WITH_LSD_NOMEM_ERROR_FUNC
#  undef lsd_nomem_error
   extern void * lsd_nomem_error(char *file, int line, char *mesg);
#else /* !WITH_LSD_NOMEM_ERROR_FUNC */
#  ifndef lsd_nomem_error
#    define lsd_nomem_error(file, line, mesg) (NULL)
#  endif /* !lsd_nomem_error */
#endif /* !WITH_LSD_NOMEM_ERROR_FUNC */

/*
 * OOM helper function
 *  Automatically call lsd_nomem_error with appropriate args
 *  and set errno to ENOMEM
 */
#define out_of_memory(mesg)                                                  \
    do {                                                                     \
        fatal("malloc failure");                                             \
        errno = ENOMEM;                                                      \
        return(lsd_nomem_error(__FILE__, __LINE__, mesg));                   \
    } while (0)

/* 
 * Some constants and tunables:
 */

/* number of elements to allocate when extending the hostlist array */
#define HOSTLIST_CHUNK    16

/* max host range: anything larger will be assumed to be an error */
#define MAX_RANGE    16384    /* 16K Hosts */

/* max number of ranges that will be processed between brackets */
#define MAX_RANGES    12288    /* 12K Ranges */

/* size of internal hostname buffer (+ some slop), hostnames will probably
 * be truncated if longer than MAXHOSTNAMELEN */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN    64
#endif

/* ----[ Internal Data Structures ]---- */


char *alpha_num = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

#if (SYSTEM_DIMENSIONS > 1)
enum {A, B, C, D};

/* logic for block node description */
/* We allocate space for three digits, 
 * each with values 0 to Z even if they are not all used */
#if (SYSTEM_DIMENSIONS == 3)
static bool axis[HOSTLIST_BASE][HOSTLIST_BASE][HOSTLIST_BASE];
#endif

#if (SYSTEM_DIMENSIONS == 4)
static bool axis[HOSTLIST_BASE][HOSTLIST_BASE][HOSTLIST_BASE][HOSTLIST_BASE];
#endif

static int axis_min[SYSTEM_DIMENSIONS];
static int axis_max[SYSTEM_DIMENSIONS];
static int box_length = (SYSTEM_DIMENSIONS * 2) + 2;

static void _parse_int_to_array(int in, int out[SYSTEM_DIMENSIONS]);
static int _get_boxes(char *buf, int max_len);
static void _clear_grid(void);
static void _set_grid(unsigned long start, unsigned long end);
static bool _test_box(void);
#endif

/* hostname type: A convenience structure used in parsing single hostnames */
struct hostname_components {
    char *hostname;         /* cache of initialized hostname        */
    char *prefix;           /* hostname prefix                      */
    unsigned long num;      /* numeric suffix                       */

    /* string representation of numeric suffix
     * points into `hostname'                                       */
    char *suffix;
};

typedef struct hostname_components *hostname_t;

/* hostrange type: A single prefix with `hi' and `lo' numeric suffix values */
struct hostrange_components {
    char *prefix;        /* alphanumeric prefix: */

    /* beginning (lo) and end (hi) of suffix range */
    unsigned long lo, hi;

    /* width of numeric output format
     * (pad with zeros up to this width) */
    int width;

    /* If singlehost is 1, `lo' and `hi' are invalid */
    unsigned singlehost:1;
};

typedef struct hostrange_components *hostrange_t;

/* The hostlist type: An array based list of hostrange_t's */
struct hostlist {
#ifndef NDEBUG
#define HOSTLIST_MAGIC    57005
    int magic;
#endif
#if    WITH_PTHREADS
    pthread_mutex_t mutex;
#endif                /* WITH_PTHREADS */

    /* current number of elements available in array */
    int size;

    /* current number of ranges stored in array */
    int nranges;

    /* current number of hosts stored in hostlist */
    int nhosts;

    /* pointer to hostrange array */
    hostrange_t *hr;

    /* list of iterators */
    struct hostlist_iterator *ilist;

};


/* a hostset is a wrapper around a hostlist */
struct hostset {
    hostlist_t hl;
};

struct hostlist_iterator {
#ifndef NDEBUG
    int magic;
#endif
    /* hostlist we are traversing */
    hostlist_t hl;

    /* current index of iterator in hl->hr[] */
    int idx;

    /* current hostrange object in list hl, i.e. hl->hr[idx] */
    hostrange_t hr;

    /* current depth we've traversed into range hr */
    int depth;

    /* next ptr for lists of iterators */
    struct hostlist_iterator *next;
};


/* ---- ---- */

/* ------[ static function prototypes ]------ */

static void _error(char *file, int line, char *mesg, ...);
static char * _next_tok(char *, char **);
static int    _zero_padded(unsigned long, int);
static int    _width_equiv(unsigned long, int *, unsigned long, int *);

static int           host_prefix_end(const char *);
static hostname_t    hostname_create(const char *);
static void          hostname_destroy(hostname_t);
static int           hostname_suffix_is_valid(hostname_t);
static int           hostname_suffix_width(hostname_t);

static hostrange_t   hostrange_new(void);
static hostrange_t   hostrange_create_single(const char *);
static hostrange_t   hostrange_create(char *, unsigned long, unsigned long,
				int);
static unsigned long hostrange_count(hostrange_t);
static hostrange_t   hostrange_copy(hostrange_t);
static void          hostrange_destroy(hostrange_t);
static hostrange_t   hostrange_delete_host(hostrange_t, unsigned long);
static int           hostrange_cmp(hostrange_t, hostrange_t);
static int           hostrange_prefix_cmp(hostrange_t, hostrange_t);
static int           hostrange_within_range(hostrange_t, hostrange_t);
static int           hostrange_width_combine(hostrange_t, hostrange_t);
static int           hostrange_empty(hostrange_t);
static char *        hostrange_pop(hostrange_t);
static char *        hostrange_shift(hostrange_t);
static int           hostrange_join(hostrange_t, hostrange_t);
static hostrange_t   hostrange_intersect(hostrange_t, hostrange_t);
static int           hostrange_hn_within(hostrange_t, hostname_t);
static size_t        hostrange_to_string(hostrange_t hr, size_t, char *, 
				char *);
static size_t        hostrange_numstr(hostrange_t, size_t, char *);

static hostlist_t  hostlist_new(void);
static hostlist_t _hostlist_create_bracketed(const char *, char *, char *);
static int         hostlist_resize(hostlist_t, size_t);
static int         hostlist_expand(hostlist_t);
static int         hostlist_push_range(hostlist_t, hostrange_t);
static int         hostlist_push_hr(hostlist_t, char *, unsigned long,
                                    unsigned long, int);
static int         hostlist_insert_range(hostlist_t, hostrange_t, int);
static void        hostlist_delete_range(hostlist_t, int n);
static void        hostlist_coalesce(hostlist_t hl);
static void        hostlist_collapse(hostlist_t hl);
static hostlist_t _hostlist_create(const char *, char *, char *);
static void        hostlist_shift_iterators(hostlist_t, int, int, int);
static int        _attempt_range_join(hostlist_t, int);
static int        _is_bracket_needed(hostlist_t, int);

static hostlist_iterator_t hostlist_iterator_new(void);
static void               _iterator_advance(hostlist_iterator_t);
static void               _iterator_advance_range(hostlist_iterator_t);

static int hostset_find_host(hostset_t, const char *);

/* ------[ macros ]------ */

#ifdef WITH_PTHREADS
#  define mutex_init(mutex)                                                  \
	do {                                                                    \
		int e = pthread_mutex_init(mutex, NULL);                             \
		if (e) {                                                             \
			errno = e;                                                       \
			lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex init:");     \
			abort();                                                         \
		}                                                                    \
	} while (0)

#  define mutex_lock(mutex)                                                  \
	do {                                                                    \
 		int e = pthread_mutex_lock(mutex);                                   \
		if (e) {                                                             \
			errno = e;                                                        \
			lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex lock:");      \
 			abort();                                                          \
		}                                                                    \
	} while (0)

#  define mutex_unlock(mutex)                                                \
	do {                                                                    \
		int e = pthread_mutex_unlock(mutex);                                 \
		if (e) {                                                             \
			errno = e;                                                       \
			lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex unlock:");   \
			abort();                                                         \
		}                                                                    \
	} while (0)

#  define mutex_destroy(mutex)                                               \
	do {                                                                    \
		int e = pthread_mutex_destroy(mutex);                                \
		if (e) {                                                             \
			errno = e;                                                       \
			lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex destroy:");  \
			abort();                                                         \
		}                                                                    \
	} while (0)

#else                /* !WITH_PTHREADS */

#  define mutex_init(mutex)
#  define mutex_lock(mutex)
#  define mutex_unlock(mutex)
#  define mutex_destroy(mutex)

#endif                /* WITH_PTHREADS */

#define LOCK_HOSTLIST(_hl)                                                   \
	do {                                                                   \
		assert(_hl != NULL);                                               \
		mutex_lock(&(_hl)->mutex);                                         \
		assert((_hl)->magic == HOSTLIST_MAGIC);                            \
	} while (0)

#define UNLOCK_HOSTLIST(_hl)                                                 \
	do {                                                                   \
		mutex_unlock(&(_hl)->mutex);                                       \
	} while (0)                       

#define seterrno_ret(_errno, _rc)                                            \
	do {                                                                   \
		errno = _errno;                                                    \
		return _rc;                                                        \
	} while (0)

/* ------[ Function Definitions ]------ */

/* ----[ general utility functions ]---- */


/*
 *  Varargs capable error reporting via lsd_fatal_error()
 */
static void _error(char *file, int line, char *msg, ...)
{
	va_list ap;
	char    buf[1024];
	int     len = 0;
	va_start(ap, msg);

	len = vsnprintf(buf, 1024, msg, ap);
	if ((len < 0) || (len > 1024)) 
		buf[1023] = '\0';

	lsd_fatal_error(file, line, buf);

	va_end(ap);
	return;
}

/* 
 * Helper function for host list string parsing routines 
 * Returns a pointer to the next token; additionally advance *str
 * to the next separator.
 *
 * next_tok was taken directly from pdsh courtesy of Jim Garlick.
 * (with modifications to support bracketed hostlists, i.e.:
 *  xxx[xx,xx,xx] is a single token)
 *
 */
static char * _next_tok(char *sep, char **str)
{
	char *tok;

	/* push str past any leading separators */
	while ((**str != '\0') && (strchr(sep, **str) != NULL))
		(*str)++;

	if (**str == '\0')
		return NULL;

	/* assign token ptr */
	tok = *str;

	/* push str past token and leave pointing to first separator */
	while ((**str != '\0') && (strchr(sep, **str) == NULL))
		(*str)++;

	/* if _single_ opening bracket exists b/w tok and str,
	 * push str past first closing bracket */
	if ((memchr(tok, '[', *str - tok) != NULL) &&
	    (memchr(tok, ']', *str - tok) == NULL)) {
		char *q = strchr(*str, ']');
		if (q && (memchr(*str, '[', q - *str) == NULL))
			*str = ++q;

		/* push str past token and leave pointing to next separator */
		while ((**str != '\0') && (strchr(sep, **str) == NULL))
			(*str)++;

		/* if _second_ opening bracket exists b/w tok and str,
		 * push str past second closing bracket */
		if ((**str != '\0') &&
		    (memchr(tok, '[', *str - q) != NULL) &&
		    (memchr(tok, ']', *str - q) == NULL)) {
			q = strchr(*str, ']');
			if (q && (memchr(*str, '[', q - *str) == NULL))
				*str = q + 1;
		}
	}

	/* nullify consecutive separators and push str beyond them */
	while ((**str != '\0') && (strchr(sep, **str) != '\0'))
		*(*str)++ = '\0';

	return tok;
}


/* return the number of zeros needed to pad "num" to "width"
 */
static int _zero_padded(unsigned long num, int width)
{
	int n = 1;
	while (num /= 10L)
		n++;
	return width > n ? width - n : 0;
}

/* test whether two format `width' parameters are "equivalent"
 * The width arguments "wn" and "wm" for integers "n" and "m" 
 * are equivalent if:
 *  
 *  o  wn == wm  OR
 *
 *  o  applying the same format width (either wn or wm) to both of  
 *     'n' and 'm' will not change the zero padding of *either* 'm' nor 'n'.
 *
 *  If this function returns 1 (or true), the appropriate width value
 *  (either 'wm' or 'wn') will have been adjusted such that both format
 *  widths are equivalent.
 */
static int _width_equiv(unsigned long n, int *wn, unsigned long m, int *wm)
{
	int npad, nmpad, mpad, mnpad;

	if (wn == wm)
		return 1;

	npad = _zero_padded(n, *wn);
	nmpad = _zero_padded(n, *wm);
	mpad = _zero_padded(m, *wm);
	mnpad = _zero_padded(m, *wn);

	if (npad != nmpad && mpad != mnpad)
		return 0;

	if (npad != nmpad) {
		if (mpad == mnpad) {
			*wm = *wn;
			return 1;
		} else
			return 0;
	} else {        /* mpad != mnpad */
		if (npad == nmpad) {
			*wn = *wm;
			return 1;
		} else
			return 0;
	}

	/* not reached */
}


/* ----[ hostname_t functions ]---- */

/* 
 * return the location of the last char in the hostname prefix
 */
static int host_prefix_end(const char *hostname)
{
	int idx, len;

	assert(hostname != NULL);

	len = strlen(hostname);
#ifdef HAVE_3D
	if (len < 4)
		return -1;
	idx = len - 4;
#else
	if (len < 1)
		return -1;
	idx = len - 1;

	while (idx >= 0 && isdigit((char) hostname[idx])) 
		idx--;
#endif
	return idx;
}

/* 
 * create a hostname_t object from a string hostname
 */
static hostname_t hostname_create(const char *hostname)
{
	hostname_t hn = NULL;
	char *p = '\0';
	int idx = 0;

	assert(hostname != NULL);

	if (!(hn = (hostname_t) malloc(sizeof(*hn))))
  		out_of_memory("hostname create");

	idx = host_prefix_end(hostname);

	if (!(hn->hostname = strdup(hostname))) {
		free(hn);
		out_of_memory("hostname create");
	}

	hn->num = 0;
	hn->prefix = NULL;
	hn->suffix = NULL;

	if (idx == (strlen(hostname) - 1)) {
		if ((hn->prefix = strdup(hostname)) == NULL) {
			hostname_destroy(hn);
			out_of_memory("hostname prefix create");
		}
		return hn;
	}

	hn->suffix = hn->hostname + idx + 1;

	hn->num = strtoul(hn->suffix, &p, HOSTLIST_BASE);

	if (*p == '\0') {
		if (!(hn->prefix = malloc((idx + 2) * sizeof(char)))) {
			hostname_destroy(hn);
			out_of_memory("hostname prefix create");
		}
		memcpy(hn->prefix, hostname, idx + 1);
		hn->prefix[idx + 1] = '\0';
	} else {
		if (!(hn->prefix = strdup(hostname))) {
			hostname_destroy(hn);
			out_of_memory("hostname prefix create");
		}
		hn->suffix = NULL;
	}

	return hn;
}

/* free a hostname object
 */
static void hostname_destroy(hostname_t hn)
{
	if (hn == NULL)
		return;
	hn->suffix = NULL;
	if (hn->hostname)
		free(hn->hostname);
	if (hn->prefix)
		free(hn->prefix);
	free(hn);
}

/* return true if the hostname has a valid numeric suffix 
 */
static int hostname_suffix_is_valid(hostname_t hn)
{
	if (!hn)
		return false;
	return hn->suffix != NULL;
}

/* return the width (in characters) of the numeric part of the hostname
 */
static int hostname_suffix_width(hostname_t hn)
{
	if (!hn)
		return -1;
	assert(hn->suffix != NULL);
	return (int) strlen(hn->suffix);
}


/* ----[ hostrange_t functions ]---- */

/* allocate a new hostrange object 
 */
static hostrange_t hostrange_new(void)
{
	hostrange_t new = (hostrange_t) malloc(sizeof(*new));
	if (!new) 
		out_of_memory("hostrange create");
	return new;
}

/* Create a hostrange_t containing a single host without a valid suffix
 * hr->prefix will represent the entire hostname.
 */
static hostrange_t hostrange_create_single(const char *prefix)
{
	hostrange_t new;

	assert(prefix != NULL);

	if ((new = hostrange_new()) == NULL)
		goto error1;

	if ((new->prefix = strdup(prefix)) == NULL)
		goto error2;

	new->singlehost = 1;
	new->lo = 0L;
	new->hi = 0L;
	new->width = 0;

	return new;

  error2:
	free(new);
  error1:
	out_of_memory("hostrange create single");
}


/* Create a hostrange object with a prefix, hi, lo, and format width
 */
static hostrange_t
hostrange_create(char *prefix, unsigned long lo, unsigned long hi, int width)
{
	hostrange_t new;

	assert(prefix != NULL);

	if ((new = hostrange_new()) == NULL)
		goto error1;

	if ((new->prefix = strdup(prefix)) == NULL)
		goto error2;

	new->lo = lo;
	new->hi = hi;
	new->width = width;

	new->singlehost = 0;

	return new;

  error2:
	free(new);
  error1:
	out_of_memory("hostrange create");
}


/* Return the number of hosts stored in the hostrange object
 */
static unsigned long hostrange_count(hostrange_t hr)
{
	assert(hr != NULL);
	if (hr->singlehost)
		return 1;
	else
		return hr->hi - hr->lo + 1;
}

/* Copy a hostrange object
 */
static hostrange_t hostrange_copy(hostrange_t hr)
{
	assert(hr != NULL);

	if (hr->singlehost)
		return hostrange_create_single(hr->prefix);
	else
		return hostrange_create(hr->prefix, hr->lo, hr->hi,
			hr->width);
}


/* free memory allocated by the hostrange object
 */
static void hostrange_destroy(hostrange_t hr)
{
	if (hr == NULL)
		return;
	if (hr->prefix)
		free(hr->prefix);
	free(hr);
}

/* hostrange_delete_host() deletes a specific host from the range.
 * If the range is split into two, the greater range is returned,
 * and `hi' of the lesser range is adjusted accordingly. If the
 * highest or lowest host is deleted from a range, NULL is returned
 * and the hostrange hr is adjusted properly.
 */
static hostrange_t hostrange_delete_host(hostrange_t hr, unsigned long n)
{
	hostrange_t new = NULL;

	assert(hr != NULL);
	assert(n >= hr->lo && n <= hr->hi);

	if (n == hr->lo)
		hr->lo++;
	else if (n == hr->hi)
		hr->hi--;
	else {
		if (!(new = hostrange_copy(hr)))
			out_of_memory("hostrange copy");
		hr->hi = n - 1;
		new->lo = n + 1;
	}

	return new;
}

/* hostrange_cmp() is used to sort hostrange objects. It will
 * sort based on the following (in order):
 *  o result of strcmp on prefixes
 *  o if widths are compatible, then: 
 *       sort based on lowest suffix in range
 *    else
 *       sort based on width                     */
static int hostrange_cmp(hostrange_t h1, hostrange_t h2)
{
	int retval;

	assert(h1 != NULL);
	assert(h2 != NULL);

	if ((retval = hostrange_prefix_cmp(h1, h2)) == 0)
		retval = hostrange_width_combine(h1, h2) ?
			h1->lo - h2->lo : h1->width - h2->width;

	return retval;
}


/* compare the prefixes of two hostrange objects. 
 * returns:
 *    < 0   if h1 prefix is less than h2 OR h2 == NULL.
 *
 *      0   if h1's prefix and h2's prefix match, 
 *          UNLESS, either h1 or h2 (NOT both) do not have a valid suffix.
 *
 *    > 0   if h1's prefix is greater than h2's OR h1 == NULL. */
static int hostrange_prefix_cmp(hostrange_t h1, hostrange_t h2)
{
	int retval;
	if (h1 == NULL)
		return 1;
	if (h2 == NULL)
		return -1;

	retval = strcmp(h1->prefix, h2->prefix);
	return retval == 0 ? h2->singlehost - h1->singlehost : retval;
}

/* returns true if h1 and h2 would be included in the same bracketed hostlist.
 * h1 and h2 will be in the same bracketed list iff:
 *
 *  1. h1 and h2 have same prefix
 *  2. neither h1 nor h2 are singlet hosts (i.e. invalid suffix)
 *
 *  (XXX: Should incompatible widths be placed in the same bracketed list?
 *        There's no good reason not to, except maybe aesthetics)
 */
static int hostrange_within_range(hostrange_t h1, hostrange_t h2)
{
	if (hostrange_prefix_cmp(h1, h2) == 0)
		return h1->singlehost || h2->singlehost ? 0 : 1;
	else
		return 0;
}


/* compare two hostrange objects to determine if they are width 
 * compatible,  returns:
 *  1 if widths can safely be combined
 *  0 if widths cannot be safely combined
 */
static int hostrange_width_combine(hostrange_t h0, hostrange_t h1)
{
	assert(h0 != NULL);
	assert(h1 != NULL);

	return _width_equiv(h0->lo, &h0->width, h1->lo, &h1->width);
}


/* Return true if hostrange hr contains no hosts, i.e. hi < lo
 */
static int hostrange_empty(hostrange_t hr)
{
	assert(hr != NULL);
	return ((hr->hi < hr->lo) || (hr->hi == (unsigned long) -1));
}

/* return the string representation of the last host in hostrange hr
 * and remove that host from the range (i.e. decrement hi if possible)
 *
 * Returns NULL if malloc fails OR there are no more hosts left
 */
static char *hostrange_pop(hostrange_t hr)
{
	size_t size = 0;
	char *host = NULL;
	assert(hr != NULL);

	if (hr->singlehost) {
		hr->lo++;    /* effectively set count == 0 */
		host = strdup(hr->prefix);
	} else if (hostrange_count(hr) > 0) {
		size = strlen(hr->prefix) + hr->width + 16;    
		if (!(host = (char *) malloc(size * sizeof(char))))
			out_of_memory("hostrange pop");
#ifdef HAVE_3D
		if (hr->width == 3) {
			int len = 0;
			int i2=0;
			int coord[SYSTEM_DIMENSIONS];

			_parse_int_to_array(hr->hi, coord);

			len = snprintf(host, size, "%s", hr->prefix);
			while(i2 < SYSTEM_DIMENSIONS) {
				if(len <= size)
					host[len++] = alpha_num[coord[i2++]];
			}
			hr->hi--;
		} else {
			snprintf(host, size, "%s%0*lu", hr->prefix, 
				 hr->width, hr->hi--);
		}
#else
		snprintf(host, size, "%s%0*lu", hr->prefix, 
			 hr->width, hr->hi--);
#endif
	}

	return host;
}

/* Same as hostrange_pop(), but remove host from start of range */
static char *hostrange_shift(hostrange_t hr)
{
	size_t size = 0;
	char *host = NULL;

	assert(hr != NULL);

	if (hr->singlehost) {
		hr->lo++;
		if (!(host = strdup(hr->prefix)))
			out_of_memory("hostrange shift");
	} else if (hostrange_count(hr) > 0) {
		size = strlen(hr->prefix) + hr->width + 16;
		if (!(host = (char *) malloc(size * sizeof(char))))
			out_of_memory("hostrange shift");
#ifdef HAVE_3D
		if (hr->width == 3) {
			int len = 0;
			int i2=0;
			int coord[SYSTEM_DIMENSIONS];

			_parse_int_to_array(hr->lo, coord);

			len = snprintf(host, size, "%s", hr->prefix);
			while(i2 < SYSTEM_DIMENSIONS) {
				if(len <= size)
					host[len++] = alpha_num[coord[i2++]];
			}
			hr->lo++;
		} else {
			snprintf(host, size, "%s%0*lu", hr->prefix,
				 hr->width, hr->lo++);
		}
#else		
		snprintf(host, size, "%s%0*lu", hr->prefix,
			hr->width, hr->lo++);
#endif
	}

	return host;
}


/* join two hostrange objects.
 *
 * returns:
 *
 * -1 if ranges do not overlap (including incompatible zero padding)
 *  0 if ranges join perfectly
 * >0 number of hosts that were duplicated in  h1 and h2 
 *
 * h2 will be coalesced into h1 if rc >= 0
 *
 * it is assumed that h1->lo <= h2->lo, i.e. hr1 <= hr2
 *
 */
static int hostrange_join(hostrange_t h1, hostrange_t h2)
{
	int duplicated = -1;

	assert(h1 != NULL);
	assert(h2 != NULL);
	assert(hostrange_cmp(h1, h2) <= 0);

	if (hostrange_prefix_cmp(h1, h2) == 0 &&
	    hostrange_width_combine(h1, h2)) {

		if (h1->singlehost && h2->singlehost) {    /* matching singlets  */
			duplicated = 1;
		} else if (h1->hi == h2->lo - 1) {    /* perfect join       */
			h1->hi = h2->hi;
			duplicated = 0;
		} else if (h1->hi >= h2->lo) {    /* some duplication   */
			if (h1->hi < h2->hi) {
				duplicated = h1->hi - h2->lo + 1;
				h1->hi = h2->hi;
			} else
				duplicated = hostrange_count(h2);
		}
	}

	return duplicated;
}

/* hostrange intersect returns the intersection (common hosts)
 * of hostrange objects h1 and h2. If there is no intersection,
 * NULL is returned.
 *
 * It is assumed that h1 <= h2 (i.e. h1->lo <= h2->lo)
 */
static hostrange_t hostrange_intersect(hostrange_t h1, hostrange_t h2)
{
	hostrange_t new = NULL;

	assert(h1 != NULL);
	assert(h2 != NULL);

	if (h1->singlehost || h2->singlehost)
		return NULL;

	assert(hostrange_cmp(h1, h2) <= 0);

	if ((hostrange_prefix_cmp(h1, h2) == 0)
	&& (h1->hi > h2->lo) 
	&& (hostrange_width_combine(h1, h2))) {

		if (!(new = hostrange_copy(h1)))
			return NULL;
		new->lo = h2->lo;
		new->hi = h2->hi < h1->hi ? h2->hi : h1->hi;
	}

	return new;
}

/* return 1 if hostname hn is within the hostrange hr
 *        0 if not.
 */
static int hostrange_hn_within(hostrange_t hr, hostname_t hn)
{
	if (hr->singlehost) {
		/*  
		 *  If the current hostrange [hr] is a `singlehost' (no valid 
		 *   numeric suffix (lo and hi)), then the hostrange [hr]
		 *   stores just one host with name == hr->prefix.
		 *  
		 *  Thus the full hostname in [hn] must match hr->prefix, in
		 *   which case we return true. Otherwise, there is no 
		 *   possibility that [hn] matches [hr].
		 */
		if (strcmp (hn->hostname, hr->prefix) == 0)
			return 1;
		else 
			return 0;
	}

	/*
	 *  Now we know [hr] is not a "singlehost", so hostname
	 *   better have a valid numeric suffix, or there is no 
	 *   way we can match
	 */
	if (!hostname_suffix_is_valid (hn))
		return 0;

	/*
	 *  If hostrange and hostname prefixes don't match, then
	 *   there is way the hostname falls within the range [hr].
	 */
	if (strcmp(hr->prefix, hn->prefix) != 0) 
		return 0;

	/*
	 *  Finally, check whether [hn], with a valid numeric suffix,
	 *   falls within the range of [hr].
	 */
	if (hn->num <= hr->hi && hn->num >= hr->lo) {
			int width = hostname_suffix_width(hn);
			int num = hn->num;
		return (_width_equiv(hr->lo, &hr->width, num, &width));
	}

	return 0;
}


/* copy a string representation of the hostrange hr into buffer buf,
 * writing at most n chars including NUL termination
 */
static size_t
hostrange_to_string(hostrange_t hr, size_t n, char *buf, char *separator)
{
	unsigned long i;
	int truncated = 0;
	int len = 0;
	char sep = separator == NULL ? ',' : separator[0];

	if (n == 0)
		return 0;
	
	assert(hr != NULL);

	if (hr->singlehost)
		return snprintf(buf, n, "%s", hr->prefix);

	for (i = hr->lo; i <= hr->hi; i++) {
		size_t m = (n - len) <= n ? n - len : 0; /* check for < 0 */
		int ret = 0;
#ifdef HAVE_3D
		if (hr->width == 3) {
			int i2=0;
			int coord[SYSTEM_DIMENSIONS];

			_parse_int_to_array(i, coord);

			len += snprintf(buf+len, m, "%s", hr->prefix);
			while(i2 < SYSTEM_DIMENSIONS) {
				if(len <= n)
					buf[len++] = alpha_num[coord[i2++]];
			}
			ret = len;
		} else {
			ret = snprintf(buf + len, m, "%s%0*lu",
				       hr->prefix, hr->width, i);
		}
#else		
		ret = snprintf(buf + len, m, "%s%0*lu",
			       hr->prefix, hr->width, i);
#endif
		if (ret < 0 || ret >= m) {
			len = n;
			truncated = 1;
			break;
		}
		len+=ret;
		buf[len++] = sep;
	}

	if (truncated) {
		buf[n-1] = '\0';
		return -1;
	} else {
		/* back up over final separator */
		buf[--len] = '\0';
		return len;
	}
}

/* Place the string representation of the numeric part of hostrange into buf
 * writing at most n chars including NUL termination.
 */
static size_t hostrange_numstr(hostrange_t hr, size_t n, char *buf)
{
	int len = 0;
	assert(buf != NULL);
	assert(hr != NULL);

	if (hr->singlehost || n == 0)
		return 0;

#ifdef HAVE_3D
	if (hr->width == 3) {
		int i2=0;
		int coord[SYSTEM_DIMENSIONS];
		
		_parse_int_to_array(hr->lo, coord);
		
		while(i2 < SYSTEM_DIMENSIONS) {
			if(len <= n)
				buf[len++] = alpha_num[coord[i2++]];
		}
	} else {
		len = snprintf(buf, n, "%0*lu", hr->width, hr->lo);
	}
#else		
	len = snprintf(buf, n, "%0*lu", hr->width, hr->lo);
#endif

	if ((len >= 0) && (len < n) && (hr->lo < hr->hi)) {
		int len2 = 0;
#ifdef HAVE_3D
		if (hr->width == 3) {
			int i2=0;
			int coord[SYSTEM_DIMENSIONS];
			
			_parse_int_to_array(hr->hi, coord);

			buf[len++] = '-';
			while(i2 < SYSTEM_DIMENSIONS) {
				if(len <= n)
					buf[len++] = alpha_num[coord[i2++]];
			}
			len2 = len;
		} else {
			len2 = snprintf(buf+len, n-len, "-%0*lu", 
					hr->width, hr->hi);
		}
#else				
		len2 = snprintf(buf+len, n-len, "-%0*lu", hr->width, hr->hi);
#endif
		if (len2 < 0) 
			len = -1;
		else
			len += len2;
	}

	return len;
}


/* ----[ hostlist functions ]---- */

/* Create a new hostlist object. 
 * Returns an empty hostlist, or NULL if memory allocation fails.
 */
static hostlist_t hostlist_new(void)
{
	int i;
	hostlist_t new = (hostlist_t) malloc(sizeof(*new));
	if (!new)
		goto fail1;

	assert(new->magic = HOSTLIST_MAGIC);
	mutex_init(&new->mutex);

	new->hr = (hostrange_t *) malloc(HOSTLIST_CHUNK * sizeof(hostrange_t));
	if (!new->hr)
		goto fail2;

	/* set entries in hostrange array to NULL */
	for (i = 0; i < HOSTLIST_CHUNK; i++)
		new->hr[i] = NULL;

	new->size = HOSTLIST_CHUNK;
	new->nranges = 0;
	new->nhosts = 0;
	new->ilist = NULL;
	return new;

  fail2:
	free(new);
  fail1:
	out_of_memory("hostlist_create");
}


/* Resize the internal array used to store the list of hostrange objects.
 *
 * returns 1 for a successful resize,
 *         0 if call to _realloc fails    
 *
 * It is assumed that the caller has the hostlist hl locked 
 */
static int hostlist_resize(hostlist_t hl, size_t newsize)
{
	int i;
	size_t oldsize;
	assert(hl != NULL);
	assert(hl->magic == HOSTLIST_MAGIC);
	oldsize = hl->size;
	hl->size = newsize;
	hl->hr = realloc((void *) hl->hr, hl->size*sizeof(hostrange_t));
	if (!(hl->hr)) 
		return 0;

	for (i = oldsize; i < newsize; i++)
		hl->hr[i] = NULL;

	return 1;
}

/* Resize hostlist by one HOSTLIST_CHUNK
 * Assumes that hostlist hl is locked by caller
 */
static int hostlist_expand(hostlist_t hl)
{
	if (!hostlist_resize(hl, hl->size + HOSTLIST_CHUNK))
		return 0;
	else
		return 1;
}

/* Push a hostrange object onto hostlist hl
 * Returns the number of hosts successfully pushed onto hl
 * or -1 if there was an error allocating memory
 */
static int hostlist_push_range(hostlist_t hl, hostrange_t hr)
{
	hostrange_t tail;
	int retval;

	assert(hr != NULL);
	LOCK_HOSTLIST(hl);

	tail = (hl->nranges > 0) ? hl->hr[hl->nranges-1] : hl->hr[0];

	if (hl->size == hl->nranges && !hostlist_expand(hl))
		goto error;

	if (hl->nranges > 0
	&& hostrange_prefix_cmp(tail, hr) == 0
	&& tail->hi == hr->lo - 1
	&& hostrange_width_combine(tail, hr)) {
		tail->hi = hr->hi;
	} else {
		hostrange_t new = hostrange_copy(hr);
		if (new == NULL)
			goto error;
		hl->hr[hl->nranges++] = new;
	}

	retval = hl->nhosts += hostrange_count(hr);

	UNLOCK_HOSTLIST(hl);

	return retval;

  error:
	UNLOCK_HOSTLIST(hl);
	return -1;
}



/* Same as hostlist_push_range() above, but prefix, lo, hi, and width
 * are passed as args 
 */
static int
hostlist_push_hr(hostlist_t hl, char *prefix, unsigned long lo,
	unsigned long hi, int width)
{
	hostrange_t hr = hostrange_create(prefix, lo, hi, width);
	int retval = hostlist_push_range(hl, hr);
	hostrange_destroy(hr);
	return retval;
}

/* Insert a range object hr into position n of the hostlist hl
 * Assumes that hl->mutex is already held by calling process
 */
static int hostlist_insert_range(hostlist_t hl, hostrange_t hr, int n)
{
	int i;
	hostrange_t tmp;
	hostlist_iterator_t hli;

	assert(hl != NULL);
	assert(hl->magic == HOSTLIST_MAGIC);
	assert(hr != NULL);

	if (n > hl->nranges)
		return 0;

	if (hl->size == hl->nranges && !hostlist_expand(hl))
		return 0;

	/* copy new hostrange into slot "n" in array */
	tmp = hl->hr[n];
	hl->hr[n] = hostrange_copy(hr);

	/* push remaining hostrange entries up */
	for (i = n + 1; i < hl->nranges + 1; i++) {
		hostrange_t last = hl->hr[i];
		hl->hr[i] = tmp;
		tmp = last;
	}
	hl->nranges++;

	/* adjust hostlist iterators if needed */
	for (hli = hl->ilist; hli; hli = hli->next) {
		if (hli->idx >= n)
			hli->hr = hli->hl->hr[++hli->idx];
	}

	return 1;
}

/* Delete the range at position n in the range array
 * Assumes the hostlist lock is already held.
 */
static void hostlist_delete_range(hostlist_t hl, int n)
{
	int i;
	hostrange_t old;

	assert(hl != NULL);
	assert(hl->magic == HOSTLIST_MAGIC);
	assert(n < hl->nranges && n >= 0);

	old = hl->hr[n];
	for (i = n; i < hl->nranges - 1; i++)
		hl->hr[i] = hl->hr[i + 1];
	hl->nranges--;
	hl->hr[hl->nranges] = NULL;
	hostlist_shift_iterators(hl, n, 0, 1);

	/* XXX caller responsible for adjusting nhosts */
	/* hl->nhosts -= hostrange_count(old) */

	hostrange_destroy(old);
}

#if WANT_RECKLESS_HOSTRANGE_EXPANSION

/* The reckless hostrange expansion function.
 * See comment in hostlist.h:hostlist_create() for more info on
 * the different choices for hostlist notation.
 */
hostlist_t _hostlist_create(const char *hostlist, char *sep, char *r_op)
{
	char *str, *orig;
	char *tok, *cur;
	int high, low, fmt = 0;
	char prefix[256] = "";
	int pos = 0;
	int error = 0;
	char range_op = r_op[0];/* XXX support > 1 char range ops in future? */

	hostlist_t new = hostlist_new();

	if (hostlist == NULL)
		return new;
#ifdef HAVE_3D
	fatal("WANT_RECKLESS_HOSTRANGE_EXPANSION does not work on "
	      "Bluegene or Sun Constellation systems!!!!");
#endif
	orig = str = strdup(hostlist);
	
	/* return an empty list if an empty string was passed in */
	if (str == NULL || strlen(str) == 0)
		goto done;

	/* Use hostlist_create_bracketed if we see "[" */
	if (strchr(str, '[') != NULL)
		return _hostlist_create_bracketed(hostlist, sep, r_op);

	while ((tok = _next_tok(sep, &str)) != NULL) {

		/* save the current string for error messages */
		cur = tok;

		high = low = 0;

		/* find end of alpha part 
		 *   do this by finding last occurence of range_op in str */
		pos = strlen(tok) - 1;
		if (strstr(tok, r_op) != '\0') {
			while (pos >= 0 && (char) tok[pos] != range_op) 
				pos--;
		}

		/* now back up past any digits */
		while (pos >= 0 && isdigit((char) tok[--pos])) {;}

		/* Check for valid x-y range (x must be a digit) 
		 *   Reset pos if the range is not valid         */
		if (!isdigit((char) tok[++pos]))
			pos = strlen(tok) - 1;

		/* create prefix string 
		 * if prefix will be zero length, but prefix already exists
		 * use the previous prefix and fmt
		 */
		if ((pos > 0) || (prefix[0] == '\0')) {
			memcpy(prefix, tok, (size_t) pos * sizeof(char));
			prefix[pos] = '\0';

			/* push pointer past prefix */
			tok += pos;

			/* count number of digits for ouput fmt */
			for (fmt = 0; isdigit(tok[fmt]); ++fmt) {;}

			if (fmt == 0)
				error = 1;

		} else
			tok += pos;

		/* get lower bound */
		low = strtoul(tok, (char **) &tok, HOSTLIST_BASE);

		if (*tok == range_op) {    /* now get range upper bound */
			/* push pointer past range op */
			++tok;

			/* find length of alpha part */
			for (pos = 0; tok[pos] && !isdigit(tok[pos]); ++pos) {;}

			/* alpha part must match prefix or error
			 * this could mean we've got something like "rtr1-a2"
			 * so just record an error
			 */
			if (pos > 0) {
				if (pos != strlen(prefix) ||
				    strncmp(prefix, tok, pos) != 0)
					error = 1;
			}

			if (*tok != '\0')
				tok += pos;

			/* make sure we have digits to the end */
			for (pos = 0; tok[pos] && isdigit((char) tok[pos]); ++pos) {;}

			if (pos > 0) {    /* we have digits to process */
				high = strtoul(tok, (char **) &tok,
					       HOSTLIST_BASE);
			} else {    /* bad boy, no digits */
				error = 1;
			}

			if ((low > high) || (high - low > MAX_RANGE))
				error = 1;

		} else {    /* single value */
			high = 0;    /* special case, ugh. */
		}

		/* error if: 
		 * 1. we are not at end of string
		 * 2. upper bound equals lower bound
		 */
		if (*tok != '\0' || high == low)
			error = 1;

		if (error) {    /* assume this is not a range on any error */
			hostlist_push_host(new, cur);
		} else {
			if (high < low)
				high = low;
			hostlist_push_hr(new, prefix, low, high, fmt);
		}

		error = 0;
	}

  done:
	if(orig)
		free(orig);

	return new;
}

#else                /* !WANT_RECKLESS_HOSTRANGE_EXPANSION */

hostlist_t _hostlist_create(const char *hostlist, char *sep, char *r_op) 
{
	return _hostlist_create_bracketed(hostlist, sep, r_op);
}

#endif                /* WANT_RECKLESS_HOSTRANGE_EXPANSION */

struct _range {
	unsigned long lo, hi;
	int width;
};

/* Grab a single range from str 
 * returns 1 if str contained a valid number or range,
 *         0 if conversion of str to a range failed.
 */
static int _parse_single_range(const char *str, struct _range *range)
{
	char *p, *q;
	char *orig = strdup(str);
	if (!orig) 
		seterrno_ret(ENOMEM, 0);
	
	if ((p = strchr(str, 'x'))) {
		goto error; /* do NOT allow boxes in here */
	}

	if ((p = strchr(str, '-'))) {
		*p++ = '\0';
		if (*p == '-')     /* do NOT allow negative numbers */
			goto error;
	}

	range->lo = strtoul(str, &q, HOSTLIST_BASE);

	if (q == str) 
		goto error;
	
	range->hi = (p && *p) ? strtoul(p, &q, HOSTLIST_BASE) : range->lo;

	if (q == p || *q != '\0') 
		goto error;
	
	if (range->lo > range->hi) 
		goto error;
	
	if (range->hi - range->lo + 1 > MAX_RANGE ) {
		_error(__FILE__, __LINE__, "Too many hosts in range `%s'\n", orig);
		free(orig);
		seterrno_ret(ERANGE, 0);
	}
	
	free(orig);
	range->width = strlen(str);
	return 1;
	
error:
	errno = EINVAL;
	_error(__FILE__, __LINE__, "Invalid range: `%s'", orig);
	free(orig);
	return 0;
}

/*
 * Convert description of a rectangular prism in 3-D node space into a set of 
 * sequential node ranges.
 * str IN - contains "<number>x<number>" in which the two number describe the
 *		XYZ boundaries of the nodes, each must contain three-digits
 * ranges IN/OUT - set of high/low numeric ranges based upon sequential ordering
 * len IN - number of entries in ranges structure
 * count OUT - location in ranges of first unused entry
 * RET 1 if str contained a valid number or range,
 *	0 if conversion of str to a range failed.
 */
static int _parse_box_range(char *str, struct _range *ranges,
			    int len, int *count)
{
	int a[3], b[3], i1, i2, i;
	char new_str[8];

	if((str[3] != 'x') || (str[7] != '\0')) 
		return 0;

	for(i = 0; i<3; i++) {
		if ((str[i] >= '0') && (str[i] <= '9'))
			a[i] = str[i] - '0';
		else if ((str[i] >= 'A') && (str[i] <= 'Z'))
			a[i] = str[i] - 'A' + 10;
		else
			return 0;

		if ((str[i+4] >= '0') && (str[i+4] <= '9'))
			b[i] = str[i+4] - '0';
		else if ((str[i+4] >= 'A') && (str[i+4] <= 'Z'))
			b[i] = str[i+4] - 'A' + 10;
		else
			return 0;
	}

	for (i1=a[0]; i1 <= b[0]; i1++) {
		for (i2=a[1]; i2 <=b[1]; i2++) {
			if (*count == len)
				return -1;
			snprintf(new_str, 8, "%c%c%c-%c%c%c", 
				 alpha_num[i1], alpha_num[i2], alpha_num[a[2]],
				 alpha_num[i1], alpha_num[i2], 
				 alpha_num[b[2]]);
			if (!_parse_single_range(new_str,&ranges[*count]))
				return -1;
			(*count)++;
		}
	}
	return 1;
}

/*
 * Convert 'str' containing comma separated digits and ranges into an array
 *  of struct _range types (max 'len' elements).  
 *
 * Return number of ranges created, or -1 on error.
 */
static int _parse_range_list(char *str, struct _range *ranges, int len)
{
	char *p;
	int count = 0;

	while (str) {
		if (count == len)
			return -1;
		if ((p = strchr(str, ',')))
			*p++ = '\0';

		if ((str[3] == 'x') && (strlen(str) == 7)) {
			if (!_parse_box_range(str, ranges, len, &count)) 
				return -1;  
		} else {
			if (!_parse_single_range(str, &ranges[count++])) 
				return -1;  
		}
		str = p;
	}
	return count;
}

/* Validate prefix and push with the numeric suffix onto the hostlist
 * The prefix can contain a up to one range expresseion (e.g. "rack[1-4]_").
 * RET 0 on success, -1 on failure (invalid prefix) */
static int
_push_range_list(hostlist_t hl, char *prefix, struct _range *range,
		 int n)
{
	int i, k, nr;
	char *p, *q;
	char new_prefix[1024], tmp_prefix[1024];

	strncpy(tmp_prefix, prefix, sizeof(tmp_prefix));
	if (((p = strrchr(tmp_prefix, '[')) != NULL) &&
	    ((q = strrchr(p, ']')) != NULL)) {
		struct _range prefix_range[MAX_RANGES];
		struct _range *saved_range = range, *pre_range = prefix_range;
		unsigned long j, prefix_cnt = 0;
		*p++ = '\0';
		*q++ = '\0';
		if (strrchr(tmp_prefix, '[') != NULL)
			return -1;	/* third range is illegal */
		nr = _parse_range_list(p, prefix_range, MAX_RANGES);
		if (nr < 0)
			return -1;	/* bad numeric expression */
		for (i = 0; i < nr; i++) {
			prefix_cnt += pre_range->hi - pre_range->lo + 1;
			if (prefix_cnt > MAX_PREFIX_CNT) {
				/* Prevent overflow of memory with user input
				 * of something like "a[0-999999999].b[0-9]" */
				return -1;
			}
			for (j = pre_range->lo; j <= pre_range->hi; j++) {
				snprintf(new_prefix, sizeof(new_prefix),
					 "%s%0*lu%s", tmp_prefix, 
					 pre_range->width, j, q);
				range = saved_range;
				for (k = 0; k < n; k++) {
					hostlist_push_hr(hl, new_prefix,
							 range->lo, range->hi,
							 range->width);
					range++;
				}
			}
			pre_range++;
		}
		return 0;
	}

	for (k = 0; k < n; k++) {
		hostlist_push_hr(hl, prefix, 
				 range->lo, range->hi, range->width);
		range++;
	}
	return 0;
}

/*
 * Create a hostlist from a string with brackets '[' ']' to aid 
 * detection of ranges and compressed lists
 */
static hostlist_t 
_hostlist_create_bracketed(const char *hostlist, char *sep, char *r_op)
{
	hostlist_t new = hostlist_new();
	struct _range ranges[MAX_RANGES];
	int nr, err;
	char *p, *tok, *str, *orig;
	char cur_tok[1024];

	if (hostlist == NULL)
		return new;

	if (!(orig = str = strdup(hostlist))) {
		hostlist_destroy(new);
		return NULL;
	}

	while ((tok = _next_tok(sep, &str)) != NULL) {
		strncpy(cur_tok, tok, 1024);

		if ((p = strrchr(tok, '[')) != NULL) {
			char *q, *prefix = tok;
			*p++ = '\0';

			if ((q = strchr(p, ']'))) {
				if ((q[1] != ',') && (q[1] != '\0'))
					goto error;
				*q = '\0';
				nr = _parse_range_list(p, ranges, MAX_RANGES);
				if (nr < 0) 
					goto error;
				if (_push_range_list(new, prefix, ranges, nr))
					goto error;
                
			} else {
				/* The hostname itself contains a '['
				 * (no ']' found). 
				 * Not likely what the user wanted. */
				hostlist_push_host(new, cur_tok);
			}

		} else
			hostlist_push_host(new, cur_tok);
	}

	free(orig);
	return new;

  error:
	err = errno = EINVAL;
	hostlist_destroy(new);
	free(orig);
	seterrno_ret(err, NULL);
}



hostlist_t hostlist_create(const char *str)
{
	return _hostlist_create(str, "\t, ", "-");
}


hostlist_t hostlist_copy(const hostlist_t hl)
{
	int i;
	hostlist_t new;

	if (!hl)
		return NULL;

	LOCK_HOSTLIST(hl);
	if (!(new = hostlist_new()))
		goto done;

	new->nranges = hl->nranges;
	new->nhosts = hl->nhosts;
	if (new->nranges > new->size)
		hostlist_resize(new, new->nranges);

	for (i = 0; i < hl->nranges; i++)
		new->hr[i] = hostrange_copy(hl->hr[i]);

  done:
	UNLOCK_HOSTLIST(hl);
	return new;
}


void hostlist_destroy(hostlist_t hl)
{
	int i;
	if (!hl)
		return;
	LOCK_HOSTLIST(hl);
	while (hl->ilist) {
		mutex_unlock(&hl->mutex);
		hostlist_iterator_destroy(hl->ilist);
		mutex_lock(&hl->mutex);
	}
	for (i = 0; i < hl->nranges; i++)
		hostrange_destroy(hl->hr[i]);
	free(hl->hr);
	assert(hl->magic = 0x1);
	UNLOCK_HOSTLIST(hl);
	mutex_destroy(&hl->mutex);
	free(hl);
}


int hostlist_push(hostlist_t hl, const char *hosts)
{
	hostlist_t new;
	int retval;
	if (!hosts || !hl)
		return 0;
	new = hostlist_create(hosts);
	if (!new)
		return 0;
	mutex_lock(&new->mutex);
	retval = new->nhosts;
	mutex_unlock(&new->mutex);
	hostlist_push_list(hl, new);
	hostlist_destroy(new);
	return retval;
}

int hostlist_push_host(hostlist_t hl, const char *str)
{
	hostrange_t hr;
	hostname_t hn;

	if (!str || !hl)
		return 0;

	hn = hostname_create(str);

	if (hostname_suffix_is_valid(hn)) 
		hr = hostrange_create(hn->prefix, hn->num, hn->num,
				      hostname_suffix_width(hn));
	else 
		hr = hostrange_create_single(str);
	
	hostlist_push_range(hl, hr);

	hostrange_destroy(hr);
	hostname_destroy(hn);

	return 1;
}

int hostlist_push_list(hostlist_t h1, hostlist_t h2)
{
	int i, n = 0;

	if (!h2 || !h1)
		return 0;

	LOCK_HOSTLIST(h2);

	for (i = 0; i < h2->nranges; i++)
		n += hostlist_push_range(h1, h2->hr[i]);

	UNLOCK_HOSTLIST(h2);

	return n;
}


char *hostlist_pop(hostlist_t hl)
{
	char *host = NULL;
	if(!hl) {
		error("hostlist_pop: no hostlist given");
		return NULL;
	}
	
	LOCK_HOSTLIST(hl);
	if (hl->nhosts > 0) {
		hostrange_t hr = hl->hr[hl->nranges - 1];
		host = hostrange_pop(hr);
		hl->nhosts--;
		if (hostrange_empty(hr)) {
			hostrange_destroy(hl->hr[--hl->nranges]);
			hl->hr[hl->nranges] = NULL;
		}
	}
	UNLOCK_HOSTLIST(hl);
	return host;
}

/* find all iterators affected by a shift (or deletion) at 
 * hl->hr[idx], depth, with the deletion of n ranges */
static void
hostlist_shift_iterators(hostlist_t hl, int idx, int depth, int n)
{
	hostlist_iterator_t i;
	if(!hl) {
		error("hostlist_shift_iterators: no hoslist given");
		return;
	}
	for (i = hl->ilist; i; i = i->next) {
		if (n == 0) {
			if (i->idx == idx && i->depth >= depth)
				i->depth = i->depth > -1 ? i->depth - 1 : -1;
		} else {
			if (i->idx >= idx) {
				if ((i->idx -= n) >= 0)
					i->hr = i->hl->hr[i->idx];
				else
					hostlist_iterator_reset(i);
			}
		}
	}
}

char *hostlist_shift(hostlist_t hl)
{
	char *host = NULL;

	if(!hl){
		error("hostlist_shift: no hoslist given");
		return NULL;
	}
	LOCK_HOSTLIST(hl);

	if (hl->nhosts > 0) {
		hostrange_t hr = hl->hr[0];

		host = hostrange_shift(hr);
		hl->nhosts--;

		if (hostrange_empty(hr)) {
			hostlist_delete_range(hl, 0);
			/* hl->nranges--; */
		} else
			hostlist_shift_iterators(hl, 0, 0, 0);
	}

	UNLOCK_HOSTLIST(hl);

	return host;
}


char *hostlist_pop_range(hostlist_t hl)
{
	int i;
	char buf[MAXHOSTRANGELEN + 1];
	hostlist_t hltmp;
	hostrange_t tail;

	if(!hl)
		return NULL;
	LOCK_HOSTLIST(hl);
	if (hl->nranges < 1 || !(hltmp = hostlist_new())) {
		UNLOCK_HOSTLIST(hl);
		return NULL;
	}

	i = hl->nranges - 2;
	tail = hl->hr[hl->nranges - 1];
	while (i >= 0 && hostrange_within_range(tail, hl->hr[i]))
		i--;

	for (i++; i < hl->nranges; i++) {
		hostlist_push_range(hltmp, hl->hr[i]);
		hostrange_destroy(hl->hr[i]);
		hl->hr[i] = NULL;
	}
	hl->nhosts -= hltmp->nhosts;
	hl->nranges -= hltmp->nranges;

	UNLOCK_HOSTLIST(hl);
	hostlist_ranged_string(hltmp, MAXHOSTRANGELEN, buf);
	hostlist_destroy(hltmp);
	return strdup(buf);
}


char *hostlist_shift_range(hostlist_t hl)
{
	int i;
	char buf[MAXHOSTRANGELEN+1];
	hostlist_t hltmp = hostlist_new();
	if (!hltmp || !hl)
		return NULL;

	LOCK_HOSTLIST(hl);

	if (hl->nranges == 0) {
		hostlist_destroy(hltmp);
		UNLOCK_HOSTLIST(hl);
		return NULL;
	}

	i = 0;
	do {
		hostlist_push_range(hltmp, hl->hr[i]);
		hostrange_destroy(hl->hr[i]);
	} while ( (++i < hl->nranges) 
		&& hostrange_within_range(hltmp->hr[0], hl->hr[i]) );

	hostlist_shift_iterators(hl, i, 0, hltmp->nranges);

	/* shift rest of ranges back in hl */
	for (; i < hl->nranges; i++) {
		hl->hr[i - hltmp->nranges] = hl->hr[i];
		hl->hr[i] = NULL;
	}
	hl->nhosts -= hltmp->nhosts;
	hl->nranges -= hltmp->nranges;

	UNLOCK_HOSTLIST(hl);

	hostlist_ranged_string(hltmp, MAXHOSTRANGELEN, buf);
	hostlist_destroy(hltmp);

	return strdup(buf);
}

/* XXX: Note: efficiency improvements needed */
int hostlist_delete(hostlist_t hl, const char *hosts)
{
	int n = 0;
	char *hostname = NULL;
	hostlist_t hltmp;
	if(!hl)
		return -1;
	
	if (!(hltmp = hostlist_create(hosts)))
		seterrno_ret(EINVAL, 0);

	while ((hostname = hostlist_pop(hltmp)) != NULL) {
		n += hostlist_delete_host(hl, hostname);
		free(hostname);
	}
	hostlist_destroy(hltmp);

	return n;
}


/* XXX watch out! poor implementation follows! (fix it at some point) */
int hostlist_delete_host(hostlist_t hl, const char *hostname)
{
	int n;

	if(!hl)
		return -1;
	n = hostlist_find(hl, hostname);

	if (n >= 0)
		hostlist_delete_nth(hl, n);
	return n >= 0 ? 1 : 0;
}


static char *
_hostrange_string(hostrange_t hr, int depth)
{
	char buf[MAXHOSTNAMELEN + 16];
	int  len = snprintf(buf, MAXHOSTNAMELEN + 15, "%s", hr->prefix);

	if (!hr->singlehost) {
#ifdef HAVE_3D
		if (hr->width == 3) {
			int i2=0;
			int coord[SYSTEM_DIMENSIONS];
			
			_parse_int_to_array((hr->lo+depth), coord);

			while(i2 < SYSTEM_DIMENSIONS) {
				if(len <= (MAXHOSTNAMELEN + 15))
					buf[len++] = alpha_num[coord[i2++]];
			}
			buf[len++] = '\0';
		} else {
			snprintf(buf+len, MAXHOSTNAMELEN+15 - len, "%0*lu", 
				 hr->width, hr->lo + depth);
		}
#else
		snprintf(buf+len, MAXHOSTNAMELEN+15 - len, "%0*lu", 
			 hr->width, hr->lo + depth);
#endif
	}
	return strdup(buf);
}

char * hostlist_nth(hostlist_t hl, int n)
{
	char *host = NULL;
	int   i, count;

	if(!hl)
		return NULL;
	LOCK_HOSTLIST(hl);
	count = 0;
	for (i = 0; i < hl->nranges; i++) {
		int num_in_range = hostrange_count(hl->hr[i]);

		if (n <= (num_in_range - 1 + count)) {
			host = _hostrange_string(hl->hr[i], n - count);
			break;
		} else
			count += num_in_range;
	}

	UNLOCK_HOSTLIST(hl);

	return host;
}


int hostlist_delete_nth(hostlist_t hl, int n)
{
	int i, count;

	if(!hl)
		return -1;
	LOCK_HOSTLIST(hl);
	assert(n >= 0 && n <= hl->nhosts);

	count = 0;

	for (i = 0; i < hl->nranges; i++) {
		int num_in_range = hostrange_count(hl->hr[i]);
		hostrange_t hr = hl->hr[i];

		if (n <= (num_in_range - 1 + count)) {
			unsigned long num = hr->lo + n - count;
			hostrange_t new;

			if (hr->singlehost) { /* this wasn't a range */
				hostlist_delete_range(hl, i);
			} else if ((new = hostrange_delete_host(hr, num))) {
				hostlist_insert_range(hl, new, i + 1);
				hostrange_destroy(new);
			} else if (hostrange_empty(hr))
				hostlist_delete_range(hl, i);

			goto done;
		} else
			count += num_in_range;

	}

  done:
	UNLOCK_HOSTLIST(hl);
	hl->nhosts--;
	return 1;
}

int hostlist_count(hostlist_t hl)
{
	int retval;
	if(!hl)
		return -1;

	LOCK_HOSTLIST(hl);
	retval = hl->nhosts;
	UNLOCK_HOSTLIST(hl);
	return retval;
}

int hostlist_find(hostlist_t hl, const char *hostname)
{
	int i, count, ret = -1;
	hostname_t hn;

	if (!hostname || !hl)
		return -1;

	hn = hostname_create(hostname);

	LOCK_HOSTLIST(hl);

	for (i = 0, count = 0; i < hl->nranges; i++) {
		if (hostrange_hn_within(hl->hr[i], hn)) {
			if (hostname_suffix_is_valid(hn))
				ret = count + hn->num - hl->hr[i]->lo;
			else
				ret = count;
			goto done;
		} else
			count += hostrange_count(hl->hr[i]);
	}

  done:
	UNLOCK_HOSTLIST(hl);
	hostname_destroy(hn);
	return ret;
}

/* hostrange compare with void * arguments to allow use with 
 * libc qsort()
 */
int _cmp(const void *hr1, const void *hr2)
{
	hostrange_t *h1 = (hostrange_t *) hr1;
	hostrange_t *h2 = (hostrange_t *) hr2;
	return hostrange_cmp((hostrange_t) * h1, (hostrange_t) * h2);
}


void hostlist_sort(hostlist_t hl)
{
	hostlist_iterator_t i;
	LOCK_HOSTLIST(hl);

	if (hl->nranges <= 1) {
		UNLOCK_HOSTLIST(hl);
		return;
	}

	qsort(hl->hr, hl->nranges, sizeof(hostrange_t), &_cmp);

	/* reset all iterators */
	for (i = hl->ilist; i; i = i->next)
		hostlist_iterator_reset(i);

	UNLOCK_HOSTLIST(hl);

	hostlist_coalesce(hl);

}


/* search through hostlist for ranges that can be collapsed 
 * does =not= delete any hosts
 */
static void hostlist_collapse(hostlist_t hl)
{
	int i;

	LOCK_HOSTLIST(hl);
	for (i = hl->nranges - 1; i > 0; i--) {
		hostrange_t hprev = hl->hr[i - 1];
		hostrange_t hnext = hl->hr[i];

		if (hostrange_prefix_cmp(hprev, hnext) == 0 &&
		    hprev->hi == hnext->lo - 1 &&
		    hostrange_width_combine(hprev, hnext)) {
			hprev->hi = hnext->hi;
			hostlist_delete_range(hl, i);
		}
	}
	UNLOCK_HOSTLIST(hl);
}

/* search through hostlist (hl) for intersecting ranges 
 * split up duplicates and coalesce ranges where possible
 */
static void hostlist_coalesce(hostlist_t hl)
{
	int i, j;
	hostrange_t new;

	LOCK_HOSTLIST(hl);

	for (i = hl->nranges - 1; i > 0; i--) {

		new = hostrange_intersect(hl->hr[i - 1], hl->hr[i]);

		if (new) {
			hostrange_t hprev = hl->hr[i - 1];
			hostrange_t hnext = hl->hr[i];
			j = i;

			if (new->hi < hprev->hi)
				hnext->hi = hprev->hi;

			hprev->hi = new->lo;
			hnext->lo = new->hi;

			if (hostrange_empty(hprev))
				hostlist_delete_range(hl, i);

			while (new->lo <= new->hi) {
				hostrange_t hr = hostrange_create( new->prefix,
					new->lo, new->lo,
					new->width );

				if (new->lo > hprev->hi)
					hostlist_insert_range(hl, hr, j++);

				if (new->lo < hnext->lo)
					hostlist_insert_range(hl, hr, j++);

				hostrange_destroy(hr);

				new->lo++;
			}
			i = hl->nranges;
			hostrange_destroy(new);
		}
	}
	UNLOCK_HOSTLIST(hl);

	hostlist_collapse(hl);

}

/* attempt to join ranges at loc and loc-1 in a hostlist  */
/* delete duplicates, return the number of hosts deleted  */
/* assumes that the hostlist hl has been locked by caller */
/* returns -1 if no range join occured */
static int _attempt_range_join(hostlist_t hl, int loc)
{
	int ndup;
	assert(hl != NULL);
	assert(hl->magic == HOSTLIST_MAGIC);
	assert(loc > 0);
	assert(loc < hl->nranges);
	ndup = hostrange_join(hl->hr[loc - 1], hl->hr[loc]);
	if (ndup >= 0) {
		hostlist_delete_range(hl, loc);
		hl->nhosts -= ndup;
	}
	return ndup;
}

void hostlist_uniq(hostlist_t hl)
{
	int i = 1;
	hostlist_iterator_t hli;
	LOCK_HOSTLIST(hl);
	if (hl->nranges <= 1) {
		UNLOCK_HOSTLIST(hl);
		return;
	}
	qsort(hl->hr, hl->nranges, sizeof(hostrange_t), &_cmp);

	while (i < hl->nranges) {
		if (_attempt_range_join(hl, i) < 0) /* No range join occurred */
			i++;
	}

	/* reset all iterators */
	for (hli = hl->ilist; hli; hli = hli->next)
		hostlist_iterator_reset(hli);

	UNLOCK_HOSTLIST(hl);
}


ssize_t hostlist_deranged_string(hostlist_t hl, size_t n, char *buf)
{
	int i;
	int len = 0;
	int truncated = 0;

	LOCK_HOSTLIST(hl);
	for (i = 0; i < hl->nranges; i++) {
		size_t m = (n - len) <= n ? n - len : 0;
		int ret = hostrange_to_string(hl->hr[i], m, buf + len, ",");
		if (ret < 0 || ret > m) {
			len = n;
			truncated = 1;
			break;
		}
		len+=ret;
		buf[len++] = ',';
	}
	UNLOCK_HOSTLIST(hl);

	buf[len > 0 ? --len : 0] = '\0';
	if (len == n)
		truncated = 1;

	return truncated ? -1 : len;
}

/* return true if a bracket is needed for the range at i in hostlist hl */
static int _is_bracket_needed(hostlist_t hl, int i)
{
	hostrange_t h1 = hl->hr[i];
	hostrange_t h2 = i < hl->nranges - 1 ? hl->hr[i + 1] : NULL;
	return hostrange_count(h1) > 1 || hostrange_within_range(h1, h2);
}

/* write the next bracketed hostlist, i.e. prefix[n-m,k,...]
 * into buf, writing at most n chars including the terminating '\0'
 *
 * leaves start pointing to one past last range object in bracketed list,
 * and returns the number of bytes written into buf.
 *
 * Assumes hostlist is locked.
 */
static int
_get_bracketed_list(hostlist_t hl, int *start, const size_t n, char *buf)
{
	hostrange_t *hr = hl->hr;
	int i = *start;
	int m, len = 0;
	int bracket_needed = _is_bracket_needed(hl, i);

	len = snprintf(buf, n, "%s", hr[i]->prefix);

	if ((len < 0) || (len > n))
		return n; /* truncated, buffer filled */

	if (bracket_needed && len < n && len >= 0)
		buf[len++] = '[';

	do {
		m = (n - len) <= n ? n - len : 0;
		len += hostrange_numstr(hr[i], m, buf + len);
		if (len >= n)
			break;
		if (bracket_needed) /* Only need commas inside brackets */
			buf[len++] = ',';
	} while (++i < hl->nranges && hostrange_within_range(hr[i], hr[i-1]));

	if (bracket_needed && len < n && len > 0) {

		/* Add trailing bracket (change trailing "," from above to "]" */
		buf[len - 1] = ']';

		/* NUL terminate for safety, but do not add terminator to len */
		buf[len]   = '\0';

	} else if (len >= n) {
		if (n > 0)
			buf[n-1] = '\0';

	} else {
		/* If len is > 0, NUL terminate (but do not add to len) */
		buf[len > 0 ? len : 0] = '\0';
	}

	*start = i;
	return len;
}

#if (SYSTEM_DIMENSIONS > 1)
static void _parse_int_to_array(int in, int out[SYSTEM_DIMENSIONS])
{
	int a;

	static int my_start_pow_minus = 0;
	static int my_start_pow = 0;
        int my_pow_minus = my_start_pow_minus;
	int my_pow = my_start_pow;
	
	if(!my_start_pow) {
		/* this will never change so just calculate it once */
		my_start_pow = 1;
		a=0;
		/* To avoid having to use the pow function and include
		   the math lib everywhere just do this. */
		while(a < SYSTEM_DIMENSIONS) {
			my_start_pow *= HOSTLIST_BASE;
			a++;
		}
		my_pow = my_start_pow;
		my_pow_minus = my_start_pow_minus =
			my_start_pow / HOSTLIST_BASE;
	}
	a=0;
	while(a < SYSTEM_DIMENSIONS) {
		out[a] = (int)in % my_pow;

		/* This only needs to be done until we get a 0 here
		   meaning we are on the last dimension. This avoids
		   dividing by 0. */
		if(SYSTEM_DIMENSIONS - a) {
			out[a] /= my_pow_minus;

			/* set this up for the next dimension */
			my_pow = my_pow_minus;
			my_pow_minus /= HOSTLIST_BASE;
		} 
		a++;
	}
}


/* logic for block node description */
/* write the next bracketed hostlist, i.e. prefix[n-m,k,...]
 * into buf, writing at most n chars including the terminating '\0'
 *
 * leaves start pointing to one past last range object in bracketed list,
 * and returns the number of bytes written into buf.
 *
 * Assumes hostlist is locked.
 */
static int
_get_boxes(char *buf, int max_len)
{
	int a, b, c, len = 0, i=0;
#if (SYSTEM_DIMENSIONS == 4)
	int d;
#endif
	int is_box;
	int curr_min[SYSTEM_DIMENSIONS], curr_max[SYSTEM_DIMENSIONS];

	memcpy(curr_min, axis_min, sizeof(curr_min));
	memcpy(curr_max, axis_max, sizeof(curr_max));
	
	curr_min[A] = -1;
	curr_max[A] = -1;
	/* scan each X-plane for a box then match across X values */
	for (a=axis_min[A]; a<=axis_max[A]; a++) {
		is_box = 1;
		for (b=axis_min[B]; b<=axis_max[B]; b++) {
			for (c=axis_min[C]; c<=axis_max[C]; c++) {
#if (SYSTEM_DIMENSIONS == 4)
				for (d=axis_min[D]; d<=axis_max[D]; d++) {
					if (!axis[a][b][c][d]) {
						is_box = 0;
						break;
					}
				}
#else
				if (!axis[a][b][c]) {
					is_box = 0;
					break;
				}
#endif

			}
		}

		if (is_box) {
			if (curr_min[A] == -1) 
				curr_min[A] = a;			
			curr_max[A] = a;
		}

		if (((len+box_length) < max_len) && (curr_min[A] != -1)
		    && ((!is_box) || (a == axis_max[A]))) {
			if(!memcmp(curr_min, curr_max, sizeof(curr_min))) {
				i = 0;
				while(i < SYSTEM_DIMENSIONS) {
					buf[len++] = alpha_num[curr_min[i++]];
				}
				buf[len++] = ',';
			} else {
				i = 0;
				while(i < SYSTEM_DIMENSIONS) {
					buf[len++] = alpha_num[curr_min[i++]];
				}
				buf[len++] = 'x';
				i = 0;
				while(i < SYSTEM_DIMENSIONS) {
					buf[len++] = alpha_num[curr_max[i++]];
				}
				buf[len++] = ',';
			}
			curr_min[A] = -1;
			curr_max[A] = -1;
		}

		if(is_box) 
			continue;
		for (b=axis_min[B]; b<=axis_max[B]; b++) {
			for (c=axis_min[C]; c<=axis_max[C]; c++) {
#if (SYSTEM_DIMENSIONS == 4)
				for (d=axis_min[D]; d<=axis_max[D]; d++) {
					if (!axis[a][b][c][d])
						continue;
					if ((len+5) >= max_len)
						break;
					sprintf(buf+len,"%c%c%c%c,",
						alpha_num[a], alpha_num[b],
						alpha_num[c], alpha_num[d]);
					len += 5;
				}
#else
				if (!axis[a][b][c])
					continue;
				if ((len+4) >= max_len)
					break;
				sprintf(buf+len,"%c%c%c,",
					alpha_num[a], alpha_num[b],
					alpha_num[c]);
				len += 4;
#endif
			}
		}
	}
	
	buf[len - 1] = ']';
	
	/* NUL terminate for safety, but do not add terminator to len */
	buf[len]   = '\0';

	return len;
}

static void
_clear_grid(void)
{
	memset(axis, 0, sizeof(axis));

	memset(axis_min, HOSTLIST_BASE, sizeof(axis_min));
	memset(axis_max, -1, sizeof(axis_max));
}

static void
_set_grid(unsigned long start, unsigned long end)
{
	int sent_start[SYSTEM_DIMENSIONS], sent_end[SYSTEM_DIMENSIONS];
	int a, b, c;
#if (SYSTEM_DIMENSIONS == 4)
	int d;
#endif
	
	_parse_int_to_array(start, sent_start);
	_parse_int_to_array(end, sent_end);

	a=0;
	while(a < SYSTEM_DIMENSIONS) {
		axis_min[a] = MIN(axis_min[a], sent_start[a]);
		axis_max[a] = MAX(axis_max[a], sent_end[a]);
		a++;
	}
	
	for (a=sent_start[A]; a<=sent_end[A]; a++) {
		for (b=sent_start[B]; b<=sent_end[B]; b++) {
			for (c=sent_start[C]; c<=sent_end[C]; c++) {
#if (SYSTEM_DIMENSIONS == 4)
				for (d=axis_min[D]; d<=axis_max[D]; d++) {
					axis[a][b][c][d] = true;
				}
#else
				axis[a][b][c] = true;
#endif
			}
		}
	}
}  

static bool
_test_box(void)
{
	int a, b, c;
#if (SYSTEM_DIMENSIONS == 4)
	int d;
#endif
	if(!memcmp(axis_min, axis_max, sizeof(axis_min))) /* single node */
		return false;
	a=0;
	while(a < SYSTEM_DIMENSIONS) {
		if (axis_min[a] > axis_max[a])
			return false;
		a++;
	}

	for (a = axis_min[A]; a<=axis_max[A]; a++) {
		for (b = axis_min[B]; b<=axis_max[B]; b++) {
			for (c = axis_min[C]; c<=axis_max[C]; c++) {
#if (SYSTEM_DIMENSIONS == 4)
				for (d=axis_min[D]; d<=axis_max[D]; d++) {
					if (!axis[a][b][c][d])
						return false;
				}
#else
				if (!axis[a][b][c])
					return false;	/* gap in box */
#endif
			}
		}
	}

	return true;
}
#endif

ssize_t hostlist_ranged_string(hostlist_t hl, size_t n, char *buf)
{
	int i = 0;
	int len = 0;
	int truncated = 0;
	bool box = false;
  
	LOCK_HOSTLIST(hl);

#if (SYSTEM_DIMENSIONS > 1)	/* logic for block node description */
	if (hl->nranges < 1)
		goto notbox;	/* no data */

	if (hl->hr[0]->width != SYSTEM_DIMENSIONS) {
		/* We use this logic to build task list ranges, so
		 * this does not necessarily contain a
		 * SYSTEM_DIMENSION dimensional
		 * host list. It could just be numeric values */
		if (hl->hr[0]->prefix[0]) {
			debug("This node is not in %dD format.  "
		 	      "Prefix is %s and suffix is %d chars long",
		  	      SYSTEM_DIMENSIONS, 
			      hl->hr[0]->prefix, hl->hr[0]->width);
		}
		goto notbox; 
	}
	_clear_grid();
	for (i=0;i<hl->nranges;i++)
		_set_grid(hl->hr[i]->lo, hl->hr[i]->hi);
	if (!memcmp(axis_min, axis_max, sizeof(axis_min))) {
		len += snprintf(buf, n, "%s", hl->hr[0]->prefix);
		i = 0;
		while(i < SYSTEM_DIMENSIONS) {
			if(len > n)
				goto too_long;
			buf[len++] = alpha_num[axis_min[i++]];
		}
	} else if (!_test_box()) {
		sprintf(buf, "%s[", hl->hr[0]->prefix);
		len = strlen(hl->hr[0]->prefix) + 1;
		len += _get_boxes(buf + len, (n-len));
	} else {
		len += snprintf(buf, n, "%s[", hl->hr[0]->prefix);
		i = 0;
		while(i < SYSTEM_DIMENSIONS) {
			if(len > n)
				goto too_long;
			buf[len++] = alpha_num[axis_min[i++]];
		}
		if(len <= n)
			buf[len++] = 'x';

		i = 0;
		while(i < SYSTEM_DIMENSIONS) {
			if(len > n)
				goto too_long;
			buf[len++] = alpha_num[axis_max[i++]];
		}
		if(len <= n)
			buf[len++] = ']';
	}
	if ((len < 0) || (len > n))
	too_long:
		len = n;	/* truncated */
	box = true;
	
notbox:
#endif
	if (!box) {
		i=0;
		while (i < hl->nranges && len < n) {
			len += _get_bracketed_list(hl, &i, n - len, buf + len);
			if ((len > 0) && (len < n) && (i < hl->nranges))
				buf[len++] = ',';
		}
	}

	UNLOCK_HOSTLIST(hl);
	
	/* NUL terminate */
	if (len >= n) {
		truncated = 1;
		if (n > 0)
			buf[n-1] = '\0';
	} else
		buf[len > 0 ? len : 0] = '\0';

	return truncated ? -1 : len;
}
/* ----[ hostlist iterator functions ]---- */

static hostlist_iterator_t hostlist_iterator_new(void)
{
	hostlist_iterator_t i = (hostlist_iterator_t) malloc(sizeof(*i));
	if (!i) 
		return NULL;
	i->hl = NULL;
	i->hr = NULL;
	i->idx = 0;
	i->depth = -1;
	i->next = i;
	assert(i->magic = HOSTLIST_MAGIC);
	return i;
}

hostlist_iterator_t hostlist_iterator_create(hostlist_t hl)
{
	hostlist_iterator_t i;

	if (!(i = hostlist_iterator_new()))
		out_of_memory("hostlist_iterator_create");

	LOCK_HOSTLIST(hl);
	i->hl = hl;
	i->hr = hl->hr[0];
	i->next = hl->ilist;
	hl->ilist = i;
	UNLOCK_HOSTLIST(hl);
	return i;
}

hostlist_iterator_t hostset_iterator_create(hostset_t set)
{
	return hostlist_iterator_create(set->hl);
}

void hostlist_iterator_reset(hostlist_iterator_t i)
{
	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);
	i->idx = 0;
	i->hr = i->hl->hr[0];
	i->depth = -1;
	return;
}

void hostlist_iterator_destroy(hostlist_iterator_t i)
{
	hostlist_iterator_t *pi;
	if (i == NULL)
		return;
	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);
	LOCK_HOSTLIST(i->hl);
	for (pi = &i->hl->ilist; *pi; pi = &(*pi)->next) {
		assert((*pi)->magic == HOSTLIST_MAGIC);
		if (*pi == i) {
			*pi = (*pi)->next;
			break;
		}
	}
	UNLOCK_HOSTLIST(i->hl);
	assert(i->magic = 0x1);
	free(i);
}

static void _iterator_advance(hostlist_iterator_t i)
{
	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);

	if (i->idx > i->hl->nranges - 1)
		return;

	if (++(i->depth) > (i->hr->hi - i->hr->lo)) {
		i->depth = 0;
		i->hr = i->hl->hr[++i->idx];
	}
}

/* advance iterator to end of current range (meaning within "[" "]")
 * i.e. advance iterator past all range objects that could be represented
 * in on bracketed hostlist.
 */
static void _iterator_advance_range(hostlist_iterator_t i)
{
	int nr, j;
	hostrange_t *hr;
	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);

	nr = i->hl->nranges;
	hr = i->hl->hr;
	j = i->idx;
	if (++i->depth > 0) {
		while (++j < nr && hostrange_within_range(i->hr, hr[j])) {;}
		i->idx = j;
		i->hr = i->hl->hr[i->idx];
		i->depth = 0;
	}
}

char *hostlist_next(hostlist_iterator_t i)
{
	char buf[MAXHOSTNAMELEN + 16];
	int len = 0;
	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);
	LOCK_HOSTLIST(i->hl);
	_iterator_advance(i);

	if (i->idx > i->hl->nranges - 1) {
		UNLOCK_HOSTLIST(i->hl);
		return NULL;
	}

	len = snprintf(buf, MAXHOSTNAMELEN + 15, "%s", i->hr->prefix);
	if (!i->hr->singlehost) {
#ifdef HAVE_3D
		if (i->hr->width == 3) {
			int i2=0;
			int coord[SYSTEM_DIMENSIONS];
			_parse_int_to_array((i->hr->lo + i->depth), coord);
			while(i2 < SYSTEM_DIMENSIONS) {
				if(len <= (MAXHOSTNAMELEN + 15))
					buf[len++] = alpha_num[coord[i2++]];
			}
		} else {
			snprintf(buf + len, MAXHOSTNAMELEN + 15 - len, "%0*lu",
				 i->hr->width, i->hr->lo + i->depth);
		}
#else
		snprintf(buf + len, MAXHOSTNAMELEN + 15 - len, "%0*lu",
			i->hr->width, i->hr->lo + i->depth);
#endif
	}
	UNLOCK_HOSTLIST(i->hl);
	return strdup(buf);
}

char *hostlist_next_range(hostlist_iterator_t i)
{
	char buf[MAXHOSTRANGELEN + 1];
	int j;

	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);
	LOCK_HOSTLIST(i->hl);

	_iterator_advance_range(i);

	if (i->idx > i->hl->nranges - 1) {
		UNLOCK_HOSTLIST(i->hl);
		return NULL;
	}

	j = i->idx;
	_get_bracketed_list(i->hl, &j, MAXHOSTRANGELEN, buf);

	UNLOCK_HOSTLIST(i->hl);

	return strdup(buf);
}

int hostlist_remove(hostlist_iterator_t i)
{
	hostrange_t new;
	assert(i != NULL);
	assert(i->magic == HOSTLIST_MAGIC);
	LOCK_HOSTLIST(i->hl);
	new = hostrange_delete_host(i->hr, i->hr->lo + i->depth);
	if (new) {
		hostlist_insert_range(i->hl, new, i->idx + 1);
		hostrange_destroy(new);
		i->hr = i->hl->hr[++i->idx];
		i->depth = -1;
	} else if (hostrange_empty(i->hr)) {
		hostlist_delete_range(i->hl, i->idx);
		/* i->hr = i->hl->hr[i->idx];
		i->depth = -1; */
	} else
		i->depth--;

	i->hl->nhosts--;
	UNLOCK_HOSTLIST(i->hl);

	return 1;
}

/* ----[ hostset functions ]---- */

hostset_t hostset_create(const char *hostlist)
{
	hostset_t new;

	if (!(new = (hostset_t) malloc(sizeof(*new))))
		goto error1;

	if (!(new->hl = hostlist_create(hostlist)))
		goto error2;

	hostlist_uniq(new->hl);
	return new;

  error2:
	free(new);
  error1:
	return NULL;
}

hostset_t hostset_copy(const hostset_t set)
{
	hostset_t new;
	if (!(new = (hostset_t) malloc(sizeof(*new))))
		goto error1;

	if (!(new->hl = hostlist_copy(set->hl)))
		goto error2;

	return new;
  error2:
	free(new);
  error1:
	return NULL;
}

void hostset_destroy(hostset_t set)
{
	if (set == NULL)
		return;
	hostlist_destroy(set->hl);
	free(set);
}

/* inserts a single range object into a hostset 
 * Assumes that the set->hl lock is already held
 * Updates hl->nhosts
 */
static int hostset_insert_range(hostset_t set, hostrange_t hr)
{
	int i = 0;
	int inserted = 0;
	int nhosts = 0;
	int ndups = 0;
	hostlist_t hl;

	hl = set->hl;

	if (hl->size == hl->nranges && !hostlist_expand(hl))
		return 0;

	nhosts = hostrange_count(hr);

	for (i = 0; i < hl->nranges; i++) {
		if (hostrange_cmp(hr, hl->hr[i]) <= 0) {

			if ((ndups = hostrange_join(hr, hl->hr[i])) >= 0) 
				hostlist_delete_range(hl, i);
			else if (ndups < 0)
				ndups = 0;

			hostlist_insert_range(hl, hr, i);

			/* now attempt to join hr[i] and hr[i-1] */
			if (i > 0) {
				int m;
				if ((m = _attempt_range_join(hl, i)) > 0)
					ndups += m;
			}
			hl->nhosts += nhosts - ndups;
			inserted = 1;
			break;
		}
	}

	if (inserted == 0) {
		hl->hr[hl->nranges++] = hostrange_copy(hr);
		hl->nhosts += nhosts;
		if (hl->nranges > 1) {
			if ((ndups = _attempt_range_join(hl, hl->nranges - 1)) <= 0)
				ndups = 0;
		}
	}

	/*
	 *  Return the number of unique hosts inserted
	 */
	return nhosts - ndups;
}

int hostset_insert(hostset_t set, const char *hosts)
{
	int i, n = 0;
	hostlist_t hl = hostlist_create(hosts);
	if (!hl)
		return 0;

	hostlist_uniq(hl);
	LOCK_HOSTLIST(set->hl);
	for (i = 0; i < hl->nranges; i++) 
		n += hostset_insert_range(set, hl->hr[i]);
	UNLOCK_HOSTLIST(set->hl);
	hostlist_destroy(hl);
	return n;
}


/* linear search through N ranges for hostname "host"
 * */
static int hostset_find_host(hostset_t set, const char *host)
{
	int i;
	int retval = 0;
	hostname_t hn;
	LOCK_HOSTLIST(set->hl);
	hn = hostname_create(host);
	for (i = 0; i < set->hl->nranges; i++) {
		if (hostrange_hn_within(set->hl->hr[i], hn)) {
			retval = 1;
			goto done;
		}
	}
  done:
	UNLOCK_HOSTLIST(set->hl);
	hostname_destroy(hn);
	return retval;
}

int hostset_intersects(hostset_t set, const char *hosts)
{
	int retval = 0;
	hostlist_t hl;
	char *hostname;

	assert(set->hl->magic == HOSTLIST_MAGIC);

	hl = hostlist_create(hosts);
	if (!hl)    /* malloc failure */
		return retval;

	while ((hostname = hostlist_pop(hl)) != NULL) {
		retval += hostset_find_host(set, hostname);
		free(hostname);
		if (retval)
			break;
	}

	hostlist_destroy(hl);

	return retval;
}

int hostset_within(hostset_t set, const char *hosts)
{
	int nhosts, nfound;
	hostlist_t hl;
	char *hostname;

	assert(set->hl->magic == HOSTLIST_MAGIC);

	if (!(hl = hostlist_create(hosts)))
        return (0);
	nhosts = hostlist_count(hl);
	nfound = 0;

	while ((hostname = hostlist_pop(hl)) != NULL) {
		nfound += hostset_find_host(set, hostname);
		free(hostname);
	}

	hostlist_destroy(hl);

	return (nhosts == nfound);
}

int hostset_delete(hostset_t set, const char *hosts)
{
	return hostlist_delete(set->hl, hosts);
}

int hostset_delete_host(hostset_t set, const char *hostname)
{
	return hostlist_delete_host(set->hl, hostname);
}

char *hostset_shift(hostset_t set)
{
	return hostlist_shift(set->hl);
}

char *hostset_pop(hostset_t set)
{
	return hostlist_pop(set->hl);
}

char *hostset_shift_range(hostset_t set)
{
	return hostlist_shift_range(set->hl);
}

char *hostset_pop_range(hostset_t set)
{
	return hostlist_pop_range(set->hl);
}

int hostset_count(hostset_t set)
{
	return hostlist_count(set->hl);
}

ssize_t hostset_ranged_string(hostset_t set, size_t n, char *buf)
{
	return hostlist_ranged_string(set->hl, n, buf);
}

ssize_t hostset_deranged_string(hostset_t set, size_t n, char *buf)
{
	return hostlist_deranged_string(set->hl, n, buf);
}

char * hostset_nth(hostset_t set, int n)
{
	return hostlist_nth(set->hl, n);
}

int hostset_find(hostset_t set, const char *hostname)
{
	return hostlist_find(set->hl, hostname);
}

#if TEST_MAIN 

int hostlist_nranges(hostlist_t hl)
{
	return hl->nranges;
}

int hostset_nranges(hostset_t set)
{
	return set->hl->nranges;
}

/* test iterator functionality on the list of hosts represented
 * by list
 */
int iterator_test(char *list)
{
	int j;
	char buf[MAXHOSTRANGELEN+1];
	hostlist_t hl = hostlist_create(list);
	hostset_t set = hostset_create(list);

	hostlist_iterator_t i = hostlist_iterator_create(hl);
	hostlist_iterator_t seti = hostset_iterator_create(set);
	hostlist_iterator_t i2 = hostlist_iterator_create(hl);
	char *host;

	hostlist_ranged_string(hl, MAXHOSTRANGELEN, buf);
	printf("iterator_test: hl = `%s' passed in `%s'\n", buf, list);
	host = hostlist_next(i);
	printf("first host in list hl = `%s'\n", host);
	free(host);

	/* forge ahead three hosts with i2 */
	for (j = 0; j < 4; j++) {
		host = hostlist_next(i2);
		free(host);
	}

	host = hostlist_shift(hl);
	printf("result of shift(hl)   = `%s'\n", host);
	free(host);
	host = hostlist_next(i);
	printf("next host in list hl  = `%s'\n", host);
	free(host);
	host = hostlist_next(i2);
	printf("next host for i2      = `%s'\n", host);
	free(host);

	hostlist_iterator_destroy(i);

	hostlist_destroy(hl);
	hostset_destroy(set);
	return 1;
}

int main(int ac, char **av)
{
	char buf[1024000];
	int i;
	char *str;

	hostlist_t hl1, hl2, hl3;
	hostset_t set, set1;
	hostlist_iterator_t iter, iter2;

    if (ac < 2)
        printf("Recommended usage: %s [hostlist]\n\n", av[0]);

	if (!(hl1 = hostlist_create(ac > 1 ? av[1] : NULL))) {
		perror("hostlist_create");
        exit(1);
    }

    /* build a temporary hostlist, remove duplicates, 
     * use it to make the hostset */
    if (!(hl2 = hostlist_create(ac > 1 ? av[1] : NULL))) {
        perror("hostlist_create");
        exit(1);
    }
    hostlist_uniq(hl2);
    hostlist_ranged_string(hl2, 102400, buf);
	if (!(set = hostset_create(buf))) {
		perror("hostset_create");
        exit(1);
    }
    hostlist_destroy(hl2);

	hl3 = hostlist_create("f[0-5]");
	hostlist_delete(hl3, "f[1-3]");
	hostlist_ranged_string(hl3, 102400, buf);
	printf("after delete = `%s'\n", buf);
	hostlist_destroy(hl3);

	hl3 = hostlist_create("bg[012x123]");
	hostlist_ranged_string(hl3, 102400, buf);
	printf("bg[012x123] == `%s'\n", buf);
	i = hostlist_count(hl3);
	assert(i == 8);
	hostlist_ranged_string(hl3, 102400, buf);
	hostlist_destroy(hl3);

	for (i = 2; i < ac; i++) {
		hostlist_push(hl1, av[i]);
		hostset_insert(set, av[i]);
	}

	hostlist_ranged_string(hl1, 102400, buf);
	printf("ranged   = `%s'\n", buf);

	iterator_test(buf);

	hostlist_deranged_string(hl1, 10240, buf);
	printf("deranged = `%s'\n", buf);

	hostset_ranged_string(set, 1024, buf);
	printf("hostset  = `%s'\n", buf);

	hostlist_sort(hl1);
	hostlist_ranged_string(hl1, 10240, buf);
	printf("sorted   = `%s'\n", buf);

	hostlist_uniq(hl1);
	hostlist_ranged_string(hl1, 10240, buf);
	printf("uniqed   = `%s'\n", buf);

	hl2 = hostlist_copy(hl1);
	printf("pop_range: ");
	while ((str = hostlist_pop_range(hl2))) {
		printf("`%s' ", str);
		free(str);
	}
	hostlist_destroy(hl2);
	printf("\n");

	hl2 = hostlist_copy(hl1);
	printf("shift_range: ");
	while ((str = hostlist_shift_range(hl2))) {
		printf("`%s' ", str);
		free(str);
	}
	hostlist_destroy(hl2);
	printf("\n");

	iter = hostset_iterator_create(set);
	iter2 = hostset_iterator_create(set);
	hostlist_iterator_destroy(iter2);

	printf("next: ");
	while ((str = hostlist_next(iter))) {
		printf("`%s' ", str);
		free(str);
	}
	printf("\n");

	hostlist_iterator_reset(iter);
	printf("next_range: ");
	while ((str = hostlist_next_range(iter))) {
		printf("`%s' ", str);
		free(str);
	}
	printf("\n");

	printf("nranges = %d\n", hostset_nranges(set));

	hostset_ranged_string(set, 1024, buf);
	printf("set = %s\n", buf);

	hostset_destroy(set);
	hostlist_destroy(hl1);
	return 0;
}

#endif		/* TEST_MAIN */

/* 
 * vi: tabstop=4 shiftwidth=4 expandtab 
 */
