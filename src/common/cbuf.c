/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************
 *  Refer to "cbuf.h" for documentation on public functions.
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef WITH_PTHREADS
#  include <pthread.h>
#  include <stdio.h>
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

static int cbuf_find_nl (cbuf_t cb);

static int cbuf_get_fd (void *srcbuf, int *pdstfd, int len);
static int cbuf_get_mem (void *srcbuf, unsigned char **pdstbuf, int len);
static int cbuf_put_fd (void *dstbuf, int *psrcfd, int len);
static int cbuf_put_mem (void *dstbuf, unsigned char **psrcbuf, int len);

static int cbuf_peeker (cbuf_t cb, int len, cbuf_iof getf, void *dst);
static int cbuf_reader (cbuf_t cb, int len, cbuf_iof getf, void *dst);
static int cbuf_replayer (cbuf_t cb, int len, cbuf_iof getf, void *dst);
static int cbuf_writer (cbuf_t cb, int len, cbuf_iof putf, void *src,
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

#  define cbuf_mutex_init(mutex)                                              \
     do {                                                                     \
         int cbuf_errno;                                                      \
         if ((cbuf_errno = pthread_mutex_init(mutex, NULL)) != 0) {           \
             fprintf(stderr, "ERROR: pthread_mutex_init() failed: %s\n",      \
                     strerror(cbuf_errno)); exit(1);                          \
         }                                                                    \
     } while (0)

#  define cbuf_mutex_lock(mutex)                                              \
     do {                                                                     \
         int cbuf_errno;                                                      \
         if ((cbuf_errno = pthread_mutex_lock(mutex)) != 0) {                 \
             fprintf(stderr, "ERROR: pthread_mutex_lock() failed: %s\n",      \
                     strerror(cbuf_errno)); exit(1);                          \
         }                                                                    \
     } while (0)

#  define cbuf_mutex_unlock(mutex)                                            \
     do {                                                                     \
         int cbuf_errno;                                                      \
         if ((cbuf_errno = pthread_mutex_unlock(mutex)) != 0) {               \
             fprintf(stderr, "ERROR: pthread_mutex_unlock() failed: %s\n",    \
                     strerror(cbuf_errno)); exit(1);                          \
         }                                                                    \
     } while (0)

#  define cbuf_mutex_destroy(mutex)                                           \
     do {                                                                     \
         int cbuf_errno;                                                      \
         if ((cbuf_errno = pthread_mutex_destroy(mutex)) != 0) {              \
             fprintf(stderr, "ERROR: pthread_mutex_destroy() failed: %s\n",   \
                     strerror(cbuf_errno)); exit(1);                          \
         }                                                                    \
     } while (0)

#else /* !WITH_PTHREADS */

#  define cbuf_mutex_init(mutex)
#  define cbuf_mutex_lock(mutex)
#  define cbuf_mutex_unlock(mutex)
#  define cbuf_mutex_destroy(mutex)

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
    cbuf_mutex_init(&cb->mutex);
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
#endif /* !NDEBUG */

    assert(cbuf_is_valid(cb));
    return(cb);
}


void
cbuf_destroy(cbuf_t cb)
{
    assert(cb != NULL);
    cbuf_mutex_lock(&cb->mutex);
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
    cbuf_mutex_unlock(&cb->mutex);
    cbuf_mutex_destroy(&cb->mutex);
    free(cb);
    return;
}


void
cbuf_flush(cbuf_t cb)
{
    assert(cb != NULL);
    cbuf_mutex_lock(&cb->mutex);
    assert(cbuf_is_valid(cb));
    cb->used = 0;
    cb->i_in = cb->i_out = 0;
    assert(cbuf_is_valid(cb));
    cbuf_mutex_unlock(&cb->mutex);
    return;
}


int
cbuf_is_empty(cbuf_t cb)
{
    int used;

    assert(cb != NULL);
    cbuf_mutex_lock(&cb->mutex);
    assert(cbuf_is_valid(cb));
    used = cb->used;
    cbuf_mutex_unlock(&cb->mutex);
    return(used == 0);
}


int
cbuf_size(cbuf_t cb)
{
    int size;

    assert(cb != NULL);
    cbuf_mutex_lock(&cb->mutex);
    assert(cbuf_is_valid(cb));
    size = cb->size;
    cbuf_mutex_unlock(&cb->mutex);
    return(size);
}


int
cbuf_free(cbuf_t cb)
{
    int free;

    assert(cb != NULL);
    cbuf_mutex_lock(&cb->mutex);
    assert(cbuf_is_valid(cb));
    free = cb->size - cb->used;
    cbuf_mutex_unlock(&cb->mutex);
    return(free);
}


