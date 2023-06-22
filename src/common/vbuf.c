/*****************************************************************************\
 *  vbuf.c - vector buffer handlers
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

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/vbuf.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define VBUF_MAGIC 0x120b00ab
#define SPAN_MAGIC 0x10abffab
#define DEFAULT_COLLECTOR_SIZE 120 /* number of bytes in collector */
#define DEFAULT_COLLECTOR_THRES 50

typedef struct span_s span_t;

struct span_s {
	uint32_t magic; /* SPAN_MAGIC */
	span_t *next;
	void *data;
	size_t bytes; /* number of bytes in data - may not match xsize(data) */
};

struct vbuf_s {
	uint32_t magic; /* VBUF_MAGIC */

	span_t *head;
	span_t *tail;

	span_t *collector;
	/*
	 * if push is bytes < collect_threshold, then force sending to collector
	 */
	size_t collector_threshold;
	size_t collector_bytes;
};

static void _check_span_magic(span_t *span)
{
	xassert(span->magic == SPAN_MAGIC);
	xassert(!span->next || (xsize(span->next) >= 0));
	xassert(span->data);
	xassert(span->bytes >= 0);
	xassert(xsize(span->data) >= span->bytes);
}

static void _check_span_chain(vbuf_t *buf)
{
	span_t *p = buf->head;

	/* walk the chain to the tail */
	while (p && p->next) {
		_check_span_magic(p);
		p = p->next;
	}

	if (p)
		_check_span_magic(p);
	xassert(p == buf->tail);
}

static void _free_span(span_t *span)
{
	xassert(span->magic == SPAN_MAGIC);
	span->magic = ~SPAN_MAGIC;
	xfree(span->data);
	xfree(span);
}

static void _check_vbuf_magic(vbuf_t *buf)
{
	xassert(buf->magic == VBUF_MAGIC);
	xassert(!buf->tail || !buf->tail->next);
	_check_span_chain(buf);
	_check_span_magic(buf->collector);
}

extern span_t *_new_span(void)
{
	span_t *span = xmalloc(sizeof(*span));
	span->magic = SPAN_MAGIC;
	return span;
}

static void _new_collector(vbuf_t *buf)
{
	xassert(!buf->collector);
	buf->collector = _new_span();
	buf->collector->data = xmalloc(buf->collector_bytes);
}

extern vbuf_t *vbuf_new(size_t collector_threshold, size_t collector_bytes)
{
	vbuf_t *buf = xmalloc(sizeof(*buf));
	buf->magic = VBUF_MAGIC;

	buf->collector_threshold = (collector_threshold ?  collector_threshold :
				    DEFAULT_COLLECTOR_THRES);
	buf->collector_bytes = (collector_bytes ? collector_bytes :
				DEFAULT_COLLECTOR_SIZE);
	xassert(buf->collector_threshold > 0);
	xassert(buf->collector_bytes > 0);

	_new_collector(buf);

	_check_vbuf_magic(buf);
	return buf;
}

extern void vbuf_free(vbuf_t *buf)
{
	_check_vbuf_magic(buf);
	xassert(buf->magic == VBUF_MAGIC);
	buf->magic = ~VBUF_MAGIC;

	if (buf->head)
		for (span_t *p = buf->head; p->next; p = p->next)
			_free_span(p);
	_free_span(buf->collector);

	xfree(buf);
}

static void _push_span(vbuf_t *buf, span_t *span)
{
	_check_vbuf_magic(buf);
	xassert(span->data);
	xassert(span->bytes > 0);

	if (!buf->tail) {
		xassert(!buf->head);
		buf->head = span;
		buf->tail = span;
	} else {
		buf->tail->next = span;
		buf->tail = span;
	}

	_check_vbuf_magic(buf);
}

static void _push_collector(vbuf_t *buf)
{
	_check_vbuf_magic(buf);
	xassert(buf->collector->bytes > 0);
	xassert(buf->collector->data);

	_push_span(buf, buf->collector);
	buf->collector = NULL;

	_new_collector(buf);
}

/* does not xfree(data)! */
static void _dup_to_collector(vbuf_t *buf, const void *data, size_t bytes)
{
	size_t collector_remain =
		(buf->collector_bytes - buf->collector->bytes);

	_check_vbuf_magic(buf);

	if (bytes > collector_remain) {
		_dup_to_collector(buf, data, collector_remain);
		/* collector should already have been pushed */
		xassert(!buf->collector->bytes);
		/* shift data up by byte count */
		data += collector_remain;
		bytes -= collector_remain;
		xassert(bytes > 0);
	}

	/* copy data directly into collector */
	memmove((buf->collector->data + buf->collector->bytes),
		data, bytes);
	buf->collector->bytes += bytes;
	xassert(buf->collector->bytes <= buf->collector_bytes);

	if (buf->collector->bytes == buf->collector_bytes) {
		/* auto push collector if it is already full */
		_push_collector(buf);
	}
}

