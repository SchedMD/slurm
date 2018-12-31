/*****************************************************************************
 *  cbuf.h
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
 *****************************************************************************/


#ifndef LSD_CBUF_H
#define LSD_CBUF_H


/***********
 *  Notes  *
 ***********/
/*
 *  Cbuf is a circular-buffer capable of dynamically resizing itself.
 *  Unread data in the buffer will be overwritten once the cbuf has
 *  reached its maximum size or is unable to allocate additional memory.
 *
 *  The CBUF_OPT_OVERWRITE option specifies how unread cbuf data will
 *  be overwritten.  If set to CBUF_NO_DROP, unread data will never be
 *  overwritten; writes into the cbuf will return -1 with ENOSPC.  If set
 *  to CBUF_WRAP_ONCE, a single write operation will wrap-around the buffer
 *  at most once, and up to cbuf_used() bytes of data may be overwritten.
 *  If set to CBUF_WRAP_MANY, a single write operation will wrap-around the
 *  buffer as many times as needed in order to write all of the data.
 *
 *  These routines are thread-safe.
 */


/****************
 *  Data Types  *
 ****************/

typedef struct cbuf * cbuf_t;           /* circular-buffer opaque data type  */

typedef enum {                          /* cbuf option names                 */
    CBUF_OPT_OVERWRITE
} cbuf_opt_t;

typedef enum {                          /* CBUF_OPT_OVERWRITE values:        */
    CBUF_NO_DROP,                       /* -never drop data, ENOSPC if full  */
    CBUF_WRAP_ONCE,                     /* -drop data, wrapping at most once */
    CBUF_WRAP_MANY                      /* -drop data, wrapping as needed    */
} cbuf_overwrite_t;


/***************
 *  Functions  *
 ***************/

cbuf_t cbuf_create (int minsize, int maxsize);
/*
 *  Creates and returns a new circular buffer, or lsd_nomem_error() on failure.
 *  The buffer is initially allocated to hold [minsize] bytes of data,
 *    but can attempt to grow up to [maxsize] bytes before overwriting data.
 *  Set minsize = maxsize to prevent cbuf from dynamically resizing itself.
 *  The default overwrite option behavior is CBUF_WRAP_MANY.
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

int cbuf_size (cbuf_t cb);
/*
 *  Returns the maximum size of the buffer allocated to [cb]
 *    (ie, the number of bytes it can currently hold).
 */

int cbuf_free (cbuf_t cb);
/*
 *  Returns the number of bytes in [cb] available for writing before unread
 *    data is overwritten (assuming the cbuf can resize itself if needed).
 */

int cbuf_used (cbuf_t cb);
/*
 *  Returns the number of bytes in [cb] available for reading.
 */

int cbuf_lines_used (cbuf_t cb);
/*
 *  Returns the number of lines in [cb] available for reading.
 */

int cbuf_reused (cbuf_t cb);
/*
 *  Returns the number of bytes in [cb] available for replaying/rewinding.
 */

int cbuf_lines_reused (cbuf_t cb);
/*
 *  Returns the number of lines in [cb] available for replaying/rewinding.
 */

int cbuf_is_empty (cbuf_t cb);
/*
 *  Returns non-zero if [cb] is empty; o/w, returns zero.
 */

int cbuf_opt_get (cbuf_t cb, cbuf_opt_t name, int *value);
/*
 *  Gets the [name] option for [cb] and sets [value] to the result.
 *  Returns 0 on success, or -1 on error (with errno set).
 */

int cbuf_opt_set (cbuf_t cb, cbuf_opt_t name, int value);
/*
 *  Sets the [name] option for [cb] to [value].
 *  Returns 0 on success, or -1 on error (with errno set).
 */

int cbuf_drop (cbuf_t src, int len);
/*
 *  Discards up to [len] bytes of unread data from [src];
 *    if [len] is -1, all unread data will be dropped.
 *  Dropped data is still available via the replay buffer.
 *  Returns the number of bytes dropped, or -1 on error (with errno set).
 */

