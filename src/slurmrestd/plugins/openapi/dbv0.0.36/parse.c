/*****************************************************************************\
 *  parse.c - Slurm REST API parsing handlers
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

#include <math.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/plugins/openapi/dbv0.0.36/api.h"

/*
 * WARNING: parser uses a ton of macros to avoid massive amounts of copy and
 * pasta of nearly identicaly code. Make sure to check the macros first before
 * adding code.
 *
 * The database structures have many cross calls and it was found just handling
 * them all in one location was much cleaner than having to copy and paste the
 * same code many times along with all the possible errors that will introduce.
 *
 * This code is basically equivent to the pack/unpack code used for the Slurm
 * internal protocol but is designed to be used against data_t for slurmrestd.
 * The code is not version checked as the plugins are locked to a specific
 * version instead.
 *
 * Each struct to be parsed/dumped is defined with an array of parser_t entries.
 * Macros are used to fill out the contents of each entry since they are highly
 * repetitive. Each struct should directly mirror the order and structure of the
 * original structure for simplicity. Through the use of offsetof() macro, we
 * are able to directly record the location of each struct member and then work
 * them directly while using a generic structure for the parser itself. At the
 * top of every parser/dumper, we put the pointer math to keep everything simple
 * in the function when using the offsets. The field can be a nested dictionary
 * and any "/" will automatically be expanded into dictionary entries to make
 * writting these definitions easy.
 *
 * Every struct type and primitive type is defined in parser_type_t along with a
 * description of what is being transcribed. With most types, there is a dumper
 * and parser function that needs to be defined in funcs[]. It is fully expected
 * that these functions are *symmetric* to make it easy to dump and load the
 * structs.
 *
 * Flags and quasi bool fields have special handlers to avoid needing to write
 * function to handle piles of flags. Even if a struct uses an individual bool
 * field for a flag, we try to place all of them in a flags array anyway.
 *
 * Stand alone structs that will be parsed/dumped need to be added to parsers to
 * allow external calls to easily dump/parse the structs.
 *
 * When structs are not symmetric, we use more complex types to make it look
 * symmetric to the users and handle the dirty work here instead of expecting
 * the user to do it.
 *
 * How to use:
 * 	1. Add all types to parser_type_t
 * 	2. Add parser and dumper function following prototypes to funcs[].
 * 	3. Add full structures to parsers (if needed).
 * 	4. Call dumper/parser where needed, if only a field is being modified,
 * 	this step can be skipped.
 *
 */

typedef struct {
	enum {
		PARSER_ENUM_FLAG_INVALID = 0,
		PARSER_ENUM_FLAG_BIT, /* set a single bit */
		PARSER_ENUM_FLAG_BOOL, /* set a bool using offset */
	} type;
	uint64_t flag;
	size_t size;
	const char *string;
	size_t field_offset;
} parser_enum_t;

typedef struct {
	size_t field_offset_count;
	size_t field_offset_node;
	size_t field_offset_task;
	size_t field_offset_nodes; /* char* node list */
} parser_tres_t;

typedef struct {
	const parser_enum_t *list;
	size_t count;
} parser_flags_t;

/* QOS preemption list uses one field to list and one field to set */
typedef struct {
	size_t field_offset_preempt_bitstr;
	size_t field_offset_preempt_list;
} parser_qos_preempt_t;

typedef struct {
	parser_type_t type;
	bool required;
	size_t field_offset;
	char *key;
	union {
		parser_flags_t flags;
		parser_tres_t tres;
		parser_qos_preempt_t qos_preempt;
	} per_type;
} parser_t;

/* templates for read and write functions */
typedef int (*parse_rfunc_t)(const parser_t *const parse, void *dst,
			     data_t *src, data_t *errors,
			     const parser_env_t *penv);
typedef int (*parse_wfunc_t)(const parser_t *const parse, void *src,
			     data_t *dst, const parser_env_t *penv);

typedef struct {
	parser_type_t type;
	const parser_t *const parse;
	const size_t parse_member_count;
} parsers_t;

#define add_parser(stype, mtype, req, field, path)                            \
	{                                                                     \
		.field_offset = offsetof(stype, field),                       \
		.key = path,                                                  \
		.required = req,                                              \
		.type = PARSE_##mtype,                                        \
	}
#define add_parser_qos_preempt(stype, req, field_bitstr, field_list, path)    \
	{                                                                     \
		.key = path,                                                  \
		.per_type.qos_preempt.field_offset_preempt_bitstr =           \
			offsetof(stype, field_bitstr),                        \
		.per_type.qos_preempt.field_offset_preempt_list =             \
			offsetof(stype, field_list),                          \
		.required = req,                                              \
		.type = PARSE_QOS_PREEMPT_LIST,                               \
	}
#define add_parser_tres(stype, req, field_count, field_node, field_task,      \
			field_nodes, path)                                    \
	{                                                                     \
		.key = path,                                                  \
		.per_type.tres.field_offset_count =                           \
			offsetof(stype, field_count),                         \
		.per_type.tres.field_offset_node =                            \
			offsetof(stype, field_node),                          \
		.per_type.tres.field_offset_nodes =                           \
			offsetof(stype, field_nodes),                         \
		.per_type.tres.field_offset_task =                            \
			offsetof(stype, field_task),                          \
		.required = req,                                              \
		.type = PARSE_TRES_NODE_COUNT_TASK,                           \
	}
#define add_parser_flags(flags_array, stype, req, field, path)                \
	{                                                                     \
		.field_offset = offsetof(stype, field),                       \
		.key = path,                                                  \
		.per_type.flags.list = flags_array,                           \
		.per_type.flags.count = ARRAY_SIZE(flags_array),              \
		.required = req,                                              \
		.type = PARSE_FLAGS,                                          \
	}
#define add_parser_enum_flag(stype, field, flagv, stringv)                    \
	{                                                                     \
		.flag = flagv,                                                \
		.size = sizeof(((stype *) 0)->field),                         \
		.string = stringv,                                            \
		.type = PARSER_ENUM_FLAG_BIT,                                 \
	}
/* will never set to FALSE, only will set to TRUE if matched  */
#define add_parse_enum_bool(stype, field, stringv)                            \
	{                                                                     \
		.field_offset = offsetof(stype, field),                       \
		.size = sizeof(((stype *) 0)->field),                         \
		.string = stringv,                                            \
		.type = PARSER_ENUM_FLAG_BOOL,                                \
	}

static int _parser_run(void *obj, const parser_t *const parse,
		       const size_t parse_member_count, data_t *data,
		       data_t *errors, const parser_env_t *penv);
static int _parser_dump(void *obj, const parser_t *const parse,
			const size_t parse_member_count, data_t *data,
			const parser_env_t *penv);

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_assoc_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_assoc_rec_t, mtype, true, field, path)

static const parser_enum_t parser_assoc_flags[] = {
	add_parser_enum_flag(slurmdb_assoc_rec_t, flags, ASSOC_FLAG_DELETED,
			     "DELETED"),
	add_parse_enum_bool(slurmdb_assoc_rec_t, is_def, "DEFAULT"),
};

static const parser_t parse_assoc_short[] = {
	/* Identifiers required for any given association */
	_add_parse_req(STRING, acct, "account"),
	_add_parse(STRING, cluster, "cluster"),
	_add_parse(STRING, partition, "partition"),
	_add_parse_req(STRING, user, "user"),
};

/* should mirror the structure of slurmdb_assoc_rec_t */
static const parser_t parse_assoc[] = {
	/* skipping accounting_list */
	_add_parse_req(STRING, acct, "account"),
	/* skipping assoc_next */
	/* skipping assoc_next_id */
	/* skipping bf_usage (not packed) */
	_add_parse(STRING, cluster, "cluster"),
	_add_parse(QOS_ID, def_qos_id, "default/qos"),
	add_parser_flags(parser_assoc_flags, slurmdb_assoc_rec_t, false, flags,
			 "flags"),
	/* skip lft */
	_add_parse(UINT32, grp_jobs, "max/jobs/per/count"),
	_add_parse(UINT32, grp_jobs_accrue, "max/jobs/per/accruing"),
	_add_parse(UINT32, grp_submit_jobs, "max/jobs/per/submitted"),
	_add_parse(TRES_LIST, grp_tres, "max/tres/total"),
	/* skipping gres_tres_ctld  (not packed) */
	_add_parse(TRES_LIST, max_tres_mins_pj, "max/tres/minutes/per/job"),
	/* skipping max_tres_mins_ctld */
	_add_parse(TRES_LIST, max_tres_run_mins, "max/tres/minutes/total"),
	/* skipping grp_tres_run_mins_ctld (not packed) */
	_add_parse(UINT32, grp_wall, "max/per/account/wall_clock"),
	_add_parse(TRES_LIST, max_tres_pj, "max/tres/per/job"),
	/* skipping max_tres_ctld */
	_add_parse(TRES_LIST, max_tres_pn, "max/tres/per/node"),
	/* skipping max_tres_pn_ctld */
	_add_parse(UINT32, max_wall_pj, "max/jobs/per/wall_clock"),
	_add_parse(UINT32, min_prio_thresh, "min/priority_threshold"),
	_add_parse(STRING, parent_acct, "parent_account"),
	/* skip parent_id */
	_add_parse(STRING, partition, "partition"),
	_add_parse(UINT32, priority, "priority"),
	_add_parse(QOS_STR_LIST, qos_list, "qos"),
	/* skip rgt */
	_add_parse(UINT32, shares_raw, "shares_raw"),
	/* slurmdbd should never set uid - it should always be zero */
	_add_parse(ASSOC_USAGE, usage, "usage"),
	_add_parse_req(STRING, user, "user"),
	/* skipping user_rec (not packed) */
};
#undef _add_parse
#undef _add_parse_req

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_user_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_user_rec_t, mtype, true, field, path)
/* should mirror the structure of slurmdb_user_rec */
static const parser_t parse_user[] = {
	_add_parse(ADMIN_LVL, admin_level, "administrator_level"),
	_add_parse(ASSOC_SHORT_LIST, assoc_list, "associations"),
	_add_parse(COORD_LIST, coord_accts, "coordinators"),
	_add_parse(STRING, default_acct, "default/account"),
	_add_parse(STRING, default_wckey, "default/wckey"),
	_add_parse_req(STRING, name, "name"),
	/* skipping old_name */
	/* skipping uid (should always be 0) */
};
#undef _add_parse
#undef _add_parse_req

#define _add_flag(flagn, flagv) \
	add_parser_enum_flag(slurmdb_job_rec_t, flags, flagn, flagv)
