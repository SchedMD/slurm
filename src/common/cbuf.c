/*****************************************************************************
 *  cbuf.c
 *****************************************************************************
 *  Copyright (C) 2002-2005 The Regents of the University of California.
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
 *  Refer to "cbuf.h" for documentation on public functions.
 *****************************************************************************/

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/common/cbuf.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"

/***************
 *  Constants  *
 ***************/

#define CBUF_CHUNK      1000
#define CBUF_MAGIC      0xDEADBEEF
#define CBUF_MAGIC_LEN  (sizeof(unsigned long))


/****************
 *  Data Types  *
 ****************/

struct cbuf {

#ifndef NDEBUG
    unsigned long       magic;          /* cookie for asserting validity     */
#endif /* !NDEBUG */

    pthread_mutex_t     mutex;          /* mutex to protect access to cbuf   */

    int                 size;           /* num bytes of data allocated       */
    int                 used;           /* num bytes of unread data          */
    bool overwrite;                     /* overwrite option behavior         */
    int                 got_wrap;       /* true if data has wrapped          */
    int                 i_in;           /* index to where data is written in */
    int                 i_out;          /* index to where data is read out   */
    int                 i_rep;          /* index to where data is replayable */
    unsigned char      *data;           /* ptr to circular buffer of data    */
};

typedef int (*cbuf_iof) (void *cbuf_data, void *arg, int len);


/****************
 *  Prototypes  *
 ****************/

static int cbuf_find_unread_line(cbuf_t *cb, int chars, int *nlines);

static int cbuf_get_fd (void *dstbuf, int *psrcfd, int len);
static int cbuf_get_mem (void *dstbuf, unsigned char **psrcbuf, int len);
static int cbuf_put_fd (void *srcbuf, int *pdstfd, int len);
static int cbuf_put_mem (void *srcbuf, unsigned char **pdstbuf, int len);

static int cbuf_dropper(cbuf_t *cb, int len);
static int cbuf_reader(cbuf_t *src, int len, cbuf_iof putf, void *dst);
static int cbuf_writer(cbuf_t *dst, int len, cbuf_iof getf, void *src,
		       int *ndropped);

#ifndef NDEBUG
static int _cbuf_is_valid(cbuf_t *cb);
static int _cbuf_mutex_is_locked(cbuf_t *cb);
#endif /* !NDEBUG */

/***************
 *  Functions  *
 ***************/

cbuf_t *cbuf_create(int size, bool overwrite)
{
    int alloc;
    cbuf_t *cb = xmalloc(sizeof(struct cbuf));

    /*  Circular buffer is empty when (i_in == i_out),
     *    so reserve 1 byte for this sentinel.
     */
    alloc = size + 1;
#ifndef NDEBUG
    /*  Reserve space for the magic cookies used to protect the
     *    cbuf data[] array from underflow and overflow.
     */
    alloc += 2 * CBUF_MAGIC_LEN;
#endif /* !NDEBUG */

    cb->data = xmalloc(alloc);
    slurm_mutex_init(&cb->mutex);
    cb->size = size;
    cb->used = 0;
    cb->overwrite = overwrite;
    cb->got_wrap = 0;
    cb->i_in = cb->i_out = cb->i_rep = 0;

#ifndef NDEBUG
    /*  C is for cookie, that's good enough for me, yeah!
     *  The magic cookies are only defined during DEBUG code.
     *  The first "magic" cookie is at the top of the structure.
     *  Magic cookies are also placed at the top & bottom of the
     *  cbuf data[] array to catch buffer underflow & overflow errors.
     */
    cb->data += CBUF_MAGIC_LEN;         /* jump forward past underflow magic */
    cb->magic = CBUF_MAGIC;
    /*
     *  Must use memcpy since overflow cookie may not be word-aligned.
     */
    memcpy(cb->data - CBUF_MAGIC_LEN, (void *) &cb->magic, CBUF_MAGIC_LEN);
    memcpy(cb->data + cb->size + 1, (void *) &cb->magic, CBUF_MAGIC_LEN);

    slurm_mutex_lock(&cb->mutex);
    assert(_cbuf_is_valid(cb));
    slurm_mutex_unlock(&cb->mutex);
#endif /* !NDEBUG */

    return(cb);
}


