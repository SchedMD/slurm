/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
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
 *  LSD-Tools is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with LSD-Tools; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************
 *  Refer to "cbuf.h" for documentation on public functions.
\*****************************************************************************/


/*  FIXME: Revisit locations of cbuf_is_valid() calls.
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef WITH_PTHREADS
#  include <pthread.h>
#  include <syslog.h>
#endif /* WITH_PTHREADS */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cbuf.h"


/*******************
 *  Out of Memory  *
 *******************/

#ifdef WITH_OOMF
#  undef out_of_memory
   extern void * out_of_memory(void);
#else /* !WITH_OOMF */
#  ifndef out_of_memory
#    define out_of_memory() (NULL)
#  endif /* !out_of_memory */
#endif /* !WITH_OOMF */


/***************
 *  Constants  *
 ***************/

#define CBUF_CHUNK      500
#define CBUF_MAGIC      0xDEADBEEF
#define CBUF_MAGIC_LEN  (sizeof(unsigned long))


/****************
 *  Data Types  *
 ****************/

struct cbuf {

#ifndef NDEBUG
    unsigned int        magic;          /* cookie for asserting validity     */
#endif /* !NDEBUG */

#ifdef WITH_PTHREADS
    pthread_mutex_t     mutex;          /* mutex to protect access to cbuf   */
#endif /* WITH_PTHREADS */

    int                 alloc;          /* num bytes malloc'd/realloc'd      */
    int                 size;           /* num bytes of data allocated       */
    int                 minsize;        /* min bytes of data to allocate     */
    int                 maxsize;        /* max bytes of data to allocate     */
    int                 used;           /* num bytes of unread data          */
    int                 i_in;           /* index to where data is written in */
    int                 i_out;          /* index to where data is read out   */
    unsigned char      *data;           /* ptr to circular buffer of data    */
};

typedef int (*cbuf_iof) (void *cbuf_data, void *arg, int len);


/****************
 *  Prototypes  *
 ****************/

static int cbuf_find_line (cbuf_t cb);

static int cbuf_get_fd (void *dstbuf, int *psrcfd, int len);
static int cbuf_get_mem (void *dstbuf, unsigned char **psrcbuf, int len);
static int cbuf_put_fd (void *srcbuf, int *pdstfd, int len);
static int cbuf_put_mem (void *srcbuf, unsigned char **pdstbuf, int len);

static int cbuf_dropper (cbuf_t cb, int len);
static int cbuf_reader (cbuf_t cb, int len, cbuf_iof putf, void *dst);
static int cbuf_replayer (cbuf_t cb, int len, cbuf_iof putf, void *dst);
static int cbuf_writer (cbuf_t cb, int len, cbuf_iof getf, void *src,
       int *pdropped);

static int cbuf_grow (cbuf_t cb, int n);
static int cbuf_shrink (cbuf_t cb);

#ifndef NDEBUG
static int cbuf_is_valid (cbuf_t cb);
#endif /* !NDEBUG */


/************
 *  Macros  *
 ************/

#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */

#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */

#ifdef WITH_PTHREADS
/*
 *  FIXME: Replace syslog/abort jazz with macro that can be overridden.
 */
#  define cbuf_mutex_init(cb)                                                 \
     do {                                                                     \
         int e = pthread_mutex_init(&cb->mutex, NULL);                        \
         if (e) errno = e, syslog(LOG_ERR, "cbuf mutex init: %m"), abort();   \
     } while (0)

#  define cbuf_mutex_lock(cb)                                                 \
     do {                                                                     \
         int e = pthread_mutex_lock(&cb->mutex);                              \
         if (e) errno = e, syslog(LOG_ERR, "cbuf mutex lock: %m"), abort();   \
     } while (0)

#  define cbuf_mutex_unlock(cb)                                               \
     do {                                                                     \
         int e = pthread_mutex_unlock(&cb->mutex);                            \
         if (e) errno = e, syslog(LOG_ERR, "cbuf mutex unlock: %m"), abort(); \
     } while (0)

