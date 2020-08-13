/*****************************************************************************\
 *  openapi.h - definitions for handling openapi
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

/*
 * Based on OpenAPI 3.0.2 spec (OAS):
 * 	https://github.com/OAI/OpenAPI-Specification/blob/master/versions/3.0.2.md
 */

#ifndef SLURMRESTD_OPENAPI_H
#define SLURMRESTD_OPENAPI_H

#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/plugin.h"
#include "src/slurmrestd/http.h"

/*
 * Register a given unique tag against a path.
 *
 * IN path - path to assign to given tag
 * RET -1 on error or >0 tag value for path.
 *
 * Can safely be called multiple times for same path.
 */
extern int register_path_tag(const char *path);

/*
 * Unregister a given unique tag against a path.
 *
 * IN tag - path tag to remove
 */
extern void unregister_path_tag(int tag);

/*
 * Find tag assigned to given path
 * IN path - split up path to match
 * IN/OUT params - on match, will populate any OAS parameters in path.
 * 	params must be DATA_TYPE_DICT.
 *
 * IN method - HTTP method to match
 * RET -1 if tag not found or tag given to register_path_tag()
 */
extern int find_path_tag(const data_t *path, data_t *params,
			 http_request_method_t method);

/*
 * Init the OAS data structs.
 * only call once!
 * IN plugin_handles - array of plugin handles loaded
 * IN plugin_count - number of plugins loaded
 * RET SLURM_SUCCESS or error
 */
extern int init_openapi(const plugin_handle_t *plugin_handles,
			const size_t plugin_count);

/*
 * Free openapi
 */
extern void destroy_openapi(void);

#endif /* SLURMRESTD_OPENAPI_H */