/* should data be sent to collector instead of getting pushed */
static bool _is_collector_target(vbuf_t *buf, size_t bytes)
{
	_check_vbuf_magic(buf);

	/*
	 * There are enough bytes left in collector or byte count is
	 * below the threshold
	 */
	return (bytes <=
		(buf->collector_bytes - buf->collector->bytes)) ||
		(bytes <= buf->collector_threshold);
}

extern void vbuf_push(vbuf_t *buf, void *data, size_t bytes)
{
	span_t *span;

	_check_vbuf_magic(buf);

	if (_is_collector_target(buf, bytes)) {
		_dup_to_collector(buf, data, bytes);
		xfree(data);
		return;
	}

	if (buf->collector->bytes > 0) {
		/*
		 * if collector has any contents, then we need to push it first
		 * to maintain the contents order
		 */
		_push_collector(buf);
	}

	span = _new_span();
	span->data = data;
	span->bytes = bytes;
	_push_span(buf, span);
}

extern void vbuf_pop(vbuf_t *buf, void **data_ptr, size_t *bytes_ptr)
{
	_check_vbuf_magic(buf);

	xassert(data_ptr && !*data_ptr);
	xassert(!bytes_ptr || !*bytes_ptr);

	if (!buf->head && (buf->collector->bytes > 0))
		_push_collector(buf);

	if (!buf->head) {
		*data_ptr = NULL;
		if (bytes_ptr)
			*bytes_ptr = 0;
	} else {
		span_t *span = buf->head;

		if (buf->tail == span) {
			buf->tail = NULL;
			xassert(!span->next);
		}
		buf->head = span->next;

		*data_ptr = span->data;
		if (bytes_ptr)
			*bytes_ptr = span->bytes;

		_free_span(span);
	}
}

extern void vbuf_join_data(vbuf_t *buf, void **data_ptr, size_t *bytes_ptr,
			   bool free)
{
	size_t total_bytes = 0, bytes = 0;
	void *data;

	_check_vbuf_magic(buf);
	xassert(data_ptr && !*data_ptr);
	xassert(!bytes_ptr || !*bytes_ptr);

	if (buf->collector->bytes > 0) {
		/* always push collector since all data needs to be collated */
		_push_collector(buf);
	}

	if (!buf->head) {
		*data_ptr = NULL;
		if (bytes_ptr)
			*bytes_ptr = 0;
		return;
	}

	/* walk the chain to get the total size */
	for (span_t *p = buf->head; p->next; p = p->next)
		total_bytes += p->bytes;

	/* always include a NULL terminator */
	data = xmalloc(total_bytes + 1);

	for (span_t *p = buf->head; p->next; p = p->next) {
		memmove(data + bytes, p->data, p->bytes);
		bytes += p->bytes;

		if (free)
			_free_span(p);
	}

	if (free) {
		buf->head = NULL;
		buf->tail = NULL;
	}

	xassert(total_bytes == bytes);

	*data_ptr = data;
	if (bytes_ptr)
		*bytes_ptr = total_bytes;
}

extern void vbuf_dup_push(vbuf_t *buf, const void *data, const size_t bytes)
{
	_check_vbuf_magic(buf);

	if (_is_collector_target(buf, bytes)) {
		_dup_to_collector(buf, data, bytes);
	} else {
		void *dup = xmalloc_nz(bytes);
		memmove(dup, data, bytes);
		vbuf_push(buf, dup, bytes);
	}
}

extern void *vbuf_to_string(vbuf_t **buf_ptr, size_t *bytes_ptr, bool free_data,
			    bool free_buf)
{
	void *data = NULL;

	_check_vbuf_magic(*buf_ptr);

	vbuf_join_data(*buf_ptr, &data, bytes_ptr, free_data);

	if (free_buf)
		FREE_NULL_VBUF(*buf_ptr);

	return data;
}

extern bool vbuf_is_empty(vbuf_t *buf)
{
	_check_vbuf_magic(buf);

	if (buf->head && (buf->head->bytes > 0))
		return false;

	return !buf->collector->bytes;
}