int
cbuf_used(cbuf_t cb)
{
    int used;

    assert(cb != NULL);
    cbuf_mutex_lock(&cb->mutex);
    assert(cbuf_is_valid(cb));
    used = cb->used;
    cbuf_mutex_unlock(&cb->mutex);
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
    cbuf_mutex_lock(&cb->mutex);
    assert(cbuf_is_valid(cb));

    n = MIN(len, cb->used);
    if (n > 0) {
        cb->used -= n;
        cb->i_out = (cb->i_out + n) % (cb->size + 1);
    }
    assert(cbuf_is_valid(cb));
    cbuf_mutex_unlock(&cb->mutex);
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
    cbuf_mutex_lock(&src->mutex);
    cbuf_mutex_lock(&dst->mutex);
    assert(cbuf_is_valid(src));
    assert(cbuf_is_valid(dst));
    /*
     *  XXX: NOT IMPLEMENTED.
     */
    assert(cbuf_is_valid(dst));
    assert(cbuf_is_valid(src));
    cbuf_mutex_unlock(&dst->mutex);
    cbuf_mutex_unlock(&src->mutex);
    return(0);
}
#endif


int
cbuf_peek(cbuf_t cb, void *dstbuf, int len)
{
    /*  XXX: NOT IMPLEMENTED.
     */
    return(0);
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
    cbuf_mutex_lock(&cb->mutex);
    n = cbuf_reader(cb, len, (cbuf_iof) cbuf_get_mem, &dstbuf);
    cbuf_mutex_unlock(&cb->mutex);
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
    cbuf_mutex_lock(&cb->mutex);
    n = cbuf_replayer(cb, len, (cbuf_iof) cbuf_get_mem, &dstbuf);
    cbuf_mutex_unlock(&cb->mutex);
    return(n);
}


