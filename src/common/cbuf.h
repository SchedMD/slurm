/*****************************************************************************\
 * src/common/cbuf.h - circular buffer implementation
 * $Id$
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
\*****************************************************************************/


#ifndef _CBUF_H
#define _CBUF_H


/***********
 *  Notes  *
 ***********/
/*
 *  Cbuf is a circular-buffer capable of dynamically resizing itself.
 *    If it has reached its maximum size or if additional memory is
 *    unavailable, then old data in the buffer will be overwritten.
 *
 *  If NDEBUG is not defined, internal debug code will be enabled.
 *    This is intended for development use only and production code
 *    should define NDEBUG.
 *
 *  By default, out_of_memory() is a macro definition that returns NULL.
 *    This macro may be redefined to invoke another routine instead.
 *    If WITH_OOMF is defined, this macro will not be defined and the
 *    linker will expect to find an external Out-Of-Memory Function.
 *
 *  If WITH_PTHREADS is defined, these routines will be thread-safe.
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
 *  The buffer is initially allocated to hold 'minsize' bytes of data,
 *    but can attempt to grow up to 'maxsize' bytes before overwriting data.
 *  Set minsize = maxsize to prevent cbuf from dynamically resizing itself.
 *  Abandoning a cbuf without calling cbuf_destroy() will cause a memory leak.
 */

void cbuf_destroy (cbuf_t cb);
/*
 *  Destroys the circular buffer 'cb'.
 */

void cbuf_flush (cbuf_t cb);
/*
 *  Flushes all data (including replay data) in 'cb'.
 */

int cbuf_is_empty (cbuf_t cb);
/*
 *  Returns non-zero if 'cb' is empty; o/w, returns zero.
 */

int cbuf_size (cbuf_t cb);
/*
 *  Returns the current size of the buffer allocated to 'cb'
 *    (ie, the number of bytes in can currently hold).
 */

int cbuf_free (cbuf_t cb);
/*
 *  Returns the number of bytes in 'cb' available for writing before
 *    old data is overwritten (unless the cbuf is able to resize itself).
 */

int cbuf_used (cbuf_t cb);
/*
 *  Returns the number of bytes in 'cb' available for reading.
 */

int cbuf_read (cbuf_t cb, void *dstbuf, int len);
/*
 *  Reads up to 'len' bytes of data from 'cb' into the buffer 'dstbuf'.
 *  Returns the number of bytes read, or <0 on error (with errno set).
 */

int cbuf_replay (cbuf_t cb, void *dstbuf, int len);
/*
 *  Replays up to 'len' bytes of previously read data from 'cb' into the
 *    buffer 'dstbuf'.
 *  Returns the number of bytes replayed, or <0 on error (with errno set).
 */

int cbuf_write (cbuf_t cb, void *srcbuf, int len, int *dropped);
/*
 *  Writes up to 'len' bytes of data from the buffer 'srcbuf' into 'cb'.
 *  Returns the number of bytes written, or <0 on error (with errno set).
 *    Sets 'dropped' (if not NULL) to the number of bytes of data overwritten.
 */

int cbuf_read_fd (cbuf_t cb, int dstfd, int len);
/*
 *  Reads up to 'len' bytes of data from 'cb' into the file referenced by the
 *    file descriptor 'dstfd'.  If len is -1, it will be set to cbuf_used().
 *  Returns the number of bytes read, or <0 on error (with errno set).
 */

int cbuf_replay_fd (cbuf_t cb, int dstfd, int len);
/*
 *  Replays up to 'len' bytes of previously read data from 'cb' into the file
 *    referenced by the file descriptor 'dstfd'.  If len is -1, it will be
 *    set to the maximum number of bytes available for replay.
 *  Returns the number of bytes replayed, or <0 on error (with errno set).
 */

int cbuf_write_fd (cbuf_t cb, int srcfd, int len, int *dropped);
/*
 *  Writes up to 'len' bytes of data from the file referenced by the file
 *    descriptor 'srcfd' into 'cb'.  If len is -1, it will be set to
 *    cbuf_free().
 *  Returns the number of bytes written, 0 on EOF, or <0 on error (with errno).
 *    Sets 'dropped' (if not NULL) to the number of bytes of data overwritten.
 */


#endif /* !_CBUF_H */
