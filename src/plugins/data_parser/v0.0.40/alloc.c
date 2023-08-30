/*****************************************************************************\
 *  alloc.c - Slurm data parser allocators for objects
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "alloc.h"

extern void *alloc_parser_obj(const parser_t *const parser)
{
	const parser_t *const lparser =
		find_parser_by_type(parser->pointer_type);
	void *obj = NULL;

	check_parser(parser);
	check_parser(lparser);

	if (parser->new)
		obj = parser->new();
	else
		obj = xmalloc(lparser->size);

	xassert(obj);
	xassert(xsize(obj) == lparser->size);

	log_flag(DATA, "created %zd byte %s object at 0x%"PRIxPTR,
		 xsize(obj), lparser->obj_type_string, (uintptr_t) obj);

	return obj;
}

extern void free_parser_obj(const parser_t *const parser, void *ptr)
{
	const parser_t *const lparser =
		find_parser_by_type(parser->pointer_type);

	check_parser(parser);
	check_parser(lparser);

	if (!ptr)
		return;

	xassert(xsize(ptr) == lparser->size);

	log_flag(DATA, "destroying %zd byte %s object at 0x%"PRIxPTR,
		 xsize(ptr), lparser->obj_type_string, (uintptr_t) ptr);

	if (parser->free)
		parser->free(ptr);
	else
		xfree_ptr(ptr);
}
