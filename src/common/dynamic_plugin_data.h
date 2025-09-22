/*****************************************************************************\
 *  dynamic_plugin_data.h - Opaque data for plugins
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

#ifndef _COMMON_DYNAMIC_PLUGIN_DATA_H
#define _COMMON_DYNAMIC_PLUGIN_DATA_H

#include "src/common/pack.h"

typedef struct dynamic_plugin_data {
	void *data;
	uint32_t plugin_id;
} dynamic_plugin_data_t;

/*
 * Pack function pointer for dynamic data
 *
 * IN data - pointer to data to pack
 * IN buffer - pack data into this buffer
 * IN protocol_version - version used to pack
 */
typedef void (*dynamic_plugin_data_pack_func)(
	void *data,
	buf_t *buffer,
	uint16_t protocol_version);

/*
 * Unpack function pointer for dynamic data
 *
 * IN data - address of pointer to new unpacked data. Must be freed using
 *	a plugin interface free function
 * IN buffer - unpack data from this buffer
 * IN protocol_version - version used to unpack
 *
 * RET - SLURM_SUCCESS or error
 */
typedef int (*dynamic_plugin_data_unpack_func)(
	void **data,
	buf_t *buffer,
	uint16_t protocol_version);

/*
 * Get unpack pointer for dynamic data
 *
 * IN plugin id
 *
 * RET function pointer for plugin data unpack function
 */
typedef dynamic_plugin_data_unpack_func (*dynamic_plugin_data_get_unpack_func)(
	uint32_t plugin_id);

/*
 * Pack plugin dynamic data
 *
 * IN plugin_data - data to pack
 * IN pack_func - function for packing
 * IN buffer - pack data into this buffer
 * IN protocol_version - version used to pack
 */
extern void dynamic_plugin_data_pack(
	dynamic_plugin_data_t *plugin_data,
	dynamic_plugin_data_pack_func pack_func,
	buf_t *buffer,
	uint16_t protocol_version);

/*
 * Pack plugin dynamic data
 *
 * IN plugin_data - address of pointer to new unpacked dynamic plugin data
 * IN pack_func - function for packing
 * IN buffer - pack data into this buffer
 * IN protocol_version - version used to unpack
 *
 * RET - SLURM_SUCCESS or error
 */
extern int dynamic_plugin_data_unpack(
	dynamic_plugin_data_t **plugin_data,
	dynamic_plugin_data_get_unpack_func get_unpack_func,
	buf_t *buffer,
	uint16_t protocol_version);

#endif