static const parser_enum_t parser_job_flags[] = {
	_add_flag(SLURMDB_JOB_CLEAR_SCHED, "CLEAR_SCHEDULING"),
	_add_flag(SLURMDB_JOB_FLAG_NOTSET, "NOT_SET"),
	_add_flag(SLURMDB_JOB_FLAG_SUBMIT, "STARTED_ON_SUBMIT"),
	_add_flag(SLURMDB_JOB_FLAG_SCHED, "STARTED_ON_SCHEDULE"),
	_add_flag(SLURMDB_JOB_FLAG_BACKFILL, "STARTED_ON_BACKFILL"),
};
#undef _add_flag

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_job_rec_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_job_rec_t  */
static const parser_t parse_job[] = {
	_add_parse(STRING, account, "account"),
	_add_parse(STRING, admin_comment, "comment/administrator"),
	_add_parse(UINT32, alloc_nodes, "allocation_nodes"),
	_add_parse(UINT32, array_job_id, "array/job_id"),
	_add_parse(UINT32, array_max_tasks, "array/limits/max/running/tasks"),
	_add_parse(STRING, array_task_str, "array/task"),
	_add_parse(UINT32, array_task_id, "array/task_id"),
	_add_parse(ASSOC_ID, associd, "association"),
	/* skip blockid (deprecated bluegene) */
	_add_parse(STRING, cluster, "cluster"),
	_add_parse(STRING, constraints, "constraints"),
	/* skip db_index */
	_add_parse(JOB_EXIT_CODE, derived_ec, "derived_exit_code"),
	_add_parse(STRING, derived_es, "comment/job"),
	_add_parse(UINT32, elapsed, "time/elapsed"),

	_add_parse(UINT32, eligible, "time/eligible"),
	_add_parse(UINT32, end, "time/end"),
	_add_parse(JOB_EXIT_CODE, exitcode, "exit_code"),
	add_parser_flags(parser_job_flags, slurmdb_job_rec_t, false, flags,
			 "flags"),
	/* skipping first_step_ptr (already added in steps) */
	_add_parse(GROUP_ID, gid, "group"),
	_add_parse(UINT32, het_job_id, "het/job_id"),
	_add_parse(UINT32, het_job_offset, "het/job_offset"),
	_add_parse(UINT32, jobid, "job_id"),
	_add_parse(STRING, jobname, "name"),
	/* skip lft */
	_add_parse(STRING, mcs_label, "mcs/label"),
	_add_parse(STRING, nodes, "nodes"),
	_add_parse(STRING, partition, "partition"),
	_add_parse(UINT32, priority, "priority"),
	_add_parse(QOS_ID, qosid, "qos"),
	_add_parse(UINT32, req_cpus, "required/CPUs"),
	_add_parse(UINT32, req_mem, "required/memory"),
	_add_parse(USER_ID, requid, "kill_request_user"),
	_add_parse(UINT32, resvid, "reservation/id"),
	_add_parse(UINT32, resv_name, "reservation/name"),
	/* skipping show_full */
	_add_parse(UINT32, eligible, "time/start"),
	_add_parse(JOB_STATE, state, "state/current"),
	_add_parse(JOB_REASON, state_reason_prev, "state/previous"),
	_add_parse(UINT32, submit, "time/submission"),
	_add_parse(JOB_STEPS, steps, "steps"),
	_add_parse(UINT32, suspended, "time/suspended"),
	_add_parse(STRING, system_comment, "comment/system"),
	_add_parse(UINT32, sys_cpu_sec, "time/system/seconds"),
	_add_parse(UINT32, sys_cpu_usec, "time/system/microseconds"),
	_add_parse(UINT32, timelimit, "time/limit"),
	_add_parse(UINT32, tot_cpu_sec, "time/total/seconds"),
	_add_parse(UINT32, tot_cpu_usec, "time/total/microseconds"),
	/* skipping track steps */
	_add_parse(TRES_LIST, tres_alloc_str, "tres/allocated"),
	_add_parse(TRES_LIST, tres_req_str, "tres/requested"),
	/* skipping uid (dup with user below) */
	/* skipping alloc_gres (dup with TRES) */
	/* skipping uid */
	_add_parse(STRING, user, "user"),
	_add_parse(UINT32, user_cpu_sec, "time/user/seconds"),
	_add_parse(UINT32, user_cpu_usec, "time/user/microseconds"),
	_add_parse(WCKEY_TAG, wckey, "wckey"),
	/* skipping wckeyid (redundant) */
	_add_parse(STRING, work_dir, "working_directory"),
};
#undef _add_parse

static const parser_enum_t parser_acct_flags[] = {
	add_parser_enum_flag(slurmdb_account_rec_t, flags,
			     SLURMDB_ACCT_FLAG_DELETED, "DELETED"),
};

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_account_rec_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_account_rec_t */
static const parser_t parse_acct[] = {
	_add_parse(ASSOC_SHORT_LIST, assoc_list, "associations"),
	_add_parse(COORD_LIST, coordinators, "coordinators"),
	_add_parse(STRING, description, "description"),
	_add_parse(STRING, name, "name"),
	_add_parse(STRING, organization, "organization"),
	add_parser_flags(parser_acct_flags, slurmdb_account_rec_t, false, flags,
			 "flags"),
};
#undef _add_parse

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_coord_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_coord_rec_t, mtype, true, field, path)
/* should mirror the structure of slurmdb_coord_rec_t  */
static const parser_t parse_coord[] = {
	_add_parse_req(STRING, name, "name"),
	_add_parse(UINT16, direct, "direct"),
};
#undef _add_parse
#undef _add_parse_req

static const parser_enum_t parser_wckey_flags[] = {
	add_parser_enum_flag(slurmdb_wckey_rec_t, flags,
			     SLURMDB_WCKEY_FLAG_DELETED, "DELETED"),
	add_parse_enum_bool(slurmdb_wckey_rec_t, is_def, "DEFAULT"),
};

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_wckey_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_wckey_rec_t, mtype, true, field, path)
/* should mirror the structure of slurmdb_wckey_rec_t */
static const parser_t parse_wckey[] = {
	_add_parse(ACCOUNT_LIST, accounting_list, "accounts"),
	_add_parse_req(STRING, cluster, "cluster"),
	_add_parse_req(UINT32, id, "id"),
	_add_parse_req(STRING, name, "name"),
	_add_parse_req(STRING, user, "user"),
	/* skipping uid */
	add_parser_flags(parser_wckey_flags, slurmdb_wckey_rec_t, false, flags,
			 "flags"),
};
#undef _add_parse
#undef _add_parse_req

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_tres_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_tres_rec_t, mtype, true, field, path)
/* should mirror the structure of slurmdb_tres_rec_t  */
static const parser_t parse_tres[] = {
	/* skip alloc_secs (sreport func) */
	/* skip rec_count (not packed) */
	_add_parse_req(STRING, type, "type"),
	_add_parse(STRING, name, "name"),
	_add_parse(UINT32, id, "id"),
	_add_parse(INT64, count, "count"),
};
#undef _add_parse
#undef _add_parse_req

#define _add_flag(flagn, flagstr) \
	add_parser_enum_flag(slurmdb_qos_rec_t, flags, flagn, flagstr)
static const parser_enum_t parser_qos_flags[] = {
	/* skipping QOS_FLAG_BASE */
	/* skipping QOS_FLAG_NOTSET */
	/* skipping QOS_FLAG_ADD */
	/* skipping QOS_FLAG_REMOVE */
	_add_flag(QOS_FLAG_PART_MIN_NODE, "PARTITION_MINIMUM_NODE"),
	_add_flag(QOS_FLAG_PART_MAX_NODE, "PARTITION_MAXIMUM_NODE"),
	_add_flag(QOS_FLAG_PART_TIME_LIMIT, "PARTITION_TIME_LIMIT"),
	_add_flag(QOS_FLAG_ENFORCE_USAGE_THRES, "ENFORCE_USAGE_THRESHOLD"),
	_add_flag(QOS_FLAG_NO_RESERVE, "NO_RESERVE"),
	_add_flag(QOS_FLAG_REQ_RESV, "REQUIRED_RESERVATION"),
	_add_flag(QOS_FLAG_DENY_LIMIT, "DENY_LIMIT"),
	_add_flag(QOS_FLAG_OVER_PART_QOS, "OVERRIDE_PARTITION_QOS"),
	_add_flag(QOS_FLAG_NO_DECAY, "NO_DECAY"),
	_add_flag(QOS_FLAG_USAGE_FACTOR_SAFE, "USAGE_FACTOR_SAFE"),
};
#undef _add_flag

#define _add_flag(flagn, flagstr) \
	add_parser_enum_flag(slurmdb_qos_rec_t, preempt_mode, flagn, flagstr)
static const parser_enum_t parser_qos_preempt_flags[] = {
	_add_flag(PREEMPT_MODE_SUSPEND, "SUSPEND"),
	_add_flag(PREEMPT_MODE_REQUEUE, "REQUEUE"),
	_add_flag(PREEMPT_MODE_CANCEL, "CANCEL"),
	_add_flag(PREEMPT_MODE_GANG, "GANG"),
	/* skip PREEMPT_MODE_OFF (it is implied by empty list) */
};
#undef _add_flag

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_qos_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_qos_rec_t, mtype, true, field, path)
/* should mirror the structure of slurmdb_qos_rec_t */
static const parser_t parse_qos[] = {
	/* skipping accounting_list */
	_add_parse(STRING, description, "description"),
	add_parser_flags(parser_qos_flags, slurmdb_qos_rec_t, false, flags,
			 "flags"),
	_add_parse(UINT32, id, "id"),
	_add_parse(UINT32, grace_time, "limits/grace_time"),
	_add_parse(UINT32, grp_jobs_accrue, "limits/max/active_jobs/accruing"),
	_add_parse(UINT32, grp_jobs, "limits/max/active_jobs/count"),
	_add_parse(TRES_LIST, grp_tres, "limits/max/tres/total"),
	/* skipping grp_tres_ctld (not packed) */
	_add_parse(TRES_LIST, grp_tres_run_mins,
		   "limits/max/tres/minutes/per/qos"),
	/* skipping grp_tres_run_mins_ctld (not packed) */
	_add_parse_req(STRING, name, "name"),
	_add_parse(UINT32, grp_wall, "limits/max/wall_clock/per/qos"),
	_add_parse(UINT32, max_jobs_pa, "limits/max/jobs/per/account"),
	_add_parse(UINT32, max_jobs_pu, "limits/max/jobs/per/user"),
	_add_parse(UINT32, max_jobs_accrue_pa,
		   "limits/max/accruing/per/account"),
	_add_parse(UINT32, max_jobs_accrue_pu, "limits/max/accruing/per/user"),
	_add_parse(TRES_LIST, max_tres_mins_pj,
		   "limits/max/tres/minutes/per/job"),
	/* skipping max_tres_mins_pj_ctld (not packed) */
	_add_parse(TRES_LIST, max_tres_pa, "limits/max/tres/per/account"),
	/* skipping max_tres_pa_ctld (not packed) */
	_add_parse(TRES_LIST, max_tres_pj, "limits/max/tres/per/job"),
	/* skipping max_tres_pj_ctld (not packed) */
	_add_parse(TRES_LIST, max_tres_pn, "limits/max/tres/per/node"),
	/* skipping max_tres_pn_ctld (not packed) */
	_add_parse(TRES_LIST, max_tres_pu, "limits/max/tres/per/user"),
	/* skipping max_tres_pu_ctld (not packed) */
	_add_parse(TRES_LIST, max_tres_run_mins_pa,
		   "limits/max/tres/minutes/per/account"),
	/* skipping max_tres_run_mins_pa_ctld (not packed) */
	_add_parse(TRES_LIST, max_tres_run_mins_pu,
		   "limits/max/tres/minutes/per/user"),
	/* skipping max_tres_run_mins_pu_ctld (not packed) */
	_add_parse(UINT32, max_wall_pj, "limits/max/wall_clock/per/job"),
	_add_parse(UINT32, min_prio_thresh, "limits/min/priority_threshold"),
	_add_parse(TRES_LIST, min_tres_pj, "limits/min/tres/per/job"),
	/* skipping min_tres_pj_ctld (not packed) */
	add_parser_qos_preempt(slurmdb_qos_rec_t, false, preempt_bitstr,
			       preempt_list, "preempt/list"),
	/* skip preempt_list (only for ops) */
	add_parser_flags(parser_qos_preempt_flags, slurmdb_qos_rec_t, false,
			 preempt_mode, "preempt/mode"),
	_add_parse(UINT32, preempt_exempt_time, "preempt/exempt_time"),
	_add_parse(UINT32, priority, "priority"),
	/* skip usage (not packed) */
	_add_parse(FLOAT64, usage_factor, "usage_factor"),
	_add_parse(FLOAT64, usage_thres, "usage_threshold"),
	/* skip blocked_until (not packed) */
};
#undef _add_parse
#undef _add_parse_req

#define _add_flag(flagn, flagv) \
	add_parser_enum_flag(slurmdb_step_rec_t, req_cpufreq_gov, flagn, flagv)
static const parser_enum_t parse_job_step_cpu_freq_flags[] = {
	_add_flag(CPU_FREQ_CONSERVATIVE, "Conservative"),
	_add_flag(CPU_FREQ_PERFORMANCE, "Performance"),
	_add_flag(CPU_FREQ_POWERSAVE, "PowerSave"),
	_add_flag(CPU_FREQ_ONDEMAND, "OnDemand"),
	_add_flag(CPU_FREQ_USERSPACE, "UserSpace"),
};
#undef _add_flag

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_step_rec_t, mtype, false, field, path)
#define _tres3(count, node, task, path)                                      \
	add_parser_tres(slurmdb_step_rec_t, false, count, node, task, nodes, \
			path)
