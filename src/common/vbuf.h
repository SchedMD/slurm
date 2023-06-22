/*****************************************************************************\
 *  vbuf.h - vector buffer handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#ifndef _VBUF_H
#define _VBUF_H

typedef struct vbuf_s vbuf_t;

/*
 * Create new vbuf object
 * IN collector_threshold - 0 for default or byte max to force collector use
 * IN collector_bytes - 0 for default or number of bytes in collector xmalloc()
 * RET ptr to new vbuf_t instance
 */
extern vbuf_t *vbuf_new(size_t collector_threshold, size_t collector_bytes);
/*
 * Release vbuf object.
 * Always use FREE_NULL_VBUF() instead.
 */
extern void vbuf_free(vbuf_t *buf);

#define FREE_NULL_VBUF(_X)     \
do {                           \
	if (_X)                \
		vbuf_free(_X); \
	_X = NULL;             \
} while (0)

/*
 * Push data to buf
 * IN buf - buffer to push data onto
 * IN data - ptr to block of memory (must be xmalloc()ed)
 * IN bytes - number of bytes populated in data (may not match xsize())
 */
extern void vbuf_push(vbuf_t *buf, void *data, size_t bytes);

/*
 * Pop data from buf
 * IN buf - buffer to pop data from
 * IN data_ptr - ptr to populate with data ptr
 * IN bytes_ptr - ptr to populate with bytes populated or NULL
 */
extern void vbuf_pop(vbuf_t *buf, void **data_ptr, size_t *bytes_ptr);

/*
 * Join all data into single xmalloc()
 * IN buf - buffer to pop all data from
 * IN data_ptr - ptr to populate with data ptr
 * IN bytes_ptr - ptr to populate with bytes populated or NULL
 * IN free - true to release all data or false to maintain data
 */
extern void vbuf_join_data(vbuf_t *buf, void **data_ptr, size_t *bytes_ptr,
			   bool free);

/*
 * Convert vbuf to single contiguous string
 * IN/OUT buf_ptr - buffer to pop all data from.
 * 	buf will be released and set to NULL.
 * IN bytes_ptr - ptr to populate with bytes populated or NULL
 * IN free_data - true to release all data or false to maintain data
 * IN free_buf - true to release all buf or false to maintain buf
 * RET ptr to xmalloc()ed (must be xfree()ed)
 */
extern void *vbuf_to_string(vbuf_t **buf_ptr, size_t *bytes_ptr, bool free_data,
			    bool free_buf);

/*
 * printf(fmt, ...) and then push result to vbuf.
 * NULL terminator is stripped.
 */
#define vbuf_push_printf(buf, fmt, ...)               \
do {                                                  \
	char *p = xstrdup_printf(fmt, ##__VA_ARGS__); \
	size_t bytes = p ? strlen(p) : 0;             \
	if (bytes > 1) {                              \
		vbuf_push(buf, p, bytes);             \
	} else {                                      \
		xassert(!bytes || (p[0] == '\0'));    \
		xfree(p);                             \
	}                                             \
} while (0)

/*
 * Duplicate data and then push onto buf
 * IN buf - buffer to push copied data onto
 * IN data - ptr to bytes to copy
 * IN bytes - number of bytes to copy in data
 */
extern void vbuf_dup_push(vbuf_t *buf, const void *data, const size_t bytes);

/*
 * Duplicate string and push onto buf
 * NULL terminator is stripped.
 */
#define vbuf_dup_string(buf, str) vbuf_dup_push(buf, str, strlen(str))

/*
 * true if buffer has no data
 * IN buf - buffer to check
 * RET true if data in buffer or false if buffer has no data
 */
extern bool vbuf_is_empty(vbuf_t *buf);

#endif /* _VBUF_H */
