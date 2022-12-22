/*****************************************************************************\
 *  ref.h - Static strings reference
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#ifndef _COMMON_REF_H_
#define _COMMON_REF_H_

#include "src/common/data.h"
#include "src/interfaces/serializer.h"

/*
 * any ld static data will always have 3 symbols defined (start, end, size).
 *
 * See also https://www.devever.net/~hl/incbin
 * See also https://csl.name/post/embedding-binary-data/
 *
 * Warning: Do *NOT* use the symbol names directly.
 * Warning: size when compiled is mangled during runtime.
 *
 * ld will replace any '.' with '_'.
 */
#define decl_static_data(name)                     \
	extern const void *_binary_##name##_start; \
	extern const void *_binary_##name##_end;   \
	extern const void *_binary_##name##_size;

/*
 * Macros to retrieve the values of static data blob
 * Warning: blob may not be NULL terminated!
 */
#define static_ref_start(name) ((const char *) &_binary_##name##_start)
#define static_ref_end(name) ((const char *) &_binary_##name##_end)
#define static_ref_size(name) \
	((const size_t) (static_ref_end(name) - static_ref_start(name)))

/* Copy and convert static data to a string that needs to be xfreed() */
#define static_ref_to_cstring(src, name)                             \
	do {                                                         \
		const size_t data_size = static_ref_size(name);      \
		unsigned char *data_ptr = xmalloc(data_size + 1);    \
		xassert(data_size < UINT32_MAX);                     \
		memcpy(data_ptr, static_ref_start(name), data_size); \
		data_ptr[data_size] = '\0';                          \
		src = (void *) data_ptr;                             \
	} while (0)

/* static data to a data_t */
#define static_ref_json_to_data_t(data, name)                     \
	do {                                                      \
		char *json_data_ptr;                              \
		static_ref_to_cstring(json_data_ptr, name);       \
		serialize_g_string_to_data(&data, json_data_ptr,  \
					   static_ref_size(name), \
					   MIME_TYPE_JSON);       \
		xfree(json_data_ptr);                             \
	} while (0);

#endif /* _COMMON_REF_H_ */