/* should mirror the structure of slurmdb_step_rec_t   */
static const parser_t parse_job_step[] = {
	_add_parse(UINT32, elapsed, "time/elapsed"),
	_add_parse(UINT32, end, "time/end"),
	_add_parse(JOB_EXIT_CODE, exitcode, "exit_code"),
	/* skipping job_ptr (redundant here) */
	_add_parse(UINT32, nnodes, "nodes/count"),
	_add_parse(STRING, nodes, "nodes/range"),
	_add_parse(UINT32, ntasks, "tasks/count"),
	_add_parse(STRING, pid_str, "pid"),
	_add_parse(UINT32, req_cpufreq_min, "CPU/requested_frequency/min"),
	_add_parse(UINT32, req_cpufreq_max, "CPU/requested_frequency/max"),
	add_parser_flags(parse_job_step_cpu_freq_flags, slurmdb_step_rec_t,
			 false, req_cpufreq_gov, "CPU/governor"),
	_add_parse(USER_ID, requid, "kill_request_user"),
	_add_parse(UINT32, start, "time/start"),
	_add_parse(JOB_STATE, state, "state"),
	_add_parse(UINT32, stats.act_cpufreq,
		   "statistics/CPU/actual_frequency"),
	_add_parse(UINT32, stats.consumed_energy, "statistics/energy/consumed"),
	_add_parse(UINT32, step_id.job_id, "step/job_id"),
	_add_parse(UINT32, step_id.step_het_comp, "step/het/component"),
	_add_parse(STEP_ID, step_id.step_id, "step/id"),
	_add_parse(STRING, stepname, "step/name"),
	_add_parse(UINT32, suspended, "time/suspended"),
	_add_parse(UINT32, sys_cpu_sec, "time/system/seconds"),
	_add_parse(UINT32, sys_cpu_usec, "time/system/microseconds"),
	_add_parse(TASK_DISTRIBUTION, task_dist, "task/distribution"),
	_add_parse(UINT32, tot_cpu_sec, "time/total/seconds"),
	_add_parse(UINT32, tot_cpu_usec, "time/total/microseconds"),
	_add_parse(UINT32, user_cpu_sec, "time/user/seconds"),
	_add_parse(UINT32, user_cpu_usec, "time/user/microseconds"),

	_add_parse(TRES_LIST, stats.tres_usage_in_ave,
		   "tres/requested/average"),
	_tres3(stats.tres_usage_in_max, stats.tres_usage_in_max_nodeid,
	       stats.tres_usage_in_max_taskid, "tres/requested/max"),
	_tres3(stats.tres_usage_in_min, stats.tres_usage_in_min_nodeid,
	       stats.tres_usage_in_min_taskid, "tres/requested/min"),
	_add_parse(TRES_LIST, stats.tres_usage_in_tot, "tres/requested/total"),
	_add_parse(TRES_LIST, stats.tres_usage_out_ave,
		   "tres/consumed/average"),
	_tres3(stats.tres_usage_out_max, stats.tres_usage_out_max_nodeid,
	       stats.tres_usage_out_max_taskid, "tres/consumed/max"),
	_tres3(stats.tres_usage_out_min, stats.tres_usage_out_min_nodeid,
	       stats.tres_usage_out_min_taskid, "tres/consumed/min"),
	_add_parse(TRES_LIST, stats.tres_usage_out_tot, "tres/consumed/total"),
	_add_parse(TRES_LIST, tres_alloc_str, "tres/allocated"),
};
#undef _add_parse
#undef _tres3

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_stats_rec_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_stats_rec_t */
static const parser_t parse_stats_rec[] = {
	_add_parse(UINT32, time_start, "time_start"),
	_add_parse(STATS_REC_ARRAY, dbd_rollup_stats, "rollups"),
	_add_parse(STATS_RPC_LIST, rpc_list, "RPCs"),
	_add_parse(STATS_USER_LIST, user_list, "users"),
};
#undef _add_parse

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_rpc_obj_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_rpc_obj_t */
static const parser_t parse_stats_user_rpcs[] = {
	_add_parse(USER_ID, id, "user"),
	_add_parse(UINT32, cnt, "count"),
	_add_parse(UINT64, time_ave, "time/average"),
	_add_parse(UINT64, time, "time/total"),
};
#undef _add_parse

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_rpc_obj_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_rpc_obj_t */
static const parser_t parse_stats_rpcs[] = {
	_add_parse(RPC_ID, id, "rpc"),
	_add_parse(UINT32, cnt, "count"),
	_add_parse(UINT64, time_ave, "time/average"),
	_add_parse(UINT64, time, "time/total"),
};
#undef _add_parse

#define _add_flag(flagn, flagstr) \
	add_parser_enum_flag(slurmdb_cluster_rec_t, flags, flagn, flagstr)
static const parser_enum_t parse_cluster_rec_flags[] = {
	_add_flag(CLUSTER_FLAG_MULTSD, "MULTIPLE_SLURMD"),
	_add_flag(CLUSTER_FLAG_FE, "FRONT_END"),
	_add_flag(CLUSTER_FLAG_CRAY_N, "CRAY_NATIVE"),
	_add_flag(CLUSTER_FLAG_FED, "FEDERATION"),
	_add_flag(CLUSTER_FLAG_EXT, "EXTERNAL"),
};
#undef _add_flag

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_cluster_rec_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_cluster_rec_t */
static const parser_t parse_cluster_rec[] = {
	/* skip accounting (sreport func only) */
	/* skip classification (to be deprecated) */
	/* skip comm_fail_time (not packed) */
	/* skip control_addr (not packed) */
	_add_parse(STRING, control_host, "controller/host"),
	_add_parse(UINT32, control_port, "controller/port"),
	/* skip dim_size (BG deprecated) */
	/* skip fed[eration] support */
	add_parser_flags(parse_cluster_rec_flags, slurmdb_cluster_rec_t, false,
			 flags, "flags"),
	/* skip lock (not packed) */
	_add_parse(STRING, name, "name"),
	_add_parse(STRING, nodes, "nodes"),
	_add_parse(SELECT_PLUGIN_ID, plugin_id_select, "select_plugin"),
	_add_parse(ASSOC_SHORT, root_assoc, "associations/root"),
	_add_parse(UINT16, rpc_version, "rpc_version"),
	/* skip send_rpc (not packed) */
	_add_parse(TRES_LIST, tres_str, "tres"),
};
#undef _add_parse

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_cluster_accounting_rec_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_cluster_accounting_rec_t */
static const parser_t parse_cluster_accounting_rec[] = {
	_add_parse(UINT64, alloc_secs, "time/allocated"),
	_add_parse(UINT64, down_secs, "time/down"),
	_add_parse(UINT64, idle_secs, "time/idle"),
	_add_parse(UINT64, over_secs, "time/overcommitted"),
	_add_parse(UINT64, pdown_secs, "time/planned_down"),
	_add_parse(UINT64, period_start, "time/start"),
	_add_parse(UINT64, period_start, "time/reserved"),
	_add_parse(STRING, tres_rec.name, "tres/name"),
	_add_parse(STRING, tres_rec.type, "tres/type"),
	_add_parse(UINT32, tres_rec.id, "tres/id"),
	_add_parse(UINT64, tres_rec.count, "tres/count"),
};
#undef _add_parse

static int _parse_to_string(const parser_t *const parse, void *obj, data_t *str,
			    data_t *errors, const parser_env_t *penv)
{
	int rc = SLURM_SUCCESS;
	char **dst = (((void *)obj) + parse->field_offset);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		xfree(*dst);
	} else if (data_convert_type(str, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		xfree(*dst);
		*dst = xstrdup(data_get_string(str));
	} else {
		rc = ESLURM_DATA_CONV_FAILED;
	}

	debug5("%s: string %s rc[%d]=%s", __func__, *dst, rc,
	       slurm_strerror(rc));

	return rc;
}

static int _dump_to_string(const parser_t *const parse, void *obj, data_t *data,
			   const parser_env_t *penv)
{
	int rc = SLURM_SUCCESS;
	char **src = (((void *)obj) + parse->field_offset);

	if (*src)
		data_set_string(data, *src);
	else
		data_set_null(data);

	return rc;
}

static int _parse_to_float128(const parser_t *const parse, void *obj,
			      data_t *str, data_t *errors,
			      const parser_env_t *penv)
{
	long double *dst = (((void *)obj) + parse->field_offset);
	int rc = SLURM_SUCCESS;

	xassert(sizeof(long double) * 8 == 128);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double)NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %Lf rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int _dump_to_float128(const parser_t *const parse, void *obj,
			     data_t *dst, const parser_env_t *penv)
{
	long double *src = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* see bug#9674 */
	if (((uint32_t)*src == INFINITE) || ((uint32_t)*src == NO_VAL))
		data_set_null(dst);
	else
		(void)data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int _parse_to_float64(const parser_t *const parse, void *obj,
			     data_t *str, data_t *errors,
			     const parser_env_t *penv)
{
	double *dst = (((void *)obj) + parse->field_offset);
	int rc = SLURM_SUCCESS;

	xassert(sizeof(double) * 8 == 64);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double)NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %f rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int _dump_to_float64(const parser_t *const parse, void *obj, data_t *dst,
			    const parser_env_t *penv)
{
	double *src = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* see bug#9674 */
	if (((uint32_t)*src == INFINITE) || ((uint32_t)*src == NO_VAL))
		(void)data_set_null(dst);
	else
		(void)data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int _parse_to_int64(const parser_t *const parse, void *obj, data_t *str,
			   data_t *errors, const parser_env_t *penv)
{
	int64_t *dst = (((void *)obj) + parse->field_offset);
	int rc = SLURM_SUCCESS;

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double)NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %zd rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int _dump_to_int64(const parser_t *const parse, void *obj, data_t *dst,
			  const parser_env_t *penv)
{
	int64_t *src = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL64) || (*src == INFINITE64))
		(void)data_set_null(dst);
	else
		(void)data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int _parse_to_uint16(const parser_t *const parse, void *obj, data_t *str,
			    data_t *errors, const parser_env_t *penv)
{
	uint16_t *dst = (((void *)obj) + parse->field_offset);
	int rc = SLURM_SUCCESS;

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_INT_64) == DATA_TYPE_INT_64)
		*dst = data_get_int(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %hu rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int _dump_to_uint16(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	int16_t *src = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL16) || (*src == INFINITE16))
		data_set_null(dst);
	else
		(void)data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int _parse_to_uint64(const parser_t *const parse, void *obj, data_t *str,
			    data_t *errors, const parser_env_t *penv)
{
	uint64_t *dst = (((void *)obj) + parse->field_offset);
	int rc = SLURM_SUCCESS;

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_INT_64) == DATA_TYPE_INT_64)
		*dst = data_get_int(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %zu rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int _dump_to_uint64(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	int64_t *src = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL64) || (*src == INFINITE64))
		data_set_null(dst);
	else
		(void)data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int _parse_to_uint32(const parser_t *const parse, void *obj, data_t *str,
			    data_t *errors, const parser_env_t *penv)
{
	uint32_t *dst = (((void *)obj) + parse->field_offset);
	int rc = SLURM_SUCCESS;

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = 0;
	} else if (data_convert_type(str, DATA_TYPE_INT_64) ==
		   DATA_TYPE_INT_64) {
		/* catch -1 and set to NO_VAL instead of rolling */
		if (0xFFFFFFFF00000000 & data_get_int(str))
			*dst = NO_VAL;
		else
			*dst = data_get_int(str);
	} else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %u rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int _dump_to_uint32(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	uint32_t *src = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((*src == NO_VAL) || (*src == INFINITE))
		data_set_null(dst);
	else
		(void)data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_PARSE_FLAGS 0xba2d2a13
typedef struct {
	int magic;
	uint32_t *flags;
	data_t *errors;
	const parser_t *const parse;
	void *obj;
} for_each_parse_flag_t;

static data_for_each_cmd_t _for_each_parse_flag(data_t *data, void *arg)
{
	for_each_parse_flag_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_PARSE_FLAGS);

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	for (int i = 0; i < args->parse->per_type.flags.count; i++) {
		const parser_enum_t *f = (args->parse->per_type.flags.list + i);

		bool match = !xstrcasecmp(data_get_string(data), f->string);

		if (f->type == PARSER_ENUM_FLAG_BIT) {
			const size_t b = f->size;
			if (!match)
				continue;

			/* C allows complier to choose a size for the enum */
			if (b == sizeof(uint64_t)) {
				uint64_t *flags = (((void *)args->obj) +
						   args->parse->field_offset);
				*flags |= f->flag;
			} else if (b == sizeof(uint32_t)) {
				uint32_t *flags = (((void *)args->obj) +
						   args->parse->field_offset);
				*flags |= f->flag;
			} else if (b == sizeof(uint16_t)) {
				uint16_t *flags = (((void *)args->obj) +
						   args->parse->field_offset);
				*flags |= f->flag;
			} else if (b == sizeof(uint8_t)) {
				uint8_t *flags = (((void *)args->obj) +
						  args->parse->field_offset);
				*flags |= f->flag;
			} else
				fatal("%s: unexpected enum size: %zu", __func__,
				      b);
		} else if (f->type == PARSER_ENUM_FLAG_BOOL) {
			const size_t b = f->size;

			if (!match)
				continue;

			/*
			 * flag applies to a bool in the obj structure and not
			 * in obj->flags.
			 * Set true while being aware of the size of the
			 * original variable since it may move around the 1.
			 */

			if (b == sizeof(uint64_t)) {
				uint64_t *ptr = (((void *)args->obj) +
						 f->field_offset);
				*ptr = true;
			} else if (b == sizeof(uint32_t)) {
				uint32_t *ptr = (((void *)args->obj) +
						 f->field_offset);
				*ptr = true;
			} else if (b == sizeof(uint16_t)) {
				uint16_t *ptr = (((void *)args->obj) +
						 f->field_offset);
				*ptr = true;
			} else if (b == sizeof(uint8_t)) {
				uint8_t *ptr = (((void *)args->obj) +
						f->field_offset);
				*ptr = true;
			} else
				fatal("%s: unexpected bool size: %zu", __func__,
				      b);
		} else
			fatal("%s: unexpect type", __func__);
	}

	return DATA_FOR_EACH_CONT;
}