int
cbuf_write(cbuf_t cb, void *srcbuf, int len, int *dropped)
{
    int n;

    assert(cb != NULL);

    if ((srcbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(&cb->mutex);
    n = cbuf_writer(cb, len, (cbuf_iof) cbuf_put_mem, &srcbuf, dropped);
    cbuf_mutex_unlock(&cb->mutex);
    return(n);
}


int
cbuf_peek_to_fd(cbuf_t cb, int dstfd, int len)
{
    /*  XXX: NOT IMPLEMENTED.
     */
    return(0);
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
    cbuf_mutex_lock(&cb->mutex);
    if (len == -1)
        len = cb->used;
    if (len > 0)
        n = cbuf_reader(cb, len, (cbuf_iof) cbuf_get_fd, &dstfd);
    cbuf_mutex_unlock(&cb->mutex);
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
    cbuf_mutex_lock(&cb->mutex);
    if (len == -1)
        len = cb->size - cb->used;
    if (len > 0)
        n = cbuf_reader(cb, len, (cbuf_iof) cbuf_get_fd, &dstfd);
    cbuf_mutex_unlock(&cb->mutex);
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
    cbuf_mutex_lock(&cb->mutex);
    if (len == -1)
        len = cb->size - cb->used;
    if (len > 0)
        n = cbuf_writer(cb, len, (cbuf_iof) cbuf_put_fd, &srcfd, dropped);
    cbuf_mutex_unlock(&cb->mutex);
    return(n);
}


int
cbuf_gets(cbuf_t cb, char *dst, int len)
{
    /*  XXX: Pro'ly best to do this as cbuf_peek + cbuf_drop.
     */
    assert(cb != NULL);

    if ((dst == NULL) || (len <= 0)) {
        errno = EINVAL;
        return(-1);
    }
    /*  XXX: NOT IMPLEMENTED.
     */
    return(-1);
}


int
cbuf_peeks(cbuf_t cb, char *dst, int len)
{
    /*  XXX: Use cbuf_find_nl.
     */
    assert(cb != NULL);

    if ((dst == NULL) || (len <= 0)) {
        errno = EINVAL;
        return(-1);
    }
    /*  XXX: NOT IMPLEMENTED.
     */
    return(-1);
}


int
cbuf_puts(cbuf_t cb, char *src, int *dropped)
{
    /*  XXX: Handle case where src string exceeds buffer size.
     *       But cannot simply advance src ptr, since writer()
     *       may be able to grow cbuf if needed.  Ugh!
     *       Pro'ly best to wrap it in a loop.  Sigh.
     *       Should always return strlen(src).
     */
    int n;
    int len;

    assert(cb != NULL);

    if (src == NULL) {
        errno = EINVAL;
        return(-1);
    }
    len = strlen(src);
    if (len == 0) {
        return(0);
    }
    cbuf_mutex_lock(&cb->mutex);
    n = cbuf_writer(cb, len, (cbuf_iof) cbuf_put_mem, src, dropped);
    cbuf_mutex_unlock(&cb->mutex);
    return(n);
}


static int
cbuf_find_nl(cbuf_t cb)
{
/*  Returns the number of bytes up to and including the next newline.
 *  Assumes 'cb' is locked upon entry.
 */
    int i, n;

    assert(cb != NULL);

    i = cb->i_out;
    n = 1;
    while (i != cb->i_in) {
        if (cb->data[i] == '\n')
            return(n);
        i = (i + 1) % (cb->size + 1);
        n++;
    }
    return(0);
}


static int
cbuf_get_fd(void *srcbuf, int *pdstfd, int len)
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
cbuf_get_mem(void *srcbuf, unsigned char **pdstbuf, int len)
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
cbuf_put_fd(void *dstbuf, int *psrcfd, int len)
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
cbuf_put_mem(void *dstbuf, unsigned char **psrcbuf, int len)
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
cbuf_peeker(cbuf_t cb, int len, cbuf_iof getf, void *dst)
{
    /*  XXX: NOT IMPLEMENTED.
     */
    return(0);
}


static int
cbuf_reader(cbuf_t cb, int len, cbuf_iof getf, void *dst)
{
/*  XXX: DOCUMENT ME.
 */
    int nget, ngot;
    int shortget;
    int n, m;

    assert(cb != NULL);
    assert(len > 0);
    assert(getf != NULL);
    assert(dst != NULL);
    assert(cbuf_is_valid(cb));

    nget = MIN(len, cb->used);
    ngot = 0;
    shortget = 0;

    n = MIN(nget, (cb->size + 1) - cb->i_out);
    if (n > 0) {
        m = getf(&cb->data[cb->i_out], dst, n);
        if (m <= 0)
            return(m);
        if (m != n)
            shortget = 1;
        ngot += m;
    }

    n = nget - ngot;
    if ((n > 0) && (!shortget)) {
        m = getf(&cb->data[0], dst, n);
        if (m > 0) {
            ngot += m;
        }
    }

    if (ngot > 0) {
        cb->used -= ngot;
        cb->i_out = (cb->i_out + ngot) % (cb->size + 1);
    }

    assert(cbuf_is_valid(cb));

    /*  Attempt to shrink buffer if possible.
     */
    if ((cb->size - cb->used > CBUF_CHUNK) && (cb->size > cb->minsize)) {
        cbuf_shrink(cb);
    }
    return(ngot);
}


static int
cbuf_replayer(cbuf_t cb, int len, cbuf_iof getf, void *dst)
{
/*  XXX: DOCUMENT ME.
 */
    assert(cb != NULL);
    assert(len > 0);
    assert(getf != NULL);
    assert(dst != NULL);
    assert(cbuf_is_valid(cb));

    /*  XXX: NOT IMPLEMENTED.
     */
    return(0);
}


static int
cbuf_writer(cbuf_t cb, int len, cbuf_iof putf, void *src, int *pdropped)
{
/*  XXX: DOCUMENT ME.
 */
    int free;
    int nget, ngot;
    int shortput;
    int n, m;

    assert(cb != NULL);
    assert(len > 0);
    assert(putf != NULL);
    assert(src != NULL);
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
    shortput = 0;

    /*  Copy first chunk of data (ie, up to the end of the buffer).
     */
    n = MIN(nget, (cb->size + 1) - cb->i_in);
    if (n > 0) {
        m = putf(&cb->data[cb->i_in], src, n);
        if (m <= 0)
            return(m);
        if (m != n)
            shortput = 1;
        cb->i_in += m;
        cb->i_in %= (cb->size + 1);     /* the hokey-pokey cbuf wrap-around */
        ngot += m;
    }

    /*  Copy second chunk of data (ie, from the beginning of the buffer).
     *  If the first putf() was short, the second putf() is not attempted.
     *  If the second putf() returns EOF/ERR, it will be masked by the success
     *    of the first putf().  This only occurs with put_fd, and the EOF/ERR
     *    condition should be returned on the next invocation.
     */
    n = nget - ngot;
    if ((n > 0) && (!shortput)) {
        m = putf(&cb->data[0], src, n);
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
 *  Assumes 'cb' is locked upon entry.
 */
    unsigned char *data;                /* tmp ptr to data buffer */
    int size_old;                       /* size of buffer upon func entry */
    int size_meta;                      /* size of sentinel & magic cookies */
    int m;                              /* num bytes to realloc */

    assert(cb != NULL);
    assert(n > 0);

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

#ifndef NDEBUG
    data = cb->data - CBUF_MAGIC_LEN;   /* jump back to what malloc returned */
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
 *  Assumes 'cb' is locked upon entry.
 */
    assert(cb != NULL);

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
static int
cbuf_is_valid(cbuf_t cb)
{
/*  Validates the data structure.  All invariants should be tested here.
 *  Returns true if everything is valid; o/w, aborts due to assertion failure.
 *  Assumes 'cb' is locked upon entry.
 */
    int free;

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

    if (cb->i_in == cb->i_out)
        free = cb->size;
    else if (cb->i_in < cb->i_out)
        free = (cb->i_out - cb->i_in) - 1;
    else
        free = ((cb->size + 1) - cb->i_in) + cb->i_out - 1;
    assert(cb->size - cb->used == free);

    return(1);
}
#endif /* !NDEBUG */