void cbuf_destroy(cbuf_t *cb)
{
    assert(cb != NULL);
    slurm_mutex_lock(&cb->mutex);
    assert(_cbuf_is_valid(cb));

#ifndef NDEBUG
    /*  The moon sometimes looks like a C, but you can't eat that.
     *  Munch the magic cookies before xfreeing memory.
     */
    cb->magic = ~CBUF_MAGIC;            /* the anti-cookie! */
    memcpy(cb->data - CBUF_MAGIC_LEN, (void *) &cb->magic, CBUF_MAGIC_LEN);
    memcpy(cb->data + cb->size + 1, (void *) &cb->magic, CBUF_MAGIC_LEN);
    cb->data -= CBUF_MAGIC_LEN;         /* jump back to what xmalloc returned */
#endif /* !NDEBUG */

    xfree(cb->data);
    slurm_mutex_unlock(&cb->mutex);
    slurm_mutex_destroy(&cb->mutex);
    xfree(cb);
    return;
}


int cbuf_free(cbuf_t *cb)
{
    int nfree;

    assert(cb != NULL);
    slurm_mutex_lock(&cb->mutex);
    assert(_cbuf_is_valid(cb));
    nfree = cb->size - cb->used;
    slurm_mutex_unlock(&cb->mutex);
    return(nfree);
}


int cbuf_used(cbuf_t *cb)
{
    int used;

    assert(cb != NULL);
    slurm_mutex_lock(&cb->mutex);
    assert(_cbuf_is_valid(cb));
    used = cb->used;
    slurm_mutex_unlock(&cb->mutex);
    return(used);
}

