/*****************************************************************************\
 *  parse.h - Slurm REST API openapi operations handlers
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

#ifndef SLURMRESTD_OPENAPI_DB_PARSE_V0037
#define SLURMRESTD_OPENAPI_DB_PARSE_V0037

#include "config.h"
#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/data.h"
#include "src/common/list.h"

#include "src/slurmrestd/operations.h"

typedef enum {
	PARSE_INVALID = 0,
	PARSE_ACCOUNT_LIST, /* list of slurmdb_accounting_rec_t * to id account names */
	PARSE_ACCOUNT, /* slurmdb_account_rec_t* */
	PARSE_ADMIN_LVL, /* uint16_t (placeholder for slurmdb_admin_level_t) */
	PARSE_ASSOC_ID, /* slurmdb_assoc_usage_t */
	PARSE_ASSOC_LIST, /* list of slurmdb_assoc_rec_t* */
	PARSE_ASSOC_SHORT_LIST, /* list of slurmdb_assoc_rec_t* only for id */
	PARSE_ASSOC_SHORT, /* slurmdb_assoc_rec_t* (for id only) */
	PARSE_ASSOC, /* slurmdb_assoc_rec_t* */
	PARSE_ASSOC_USAGE, /* slurmdb_assoc_usage_t */
	PARSE_CLASSIFICATION_TYPE, /* slurmdb_classification_type_t */
	PARSE_CLUSTER_ACCT_REC_LIST, /* list of slurmdb_cluster_accounting_rec_t* */
	PARSE_CLUSTER_ACCT_REC, /* slurmdb_cluster_accounting_rec_t* */
	PARSE_CLUSTER_CLASSIFICATION, /* uint16_t joined with slurmdb_classification_type_t */
	PARSE_CLUSTER_REC, /* slurmdb_cluster_rec_t* */
	PARSE_COORD_LIST, /* List of slurmdb_coord_rec_t * */
	PARSE_COORD, /* slurmdb_coord_rec_t* */
	PARSE_FLAGS, /* must use with parser_enum_t array */
	PARSE_FLOAT128, /* long double */
	PARSE_FLOAT64, /* double */
	PARSE_GROUP_ID, /* Group from numeric GID <-> gid_t */
	PARSE_INT64, /* int64_t */
	PARSE_JOB_EXIT_CODE, /* int32_t */
	PARSE_JOB_REASON, /* uint32_t <-> enum job_state_reason */
	PARSE_JOB, /* slurmdb_job_rec_t* */
	PARSE_JOB_STATE, /* uint32_t <-> JOB_STATE_FLAGS */
	PARSE_JOB_STEP, /* slurmdb_step_rec_t* */
	PARSE_JOB_STEPS, /* slurmdb_job_rec_t->steps -> list of slurmdb_step_rec_t *'s*/
	PARSE_QOS_ID, /* uint32_t of QOS id */
	PARSE_QOS_PREEMPT_LIST, /* slurmdb_qos_rec_t->preempt_bitstr & preempt_list */
	PARSE_QOS, /* slurmdb_qos_rec_t* */
	PARSE_QOS_STR_LIST, /* List of char* of QOS names */
	PARSE_RPC_ID, /* slurmdbd_msg_type_t */
	PARSE_SELECT_PLUGIN_ID, /* int (SELECT_PLUGIN_*) -> string */
	PARSE_STATS_REC_ARRAY, /* array of slurmdb_stats_rec_t* */
	PARSE_STATS_REC, /* slurmdb_stats_rec_t* */
	PARSE_STATS_RPC_LIST, /* list of slurmdb_rpc_obj_t* */
	PARSE_STATS_USER_LIST, /* list of slurmdb_rpc_obj_t* */
	PARSE_STEP_CPUFREQ_GOV, /* slurmdb_step_rec_t.req_cpufreq_gov (uint32_t) of CPU_FREQ_* flags */
	PARSE_STEP_ID, /* uint32_t of job step id */
	PARSE_STRING, /* char */
	PARSE_TASK_DISTRIBUTION, /* uint32_t <-> task_dist_states_t */
	PARSE_TRES_LIST, /* List of slurmdb_tres_rec_t* combined into a TRES string */
	PARSE_TRES, /* slurmdb_tres_rec_t* */
	PARSE_UINT16, /* uint16_t */
	PARSE_UINT32, /* uint32_t */
	PARSE_UINT64, /* uint64_t */
	PARSE_USER_ID, /* User from numeric UID */
	PARSE_USER, /* slurmdb_user_rec*  */
	PARSE_WCKEY, /* slurmdb_wckey_rec_t*  */
	PARSE_WCKEY_TAG, /* uint32_t - * prefix denotes default */
} parser_type_t;

typedef struct {
	/* required for PARSE_ASSOC_LIST */
	rest_auth_context_t *auth;
	/* required for PARSE_TRES_COUNT */
	List g_tres_list;
	/* required for PARSE_QOS_ID */
	List g_qos_list;
	List g_assoc_list;
} parser_env_t;

extern int parse(parser_type_t type, void *obj, data_t *src, data_t *errors,
		 const parser_env_t *penv);
extern int dump(parser_type_t type, void *obj, data_t *dst,
		const parser_env_t *penv);

#endif
