/*****************************************************************************
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
 *****************************************************************************/


#ifndef _CBUF_H
#define _CBUF_H


/***********
 *  Notes  *
 ***********/
/*
 *  Cbuf is a circular-buffer capable of dynamically resizing itself.  Old data
 *  in the buffer will be overwritten once it has reached its maximum size or
 *  is unable to allocate additional memory.  Writes into a cbuf are bounded
 *  by its maximum size; thus, a write/move/copy may return less than the
 *  number of bytes requested.  The exception to this is cbuf_put_line() which
 *  writes the entire string, dropping characters from the front as needed.
 *
 *  If NDEBUG is not defined, internal debug code will be enabled.  This is
 *  intended for development use only and production code should define NDEBUG.
 *
 *  If WITH_LSD_FATAL_ERROR_FUNC is defined, the linker will expect to find an
 *  external lsd_fatal_error() function.  By default, lsd_fatal_error() is a
 *  macro definition that outputs an error message to stderr.  This macro may
 *  be redefined to invoke another routine instead.
 *
 *  If WITH_LSD_NOMEM_ERROR_FUNC is defined, the linker will expect to find an
 *  external lsd_nomem_error() function.  By default, lsd_nomem_error() is a
 *  macro definition that returns NULL.  This macro may be redefined to invoke
 *  another routine instead.
 *
 *  If WITH_PTHREADS is defined, these routines will be thread-safe.
 *
 *  XXX: Buffer shrinking not implemented yet.
 *  XXX: Adaptive chunksize not implemented yet.
 *  XXX: Buffer wrap control not implemented yet.
 */


/****************
 *  Data Types  *
 ****************/

typedef struct cbuf * cbuf_t;
/*
 *  Circular-buffer opaque data type.
 */


/***************
 *  Functions  *
 ***************/

cbuf_t cbuf_create (int minsize, int maxsize);
/*
 *  Creates and returns a new circular buffer, or out_of_memory() on failure.
 *  The buffer is initially allocated to hold [minsize] bytes of data,
 *    but can attempt to grow up to [maxsize] bytes before overwriting data.
 *  Set minsize = maxsize to prevent cbuf from dynamically resizing itself.
 *  Abandoning a cbuf without calling cbuf_destroy() will cause a memory leak.
 */

void cbuf_destroy (cbuf_t cb);
/*
 *  Destroys the circular buffer [cb].
 */

void cbuf_flush (cbuf_t cb);
/*
 *  Flushes all data (including replay data) in [cb].
 */

int cbuf_is_empty (cbuf_t cb);
/*
 *  Returns non-zero if [cb] is empty; o/w, returns zero.
 */

int cbuf_size (cbuf_t cb);
/*
 *  Returns the current size of the buffer allocated to [cb]
 *    (ie, the number of bytes in can currently hold).
 */

int cbuf_free (cbuf_t cb);
/*
 *  Returns the number of bytes in [cb] available for writing before
 *    old data is overwritten (unless the cbuf is able to resize itself).
 */

int cbuf_used (cbuf_t cb);
/*
 *  Returns the number of bytes in [cb] available for reading.
 */

int cbuf_drop (cbuf_t cb, int len);
/*
 *  Discards up to [len] bytes of unread data from [cb];
 *    this data will still be available via the replay buffer.
 *  Returns the number of bytes dropped, or <0 on error (with errno set).
 */

int cbuf_copy (cbuf_t src, cbuf_t dst, int len, int *ndropped);
/*
 *  Copies up to [len] bytes of data from the [src] cbuf into the [dst] cbuf.
 *    If [len] is -1, it will be set to the number of [src] bytes available
 *    for reading (ie, cbuf_used(src)).
 *  Returns the number of bytes copied, or <0 on error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of [dst] bytes overwritten.
 */

int cbuf_move (cbuf_t src, cbuf_t dst, int len, int *ndropped);
/*
 *  Moves up to [len] bytes of data from the [src] cbuf into the [dst] cbuf.
 *    If [len] is -1, it will be set to the number of [src] bytes available
 *    for reading (ie, cbuf_used(src)).
 *  Returns the number of bytes moved, or <0 on error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of [dst] bytes overwritten.
 */

int cbuf_peek (cbuf_t cb, void *dstbuf, int len);
/*
 *  Reads up to [len] bytes of data from [cb] into the buffer [dstbuf],
 *    but does not consume the data read from the cbuf.
 *  The "peek" can be committed to the cbuf via a call to cbuf_drop(),
 *    but the peek+drop operation is not atomic.
 *  Returns the number of bytes read, or <0 on error (with errno set).
 */