#  define cbuf_mutex_destroy(cb)                                              \
     do {                                                                     \
         int e = pthread_mutex_destroy(&cb->mutex);                           \
         if (e) errno = e, syslog(LOG_ERR, "cbuf mutex destroy: %m"), abort();\
     } while (0)

#  ifndef NDEBUG
     static int cbuf_mutex_is_locked (cbuf_t cb);
#  endif /* !NDEBUG */

#else /* !WITH_PTHREADS */

#  define cbuf_mutex_init(cb)
#  define cbuf_mutex_lock(cb)
#  define cbuf_mutex_unlock(cb)
#  define cbuf_mutex_destroy(cb)
#  define cbuf_mutex_is_locked(cb) (1)

#endif /* !WITH_PTHREADS */


/***************
 *  Functions  *
 ***************/

cbuf_t
cbuf_create(int minsize, int maxsize)
{
    cbuf_t cb;

    if (minsize <= 0) {
        errno = EINVAL;
        return(NULL);
    }
    if (!(cb = malloc(sizeof(struct cbuf)))) {
        errno = ENOMEM;
        return(out_of_memory());
    }
    /*  Circular buffer is empty when (i_in == i_out),
     *    so reserve 1 byte for this sentinel.
     */
    cb->alloc = minsize + 1;
#ifndef NDEBUG
    /*  Reserve space for the magic cookies used to protect the
     *    cb->data[] array from underflow and overflow.
     */
    cb->alloc += 2 * CBUF_MAGIC_LEN;
#endif /* !NDEBUG */

    if (!(cb->data = malloc(cb->alloc))) {
        free(cb);
        errno = ENOMEM;
        return(out_of_memory());
    }
    cbuf_mutex_init(cb);
    cb->size = minsize;
    cb->minsize = minsize;
    cb->maxsize = (maxsize > minsize) ? maxsize : minsize;
    cb->used = 0;
    cb->i_in = cb->i_out = 0;

#ifndef NDEBUG
    /*  C is for cookie, that's good enough for me, yeah!
     *  The magic cookies are only defined during DEBUG code.
     *  The first 'magic' cookie is at the top of the structure.
     *  Magic cookies are also placed at the top & bottom of the
     *  cb->data[] array to catch buffer underflow & overflow errors.
     */
    cb->data += CBUF_MAGIC_LEN;         /* jump forward past underflow magic */
    cb->magic = CBUF_MAGIC;
    /*
     *  Must use memcpy/memcmp since overflow cookie may not be word-aligned.
     */
    memcpy(cb->data - CBUF_MAGIC_LEN, (void *) &cb->magic, CBUF_MAGIC_LEN);
    memcpy(cb->data + cb->size + 1, (void *) &cb->magic, CBUF_MAGIC_LEN);

    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    cbuf_mutex_unlock(cb);
#endif /* !NDEBUG */

    return(cb);
}


void
cbuf_destroy(cbuf_t cb)
{
    assert(cb != NULL);
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));

#ifndef NDEBUG
    /*  The moon sometimes looks like a C, but you can't eat that.
     *  Munch the magic cookies before freeing memory.
     */
    cb->magic = ~CBUF_MAGIC;            /* the anti-cookie! */
    memcpy(cb->data - CBUF_MAGIC_LEN, (void *) &cb->magic, CBUF_MAGIC_LEN);
    memcpy(cb->data + cb->size + 1, (void *) &cb->magic, CBUF_MAGIC_LEN);
    cb->data -= CBUF_MAGIC_LEN;         /* jump back to what malloc returned */
#endif /* !NDEBUG */

    free(cb->data);
    cbuf_mutex_unlock(cb);
    cbuf_mutex_destroy(cb);
    free(cb);
    return;
}


void
cbuf_flush(cbuf_t cb)
{
    assert(cb != NULL);
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    cb->used = 0;
    cb->i_in = cb->i_out = 0;
    assert(cbuf_is_valid(cb));
    cbuf_mutex_unlock(cb);
    return;
}


int
cbuf_is_empty(cbuf_t cb)
{
    int used;

    assert(cb != NULL);
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    used = cb->used;
    cbuf_mutex_unlock(cb);
    return(used == 0);
}


