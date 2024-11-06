/*****************************************************************************\
 *  operations.h - definitions for handling http operations
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

#ifndef SLURMRESTD_OPERATIONS_H
#define SLURMRESTD_OPERATIONS_H

#include "src/interfaces/serializer.h"
#include "src/slurmrestd/http.h"
#include "src/slurmrestd/openapi.h"

extern serializer_flags_t yaml_flags;
extern serializer_flags_t json_flags;

/*
 * setup locks.
 * only call once!
 */
extern int init_operations(data_parser_t **parsers);
extern void destroy_operations(void);

/*
 * Bind callback handler for a given URL pattern.
 * Same rules as bind_operation_handler() but handles populating response and
 * tracking warnings and errors.
 *
 * IN op_path - operation path to bind
 * IN meta - meta info about plugin that owns callback or NULL
 * RET SLURM_SUCCESS or error
 */
extern int bind_operation_path(const openapi_path_binding_t *op_path,
			       const openapi_resp_meta_t *meta);

/*
 * Parses incoming requests and calls handlers.
 * expected to be called as on_http_request_t() by http.c.
 * RET SLURM_SUCCESS or error
 */
extern int operations_router(on_http_request_args_t *args);

/*
 * Retrieve db_conn for slurmdbd calls.
 * WARNING: Only valid inside of openapi_handler_t()
 * RET NULL on error or db_conn pointer
 *
 * Note: this is not implemented here but must be in the caller
 */
extern void *openapi_get_db_conn(void *ctxt);

#endif /* SLURMRESTD_OPERATIONS_H */
