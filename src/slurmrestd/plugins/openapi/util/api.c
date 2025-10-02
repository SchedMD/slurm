/*****************************************************************************\
 *  api.c - Slurm REST API openapi operations handlers
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

#include <stdint.h>

#include "src/common/log.h"

#include "src/slurmrestd/openapi.h"

#include "api.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Slurm OpenAPI util";
const char plugin_type[] = "openapi/util";
const uint32_t plugin_id = 112;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

const openapi_resp_meta_t plugin_meta = {
	.plugin = {
		.type = (char *) plugin_type,
		.name = (char *) plugin_name,
	},
	.slurm = {
		.version = {
			.major = SLURM_MAJOR,
			.micro = SLURM_MICRO,
			.minor = SLURM_MINOR,
		},
		.release = SLURM_VERSION_STRING,
	}
};

#define OP_FLAGS \
	(OP_BIND_DATA_PARSER | OP_BIND_OPENAPI_RESP_FMT | OP_BIND_NO_SLURMDBD)

const openapi_path_binding_t openapi_paths[] = { { 0 } };

extern void slurm_openapi_p_init(void)
{
	/* do nothing */
}

extern void slurm_openapi_p_fini(void)
{
	/* do nothing */
}

extern int slurm_openapi_p_get_paths(const openapi_path_binding_t **paths_ptr,
				     const openapi_resp_meta_t **meta_ptr)
{
	*paths_ptr = openapi_paths;
	*meta_ptr = &plugin_meta;
	return SLURM_SUCCESS;
}