static int _parse_flags(const parser_t *const parse, void *obj, data_t *src,
			data_t *errors, const parser_env_t *penv)
{
	for_each_parse_flag_t args = {
		.errors = errors,
		.magic = MAGIC_FOREACH_PARSE_FLAGS,
		.obj = obj,
		.parse = parse,
	};

	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	if (data_list_for_each(src, _for_each_parse_flag, &args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
	;
}

static int _dump_flags(const parser_t *const parse, void *obj, data_t *data,
		       const parser_env_t *penv)
{
	xassert(data_get_type(data) == DATA_TYPE_NULL);
	data_set_list(data);

	for (int i = 0; i < parse->per_type.flags.count; i++) {
		const parser_enum_t *f = (parse->per_type.flags.list + i);
		bool found = false;

		if (f->type == PARSER_ENUM_FLAG_BIT) {
			const size_t b = f->size;

			/* C allows complier to choose a size for the enum */
			if (b == sizeof(uint64_t)) {
				uint64_t *flags = (((void *)obj) +
						   parse->field_offset);
				if (*flags & f->flag)
					found = true;
			} else if (b == sizeof(uint32_t)) {
				uint32_t *flags = (((void *)obj) +
						   parse->field_offset);
				if (*flags & f->flag)
					found = true;
			} else if (b == sizeof(uint16_t)) {
				uint16_t *flags = (((void *)obj) +
						   parse->field_offset);
				if (*flags & f->flag)
					found = true;
			} else if (b == sizeof(uint8_t)) {
				uint8_t *flags = (((void *)obj) +
						  parse->field_offset);
				if (*flags & f->flag)
					found = true;
			} else
				fatal("%s: unexpected enum size: %zu", __func__,
				      b);

		} else if (f->type == PARSER_ENUM_FLAG_BOOL) {
			const size_t b = f->size;

			if (b == sizeof(uint64_t)) {
				uint64_t *ptr = (((void *)obj) +
						 f->field_offset);
				if (*ptr)
					found = true;
			} else if (b == sizeof(uint32_t)) {
				uint32_t *ptr = (((void *)obj) +
						 f->field_offset);
				if (*ptr)
					found = true;
			} else if (b == sizeof(uint16_t)) {
				uint16_t *ptr = (((void *)obj) +
						 f->field_offset);
				if (*ptr)
					found = true;
			} else if (b == sizeof(uint8_t)) {
				uint8_t *ptr = (((void *)obj) +
						f->field_offset);
				if (*ptr)
					found = true;
			} else
				fatal("%s: unexpected bool size: %zu", __func__,
				      b);

		} else
			fatal("%s: unknown flag type", __func__);

		if (found)
			data_set_string(data_list_append(data), f->string);
	}

	return SLURM_SUCCESS;
	;
}

#define MAGIC_FOREACH_PARSE_QOS 0xabaa2c18
typedef struct {
	int magic;
	List qos_list;
	data_t *errors;
} for_each_parse_qos_t;

static data_for_each_cmd_t _for_each_parse_qos(data_t *data, void *arg)
{
	for_each_parse_qos_t *args = arg;
	data_t *name;

	xassert(args->magic == MAGIC_FOREACH_PARSE_QOS);

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_FAIL;

	/*
	 * Note: we ignore everything but name for loading QOS into an
	 * qos_list as that is the only field accepted
	 */

	if (!(name = data_key_get(data, "name")) ||
	    data_convert_type(name, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	(void)list_append(args->qos_list, xstrdup(data_get_string(name)));

	return DATA_FOR_EACH_CONT;
}

static int _parse_qos_str_id(const parser_t *const parse, void *obj,
			     data_t *src, data_t *errors,
			     const parser_env_t *penv)
{
	char *qos_name = NULL;
	uint32_t *qos_id = (((void *)obj) + parse->field_offset);
	slurmdb_qos_rec_t *qos;

	if (data_get_type(src) == DATA_TYPE_NULL) {
		*qos_id = 0;
		return SLURM_SUCCESS;
	} else if (data_convert_type(src, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		qos_name = data_get_string(src);
	} else
		return ESLURM_DATA_CONV_FAILED;

	if (!qos_name || !qos_name[0])
		return ESLURM_DATA_CONV_FAILED;

	/* find qos by name from global list */
	xassert(penv->g_qos_list);
	if (!penv->g_qos_list)
		return ESLURM_REST_EMPTY_RESULT;

	qos = list_find_first(penv->g_qos_list,
			      slurmdb_find_qos_in_list_by_name, qos_name);
	if (!qos)
		return ESLURM_REST_EMPTY_RESULT;

	*qos_id = qos->id;

	return SLURM_SUCCESS;
}

static int _dump_qos_str_id(const parser_t *const parse, void *obj, data_t *dst,
			    const parser_env_t *penv)
{
	uint32_t *qos_id = (((void *)obj) + parse->field_offset);
	slurmdb_qos_rec_t *qos;

	if (*qos_id == 0) {
		data_set_null(dst);
		return SLURM_SUCCESS;
	}

	/* find qos by id from global list */
	xassert(penv->g_qos_list);
	if (!penv->g_qos_list)
		return ESLURM_REST_EMPTY_RESULT;

	qos = list_find_first(penv->g_qos_list, slurmdb_find_qos_in_list,
			      qos_id);
	if (!qos)
		/* QOS has an ID but it is not found??? */
		return ESLURM_REST_EMPTY_RESULT;

	(void)data_set_string(dst, qos->name);

	return SLURM_SUCCESS;
}

static int _parse_qos_str_list(const parser_t *const parse, void *obj,
			       data_t *src, data_t *errors,
			       const parser_env_t *penv)
{
	List *qos_list = (((void *)obj) + parse->field_offset);
	for_each_parse_qos_t args = {
		.magic = MAGIC_FOREACH_PARSE_QOS,
		.errors = errors,
	};

	if (!*qos_list)
		*qos_list = list_create(xfree_ptr);

	args.qos_list = *qos_list;

	if (data_list_for_each(src, _for_each_parse_qos, &args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_DUMP_QOS_STR_LIST 0xaaae2af2
typedef struct {
	int magic;
	data_t *qos;
} foreach_dump_qos_str_list_t;

static int _foreach_dump_qos_str_list(void *x, void *arg)
{
	char *qos = x;
	foreach_dump_qos_str_list_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_DUMP_QOS_STR_LIST);

	data_set_string(data_list_append(args->qos), qos);
	return 0;
}

static int _dump_qos_str_list(const parser_t *const parse, void *obj,
			      data_t *dst, const parser_env_t *penv)
{
	List *qos_list = (((void *)obj) + parse->field_offset);
	foreach_dump_qos_str_list_t args = {
		.magic = MAGIC_FOREACH_DUMP_QOS_STR_LIST,
		.qos = dst,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	if (list_for_each(*qos_list, _foreach_dump_qos_str_list, &args) < 0)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_QOS_PREEMPT_LIST 0xa8eb1313
typedef struct {
	int magic;
	data_t *errors;
	List qos_list;
	const parser_env_t *penv;
} foreach_parse_qos_preempt_list_t;

static data_for_each_cmd_t _foreach_parse_qos_preempt_list(data_t *data,
							   void *arg)
{
	foreach_parse_qos_preempt_list_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_QOS_PREEMPT_LIST);

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	list_append(args->qos_list, xstrdup(data_get_string(data)));
	return DATA_FOR_EACH_CONT;
}

static int _parse_qos_preempt_list(const parser_t *const parse, void *obj,
				   data_t *src, data_t *errors,
				   const parser_env_t *penv)
{
#ifndef NDEBUG
	bitstr_t **preempt_bitstr =
		(((void *)obj) +
		 parse->per_type.qos_preempt.field_offset_preempt_bitstr);
#endif
	List *preempt_list =
		(((void *)obj) +
		 parse->per_type.qos_preempt.field_offset_preempt_list);
	foreach_parse_qos_preempt_list_t args = {
		.magic = MAGIC_FOREACH_QOS_PREEMPT_LIST,
		.penv = penv,
		.qos_list = list_create(xfree_ptr),
	};

	xassert(!parse->field_offset);
	xassert(!*preempt_bitstr);

	if ((data_get_type(src) != DATA_TYPE_LIST) ||
	    (data_list_for_each(src, _foreach_parse_qos_preempt_list, &args) <
	     0)) {
		FREE_NULL_LIST(args.qos_list);
		return ESLURM_REST_FAIL_PARSING;
	}

	*preempt_list = args.qos_list;

	return SLURM_SUCCESS;
}

static int _dump_qos_preempt_list(const parser_t *const parse, void *obj,
				  data_t *dst, const parser_env_t *penv)
{
	bitstr_t **preempt_bitstr =
		(((void *)obj) +
		 parse->per_type.qos_preempt.field_offset_preempt_bitstr);
#ifndef NDEBUG
	List *preempt_list =
		(((void *)obj) +
		 parse->per_type.qos_preempt.field_offset_preempt_list);
#endif

	xassert(!parse->field_offset);
	xassert(!*preempt_list);
	xassert(penv->g_qos_list);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!penv->g_qos_list)
		return ESLURM_NOT_SUPPORTED;

	if (!*preempt_bitstr)
		return SLURM_SUCCESS;

	/* based on get_qos_complete_str_bitstr() */
	for (int i = 0; (i < bit_size(*preempt_bitstr)); i++) {
		slurmdb_qos_rec_t *ptr_qos;

		if (!bit_test(*preempt_bitstr, i))
			continue;

		if (!(ptr_qos = list_find_first(penv->g_qos_list,
						slurmdb_find_qos_in_list,
						&i))) {
			/*
			 * There is a race condition here where the global
			 * QOS list could have changed betwen the query of the
			 * list and the bitstrs. Just error and have the user
			 * try again.
			 */
			error("%s: unable to find QOS with level: %u", __func__,
			      i);
			return ESLURM_DATA_CONV_FAILED;
		}

		data_set_string(data_list_append(dst), ptr_qos->name);
	}

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_PARSE_ASSOC 0xdbed1a13
typedef struct {
	int magic;
	data_t *errors;
	List assoc_list;
	const parser_env_t *penv;
} foreach_parse_assoc_t;

static data_for_each_cmd_t _foreach_parse_assoc(data_t *data, void *arg)
{
	foreach_parse_assoc_t *args = arg;
	slurmdb_assoc_rec_t *assoc;

	xassert(args->magic == MAGIC_FOREACH_PARSE_ASSOC);

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_FAIL;

	assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);
	list_append(args->assoc_list, assoc);

	if (_parser_run(assoc, parse_assoc, ARRAY_SIZE(parse_assoc), data,
			args->errors, args->penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _parse_assoc_list(const parser_t *const parse, void *obj,
			     data_t *src, data_t *errors,
			     const parser_env_t *penv)
{
	List *assoc_list = (((void *)obj) + parse->field_offset);
	foreach_parse_assoc_t assoc_args = {
		.magic = MAGIC_FOREACH_PARSE_ASSOC,
		.assoc_list = *assoc_list,
		.penv = penv,
	};

	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	if (data_list_for_each(src, _foreach_parse_assoc, &assoc_args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_ASSOC 0xfefe2af3
typedef struct {
	int magic;
	data_t *assocs;
	const parser_env_t *penv;
} foreach_assoc_t;

static int _foreach_assoc(void *x, void *arg)
{
	slurmdb_assoc_rec_t *assoc = x;
	foreach_assoc_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_ASSOC);

	if (_parser_dump(assoc, parse_assoc, ARRAY_SIZE(parse_assoc),
			 data_set_dict(data_list_append(args->assocs)),
			 args->penv) < 0)
		return -1;

	return 0;
}

static int _dump_assoc_list(const parser_t *const parse, void *obj, data_t *dst,
			    const parser_env_t *penv)
{
	List *assoc_list = (((void *)obj) + parse->field_offset);
	foreach_assoc_t args = {
		.magic = MAGIC_FOREACH_ASSOC,
		.penv = penv,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	args.assocs = data_set_list(dst);

	if (!*assoc_list)
		return 0;

	if (list_for_each(*assoc_list, _foreach_assoc, &args) < 0)
		return -1;

	return 0;
}

#define MAGIC_FOREACH_PARSE_ASSOC_SHORT 0x8bbd1a00
typedef struct {
	int magic;
	data_t *errors;
	List assoc_list;
	const parser_env_t *penv;
} foreach_parse_assoc_short_t;

static data_for_each_cmd_t _foreach_parse_assoc_short(data_t *data, void *arg)
{
	foreach_parse_assoc_t *args = arg;
	slurmdb_assoc_rec_t *assoc;

	xassert(args->magic == MAGIC_FOREACH_PARSE_ASSOC_SHORT);

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_FAIL;

	assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);
	list_append(args->assoc_list, assoc);

	if (_parser_run(assoc, parse_assoc_short, ARRAY_SIZE(parse_assoc_short),
			data, args->errors, args->penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _parse_assoc_short_list(const parser_t *const parse, void *obj,
				   data_t *src, data_t *errors,
				   const parser_env_t *penv)
{
	List *assoc_list = (((void *)obj) + parse->field_offset);
	foreach_parse_assoc_short_t assoc_args = {
		.magic = MAGIC_FOREACH_PARSE_ASSOC_SHORT,
		.assoc_list = *assoc_list,
		.penv = penv,
	};

	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	if (data_list_for_each(src, _foreach_parse_assoc_short, &assoc_args) <
	    0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_ACCT_SHORT 0xaefeb0f1
typedef struct {
	int magic;
	data_t *accts;
} foreach_acct_short_t;

static int _foreach_acct_short(void *x, void *arg)
{
	slurmdb_account_rec_t *acct = x;
	foreach_acct_short_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_ACCT_SHORT);

	(void)data_set_string(data_list_append(args->accts), acct->name);
	return 0;
}

static int _dump_acct_list(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	List *acct_list = (((void *)obj) + parse->field_offset);
	foreach_acct_short_t args = {
		.magic = MAGIC_FOREACH_ACCT_SHORT,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	args.accts = data_set_list(dst);

	if (!*acct_list)
		return 0;

	if (list_for_each(*acct_list, _foreach_acct_short, &args) < 0)
		return -1;

	return 0;
}

#define MAGIC_FOREACH_ACCT_PARSE_SHORT 0x8eaeb0f1
typedef struct {
	int magic;
	List acct_list;
} foreach_acct_short_parse_t;

static data_for_each_cmd_t _for_each_parse_assoc(data_t *data, void *arg)
{
	foreach_acct_short_parse_t *args = arg;

	if (data_get_type(data) == DATA_TYPE_NULL)
		return DATA_FOR_EACH_FAIL;
	else if (data_convert_type(data, DATA_TYPE_STRING) ==
		 DATA_TYPE_STRING) {
		list_append(args->acct_list, data_get_string(data));
		return DATA_FOR_EACH_CONT;
	}

	return DATA_FOR_EACH_FAIL;
}

static int _parse_acct_list(const parser_t *const parse, void *obj, data_t *src,
			    data_t *errors, const parser_env_t *penv)
{
	List *acct_list = (((void *)obj) + parse->field_offset);
	foreach_acct_short_parse_t args = {
		.magic = MAGIC_FOREACH_ACCT_PARSE_SHORT,
		.acct_list = *acct_list = list_create(xfree_ptr),
	};

	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	if (data_list_for_each(src, _for_each_parse_assoc, &args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_ASSOC_SHORT 0xfefe00f0
typedef struct {
	int magic;
	data_t *assocs;
	const parser_env_t *penv;
} foreach_assoc_short_t;

static int _foreach_assoc_short(void *x, void *arg)
{
	slurmdb_assoc_rec_t *assoc = x;
	foreach_assoc_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_ASSOC_SHORT);

	if (_parser_dump(assoc, parse_assoc_short,
			 ARRAY_SIZE(parse_assoc_short),
			 data_set_dict(data_list_append(args->assocs)),
			 args->penv) < 0)
		return -1;

	return 0;
}

static int _dump_assoc_short_list(const parser_t *const parse, void *obj,
				  data_t *dst, const parser_env_t *penv)
{
	List *assoc_list = (((void *)obj) + parse->field_offset);
	foreach_assoc_short_t args = {
		.magic = MAGIC_FOREACH_ASSOC_SHORT,
		.penv = penv,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	args.assocs = data_set_list(dst);

	if (!*assoc_list)
		return 0;

	if (list_for_each(*assoc_list, _foreach_assoc_short, &args) < 0)
		return -1;

	return 0;
}

#define MAGIC_FIND_ASSOC 0xa8ba2c18
typedef struct {
	int magic;
	slurmdb_assoc_rec_t *assoc;
} find_assoc_id_t;

/* checks for mis-matches and rejects on the spot */
#define _match(field)                                              \
	do {                                                       \
		/* both null */                                    \
		if (!args->assoc->field && !assoc->field)          \
			continue;                                  \
		/* only  1 is null */                              \
		if (!args->assoc->field != !assoc->field)          \
			return 0;                                  \
		if (xstrcasecmp(args->assoc->field, assoc->field)) \
			return 0;                                  \
	} while (0);

static int _find_assoc_id(void *x, void *key)
{
	slurmdb_assoc_rec_t *assoc = x;
	find_assoc_id_t *args = key;

	xassert(args->magic == MAGIC_FIND_ASSOC);

	if ((args->assoc->id > 0) && (args->assoc->id == assoc->id))
		return 1;

	_match(acct);
	_match(cluster);
	_match(cluster);
	_match(partition);
	_match(user);

	return 1;
}
#undef _match

static int _parse_assoc_id(const parser_t *const parse, void *obj, data_t *src,
			   data_t *errors, const parser_env_t *penv)
{
	int rc = SLURM_SUCCESS;
	uint32_t *associd = (((void *)obj) + parse->field_offset);
	slurmdb_assoc_rec_t *assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);

	rc = _parser_run(assoc, parse_assoc_short,
			 ARRAY_SIZE(parse_assoc_short), src, errors, penv);

	if (!rc) {
		find_assoc_id_t args = {
			.magic = MAGIC_FIND_ASSOC,
			.assoc = assoc,
		};
		slurmdb_assoc_rec_t *match = list_find_first(
			penv->g_assoc_list, _find_assoc_id, &args);

		if (match)
			*associd = match->id;
		else
			rc = ESLURM_REST_EMPTY_RESULT;
	}

	slurmdb_destroy_assoc_rec(assoc);

	return rc;
}

static int _dump_assoc_id(const parser_t *const parse, void *obj, data_t *dst,
			  const parser_env_t *penv)
{
	uint32_t *associd = (((void *)obj) + parse->field_offset);
	slurmdb_assoc_rec_t *assoc = NULL;

	if (!*associd || (*associd == NO_VAL))
		return SLURM_SUCCESS;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(penv->g_assoc_list);

	if (!(assoc = list_find_first(penv->g_assoc_list,
				      slurmdb_find_assoc_in_list, associd)))
		return ESLURM_DATA_CONV_FAILED;

	return _parser_dump(assoc, parse_assoc_short,
			    ARRAY_SIZE(parse_assoc_short), dst, penv);
}

static int _parse_tres(const parser_t *const parse, void *obj, data_t *src,
		       data_t *errors, const parser_env_t *penv)
{
	slurmdb_tres_rec_t **tres = (((void *)obj) + parse->field_offset);

	xassert(!parse->field_offset);

	if (!penv->g_tres_list) {
		xassert(penv->g_tres_list);
		return ESLURM_NOT_SUPPORTED;
	}

	if (data_get_type(src) != DATA_TYPE_DICT)
		return ESLURM_REST_FAIL_PARSING;

	return _parser_run(*tres, parse_tres, ARRAY_SIZE(parse_tres), src,
			   errors, penv);
}

static int _dump_tres(const parser_t *const parse, void *obj, data_t *dst,
		      const parser_env_t *penv)
{
	slurmdb_tres_rec_t **tres = (((void *)obj) + parse->field_offset);

	return _parser_dump(*tres, parse_tres, ARRAY_SIZE(parse_tres), dst,
			    penv);
}

#define MAGIC_FOREACH_PARSE_TRES_COUNT 0xfbba2c18
typedef struct {
	int magic;
	List tres;
	data_t *errors;
	const parser_env_t *penv;
} for_each_parse_tres_t;

#define MAGIC_FIND_TRES 0xf4ba2c18
typedef struct {
	int magic;
	slurmdb_tres_rec_t *tres;
} find_tres_id_t;

static int _find_tres_id(void *x, void *key)
{
	find_tres_id_t *args = key;
	slurmdb_tres_rec_t *tres = x;

	xassert(args->magic == MAGIC_FIND_TRES);

	if ((args->tres->id > 0) && args->tres->id == tres->id)
		return 1;
	if ((!args->tres->name || !args->tres->name[0]) &&
	    !xstrcasecmp(args->tres->type, tres->type))
		return 1;
	else if (!xstrcasecmp(args->tres->name, tres->name) &&
		 !xstrcasecmp(args->tres->type, tres->type))
		return 1;
	else
		return 0;
}

static data_for_each_cmd_t _for_each_parse_tres_count(data_t *data, void *arg)
{
	for_each_parse_tres_t *args = arg;
	slurmdb_tres_rec_t *tres, *ftres;
	data_t *errors = args->errors;
	find_tres_id_t targs = {
		.magic = MAGIC_FIND_TRES,
	};

	xassert(args->magic == MAGIC_FOREACH_PARSE_TRES_COUNT);

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_FAIL;

	tres = xmalloc(sizeof(*tres));
	(void)list_append(args->tres, tres);

	if (_parser_run(tres, parse_tres, ARRAY_SIZE(parse_tres), data,
			args->errors, args->penv))
		return DATA_FOR_EACH_FAIL;

	if (tres->count < 0) {
		resp_error(errors, ESLURM_REST_FAIL_PARSING,
			   "TRES count below 0", "count");
		return DATA_FOR_EACH_FAIL;
	}

	targs.tres = tres;

	/* Lookup from g_tres_list */
	if ((ftres = list_find_first(args->penv->g_tres_list, _find_tres_id,
				     &targs))) {
		if ((tres->id > 0) && tres->id != ftres->id) {
			resp_error(errors, ESLURM_INVALID_TRES,
				   "TRES id unknown", "id");
			return DATA_FOR_EACH_FAIL;
		}

		if (!tres->id)
			tres->id = ftres->id;
	}

	return DATA_FOR_EACH_CONT;
}

static int _parse_tres_list(const parser_t *const parse, void *obj, data_t *src,
			    data_t *errors, const parser_env_t *penv)
{
	char **tres = (((void *)obj) + parse->field_offset);
	for_each_parse_tres_t args = {
		.magic = MAGIC_FOREACH_PARSE_TRES_COUNT,
		.penv = penv,
		.tres = list_create(slurmdb_destroy_tres_rec),
		.errors = errors,
	};

	if (!penv->g_tres_list) {
		xassert(penv->g_tres_list);
		return ESLURM_NOT_SUPPORTED;
	}

	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	if (data_list_for_each(src, _for_each_parse_tres_count, &args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	if ((*tres = slurmdb_make_tres_string(args.tres, TRES_STR_FLAG_SIMPLE)))
		return SLURM_SUCCESS;
	else
		return ESLURM_REST_FAIL_PARSING;
}

#define MAGIC_LIST_PER_TRES 0xf7f8baf0
typedef struct {
	int magic;
	data_t *tres;
	const parser_env_t *penv;
} foreach_list_per_tres_t;

static int _dump_tres_list_tres(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = (slurmdb_tres_rec_t *)x;
	foreach_list_per_tres_t *args = arg;

	if (!tres->type && tres->id) {
		slurmdb_tres_rec_t *c = list_find_first(
			args->penv->g_tres_list, slurmdb_find_tres_in_list,
			&tres->id);

		if (c) {
			tres->type = xstrdup(c->type);
			tres->name = xstrdup(c->name);
		}
	}

	if (_parser_dump(tres, parse_tres, ARRAY_SIZE(parse_tres),
			 data_set_dict(data_list_append(args->tres)),
			 args->penv))
		return -1;

	return 0;
}

static int _dump_tres_list(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	char **tres = (((void *)obj) + parse->field_offset);
	List tres_list = NULL;
	foreach_list_per_tres_t args = {
		.magic = MAGIC_LIST_PER_TRES,
		.tres = data_set_list(dst),
		.penv = penv,
	};

	xassert(penv->g_tres_list);
	if (!penv->g_tres_list)
		return ESLURM_NOT_SUPPORTED;

	if (!*tres || !*tres[0])
		/* ignore empty TRES strings */
		return SLURM_SUCCESS;

	slurmdb_tres_list_from_string(&tres_list, *tres, TRES_STR_FLAG_BYTES);

	if (!tres_list)
		return ESLURM_DATA_CONV_FAILED;

	list_for_each(tres_list, _dump_tres_list_tres, &args);

	FREE_NULL_LIST(tres_list);

	return SLURM_SUCCESS;
}
/* based on slurmdb_tres_rec_t but includes node and task */
typedef struct {
	uint64_t count;
	char *node;
	uint64_t task;
	uint32_t id;
	char *name;
	char *type;
} slurmdb_tres_nct_rec_t;

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_tres_nct_rec_t, mtype, false, field, path)
#define _add_parse_req(mtype, field, path) \
	add_parser(slurmdb_tres_nct_rec_t, mtype, true, field, path)
/* should mirror the structure of slurmdb_tres_nct_rec_t  */
static const parser_t parse_tres_nct[] = {
	_add_parse_req(STRING, type, "type"), _add_parse(STRING, name, "name"),
	_add_parse(UINT32, id, "id"),	      _add_parse(INT64, count, "count"),
	_add_parse(INT64, task, "task"),      _add_parse(STRING, node, "node"),
};
#undef _add_parse
#undef _add_parse_req

typedef enum {
	TRES_EXPLODE_COUNT = 1,
	TRES_EXPLODE_NODE,
	TRES_EXPLODE_TASK,
} tres_explode_type_t;

#define MAGIC_LIST_PER_TRES_TYPE_NCT 0xb1d8acd2
typedef struct {
	int magic;
	tres_explode_type_t type;
	slurmdb_tres_nct_rec_t *tres_nct;
	int tres_nct_count;
	hostlist_t host_list;
} foreach_list_per_tres_type_nct_t;

static int _foreach_list_per_tres_type_nct(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = (slurmdb_tres_rec_t *)x;
	foreach_list_per_tres_type_nct_t *args = arg;
	slurmdb_tres_nct_rec_t *tres_nct = NULL;

	xassert(args->magic == MAGIC_LIST_PER_TRES_TYPE_NCT);

	for (int i = 0; i < args->tres_nct_count; i++)
		if (args->tres_nct[i].id == tres->id)
			tres_nct = args->tres_nct + i;

	xassert(tres_nct);
	if (!tres_nct)
		/* out of sync?? */
		return -1;

	switch (args->type) {
	case TRES_EXPLODE_NODE:
		xassert(!tres_nct->node);
		free(tres_nct->node);
		/* based on find_hostname() */
		tres_nct->node = hostlist_nth(args->host_list, tres->count);
		return 1;
	case TRES_EXPLODE_TASK:
		xassert(!tres_nct->task);
		tres_nct->task = tres->count;
		return 1;
	case TRES_EXPLODE_COUNT:
		xassert(!tres_nct->count);
		tres_nct->count = tres->count;
		return 1;
	default:
		fatal("%s: unexpected type", __func__);
	}
}

#define MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST 0x31b8aad2
typedef struct {
	int magic;
	slurmdb_tres_nct_rec_t *tres_nct;
	int tres_nct_count;
	int offset;
} foreach_populate_g_tres_list;

static int _foreach_populate_g_tres_list(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = x;
	foreach_populate_g_tres_list *args = arg;
	slurmdb_tres_nct_rec_t *tres_nct = args->tres_nct + args->offset;

	xassert(args->magic == MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST);

	tres_nct->id = tres->id;
	tres_nct->name = tres->name;
	tres_nct->type = tres->type;

	xassert(args->offset < args->tres_nct_count);
	args->offset += 1;
	return 0;
}

static int _dump_tres_nct(const parser_t *const parse, void *obj, data_t *dst,
			  const parser_env_t *penv)
{
	int rc = ESLURM_DATA_CONV_FAILED;
	foreach_list_per_tres_type_nct_t args = {
		.magic = MAGIC_LIST_PER_TRES_TYPE_NCT,
	};
	foreach_populate_g_tres_list gtres_args = {
		.magic = MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST,
	};
	slurmdb_tres_nct_rec_t *tres_nct = NULL;
	int tres_nct_count = 0;
	char **tres_count = (((void *)obj) +
			     parse->per_type.tres.field_offset_count);
	char **tres_node = (((void *)obj) +
			    parse->per_type.tres.field_offset_node);
	char **tres_task = (((void *)obj) +
			    parse->per_type.tres.field_offset_task);
	char **nodes = (((void *)obj) +
			parse->per_type.tres.field_offset_nodes);
	List tres_count_list = NULL;
	List tres_node_list = NULL;
	List tres_task_list = NULL;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	xassert(!parse->field_offset);
	xassert(penv->g_tres_list);
	if (!penv->g_tres_list)
		goto cleanup;

	if (!*tres_count && !*tres_node && !*tres_task)
		/* ignore empty TRES strings */
		goto cleanup;

	args.tres_nct_count = gtres_args.tres_nct_count = tres_nct_count =
		list_count(penv->g_tres_list);
	args.tres_nct = gtres_args.tres_nct = tres_nct = xcalloc(
		list_count(penv->g_tres_list), sizeof(*tres_nct));
	if (list_for_each(penv->g_tres_list, _foreach_populate_g_tres_list,
			  &gtres_args) < 0)
		goto cleanup;

	args.host_list = hostlist_create(*nodes);

	slurmdb_tres_list_from_string(&tres_count_list, *tres_count,
				      TRES_STR_FLAG_BYTES);
	slurmdb_tres_list_from_string(&tres_node_list, *tres_node,
				      TRES_STR_FLAG_BYTES);
	slurmdb_tres_list_from_string(&tres_task_list, *tres_task,
				      TRES_STR_FLAG_BYTES);

	args.type = TRES_EXPLODE_COUNT;
	if (tres_count_list &&
	    (list_for_each(tres_count_list, _foreach_list_per_tres_type_nct,
			   &args) < 0))
		goto cleanup;
	args.type = TRES_EXPLODE_NODE;
	if (tres_node_list &&
	    (list_for_each(tres_node_list, _foreach_list_per_tres_type_nct,
			   &args) < 0))
		goto cleanup;
	args.type = TRES_EXPLODE_TASK;
	if (tres_task_list &&
	    (list_for_each(tres_task_list, _foreach_list_per_tres_type_nct,
			   &args) < 0))
		goto cleanup;
	xassert(!(args.type = 0));

	for (int i = 0; i < tres_nct_count; i++)
		if (tres_nct[i].count || tres_nct[i].node || tres_nct[i].task)
			_parser_dump((tres_nct + i), parse_tres_nct,
				     ARRAY_SIZE(parse_tres_nct),
				     data_set_dict(data_list_append(dst)),
				     penv);

	rc = SLURM_SUCCESS;
cleanup:
	FREE_NULL_LIST(tres_count_list);
	FREE_NULL_LIST(tres_node_list);
	FREE_NULL_LIST(tres_task_list);
	FREE_NULL_HOSTLIST(args.host_list);
	for (int i = 0; i < tres_nct_count; i++)
		/* hostlist_nth doesn't use xfree() */
		free(tres_nct[i].node);
	xfree(tres_nct);

	return rc;
}

static int _parse_admin_lvl(const parser_t *const parse, void *obj, data_t *src,
			    data_t *errors, const parser_env_t *penv)
{
	uint16_t *admin_level = (((void *)obj) + parse->field_offset);
	xassert(!(0xffff0000 & *admin_level));

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return ESLURM_REST_FAIL_PARSING;

	*admin_level = str_2_slurmdb_admin_level(data_get_string(src));

	if (*admin_level == SLURMDB_ADMIN_NOTSET)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

static int _dump_admin_lvl(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	uint16_t *admin_level = (((void *)obj) + parse->field_offset);

	(void)data_set_string(dst, slurmdb_admin_level_str(*admin_level));

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_PARSE_COORD 0xdeed1a14
typedef struct {
	int magic;
	data_t *errors;
	List coord_list;
	const parser_env_t *penv;
} foreach_update_coord_t;

static data_for_each_cmd_t _foreach_update_coord(data_t *data, void *arg)
{
	foreach_update_coord_t *args = arg;
	slurmdb_coord_rec_t *coord;

	xassert(args->magic == MAGIC_FOREACH_PARSE_COORD);

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_FAIL;

	coord = xmalloc(sizeof(*coord));
	list_append(args->coord_list, coord);

	if (_parser_run(coord, parse_coord, ARRAY_SIZE(parse_coord), data,
			args->errors, args->penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _parse_coord_list(const parser_t *const parse, void *obj,
			     data_t *src, data_t *errors,
			     const parser_env_t *penv)
{
	List *coord_list = (((void *)obj) + parse->field_offset);
	foreach_update_coord_t coord_args = {
		.magic = MAGIC_FOREACH_PARSE_COORD,
		.coord_list = *coord_list,
		.penv = penv,
	};

	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	if (data_list_for_each(src, _foreach_update_coord, &coord_args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_COORDINATOR 0xaefef2f5
typedef struct {
	int magic;
	data_t *coordinators;
	const parser_env_t *penv;
} foreach_coordinator_t;

static int _foreach_coordinator(void *x, void *arg)
{
	slurmdb_coord_rec_t *coor = x;
	foreach_coordinator_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_COORDINATOR);

	if (_parser_dump(coor, parse_coord, ARRAY_SIZE(parse_coord),
			 data_set_dict(data_list_append(args->coordinators)),
			 args->penv))
		return -1;

	return 0;
}

static int _dump_coord_list(const parser_t *const parse, void *obj, data_t *dst,
			    const parser_env_t *penv)
{
	List *coord_list = (((void *)obj) + parse->field_offset);
	foreach_coordinator_t args = {
		.magic = MAGIC_FOREACH_COORDINATOR,
		.coordinators = data_set_list(dst),
		.penv = penv,
	};

	if (list_for_each(*coord_list, _foreach_coordinator, &args) < 0)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}
#define MAGIC_FOREACH_STEP 0x7e2eaef1
typedef struct {
	int magic;
	data_t *steps;
	const parser_env_t *penv;
} foreach_step_t;

static int _foreach_step(void *x, void *arg)
{
	int rc = 1;
	slurmdb_step_rec_t *step = x;
	foreach_step_t *args = arg;
	data_t *dstep = data_set_dict(data_list_append(args->steps));

	xassert(args->magic == MAGIC_FOREACH_STEP);

	hostlist_t host_list = hostlist_create(step->nodes);
	if (!host_list) {
		rc = -1;
		goto cleanup;
	}

	xassert(hostlist_count(host_list) == step->nnodes);
	if (!rc && hostlist_count(host_list)) {
		char *host;
		data_t *d = data_set_list(
			data_define_dict_path(dstep, "nodes/list"));
		hostlist_iterator_t itr = hostlist_iterator_create(host_list);

		while ((host = hostlist_next(itr)))
			data_set_string(data_list_append(d), host);

		hostlist_iterator_destroy(itr);
	}

	if (_parser_dump(step, parse_job_step, ARRAY_SIZE(parse_job_step),
			 dstep, args->penv))
		rc = -1;
cleanup:
	FREE_NULL_HOSTLIST(host_list);

	return rc;
}

static int _dump_job_steps(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	foreach_step_t args = {
		.magic = MAGIC_FOREACH_STEP,
		.steps = data_set_list(dst),
		.penv = penv,
	};
	List *steps = (((void *)obj) + parse->field_offset);

	if (list_for_each(*steps, _foreach_step, &args) < 0)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

static int _dump_job_exit_code(const parser_t *const parse, void *obj,
			       data_t *dst, const parser_env_t *penv)
{
	uint32_t *ec = (((void *)obj) + parse->field_offset);
	data_t *drc, *dsc;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	(void)data_set_dict(dst);

	dsc = data_key_set(dst, "status");
	drc = data_key_set(dst, "return_code");

	if (*ec == NO_VAL)
		data_set_string(dsc, "PENDING");
	else if (WIFEXITED(*ec)) {
		data_set_string(dsc, "SUCCESS");
		data_set_int(drc, 0);
	} else if (WIFSIGNALED(*ec)) {
		data_t *sig = data_set_dict(data_key_set(dst, "signal"));
		data_set_string(dsc, "SIGNALED");

		data_set_int(data_key_set(sig, "signal_id"), WTERMSIG(*ec));
		data_set_string(data_key_set(sig, "name"),
				strsignal(WTERMSIG(*ec)));
	} else if (WCOREDUMP(*ec)) {
		data_set_string(dsc, "CORE_DUMPED");
	} else {
		data_set_string(dsc, "ERROR");
		data_set_int(drc, WEXITSTATUS(*ec));
	}

	return SLURM_SUCCESS;
}

#define _add_parse(mtype, field, path) \
	add_parser(slurmdb_assoc_usage_t, mtype, false, field, path)
/* should mirror the structure of slurmdb_assoc_usage_t */
static const parser_t parse_assoc_usage[] = {
	_add_parse(UINT32, accrue_cnt, "accrue_job_count"),
	/* skipping children_list (not packed) */
	/* skipping grp_node_bitmap (not packed) */
	/* skipping grp_node_job_cnt (not packed) */
	/* skipping grp_used_tres (not packed) */
	/* skipping grp_used_tres_run_secs (not packed) */
	_add_parse(FLOAT64, grp_used_wall, "group_used_wallclock"),
	_add_parse(FLOAT64, fs_factor, "fairshare_factor"),
	_add_parse(UINT32, level_shares, "fairshare_shares"),
	/* skipping parent_assoc_ptr (not packed) */
	_add_parse(FLOAT64, priority_norm, "normalized_priority"),
	/* skipping fs_assoc_ptr (not packed) */
	_add_parse(FLOAT128, shares_norm, "normalized_shares"),
	/* skipping tres_count (not packed) */
	_add_parse(FLOAT64, usage_efctv, "effective_normalized_usage"),
	_add_parse(FLOAT64, usage_norm, "normalized_usage"),
	_add_parse(UINT64, usage_raw, "raw_usage"),
	/* skipping fs_assoc_ptr (not packed) */
	/* skipping raw_TRES_usage (not packed) */
	_add_parse(UINT32, used_jobs, "active_jobs"),
	_add_parse(UINT32, used_submit_jobs, "job_count"),
	_add_parse(FLOAT64, level_fs, "fairshare_level"),
	/* skipping valid_qos */
};
#undef _add_parse

static int _parse_assoc_usage(const parser_t *const parse, void *obj,
			      data_t *src, data_t *errors,
			      const parser_env_t *penv)
{
	slurmdb_assoc_rec_t *usage = (((void *)obj) + parse->field_offset);

	if (data_get_type(src) != DATA_TYPE_DICT)
		return ESLURM_REST_FAIL_PARSING;

	return _parser_run(usage, parse_assoc_usage,
			   ARRAY_SIZE(parse_assoc_usage), src, errors, penv);
}

static int _dump_assoc_usage(const parser_t *const parse, void *obj,
			     data_t *dst, const parser_env_t *penv)
{
	slurmdb_assoc_rec_t **usage = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*usage)
		return _parser_dump(*usage, parse_assoc_usage,
				    ARRAY_SIZE(parse_assoc_usage),
				    data_set_dict(dst), penv);

	return SLURM_SUCCESS;
}

static int _dump_stats_rec_array(const parser_t *const parse, void *obj,
				 data_t *dst, const parser_env_t *penv)
{
	slurmdb_rollup_stats_t **ptr_stats = (((void *)obj) +
					      parse->field_offset);
	slurmdb_rollup_stats_t *rollup_stats;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	if (!(rollup_stats = *ptr_stats))
		return ESLURM_DATA_CONV_FAILED;

	for (int i = 0; i < DBD_ROLLUP_COUNT; i++) {
		data_t *d;
		uint64_t roll_ave;

		if (rollup_stats->time_total[i] == 0)
			continue;

		d = data_set_dict(data_list_append(dst));

		if (i == 0)
			data_set_string(data_key_set(d, "type"), "internal");
		else if (i == 1)
			data_set_string(data_key_set(d, "type"), "user");
		else
			data_set_string(data_key_set(d, "type"), "unknown");

		data_set_int(data_key_set(d, "last_run"),
			     rollup_stats->timestamp[i]);

		roll_ave = rollup_stats->time_total[i];
		if (rollup_stats->count[i] > 1)
			roll_ave /= rollup_stats->count[i];

		data_set_int(data_key_set(d, "last_cycle"),
			     rollup_stats->time_last[i]);
		data_set_int(data_key_set(d, "max_cycle"),
			     rollup_stats->time_max[i]);
		data_set_int(data_key_set(d, "total_time"),
			     rollup_stats->time_total[i]);
		data_set_int(data_key_set(d, "total_cycles"),
			     rollup_stats->count[i]);
		data_set_int(data_key_set(d, "mean_cycles"), roll_ave);
	}

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_STATS_RPC 0x8a2e3ef1
typedef struct {
	int magic;
	const parser_env_t *penv;
	data_t *rpcs;
} foreach_stats_rpc_t;

static int _foreach_stats_rpc(void *x, void *arg)
{
	slurmdb_rpc_obj_t *rpc_obj = (slurmdb_rpc_obj_t *)x;
	foreach_stats_rpc_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_STATS_RPC);

	if (_parser_dump(
		    rpc_obj, parse_stats_rpcs, ARRAY_SIZE(parse_stats_rpcs),
		    data_set_dict(data_list_append(args->rpcs)), args->penv))
		return -1;

	return 0;
}

static int _dump_stats_rpc_list(const parser_t *const parse, void *obj,
				data_t *dst, const parser_env_t *penv)
{
	List *rpc_list = (((void *)obj) + parse->field_offset);
	foreach_stats_rpc_t args = {
		.magic = MAGIC_FOREACH_STATS_RPC,
		.penv = penv,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	args.rpcs = data_set_list(dst);

	if (list_for_each(*rpc_list, _foreach_stats_rpc, &args) < 0)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_STATS_USER_RPC 0x8a2e3ef3
typedef struct {
	int magic;
	const parser_env_t *penv;
	data_t *users;
} foreach_stats_user_rpc_t;

static int _foreach_stats_user_rpc(void *x, void *arg)
{
	slurmdb_rpc_obj_t *rpc_obj = (slurmdb_rpc_obj_t *)x;
	foreach_stats_user_rpc_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_STATS_USER_RPC);

	if (_parser_dump(rpc_obj, parse_stats_user_rpcs,
			 ARRAY_SIZE(parse_stats_user_rpcs),
			 data_set_dict(data_list_append(args->users)),
			 args->penv))
		return -1;

	return 0;
}

static int _dump_stats_user_list(const parser_t *const parse, void *obj,
				 data_t *dst, const parser_env_t *penv)
{
	List *user_list = (((void *)obj) + parse->field_offset);
	foreach_stats_user_rpc_t args = {
		.magic = MAGIC_FOREACH_STATS_USER_RPC,
		.penv = penv,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	args.users = data_set_list(dst);

	if (list_for_each(*user_list, _foreach_stats_user_rpc, &args))
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

static int _dump_rpc_id(const parser_t *const parse, void *obj, data_t *dst,
			const parser_env_t *penv)
{
	slurmdbd_msg_type_t *id = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, slurmdbd_msg_type_2_str(*id, 1));

	return SLURM_SUCCESS;
}

static int _dump_clust_acct_rec(const parser_t *const parse, void *obj,
				data_t *dst, const parser_env_t *penv)
{
	slurmdb_cluster_accounting_rec_t *acct = (((void *)obj) +
						  parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	if (!acct)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

static int _parse_clust_acct_rec_list(const parser_t *const parse, void *obj,
				      data_t *src, data_t *errors,
				      const parser_env_t *penv)
{
	if (data_get_type(src) != DATA_TYPE_LIST)
		return ESLURM_REST_FAIL_PARSING;

	/*
	 * List of stats: slurmdb_cluster_accounting_rec_t
	 * This can not be ingested, so we will ignore it.
	 */
	debug("%s: ignoring slurmdb_cluster_accounting_rec_t", __func__);

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_ACCT_REC 0xa22e3ef3
typedef struct {
	int magic;
	const parser_env_t *penv;
	data_t *list;
} _foreach_clust_acct_rec_t;

static int _foreach_clust_acct_rec(void *x, void *arg)
{
	slurmdb_cluster_accounting_rec_t *obj = x;
	_foreach_clust_acct_rec_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_ACCT_REC);

	if (_parser_dump(obj, parse_cluster_accounting_rec,
			 ARRAY_SIZE(parse_cluster_accounting_rec),
			 data_set_dict(data_list_append(args->list)),
			 args->penv))
		return -1;

	return 0;
}

static int _dump_clust_acct_rec_list(const parser_t *const parse, void *obj,
				     data_t *dst, const parser_env_t *penv)
{
	List *acct_list = (((void *)obj) + parse->field_offset);
	_foreach_clust_acct_rec_t args = {
		.magic = MAGIC_FOREACH_ACCT_REC,
		.penv = penv,
		.list = dst,
	};

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!acct_list)
		return ESLURM_REST_FAIL_PARSING;

	data_set_list(dst);

	if (list_for_each(*acct_list, _foreach_clust_acct_rec, &args) < 0)
		return ESLURM_REST_FAIL_PARSING;

	return SLURM_SUCCESS;
}

static int _parse_select_plugin_id(const parser_t *const parse, void *obj,
				   data_t *src, data_t *errors,
				   const parser_env_t *penv)
{
	int *id = (((void *)obj) + parse->field_offset);

	if (data_get_type(src) == DATA_TYPE_NULL)
		return ESLURM_REST_FAIL_PARSING;
	else if (data_convert_type(src, DATA_TYPE_STRING) == DATA_TYPE_STRING &&
		 (*id = select_string_to_plugin_id(data_get_string(src)) > 0))
		return SLURM_SUCCESS;

	return ESLURM_REST_FAIL_PARSING;
}

static int _dump_select_plugin_id(const parser_t *const parse, void *obj,
				  data_t *dst, const parser_env_t *penv)
{
	int *id = (((void *)obj) + parse->field_offset);
	char *s = select_plugin_id_to_string(*id);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (s) {
		data_set_string(dst, s);
	} else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

static int _dump_task_distribution(const parser_t *const parse, void *obj,
				   data_t *dst, const parser_env_t *penv)
{
	uint32_t *dist = (((void *)obj) + parse->field_offset);
	char *d = slurm_step_layout_type_name(*dist);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string_own(dst, d);

	return SLURM_SUCCESS;
}

static int _dump_step_id(const parser_t *const parse, void *obj, data_t *dst,
			 const parser_env_t *penv)
{
	uint32_t *id = (((void *)obj) + parse->field_offset);

	// TODO rewrite after bug#9622 resolved

	switch (*id) {
	case SLURM_EXTERN_CONT:
		data_set_string(dst, "extern");
		break;
	case SLURM_BATCH_SCRIPT:
		data_set_string(dst, "batch");
		break;
	case SLURM_PENDING_STEP:
		data_set_string(dst, "pending");
		break;
	case SLURM_INTERACTIVE_STEP:
		data_set_string(dst, "interactive");
		break;
	default:
		data_set_int(dst, *id);
	}

	return SLURM_SUCCESS;
}

static int _dump_wckey_tag(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	char **src = (((void *)obj) + parse->field_offset);
	data_t *flags, *key;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!*src) {
		data_set_null(dst);
		return SLURM_SUCCESS;
	}

	key = data_key_set(data_set_dict(dst), "wckey");
	flags = data_set_list(data_key_set(dst, "flags"));

	if (*src[0] == '*') {
		data_set_string(data_list_append(flags), "ASSIGNED_DEFAULT");
		data_set_string(key, (*src + 1));
	} else
		data_set_string(key, *src);

	return SLURM_SUCCESS;
}

static int _dump_user_id(const parser_t *const parse, void *obj, data_t *dst,
			 const parser_env_t *penv)
{
	uid_t *uid = (((void *)obj) + parse->field_offset);
	char *u;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((u = uid_to_string_or_null(*uid)))
		data_set_string_own(dst, u);
	else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

static int _parse_user_id(const parser_t *const parse, void *obj, data_t *src,
			  data_t *errors, const parser_env_t *penv)
{
	uid_t *uid = (((void *)obj) + parse->field_offset);

	if (data_get_type(src) == DATA_TYPE_NULL)
		return ESLURM_REST_FAIL_PARSING;
	else if (data_convert_type(src, DATA_TYPE_STRING) == DATA_TYPE_STRING &&
		 !uid_from_string(data_get_string(src), uid))
		return SLURM_SUCCESS;

	return ESLURM_REST_FAIL_PARSING;
}

static int _dump_group_id(const parser_t *const parse, void *obj, data_t *dst,
			  const parser_env_t *penv)
{
	gid_t *gid = (((void *)obj) + parse->field_offset);
	char *g;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((g = gid_to_string_or_null(*gid)))
		data_set_string_own(dst, g);
	else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

static int _dump_job_reason(const parser_t *const parse, void *obj, data_t *dst,
			    const parser_env_t *penv)
{
	uint32_t *state = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_string(dst, job_reason_string(*state));

	return SLURM_SUCCESS;
}

static int _dump_job_state(const parser_t *const parse, void *obj, data_t *dst,
			   const parser_env_t *penv)
{
	uint32_t *state = (((void *)obj) + parse->field_offset);

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_string(dst, job_state_string(*state));

	return SLURM_SUCCESS;
}

typedef struct {
	parse_rfunc_t rfunc;
	parse_wfunc_t wfunc;
	parser_type_t type;
} parser_funcs_t;

#define _add_func(rfuncp, wfuncp, typev)                        \
	{                                                       \
		.rfunc = rfuncp, .wfunc = wfuncp, .type = typev \
	}
const parser_funcs_t funcs[] = {
	_add_func(_parse_to_string, _dump_to_string, PARSE_STRING),
	_add_func(_parse_to_uint32, _dump_to_uint32, PARSE_UINT32),
	_add_func(_parse_to_int64, _dump_to_int64, PARSE_INT64),
	_add_func(_parse_to_uint64, _dump_to_uint64, PARSE_UINT64),
	_add_func(_parse_to_uint16, _dump_to_uint16, PARSE_UINT16),
	_add_func(_parse_flags, _dump_flags, PARSE_FLAGS),
	_add_func(_parse_qos_str_id, _dump_qos_str_id, PARSE_QOS_ID),
	_add_func(_parse_qos_str_list, _dump_qos_str_list, PARSE_QOS_STR_LIST),
	_add_func(_parse_qos_preempt_list, _dump_qos_preempt_list,
		  PARSE_QOS_PREEMPT_LIST),
	_add_func(_parse_tres, _dump_tres, PARSE_TRES),
	_add_func(_parse_tres_list, _dump_tres_list, PARSE_TRES_LIST),
	_add_func(NULL, _dump_tres_nct, PARSE_TRES_NODE_COUNT_TASK),
	_add_func(NULL, _dump_job_steps, PARSE_JOB_STEPS),
	_add_func(NULL, _dump_job_exit_code, PARSE_JOB_EXIT_CODE),
	_add_func(_parse_admin_lvl, _dump_admin_lvl, PARSE_ADMIN_LVL),
	_add_func(_parse_acct_list, _dump_acct_list, PARSE_ACCOUNT_LIST),
	_add_func(_parse_assoc_list, _dump_assoc_list, PARSE_ASSOC_LIST),
	_add_func(_parse_assoc_short_list, _dump_assoc_short_list,
		  PARSE_ASSOC_SHORT_LIST),
	_add_func(_parse_assoc_usage, _dump_assoc_usage, PARSE_ASSOC_USAGE),
	_add_func(_parse_assoc_id, _dump_assoc_id, PARSE_ASSOC_ID),
	_add_func(_parse_coord_list, _dump_coord_list, PARSE_COORD_LIST),
	_add_func(_parse_to_float64, _dump_to_float64, PARSE_FLOAT64),
	_add_func(_parse_to_float128, _dump_to_float128, PARSE_FLOAT128),
	_add_func(NULL, _dump_stats_rec_array, PARSE_STATS_REC_ARRAY),
	_add_func(NULL, _dump_stats_rpc_list, PARSE_STATS_RPC_LIST),
	_add_func(NULL, _dump_stats_user_list, PARSE_STATS_USER_LIST),
	_add_func(NULL, _dump_rpc_id, PARSE_RPC_ID),
	_add_func(NULL, _dump_clust_acct_rec, PARSE_CLUSTER_ACCT_REC),
	_add_func(_parse_clust_acct_rec_list, _dump_clust_acct_rec_list,
		  PARSE_CLUSTER_ACCT_REC_LIST),
	_add_func(_parse_select_plugin_id, _dump_select_plugin_id,
		  PARSE_SELECT_PLUGIN_ID),
	_add_func(NULL, _dump_task_distribution, PARSE_TASK_DISTRIBUTION),
	_add_func(NULL, _dump_step_id, PARSE_STEP_ID),
	_add_func(NULL, _dump_wckey_tag, PARSE_WCKEY_TAG),
	_add_func(NULL, _dump_group_id, PARSE_GROUP_ID),
	_add_func(NULL, _dump_job_reason, PARSE_JOB_REASON),
	_add_func(NULL, _dump_job_state, PARSE_JOB_STATE),
	_add_func(_parse_user_id, _dump_user_id, PARSE_USER_ID),
};
#undef _add_func

#define _add_parser(parser, typev)                        \
	{                                                 \
		.type = typev, .parse = parser,           \
		.parse_member_count = ARRAY_SIZE(parser), \
	}
const parsers_t parsers[] = {
	_add_parser(parse_assoc_short, PARSE_ASSOC_SHORT),
	_add_parser(parse_assoc, PARSE_ASSOC),
	_add_parser(parse_job_step, PARSE_JOB_STEP),
	_add_parser(parse_user, PARSE_USER),
	_add_parser(parse_job, PARSE_JOB),
	_add_parser(parse_acct, PARSE_ACCOUNT),
	_add_parser(parse_tres, PARSE_TRES),
	_add_parser(parse_qos, PARSE_QOS),
	_add_parser(parse_coord, PARSE_COORD),
	_add_parser(parse_wckey, PARSE_WCKEY),
	_add_parser(parse_stats_rec, PARSE_STATS_REC),
	_add_parser(parse_cluster_rec, PARSE_CLUSTER_REC),
};
#undef _add_parser

extern int parse(parser_type_t type, void *obj, data_t *src, data_t *errors,
		 const parser_env_t *penv)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++)
		if (parsers[i].type == type)
			return _parser_run(obj, parsers[i].parse,
					   parsers[i].parse_member_count, src,
					   errors, penv);

	fatal("invalid type?");
}

extern int dump(parser_type_t type, void *obj, data_t *dst,
		const parser_env_t *penv)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++)
		if (parsers[i].type == type)
			return _parser_dump(obj, parsers[i].parse,
					    parsers[i].parse_member_count, dst,
					    penv);

	fatal("invalid type?");
}

static int _parser_run(void *obj, const parser_t *const parse,
		       const size_t parse_member_count, data_t *data,
		       data_t *errors, const parser_env_t *penv)
{
	int rc = SLURM_SUCCESS;

	for (int i = 0; (!rc) && (i < parse_member_count); i++) {
		for (int f = 0; f < ARRAY_SIZE(funcs); f++) {
			data_t *pd = data_resolve_dict_path(data, parse[i].key);

			if (pd && parse[i].type == funcs[f].type) {
				xassert(funcs[f].rfunc);
				rc = funcs[f].rfunc((parse + i), obj, pd,
						    errors, penv);
			}
		}

		if (rc && parse[i].required)
			resp_error(errors, rc, "Required field failed to parse",
				   parse[i].key);
		else
			rc = SLURM_SUCCESS;
	}

	return rc;
}

static int _parser_dump(void *obj, const parser_t *const parse,
			const size_t parse_member_count, data_t *data,
			const parser_env_t *penv)
{
	int rc = SLURM_SUCCESS;

	for (int i = 0; (!rc) && (i < parse_member_count); i++) {
		data_t *pd;

		/* make sure we aren't clobbering something */
		xassert(!data_resolve_dict_path(data, parse[i].key));

		if (!(pd = data_define_dict_path(data, parse[i].key))) {
			error("%s: failed to define field %s", __func__,
			      parse[i].key);
			rc = ESLURM_REST_EMPTY_RESULT;
			break;
		}

		for (int f = 0; (!rc) && (f < ARRAY_SIZE(funcs)); f++) {
			if (parse[i].type == funcs[f].type) {
				xassert(funcs[f].wfunc);

				if ((rc = funcs[f].wfunc((parse + i), obj, pd,
							 penv))) {
					error("%s: failed on field %s: %s",
					      __func__, parse[i].key,
					      slurm_strerror(rc));
					break;
				}
			}
		}
	}

	return rc;
}
