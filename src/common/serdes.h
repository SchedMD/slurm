/*****************************************************************************\
 *  serdes.h - serialize and deserialize
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

#ifndef _COMMON_SERDES_H
#define _COMMON_SERDES_H

#include "src/interfaces/serializer.h"

/*
 * Parse given (potentially partial) string buffer into target struct dst
 * NOTE: Use SERDES_PARSE() macro instead of calling directly!
 * IN/OUT state_ptr - Pointer populated with parsing state
 *	If start_ptr is !NULL, then serializer expects another call to
 *	serialize_g_parse().
 *	Passing a NULL src will always release state_ptr.
 * IN parser - return from data_parser_g_new()
 * IN type - expected data_parser type of obj
 * IN dst - ptr to struct/scalar to populate
 *	This *must* be a pointer to the object and not just a value of the
 *	object.
 * IN dst_bytes - size of object pointed to by dst
 * IN src - string buffer to parse into obj.
 *	[offset, size) will be string to be read.
 *	src offset will be set with how many bytes have been processed
 * IN mime_type - deserialize data using given mime_type
 * RET SLURM_SUCCESS or error
 */
extern int serdes_parse(serialize_parse_state_t **state_ptr,
			data_parser_t *parser, data_parser_type_t type,
			void *dst, ssize_t dst_bytes, buf_t *src,
			const char *mime_type);

#define SERDES_PARSE(state, parser, type, dst, src, mime_type) \
	serdes_parse(&state, parser, DATA_PARSER_##type, &dst, sizeof(dst), \
		     src, mime_type)

/*
 * Parse given string buffer into target struct dst
 * NOTE: Use SERDES_PARSE_BUF() macro instead of calling directly!
 * IN parser - return from data_parser_g_new()
 * IN type - expected data_parser type of obj
 * IN dst - ptr to struct/scalar to populate
 *	This *must* be a pointer to the object and not just a value of the
 *	object.
 * IN dst_bytes - size of object pointed to by dst
 * IN src - string buffer to parse into obj.
 *	[offset, size) will be string to be read.
 *	src offset will be set with how many bytes have been processed
 * IN mime_type - deserialize data using given mime_type
 * RET SLURM_SUCCESS or error
 */
extern int serdes_parse_buf(data_parser_t *parser, data_parser_type_t type,
			    void *dst, ssize_t dst_bytes, buf_t *src,
			    const char *mime_type);

#define SERDES_PARSE_BUF(parser, type, dst, src, mime_type) \
	serdes_parse_buf(parser, DATA_PARSER_##type, &dst, sizeof(dst), src, \
			 mime_type)

/*
 * Dump given target struct src into fixed size buffer
 * NOTE: Use SERDES_DUMP() macro instead of calling directly!
 * IN/OUT state_ptr - Pointer populated with parsing state
 *	If state_ptr is !NULL, then serializer expects another call to
 *	serialize_g_dump().
 *	Passing a NULL dst will always release state_ptr.
 * IN parser - return from data_parser_g_new()
 * IN type - data_parser type of obj
 * IN src - ptr to struct/scalar to dump to data_t
 *	This *must* be a pointer to the object and not just a value of the object.
 * IN src_bytes - size of object pointed to by src
 * IN/OUT dst - buffer to populate with serialized output.
 *	Writes will try to honor the size of the buffer (but may resize it).
 *	Offset to indicate the bytes populated.
 * IN flags - optional flags to specify to serilzier to change presentation of
 *	data
 * RET SLURM_SUCCESS or error
 */
extern int serdes_dump(serialize_dump_state_t **state_ptr,
		       data_parser_t *parser, data_parser_type_t type,
		       void *src, ssize_t src_bytes, buf_t *dst,
		       const char *mime_type, serializer_flags_t flags);

#define SERDES_DUMP(state, parser, type, src, dst, mime_type, flags) \
	serdes_dump(&state, parser, DATA_PARSER_##type, &src, sizeof(src), \
		    dst, mime_type, flags)

/*
 * Dump given target struct src into buffer
 * NOTE: Use SERDES_DUMP_BUF() macro instead of calling directly!
 * NOTE: Use SERDES_DUMP() for fixed size buffers instead.
 * IN parser - return from data_parser_g_new()
 * IN type - data_parser type of obj
 * IN src - ptr to struct/scalar to dump to data_t
 *	This *must* be a pointer to the object and not just a value of the object.
 * IN src_bytes - size of object pointed to by src
 * OUT dst - pointer to buffer to populate
 *	dst buffer will be resized to fit entire string.
 * IN flags - optional flags to specify to serilzier to change presentation of
 *	data
 * RET SLURM_SUCCESS or error
 */
extern int serdes_dump_buf(data_parser_t *parser, data_parser_type_t type,
			   void *src, ssize_t src_bytes, buf_t *dst,
			   const char *mime_type, serializer_flags_t flags);

#define SERDES_DUMP_BUF(parser, type, src, dst, mime_type, flags) \
	serdes_dump_buf(parser, DATA_PARSER_##type, &src, sizeof(src), dst, \
			mime_type, flags)

#endif
