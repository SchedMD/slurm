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

#ifndef SLURMRESTD_OPENAPI_V0038
#define SLURMRESTD_OPENAPI_V0038

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/data.h"

extern int get_date_param(data_t *query, const char *param, time_t *time);

/*
 * Fill out boilerplate for every data response
 * RET ptr to errors dict
 */
extern data_t *populate_response_format(data_t *resp);

/*
 * Add a response error to errors
 * IN errors - data list to append a new error
 * IN why - description of error or NULL
 * IN error_code - Error number
 * IN source - Where the error was generated
 * RET value of error_code
 */
extern int resp_error(data_t *errors, int error_code, const char *source,
		      const char *why, ...)
	__attribute__((format(printf, 4, 5)));

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

#endif
