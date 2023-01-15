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

#ifndef _DATA_PARSER_H
#define _DATA_PARSER_H

#include "src/common/data.h"

typedef enum {
	/* there is an implied * on every type */
	DATA_PARSER_TYPE_INVALID = 0,
	DATA_PARSER_BITSTR, /* bitstr_t */
	DATA_PARSER_STRING, /* char* */
	DATA_PARSER_UINT16, /* uint16_t */
	DATA_PARSER_UINT16_NO_VAL, /* uint16_t - may be NO_VAL16 or INFINTE16 */
	DATA_PARSER_UINT32, /* uint32_t */
	DATA_PARSER_UINT32_NO_VAL, /* uint32_t - maybe NO_VAL or INFINTE*/
	DATA_PARSER_UINT64, /* uint64_t */
	DATA_PARSER_UINT64_NO_VAL, /* uint64_t - NO_VAL64 or INFINTE64 */
	DATA_PARSER_INT64, /* int64_t */
	DATA_PARSER_INT64_NO_VAL, /* int64_t - NO_VAL64 or INFINTE64 */
	DATA_PARSER_FLOAT128, /* long double */
	DATA_PARSER_FLOAT64, /* double */
	DATA_PARSER_FLOAT64_NO_VAL, /* double - may be NO_VAL of INFINITE */
	DATA_PARSER_BOOL, /* uint8_t */
	DATA_PARSER_BOOL16, /* uint16_t */
	DATA_PARSER_BOOL16_NO_VAL, /* uint16_t - false if NO_VAL16 */
	DATA_PARSER_CSV_LIST, /* char * - comma delimited list */
	DATA_PARSER_ACCOUNT_LIST, /* list of slurmdb_account_rec_t* */
	DATA_PARSER_ACCOUNT, /* slurmdb_account_rec_t */
	DATA_PARSER_ACCOUNT_FLAGS, /* slurmdb_account_rec_t->flags & SLURMDB_ACCT_FLAG_* */
	DATA_PARSER_ACCOUNTING_LIST, /* list of slurmdb_accounting_rec_t* */
	DATA_PARSER_ACCOUNTING, /* slurmdb_accounting_rec_t */
	DATA_PARSER_ADMIN_LVL, /* uint16_t (placeholder for slurmdb_admin_level_t) */
	DATA_PARSER_ASSOC_ID, /* slurmdb_assoc_usage_t */
	DATA_PARSER_ASSOC_LIST, /* list of slurmdb_assoc_rec_t* */
	DATA_PARSER_ASSOC_SHORT_LIST, /* list of slurmdb_assoc_rec_t* only for id */
	DATA_PARSER_ASSOC_SHORT, /* slurmdb_assoc_rec_t (for id only) */
	DATA_PARSER_ASSOC_SHORT_PTR, /* slurmdb_assoc_rec_t* (for id only) */
	DATA_PARSER_ASSOC, /* slurmdb_assoc_rec_t */
	DATA_PARSER_ASSOC_FLAGS, /* slurmdb_assoc_rec_t->flags & ASSOC_FLAG_* */
	DATA_PARSER_ASSOC_USAGE, /* slurmdb_assoc_usage_t */
	DATA_PARSER_ASSOC_USAGE_PTR, /* slurmdb_assoc_usage_t* */
	DATA_PARSER_CLASSIFICATION_TYPE, /* slurmdb_classification_type_t */
	DATA_PARSER_CLUSTER_ACCT_REC_LIST, /* list of slurmdb_cluster_accounting_rec_t* */
	DATA_PARSER_CLUSTER_ACCT_REC, /* slurmdb_cluster_accounting_rec_t */
	DATA_PARSER_CLUSTER_CLASSIFICATION, /* uint16_t joined with slurmdb_classification_type_t */
	DATA_PARSER_CLUSTER_REC_LIST, /* list of slurmdb_cluster_rec_t */
	DATA_PARSER_CLUSTER_REC, /* slurmdb_cluster_rec_t */
	DATA_PARSER_CLUSTER_REC_FLAGS, /* slurmdb_cluster_rec_t->flags & CLUSTER_FLAG_* */
	DATA_PARSER_COORD_LIST, /* List of slurmdb_coord_rec_t* */
	DATA_PARSER_COORD, /* slurmdb_coord_rec_t */
	DATA_PARSER_CPU_FREQ_FLAGS, /* uint32_t & CPU_FREQ_* */
	DATA_PARSER_GROUP_ID, /* Group from numeric GID <-> gid_t */
	DATA_PARSER_GROUP_NAME, /* Group from string group name <-> gid_t */
	DATA_PARSER_JOB_EXIT_CODE, /* int32_t */
	DATA_PARSER_JOB_REASON, /* uint32_t <-> enum job_state_reason */
	DATA_PARSER_JOB_LIST, /* list of slurmdb_job_rec_t* */
	DATA_PARSER_JOB, /* slurmdb_job_rec_t */
	DATA_PARSER_SLURMDB_JOB_FLAGS, /* slurmdb_job_rec_t->flags & SLURMDB_JOB_* */
	DATA_PARSER_JOB_STATE, /* uint32_t <-> JOB_STATE_FLAGS */
	DATA_PARSER_STEP_INFO_MSG, /* job_step_info_response_msg_t */
	DATA_PARSER_STEP_INFO, /* job_step_info_t */
	DATA_PARSER_STEP_INFO_ARRAY, /* job_step_info_t* */
	DATA_PARSER_STEP, /* slurmdb_step_rec_t */
	DATA_PARSER_STEP_LIST, /* List of slurmdb_step_rec_t* */
	DATA_PARSER_STEP_NODES, /* slurmdb_step_rec_t->nodes */
	DATA_PARSER_STEP_TRES_REQ_MAX, /* slurmdb_step_rec_t->tres_usage_in_max(|_nodeid|taskid) */
	DATA_PARSER_STEP_TRES_REQ_MIN, /* slurmdb_step_rec_t->tres_usage_in_min(|_nodeid|taskid) */
	DATA_PARSER_STEP_TRES_USAGE_MAX, /* slurmdb_step_rec_t->tres_usage_out_in_max(|_nodeid|taskid) */
	DATA_PARSER_STEP_TRES_USAGE_MIN, /* slurmdb_step_rec_t->tres_usage_out_in_min(|_nodeid|taskid) */
	DATA_PARSER_JOB_USER, /* user/uid from slurmdb_job_rec_t* */
	DATA_PARSER_QOS_ID, /* uint32_t of QOS id */
	DATA_PARSER_QOS_ID_LIST, /* List of char* of QOS ids */
	DATA_PARSER_QOS_STRING_ID_LIST, /* List of char* of QOS ids */
	DATA_PARSER_QOS_NAME, /* char * of QOS name */
	DATA_PARSER_QOS_NAME_LIST, /* List of char* of QOS names */
	DATA_PARSER_QOS_PREEMPT_LIST, /* slurmdb_qos_rec_t->preempt_bitstr & preempt_list */
	DATA_PARSER_QOS, /* slurmdb_qos_rec_t */
	DATA_PARSER_QOS_LIST, /* list of slurmdb_qos_rec_t* */
	DATA_PARSER_QOS_FLAGS, /* slurmdb_qos_rec_t->flags & QOS_FLAG_* */
	DATA_PARSER_QOS_PREEMPT_MODES, /* slurmdb_qos_rec_t->preempt_mode & QOS_FLAG_* */
	DATA_PARSER_RPC_ID, /* slurmdbd_msg_type_t */
	DATA_PARSER_SELECT_PLUGIN_ID, /* int (SELECT_PLUGIN_*) -> string */
	DATA_PARSER_STATS_REC_PTR, /* slurmdb_stats_rec_t* */
	DATA_PARSER_STATS_REC, /* slurmdb_stats_rec_t */
	DATA_PARSER_STATS_RPC_LIST, /* list of slurmdb_rpc_obj_t* */
	DATA_PARSER_STATS_RPC, /* slurmdb_rpc_obj_t */
	DATA_PARSER_STATS_USER_LIST, /* list of slurmdb_rpc_obj_t* */
	DATA_PARSER_STATS_USER, /* slurmdb_rpc_obj_t */
	DATA_PARSER_ROLLUP_STATS, /* slurmdb_rollup_stats_t */
	DATA_PARSER_ROLLUP_STATS_PTR, /* slurmdb_rollup_stats_t* */
	DATA_PARSER_STEP_CPUFREQ_GOV, /* slurmdb_step_rec_t.req_cpufreq_gov (uint32_t) of CPU_FREQ_* flags */
	DATA_PARSER_SLURM_STEP_ID, /* slurm_step_id_t */
	DATA_PARSER_STEP_ID, /* uint32_t of job step id */
	DATA_PARSER_TASK_DISTRIBUTION, /* uint32_t <-> task_dist_states_t */
	DATA_PARSER_TRES_STR, /* List of slurmdb_tres_rec_t* combined into a TRES string with TRES type/name instead of ID */
	DATA_PARSER_TRES_ID_STR, /* List of slurmdb_tres_rec_t* combined into a TRES string with TRES id# instead of type/name */
	DATA_PARSER_TRES_LIST, /* List of slurmdb_tres_rec_t* */
	DATA_PARSER_TRES, /* slurmdb_tres_rec_t */
	DATA_PARSER_TRES_NCT, /* slurmdb_tres_nct_rec_t */
	DATA_PARSER_USER_ID, /* User from numeric UID */
	DATA_PARSER_USER, /* slurmdb_user_rec_t */
	DATA_PARSER_USER_LIST, /* List of slurmdb_user_rec_t*  */
	DATA_PARSER_USER_FLAGS, /* slurmdb_user_rec_t->parser_user_flags & SLURMDB_USER_FLAG_* */
	DATA_PARSER_WCKEY, /* slurmdb_wckey_rec_t */
	DATA_PARSER_WCKEY_LIST, /* List of slurmdb_wckey_rec_t* */
	DATA_PARSER_WCKEY_FLAGS, /* slurmdb_wckey_rec_t->flags & SLURMDB_WCKEY_FLAG_* */
	DATA_PARSER_WCKEY_TAG, /* uint32_t - * prefix denotes default */
	DATA_PARSER_SINFO_DATA, /* sinfo_data_t */
	DATA_PARSER_SINFO_DATA_LIST, /* list of sinfo_data_t* */
	DATA_PARSER_STATS_MSG, /* stats_info_response_msg_t */
	DATA_PARSER_STATS_MSG_CYCLE_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_CYCLE_MEAN_DEPTH, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_CYCLE_PER_MIN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_CYCLE_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_DEPTH_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_DEPTH_MEAN_TRY, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_QUEUE_LEN_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_TABLE_SIZE_MEAN, /* stats_info_response_msg_t-> computed value */
	DATA_PARSER_STATS_MSG_BF_ACTIVE, /* stats_info_response_msg_t-> computed bool */
	DATA_PARSER_STATS_MSG_RPCS_BY_TYPE, /* stats_info_response_msg_t-> computed bool */
	DATA_PARSER_STATS_MSG_RPCS_BY_USER, /* stats_info_response_msg_t-> computed bool */
	DATA_PARSER_CONTROLLER_PING, /* controller_ping_t */
	DATA_PARSER_CONTROLLER_PING_ARRAY, /* controller_ping_t (NULL terminated array) */
	DATA_PARSER_CONTROLLER_PING_MODE, /* char * - verbose controller mode */
	DATA_PARSER_CONTROLLER_PING_RESULT, /* bool - "UP" or "DOWN" */
	DATA_PARSER_NODE, /* node_info_t */
	DATA_PARSER_NODE_ARRAY, /* node_info_t** (NULL terminated) */
	DATA_PARSER_NODES, /* node_info_msg_t */
	DATA_PARSER_NODES_PTR, /* node_info_msg_t* */
	DATA_PARSER_NODE_STATES, /* uint32_t & NODE_STATE_* */
	DATA_PARSER_NODE_STATES_NO_VAL, /* uint32_t & NODE_STATE_* or NO_VAL */
	DATA_PARSER_NODE_SELECT_ALLOC_MEMORY, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_ALLOC_CPUS, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_ALLOC_IDLE_CPUS, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_TRES_USED, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_NODE_SELECT_TRES_WEIGHTED, /* node_info_t->select_nodeinfo  */
	DATA_PARSER_UPDATE_NODE_MSG, /* update_node_msg_t */
	DATA_PARSER_LICENSES, /* license_info_msg_t */
	DATA_PARSER_LICENSE, /* slurm_license_info_t */
	DATA_PARSER_JOB_INFO_MSG, /* job_info_msg_t */
	DATA_PARSER_JOB_INFO, /* slurm_job_info_t */
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
	DATA_PARSER_JOB_MAIL_FLAGS, /* uint16_t & MAIL_JOB_* */
	DATA_PARSER_NICE, /* uint32_t - nice value - NICE_OFFSET */
	DATA_PARSER_JOB_MEM_PER_CPU, /* uint64_t & MEM_PER_CPU */
	DATA_PARSER_JOB_MEM_PER_NODE, /* uint64_t & ~MEM_PER_CPU */
	DATA_PARSER_ACCT_GATHER_PROFILE, /* uint32_t - ACCT_GATHER_PROFILE_* */
	DATA_PARSER_ACCT_GATHER_ENERGY, /* acct_gather_energy_t */
	DATA_PARSER_ACCT_GATHER_ENERGY_PTR, /* acct_gather_energy_t* */
	DATA_PARSER_JOB_SHARED, /* uint16_t - JOB_SHARED_* */
	DATA_PARSER_ALLOCATED_CORES, /* uint32_t if slurm_conf.select_type_param & (CR_CORE|CR_SOCKET) */
	DATA_PARSER_ALLOCATED_CPUS, /* uint32_t if slurm_conf.select_type_param & CR_CPU */
	DATA_PARSER_HOSTLIST, /* hostlist_t */
	DATA_PARSER_HOSTLIST_STRING, /* char * - acts like hostlist_t */
	DATA_PARSER_POWER_FLAGS, /* uint8_t & SLURM_POWER_FLAGS_* */
	DATA_PARSER_PARTITION_INFO, /* partition_info_t */
	DATA_PARSER_PARTITION_INFO_PTR, /* partition_info_t* */
	DATA_PARSER_PARTITION_INFO_MSG, /* partition_info_msg_t */
	DATA_PARSER_PARTITION_INFO_ARRAY, /* partition_info_t** */
	DATA_PARSER_EXT_SENSORS_DATA, /* ext_sensors_data_t */
	DATA_PARSER_EXT_SENSORS_DATA_PTR, /* ext_sensors_data_t* */
	DATA_PARSER_POWER_MGMT_DATA, /* power_mgmt_data_t */
	DATA_PARSER_POWER_MGMT_DATA_PTR, /* power_mgmt_data_t* */
	DATA_PARSER_RESERVATION_INFO, /* reserve_info_t */
	DATA_PARSER_RESERVATION_FLAGS, /* uint64_t & RESERVE_FLAG_* */
	DATA_PARSER_RESERVATION_INFO_MSG, /* reserve_info_msg_t */
	DATA_PARSER_RESERVATION_CORE_SPEC, /* resv_core_spec_t */
	DATA_PARSER_RESERVATION_INFO_CORE_SPEC, /* reserve_info_t->core_spec+core_spec_cnt */
	DATA_PARSER_RESERVATION_INFO_ARRAY, /* reserve_info_t** */
	DATA_PARSER_JOB_ARRAY_RESPONSE_MSG, /* job_array_resp_msg_t */
	DATA_PARSER_JOB_ARRAY_RESPONSE_MSG_PTR, /* job_array_resp_msg_t * */
	DATA_PARSER_ERROR, /* int -> slurm_strerror() */
	DATA_PARSER_JOB_SUBMIT_RESPONSE_MSG, /* submit_response_msg_t */
	DATA_PARSER_JOB_DESC_MSG, /* job_desc_msg_t */
	DATA_PARSER_JOB_DESC_MSG_ARGV, /* job_desc_msg_t->argv+argc */
	DATA_PARSER_JOB_DESC_MSG_CPU_FREQ, /* job_desc_msg_t->cpu_freq* */
	DATA_PARSER_JOB_DESC_MSG_ENV, /* job_desc_msg_t->env* */
	DATA_PARSER_JOB_DESC_MSG_NODES, /* job_desc_msg_t->min/max_cpus */
	DATA_PARSER_JOB_DESC_MSG_SPANK_ENV, /* job_desc_msg_t->spank_env* */
	DATA_PARSER_JOB_DESC_MSG_PTR, /* job_desc_msg_t* */
	DATA_PARSER_JOB_DESC_MSG_LIST, /* list_t of job_desc_msg_t* */
	DATA_PARSER_STRING_ARRAY, /* char** (NULL terminated) */
	DATA_PARSER_SIGNAL, /* uint16_t - UNIX process signal */
	DATA_PARSER_CPU_BINDING_FLAGS, /* uint16_t <-> cpu_bind_type_t */
	DATA_PARSER_CRON_ENTRY, /* cron_entry_t */
	DATA_PARSER_CRON_ENTRY_PTR, /* cron_entry_t* */
	DATA_PARSER_CRON_ENTRY_FLAGS, /* cron_entry_flag_t */
	DATA_PARSER_MEMORY_BINDING_TYPE, /* mem_bind_type_t */
	DATA_PARSER_OPEN_MODE, /* uint8_t - OPEN_MODE_* */
	DATA_PARSER_WARN_FLAGS, /* uint16_t - KILL_*|WARN_SENT */
	DATA_PARSER_X11_FLAGS, /* uint16_t - X11_FORWARD_* */
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

/*
 * Initalize new data parser against given plugin
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
 * IN listf - list function if plugin_type = "list"
 * IN skip_loading - skip any calls related to loading the plugins
 * RET parser ptr
 * 	Must be freed by call to data_parser_free()
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
 */
extern const char *data_parser_get_plugin(data_parser_t *parser);

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
 * Dump object of given type to STDOUT
 * Uses the current release version of the data_parser plugin.
 * This function is only intended for the simple dump of the data and then
 * exiting of the CLI command.
 * IN type - data parser type for *obj
 * IN obj_bytes - sizeof(*obj)
 * IN key - dictionary key of entry to place object
 * IN argc - argc arg of main()
 * IN argv - argv arg of main()
 * IN acct_db_conn - slurmdb connection or NULL
 * IN mime_type - dump object as given mime type
 * RET SLURM_SUCCESS or error
 */
extern int data_parser_dump_cli_stdout(data_parser_type_t type, void *obj,
				       int obj_bytes, const char *key, int argc,
				       char **argv, void *acct_db_conn,
				       const char *mime_type);

#define DATA_DUMP_CLI(type, src, key, argc, argv, db_conn, mime_type)      \
	data_parser_dump_cli_stdout(DATA_PARSER_##type, &src, sizeof(src), \
				    key, argc, argv, db_conn, mime_type)

/*
 * Populate OpenAPI schema for each parser
 */
extern int data_parser_g_specify(data_parser_t *parser, data_t *dst);

#endif
