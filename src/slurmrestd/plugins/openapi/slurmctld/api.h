/*****************************************************************************\
 *  api.h - Slurm REST API openapi operations handlers
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

#ifndef OPENAPI_SLURMCTLD
#define OPENAPI_SLURMCTLD

#include "src/common/data.h"
#include "src/interfaces/data_parser.h"
#include "src/slurmrestd/openapi.h"

typedef openapi_ctxt_t ctxt_t;

#define resp_error(ctxt, error_code, source, why, ...) \
	openapi_resp_error(ctxt, error_code, source, why, ##__VA_ARGS__)
#define resp_warn(ctxt, source, why, ...) \
	openapi_resp_warn(ctxt, source, why, ##__VA_ARGS__)

/* ------------ handlers for user requests --------------- */

#define get_str_param(name, req, ctxt) \
	openapi_get_str_param(ctxt, req, name, __func__)

#define get_date_param(name, req, time_ptr, ctxt) \
	openapi_get_date_param(ctxt, req, name, &time_ptr, __func__)

/* ------------ declarations for each operation --------------- */

extern void init_op_diag(void);
extern void init_op_jobs(void);
extern void init_op_nodes(void);
extern void init_op_partitions(void);
extern void init_op_reservations(void);
extern void destroy_op_diag(void);
extern void destroy_op_jobs(void);
extern void destroy_op_nodes(void);
extern void destroy_op_partitions(void);
extern void destroy_op_reservations(void);

/* register handler against each parser */
extern void bind_handler(const char *str_path, openapi_ctxt_handler_t callback);

#endif
