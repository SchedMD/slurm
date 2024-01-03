/*****************************************************************************\
 *  data_t parser plugin interface
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
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

#ifndef _INTERFACES_DATA_PARSER_H
#define _INTERFACES_DATA_PARSER_H

#include "src/common/data.h"
#include "src/common/openapi.h"

/*
 * Enumeration of all parsers that data_parser plugins will handle.
 * Warning: Not all data_parsers implement every enum
 * Warning: Never hard code the value of an enum as assigned values may change
 *	any time.
 *
 * Each enum must correspond to exactly 1 type:
 * 	'char *' and 'char' and 'unsigned char' and 'unsigned char *' would be
 * 	considered 4 different parsers.
 *
 * When an enum has a _PTR suffix, then it must be a pointer to the same type
 * without the _PTR suffix.
 *
 */
typedef enum {
	DATA_PARSER_TYPE_INVALID = 0,
	DATA_PARSER_BITSTR, /* bitstr_t */
	DATA_PARSER_STRING, /* char* */
	DATA_PARSER_UINT16, /* uint16_t */
	DATA_PARSER_UINT16_NO_VAL, /* uint16_t - may be NO_VAL16 or INFINITE16 */
	DATA_PARSER_UINT16_NO_VAL_STRUCT, /* UINT16_NO_VAL_t */
	DATA_PARSER_UINT16_NO_VAL_STRUCT_PTR, /* UINT16_NO_VAL_t* */
	DATA_PARSER_UINT32, /* uint32_t */
	DATA_PARSER_UINT32_NO_VAL, /* uint32_t - maybe NO_VAL or INFINTE*/
	DATA_PARSER_UINT32_NO_VAL_STRUCT, /* UINT32_NO_VAL_t */
	DATA_PARSER_UINT32_NO_VAL_STRUCT_PTR, /* UINT32_NO_VAL_t* */
	DATA_PARSER_UINT64, /* uint64_t */
	DATA_PARSER_UINT64_NO_VAL, /* uint64_t - NO_VAL64 or INFINTE64 */
	DATA_PARSER_UINT64_NO_VAL_STRUCT, /* UINT64_NO_VAL_t */
	DATA_PARSER_UINT64_NO_VAL_STRUCT_PTR, /* UINT64_NO_VAL_t* */
	DATA_PARSER_INT32, /* int32_t */
	DATA_PARSER_INT64, /* int64_t */
	DATA_PARSER_INT64_NO_VAL, /* int64_t - NO_VAL64 or INFINTE64 */
	DATA_PARSER_INT64_NO_VAL_STRUCT, /* INT64_NO_VAL_t */
	DATA_PARSER_INT64_NO_VAL_STRUCT_PTR, /* INT64_NO_VAL_t* */
	DATA_PARSER_FLOAT128, /* long double */
	DATA_PARSER_FLOAT64, /* double */
	DATA_PARSER_FLOAT64_NO_VAL, /* double - may be NO_VAL of INFINITE */
	DATA_PARSER_FLOAT64_NO_VAL_STRUCT, /* FLOAT64_NO_VAL_t */
	DATA_PARSER_FLOAT64_NO_VAL_STRUCT_PTR, /* FLOAT64_NO_VAL_t* */
	DATA_PARSER_BOOL, /* uint8_t */
	DATA_PARSER_BOOL16, /* uint16_t */
	DATA_PARSER_BOOL16_NO_VAL, /* uint16_t - false if NO_VAL16 */
	DATA_PARSER_CSV_STRING, /* char * - comma delimited list stored as a string */
	DATA_PARSER_CSV_STRING_LIST, /* list_t of char* - comma delimited list stored as a list_t* */
	DATA_PARSER_OPENAPI_ACCOUNTS_ADD_COND_RESP, /* openapi_resp_accounts_add_cond_t */
	DATA_PARSER_OPENAPI_ACCOUNTS_ADD_COND_RESP_PTR, /* openapi_resp_accounts_add_cond_t* */
	DATA_PARSER_OPENAPI_ACCOUNTS_ADD_COND_RESP_STR, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_ACCOUNTS_ADD_COND_RESP_STR_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_ACCOUNTS_REMOVED_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_ACCOUNTS_REMOVED_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_ACCOUNTS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_ACCOUNTS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_ACCOUNT_PARAM, /* openapi_account_param_t */
	DATA_PARSER_OPENAPI_ACCOUNT_PARAM_PTR, /* openapi_account_param_t* */
	DATA_PARSER_OPENAPI_ACCOUNT_QUERY, /* openapi_account_query_t */
	DATA_PARSER_OPENAPI_ACCOUNT_QUERY_PTR, /* openapi_account_query_t* */
	DATA_PARSER_ACCOUNT_CONDITION, /* slurmdb_account_cond_t */
	DATA_PARSER_ACCOUNT_CONDITION_PTR, /* slurmdb_account_cond_t* */
	DATA_PARSER_ACCOUNT_LIST, /* list of slurmdb_account_rec_t* */
	DATA_PARSER_ACCOUNT, /* slurmdb_account_rec_t */
	DATA_PARSER_ACCOUNT_PTR, /* slurmdb_account_rec_t* */
	DATA_PARSER_ACCOUNT_SHORT, /* slurmdb_account_rec_t subset */
	DATA_PARSER_ACCOUNT_SHORT_PTR, /* slurmdb_account_rec_t* */
	DATA_PARSER_ACCOUNT_FLAGS, /* slurmdb_account_rec_t->flags & SLURMDB_ACCT_FLAG_* */
	DATA_PARSER_ACCOUNTING_LIST, /* list of slurmdb_accounting_rec_t* */
	DATA_PARSER_ACCOUNTING, /* slurmdb_accounting_rec_t */
	DATA_PARSER_ACCOUNTING_PTR, /* slurmdb_accounting_rec_t* */
	DATA_PARSER_ACCOUNTS_ADD_COND, /* slurmdb_add_assoc_cond_t */
	DATA_PARSER_ACCOUNTS_ADD_COND_PTR, /* slurmdb_add_assoc_cond_t* */
	DATA_PARSER_ADMIN_LVL, /* uint16_t (placeholder for slurmdb_admin_level_t) */
	DATA_PARSER_ASSOC_ID, /* uint32_t - Assumes local cluster which may be wrong */
	DATA_PARSER_ASSOC_ID_STRING, /* char * of assoc id */
	DATA_PARSER_ASSOC_ID_STRING_CSV_LIST, /* list of char* */
	DATA_PARSER_ASSOC_LIST, /* list of slurmdb_assoc_rec_t* */
	DATA_PARSER_OPENAPI_ASSOCS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_ASSOCS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_ASSOC_SHORT_LIST, /* list of slurmdb_assoc_rec_t* only for id */
	DATA_PARSER_ASSOC_SHORT, /* slurmdb_assoc_rec_t (for id only) */
	DATA_PARSER_ASSOC_SHORT_PTR, /* slurmdb_assoc_rec_t* (for id only) */
	DATA_PARSER_ASSOC, /* slurmdb_assoc_rec_t */
	DATA_PARSER_ASSOC_PTR, /* slurmdb_assoc_rec_t* */
	DATA_PARSER_ASSOC_FLAGS, /* slurmdb_assoc_rec_t->flags & ASSOC_FLAG_* */
	DATA_PARSER_ASSOC_USAGE, /* slurmdb_assoc_usage_t */
	DATA_PARSER_ASSOC_USAGE_PTR, /* slurmdb_assoc_usage_t* */
	DATA_PARSER_ASSOC_REC_SET, /* slurmdb_assoc_rec_t */
	DATA_PARSER_ASSOC_REC_SET_PTR, /* slurmdb_assoc_rec_t* */
	DATA_PARSER_OPENAPI_ASSOCS_REMOVED_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_ASSOCS_REMOVED_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_ASSOC_CONDITION, /* slurmdb_assoc_cond_t */
	DATA_PARSER_ASSOC_CONDITION_PTR, /* slurmdb_assoc_cond_t* */
	DATA_PARSER_CLASSIFICATION_TYPE, /* slurmdb_classification_type_t */
	DATA_PARSER_CLUSTER_ACCT_REC_LIST, /* list of slurmdb_cluster_accounting_rec_t* */
	DATA_PARSER_CLUSTER_ACCT_REC, /* slurmdb_cluster_accounting_rec_t */
	DATA_PARSER_CLUSTER_ACCT_REC_PTR, /* slurmdb_cluster_accounting_rec_t* */
	DATA_PARSER_CLUSTER_CLASSIFICATION, /* uint16_t joined with slurmdb_classification_type_t */
	DATA_PARSER_CLUSTER_REC_LIST, /* list of slurmdb_cluster_rec_t */
	DATA_PARSER_CLUSTER_REC, /* slurmdb_cluster_rec_t */
	DATA_PARSER_CLUSTER_REC_PTR, /* slurmdb_cluster_rec_t* */
	DATA_PARSER_CLUSTER_REC_FLAGS, /* slurmdb_cluster_rec_t->flags & CLUSTER_FLAG_* */
	DATA_PARSER_OPENAPI_CLUSTERS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_CLUSTERS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_CLUSTERS_REMOVED_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_CLUSTERS_REMOVED_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_CLUSTER_PARAM, /* openapi_cluster_param_t */
	DATA_PARSER_OPENAPI_CLUSTER_PARAM_PTR, /* openapi_cluster_param_t* */
	DATA_PARSER_CLUSTER_CONDITION, /* slurmdb_cluster_cond_t */
	DATA_PARSER_CLUSTER_CONDITION_PTR, /* slurmdb_cluster_cond_t* */
	DATA_PARSER_COORD_LIST, /* List of slurmdb_coord_rec_t* */
	DATA_PARSER_COORD, /* slurmdb_coord_rec_t */
	DATA_PARSER_COORD_PTR, /* slurmdb_coord_rec_t* */
	DATA_PARSER_CPU_FREQ_FLAGS, /* uint32_t & CPU_FREQ_* */
	DATA_PARSER_GROUP_ID, /* Group from numeric GID <-> gid_t */
	DATA_PARSER_GROUP_ID_STRING, /* char * - group id string */
	DATA_PARSER_GROUP_ID_STRING_LIST, /* list_t of string group id */
	DATA_PARSER_GROUP_NAME, /* Group from string group name <-> gid_t */
	DATA_PARSER_INSTANCE, /* slurmdb_instance_rec_t */
	DATA_PARSER_INSTANCE_PTR, /* slurmdb_instance_rec_t* */
	DATA_PARSER_INSTANCE_CONDITION, /* slurmdb_instance_cond_t */
	DATA_PARSER_INSTANCE_CONDITION_PTR, /* slurmdb_instance_cond_t* */
	DATA_PARSER_INSTANCE_LIST, /* list of slurmdb_instance_rec_t */
	DATA_PARSER_OPENAPI_INSTANCES_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_INSTANCES_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_JOB_EXIT_CODE, /* int32_t */
	DATA_PARSER_JOB_REASON, /* uint32_t <-> enum job_state_reason */
	DATA_PARSER_JOB_LIST, /* list of slurmdb_job_rec_t* */
	DATA_PARSER_JOB, /* slurmdb_job_rec_t */
	DATA_PARSER_JOB_PTR, /* slurmdb_job_rec_t* */
	DATA_PARSER_JOB_ASSOC_ID, /* slurmdb_job_rec_t->associd,cluster */
	DATA_PARSER_JOB_CONDITION, /* slurmdb_job_cond_t */
	DATA_PARSER_JOB_CONDITION_FLAGS, /* uint32_t - JOBCOND_FLAG_* */
	DATA_PARSER_JOB_CONDITION_DB_FLAGS, /* uint32_t - SLURMDB_JOB_FLAG_* */
	DATA_PARSER_JOB_CONDITION_SUBMIT_TIME, /* slurmdb_job_cond_t->usage_start&flags */
	DATA_PARSER_JOB_CONDITION_PTR, /* slurmdb_job_cond_t* */
	DATA_PARSER_OPENAPI_SLURMDBD_JOBS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_SLURMDBD_JOBS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_SLURMDBD_JOB_PARAM, /* openapi_job_param_t */
	DATA_PARSER_OPENAPI_SLURMDBD_JOB_PARAM_PTR, /* openapi_job_param_t* */
	DATA_PARSER_SLURMDB_JOB_FLAGS, /* slurmdb_job_rec_t->flags & SLURMDB_JOB_* */
	DATA_PARSER_JOB_STATE, /* uint32_t <-> JOB_* & JOB_STATE_FLAGS */
	DATA_PARSER_JOB_STATE_ID_STRING, /* char* <-> JOB_* & JOB_STATE_FLAGS - as stringified integer */
	DATA_PARSER_JOB_STATE_ID_STRING_LIST, /* list_t of char* */
	DATA_PARSER_OPENAPI_STEP_INFO_MSG, /* openapi_resp_job_step_info_msg_t */
	DATA_PARSER_OPENAPI_STEP_INFO_MSG_PTR, /* openapi_resp_job_step_info_msg_t* */
	DATA_PARSER_STEP_INFO_MSG, /* job_step_info_response_msg_t */
	DATA_PARSER_STEP_INFO_MSG_PTR, /* job_step_info_response_msg_t* */
	DATA_PARSER_STEP_INFO, /* job_step_info_t */
	DATA_PARSER_STEP_INFO_PTR, /* job_step_info_t* */
	DATA_PARSER_STEP_INFO_ARRAY, /* job_step_info_t* */
	DATA_PARSER_STEP, /* slurmdb_step_rec_t */
	DATA_PARSER_STEP_PTR, /* slurmdb_step_rec_t* */
	DATA_PARSER_STEP_LIST, /* List of slurmdb_step_rec_t* */
	DATA_PARSER_STEP_NODES, /* slurmdb_step_rec_t->nodes */
	DATA_PARSER_STEP_TRES_REQ_MAX, /* slurmdb_step_rec_t->tres_usage_in_max(|_nodeid|taskid) */
	DATA_PARSER_STEP_TRES_REQ_MIN, /* slurmdb_step_rec_t->tres_usage_in_min(|_nodeid|taskid) */
	DATA_PARSER_STEP_TRES_USAGE_MAX, /* slurmdb_step_rec_t->tres_usage_out_in_max(|_nodeid|taskid) */
	DATA_PARSER_STEP_TRES_USAGE_MIN, /* slurmdb_step_rec_t->tres_usage_out_in_min(|_nodeid|taskid) */
	DATA_PARSER_STEP_NAMES, /* uint32_t <-> SLURM_EXTERN_CONT,SLURM_BATCH_SCRIPT,SLURM_PENDING_STEP,SLURM_INTERACTIVE_STEP */
	DATA_PARSER_JOB_USER, /* user/uid from slurmdb_job_rec_t* */
	DATA_PARSER_NEED_PREREQS_FLAGS, /* need_t */
	DATA_PARSER_QOS_ID, /* uint32_t of QOS->name or stringified QOS->id if name unresolvable */
	DATA_PARSER_QOS_ID_LIST, /* List of char* of QOS ids */
	DATA_PARSER_QOS_STRING_ID_LIST, /* List of char* of QOS ids */
	DATA_PARSER_QOS_ID_STRING_CSV_LIST, /* List of char* of QOS ids */
	DATA_PARSER_QOS_ID_STRING, /* char* of QOS id */
	DATA_PARSER_QOS_NAME, /* char * of QOS name */
	DATA_PARSER_QOS_NAME_LIST, /* List of char* of QOS names */
	DATA_PARSER_QOS_NAME_CSV_LIST, /* List of char* of QOS names */
	DATA_PARSER_QOS_PREEMPT_LIST, /* slurmdb_qos_rec_t->preempt_bitstr & preempt_list */
	DATA_PARSER_QOS, /* slurmdb_qos_rec_t */
	DATA_PARSER_QOS_PTR, /* slurmdb_qos_rec_t* */
	DATA_PARSER_QOS_LIST, /* list of slurmdb_qos_rec_t* */
	DATA_PARSER_QOS_FLAGS, /* slurmdb_qos_rec_t->flags & QOS_FLAG_* */
	DATA_PARSER_QOS_PREEMPT_MODES, /* slurmdb_qos_rec_t->preempt_mode & QOS_FLAG_* */
	DATA_PARSER_QOS_CONDITION, /* slurmdb_qos_cond_t */
	DATA_PARSER_QOS_CONDITION_PTR, /* slurmdb_qos_cond_t* */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_REMOVED_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_REMOVED_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_PARAM, /* openapi_qos_param_t */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_PARAM_PTR, /* openapi_qos_param_t* */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_QUERY, /* openapi_qos_query_t */
	DATA_PARSER_OPENAPI_SLURMDBD_QOS_QUERY_PTR, /* openapi_qos_query_t* */
	DATA_PARSER_RPC_ID, /* slurm_msg_type_t - uint16_t */
	DATA_PARSER_SLURMDB_RPC_ID, /* slurmdbd_msg_type_t */
	DATA_PARSER_SELECT_PLUGIN_ID, /* DEPRECATED v41: int (SELECT_PLUGIN_*) -> string */
	DATA_PARSER_STATS_REC_PTR, /* slurmdb_stats_rec_t* */
	DATA_PARSER_STATS_REC, /* slurmdb_stats_rec_t */
	DATA_PARSER_OPENAPI_SLURMDBD_STATS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_SLURMDBD_STATS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_STATS_RPC_LIST, /* list of slurmdb_rpc_obj_t* */
	DATA_PARSER_STATS_RPC, /* slurmdb_rpc_obj_t */
	DATA_PARSER_STATS_RPC_PTR, /* slurmdb_rpc_obj_t* */
	DATA_PARSER_STATS_USER_LIST, /* list of slurmdb_rpc_obj_t* */
	DATA_PARSER_STATS_USER, /* slurmdb_rpc_obj_t */
	DATA_PARSER_STATS_USER_PTR, /* slurmdb_rpc_obj_t* */
	DATA_PARSER_ROLLUP_STATS, /* slurmdb_rollup_stats_t */
	DATA_PARSER_ROLLUP_STATS_PTR, /* slurmdb_rollup_stats_t* */
	DATA_PARSER_STEP_CPUFREQ_GOV, /* slurmdb_step_rec_t.req_cpufreq_gov (uint32_t) of CPU_FREQ_* flags */
	DATA_PARSER_SLURM_STEP_ID, /* slurm_step_id_t */
	DATA_PARSER_SLURM_STEP_ID_PTR, /* slurm_step_id_t* */
	DATA_PARSER_SLURM_STEP_ID_STRING, /* slurm_step_id_t -> SELECTED_STEP */
	DATA_PARSER_SLURM_STEP_ID_STRING_PTR, /* slurm_step_id_t* -> SLURM_STEP_ID_STRING */
	DATA_PARSER_STEP_ID, /* uint32_t of job step id */
	DATA_PARSER_STEP_ID_PTR, /* uint32_t* of job step id */
	DATA_PARSER_TASK_DISTRIBUTION, /* uint32_t <-> task_dist_states_t */
	DATA_PARSER_TRES_STR, /* List of slurmdb_tres_rec_t* combined into a TRES string with TRES type/name instead of ID */
	DATA_PARSER_TRES_ID_STR, /* List of slurmdb_tres_rec_t* combined into a TRES string with TRES id# instead of type/name */
	DATA_PARSER_TRES_LIST, /* List of slurmdb_tres_rec_t* */
	DATA_PARSER_TRES, /* slurmdb_tres_rec_t */
	DATA_PARSER_TRES_PTR, /* slurmdb_tres_rec_t* */
	DATA_PARSER_OPENAPI_TRES_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_TRES_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_TRES_NCT, /* slurmdb_tres_nct_rec_t */
	DATA_PARSER_TRES_NCT_PTR, /* slurmdb_tres_nct_rec_t* */
	DATA_PARSER_USER_ID, /* User from numeric UID */
	DATA_PARSER_USER_ID_STRING, /* char * - user id string */
	DATA_PARSER_USER_ID_STRING_LIST, /* list_t of string user id */
	DATA_PARSER_USER_SHORT, /* slurmdb_user_rec_t subset */
	DATA_PARSER_USER_SHORT_PTR, /* slurmdb_user_rec_t* */
	DATA_PARSER_USER, /* slurmdb_user_rec_t */
	DATA_PARSER_USER_PTR, /* slurmdb_user_rec_t* */
	DATA_PARSER_USER_LIST, /* List of slurmdb_user_rec_t*  */
	DATA_PARSER_USER_FLAGS, /* slurmdb_user_rec_t->parser_user_flags & SLURMDB_USER_FLAG_* */
	DATA_PARSER_USER_CONDITION, /* slurmdb_user_cond_t */
	DATA_PARSER_USER_CONDITION_PTR, /* slurmdb_user_cond_t* */
	DATA_PARSER_USERS_ADD_COND, /* slurmdb_add_assoc_cond_t subset */
	DATA_PARSER_USERS_ADD_COND_PTR, /* slurmdb_add_assoc_cond_t* */
	DATA_PARSER_OPENAPI_USERS_ADD_COND_RESP, /* openapi_resp_users_add_cond_t */
	DATA_PARSER_OPENAPI_USERS_ADD_COND_RESP_PTR, /* openapi_resp_users_add_cond_t* */
	DATA_PARSER_OPENAPI_USERS_ADD_COND_RESP_STR, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_USERS_ADD_COND_RESP_STR_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_USERS_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_USERS_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_USERS_REMOVED_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_USERS_REMOVED_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_WCKEY, /* slurmdb_wckey_rec_t */
	DATA_PARSER_WCKEY_PTR, /* slurmdb_wckey_rec_t* */
	DATA_PARSER_WCKEY_LIST, /* List of slurmdb_wckey_rec_t* */
	DATA_PARSER_WCKEY_FLAGS, /* slurmdb_wckey_rec_t->flags & SLURMDB_WCKEY_FLAG_* */
	DATA_PARSER_WCKEY_TAG, /* uint32_t - * prefix denotes default */
	DATA_PARSER_WCKEY_TAG_STRUCT, /* WCKEY_TAG_STRUCT_t */
	DATA_PARSER_WCKEY_TAG_STRUCT_PTR, /* WCKEY_TAG_STRUCT_t* */
	DATA_PARSER_WCKEY_TAG_FLAGS, /* WCKEY_TAG_FLAGS_t */
	DATA_PARSER_OPENAPI_WCKEY_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_WCKEY_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_WCKEY_REMOVED_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_WCKEY_REMOVED_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_OPENAPI_WCKEY_PARAM, /* openapi_wckey_param_t */
	DATA_PARSER_OPENAPI_WCKEY_PARAM_PTR, /* openapi_wckey_param_t* */
	DATA_PARSER_WCKEY_CONDITION, /* slurmdb_wckey_cond_t */
	DATA_PARSER_WCKEY_CONDITION_PTR, /* slurmdb_wckey_cond_t* */
	DATA_PARSER_SINFO_DATA, /* sinfo_data_t */
	DATA_PARSER_SINFO_DATA_PTR, /* sinfo_data_t* */
	DATA_PARSER_SINFO_DATA_LIST, /* list of sinfo_data_t* */
	DATA_PARSER_OPENAPI_SINFO_RESP, /* list of sinfo_data_t */
	DATA_PARSER_OPENAPI_SINFO_RESP_PTR, /* list of sinfo_data_t* */
	DATA_PARSER_OPENAPI_DIAG_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_DIAG_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_STATS_MSG, /* stats_info_response_msg_t */
	DATA_PARSER_STATS_MSG_PTR, /* stats_info_response_msg_t* */
	DATA_PARSER_STATS_MSG_CYCLE_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_CYCLE_MEAN_DEPTH, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_CYCLE_PER_MIN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_SCHEDULE_EXIT, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_CYCLE_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_DEPTH_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_DEPTH_MEAN_TRY, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_QUEUE_LEN_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_TABLE_SIZE_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_ACTIVE, /* stats_info_response_msg_t-> computed bool */
	DATA_PARSER_STATS_MSG_BF_EXIT, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_RPC_TYPE, /* STATS_MSG_RPC_TYPE_t */
	DATA_PARSER_STATS_MSG_RPC_TYPE_PTR, /* STATS_MSG_RPC_TYPE_t* */
	DATA_PARSER_STATS_MSG_RPCS_BY_TYPE, /* stats_info_response_msg_t-> computed bool */
	DATA_PARSER_STATS_MSG_RPC_USER, /* STATS_MSG_RPC_USER_t */
	DATA_PARSER_STATS_MSG_RPC_USER_PTR, /* STATS_MSG_RPC_USER_t* */
	DATA_PARSER_STATS_MSG_RPCS_BY_USER, /* stats_info_response_msg_t-> computed bool */
	DATA_PARSER_STATS_MSG_RPC_QUEUE, /* STATS_MSG_RPC_QUEUE_t */
	DATA_PARSER_STATS_MSG_RPC_QUEUE_PTR, /* STATS_MSG_RPC_QUEUE_t* */
	DATA_PARSER_STATS_MSG_RPCS_QUEUE, /* stats_info_response_msg_t-> computed */
	DATA_PARSER_STATS_MSG_RPC_DUMP, /* STATS_MSG_RPC_DUMP_t */
	DATA_PARSER_STATS_MSG_RPC_DUMP_PTR, /* STATS_MSG_RPC_DUMP_t* */
	DATA_PARSER_STATS_MSG_RPCS_DUMP, /* stats_info_response_msg_t-> computed */
	DATA_PARSER_BF_EXIT_FIELDS, /* bf_exit_fields_t */
	DATA_PARSER_BF_EXIT_FIELDS_PTR, /* bf_exit_fields_t* */
	DATA_PARSER_SCHEDULE_EXIT_FIELDS, /* schedule_exit_fields_t */
	DATA_PARSER_SCHEDULE_EXIT_FIELDS_PTR, /* schedule_exit_fields_t* */
	DATA_PARSER_CONTROLLER_PING, /* controller_ping_t */
	DATA_PARSER_CONTROLLER_PING_PTR, /* controller_ping_t* */
	DATA_PARSER_CONTROLLER_PING_ARRAY, /* controller_ping_t (NULL terminated array) */
	DATA_PARSER_OPENAPI_PING_ARRAY_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_PING_ARRAY_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_CONTROLLER_PING_MODE, /* char * - verbose controller mode */
	DATA_PARSER_CONTROLLER_PING_RESULT, /* bool - "UP" or "DOWN" */
	DATA_PARSER_NODE, /* node_info_t */
	DATA_PARSER_NODE_PTR, /* node_info_t* */
	DATA_PARSER_NODE_ARRAY, /* node_info_t** (NULL terminated) */
	DATA_PARSER_NODES, /* node_info_msg_t */
	DATA_PARSER_NODES_PTR, /* node_info_msg_t* */
	DATA_PARSER_OPENAPI_NODES_RESP, /* openapi_resp_node_info_msg_t */
	DATA_PARSER_OPENAPI_NODES_RESP_PTR, /* openapi_resp_node_info_msg_t* */
	DATA_PARSER_OPENAPI_NODE_PARAM, /* openapi_node_param_t */
	DATA_PARSER_OPENAPI_NODE_PARAM_PTR, /* openapi_node_param_t* */
	DATA_PARSER_OPENAPI_NODES_QUERY, /* openapi_nodes_query_t */
	DATA_PARSER_OPENAPI_NODES_QUERY_PTR, /* openapi_nodes_query_t* */
	DATA_PARSER_NODE_STATES, /* uint32_t & NODE_STATE_* */
	DATA_PARSER_NODE_STATES_NO_VAL, /* uint32_t & NODE_STATE_* or NO_VAL */
	DATA_PARSER_NODE_SELECT_ALLOC_MEMORY, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_ALLOC_CPUS, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_ALLOC_IDLE_CPUS, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_TRES_USED, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_TRES_WEIGHTED, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_UPDATE_NODE_MSG, /* update_node_msg_t */
	DATA_PARSER_UPDATE_NODE_MSG_PTR, /* update_node_msg_t* */
	DATA_PARSER_OPENAPI_LICENSES_RESP, /* openapi_resp_license_info_msg_t */
	DATA_PARSER_OPENAPI_LICENSES_RESP_PTR, /* openapi_resp_license_info_msg_t* */
	DATA_PARSER_LICENSES, /* license_info_msg_t */
	DATA_PARSER_LICENSES_PTR, /* license_info_msg_t* */
	DATA_PARSER_LICENSE, /* slurm_license_info_t */
	DATA_PARSER_LICENSE_PTR, /* slurm_license_info_t* */
	DATA_PARSER_JOB_INFO_MSG, /* job_info_msg_t */
	DATA_PARSER_JOB_INFO_MSG_PTR, /* job_info_msg_t* */
	DATA_PARSER_OPENAPI_JOB_INFO_RESP, /* openapi_resp_job_info_msg_t */
	DATA_PARSER_OPENAPI_JOB_INFO_RESP_PTR, /* openapi_resp_job_info_msg_t* */
	DATA_PARSER_OPENAPI_JOB_INFO_PARAM, /* openapi_job_info_param_t */
	DATA_PARSER_OPENAPI_JOB_INFO_PARAM_PTR, /* openapi_job_info_param_t* */
	DATA_PARSER_OPENAPI_JOB_INFO_DELETE_QUERY, /* openapi_job_info_delete_query_t */
	DATA_PARSER_OPENAPI_JOB_INFO_DELETE_QUERY_PTR, /* openapi_job_info_delete_query_t* */
	DATA_PARSER_OPENAPI_JOB_INFO_QUERY, /* openapi_job_info_query_t */
	DATA_PARSER_OPENAPI_JOB_INFO_QUERY_PTR, /* openapi_job_info_query_t* */
	DATA_PARSER_JOB_INFO, /* slurm_job_info_t */
	DATA_PARSER_JOB_INFO_PTR, /* slurm_job_info_t* */
	DATA_PARSER_JOB_INFO_STDIN, /* slurm_job_info_t->stdin (handles % replacements) */
	DATA_PARSER_JOB_INFO_STDOUT, /* slurm_job_info_t->stdout (handles % replacements) */
	DATA_PARSER_JOB_INFO_STDERR, /* slurm_job_info_t->stderr (handles % replacements) */
	DATA_PARSER_JOB_FLAGS, /* uint64_t & KILL_INV_DEP/HAS_STATE_DIR/... */
	DATA_PARSER_JOB_SHOW_FLAGS, /* uint32_t & SHOW_* */
	DATA_PARSER_CORE_SPEC, /* uint16_t & ~CORE_SPEC_THREAD */
	DATA_PARSER_THREAD_SPEC, /* uint16_t & CORE_SPEC_THREAD */
	DATA_PARSER_JOB_INFO_GRES_DETAIL, /* slurm_job_info_t->core_spec & CORE_SPEC_THREAD */
	DATA_PARSER_JOB_RES, /* job_resources_t */
	DATA_PARSER_JOB_RES_PTR, /* job_resources_t* */
	DATA_PARSER_JOB_RES_NODES, /* job_resources_t->nodes,core_bitmap,nhosts */
	DATA_PARSER_JOB_RES_NODE, /* JOB_RES_NODE_t */
	DATA_PARSER_JOB_RES_NODE_PTR, /* JOB_RES_NODE_t* */
	DATA_PARSER_JOB_RES_CORE, /* JOB_RES_CORE_t */
	DATA_PARSER_JOB_RES_CORE_PTR, /* JOB_RES_CORE_t* */
	DATA_PARSER_JOB_RES_CORE_STATUS, /* JOB_RES_CORE_status_t */
	DATA_PARSER_JOB_RES_CORE_ARRAY, /* JOB_RES_CORE_t[] */
	DATA_PARSER_JOB_RES_SOCKET, /* JOB_RES_SOCKET_t */
	DATA_PARSER_JOB_RES_SOCKET_PTR, /* JOB_RES_SOCKET_t* */
	DATA_PARSER_JOB_RES_SOCKET_ARRAY, /* JOB_RES_SOCKET_t[] */
	DATA_PARSER_JOB_MAIL_FLAGS, /* uint16_t & MAIL_JOB_* */
	DATA_PARSER_NICE, /* uint32_t - nice value - NICE_OFFSET */
	DATA_PARSER_MEM_PER_CPUS, /* uint64_t & MEM_PER_CPU */
	DATA_PARSER_MEM_PER_NODE, /* uint64_t & ~MEM_PER_CPU */
	DATA_PARSER_ACCT_GATHER_PROFILE, /* uint32_t - ACCT_GATHER_PROFILE_* */
	DATA_PARSER_ACCT_GATHER_ENERGY, /* acct_gather_energy_t */
	DATA_PARSER_ACCT_GATHER_ENERGY_PTR, /* acct_gather_energy_t* */
	DATA_PARSER_JOB_SHARED, /* uint16_t - JOB_SHARED_* */
	DATA_PARSER_JOB_EXCLUSIVE, /* uint16_t - JOB_SHARED_* */
	DATA_PARSER_JOB_EXCLUSIVE_FLAGS, /* uint16_t - JOB_SHARED_* */
	DATA_PARSER_ALLOCATED_CORES, /* DEPRECATED v41: uint32_t if slurm_conf.select_type_param & (CR_CORE|CR_SOCKET) */
	DATA_PARSER_ALLOCATED_CPUS, /* DEPRECATED v41: uint32_t if slurm_conf.select_type_param & CR_CPU */
	DATA_PARSER_HOSTLIST, /* hostlist_t* */
	DATA_PARSER_HOSTLIST_STRING, /* char * - acts like hostlist_t* */
	DATA_PARSER_POWER_FLAGS, /* uint8_t & SLURM_POWER_FLAGS_* */
	DATA_PARSER_PARTITION_INFO, /* partition_info_t */
	DATA_PARSER_PARTITION_INFO_PTR, /* partition_info_t* */
	DATA_PARSER_PARTITION_INFO_MSG, /* partition_info_msg_t */
	DATA_PARSER_PARTITION_INFO_MSG_PTR, /* partition_info_msg_t* */
	DATA_PARSER_PARTITION_INFO_ARRAY, /* partition_info_t** */
	DATA_PARSER_PARTITION_STATES, /* uint16_t & PARTITION_* */
	DATA_PARSER_OPENAPI_PARTITION_RESP, /* openapi_resp_partitions_info_msg_t */
	DATA_PARSER_OPENAPI_PARTITION_RESP_PTR, /* openapi_resp_partitions_info_msg_t* */
	DATA_PARSER_OPENAPI_PARTITION_PARAM, /* openapi_partition_param_t */
	DATA_PARSER_OPENAPI_PARTITION_PARAM_PTR, /* openapi_partition_param_t* */
	DATA_PARSER_OPENAPI_PARTITIONS_QUERY, /* openapi_partitions_query_t */
	DATA_PARSER_OPENAPI_PARTITIONS_QUERY_PTR, /* openapi_partitions_query_t* */
	DATA_PARSER_EXT_SENSORS_DATA, /* ext_sensors_data_t */
	DATA_PARSER_EXT_SENSORS_DATA_PTR, /* ext_sensors_data_t* */
	DATA_PARSER_POWER_MGMT_DATA, /* power_mgmt_data_t */
	DATA_PARSER_POWER_MGMT_DATA_PTR, /* power_mgmt_data_t* */
	DATA_PARSER_RESERVATION_INFO, /* reserve_info_t */
	DATA_PARSER_RESERVATION_INFO_PTR, /* reserve_info_t* */
	DATA_PARSER_RESERVATION_FLAGS, /* uint64_t & RESERVE_FLAG_* */
	DATA_PARSER_RESERVATION_INFO_MSG, /* reserve_info_msg_t */
	DATA_PARSER_RESERVATION_INFO_MSG_PTR, /* reserve_info_msg_t* */
	DATA_PARSER_RESERVATION_CORE_SPEC, /* resv_core_spec_t */
	DATA_PARSER_RESERVATION_CORE_SPEC_PTR, /* resv_core_spec_t* */
	DATA_PARSER_RESERVATION_INFO_CORE_SPEC, /* reserve_info_t->core_spec+core_spec_cnt */
	DATA_PARSER_RESERVATION_INFO_ARRAY, /* reserve_info_t** */
	DATA_PARSER_OPENAPI_RESERVATION_RESP, /* openapi_resp_reserve_info_msg_t */
	DATA_PARSER_OPENAPI_RESERVATION_RESP_PTR, /* openapi_resp_reserve_info_msg_t* */
	DATA_PARSER_OPENAPI_RESERVATION_PARAM, /* openapi_reservation_param_t */
	DATA_PARSER_OPENAPI_RESERVATION_PARAM_PTR, /* openapi_reservation_param_t* */
	DATA_PARSER_OPENAPI_RESERVATION_QUERY, /* openapi_reservation_query_t */
	DATA_PARSER_OPENAPI_RESERVATION_QUERY_PTR, /* openapi_reservation_query_t* */
	DATA_PARSER_JOB_ARRAY_RESPONSE_MSG, /* job_array_resp_msg_t */
	DATA_PARSER_JOB_ARRAY_RESPONSE_MSG_PTR, /* job_array_resp_msg_t * */
	DATA_PARSER_JOB_ARRAY_RESPONSE_MSG_ENTRY, /* JOB_ARRAY_RESPONSE_MSG_entry_t */
	DATA_PARSER_JOB_ARRAY_RESPONSE_MSG_ENTRY_PTR, /* JOB_ARRAY_RESPONSE_MSG_entry_t* */
	DATA_PARSER_JOB_ARRAY_RESPONSE_ARRAY, /* JOB_ARRAY_RESPONSE_MSG_entry_t[] */
	DATA_PARSER_OPENAPI_JOB_POST_RESPONSE, /* job_post_response_t */
	DATA_PARSER_OPENAPI_JOB_POST_RESPONSE_PTR, /* job_post_response_t* */
	DATA_PARSER_ERROR, /* int -> slurm_strerror() */
	DATA_PARSER_JOB_SUBMIT_RESPONSE_MSG, /* submit_response_msg_t */
	DATA_PARSER_JOB_SUBMIT_RESPONSE_MSG_PTR, /* submit_response_msg_t* */
	DATA_PARSER_OPENAPI_JOB_SUBMIT_RESPONSE, /* job_submit_response_t */
	DATA_PARSER_OPENAPI_JOB_SUBMIT_RESPONSE_PTR, /* job_submit_response_t* */
	DATA_PARSER_JOB_SUBMIT_REQ, /* job_submit_request_t */
	DATA_PARSER_JOB_SUBMIT_REQ_PTR, /* job_submit_request_t */
	DATA_PARSER_JOB_DESC_MSG, /* job_desc_msg_t */
	DATA_PARSER_JOB_DESC_MSG_ARGV, /* job_desc_msg_t->argv+argc */
	DATA_PARSER_JOB_DESC_MSG_CPU_FREQ, /* job_desc_msg_t->cpu_freq* */
	DATA_PARSER_JOB_DESC_MSG_ENV, /* job_desc_msg_t->env* */
	DATA_PARSER_JOB_DESC_MSG_NODES, /* job_desc_msg_t->min/max_cpus */
	DATA_PARSER_JOB_DESC_MSG_SPANK_ENV, /* job_desc_msg_t->spank_env* */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_CPU, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_FSIZE, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_DATA, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_STACK, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_CORE, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_RSS, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_NPROC, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_NOFILE, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_MEMLOCK, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_RLIMIT_AS, /* job_desc_msg_t->environment */
	DATA_PARSER_JOB_DESC_MSG_PTR, /* job_desc_msg_t* */
	DATA_PARSER_JOB_DESC_MSG_LIST, /* list_t of job_desc_msg_t* */
	DATA_PARSER_STRING_ARRAY, /* char** (NULL terminated) */
	DATA_PARSER_STRING_LIST, /* list_t of char* */
	DATA_PARSER_SIGNAL, /* uint16_t - UNIX process signal */
	DATA_PARSER_CPU_BINDING_FLAGS, /* uint16_t <-> cpu_bind_type_t */
	DATA_PARSER_CRON_ENTRY, /* cron_entry_t */
	DATA_PARSER_CRON_ENTRY_PTR, /* cron_entry_t* */
	DATA_PARSER_CRON_ENTRY_FLAGS, /* cron_entry_flag_t */
	DATA_PARSER_MEMORY_BINDING_TYPE, /* mem_bind_type_t */
	DATA_PARSER_OPEN_MODE, /* uint8_t - OPEN_MODE_* */
	DATA_PARSER_WARN_FLAGS, /* uint16_t - KILL_*|WARN_SENT */
	DATA_PARSER_X11_FLAGS, /* uint16_t - X11_FORWARD_* */
	DATA_PARSER_HOLD, /* uint32_t (priority) --hold */
	DATA_PARSER_OPENAPI_META, /* openapi_resp_meta_t */
	DATA_PARSER_OPENAPI_META_PTR, /* openapi_resp_meta_t* */
	DATA_PARSER_OPENAPI_META_COMMAND, /* openapi_resp_meta_t->command */
	DATA_PARSER_OPENAPI_ERROR, /* openapi_resp_error_t */
	DATA_PARSER_OPENAPI_ERROR_PTR, /* openapi_resp_error_t* */
	DATA_PARSER_OPENAPI_ERRORS, /* list_t of openapi_resp_error_t* */
	DATA_PARSER_OPENAPI_WARNING, /* openapi_resp_warning_t */
	DATA_PARSER_OPENAPI_WARNING_PTR, /* openapi_resp_warning_t* */
	DATA_PARSER_OPENAPI_WARNINGS, /* list_t of openapi_resp_warning_t* */
	DATA_PARSER_OPENAPI_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_TIMESTAMP, /* time_t */
	DATA_PARSER_TIMESTAMP_NO_VAL, /* time_t */
	DATA_PARSER_OPENAPI_SLURMDBD_CONFIG_RESP, /* openapi_resp_slurmdbd_config_t */
	DATA_PARSER_OPENAPI_SLURMDBD_CONFIG_RESP_PTR, /* openapi_resp_slurmdbd_config_t* */
	DATA_PARSER_SELECTED_STEP, /* slurm_selected_step_t */
	DATA_PARSER_SELECTED_STEP_PTR, /* slurm_selected_step_t* */
	DATA_PARSER_SELECTED_STEP_LIST, /* list_t of slurm_selected_step_t* */
	DATA_PARSER_OPENAPI_USER_PARAM, /* openapi_user_param_t */
	DATA_PARSER_OPENAPI_USER_PARAM_PTR, /* openapi_user_param_t* */
	DATA_PARSER_OPENAPI_USER_QUERY, /* openapi_user_query_t */
	DATA_PARSER_OPENAPI_USER_QUERY_PTR, /* openapi_user_query_t* */
	DATA_PARSER_FLAGS, /* data_parser_flags_t */
	DATA_PARSER_PROCESS_EXIT_CODE_VERBOSE, /* proc_exit_code_verbose_t */
	DATA_PARSER_PROCESS_EXIT_CODE_VERBOSE_PTR, /* proc_exit_code_verbose_t* */
	DATA_PARSER_PROCESS_EXIT_CODE_STATUS, /* proc_exit_code_status_t */
	DATA_PARSER_PROCESS_EXIT_CODE, /* uint32_t */
	DATA_PARSER_SHARES_REQ_MSG, /* shares_request_msg_t */
	DATA_PARSER_SHARES_REQ_MSG_PTR, /* shares_request_msg_t* */
	DATA_PARSER_SHARES_RESP_MSG, /* shares_response_msg_t */
	DATA_PARSER_SHARES_RESP_MSG_PTR, /* shares_response_msg_t* */
	DATA_PARSER_OPENAPI_SHARES_RESP, /* openapi_resp_single_t */
	DATA_PARSER_OPENAPI_SHARES_RESP_PTR, /* openapi_resp_single_t* */
	DATA_PARSER_SHARES_UINT64_TRES, /* SHARES_UINT64_TRES_t */
	DATA_PARSER_SHARES_UINT64_TRES_PTR, /* SHARES_UINT64_TRES_t* */
	DATA_PARSER_SHARES_UINT64_TRES_LIST, /* list_t of SHARES_UINT64_TRES_t */
	DATA_PARSER_SHARES_FLOAT128_TRES, /* SHARES_FLOAT128_TRES_t */
	DATA_PARSER_SHARES_FLOAT128_TRES_PTR, /* SHARES_FLOAT128_TRES_t* */
	DATA_PARSER_SHARES_FLOAT128_TRES_LIST, /* list_t of SHARES_FLOAT128_TRES_t */
	DATA_PARSER_ASSOC_SHARES_OBJ_LIST, /* list of assoc_shares_object_t* to convert to assoc_shares_object_wrap_t */
	DATA_PARSER_ASSOC_SHARES_OBJ_WRAP, /* assoc_shares_object_wrap_t to wrap assoc_shares_object_t */
	DATA_PARSER_ASSOC_SHARES_OBJ_WRAP_TRES_RUN_SECS, /* assoc_shares_object_wrap_t->tres_cnt&obj->tres_run_secs */
	DATA_PARSER_ASSOC_SHARES_OBJ_WRAP_TRES_GRP_MINS, /* assoc_shares_object_wrap_t->tres_cnt&obj->tres_grp_mins */
	DATA_PARSER_ASSOC_SHARES_OBJ_WRAP_TRES_USAGE_RAW, /* assoc_shares_object_wrap_t->tres_cnt&obj->usage_tres_raw */
	DATA_PARSER_ASSOC_SHARES_OBJ_WRAP_TYPE, /* assoc_shares_object_wrap_t.obj->user */
	DATA_PARSER_ASSOC_SHARES_OBJ_WRAP_PTR, /* assoc_shares_object_t* */
	DATA_PARSER_OVERSUBSCRIBE_JOBS, /* max_share */
	DATA_PARSER_OVERSUBSCRIBE_FLAGS, /* max_share */
	DATA_PARSER_JOB_PLANNED_TIME, /* slurmdb_job_rec_t->start - slurmdb_job_rec_t->eligible */
	DATA_PARSER_CR_TYPE, /* uint16_t - CR_* */
	DATA_PARSER_NODE_CR_TYPE, /* enum node_cr_state - uint32_t - NODE_CR_* */
	DATA_PARSER_TYPE_MAX
} data_parser_type_t;