int cbuf_read (cbuf_t cb, void *dstbuf, int len);
/*
 *  Reads up to [len] bytes of data from [cb] into the buffer [dstbuf].
 *  Returns the number of bytes read, or <0 on error (with errno set).
 */

int cbuf_replay (cbuf_t cb, void *dstbuf, int len);
/*
 *  Replays up to [len] bytes of previously read data from [cb] into the
 *    buffer [dstbuf].
 *  Returns the number of bytes replayed, or <0 on error (with errno set).
 *  XXX: Not implemented yet.
 */

int cbuf_write (cbuf_t cb, const void *srcbuf, int len, int *ndropped);
/*
 *  Writes up to [len] bytes of data from the buffer [srcbuf] into [cb].
 *  Returns the number of bytes written, or <0 on error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of bytes overwritten.
 */

int cbuf_peek_to_fd (cbuf_t cb, int dstfd, int len);
/*
 *  Reads up to [len] bytes of data from [cb] into the file referenced by the
 *    file descriptor [dstfd], but does not consume data read from the cbuf.
 *    If [len] is -1, it will be set to the number of bytes in [cb] available
 *    for reading (ie, cbuf_used(cb)).
 *  The "peek" can be committed to the cbuf via a call to cbuf_drop(),
 *    but the peek+drop operation is not atomic.
 *  Returns the number of bytes read, or <0 on error (with errno set).
 */

int cbuf_read_to_fd (cbuf_t cb, int dstfd, int len);
/*
 *  Reads up to [len] bytes of data from [cb] into the file referenced by the
 *    file descriptor [dstfd].  If [len] is -1, it will be set to the number
 *    of bytes in [cb] available for reading (ie, cbuf_used(cb)).
 *  Returns the number of bytes read, or <0 on error (with errno set).
 */

int cbuf_replay_to_fd (cbuf_t cb, int dstfd, int len);
/*
 *  Replays up to [len] bytes of previously read data from [cb] into the file
 *    referenced by the file descriptor [dstfd].  If [len] is -1, it will be
 *    set to the maximum number of bytes available for replay.
 *  Returns the number of bytes replayed, or <0 on error (with errno set).
 *  XXX: Not implemented yet.
 */

int cbuf_write_from_fd (cbuf_t cb, int srcfd, int len, int *ndropped);
/*
 *  Writes up to [len] bytes of data from the file referenced by the file
 *    descriptor [srcfd] into [cb].  If [len] is -1, it will be set to the
 *    number of bytes in [cb] available for writing (ie, cbuf_free(cb)).
 *  Returns the number of bytes written, 0 on EOF, or <0 on error (with errno).
 *    Sets [ndropped] (if not NULL) to the number of bytes overwritten.
 */

int cbuf_get_line (cbuf_t cb, char *dst, int len);
/*
 *  Reads a line of data from [cb] into the buffer [dst].  Reading stops after
 *    a newline or NUL which is also written into the [dst] buffer.  The buffer
 *    will be NUL-terminated and contain at most ([len] - 1) characters.  If
 *    [len] is 0, the line will be discarded from the cbuf and nothing will be
 *    written into the [dst] buffer.
 *  Returns the line strlen on success; if >= [len], truncation occurred and
 *    the excess line data was discarded.  Returns 0 if a newline is not found;
 *    no data is consumed in this case.  Returns <0 on error (with errno set).
 */

int cbuf_peek_line (cbuf_t cb, char *dst, int len);
/*
 *  Reads a line of data from [cb] into the buffer [dst], but does not consume
 *    the data read from the cbuf.  Reading stops after a newline or NUL which
 *    is also written into the [dst] buffer.  The buffer will be NUL-terminated
 *    and contain at most ([len] - 1) characters.
 *  The "peek" can be committed to the cbuf via a call to cbuf_drop(),
 *    but the peek+drop operation is not atomic.
 *  Returns the strlen of the line on success; truncation occurred if >= [len].
 *    Returns 0 if a newline is not found, or <0 on error (with errno set).
 */

int cbuf_put_line (cbuf_t cb, const char *src, int *ndropped);
/*
 *  Writes the entire NUL-terminated string [src] into [cb].  A newline will
 *    be appended to the cbuf if [src] does not contain a trailing newline.
 *  Returns the number of bytes written, or <0 or error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of bytes overwritten.
 */


#endif /* !_CBUF_H */