int
cbuf_size(cbuf_t cb)
{
    int size;

    assert(cb != NULL);
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    size = cb->size;
    cbuf_mutex_unlock(cb);
    return(size);
}


int
cbuf_free(cbuf_t cb)
{
    int free;

    assert(cb != NULL);
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    free = cb->size - cb->used;
    cbuf_mutex_unlock(cb);
    return(free);
}


int
cbuf_used(cbuf_t cb)
{
    int used;

    assert(cb != NULL);
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    used = cb->used;
    cbuf_mutex_unlock(cb);
    return(used);
}


#if 0
int
cbuf_copy(cbuf_t src, cbuf_t dst, int len)
{
    /*  XXX: Identical to cbuf_move(), but s/cbuf_reader/cbuf_peeker/.
     */
    return(0);
}
#endif


int
cbuf_drop(cbuf_t cb, int len)
{
    int n;

    assert(cb != NULL);

    if (len < 0) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(cb);
    assert(cbuf_is_valid(cb));
    n = MIN(len, cb->used);
    cbuf_dropper(cb, n);
    assert(cbuf_is_valid(cb));
    cbuf_mutex_unlock(cb);
    return(n);
}


#if 0
int
cbuf_move(cbuf_t src, cbuf_t dst, int len)
{
    unsigned char buf[CBUF_PAGE];
    int n, m;

    assert(src != NULL);
    assert(dst != NULL);

    if (len < 0) {
        errno = EINVAL;
        return(-1);
    }

    /*  XXX: What about deadlock?  Yow!
     *       Grab the locks in  order of the lowest memory address.
     */
    cbuf_mutex_lock(src);
    cbuf_mutex_lock(dst);
    assert(cbuf_is_valid(src));
    assert(cbuf_is_valid(dst));
    /*
     *  XXX: NOT IMPLEMENTED.
     */
    assert(cbuf_is_valid(dst));
    assert(cbuf_is_valid(src));
    cbuf_mutex_unlock(dst);
    cbuf_mutex_unlock(src);
    return(0);
}
#endif