/*
 * Function prototype for callback when there is a parsing error
 * Return: true to continue parsing, false to stop parsing
 */
typedef bool (*data_parser_on_error_t)(void *arg, data_parser_type_t type,
				       int error_code, const char *source,
				       const char *why, ...)
	__attribute__((format(printf, 5, 6)));
/*
 * Function prototype for callback when there is a parsing warning
 */
typedef void (*data_parser_on_warn_t)(void *arg, data_parser_type_t type,
				      const char *source, const char *why, ...)
	__attribute__((format(printf, 4, 5)));

typedef struct data_parser_s data_parser_t;

/* data_parser plugin for current Slurm version */
#define SLURM_DATA_PARSER_VERSION \
	("data_parser/v" XSTRINGIFY(SLURM_API_AGE) "." \
	 XSTRINGIFY(SLURM_API_REVISION) "." \
	 XSTRINGIFY(SLURM_API_CURRENT))

/* Separator character for parameters for a given data_parser plugin list */
#define SLURM_DATA_PARSER_PLUGIN_PARAMS_CHAR "+"

/*
 * Initialize new data parser against given plugin
 * IN on_parse_error - callback when an parsing error is encountered
 * 	ptr must remain valid until free called.
 * IN on_dump_error - callback when an parsing error is encountered
 * 	ptr must remain valid until free called.
 * IN on_error_arg - ptr to pass to on_error function (not modified)
 * 	ptr must remain valid until free called.
 * IN on_parse_warn - callback when an parsing warning is encountered
 * 	ptr must remain valid until free called.
 * IN on_dump_warn - callback when an parsing warning is encountered
 * 	ptr must remain valid until free called.
 * IN on_warn_arg - ptr to pass to on_warn function (not modified)
 * 	ptr must remain valid until free called.
 * IN plugin_type - plugin_type of data_parser plugin to load/use
 * 	Parameters for plugin may be included when delimited by
 * 	SLURM_DATA_PARSER_PLUGIN_PARAMS_CHAR.
 * IN listf - list function if plugin_type = "list"
 * IN skip_loading - skip any calls related to loading the plugins
 * RET parser ptr
 * 	Must be freed by call to data_parser_g_free()
 */
