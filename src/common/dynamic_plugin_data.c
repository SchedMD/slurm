/*****************************************************************************\
 *  dynamic_plugin_data.c - Opaque data for plugins
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

#include "src/common/dynamic_plugin_data.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"

extern void dynamic_plugin_data_pack(
	dynamic_plugin_data_t *plugin_data,
	dynamic_plugin_data_pack_func pack_func,
	buf_t *buffer,
	uint16_t protocol_version)
{
	uint32_t length_position = 0, start = 0, end = 0;

	xassert(buffer);

	/*
	 * Save length position and pack zero length value to possibly overwrite
	 * later.
	 */
	length_position = get_buf_offset(buffer);
	pack32(0, buffer);

	if (!plugin_data || !plugin_data->data)
		return;

	xassert(pack_func);

	/* Pack plugin id and plugin data */
	start = get_buf_offset(buffer);
	pack32(plugin_data->plugin_id, buffer);
	pack_func(plugin_data->data, buffer, protocol_version);

	/*
	 * Pack the size of the data at the previously saved length position of
	 * the buffer.
	 */
	end = get_buf_offset(buffer);
	set_buf_offset(buffer, length_position);
	pack32(end - start, buffer);

	/* Reset offset to the end of data */
	set_buf_offset(buffer, end);
}

extern int dynamic_plugin_data_unpack(
	dynamic_plugin_data_t **plugin_data,
	dynamic_plugin_data_get_unpack_func get_unpack_func,
	buf_t *buffer,
	uint16_t protocol_version)
{
	uint32_t length = 0, end = 0;
	dynamic_plugin_data_unpack_func unpack_func = NULL;

	xassert(buffer);

	/* Get length of entire message */
	safe_unpack32(&length, buffer);

	if (remaining_buf(buffer) < length)
		return SLURM_ERROR;

	end = get_buf_offset(buffer) + length;

	if (!length || !plugin_data) {
		debug2("%s: skipping unpack of %u bytes", __func__, length);
		set_buf_offset(buffer, end);
		return SLURM_SUCCESS;
	}

	*plugin_data = xmalloc(sizeof(**plugin_data));

	/* Get plugin id */
	safe_unpack32(&(*plugin_data)->plugin_id, buffer);

	/* Find the correct unpack function using the plugin id */
	if (!(unpack_func = get_unpack_func((*plugin_data)->plugin_id))) {
		debug2("%s: skipping unpack of %u bytes", __func__, length);
		set_buf_offset(buffer, end);
		return SLURM_SUCCESS;
	}

	/* Get plugin data using the correct unpack function */
	if (unpack_func(&(*plugin_data)->data, buffer, protocol_version))
		goto unpack_error;

	if (get_buf_offset(buffer) > end) {
		error("%s: missing %u bytes for length of %u",
		      __func__, (get_buf_offset(buffer) - end), length);
		goto unpack_error;
	} else if (get_buf_offset(buffer) < end) {
		error("%s: unpacked %u bytes more than expected %u",
		      __func__, (end - get_buf_offset(buffer)), length);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	xfree(*plugin_data);
	return SLURM_ERROR;
}
