/*****************************************************************************\
 *  api.c - Slurm REST API openapi operations handlers
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

#include "config.h"

#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/openapi.h"
#include "src/common/ref.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.36/api.h"

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
const char plugin_name[] = "REST v0.0.36";
const char plugin_type[] = "openapi/v0.0.36";
const uint32_t plugin_id = 100;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

decl_static_data(openapi_json);

extern data_t *populate_response_format(data_t *resp)
{
	data_t *plugin, *slurm, *slurmv, *meta;

	if (data_get_type(resp) != DATA_TYPE_NULL) {
		xassert(data_get_type(resp) == DATA_TYPE_DICT);
		return data_key_get(resp, "errors");
	}

	data_set_dict(resp);

	meta = data_set_dict(data_key_set(resp, "meta"));
	plugin = data_set_dict(data_key_set(meta, "plugin"));
	slurm = data_set_dict(data_key_set(meta, "Slurm"));
	slurmv = data_set_dict(data_key_set(slurm, "version"));

	data_set_string(data_key_set(slurm, "release"), SLURM_VERSION_STRING);
	(void) data_convert_type(data_set_string(data_key_set(slurmv, "major"),
						 SLURM_MAJOR),
				 DATA_TYPE_INT_64);
	(void) data_convert_type(data_set_string(data_key_set(slurmv, "micro"),
						 SLURM_MICRO),
				 DATA_TYPE_INT_64);
	(void) data_convert_type(data_set_string(data_key_set(slurmv, "minor"),
						 SLURM_MINOR),
				 DATA_TYPE_INT_64);

	data_set_string(data_key_set(plugin, "type"), plugin_type);
	data_set_string(data_key_set(plugin, "name"), plugin_name);

	return data_set_list(data_key_set(resp, "errors"));
}

extern data_t *slurm_openapi_p_get_specification(openapi_spec_flags_t *flags)
{
	data_t *spec = NULL;

	static_ref_json_to_data_t(spec, openapi_json);

	return spec;
}

extern void slurm_openapi_p_init(void)
{
	init_op_diag();
	init_op_jobs();
	init_op_nodes();
	init_op_partitions();
}

extern void slurm_openapi_p_fini(void)
{
	destroy_op_diag();
	destroy_op_jobs();
	destroy_op_nodes();
	destroy_op_partitions();
}