extern data_parser_t *data_parser_g_new(data_parser_on_error_t on_parse_error,
					data_parser_on_error_t on_dump_error,
					data_parser_on_error_t on_query_error,
					void *error_arg,
					data_parser_on_warn_t on_parse_warn,
					data_parser_on_warn_t on_dump_warn,
					data_parser_on_warn_t on_query_warn,
					void *warn_arg, const char *plugin_type,
					plugrack_foreach_t listf,
					bool skip_loading);

/*
 * Initialize a new data parser for all plugins found
 *
 * IN on_parse_error - callback when an parsing error is encountered
 * 	ptr must remain valid until free called.
 * IN on_dump_error - callback when an parsing error is encountered
 * 	ptr must remain valid until free called.
 * IN on_error_arg - ptr to pass to on_error function (not modified)
 * 	ptr must remain valid until free called.
 * IN on_parse_warn - callback when an parsing warning is encountered
 * 	ptr must remain valid until free called.
 * IN on_dump_warn - callback when an parsing warning is encountered
 * 	ptr must remain valid until free called.
 * IN on_warn_arg - ptr to pass to on_warn function (not modified)
 * 	ptr must remain valid until free called.
 * IN plugin_type - CSV of plugin_types of data_parser plugin to load/use
 * IN listf - list function if plugin_type = "list"
 * IN skip_loading - skip any calls related to loading the plugins
 * RET NULL terminated parser array ptr
 * 	Must be freed by call to data_parser_g_array_free()
 */