int cbuf_peek (cbuf_t src, void *dstbuf, int len);
/*
 *  Reads up to [len] bytes of data from the [src] cbuf into [dstbuf],
 *    but does not consume the data read from the cbuf.
 *  The "peek" can be committed to the cbuf via a call to cbuf_drop(),
 *    but the peek+drop combination is not atomic.
 *  Returns the number of bytes read, or -1 on error (with errno set).
 */

int cbuf_read (cbuf_t src, void *dstbuf, int len);
/*
 *  Reads up to [len] bytes of data from the [src] cbuf into [dstbuf].
 *  Returns the number of bytes read, or -1 on error (with errno set).
 */

int cbuf_replay (cbuf_t src, void *dstbuf, int len);
/*
 *  Replays up to [len] bytes of previously read data from the [src] cbuf
 *    into [dstbuf].
 *  Returns the number of bytes replayed, or -1 on error (with errno set).
 */

int cbuf_rewind (cbuf_t src, int len);
/*
 *  Rewinds [src] by up to [len] bytes, placing previously read data back in
 *    the unread data buffer; if [len] is -1, all replay data will be rewound.
 *  Returns the number of bytes rewound, or -1 on error (with errno set).
 */

int cbuf_write (cbuf_t dst, void *srcbuf, int len, int *ndropped);
/*
 *  Writes up to [len] bytes of data from [srcbuf] into the [dst] cbuf
 *    according to dst's CBUF_OPT_OVERWRITE behavior.
 *  Returns the number of bytes written, or -1 on error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of bytes overwritten.
 */

int cbuf_drop_line (cbuf_t src, int len, int lines);
/*
 *  Discards the specified [lines] of data from [src].  If [lines] is -1,
 *    discards the maximum number of lines comprised of up to [len] characters.
 *  Dropped data is still available via the replay buffer.
 *  Returns the number of bytes dropped, or -1 on error (with errno set).
 *    Returns 0 if the number of lines is not available (ie, all or none).
 */

int cbuf_peek_line (cbuf_t src, char *dstbuf, int len, int lines);
/*
 *  Reads the specified [lines] of data from the [src] cbuf into [dstbuf],
 *    but does not consume the data read from the cbuf.  If [lines] is -1,
 *    reads the maximum number of lines that [dstbuf] can hold.  The buffer
 *    will be NUL-terminated and contain at most ([len] - 1) characters.
 *  The "peek" can be committed to the cbuf via a call to cbuf_drop(),
 *    but the peek+drop combination is not atomic.
 *  Returns strlen of the line(s) on success; truncation occurred if >= [len].
 *    Returns 0 if the number of lines is not available (ie, all or none).
 *    Returns -1 on error (with errno set).
 */

int cbuf_read_line (cbuf_t src, char *dstbuf, int len, int lines);
/*
 *  Reads the specified [lines] of data from the [src] cbuf into [dstbuf].
 *    If [lines] is -1, reads the maximum number of lines that [dstbuf]
 *    can hold.  The buffer will be NUL-terminated and contain at most
 *    ([len] - 1) characters.
 *  Returns strlen of the line(s) on success; truncation occurred if >= [len],
 *    in which case excess line data is discarded.  Returns 0 if the number
 *    of lines is not available (ie, all or none), in which case no data is
 *    consumed.  Returns -1 on error (with errno set).
 */

int cbuf_replay_line (cbuf_t src, char *dstbuf, int len, int lines);
/*
 *  Replays the specified [lines] of data from the [src] cbuf into [dstbuf].
 *    If [lines] is -1, replays the maximum number of lines that [dstbuf]
 *    can hold.  A newline will be appended to [dstbuf] if the last (ie, most
 *    recently read) line does not contain a trailing newline.  The buffer
 *    will be NUL-terminated and contain at most ([len] - 1) characters.
 *  Returns strlen of the line(s) on success; truncation occurred if >= [len].
 *    Returns 0 if the number of lines is not available (ie, all or none).
 *    Returns -1 on error (with errno set).
 */