int
cbuf_peek(cbuf_t cb, void *dstbuf, int len)
{
    int n;

    assert(cb != NULL);

    if ((dstbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(cb);
    n = cbuf_reader(cb, len, (cbuf_iof) cbuf_put_mem, &dstbuf);
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_read(cbuf_t cb, void *dstbuf, int len)
{
    int n;

    assert(cb != NULL);

    if ((dstbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(cb);
    n = cbuf_reader(cb, len, (cbuf_iof) cbuf_put_mem, &dstbuf);
    if (n > 0) {
        cbuf_dropper(cb, n);
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_replay(cbuf_t cb, void *dstbuf, int len)
{
    int n;

    assert(cb != NULL);

    if ((dstbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(cb);
    n = cbuf_replayer(cb, len, (cbuf_iof) cbuf_put_mem, &dstbuf);
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_write(cbuf_t cb, const void *srcbuf, int len, int *dropped)
{
    int n;

    assert(cb != NULL);

    if ((srcbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (dropped) {
        *dropped = 0;
    }
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(cb);
    n = cbuf_writer(cb, len, (cbuf_iof) cbuf_get_mem, &srcbuf, dropped);
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_peek_to_fd(cbuf_t cb, int dstfd, int len)
{
    int n = 0;

    assert(cb != NULL);

    if ((dstfd < 0) || (len < -1)) {
        errno = EINVAL;
        return(-1);
    }
    cbuf_mutex_lock(cb);
    if (len == -1) {
        len = cb->used;
    }
    if (len > 0) {
        n = cbuf_reader(cb, len, (cbuf_iof) cbuf_put_fd, &dstfd);
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_read_to_fd(cbuf_t cb, int dstfd, int len)
{
    int n = 0;

    assert(cb != NULL);

    if ((dstfd < 0) || (len < -1)) {
        errno = EINVAL;
        return(-1);
    }
    cbuf_mutex_lock(cb);
    if (len == -1) {
        len = cb->used;
    }
    if (len > 0) {
        n = cbuf_reader(cb, len, (cbuf_iof) cbuf_put_fd, &dstfd);
        if (n > 0) {
            cbuf_dropper(cb, n);
        }
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_replay_to_fd(cbuf_t cb, int dstfd, int len)
{
    int n = 0;

    assert(cb != NULL);

    if ((dstfd < 0) || (len < -1)) {
        errno = EINVAL;
        return(-1);
    }
    cbuf_mutex_lock(cb);
    if (len == -1) {
        len = cb->size - cb->used;
    }
    if (len > 0) {
        n = cbuf_replayer(cb, len, (cbuf_iof) cbuf_put_fd, &dstfd);
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_write_from_fd(cbuf_t cb, int srcfd, int len, int *dropped)
{
    int n = 0;

    assert(cb != NULL);

    if ((srcfd < 0) || (len < -1)) {
        errno = EINVAL;
        return(-1);
    }
    if (dropped) {
        *dropped = 0;
    }
    cbuf_mutex_lock(cb);
    /*
     *  XXX: Is the current -1 len behavior such a good idea?
     *       This prevents the buffer from both wrapping and growing.
     */
    if (len == -1) {
        len = cb->size - cb->used;
    }
    if (len > 0) {
        n = cbuf_writer(cb, len, (cbuf_iof) cbuf_get_fd, &srcfd, dropped);
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_get_line(cbuf_t cb, char *dst, int len)
{
    int n, m;
    char *pdst;

    assert(cb != NULL);

    if (((dst == NULL) && (len != 0)) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    pdst = dst;
    cbuf_mutex_lock(cb);
    n = cbuf_find_line(cb);
    if (n > 0) {
        if (len > 0) {
            m = MIN(n, len - 1);
            if (m > 0) {
                n = cbuf_reader(cb, m, (cbuf_iof) cbuf_put_mem, &pdst);
                assert(n == m);
            }
            dst[m] = '\0';
        }
        cbuf_dropper(cb, n);
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_peek_line(cbuf_t cb, char *dst, int len)
{
    int n, m;
    char *pdst;

    assert(cb != NULL);

    if (((dst == NULL) && (len != 0)) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    pdst = dst;
    cbuf_mutex_lock(cb);
    n = cbuf_find_line(cb);
    if (n > 0) {
        if (len > 0) {
            m = MIN(n, len - 1);
            if (m > 0) {
                n = cbuf_reader(cb, m, (cbuf_iof) cbuf_put_mem, &pdst);
                assert(n == m);
            }
            dst[m] = '\0';
        }
    }
    cbuf_mutex_unlock(cb);
    return(n);
}


int
cbuf_put_line(cbuf_t cb, const char *src, int *dropped)
{
    int len;
    int nget, ngot, ndropped, n, d;
    const char *psrc;
    const char *newline = "\n";

    assert(cb != NULL);

    if (src == NULL) {
        errno = EINVAL;
        return(-1);
    }
    len = strlen(src);
    nget = len;
    ngot = 0;
    ndropped = 0;
    psrc = src;
    cbuf_mutex_lock(cb);
    while (nget > 0) {
        n = cbuf_writer(cb, nget, (cbuf_iof) cbuf_get_mem, &psrc, &d);
        assert (n > 0);
        nget -= n;
        ngot += n;
        ndropped += d;
    }
    if (src[len - 1] != '\n') {         /* append newline if needed */
        n = cbuf_writer(cb, 1, (cbuf_iof) cbuf_get_mem, &newline, &d);
        ngot++;
        ndropped += d;
    }
    cbuf_mutex_unlock(cb);
    assert((ngot == len) || (ngot == len + 1));
    if (dropped) {
        *dropped = ndropped;
    }
    return(ngot);
}


static int
cbuf_find_line(cbuf_t cb)
{
/*  Returns the number of bytes up to and including the next newline or NUL.
 */
    int i, n;
    unsigned char c;

    assert(cb != NULL);
    assert(cbuf_mutex_is_locked(cb));

    i = cb->i_out;
    n = 1;
    while (i != cb->i_in) {
        c = cb->data[i];
        if ((c == '\n') || (c == '\0')) {
            assert(n <= cb->used);
            return(n);
        }
        i = (i + 1) % (cb->size + 1);
        n++;
    }
    return(0);
}


static int
cbuf_get_fd(void *dstbuf, int *psrcfd, int len)
{
/*  Copies data from the file referenced by the file descriptor
 *    pointed at by 'psrcfd' into cbuf's 'dstbuf'.
 *  Returns the number of bytes read from the fd, 0 on EOF, or <0 on error.
 */
    int n;

    assert(dstbuf != NULL);
    assert(psrcfd != NULL);
    assert(*psrcfd >= 0);
    assert(len > 0);

    do {
        n = read(*psrcfd, dstbuf, len);
    } while ((n < 0) && (errno == EINTR));
    return(n);
}


static int
cbuf_get_mem(void *dstbuf, unsigned char **psrcbuf, int len)
{
/*  Copies data from the buffer pointed at by 'psrcbuf' into cbuf's 'dstbuf'.
 *  Returns the number of bytes copied.
 */
    assert(dstbuf != NULL);
    assert(psrcbuf != NULL);
    assert(*psrcbuf != NULL);
    assert(len > 0);

    memcpy(dstbuf, *psrcbuf, len);
    *psrcbuf += len;
    return(len);
}


static int
cbuf_put_fd(void *srcbuf, int *pdstfd, int len)
{
/*  Copies data from cbuf's 'srcbuf' into the file referenced
 *    by the file descriptor pointed at by 'pdstfd'.
 *  Returns the number of bytes written to the fd, or <0 on error.
 */
    int n;

    assert(srcbuf != NULL);
    assert(pdstfd != NULL);
    assert(*pdstfd >= 0);
    assert(len > 0);

    do {
        n = write(*pdstfd, srcbuf, len);
    } while ((n < 0) && (errno == EINTR));
    return(n);
}


static int
cbuf_put_mem(void *srcbuf, unsigned char **pdstbuf, int len)
{
/*  Copies data from cbuf's 'srcbuf' into the buffer pointed at by 'pdstbuf'.
 *  Returns the number of bytes copied.
 */
    assert(srcbuf != NULL);
    assert(pdstbuf != NULL);
    assert(*pdstbuf != NULL);
    assert(len > 0);

    memcpy(*pdstbuf, srcbuf, len);
    *pdstbuf += len;
    return(len);
}


static int
cbuf_dropper(cbuf_t cb, int len)
{
/*  Discards up to 'len' bytes of unread data from 'cb'.
 *  Returns the number of bytes dropped.
 */
    assert(cb != NULL);
    assert(len > 0);
    assert(len <= cb->used);
    assert(cbuf_mutex_is_locked(cb));

    cb->used -= len;
    cb->i_out = (cb->i_out + len) % (cb->size + 1);
    assert(cbuf_is_valid(cb));

    /*  Attempt to shrink buffer if possible.
     */
    if ((cb->size - cb->used > CBUF_CHUNK) && (cb->size > cb->minsize)) {
        cbuf_shrink(cb);
    }
    /*  Don't call me clumsy, don't call me a fool.
     *  When things fall down on me, I'm following the rule.
     */
    return(len);
}


static int
cbuf_reader(cbuf_t cb, int len, cbuf_iof putf, void *dst)
{
/*  XXX: DOCUMENT ME.
 */
    int nget, ngot;
    int shortput;
    int n, m;

    assert(cb != NULL);
    assert(len > 0);
    assert(putf != NULL);
    assert(dst != NULL);
    assert(cbuf_mutex_is_locked(cb));
    assert(cbuf_is_valid(cb));

    nget = MIN(len, cb->used);
    ngot = 0;
    shortput = 0;

    n = MIN(nget, (cb->size + 1) - cb->i_out);
    if (n > 0) {
        m = putf(&cb->data[cb->i_out], dst, n);
        if (m <= 0) {
            return(m);
        }
        if (m != n) {
            shortput = 1;
        }
        ngot += m;
    }

    n = nget - ngot;
    if ((n > 0) && (!shortput)) {
        m = putf(&cb->data[0], dst, n);
        if (m > 0) {
            ngot += m;
        }
    }
    return(ngot);
}


static int
cbuf_replayer(cbuf_t cb, int len, cbuf_iof putf, void *dst)
{
/*  XXX: DOCUMENT ME.
 */
    assert(cb != NULL);
    assert(len > 0);
    assert(putf != NULL);
    assert(dst != NULL);
    assert(cbuf_mutex_is_locked(cb));
    assert(cbuf_is_valid(cb));

    /*  XXX: NOT IMPLEMENTED.
     */
    errno = ENOSYS;
    return(-1);
}


static int
cbuf_writer(cbuf_t cb, int len, cbuf_iof getf, void *src, int *pdropped)
{
/*  XXX: DOCUMENT ME.
 */
    int free;
    int nget, ngot;
    int shortget;
    int n, m;

    assert(cb != NULL);
    assert(len > 0);
    assert(getf != NULL);
    assert(src != NULL);
    assert(cbuf_mutex_is_locked(cb));
    assert(cbuf_is_valid(cb));

    /*  Attempt to grow buffer if necessary.
     */
    free = cb->size - cb->used;
    if ((len > free) && (cb->size < cb->maxsize)) {
        free += cbuf_grow(cb, len - free);
    }
    /*  Compute total number of bytes to attempt to write into buffer.
     */
    nget = MIN(len, cb->size);
    ngot = 0;
    shortget = 0;

    if (pdropped) {
        *pdropped = 0;
    }
    /*  Copy first chunk of data (ie, up to the end of the buffer).
     */
    n = MIN(nget, (cb->size + 1) - cb->i_in);
    if (n > 0) {
        m = getf(&cb->data[cb->i_in], src, n);
        if (m <= 0) {
            return(m);                  /* got ERR or EOF */
        }
        if (m != n) {
            shortget = 1;
        }
        cb->i_in += m;
        cb->i_in %= (cb->size + 1);     /* the hokey-pokey cbuf wrap-around */
        ngot += m;
    }

    /*  Copy second chunk of data (ie, from the beginning of the buffer).
     *  If the first getf() was short, the second getf() is not attempted.
     *  If the second getf() returns EOF/ERR, it will be masked by the success
     *    of the first getf().  This only occurs with get_fd, and the EOF/ERR
     *    condition should be returned on the next invocation.
     */
    n = nget - ngot;
    if ((n > 0) && (!shortget)) {
        m = getf(&cb->data[0], src, n);
        if (m > 0) {
            cb->i_in += m;              /* hokey-pokey not needed here */
            ngot += m;
        }
    }
    /*  Check to see if any data in the circular-buffer was overwritten.
     */
    if (ngot > 0) {
        cb->used += ngot;
        if (cb->used > cb->size) {
            cb->used = cb->size;
            cb->i_out = (cb->i_in + 1) % (cb->size + 1);
        }
    }

    assert(cbuf_is_valid(cb));

    if (pdropped) {
        *pdropped = MAX(0, ngot - free);
    }
    return(ngot);
}


static int
cbuf_grow(cbuf_t cb, int n)
{
/*  Attempts to grow the circular buffer 'cb' by at least 'n' bytes.
 *  Returns the number of bytes by which the buffer has grown (which may be
 *    less-than, equal-to, or greater-than the number of bytes requested).
 */
    unsigned char *data;                /* tmp ptr to data buffer */
    int size_old;                       /* size of buffer upon func entry */
    int size_meta;                      /* size of sentinel & magic cookies */
    int m;                              /* num bytes to realloc */

    assert(cb != NULL);
    assert(n > 0);
    assert(cbuf_mutex_is_locked(cb));

    if (cb->size == cb->maxsize) {
        return(0);
    }
    size_old = cb->size;
    size_meta = cb->alloc - cb->size;
    assert(size_meta > 0);

    /*  Attempt to grow the data buffer by multiples of the chunk-size.
     */
    m = cb->alloc + n;
    m = m + (CBUF_CHUNK - (m % CBUF_CHUNK));
    m = MIN(m, (cb->maxsize + size_meta));
    assert(m > cb->alloc);

    data = cb->data;
#ifndef NDEBUG
    data -= CBUF_MAGIC_LEN;             /* jump back to what malloc returned */
#endif /* !NDEBUG */

    if (!(data = realloc(data, m))) {
        /*
         *  XXX: Set flag to prevent regrowing when out of memory?
         */
        return(0);                      /* unable to grow data buffer */
    }

    cb->data = data;
    cb->alloc = m;
    cb->size = m - size_meta;

#ifndef NDEBUG
    /*  A round cookie with one bite out of it looks like a C.
     *  The underflow cookie will have been copied by realloc() if needed.
     *    But the overflow cookie must be rebaked.
     */
    cb->data += CBUF_MAGIC_LEN;         /* jump forward past underflow magic */
    * (unsigned int *) (cb->data + cb->size + 1) = CBUF_MAGIC;
#endif /* !NDEBUG */

    /*  If unread data wrapped-around the old buffer, move the first chunk
     *    to the new end of the buffer so it wraps-around in the same manner.
     *
     *  XXX: What does this do to the replay buffer?
     *       Replay data should be shifted as well.
     */
    if (cb->i_out > cb->i_in) {
        n = (size_old + 1) - cb->i_out;
        m = (cb->size + 1) - n;
        memmove(cb->data + m, cb->data + cb->i_out, n);
        cb->i_out = m;
    }

    assert(cbuf_is_valid(cb));
    return(cb->size - size_old);
}


static int
cbuf_shrink(cbuf_t cb)
{
/*  XXX: DOCUMENT ME.
 */
    assert(cb != NULL);
    assert(cbuf_mutex_is_locked(cb));

    if (cb->size == cb->minsize) {
        return(0);
    }
    if (cb->size - cb->used <= CBUF_CHUNK) {
        return(0);
    }
    /*  XXX: NOT IMPLEMENTED.
     */
    assert(cbuf_is_valid(cb));
    return(0);
}


#ifndef NDEBUG
#ifdef WITH_PTHREADS
static int
cbuf_mutex_is_locked(cbuf_t cb)
{
/*  Returns true if the mutex is locked; o/w, returns false.
 */
    int rc;

    assert(cb != NULL);
    rc = pthread_mutex_trylock(&cb->mutex);
    return(rc == EBUSY ? 1 : 0);
}
#endif /* WITH_PTHREADS */
#endif /* !NDEBUG */


#ifndef NDEBUG
static int
cbuf_is_valid(cbuf_t cb)
{
/*  Validates the data structure.  All invariants should be tested here.
 *  Returns true if everything is valid; o/w, aborts due to assertion failure.
 */
    int free;

    assert(cb != NULL);
    assert(cbuf_mutex_is_locked(cb));
    assert(cb->data != NULL);
    assert(cb->magic == CBUF_MAGIC);
    assert(memcmp(cb->data - CBUF_MAGIC_LEN,
        (void *) &cb->magic, CBUF_MAGIC_LEN) == 0);
    assert(memcmp(cb->data + cb->size + 1,
        (void *) &cb->magic, CBUF_MAGIC_LEN) == 0);

    assert(cb->alloc > 0);
    assert(cb->alloc > cb->size);
    assert(cb->size > 0);
    assert(cb->size >= cb->minsize);
    assert(cb->size <= cb->maxsize);
    assert(cb->minsize > 0);
    assert(cb->maxsize > 0);
    assert(cb->used >= 0);
    assert(cb->used <= cb->size);
    assert(cb->i_in >= 0);
    assert(cb->i_in <= cb->size);
    assert(cb->i_out >= 0);
    assert(cb->i_out <= cb->size);

    if (cb->i_in == cb->i_out) {
        free = cb->size;
    }
    else if (cb->i_in < cb->i_out) {
        free = (cb->i_out - cb->i_in) - 1;
    }
    else {
        free = ((cb->size + 1) - cb->i_in) + cb->i_out - 1;
    }
    assert(cb->size - cb->used == free);

    return(1);
}
#endif /* !NDEBUG */
