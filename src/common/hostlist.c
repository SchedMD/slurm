/*****************************************************************************\
 *  hostlist.c
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/strnatcmp.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(hostlist_create,		slurm_hostlist_create);
strong_alias(hostlist_create_client,	slurm_hostlist_create_client);
strong_alias(hostlist_copy,		slurm_hostlist_copy);
strong_alias(hostlist_count,		slurm_hostlist_count);
strong_alias(hostlist_delete,		slurm_hostlist_delete);
strong_alias(hostlist_delete_host,	slurm_hostlist_delete_host);
strong_alias(hostlist_delete_nth,	slurm_hostlist_delete_nth);
strong_alias(hostlist_deranged_string,	slurm_hostlist_deranged_string);
strong_alias(hostlist_deranged_string_xmalloc,
					slurm_hostlist_deranged_string_xmalloc);
strong_alias(hostlist_destroy,		slurm_hostlist_destroy);
strong_alias(hostlist_find,		slurm_hostlist_find);
strong_alias(hostlist_iterator_create,	slurm_hostlist_iterator_create);
strong_alias(hostlist_iterator_destroy,	slurm_hostlist_iterator_destroy);
strong_alias(hostlist_iterator_reset,	slurm_hostlist_iterator_reset);
strong_alias(hostlist_next,		slurm_hostlist_next);
strong_alias(hostlist_nth,		slurm_hostlist_nth);
strong_alias(hostlist_drop, slurm_hostlist_drop);
strong_alias(hostlist_pop,		slurm_hostlist_pop);
strong_alias(hostlist_push,		slurm_hostlist_push);
strong_alias(hostlist_push_host,	slurm_hostlist_push_host);
strong_alias(hostlist_push_list,	slurm_hostlist_push_list);
strong_alias(hostlist_ranged_string,	slurm_hostlist_ranged_string);
strong_alias(hostlist_ranged_string_xmalloc,
					slurm_hostlist_ranged_string_xmalloc);
strong_alias(hostlist_remove,		slurm_hostlist_remove);
strong_alias(hostlist_shift,		slurm_hostlist_shift);
strong_alias(hostlist_sort,		slurm_hostlist_sort);
strong_alias(hostlist_split_treewidth, slurm_hostlist_split_treewidth);
strong_alias(hostlist_cmp_first,	slurm_hostlist_cmp_first);
strong_alias(hostlist_uniq,		slurm_hostlist_uniq);
strong_alias(hostset_count,		slurm_hostset_count);
strong_alias(hostset_create,		slurm_hostset_create);
strong_alias(hostset_delete,		slurm_hostset_delete);
strong_alias(hostset_destroy,		slurm_hostset_destroy);
strong_alias(hostset_find,		slurm_hostset_find);
strong_alias(hostset_insert,		slurm_hostset_insert);
strong_alias(hostset_shift,		slurm_hostset_shift);
strong_alias(hostset_within,		slurm_hostset_within);
strong_alias(hostset_nth,		slurm_hostset_nth);

#define out_of_memory(mesg)			\
do {						\
	log_oom(__FILE__, __LINE__, __func__);	\
	abort();				\
} while (0)

/*
 * Some constants and tunables:
 */

/* number of elements to allocate when extending the hostlist array */
#define HOSTLIST_CHUNK    16

/* max number of ranges that will be processed between brackets */
#define MAX_RANGES   (256*1024)    /* 256K ranks */

/* ----[ Internal Data Structures ]---- */

/* hostname type: A convenience structure used in parsing single hostnames */
typedef struct {
	char *hostname;         /* cache of initialized hostname        */
	char *prefix;           /* hostname prefix                      */
	unsigned long num;      /* numeric suffix                       */

	/* string representation of numeric suffix
	 * points into `hostname'                                       */
	char *suffix;
} hostname_t;

/* hostrange type: A single prefix with `hi' and `lo' numeric suffix values */
typedef struct {
	char *prefix;        /* alphanumeric prefix: */

	/* beginning (lo) and end (hi) of suffix range */
	unsigned long lo, hi;

	/* width of numeric output format
	 * (pad with zeros up to this width) */
	int width;

	/* If singlehost is 1, `lo' and `hi' are invalid */
	bool singlehost;
} hostrange_t;

/* The hostlist type: An array based list of hostrange_t's */
#define HOSTLIST_MAGIC 57005
struct hostlist {
	int magic;
	pthread_mutex_t mutex;

	/* current number of elements available in array */
	int size;

	/* current number of ranges stored in array */
	int nranges;

	/* current number of hosts stored in hostlist */
	int nhosts;

	/* pointer to hostrange array */
	hostrange_t **hr;

	/* list of iterators */
	struct hostlist_iterator *ilist;

};


/* a hostset is a wrapper around a hostlist */
struct hostset {
	hostlist_t *hl;
};

#define HOSTLIST_ITR_MAGIC 57007
struct hostlist_iterator {
	int magic;

	/* hostlist we are traversing */
	hostlist_t *hl;

	/* current index of iterator in hl->hr[] */
	int idx;

	/* current hostrange object in list hl, i.e. hl->hr[idx] */
	hostrange_t *hr;

	/* current depth we've traversed into range hr */
	int depth;

	/* next ptr for lists of iterators */
	struct hostlist_iterator *next;
};

struct _range {
	unsigned long lo, hi;
	int width;
};

/* ---- ---- */

/* ------[ static function prototypes ]------ */

static char * _next_tok(char *, char **);
static int    _zero_padded(unsigned long, int);
static int    _width_equiv(unsigned long, int *, unsigned long, int *);

static hostname_t *hostname_create(const char *);
static void hostname_destroy(hostname_t *);
static int hostname_suffix_is_valid(hostname_t *);
static int hostname_suffix_width(hostname_t *);

static hostrange_t *hostrange_new(void);
static hostrange_t *hostrange_create_single(const char *);
static hostrange_t *hostrange_create(char *, unsigned long, unsigned long, int);
static unsigned long hostrange_count(hostrange_t *);
static hostrange_t *hostrange_copy(hostrange_t *);
static void hostrange_destroy(hostrange_t *);
static hostrange_t *hostrange_delete_host(hostrange_t *, unsigned long);
static int hostrange_cmp(hostrange_t *, hostrange_t *);
static int hostrange_prefix_cmp(hostrange_t *, hostrange_t *);
static int hostrange_within_range(hostrange_t *, hostrange_t *);
static int hostrange_width_combine(hostrange_t *, hostrange_t *);
static int hostrange_empty(hostrange_t *);
static char *hostrange_pop(hostrange_t *);
static char *hostrange_shift(hostrange_t *);
static int hostrange_join(hostrange_t *, hostrange_t *);
static hostrange_t *hostrange_intersect(hostrange_t *, hostrange_t *);
static int hostrange_hn_within(hostrange_t *, hostname_t *);
static size_t hostrange_to_string(hostrange_t *hr, size_t, char *, char *);
static size_t hostrange_numstr(hostrange_t *, size_t, char *);

static hostlist_t *hostlist_new(void);
static hostlist_t *_hostlist_create_bracketed(const char *, char *, char *);
static void hostlist_resize(hostlist_t *, size_t);
static void hostlist_expand(hostlist_t *);
static int hostlist_push_range(hostlist_t *, hostrange_t *);
static int hostlist_push_hr(hostlist_t *, char *, char *, unsigned long,
			    unsigned long, int);
static int hostlist_insert_range(hostlist_t *, hostrange_t *, int);
static void hostlist_delete_range(hostlist_t *, int n);
static void hostlist_coalesce(hostlist_t *hl);
static void hostlist_collapse(hostlist_t *hl);
static hostlist_t *_hostlist_create(const char *, char *, char *);
static void hostlist_shift_iterators(hostlist_t *, int, int, int);
static int _attempt_range_join(hostlist_t *, int);
static int _is_bracket_needed(hostlist_t *, int);

static hostlist_iterator_t *hostlist_iterator_new(void);
static void _iterator_advance(hostlist_iterator_t *);