extern data_parser_t **data_parser_g_new_array(
	data_parser_on_error_t on_parse_error,
	data_parser_on_error_t on_dump_error,
	data_parser_on_error_t on_query_error,
	void *error_arg,
	data_parser_on_warn_t on_parse_warn,
	data_parser_on_warn_t on_dump_warn,
	data_parser_on_warn_t on_query_warn,
	void *warn_arg,
	const char *plugin_type,
	plugrack_foreach_t listf,
	bool skip_loading);

typedef enum {
	DATA_PARSER_ATTR_INVALID = 0,
	DATA_PARSER_ATTR_DBCONN_PTR, /* return of slurmdb_connection_get() - will not xfree */
	DATA_PARSER_ATTR_QOS_LIST, /* List<slurmdb_qos_rec_t *> - will xfree() */
	DATA_PARSER_ATTR_TRES_LIST, /* List<slurmdb_tres_rec_t *> - will xfree() */
	DATA_PARSER_ATTR_MAX /* place holder - do not use */
} data_parser_attr_type_t;

/*
 * Assign additional resource to parser
 * IN parser - parser to add resource
 * IN type - type of resource to assign
 * IN obj - ptr of resource to assign
 *   make sure to match the type given in data_parser_attr_type_t
 * RET SLURM_SUCCESS or error
 */