int cbuf_read(cbuf_t *src, void *dstbuf, int len)
{
    int n;

    assert(src != NULL);

    if ((dstbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    slurm_mutex_lock(&src->mutex);
    assert(_cbuf_is_valid(src));
    n = cbuf_reader(src, len, (cbuf_iof) cbuf_put_mem, &dstbuf);
    if (n > 0) {
        cbuf_dropper(src, n);
    }
    assert(_cbuf_is_valid(src));
    slurm_mutex_unlock(&src->mutex);
    return(n);
}


int cbuf_write(cbuf_t *dst, void *srcbuf, int len, int *ndropped)
{
    int n;

    assert(dst != NULL);

    if (ndropped) {
        *ndropped = 0;
    }
    if ((srcbuf == NULL) || (len < 0)) {
        errno = EINVAL;
        return(-1);
    }
    if (len == 0) {
        return(0);
    }
    slurm_mutex_lock(&dst->mutex);
    assert(_cbuf_is_valid(dst));
    n = cbuf_writer(dst, len, (cbuf_iof) cbuf_get_mem, &srcbuf, ndropped);
    assert(_cbuf_is_valid(dst));
    slurm_mutex_unlock(&dst->mutex);
    return(n);
}


int cbuf_peek_line(cbuf_t *src, char *dstbuf, int len, int lines)
{
    int n, m, l;
    char *pdst;

    assert(src != NULL);

    if ((dstbuf == NULL) || (len < 0) || (lines < -1)) {
        errno = EINVAL;
        return(-1);
    }
    if (lines == 0) {
        return(0);
    }
    slurm_mutex_lock(&src->mutex);
    assert(_cbuf_is_valid(src));
    n = cbuf_find_unread_line(src, len - 1, &lines);
    if (n > 0) {
        if (len > 0) {
            m = MIN(n, len - 1);
            if (m > 0) {
                pdst = dstbuf;
                l = cbuf_reader(src, m, (cbuf_iof) cbuf_put_mem, &pdst);
		if (l)
			assert(l == m);
            }
            assert(m < len);
            dstbuf[m] = '\0';
        }
    }
    assert(_cbuf_is_valid(src));
    slurm_mutex_unlock(&src->mutex);
    return(n);
}


int cbuf_read_line(cbuf_t *src, char *dstbuf, int len, int lines)
{
    int n, m, l;
    char *pdst;

    assert(src != NULL);

    if ((dstbuf == NULL) || (len < 0) || (lines < -1)) {
        errno = EINVAL;
        return(-1);
    }
    if (lines == 0) {
        return(0);
    }
    slurm_mutex_lock(&src->mutex);
    assert(_cbuf_is_valid(src));
    n = cbuf_find_unread_line(src, len - 1, &lines);
    if (n > 0) {
        if (len > 0) {
            m = MIN(n, len - 1);
            if (m > 0) {
                pdst = dstbuf;
                l = cbuf_reader(src, m, (cbuf_iof) cbuf_put_mem, &pdst);
		if (l)
			assert(l == m);
            }
            assert(m < len);
            dstbuf[m] = '\0';
        }
        cbuf_dropper(src, n);
    }
    assert(_cbuf_is_valid(src));
    slurm_mutex_unlock(&src->mutex);
    return(n);
}


int cbuf_read_to_fd(cbuf_t *src, int dstfd, int len)
{
    int n = 0;

    assert(src != NULL);

    if ((dstfd < 0) || (len < -1)) {
        errno = EINVAL;
        return(-1);
    }
    slurm_mutex_lock(&src->mutex);
    assert(_cbuf_is_valid(src));
    if (len == -1) {
        len = src->used;
    }
    if (len > 0) {
        n = cbuf_reader(src, len, (cbuf_iof) cbuf_put_fd, &dstfd);
        if (n > 0) {
            cbuf_dropper(src, n);
        }
    }
    assert(_cbuf_is_valid(src));
    slurm_mutex_unlock(&src->mutex);
    return(n);
}


int cbuf_write_from_fd(cbuf_t *dst, int srcfd, int len, int *ndropped)
{
    int n = 0;

    assert(dst != NULL);

    if (ndropped) {
        *ndropped = 0;
    }
    if ((srcfd < 0) || (len < -1)) {
        errno = EINVAL;
        return(-1);
    }
    slurm_mutex_lock(&dst->mutex);
    assert(_cbuf_is_valid(dst));
    if (len == -1) {
        /*
         *  Try to use all of the free buffer space available for writing.
         *    If it is all in use, try to grab another chunk.
         */
        len = dst->size - dst->used;
        if (len == 0) {
            len = CBUF_CHUNK;
        }
    }
    if (len > 0) {
        n = cbuf_writer(dst, len, (cbuf_iof) cbuf_get_fd, &srcfd, ndropped);
    }
    assert(_cbuf_is_valid(dst));
    slurm_mutex_unlock(&dst->mutex);
    return(n);
}


static int cbuf_find_unread_line(cbuf_t *cb, int chars, int *nlines)
{
/*  Finds the specified number of lines from the unread region of the buffer.
 *  If ([nlines] > 0), returns the number of bytes comprising the line count,
 *    or 0 if this number of lines is not available (ie, all or none).
 *  If ([nlines] == -1), returns the number of bytes comprising the maximum
 *    line count bounded by the number of characters specified by [chars].
 *  Only complete lines (ie, those terminated by a newline) are counted.
 *  Sets the value-result parameter [nlines] to the number of lines found.
 */
    int i, n, m, l;
    int lines;

    assert(cb != NULL);
    assert(nlines != NULL);
    assert(*nlines >= -1);
    assert(_cbuf_mutex_is_locked(cb));

    n = m = l = 0;
    lines = *nlines;
    *nlines = 0;

    if ((lines == 0) || ((lines <= -1) && (chars <= 0))) {
        return(0);
    }
    if (cb->used == 0) {
        return(0);                      /* no unread data available */
    }
    if (lines > 0) {
        chars = -1;                     /* chars param not used if lines > 0 */
    }
    i = cb->i_out;
    while (i != cb->i_in) {
        ++n;
        if (chars > 0) {
            --chars;
        }
        if (cb->data[i] == '\n') {
            if (lines > 0) {
                --lines;
            }
            m = n;
            ++l;
        }
        if ((chars == 0) || (lines == 0)) {
            break;
        }
        i = (i + 1) % (cb->size + 1);
    }
    if (lines > 0) {
        return(0);                      /* all or none, and not enough found */
    }
    *nlines = l;
    return(m);
}


static int
cbuf_get_fd (void *dstbuf, int *psrcfd, int len)
{
/*  Copies data from the file referenced by the file descriptor
 *    pointed at by [psrcfd] into cbuf's [dstbuf].
 *  Returns the number of bytes read from the fd, 0 on EOF, or -1 on error.
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
cbuf_get_mem (void *dstbuf, unsigned char **psrcbuf, int len)
{
/*  Copies data from the buffer pointed at by [psrcbuf] into cbuf's [dstbuf].
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
cbuf_put_fd (void *srcbuf, int *pdstfd, int len)
{
/*  Copies data from cbuf's [srcbuf] into the file referenced
 *    by the file descriptor pointed at by [pdstfd].
 *  Returns the number of bytes written to the fd, or -1 on error.
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
cbuf_put_mem (void *srcbuf, unsigned char **pdstbuf, int len)
{
/*  Copies data from cbuf's [srcbuf] into the buffer pointed at by [pdstbuf].
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


static int cbuf_dropper(cbuf_t *cb, int len)
{
/*  Discards exactly [len] bytes of unread data from [cb].
 *  Returns the number of bytes dropped.
 */
    assert(cb != NULL);
    assert(len > 0);
    assert(len <= cb->used);
    assert(_cbuf_mutex_is_locked(cb));

    cb->used -= len;
    cb->i_out = (cb->i_out + len) % (cb->size + 1);

    return(len);
}


static int cbuf_reader(cbuf_t *src, int len, cbuf_iof putf, void *dst)
{
/*  Reads up to [len] bytes from [src] into the object pointed at by [dst].
 *    The I/O function [putf] specifies how data is written into [dst].
 *  Returns the number of bytes read, or -1 on error (with errno set).
 *  Note that [dst] is a value-result parameter and will be "moved forward"
 *    by the number of bytes written into it.
 */
    int nleft, n, m;
    int i_src;

    assert(src != NULL);
    assert(len > 0);
    assert(putf != NULL);
    assert(dst != NULL);
    assert(_cbuf_mutex_is_locked(src));

    /*  Bound len by the number of bytes available.
     */
    len = MIN(len, src->used);
    if (len == 0) {
        return(0);
    }
    /*  Copy data from src cbuf to dst obj.  Do the cbuf hokey-pokey and
     *    wrap-around the buffer at most once.  Break out if putf() returns
     *    either an ERR or a short count.
     */
    i_src = src->i_out;
    nleft = len;
    m = 0;
    while (nleft > 0) {
        n = MIN(nleft, (src->size + 1) - i_src);
        m = putf(&src->data[i_src], dst, n);
        if (m > 0) {
            nleft -= m;
            i_src = (i_src + m) % (src->size + 1);
        }
        if (n != m) {
            break;                      /* got ERR or "short" putf() */
        }
    }
    /*  Compute number of bytes written to dst obj.
     */
    n = len - nleft;
    assert((n >= 0) && (n <= len));
    /*
     *  If no data has been written, return the ERR reported by putf().
     */
    if (n == 0) {
        return(m);
    }
    return(n);
}


static int cbuf_writer(cbuf_t *dst, int len, cbuf_iof getf, void *src, int *ndropped)
{
/*  Writes up to [len] bytes from the object pointed at by [src] into [dst].
 *    The I/O function [getf] specifies how data is read from [src].
 *  Returns the number of bytes written, or -1 on error (with errno set).
 *  Sets [ndropped] (if not NULL) to the number of [dst] bytes overwritten.
 *  Note that [src] is a value-result parameter and will be "moved forward"
 *    by the number of bytes read from it.
 */
    int nfree, nleft, nrepl, n, m;
    int i_dst;

    assert(dst != NULL);
    assert(len > 0);
    assert(getf != NULL);
    assert(src != NULL);
    assert(_cbuf_mutex_is_locked(dst));

    /*  Attempt to grow dst cbuf if necessary.
     */
    nfree = dst->size - dst->used;
    /*  Compute number of bytes to write to dst cbuf.
     */
    if (!dst->overwrite) {
        len = MIN(len, dst->size - dst->used);
        if (len == 0) {
            errno = ENOSPC;
            return(-1);
        }
    }
    /*  Copy data from src obj to dst cbuf.  Do the cbuf hokey-pokey and
     *    wrap-around the buffer as needed.  Break out if getf() returns
     *    either an EOF/ERR or a short count.
     */
    i_dst = dst->i_in;
    nleft = len;
    m = 0;
    while (nleft > 0) {
        n = MIN(nleft, (dst->size + 1) - i_dst);
        m = getf(&dst->data[i_dst], src, n);
        if (m > 0) {
            nleft -= m;
            i_dst = (i_dst + m) % (dst->size + 1);
        }
        if (n != m) {
            break;                      /* got EOF/ERR or "short" getf() */
        }
    }
    /*  Compute number of bytes written to dst cbuf.
     */
    n = len - nleft;
    assert((n >= 0) && (n <= len));
    /*
     *  If no data has been written, return the EOF/ERR reported by getf().
     */
    if (n == 0) {
        return(m);
    }
    /*  Update dst cbuf metadata.
     */
    if (n > 0) {
        nrepl = (dst->i_out - dst->i_rep + (dst->size + 1)) % (dst->size + 1);
        dst->used = MIN(dst->used + n, dst->size);
        assert(i_dst == (dst->i_in + n) % (dst->size + 1));
        dst->i_in = i_dst;
        if (n > nfree - nrepl) {
            dst->got_wrap = 1;
            dst->i_rep = (dst->i_in + 1) % (dst->size + 1);
        }
        if (n > nfree) {
            dst->i_out = dst->i_rep;
        }
    }
    if (ndropped) {
        *ndropped = MAX(0, n - nfree);
    }
    return(n);
}


#ifndef NDEBUG
static int _cbuf_mutex_is_locked(cbuf_t *cb)
{
/*  Returns true if the mutex is locked; o/w, returns false.
 */
    int rc;

    assert(cb != NULL);
    rc = pthread_mutex_trylock(&cb->mutex);
    return(rc == EBUSY ? 1 : 0);
}

static int _cbuf_is_valid(cbuf_t *cb)
{
/*  Validates the data structure.  All invariants should be tested here.
 *  Returns true if everything is valid; o/w, aborts due to assertion failure.
 */
    int nfree;

    assert(cb != NULL);
    assert(_cbuf_mutex_is_locked(cb));
    assert(cb->data != NULL);
    assert(cb->magic == CBUF_MAGIC);
    /*
     *  Must use memcmp since overflow cookie may not be word-aligned.
     */
    assert(memcmp(cb->data - CBUF_MAGIC_LEN,
        (void *) &cb->magic, CBUF_MAGIC_LEN) == 0);
    assert(memcmp(cb->data + cb->size + 1,
        (void *) &cb->magic, CBUF_MAGIC_LEN) == 0);

    assert(cb->size > 0);
    assert(cb->used >= 0);
    assert(cb->used <= cb->size);
    assert(cb->got_wrap || !cb->i_rep); /* i_rep = 0 if data has not wrapped */
    assert(cb->i_in >= 0);
    assert(cb->i_in <= cb->size);
    assert(cb->i_out >= 0);
    assert(cb->i_out <= cb->size);
    assert(cb->i_rep >= 0);
    assert(cb->i_rep <= cb->size);

    if (cb->i_in >= cb->i_out) {
        assert((cb->i_rep > cb->i_in) || (cb->i_rep <= cb->i_out));
    }
    else /* if (cb->in < cb->i_out) */ {
        assert((cb->i_rep > cb->i_in) && (cb->i_rep <= cb->i_out));
    }
    nfree = (cb->i_out - cb->i_in - 1 + (cb->size + 1)) % (cb->size + 1);
    assert(cb->size - cb->used == nfree);

    return(1);
}
#endif /* !NDEBUG */