static int hostset_find_host(hostset_t *, const char *);

/* ------[ macros ]------ */

#define LOCK_HOSTLIST(_hl)				\
	do {						\
		xassert(_hl != NULL);			\
		slurm_mutex_lock(&(_hl)->mutex);		\
		xassert((_hl)->magic == HOSTLIST_MAGIC);	\
	} while (0)

#define UNLOCK_HOSTLIST(_hl)			\
	do {					\
		slurm_mutex_unlock(&(_hl)->mutex);	\
	} while (0)

#define seterrno_ret(_errno, _rc)		\
	do {					\
		errno = _errno;			\
		return _rc;			\
	} while (0)

/* ------[ Function Definitions ]------ */

/* ----[ general utility functions ]---- */

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
	char *tok, *parse, *open_bracket, *close_bracket;

	/* push str past any leading separators */
	while ((**str != '\0') && (strchr(sep, **str) != NULL))
		(*str)++;

	if (**str == '\0')
		return NULL;

	/* assign token ptr */
	tok = *str;
	parse = tok;

	while (1) {
		/* push str past token and leave pointing to first separator */
		*str += strcspn(*str, sep);

		/* push str past pairs of brackets */
bracket:
		open_bracket = memchr(parse, '[', *str - parse);
		if (open_bracket == NULL)
			break;
		close_bracket = strchr(parse, ']');
		if ((close_bracket == NULL) || (close_bracket < open_bracket))
			break;
		if (close_bracket < *str) {
			parse = close_bracket + 1;
			goto bracket;
		} else {
			*str = close_bracket;
		}
	}

	/* nullify consecutive separators and push str beyond them */
	while ((**str != '\0') && (strchr(sep, **str) != NULL))
		*(*str)++ = '\0';

	return tok;
}


/*
 * return the number of zeros needed to pad "num" to "width"
 */
static int _zero_padded(unsigned long num, int width)
{
	int n = 1;
	while (num /= 10L)
		n++;
	return (width > n) ? (width - n) : 0;
}

/*
 * test whether two format `width' parameters are "equivalent"
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

	if (*wn == *wm)
		return 1;

	npad  = _zero_padded(n, *wn);
	nmpad = _zero_padded(n, *wm);
	mpad  = _zero_padded(m, *wm);
	mnpad = _zero_padded(m, *wn);

	if ((npad != nmpad) && (mpad != mnpad))
		return 0;
	else if (npad != nmpad)	/* mpad == mnpad: adjust wm */
		*wm = *wn;
	else			/* npad == nmpad: adjust wn */
		*wn = *wm;
	return 1;
}


/* ----[ hostname_t functions ]---- */

/*
 * return the location of the last char in the hostname prefix
 */
static int _prefix_end_len(const char *hostname, int len)
{
	int idx = len - 1;

	while ((idx >= 0) && isdigit((int) hostname[idx]))
		idx--;

	return idx;
}

/*
 * Read the numeric suffix of a hostname that holds len characters. plen gets
 * the length of the prefix in front of it, num the value of the suffix and
 * width the number of characters it was written in.
 *
 * Returns -1 when the suffix is ULONG_MAX or larger, 0 when the name holds no
 * suffix or has other characters after the digits, and 1 when the suffix is
 * read. plen is always set, num and width only when the suffix is read.
 * ULONG_MAX is refused because a range can not end on it.
 */
static int _parse_hostname_len(const char *hostname, int len, int *plen,
			       unsigned long *num, int *width)
{
	unsigned long val;
	char *p;
	int slen;

	*plen = _prefix_end_len(hostname, len) + 1;
	slen = len - *plen;

	if (!slen)
		return 0;

	val = strtoul(hostname + *plen, &p, 10);
	if (*p)
		return 0;
	if (val == ULONG_MAX)
		return -1;

	*num = val;
	*width = slen;

	return 1;
}

/*
 * create a hostname_t object from a string hostname
 */
static hostname_t *hostname_create(const char *hostname)
{
	hostname_t *hn;
	unsigned long num = 0;
	int len, plen, width, rc;

	xassert(hostname);

	len = strlen(hostname);

	if ((rc = _parse_hostname_len(hostname, len, &plen, &num, &width)) < 0)
		return NULL;

	hn = xmalloc(sizeof(*hn));

	hn->hostname = xstrdup(hostname);
	hn->num = num;

	if (rc) {
		hn->prefix = xstrndup(hostname, plen);
		hn->suffix = hn->hostname + plen;
	} else {
		hn->prefix = xstrdup(hostname);
		hn->suffix = NULL;
	}

	return hn;
}

/* free a hostname object
 */
static void hostname_destroy(hostname_t *hn)
{
	if (hn == NULL)
		return;
	hn->suffix = NULL;
	xfree(hn->hostname);
	xfree(hn->prefix);
	xfree(hn);
}

/* return true if the hostname has a valid numeric suffix
 */
static int hostname_suffix_is_valid(hostname_t *hn)
{
	if (!hn)
		return false;
	return hn->suffix != NULL;
}

/* return the width (in characters) of the numeric part of the hostname
 */
static int hostname_suffix_width(hostname_t *hn)
{
	if (!hn)
		return -1;
	xassert(hn->suffix);
	return (int) strlen(hn->suffix);
}


/* ----[ hostrange_t functions ]---- */

/* allocate a new hostrange object
 */
static hostrange_t *hostrange_new(void)
{
	return xmalloc(sizeof(hostrange_t));
}

/* Create a hostrange_t containing a single host without a valid suffix
 * hr->prefix will represent the entire hostname.
 */
static hostrange_t *hostrange_create_single(const char *prefix)
{
	hostrange_t *new;

	xassert(prefix);

	new = hostrange_new();

	new->prefix = xstrdup(prefix);

	new->singlehost = 1;
	new->lo = 0L;
	new->hi = 0L;
	new->width = 0;

	return new;
}


/* Create a hostrange object with a prefix, hi, lo, and format width
 */
static hostrange_t *hostrange_create(char *prefix, unsigned long lo,
				     unsigned long hi, int width)
{
	hostrange_t *new;

	xassert(prefix);

	new = hostrange_new();

	new->prefix = xstrdup(prefix);

	new->lo = lo;
	new->hi = hi;
	new->width = width;

	new->singlehost = 0;

	return new;
}


/* Return the number of hosts stored in the hostrange object
 */
static unsigned long hostrange_count(hostrange_t *hr)
{
	xassert(hr);
	if (hr->singlehost)
		return 1;
	else
		return hr->hi - hr->lo + 1;
}

/* Copy a hostrange object
 */
static hostrange_t *hostrange_copy(hostrange_t *hr)
{
	xassert(hr);

	if (hr->singlehost)
		return hostrange_create_single(hr->prefix);
	else
		return hostrange_create(hr->prefix, hr->lo, hr->hi,
					hr->width);
}


/* free memory allocated by the hostrange object
 */
static void hostrange_destroy(hostrange_t *hr)
{
	if (hr == NULL)
		return;
	xfree(hr->prefix);
	xfree(hr);
}

/* hostrange_delete_host() deletes a specific host from the range.
 * If the range is split into two, the greater range is returned,
 * and `hi' of the lesser range is adjusted accordingly. If the
 * highest or lowest host is deleted from a range, NULL is returned
 * and the hostrange hr is adjusted properly.
 */
static hostrange_t *hostrange_delete_host(hostrange_t *hr, unsigned long n)
{
	hostrange_t *new = NULL;

	xassert(hr);
	xassert((n >= hr->lo) && (n <= hr->hi));

	if (n == hr->lo)
		hr->lo++;
	else if (n == hr->hi)
		hr->hi--;
	else {
		new = hostrange_copy(hr);
		hr->hi = n - 1;
		new->lo = n + 1;
	}

	return new;
}

extern int hostlist_cmp_first(hostlist_t *hl1, hostlist_t *hl2)
{
	return hostrange_cmp(hl1->hr[0], hl2->hr[0]);
}