extern int data_parser_g_assign(data_parser_t *parser,
				data_parser_attr_type_t type, void *obj);

/*
 * Get Plugin type as string.
 * String is valid for the life of the parser.
 * RET plugin including params as string
 */
extern const char *data_parser_get_plugin(data_parser_t *parser);

/*
 * Get Plugin type as string stripping "data_parser/".
 * String is valid for the life of the parser.
 */
extern const char *data_parser_get_plugin_version(data_parser_t *parser);

/*
 * Get Plugin params as string
 * String is valid for the life of the parser.
 */
extern const char *data_parser_get_plugin_params(data_parser_t *parser);

/*
 * Free data parser instance
 * IN parser - parser to free
 * IN skip_unloading - skip unloading plugins
 * RET SLURM_SUCCESS or error
 */
extern void data_parser_g_free(data_parser_t *parser, bool skip_unloading);

#define FREE_NULL_DATA_PARSER(_X)                     \
	do {                                          \
		if (_X)                               \
			data_parser_g_free(_X, true); \
		_X = NULL;                            \
	} while (0)

/*
 * Free NULL terminated array of parsers
 */
extern void data_parser_g_array_free(data_parser_t **ptr, bool skip_unloading);

#define FREE_NULL_DATA_PARSER_ARRAY(_X, SKIP_UNLOAD)               \
	do {                                                       \
		if (_X)                                            \
			data_parser_g_array_free(_X, SKIP_UNLOAD); \
		_X = NULL;                                         \
	} while (0)

