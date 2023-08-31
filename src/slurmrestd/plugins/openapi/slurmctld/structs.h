/*****************************************************************************\
 *  api.h - Slurm REST API openapi structs
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#ifndef OPENAPI_SLURMCTLD_STRUCTS
#define OPENAPI_SLURMCTLD_STRUCTS

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/interfaces/data_parser.h"
#include "src/slurmrestd/openapi.h"

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	job_array_resp_msg_t *results;
	char *job_id;
	char *step_id;
	char *job_submit_user_msg;
} job_post_response_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	submit_response_msg_t resp;
} job_submit_response_t;

typedef struct {
	char *script;
	job_desc_msg_t *job;
	list_t *jobs; /* list of job_desc_msg_t* */
} job_submit_request_t;

typedef struct {
	slurm_selected_step_t job_id;
} openapi_job_info_param_t;

typedef struct {
	uint16_t signal;
	uint16_t flags;
} openapi_job_info_delete_query_t;

typedef struct {
	time_t update_time;
	uint16_t show_flags;
} openapi_job_info_query_t;

typedef struct {
	char *node_name;
} openapi_node_param_t;

typedef struct {
	time_t update_time;
	uint16_t show_flags;
} openapi_nodes_query_t;

typedef struct {
	char *partition_name;
} openapi_partition_param_t;

typedef struct {
	time_t update_time;
	uint16_t show_flags;
} openapi_partitions_query_t;

typedef struct {
	char *reservation_name;
} openapi_reservation_param_t;

typedef struct {
	time_t update_time;
} openapi_reservation_query_t;

#endif
