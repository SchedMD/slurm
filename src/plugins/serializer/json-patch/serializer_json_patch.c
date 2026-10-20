/*****************************************************************************\
 *  serializer_json_patch.c - Serializer for RFC 6902 JSON Patch
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/interfaces/serializer.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Serializer JSON Patch plugin";
const char plugin_type[] = "serializer/json-patch";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Required for serializer plugins: */
const serializer_mimetype_t mime_types[] = {
	{ .mimetype = "application/json-patch+json" }, /* RFC 6902 section 6 */
	{ 0 },
};

extern int serialize_p_init(serializer_flags_t flags)
{
	return SLURM_SUCCESS;
}

extern void serialize_p_fini(void)
{
	/* does nothing */
}

extern int serialize_p_data_to_string(char **dest, size_t *length, data_t *src,
				      serializer_flags_t flags)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int serialize_p_string_to_data(data_t **dest, const char *src,
				      size_t length)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int serialize_p_dump(serialize_dump_state_t **state_ptr,
			    data_parser_t *parser, data_parser_type_t type,
			    void *src, ssize_t src_bytes, buf_t *dst,
			    serializer_flags_t flags)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int serialize_p_parse(serialize_parse_state_t **state_ptr,
			     data_parser_t *parser, data_parser_type_t type,
			     void *dst, ssize_t dst_bytes, buf_t *src)
{
	return ESLURM_NOT_SUPPORTED;
}