/*
 * Parse given data_t source into target struct dst
 * use DATA_PARSE() macro instead of calling directly!
 *
 * IN parser - return from data_parser_g_new()
 * IN type - expected type of data (there is no guessing here)
 * IN dst - ptr to struct/scalar to populate
 * 	This *must* be a pointer to the object and not just a value of the object.
 * IN dst_bytes - size of object pointed to by dst
 * IN src - data to parse into obj
 * IN/OUT parent_path - array of parent dictionary keys.
 *    Parse path from entire source to this specific src data_t.
 *    Assist any callers with knowing where parsing failed in tree.
 * RET SLURM_SUCCESS or error
 */
extern int data_parser_g_parse(data_parser_t *parser, data_parser_type_t type,
			       void *dst, ssize_t dst_bytes, data_t *src,
			       data_t *parent_path);

#define DATA_PARSE(parser, type, dst, src, parent_path)                    \
	data_parser_g_parse(parser, DATA_PARSER_##type, &dst, sizeof(dst), \
			    src, parent_path)

/*
 * Parse given target struct src into data_t dst
 * use DATA_DUMP() macro instead of calling directly!
 *
 * IN parser - return from data_parser_g_new()
 * IN type - type of obj
 * IN src - ptr to struct/scalar to dump to data_t
 * 	This *must* be a pointer to the object and not just a value of the object.
 * IN src_bytes - size of object pointed to by src
 * IN src - data to parse into obj
 * IN dst - ptr to data to populate with obj dump
 * RET SLURM_SUCCESS or error
 */
