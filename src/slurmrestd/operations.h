/*****************************************************************************\
 *  operations.h - definitions for handling http operations
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

#ifndef SLURMRESTD_OPERATIONS_H
#define SLURMRESTD_OPERATIONS_H

#include "src/common/data.h"
#include "src/interfaces/openapi.h"
#include "src/slurmrestd/http.h"
#include "src/slurmrestd/rest_auth.h"

extern openapi_t *openapi_state;

/*
 * setup locks.
 * only call once!
 */
extern int init_operations(void);
extern void destroy_operations(void);

/*
 * Bind callback handler for a given URL pattern.
 * Will query OpenAPI spec for description of path including variables
 *	Note that variables in the path are only partially support:
 *	supported: /users/{id}
 *	supported: /cars/{carId}/drivers/{driverId}
 *	not supported: /report.{format}
 *		complex variables in a single directory are not supported
 *		as we would need to make a regex to match them out or somehow
 *		otherwise force a match which would be slow
 *		there can only be 1 variable with no extra spaces as we
 *		only check the data type of the dir entry
 *
 *
 * IN path - url path to match
 * IN callback - handler function for callback
 * IN tag - arbitrary tag passed to handler when path matched
 * RET SLURM_SUCCESS or error
 */
extern int bind_operation_handler(const char *path, openapi_handler_t callback,
				  int tag);

/*
 * Unbind a given callback handler from all paths
 * WARNING: NOT YET IMPLEMENTED
 * IN path path to remove
 * RET SLURM_SUCCESS or error
 */
extern int unbind_operation_handler(openapi_handler_t callback);

/*
 * Parses incoming requests and calls handlers.
 * expected to be called as on_http_request_t() by http.c.
 * RET SLURM_SUCCESS or error
 */
extern int operations_router(on_http_request_args_t *args);

/*
 * Retrieves db_conn from auth context handed by operation_handler_t
 *
 * RET non-null pointer or NULL on failure
 */
extern void *get_operation_db_conn(rest_auth_context_t *auth);

/*
 * Retrieve db_conn for slurmdbd calls.
 * WARNING: Only valid inside of openapi_handler_t()
 * RET NULL on error or db_conn pointer
 *
 * Note: this is not implemented here but must be in the caller
 */
extern void *openapi_get_db_conn(void *ctxt);

#endif /* SLURMRESTD_OPERATIONS_H */