int cbuf_rewind_line (cbuf_t src, int len, int lines);
/*
 *  Rewinds [src] by the specified [lines] of data, placing previously read
 *    data back in the unread data buffer.  If [lines] is -1, rewinds the
 *    maximum number of lines comprised of up to [len] characters.
 *  Returns the number of bytes rewound, or -1 on error (with errno set).
 *    Returns 0 if the number of lines is not available (ie, all or none).
 */

int cbuf_write_line (cbuf_t dst, char *srcbuf, int *ndropped);
/*
 *  Writes the entire NUL-terminated [srcbuf] string into the [dst] cbuf
 *    according to dst's CBUF_OPT_OVERWRITE behavior.  A newline will be
 *    appended to the cbuf if [srcbuf] does not contain a trailing newline.
 *  Returns the number of bytes written, or -1 or error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of bytes overwritten.
 */

int cbuf_peek_to_fd (cbuf_t src, int dstfd, int len);
/*
 *  Reads up to [len] bytes of data from the [src] cbuf into the file
 *    referenced by the [dstfd] file descriptor, but does not consume the
 *    data read from the cbuf.  If [len] is -1, it will be set to the number
 *    of [src] bytes available for reading.
 *  The "peek" can be committed to the cbuf via a call to cbuf_drop(),
 *    but the peek+drop combination is not atomic.
 *  Returns the number of bytes read, or -1 on error (with errno set).
 */

int cbuf_read_to_fd (cbuf_t src, int dstfd, int len);
/*
 *  Reads up to [len] bytes of data from the [src] cbuf into the file
 *    referenced by the [dstfd] file descriptor.  If [len] is -1, it will
 *    be set to the number of [src] bytes available for reading.
 *  Returns the number of bytes read, or -1 on error (with errno set).
 */

int cbuf_replay_to_fd (cbuf_t src, int dstfd, int len);
/*
 *  Replays up to [len] bytes of previously read data from the [src] cbuf into
 *    the file referenced by the [dstfd] file descriptor.  If [len] is -1, it
 *    will be set to the maximum number of [src] bytes available for replay.
 *  Returns the number of bytes replayed, or -1 on error (with errno set).
 */

int cbuf_write_from_fd (cbuf_t dst, int srcfd, int len, int *ndropped);
/*
 *  Writes up to [len] bytes of data from the file referenced by the
 *    [srcfd] file descriptor into the [dst] cbuf according to dst's
 *    CBUF_OPT_OVERWRITE behavior.  If [len] is -1, it will be set to
 *    an appropriate chunk size.
 *  Returns the number of bytes written, 0 on EOF, or -1 on error (with errno).
 *    Sets [ndropped] (if not NULL) to the number of bytes overwritten.
 */

int cbuf_copy (cbuf_t src, cbuf_t dst, int len, int *ndropped);
/*
 *  Copies up to [len] bytes of data from the [src] cbuf into the [dst] cbuf
 *    according to dst's CBUF_OPT_OVERWRITE behavior.  If [len] is -1,
 *    it will be set to the number of [src] bytes available for reading.
 *  Returns the number of bytes copied, or -1 on error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of [dst] bytes overwritten.
 */

int cbuf_move (cbuf_t src, cbuf_t dst, int len, int *ndropped);
/*
 *  Moves up to [len] bytes of data from the [src] cbuf into the [dst] cbuf
 *    according to dst's CBUF_OPT_OVERWRITE behavior.  If [len] is -1,
 *    it will be set to the number of [src] bytes available for reading.
 *  Returns the number of bytes moved, or -1 on error (with errno set).
 *    Sets [ndropped] (if not NULL) to the number of [dst] bytes overwritten.
 */


#endif /* !LSD_CBUF_H */