extern int data_parser_g_dump(data_parser_t *parser, data_parser_type_t type,
			      void *src, ssize_t src_bytes, data_t *dst);

#define DATA_DUMP(parser, type, src, dst) \
	data_parser_g_dump(parser, DATA_PARSER_##type, &src, sizeof(src), dst)

/*
 * Generate meta instance for a CLI command
 */
extern openapi_resp_meta_t *data_parser_cli_meta(int argc, char **argv,
						 const char *mime_type,
						 const char *data_parser);

#define DATA_PARSER_DUMP_CLI_CTXT_MAGIC 0x1BA211B3
typedef struct {
	int magic; /* DATA_PARSER_DUMP_CLI_CTXT_MAGIC */
	int rc;
	list_t *errors;
	list_t *warnings;
	const char *data_parser;
} data_parser_dump_cli_ctxt_t;

/*
 * Dump object of given type to STDOUT
 * This function is only intended for the simple dump of the data and then
 * exiting of the CLI command.
 * IN type - data parser type for *obj
 * IN obj_bytes - sizeof(*obj)
 * IN acct_db_conn - slurmdb connection or NULL
 * IN mime_type - dump object as given mime type
 * IN data_parser - data_parser parameters
 * IN meta - ptr to meta instance
 * RET SLURM_SUCCESS or error
 */
extern int data_parser_dump_cli_stdout(data_parser_type_t type, void *obj,
				       int obj_bytes, void *acct_db_conn,
				       const char *mime_type,
				       const char *data_parser,
				       data_parser_dump_cli_ctxt_t *ctxt,
				       openapi_resp_meta_t *meta);

/*
 * Dump object to stdout
 */
#define DATA_DUMP_CLI(type, src, argc, argv, db_conn, mime_type,              \
		      data_parser_str, rc)                                    \
	do {                                                                  \
		data_parser_dump_cli_ctxt_t dump_ctxt = {                     \
			.magic = DATA_PARSER_DUMP_CLI_CTXT_MAGIC,             \
			.data_parser = data_parser_str,                       \
		};                                                            \
		__typeof__(src) *src_ptr = &src;                              \
		if (!src.OPENAPI_RESP_STRUCT_META_FIELD_NAME)                 \
			src.OPENAPI_RESP_STRUCT_META_FIELD_NAME =             \
				data_parser_cli_meta(argc, argv, mime_type,   \
						     data_parser_str);        \
		if (!src.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME)               \
			src.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME =           \
				dump_ctxt.errors =                            \
					list_create(free_openapi_resp_error); \
		else                                                          \
			dump_ctxt.errors =                                    \
				src.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME;    \
		if (!src.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME)             \
			src.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME =         \
				dump_ctxt.warnings = list_create(             \
					free_openapi_resp_warning);           \
		else                                                          \
			dump_ctxt.warnings =                                  \
				src.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME;  \
		rc = data_parser_dump_cli_stdout(                             \
			DATA_PARSER_##type, src_ptr, sizeof(*src_ptr),        \
			db_conn, mime_type, data_parser_str, &dump_ctxt,      \
			src.OPENAPI_RESP_STRUCT_META_FIELD_NAME);             \
		FREE_OPENAPI_RESP_COMMON_CONTENTS(src_ptr);                   \
	} while (false)

/*
 * Dump object as single field to in common openapi response dictionary
 */
#define DATA_DUMP_CLI_SINGLE(type, src, argc, argv, db_conn, mime_type, \
			     data_parser, rc)                           \
	do {                                                            \
		openapi_resp_single_t openapi_resp = {                  \
			.response = src,                                \
		};                                                      \
		DATA_DUMP_CLI(type, openapi_resp, argc, argv, db_conn,  \
			      mime_type, data_parser, rc);              \
	} while (false)

/*
 * Populate OpenAPI schema for each parser
 * IN parser - parser to add schemas from
 * IN/OUT dst - OpenAPI specification to populate
 * RET SLURM_SUCCESS or ESLURM_NOT_SUPPORTED (to skip) or error
 */
extern int data_parser_g_specify(data_parser_t *parser, data_t *dst);

#endif