/* hostrange_cmp() is used to sort hostrange objects. It will
 * sort based on the following (in order):
 *  o result of xstrcmp on prefixes
 *  o if widths are compatible, then:
 *       sort based on lowest suffix in range
 *    else
 *       sort based on width                     */
static int hostrange_cmp(hostrange_t *h1, hostrange_t *h2)
{
	int retval;

	xassert(h1 && h2);

	if ((retval = hostrange_prefix_cmp(h1, h2)) == 0)
		retval = hostrange_width_combine(h1, h2) ?
				 ((h1->lo > h2->lo) - (h1->lo < h2->lo)) :
				 h1->width - h2->width;

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
static int hostrange_prefix_cmp(hostrange_t *h1, hostrange_t *h2)
{
	int retval;
	if (h1 == NULL)
		return 1;
	if (h2 == NULL)
		return -1;

	if (!xstrcmp(h1->prefix, h2->prefix))
		retval = 0;
	else /* natural order puts rack2 before rack10 */
		retval = strnatcmp(h1->prefix, h2->prefix);

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
static int hostrange_within_range(hostrange_t *h1, hostrange_t *h2)
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
static int hostrange_width_combine(hostrange_t *h0, hostrange_t *h1)
{
	xassert(h0 && h1);

	return _width_equiv(h0->lo, &h0->width, h1->lo, &h1->width);
}


/* Return true if hostrange hr contains no hosts, i.e. hi < lo
 */
static int hostrange_empty(hostrange_t *hr)
{
	xassert(hr);
	return ((hr->hi < hr->lo) || (hr->hi == (unsigned long) -1));
}

/* return the string representation of the last host in hostrange hr
 * and remove that host from the range (i.e. decrement hi if possible)
 *
 * Returns NULL if malloc fails OR there are no more hosts left
 */
static char *hostrange_pop(hostrange_t *hr)
{
	size_t size = 0;
	char *host = NULL;

	xassert(hr);

	if (hr->singlehost) {
		hr->lo++;    /* effectively set count == 0 */
		host = strdup(hr->prefix);
		if (host == NULL)
			out_of_memory("hostrange pop");
	} else if (hostrange_count(hr) > 0) {
		size = strlen(hr->prefix) + hr->width + 16;
		if (!(host = malloc(size)))
			out_of_memory("hostrange pop");
		snprintf(host, size, "%s%0*lu", hr->prefix, hr->width,
			 hr->hi--);
	}

	return host;
}

/* Same as hostrange_pop(), but remove host from start of range */
static char *hostrange_shift(hostrange_t *hr)
{
	size_t size = 0;
	char *host = NULL;

	xassert(hr);

	if (hr->singlehost) {
		hr->lo++;
		if (!(host = strdup(hr->prefix)))
			out_of_memory("hostrange shift");
	} else if (hostrange_count(hr) > 0) {
		size = strlen(hr->prefix) + hr->width + 16;
		if (!(host = malloc(size)))
			out_of_memory("hostrange shift");
		snprintf(host, size, "%s%0*lu", hr->prefix, hr->width,
			 hr->lo++);
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
static int hostrange_join(hostrange_t *h1, hostrange_t *h2)
{
	int duplicated = -1;

	xassert(h1 && h2);
	xassert(hostrange_cmp(h1, h2) <= 0);

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
static hostrange_t *hostrange_intersect(hostrange_t *h1, hostrange_t *h2)
{
	hostrange_t *new = NULL;

	xassert(h1 && h2);

	if (h1->singlehost || h2->singlehost)
		return NULL;

	/* Different padding sorts by width, so h1 may start above h2. */
	if (h1->lo > h2->lo)
		return NULL;

	xassert(hostrange_cmp(h1, h2) <= 0);

	if ((h1->hi > h2->lo)
	    && (hostrange_prefix_cmp(h1, h2) == 0)
	    && (hostrange_width_combine(h1, h2))) {

		new = hostrange_copy(h1);
		new->lo = h2->lo;
		new->hi = h2->hi < h1->hi ? h2->hi : h1->hi;
	}

	return new;
}

/* return 1 if hostname hn is within the hostrange hr
 *        0 if not.
 */
static int hostrange_hn_within(hostrange_t *hr, hostname_t *hn)
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
	if (strcmp(hr->prefix, hn->prefix) != 0) {
		int len1, len2, ldiff;

		/* Below logic was added since primarily for a cray
		 * where people typically drop
		 * leading zeros into the prefix so you can do
		 * something like nid0000[2-7].  But doing this messes
		 * up the hostlist_find since when someone queries
		 * against nid00002 the prefixes don't match.  The
		 * below code is there to make sure get the best
		 * chance for comparison.
		 */

		/* First see if by taking some of the leading digits of the
		 * suffix of hn and moving it to the end of the prefix if it
		 * would be a match.
		 */
		len1  = strlen(hr->prefix);
		len2  = strlen(hn->prefix);

		/* These are definitely different */
		if (len1 == len2)
			return 0;

		ldiff = len1 - len2;

		if (ldiff > 0 && (strlen(hn->suffix) >= ldiff)) {
			/* Tack on ldiff of the hostname's suffix to
			 * that of it's prefix */
			xstrncat(hn->prefix, hn->suffix, ldiff);
		} else if (ldiff < 0) {
			/* strip off the ldiff here */
			hn->prefix[len2+ldiff] = '\0';
		} else
			return 0;

		/* Now adjust the suffix of the hostname object. */
		hn->suffix += ldiff;
		/* And the numeric representation just in case
		 * whatever we just tacked on to the prefix
		 * had something other than 0 in it.
		 */
		hn->num = strtoul(hn->suffix, NULL, 10);

		/* Now compare them and see if they match */
		if (strcmp(hr->prefix, hn->prefix) != 0)
			return 0;
	}

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
static size_t hostrange_to_string(hostrange_t *hr, size_t n, char *buf,
				  char *separator)
{
	unsigned long i;
	int ret, len = 0;
	char sep = separator == NULL ? ',' : separator[0];

	if (n == 0)
		return 0;

	xassert(hr);

	if (hr->singlehost) {
		ret = snprintf(buf, n, "%s", hr->prefix);
		if (ret < 0 || ret >= n)
			goto truncated;
		return ret;
	}

	for (i = hr->lo; i <= hr->hi; i++) {
		if (i > hr->lo)
			buf[len++] = sep;
		if (len >= n)
			goto truncated;

		ret = snprintf(buf + len, n - len, "%s%0*lu", hr->prefix,
			       hr->width, i);
		if ((ret < 0) || ((len += ret) >= n))
			goto truncated;
	}

	buf[len] = '\0';
	return len;
truncated:
	buf[n-1] = '\0';
	return -1;
}

/* Place the string representation of the numeric part of hostrange into buf
 * writing at most n chars including NUL termination.
 */
static size_t hostrange_numstr(hostrange_t *hr, size_t n, char *buf)
{
	int len = 0;

	xassert(buf && hr);

	if (hr->singlehost || n == 0)
		return 0;

	len = snprintf(buf, n, "%0*lu", hr->width, hr->lo);
	if ((len < 0) || (len >= n))
		return -1;

	if (hr->lo < hr->hi) {
		int len2 = snprintf(buf + len, n - len, "-%0*lu", hr->width,
				    hr->hi);
		if ((len2 < 0) || ((len += len2) >= n))
			return -1;
	}

	return len;
}


/* ----[ hostlist functions ]---- */

/* Create a new hostlist object.
 * Returns an empty hostlist, or NULL if memory allocation fails.
 */
static hostlist_t *hostlist_new(void)
{
	int i;
	hostlist_t *new = xmalloc(sizeof(*new));

	new->magic = HOSTLIST_MAGIC;
	slurm_mutex_init(&new->mutex);

	new->hr = xcalloc(HOSTLIST_CHUNK, sizeof(hostrange_t *));

	/* set entries in hostrange array to NULL */
	for (i = 0; i < HOSTLIST_CHUNK; i++)
		new->hr[i] = NULL;

	new->size = HOSTLIST_CHUNK;
	new->nranges = 0;
	new->nhosts = 0;
	new->ilist = NULL;
	return new;
}


/* Resize the internal array used to store the list of hostrange objects.
 *
 * It is assumed that the caller has the hostlist hl locked
 */
static void hostlist_resize(hostlist_t *hl, size_t newsize)
{
	xassert(hl);
	xassert(hl->magic == HOSTLIST_MAGIC);
	hl->size = newsize;
	xrecalloc(hl->hr, hl->size, sizeof(hostrange_t *));
}

/* Resize hostlist by one HOSTLIST_CHUNK
 * Assumes that hostlist hl is locked by caller
 */
static void hostlist_expand(hostlist_t *hl)
{
	hostlist_resize(hl, hl->size + HOSTLIST_CHUNK);
}

/* Push a hostrange object onto hostlist hl
 * Returns the number of hosts successfully pushed onto hl
 */
static int hostlist_push_range(hostlist_t *hl, hostrange_t *hr)
{
	hostrange_t *tail;
	int retval;

	xassert(hr);
	LOCK_HOSTLIST(hl);

	tail = (hl->nranges > 0) ? hl->hr[hl->nranges-1] : hl->hr[0];

	if (hl->size == hl->nranges)
		hostlist_expand(hl);

	if (hl->nranges > 0
	    && tail->hi == hr->lo - 1
	    && hostrange_prefix_cmp(tail, hr) == 0
	    && hostrange_width_combine(tail, hr)) {
		tail->hi = hr->hi;
	} else {
		hostrange_t *new = hostrange_copy(hr);
		hl->hr[hl->nranges++] = new;
	}

	retval = hl->nhosts += hostrange_count(hr);

	UNLOCK_HOSTLIST(hl);

	return retval;
}



/* Same as hostlist_push_range() above, but prefix, lo, hi, and width
 * are passed as args
 */
static int hostlist_push_hr(hostlist_t *hl, char *prefix, char *suffix,
			    unsigned long lo, unsigned long hi, int width)
{
	int retval = 0;
	hostrange_t *hr;
	if (suffix) {
		char *host = NULL;
		unsigned long i;

		xassert(prefix);

		hr = hostrange_new();
		hr->singlehost = 1;
		hr->lo = 0L;
		hr->hi = 0L;
		hr->width = 0;

		for (i = lo; i <= hi; i++) {
			xstrfmtcat(host, "%s%0*lu%s", prefix, width, i, suffix);
			hr->prefix = host;
			retval += hostlist_push_range(hl, hr);
			xfree(host);
		}
		hr->prefix = NULL;
	} else {
		hr = hostrange_create(prefix, lo, hi, width);
		retval = hostlist_push_range(hl, hr);
	}
	hostrange_destroy(hr);
	return retval;
}

/* Insert a range object hr into position n of the hostlist hl
 * Assumes that hl->mutex is already held by calling process
 */
static int hostlist_insert_range(hostlist_t *hl, hostrange_t *hr, int n)
{
	int i;
	hostrange_t *tmp;
	hostlist_iterator_t *hli;

	xassert(hl && hr);
	xassert(hl->magic == HOSTLIST_MAGIC);

	if (n > hl->nranges)
		return 0;

	if (hl->size == hl->nranges)
		hostlist_expand(hl);

	/* copy new hostrange into slot "n" in array */
	tmp = hl->hr[n];
	hl->hr[n] = hostrange_copy(hr);

	/* push remaining hostrange entries up */
	for (i = n + 1; i < hl->nranges + 1; i++) {
		hostrange_t *last = hl->hr[i];
		hl->hr[i] = tmp;
		tmp = last;
	}
	hl->nranges++;

	/* adjust hostlist iterators if needed */
	for (hli = hl->ilist; hli; hli = hli->next) {
		if (hli->idx >= n) {
			hli->idx++;
			if (hli->idx < hl->nranges)
				hli->hr = hl->hr[hli->idx];
			else
				hli->hr = NULL;
		}
	}

	return 1;
}

/* Delete the range at position n in the range array
 * Assumes the hostlist lock is already held.
 */
static void hostlist_delete_range(hostlist_t *hl, int n)
{
	int i;
	hostrange_t *old;

	xassert(hl);
	xassert(hl->magic == HOSTLIST_MAGIC);
	xassert((n < hl->nranges) && (n >= 0));

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
hostlist_t *_hostlist_create(const char *hostlist, char *sep, char *r_op)
{
	char *str, *orig;
	char *tok, *cur;
	int high, low, fmt = 0;
	char prefix[256] = "";
	int pos = 0;
	int error = 0;
	char range_op = r_op[0];/* XXX support > 1 char range ops in future? */

	hostlist_t *new = hostlist_new();

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
		 *   do this by finding last occurrence of range_op in str */
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
			memcpy(prefix, tok, (size_t) pos);
			prefix[pos] = '\0';

			/* push pointer past prefix */
			tok += pos;

			/* count number of digits for output fmt */
			for (fmt = 0; isdigit(tok[fmt]); ++fmt) {;}

			if (fmt == 0)
				error = 1;

		} else
			tok += pos;

		/* get lower bound */
		low = strtoul(tok, (char **) &tok, 10);

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
				    xstrncmp(prefix, tok, pos) != 0)
					error = 1;
			}

			if (*tok != '\0')
				tok += pos;

			/* make sure we have digits to the end */
			for (pos = 0;
			     tok[pos] && isdigit((char) tok[pos]);
			     ++pos) {;}

			if (pos > 0) {    /* we have digits to process */
				high = strtoul(tok, (char **) &tok, 10);
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
			hostlist_push_hr(new, prefix, NULL, low, high, fmt);
		}

		error = 0;
	}

done:
	if (orig)
		free(orig);

	return new;
}

#else                /* !WANT_RECKLESS_HOSTRANGE_EXPANSION */

hostlist_t *_hostlist_create(const char *hostlist, char *sep, char *r_op)
{
	return _hostlist_create_bracketed(hostlist, sep, r_op);
}

#endif                /* WANT_RECKLESS_HOSTRANGE_EXPANSION */

static int _grow_ranges(struct _range * *ranges,	/* in/out */
			int *capacity,			/* in/out */
			int max_capacity)
{
	int new_capacity;

	if ((*capacity) >= max_capacity)
		fatal("%s: Can't grow ranges -- already at max", __func__);

	new_capacity = (*capacity) * 2 + 10;
	if (new_capacity > max_capacity)
		new_capacity = max_capacity;

	xrealloc_nz((*ranges), (sizeof(struct _range) * new_capacity));
	*capacity = new_capacity;

	return 1;
}

/* Grab a single range from str
 * returns 1 if str contained a valid number or range,
 *         0 if conversion of str to a range failed.
 */
static int _parse_single_range(char *str, struct _range *range)
{
	char *p, *q;
	char *orig = strdup(str);

	if (!orig)
		seterrno_ret(ENOMEM, 0);

	if ((p = strchr(str, '-'))) {
		*p++ = '\0';
		if (*p == '-') {   /* do NOT allow negative numbers */
			error("%s: Invalid range: `%s'", __func__, orig);
			free(orig);
			return 0;
		}
	}

	range->width = strlen(str);
	range->lo = strtoul(str, &q, 10);

	if ((q == str) || *q) {
		error("%s: Invalid range: `%s'", __func__, orig);
		free(orig);
		return 0;
	}

	range->hi = (p && *p) ? strtoul(p, &q, 10) : range->lo;

	if ((q == p) || *q || (range->hi == ULONG_MAX)) {
		error("%s: Invalid range: `%s'", __func__, orig);
		free(orig);
		return 0;
	}

	if (range->lo > range->hi) {
		error("%s: Invalid range: `%s'", __func__, orig);
		free(orig);
		return 0;
	}

	if (range->hi - range->lo + 1 > MAX_RANGE) {
		error("%s: Too many hosts in range `%s'", __func__, orig);
		free(orig);
		return 0;
	}

	free(orig);
	return 1;
}

/*
 * Convert 'str' containing comma separated digits and ranges into an array
 *  of struct _range types (dynamically allocated and resized).
 *
 * Return number of ranges created, or -1 on error.
 */
static int _parse_range_list(char *str,
			     struct _range * *ranges, int *capacity,
			     int max_capacity)
{
	char *p;
	int count = 0;

	while (str) {
		if (count == max_capacity)
			fatal("%s: Too many ranges, can't process entire list",
			      __func__);
		if ((p = strchr(str, ',')))
			*p++ = '\0';
/* 		info("looking at %s", str); */
		if (count >= (*capacity)) {
			if (!_grow_ranges(ranges, capacity, max_capacity))
				return -1;
		}
		if (!_parse_single_range(str, &(*ranges)[count++]))
			return -1;
		str = p;
	}
	return count;
}

/* Validate prefix and push with the numeric suffix onto the hostlist
 * The prefix can contain a up to one range expresseion (e.g. "rack[1-4]_").
 * RET 0 on success, -1 on failure (invalid prefix) */
static int _push_range_list(hostlist_t *hl, char *prefix, char *suffix,
			    struct _range *range, int n)
{
	int i, k, nr;
	char *p, *q;
	char *new_prefix = NULL;

	if (((p = strrchr(prefix, '[')) != NULL) &&
	    ((q = strrchr(p, ']')) != NULL)) {
		struct _range *prefix_range = NULL;
		int pr_capacity = 0;
		struct _range *saved_range = range, *pre_range;
		unsigned long j, prefix_cnt = 0;
		bool recurse = false;
		*p++ = '\0';
		*q++ = '\0';
		if (strrchr(prefix, '[') != NULL)
			recurse = true;
		nr = _parse_range_list(p, &prefix_range, &pr_capacity,
				       MAX_RANGES);
		if (nr < 0) {
			xfree(prefix_range);
			return -1;	/* bad numeric expression */
		}
		pre_range = prefix_range;
		for (i = 0; i < nr; i++) {
			prefix_cnt += pre_range->hi - pre_range->lo + 1;
			if (prefix_cnt > MAX_PREFIX_CNT) {
				/* Prevent overflow of memory with user input
				 * of something like "a[0-999999999].b[0-9]" */
				xfree(prefix_range);
				return -1;
			}
			for (j = pre_range->lo; j <= pre_range->hi; j++) {
				xstrfmtcat(new_prefix, "%s%0*lu%s",
					   prefix, pre_range->width, j, q);
				if (recurse) {
					if (_push_range_list(hl, new_prefix,
							     suffix,
							     saved_range, n)) {
						xfree(new_prefix);
						xfree(prefix_range);
						return -1;
					}
				} else {
					range = saved_range;
					for (k = 0; k < n; k++) {
						hostlist_push_hr(hl, new_prefix,
								 suffix,
								 range->lo,
								 range->hi,
								 range->width);
						range++;
					}
				}
				xfree(new_prefix);
			}
			pre_range++;
		}
		xfree(prefix_range);
		return 0;
	}

	for (k = 0; k < n; k++) {
		hostlist_push_hr(hl, prefix, suffix,
				 range->lo, range->hi, range->width);
		range++;
	}
	return 0;
}

/*
 * Create a hostlist from a string with brackets '[' ']' to aid
 * detection of ranges and compressed lists
 */
static hostlist_t *_hostlist_create_bracketed(const char *hostlist, char *sep,
					      char *r_op)
{
	hostlist_t *new = hostlist_new();
	struct _range *ranges = NULL;
	int capacity = 0;
	int nr, err;
	char *p, *tok, *str, *orig;

	if (hostlist == NULL)
		return new;

	if (!(orig = str = strdup(hostlist))) {
		hostlist_destroy(new);
		return NULL;
	}

	while ((tok = _next_tok(sep, &str)) != NULL) {
		if ((p = strrchr(tok, '[')) != NULL) {
			char *q, *prefix = tok, *suffix = NULL;
			*p++ = '\0';

			if ((q = strchr(p, ']'))) {
				if ((q[1] != ',') && (q[1] != '\0'))
					suffix = q + 1;
				*q = '\0';
				nr = _parse_range_list(p,
						       &ranges,
						       &capacity, MAX_RANGES);
				if (nr < 0)
					goto error;
				if (_push_range_list(new, prefix, suffix,
						     ranges, nr))
					goto error;
			} else {
				/* Found '[' but ']' is missing. */
				goto error;
			}
		} else {
			if (!hostlist_push_host(new, tok))
				goto error;
		}
	}
	xfree(ranges);

	free(orig);
	return new;

error:
	err = errno = EINVAL;
	hostlist_destroy(new);
	xfree(ranges);
	free(orig);
	seterrno_ret(err, NULL);
}

/*
 * destroy hostlist iterator
 * requires hostlist mutex to be locked
 */
static void _hostlist_iterator_destroy(hostlist_iterator_t *i)
{
	if (!i)
		return;

	xassert(i->magic == HOSTLIST_ITR_MAGIC);

	for (hostlist_iterator_t **pi = &i->hl->ilist; *pi; pi = &(*pi)->next) {
		xassert((*pi)->magic == HOSTLIST_ITR_MAGIC);
		if (*pi == i) {
			*pi = (*pi)->next;
			break;
		}
	}

	xfree(i);
}

hostlist_t *hostlist_create(const char *str)
{
	return _hostlist_create(str, "\t, \n", "-");
}

extern hostlist_t *hostlist_create_client(const char *str)
{
	if (xstrchr(str, '{')) {
		char *hostlist = NULL;
		hostlist_t *hl = NULL;
		if (!slurm_controller_hostlist_expansion(str, &hostlist)) {
			hl = _hostlist_create(hostlist, "\t, \n", "-");
			xfree(hostlist);
			return hl;
		} else {
			error("%s: controller failed to expand hostlist function",
			      __func__);
		}
	}
	return _hostlist_create(str, "\t, \n", "-");
}

hostlist_t *hostlist_copy(hostlist_t *hl)
{
	int i;
	hostlist_t *new;

	if (!hl)
		return NULL;

	LOCK_HOSTLIST(hl);
	new = hostlist_new();

	new->nranges = hl->nranges;
	new->nhosts = hl->nhosts;
	if (new->nranges > new->size)
		hostlist_resize(new, new->nranges);

	for (i = 0; i < hl->nranges; i++)
		new->hr[i] = hostrange_copy(hl->hr[i]);

	UNLOCK_HOSTLIST(hl);
	return new;
}

void hostlist_destroy(hostlist_t *hl)
{
	int i;
	if (!hl)
		return;
	LOCK_HOSTLIST(hl);
	while (hl->ilist)
		_hostlist_iterator_destroy(hl->ilist);
	for (i = 0; i < hl->nranges; i++)
		hostrange_destroy(hl->hr[i]);
	xfree(hl->hr);
	UNLOCK_HOSTLIST(hl);
	slurm_mutex_destroy(&hl->mutex);
	xfree(hl);
}

int hostlist_push(hostlist_t *hl, const char *hosts)
{
	hostlist_t *new;
	int retval;
	if (!hosts || !hl)
		return 0;
	if (!(new = hostlist_create(hosts)))
		return 0;
	LOCK_HOSTLIST(new);
	retval = new->nhosts;
	UNLOCK_HOSTLIST(new);
	hostlist_push_list(hl, new);
	hostlist_destroy(new);
	return retval;
}

int hostlist_push_host(hostlist_t *hl, const char *str)
{
	/* hostlist_push_range() folds or copies, so it keeps no pointer here. */
	hostrange_t hr = { .singlehost = 1 };
	char prefix_buf[HOST_NAME_MAX + 1];
	char *prefix = prefix_buf;
	int len, plen, rc;

	if (!str || !hl)
		return 0;

	len = strlen(str);

	rc = _parse_hostname_len(str, len, &plen, &hr.lo, &hr.width);
	if (rc < 0) {
		error("%s: Invalid host name: `%s'", __func__, str);
		return 0;
	}

	if (rc) {
		if ((size_t) plen >= sizeof(prefix_buf))
			prefix = xmalloc(plen + 1);
		memcpy(prefix, str, plen);
		prefix[plen] = '\0';
		hr.hi = hr.lo;
		hr.prefix = prefix;
		hr.singlehost = 0;
	} else {
		hr.prefix = (char *) str;
	}

	hostlist_push_range(hl, &hr);

	if (prefix != prefix_buf)
		xfree(prefix);

	return 1;
}

int hostlist_push_list(hostlist_t *h1, hostlist_t *h2)
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

extern void hostlist_drop(hostlist_t *hl)
{
	if (!hl) {
		error("%s: no hostlist given", __func__);
		return;
	}

	LOCK_HOSTLIST(hl);
	if (hl->nhosts > 0) {
		hostrange_t *hr = hl->hr[hl->nranges - 1];

		if (hostrange_count(hr) > 0)
			hr->hi--;

		hl->nhosts--;

		if (hr->singlehost || hostrange_empty(hr)) {
			hostrange_destroy(hl->hr[--hl->nranges]);
			hl->hr[hl->nranges] = NULL;
		}
	}
	UNLOCK_HOSTLIST(hl);
}

char *hostlist_pop(hostlist_t *hl)
{
	char *host = NULL;
	if (!hl) {
		error("%s: no hostlist given", __func__);
		return NULL;
	}

	LOCK_HOSTLIST(hl);
	if (hl->nhosts > 0) {
		hostrange_t *hr = hl->hr[hl->nranges - 1];
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
static void hostlist_shift_iterators(hostlist_t *hl, int idx, int depth, int n)
{
	hostlist_iterator_t *i;
	if (!hl) {
		error("%s: no hostlist given", __func__);
		return;
	}
	for (i = hl->ilist; i; i = i->next) {
		if (n == 0) {
			if (i->idx == idx && i->depth >= depth)
				i->depth = i->depth > -1 ? i->depth - 1 : -1;
		} else {
			/* Modify iter if rm indexes have already been seen */
			if (i->idx >= idx) {
				/* Current referenced host range was deleted */
				if (i->idx < (idx + n)) {
					/*
					 * idx is now the index of the first
					 * host range after the deleted ones
					 */
					i->idx = idx;
					i->depth = -1; /* reset depth */
				} else {
					/*
					 * Current host range not deleted,
					 * update i->idx to the current host
					 * range's new index but keep depth the
					 * same.
					 */
					i->idx -= n;
				}

				i->hr = i->hl->hr[i->idx];
			}
		}
	}
}

char *hostlist_shift(hostlist_t *hl)
{
	char *host = NULL;

	if (!hl){
		error("%s: no hostlist given", __func__);
		return NULL;
	}

	LOCK_HOSTLIST(hl);

	if (hl->nhosts > 0) {
		hostrange_t *hr = hl->hr[0];

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

/* XXX: Note: efficiency improvements needed */
int hostlist_delete(hostlist_t *hl, const char *hosts)
{
	int n = 0;
	char *hostname = NULL;
	hostlist_t *hltmp;
	if (!hl)
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
int hostlist_delete_host(hostlist_t *hl, const char *hostname)
{
	int n;

	if (!hl)
		return -1;
	n = hostlist_find(hl, hostname);

	if (n >= 0)
		hostlist_delete_nth(hl, n);
	return n >= 0 ? 1 : 0;
}


static char *_hostrange_string(hostrange_t *hr, int depth)
{
	char buf[HOST_NAME_MAX + 16];
	const int size = sizeof(buf);
	int  len = snprintf(buf, size, "%s", hr->prefix);

	if ((len < 0) || (len + 1 >= size))
		return NULL;

	if (!hr->singlehost) {
		int ret = snprintf(buf + len, size - len, "%0*lu", hr->width,
				   hr->lo + depth);
		if ((ret < 0) || ((len + ret) >= size))
			return NULL;
	}
	return strdup(buf);
}

char *hostlist_nth(hostlist_t *hl, int n)
{
	char *host = NULL;
	int   i, count;

	if (!hl)
		return NULL;
	LOCK_HOSTLIST(hl);
	xassert(n >= 0);

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


int hostlist_delete_nth(hostlist_t *hl, int n)
{
	int i, count, rc = 0;

	if (!hl)
		return -1;
	LOCK_HOSTLIST(hl);
	xassert(n >= 0 && n < hl->nhosts);

	count = 0;

	for (i = 0; i < hl->nranges; i++) {
		int num_in_range = hostrange_count(hl->hr[i]);
		hostrange_t *hr = hl->hr[i];

		if (n <= (num_in_range - 1 + count)) {
			unsigned long num = hr->lo + n - count;
			hostrange_t *new;

			if (hr->singlehost) { /* this wasn't a range */
				hostlist_delete_range(hl, i);
			} else if ((new = hostrange_delete_host(hr, num))) {
				hostlist_insert_range(hl, new, i + 1);
				hostrange_destroy(new);
			} else if (hostrange_empty(hr))
				hostlist_delete_range(hl, i);

			hl->nhosts--;
			rc = 1;
			break;
		} else
			count += num_in_range;

	}

	UNLOCK_HOSTLIST(hl);
	return rc;
}

int hostlist_count(hostlist_t *hl)
{
	int retval;
	if (!hl)
		return -1;

	LOCK_HOSTLIST(hl);
	retval = hl->nhosts;
	UNLOCK_HOSTLIST(hl);
	return retval;
}

int hostlist_find(hostlist_t *hl, const char *hostname)
{
	int i, count, ret = -1;
	hostname_t *hn;

	if (!hostname || !hl)
		return -1;

	if (!(hn = hostname_create(hostname)))
		return -1;

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
	hostrange_t **h1 = (hostrange_t **) hr1;
	hostrange_t **h2 = (hostrange_t **) hr2;
	return hostrange_cmp((hostrange_t *) *h1, (hostrange_t *) *h2);
}


void hostlist_sort(hostlist_t *hl)
{
	hostlist_iterator_t *i;
	LOCK_HOSTLIST(hl);

	if (hl->nranges <= 1) {
		UNLOCK_HOSTLIST(hl);
		return;
	}

	qsort(hl->hr, hl->nranges, sizeof(hostrange_t *), &_cmp);

	/* reset all iterators */
	for (i = hl->ilist; i; i = i->next)
		hostlist_iterator_reset(i);

	UNLOCK_HOSTLIST(hl);

	hostlist_coalesce(hl);

}


/* search through hostlist for ranges that can be collapsed
 * does =not= delete any hosts
 */
static void hostlist_collapse(hostlist_t *hl)
{
	int i;

	LOCK_HOSTLIST(hl);
	for (i = hl->nranges - 1; i > 0; i--) {
		hostrange_t *hprev = hl->hr[i - 1];
		hostrange_t *hnext = hl->hr[i];

		if (hprev->hi == hnext->lo - 1 &&
		    hostrange_prefix_cmp(hprev, hnext) == 0 &&
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
static void hostlist_coalesce(hostlist_t *hl)
{
	int i, j;
	hostrange_t *new;

	LOCK_HOSTLIST(hl);

	for (i = hl->nranges - 1; i > 0; i--) {

		new = hostrange_intersect(hl->hr[i - 1], hl->hr[i]);

		if (new) {
			hostrange_t *hprev = hl->hr[i - 1];
			hostrange_t *hnext = hl->hr[i];
			j = i;

			if (new->hi < hprev->hi)
				hnext->hi = hprev->hi;

			hprev->hi = new->lo;
			hnext->lo = new->hi;

			if (hostrange_empty(hprev))
				hostlist_delete_range(hl, i);

			while (new->lo <= new->hi) {
				hostrange_t *hr = hostrange_create(new->prefix,
								   new->lo, new->lo,
								   new->width);

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
/* returns -1 if no range join occurred */
static int _attempt_range_join(hostlist_t *hl, int loc)
{
	int ndup;
	xassert(hl);
	xassert(hl->magic == HOSTLIST_MAGIC);
	xassert(loc > 0);
	xassert(loc < hl->nranges);
	ndup = hostrange_join(hl->hr[loc - 1], hl->hr[loc]);
	if (ndup >= 0) {
		hostlist_delete_range(hl, loc);
		hl->nhosts -= ndup;
	}
	return ndup;
}

void hostlist_uniq(hostlist_t *hl)
{
	int i = 1;
	hostlist_iterator_t *hli;
	LOCK_HOSTLIST(hl);
	if (hl->nranges <= 1) {
		UNLOCK_HOSTLIST(hl);
		return;
	}
	qsort(hl->hr, hl->nranges, sizeof(hostrange_t *), &_cmp);

	while (i < hl->nranges) {
		if (_attempt_range_join(hl, i) < 0) /* No range join occurred */
			i++;
	}

	/* reset all iterators */
	for (hli = hl->ilist; hli; hli = hli->next)
		hostlist_iterator_reset(hli);

	UNLOCK_HOSTLIST(hl);
}

hostlist_t *hostlist_deduplicate(const char *hosts)
{
	hostlist_t *hl, *uniq_hl;
	hostlist_iterator_t *itr;
	char *host;

	hl = hostlist_create(hosts);
	uniq_hl = hostlist_create(NULL);
	if (!hl)
		return uniq_hl;

	itr = hostlist_iterator_create(hl);
	while ((host = hostlist_next(itr))) {
		if (hostlist_find(uniq_hl, host) == -1)
			hostlist_push_host(uniq_hl, host);
		free(host);
	}
	hostlist_iterator_destroy(itr);
	hostlist_destroy(hl);

	return uniq_hl;
}

char *hostlist_deranged_string_xmalloc(hostlist_t *hl)
{
	int buf_size = 8192;
	char *buf = xmalloc_nz(buf_size);

	while (hostlist_deranged_string(hl, buf_size, buf) < 0) {
		buf_size *= 2;
		xrealloc_nz(buf, buf_size);
	}
	return buf;
}

ssize_t hostlist_deranged_string(hostlist_t *hl, size_t n, char *buf)
{
	int i;
	int len = 0, ret;

	LOCK_HOSTLIST(hl);
	for (i = 0; i < hl->nranges && len < n; i++) {
		if (i)
			buf[len++] = ',';
		if (len >= n)
			goto truncated;
		ret = hostrange_to_string(hl->hr[i], n - len, buf + len, ",");
		if (ret < 0)
			goto truncated;
		len += ret;
	}
	UNLOCK_HOSTLIST(hl);
	return len;
truncated:
	UNLOCK_HOSTLIST(hl);
	buf[n-1] = '\0';
	return -1;
}

/* this is used to set how many nodes are going to be on each branch
 * of the tree.
 * IN total       - total number of nodes to send to
 * IN tree_width  - how wide the tree should be on each hop
 * OUT span       - pointer to int array tree_width in length each space
 *		    containing the number of nodes to send to each hop
 *		    on the span.
 * RET int	  - the number of levels opened in the tree, or SLURM_ERROR
 */
static int _set_span(int total, uint16_t tree_width, int **span)
{
	int depth = 0;

	/* This should not happen. This is an error. */
	if (!span || total < 1)
		return SLURM_ERROR;

	/* If default span */
	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	/* Safeguard from leaks */
	if (*span)
		xfree(*span);

	/*
	 * Memory optimization:
	 * Don't allocate if we are in the last step to the leaves, as this is
	 * considered direct communication and we don't really need it.
	 */
	if (total <= tree_width)
		return 1;

	/* Each cell will contain the #nodes below this specific branch */
	*span = xcalloc(tree_width, sizeof(int));

	/*
	 * Try to fill levels until no more nodes are available.
	 *
	 * Each time a new level is created, it is exponentially bigger than the
	 * previous one
	 */
	for (uint32_t branch_capacity = 1, level_capacity = tree_width; total;
	     branch_capacity *= tree_width, level_capacity *= tree_width) {
		/* Remaining nodes can fill a whole new level up, or not */
		if (level_capacity <= total) {
			for (int i = 0; i < tree_width; i++)
				(*span)[i] += branch_capacity;
			total -= level_capacity;
		} else {
			/* Evenly distribute remaining nodes */
			branch_capacity = total / tree_width;
			/* But left the division remainder ones */
			level_capacity = branch_capacity * tree_width;
			/* Fill current level up, as much as possible */
			for (int i = 0; i < tree_width; i++)
				(*span)[i] += branch_capacity;
			total -= level_capacity;

			/* Evenly distribute the remainder nodes */
			for (int i = 0; total; i++, total--)
				(*span)[i]++;

			/* total == 0 always at this point */
		}

		/* One level more has been added */
		depth++;

		/* The level needed all the nodes, no more levels are added */
		if (!total)
			break;
	}

	/* Inform the caller about the number of levels below itself */
	return depth;
}

/* Split a hostlist into a fanout tree */
extern int hostlist_split_treewidth(hostlist_t *hl, hostlist_t ***sp_hl,
				    int *count, uint16_t tree_width)
{
	int host_count = hostlist_count(hl), depth, *span = NULL;
	char *name;

	/* If default span */
	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	/* This should not happen. This is an error. */
	if ((depth = _set_span(host_count, tree_width, &span)) < 0)
		return SLURM_ERROR;

	/*
	 * Memory optimization:
	 * _set_span doesn't return array for direct communication
	 * (if depth == 1 then span == NULL), so we just fill the hostlist array
	 * directly.
	 */
	if (depth == 1)
		tree_width = host_count;

	/* Each cell will contain the hostlist below this specific branch */
	*sp_hl = xcalloc(tree_width, sizeof(hostlist_t *));

	/*
	 * Fill the hostlists for each branch according to the distribution in
	 * set_span.
	 *
	 * Additionally, try to preserve network locality (based on distance)
	 * for subtrees, by assuming consecutive nodes are placed one next to
	 * each other
	 */
	for (*count = 0; (*count < tree_width) && (name = hostlist_shift(hl));
	     (*count)++) {
		/* Open the new branch, and add the 1st one */
		(*sp_hl)[*count] = hostlist_create(name);
		free(name);

		/* Consecutively add the rest of nodes for this branch */
		for (int i = 1; span && (i < span[*count]); i++) {
			name = hostlist_shift(hl);
			hostlist_push_host((*sp_hl)[*count], name);
			free(name);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			char *buf =
			hostlist_ranged_string_xmalloc((*sp_hl)[*count]);
			debug("ROUTE: ... sublist[%d] %s", *count, buf);
			xfree(buf);
		}
	}

	xfree(span);
	return depth;
}

/* return true if a bracket is needed for the range at i in hostlist hl */
static int _is_bracket_needed(hostlist_t *hl, int i)
{
	hostrange_t *h1 = hl->hr[i];
	hostrange_t *h2 = i < hl->nranges - 1 ? hl->hr[i + 1] : NULL;
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
static int _get_bracketed_list(hostlist_t *hl, int *start, const size_t n,
			       char *buf)
{
	hostrange_t **hr = hl->hr;
	int i = *start;
	int m, len = 0;
	int bracket_needed = _is_bracket_needed(hl, i);

	len = snprintf(buf, n, "%s", hr[i]->prefix);
	if (len < 0 || len + 4 >= n)	/* min: '[', <digit>, ']', '\0' */
		return n;		/* truncated, buffer filled */

	if (bracket_needed)
		buf[len++] = '[';

	do {
		if (i > *start)
			buf[len++] = ',';
		m = hostrange_numstr(hr[i], n - len, buf + len);
		if (m < 0 || (len += m) >= n - 1)	/* insufficient space */
			return n;
	} while (++i < hl->nranges && hostrange_within_range(hr[i], hr[i-1]));

	if (bracket_needed)
		buf[len++] = ']';

	buf[len] = '\0';
	*start = i;
	return len;
}

char *hostlist_ranged_string_malloc(hostlist_t *hl)
{
	int buf_size = 8192;
	char *buf = malloc(buf_size);
	while (buf && (hostlist_ranged_string(hl, buf_size, buf) < 0)) {
		buf_size *= 2;
		buf = realloc(buf, buf_size);
	}
	if (buf == NULL)
		out_of_memory("hostlist_ranged_string_malloc");
	return buf;
}

char *hostlist_ranged_string_xmalloc(hostlist_t *hl)
{
	int buf_size = 8192;
	char *buf = xmalloc_nz(buf_size);

	while (hostlist_ranged_string(hl, buf_size, buf) < 0) {
		buf_size *= 2;
		xrealloc_nz(buf, buf_size);
	}
	return buf;
}

ssize_t hostlist_ranged_string(hostlist_t *hl, size_t n, char *buf)
{
	int len = 0;
	bool truncated = false;

	LOCK_HOSTLIST(hl);

	for (int i = 0; (i < hl->nranges) && (len < n);) {
		if (i)
			buf[len++] = ',';
		len += _get_bracketed_list(hl, &i, n - len, buf + len);
	}

	UNLOCK_HOSTLIST(hl);

	/* NULL terminate */
	if (len >= n) {
		truncated = true;
		if (n > 0)
			buf[n - 1] = '\0';
	} else {
		buf[len] = '\0';
	}

	return truncated ? -1 : len;
}

/* ----[ hostlist iterator functions ]---- */

static hostlist_iterator_t *hostlist_iterator_new(void)
{
	hostlist_iterator_t *i = xmalloc(sizeof(*i));

	i->magic = HOSTLIST_ITR_MAGIC;
	i->hl = NULL;
	i->hr = NULL;
	i->idx = 0;
	i->depth = -1;
	i->next = i;

	return i;
}

hostlist_iterator_t *hostlist_iterator_create(hostlist_t *hl)
{
	hostlist_iterator_t *i = hostlist_iterator_new();

	LOCK_HOSTLIST(hl);
	i->hl = hl;
	i->hr = hl->hr[0];
	i->next = hl->ilist;
	hl->ilist = i;
	UNLOCK_HOSTLIST(hl);
	return i;
}

hostlist_iterator_t *hostset_iterator_create(hostset_t *set)
{
	return hostlist_iterator_create(set->hl);
}

void hostlist_iterator_reset(hostlist_iterator_t *i)
{
	xassert(i);
	xassert(i->magic == HOSTLIST_ITR_MAGIC);
	i->idx = 0;
	i->hr = i->hl->hr[0];
	i->depth = -1;
	return;
}

void hostlist_iterator_destroy(hostlist_iterator_t *i)
{
	hostlist_t *hl;
	if (i == NULL)
		return;
	/* _hostlist_iterator_destroy free's 'i' so grab the hl now */
	hl = i->hl;
	LOCK_HOSTLIST(hl);
	_hostlist_iterator_destroy(i);
	UNLOCK_HOSTLIST(hl);
}

static void _iterator_advance(hostlist_iterator_t *i)
{
	xassert(i);
	xassert(i->magic == HOSTLIST_ITR_MAGIC);

	if (i->idx > i->hl->nranges - 1)
		return;

	if (++(i->depth) > (i->hr->hi - i->hr->lo)) {
		i->depth = 0;
		i->idx++;
		if (i->idx < i->hl->nranges)
			i->hr = i->hl->hr[i->idx];
		else
			i->hr = NULL;
	}
}

char *hostlist_next(hostlist_iterator_t *i)
{
	char buf[HOST_NAME_MAX + 16];
	const int size = sizeof(buf);
	int len = 0;

	xassert(i);
	xassert(i->magic == HOSTLIST_ITR_MAGIC);
	LOCK_HOSTLIST(i->hl);
	_iterator_advance(i);

	if (i->idx > i->hl->nranges - 1)
		goto no_next;

	len = snprintf(buf, size, "%s", i->hr->prefix);
	if ((len < 0) || (len + 1 >= size))
		goto no_next;

	if (!i->hr->singlehost) {
		int ret = snprintf(buf + len, size - len, "%0*lu",
				   i->hr->width, i->hr->lo + i->depth);
		if ((ret < 0) || ((len + ret) >= size))
			goto no_next;
	}
	UNLOCK_HOSTLIST(i->hl);
	return strdup(buf);
no_next:
	UNLOCK_HOSTLIST(i->hl);
	return NULL;
}

int hostlist_remove(hostlist_iterator_t *i)
{
	hostrange_t *new;

	xassert(i);
	xassert(i->magic == HOSTLIST_ITR_MAGIC);
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

hostset_t *hostset_create(const char *hostlist)
{
	hostset_t *new = xmalloc(sizeof(*new));

	if (!(new->hl = hostlist_create(hostlist))) {
		xfree(new);
		return NULL;
	}

	hostlist_uniq(new->hl);
	return new;
}

void hostset_destroy(hostset_t *set)
{
	if (set == NULL)
		return;
	hostlist_destroy(set->hl);
	xfree(set);
}

/* inserts a single range object into a hostset
 * Assumes that the set->hl lock is already held
 * Updates hl->nhosts
 */
static int hostset_insert_range(hostset_t *set, hostrange_t *hr)
{
	int i = 0;
	int inserted = 0;
	int nhosts = 0;
	int ndups = 0;
	hostlist_t *hl;

	hl = set->hl;

	if (hl->size == hl->nranges)
		hostlist_expand(hl);

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

int hostset_insert(hostset_t *set, const char *hosts)
{
	int i, n = 0;
	hostlist_t *hl = hostlist_create(hosts);
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
static int hostset_find_host(hostset_t *set, const char *host)
{
	int i;
	int retval = 0;
	hostname_t *hn;
	LOCK_HOSTLIST(set->hl);
	if (!(hn = hostname_create(host)))
		goto done;
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

int hostset_intersects(hostset_t *set, const char *hosts)
{
	int retval = 0;
	hostlist_t *hl;
	char *hostname;

	xassert(set->hl->magic == HOSTLIST_MAGIC);

	hl = hostlist_create(hosts);
	while ((hostname = hostlist_pop(hl)) != NULL) {
		retval += hostset_find_host(set, hostname);
		free(hostname);
		if (retval)
			break;
	}

	hostlist_destroy(hl);

	return retval;
}

int hostset_within(hostset_t *set, const char *hosts)
{
	int nhosts, nfound;
	hostlist_t *hl;
	char *hostname;

	xassert(set->hl->magic == HOSTLIST_MAGIC);

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

int hostset_delete(hostset_t *set, const char *hosts)
{
	return hostlist_delete(set->hl, hosts);
}

int hostset_delete_host(hostset_t *set, const char *hostname)
{
	return hostlist_delete_host(set->hl, hostname);
}

char *hostset_shift(hostset_t *set)
{
	return hostlist_shift(set->hl);
}

char *hostset_pop(hostset_t *set)
{
	return hostlist_pop(set->hl);
}

int hostset_count(hostset_t *set)
{
	return hostlist_count(set->hl);
}

ssize_t hostset_ranged_string(hostset_t *set, size_t n, char *buf)
{
	return hostlist_ranged_string(set->hl, n, buf);
}

ssize_t hostset_deranged_string(hostset_t *set, size_t n, char *buf)
{
	return hostlist_deranged_string(set->hl, n, buf);
}

char *hostset_deranged_string_xmalloc(hostset_t *set)
{
	return hostlist_deranged_string_xmalloc(set->hl);
}

char *hostset_ranged_string_xmalloc(hostset_t *set)
{
	return hostlist_ranged_string_xmalloc(set->hl);
}

char *hostset_nth(hostset_t *set, int n)
{
	return hostlist_nth(set->hl, n);
}

int hostset_find(hostset_t *set, const char *hostname)
{
	return hostlist_find(set->hl, hostname);
}
