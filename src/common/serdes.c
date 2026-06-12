/*****************************************************************************\
 *  serdes.c - serialize and deserialize
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "slurm/slurm_errno.h"

#include "src/common/data.h"
#include "src/common/serdes.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

/*
 * Serializer plugin does doesn't support directly parsing from struct to
 * mime_type format. We need to instead parse into data_t into the mime_type.
 */
static int _indirect_parse(serialize_parse_state_t **state_ptr,
			   data_parser_t *parser, data_parser_type_t type,
			   void *dst, ssize_t dst_bytes, buf_t *src,
			   const char *mime_type)
{
	DEF_TIMERS;
	const void *ptr = (((void *) get_buf_data(src)) + get_buf_offset(src));
	const ssize_t bytes = remaining_buf(src);
	data_t *d = NULL;
	data_t *parent_path = NULL;
	int rc = EINVAL;

	START_TIMER;

	parent_path = data_set_list(data_new());

	if (!(rc = serialize_g_string_to_data(&d, ptr, bytes, mime_type)))
		rc = data_parser_g_parse(parser, type, dst, dst_bytes, d,
					 parent_path);

	/* Treat all bytes as read */
	if (!rc)
		set_buf_offset(src, size_buf(src));

	FREE_NULL_DATA(d);
	FREE_NULL_DATA(parent_path);
	END_TIMER2(__func__);

	return rc;
}

extern int serdes_parse(serialize_parse_state_t **state_ptr,
			data_parser_t *parser, data_parser_type_t type,
			void *dst, ssize_t dst_bytes, buf_t *src,
			const char *mime_type)
{
	int rc = serialize_g_parse(state_ptr, parser, type, dst, dst_bytes, src,
				   mime_type);

	if (rc != ESLURM_NOT_SUPPORTED)
		return rc;

	return _indirect_parse(state_ptr, parser, type, dst, dst_bytes, src,
			       mime_type);
}

extern int serdes_parse_buf(data_parser_t *parser, data_parser_type_t type,
			    void *dst, ssize_t dst_bytes, buf_t *src,
			    const char *mime_type)
{
	serialize_parse_state_t *state = NULL;
	int rc = EINVAL;

	do {
		rc = serdes_parse(&state, parser, type, dst, dst_bytes, src,
				  mime_type);
	} while (!rc && state);

	xassert(!state);
	return rc;
}

/*
 * Serializer plugin does doesn't support directly dumping from mime_type format
 * to struct. We need to instead dump into data_t from the mime_type and then
 * dump into the struct.
 */
static int _indirect_dump(serialize_dump_state_t **state_ptr,
			  data_parser_t *parser, data_parser_type_t type,
			  void *src, ssize_t src_bytes, buf_t *dst,
			  const char *mime_type, serializer_flags_t flags)
{
	DEF_TIMERS;
	data_t *d = NULL;
	size_t length = 0;
	char *buf = NULL;
	int rc = EINVAL;

	START_TIMER;

	d = data_new();

	if (!(rc = data_parser_g_dump(parser, type, src, src_bytes, d))) {
		if (data_parser_g_is_complex(parser))
			flags |= SER_FLAGS_COMPLEX;

		if (!(rc = serialize_g_data_to_string(&buf, &length, d,
						      mime_type, flags)))
			assign_buf(dst, &buf, length);
	}

	xfree(buf);
	FREE_NULL_DATA(d);
	END_TIMER2(__func__);

	return rc;
}

extern int serdes_dump(serialize_dump_state_t **state_ptr,
		       data_parser_t *parser, data_parser_type_t type,
		       void *src, ssize_t src_bytes, buf_t *dst,
		       const char *mime_type, serializer_flags_t flags)
{
	int rc = serialize_g_dump(state_ptr, parser, type, src, src_bytes, dst,
				  mime_type, flags);

	if (rc != ESLURM_NOT_SUPPORTED)
		return rc;

	return _indirect_dump(state_ptr, parser, type, src, src_bytes, dst,
			      mime_type, flags);
}

extern int serdes_dump_buf(data_parser_t *parser, data_parser_type_t type,
			   void *src, ssize_t src_bytes, buf_t *dst,
			   const char *mime_type, serializer_flags_t flags)
{
	serialize_dump_state_t *state = NULL;
	int rc = SLURM_SUCCESS;

	while (!rc) {
		if ((rc = serdes_dump(&state, parser, type, src, src_bytes, dst,
				      mime_type, flags)))
			break;

		/* check if dump is complete */
		if (!state)
			break;

		/*
		 * Expand buffer as dump is incomplete or release state on
		 * failure
		 */
		if ((rc = try_grow_buf(dst, BUF_SIZE)))
			(void) serdes_dump(&state, parser, type, src, src_bytes,
					   NULL, mime_type, flags);
	};

	/* Avoid leaking content from failed dump */
	if (rc)
		set_buf_offset(dst, 0);

	xassert(!state);
	return rc;
}
