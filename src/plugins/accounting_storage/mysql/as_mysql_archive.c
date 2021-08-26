/*****************************************************************************\
 *  as_mysql_archive.c - functions dealing with the archiving.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "as_mysql_archive.h"
#include "src/common/env.h"
#include "src/common/slurm_time.h"
#include "src/common/slurmdbd_defs.h"

#define SLURM_19_05_PROTOCOL_VERSION ((34 << 8) | 0)
#define SLURM_18_08_PROTOCOL_VERSION ((33 << 8) | 0)
#define SLURM_17_11_PROTOCOL_VERSION ((32 << 8) | 0)
#define SLURM_17_02_PROTOCOL_VERSION ((31 << 8) | 0) /* slurm version 17.02. */
#define SLURM_16_05_PROTOCOL_VERSION ((30 << 8) | 0) /* slurm version 16.05. */
#define SLURM_15_08_PROTOCOL_VERSION ((29 << 8) | 0) /* slurm version 15.08. */
#define SLURM_14_11_PROTOCOL_VERSION ((28 << 8) | 0) /* slurm version 14.11. */
#define SLURM_14_03_PROTOCOL_VERSION ((27 << 8) | 0) /* slurm version
						      * 14.03, not
						      * needed here
						      * but added for
						      * reference. */
/* Before 14.03 the DBD had it's own versioning, in 14.03 all daemons use the
 * same version numbering. */
#define SLURMDBD_2_6_VERSION   12	/* slurm version 2.6 */
#define SLURMDBD_2_5_VERSION   11	/* slurm version 2.5 */

#define MAX_PURGE_LIMIT 50000 /* Number of records that are purged at a time
				 so that locks can be periodically released. */
#define MAX_ARCHIVE_AGE (60 * 60 * 24 * 60) /* If archive data is older than
					       this then archive by month to
					       handle large datasets. */

#ifndef RECORDS_PER_PASS
#define RECORDS_PER_PASS 1000	/* Records per single sql statement. */
#endif /* RECORDS_PER_PASS */

typedef struct {
	char *cluster_nodes;
	char *node_name;
	char *period_end;
	char *period_start;
	char *reason;
	char *reason_uid;
	char *state;
	char *tres_str;
} local_event_t;

static void _free_local_event_members(local_event_t *object)
{
	if (object) {
		xfree(object->cluster_nodes);
		xfree(object->node_name);
		xfree(object->period_end);
		xfree(object->period_start);
		xfree(object->reason);
		xfree(object->reason_uid);
		xfree(object->state);
		xfree(object->tres_str);
	}
}

typedef struct {
	char *account;
	char *admin_comment;
	char *alloc_nodes;
	char *associd;
	char *array_jobid;
	char *array_max_tasks;
	char *array_taskid;
	char *array_task_pending;
	char *array_task_str;
	char *blockid;
	char *constraints;
	char *deleted;
	char *derived_ec;
	char *derived_es;
	char *env;
	char *exit_code;
	char *eligible;
	char *end;
	char *flags;
	char *gid;
	char *gres_used;
	char *het_job_id;
	char *het_job_offset;
	char *job_db_inx;
	char *jobid;
	char *kill_requid;
	char *mcs_label;
	char *mod_time;
	char *name;
	char *nodelist;
	char *node_inx;
	char *partition;
	char *priority;
	char *qos;
	char *req_cpus;
	char *req_mem;
	char *resvid;
	char *script;
	char *start;
	char *state;
	char *state_reason_prev;
	char *submit;
	char *suspended;
	char *system_comment;
	char *timelimit;
	char *track_steps;
	char *tres_alloc_str;
	char *tres_req_str;
	char *uid;
	char *wckey;
	char *wckey_id;
	char *work_dir;
} local_job_t;

static void _convert_old_step_id(char **step_id)
{
	if (!step_id || !*step_id)
		return;

	if (!xstrcmp(*step_id, "-2")) {
		xfree(*step_id);
		*step_id = xstrdup_printf("%d", SLURM_BATCH_SCRIPT);
	} else if (!xstrcmp(*step_id, "-1")) {
		xfree(*step_id);
		*step_id = xstrdup_printf("%d", SLURM_EXTERN_CONT);
	}
}

static void _free_local_job_members(local_job_t *object)
{
	if (object) {
		xfree(object->account);
		xfree(object->admin_comment);
		xfree(object->alloc_nodes);
		xfree(object->associd);
		xfree(object->array_jobid);
		xfree(object->array_max_tasks);
		xfree(object->array_taskid);
		xfree(object->array_task_pending);
		xfree(object->array_task_str);
		xfree(object->blockid);
		xfree(object->constraints);
		xfree(object->deleted);
		xfree(object->derived_ec);
		xfree(object->derived_es);
		xfree(object->env);
		xfree(object->exit_code);
		xfree(object->eligible);
		xfree(object->end);
		xfree(object->flags);
		xfree(object->gid);
		xfree(object->gres_used);
		xfree(object->het_job_id);
		xfree(object->het_job_offset);
		xfree(object->job_db_inx);
		xfree(object->jobid);
		xfree(object->kill_requid);
		xfree(object->mcs_label);
		xfree(object->mod_time);
		xfree(object->name);
		xfree(object->nodelist);
		xfree(object->node_inx);
		xfree(object->partition);
		xfree(object->priority);
		xfree(object->qos);
		xfree(object->req_cpus);
		xfree(object->req_mem);
		xfree(object->resvid);
		xfree(object->script);
		xfree(object->start);
		xfree(object->state);
		xfree(object->state_reason_prev);
		xfree(object->submit);
		xfree(object->suspended);
		xfree(object->system_comment);
		xfree(object->timelimit);
		xfree(object->track_steps);
		xfree(object->tres_alloc_str);
		xfree(object->tres_req_str);
		xfree(object->uid);
		xfree(object->wckey);
		xfree(object->wckey_id);
		xfree(object->work_dir);
	}
}

typedef struct {
	char *assocs;
	char *deleted;
	char *flags;
	char *id;
	char *name;
	char *nodes;
	char *node_inx;
	char *time_end;
	char *time_start;
	char *tres_str;
	char *unused_wall;
} local_resv_t;

static void _free_local_resv_members(local_resv_t *object)
{
	if (object) {
		xfree(object->assocs);
		xfree(object->deleted);
		xfree(object->flags);
		xfree(object->id);
		xfree(object->name);
		xfree(object->nodes);
		xfree(object->node_inx);
		xfree(object->time_end);
		xfree(object->time_start);
		xfree(object->tres_str);
		xfree(object->unused_wall);
	}
}

typedef struct {
	char *act_cpufreq;
	char *deleted;
	char *exit_code;
	char *consumed_energy;
	char *job_db_inx;
	char *kill_requid;
	char *name;
	char *nodelist;
	char *nodes;
	char *node_inx;
	char *period_end;
	char *period_start;
	char *period_suspended;
	char *req_cpufreq_min;
	char *req_cpufreq_max;
	char *req_cpufreq_gov;
	char *state;
	char *stepid;
	char *step_het_comp;
	char *submit_line;
	char *sys_sec;
	char *sys_usec;
	char *tasks;
	char *task_dist;
	char *tres_alloc_str;
	char *tres_usage_in_ave;
	char *tres_usage_in_max;
	char *tres_usage_in_max_nodeid;
	char *tres_usage_in_max_taskid;
	char *tres_usage_in_min;
	char *tres_usage_in_min_nodeid;
	char *tres_usage_in_min_taskid;
	char *tres_usage_in_tot;
	char *tres_usage_out_ave;
	char *tres_usage_out_max;
	char *tres_usage_out_max_nodeid;
	char *tres_usage_out_max_taskid;
	char *tres_usage_out_min;
	char *tres_usage_out_min_nodeid;
	char *tres_usage_out_min_taskid;
	char *tres_usage_out_tot;
	char *user_sec;
	char *user_usec;
} local_step_t;

static void _free_local_step_members(local_step_t *object)
{
	if (object) {
		xfree(object->act_cpufreq);
		xfree(object->deleted);
		xfree(object->exit_code);
		xfree(object->consumed_energy);
		xfree(object->job_db_inx);
		xfree(object->kill_requid);
		xfree(object->name);
		xfree(object->nodelist);
		xfree(object->nodes);
		xfree(object->node_inx);
		xfree(object->period_end);
		xfree(object->period_start);
		xfree(object->period_suspended);
		xfree(object->req_cpufreq_min);
		xfree(object->req_cpufreq_max);
		xfree(object->req_cpufreq_gov);
		xfree(object->state);
		xfree(object->stepid);
		xfree(object->step_het_comp);
		xfree(object->submit_line);
		xfree(object->sys_sec);
		xfree(object->sys_usec);
		xfree(object->tasks);
		xfree(object->task_dist);
		xfree(object->tres_alloc_str);
		xfree(object->tres_usage_in_ave);
		xfree(object->tres_usage_in_max);
		xfree(object->tres_usage_in_max_nodeid);
		xfree(object->tres_usage_in_max_taskid);
		xfree(object->tres_usage_in_min);
		xfree(object->tres_usage_in_min_nodeid);
		xfree(object->tres_usage_in_min_taskid);
		xfree(object->tres_usage_in_tot);
		xfree(object->tres_usage_out_ave);
		xfree(object->tres_usage_out_max);
		xfree(object->tres_usage_out_max_nodeid);
		xfree(object->tres_usage_out_max_taskid);
		xfree(object->tres_usage_out_min);
		xfree(object->tres_usage_out_min_nodeid);
		xfree(object->tres_usage_out_min_taskid);
		xfree(object->tres_usage_out_tot);
		xfree(object->user_sec);
		xfree(object->user_usec);
	}
}

typedef struct {
	char *associd;
	char *job_db_inx;
	char *period_end;
	char *period_start;
} local_suspend_t;

static void _free_local_suspend_members(local_suspend_t *object)
{
	if (object) {
		xfree(object->associd);
		xfree(object->job_db_inx);
		xfree(object->period_end);
		xfree(object->period_start);
	}
}

typedef struct {
	char *id;
	char *timestamp;
	char *action;
	char *name;
	char *actor;
	char *info;
	char *cluster;
} local_txn_t;

static void _free_local_txn_members(local_txn_t *object)
{
	if (object) {
		xfree(object->id);
		xfree(object->timestamp);
		xfree(object->action);
		xfree(object->name);
		xfree(object->actor);
		xfree(object->info);
		xfree(object->cluster);
	}
}

typedef struct {
	char *alloc_secs;
	char *id;
	char *time_start;
	char *tres_id;
	char *creation_time;
	char *mod_time;
	char *deleted;
} local_usage_t;

static void _free_local_usage_members(local_usage_t *object)
{
	if (object) {
		xfree(object->alloc_secs);
		xfree(object->id);
		xfree(object->time_start);
		xfree(object->tres_id);
		xfree(object->creation_time);
		xfree(object->mod_time);
		xfree(object->deleted);
	}
}

typedef struct {
	char *alloc_secs;
	char *down_secs;
	char *idle_secs;
	char *over_secs;
	char *pdown_secs;
	char *time_start;
	char *plan_secs;
	char *tres_id;
	char *tres_cnt;
	char *creation_time;
	char *mod_time;
	char *deleted;
} local_cluster_usage_t;

static void _free_local_cluster_members(local_cluster_usage_t *object)
{
	if (object) {
		xfree(object->alloc_secs);
		xfree(object->down_secs);
		xfree(object->idle_secs);
		xfree(object->over_secs);
		xfree(object->pdown_secs);
		xfree(object->time_start);
		xfree(object->plan_secs);
		xfree(object->tres_id);
		xfree(object->tres_cnt);
		xfree(object->creation_time);
		xfree(object->mod_time);
		xfree(object->deleted);
	}
}

/* if this changes you will need to edit the corresponding enum below */
char *event_req_inx[] = {
	"time_start",
	"time_end",
	"node_name",
	"cluster_nodes",
	"reason",
	"reason_uid",
	"state",
	"tres",
};

enum {
	EVENT_REQ_START,
	EVENT_REQ_END,
	EVENT_REQ_NODE,
	EVENT_REQ_CNODES,
	EVENT_REQ_REASON,
	EVENT_REQ_REASON_UID,
	EVENT_REQ_STATE,
	EVENT_REQ_TRES,
	EVENT_REQ_COUNT
};

/* if this changes you will need to edit the corresponding enum below */
static char *job_req_inx[] = {
	"account",
	"admin_comment",
	"array_max_tasks",
	"array_task_pending",
	"array_task_str",
	"nodes_alloc",
	"id_assoc",
	"id_array_job",
	"id_array_task",
	"batch_script",
	"id_block",
	"constraints",
	"deleted",
	"derived_ec",
	"derived_es",
	"env_vars",
	"exit_code",
	"flags",
	"timelimit",
	"time_eligible",
	"time_end",
	"id_group",
	"gres_used",
	"het_job_id",
	"het_job_offset",
	"job_db_inx",
	"id_job",
	"kill_requid",
	"mcs_label",
	"mod_time",
	"job_name",
	"nodelist",
	"node_inx",
	"`partition`",
	"priority",
	"id_qos",
	"cpus_req",
	"mem_req",
	"id_resv",
	"time_start",
	"state",
	"state_reason_prev",
	"system_comment",
	"time_submit",
	"time_suspended",
	"track_steps",
	"id_user",
	"wckey",
	"id_wckey",
	"work_dir",
	"tres_alloc",
	"tres_req",
};

enum {
	JOB_REQ_ACCOUNT,
	JOB_REQ_ADMIN_COMMENT,
	JOB_REQ_ARRAY_MAX,
	JOB_REQ_ARRAY_TASK_PENDING,
	JOB_REQ_ARRAY_TASK_STR,
	JOB_REQ_ALLOC_NODES,
	JOB_REQ_ASSOCID,
	JOB_REQ_ARRAYJOBID,
	JOB_REQ_ARRAYTASKID,
	JOB_REQ_SCRIPT,
	JOB_REQ_BLOCKID,
	JOB_REQ_CONSTRAINTS,
	JOB_REQ_DELETED,
	JOB_REQ_DERIVED_EC,
	JOB_REQ_DERIVED_ES,
	JOB_REQ_ENV,
	JOB_REQ_EXIT_CODE,
	JOB_REQ_FLAGS,
	JOB_REQ_TIMELIMIT,
	JOB_REQ_ELIGIBLE,
	JOB_REQ_END,
	JOB_REQ_GID,
	JOB_REQ_GRES_USED,
	JOB_REQ_HET_JOB_ID,
	JOB_REQ_HET_JOB_OFFSET,
	JOB_REQ_DB_INX,
	JOB_REQ_JOBID,
	JOB_REQ_KILL_REQUID,
	JOB_REQ_MCS_LABEL,
	JOB_REQ_MOD_TIME,
	JOB_REQ_NAME,
	JOB_REQ_NODELIST,
	JOB_REQ_NODE_INX,
	JOB_REQ_PARTITION,
	JOB_REQ_PRIORITY,
	JOB_REQ_QOS,
	JOB_REQ_REQ_CPUS,
	JOB_REQ_REQ_MEM,
	JOB_REQ_RESVID,
	JOB_REQ_START,
	JOB_REQ_STATE,
	JOB_REQ_STATE_REASON,
	JOB_REQ_SYSTEM_COMMENT,
	JOB_REQ_SUBMIT,
	JOB_REQ_SUSPENDED,
	JOB_REQ_TRACKSTEPS,
	JOB_REQ_UID,
	JOB_REQ_WCKEY,
	JOB_REQ_WCKEYID,
	JOB_REQ_WORK_DIR,
	JOB_REQ_TRESA,
	JOB_REQ_TRESR,
	JOB_REQ_COUNT
};

/* if this changes you will need to edit the corresponding enum */
char *resv_req_inx[] = {
	"id_resv",
	"assoclist",
	"deleted",
	"flags",
	"tres",
	"nodelist",
	"node_inx",
	"resv_name",
	"time_start",
	"time_end",
	"unused_wall",
};

enum {
	RESV_REQ_ID,
	RESV_REQ_ASSOCS,
	RESV_REQ_DELETED,
	RESV_REQ_FLAGS,
	RESV_REQ_TRES,
	RESV_REQ_NODES,
	RESV_REQ_NODE_INX,
	RESV_REQ_NAME,
	RESV_REQ_START,
	RESV_REQ_END,
	RESV_REQ_UNUSED,
	RESV_REQ_COUNT
};

/* if this changes you will need to edit the corresponding enum below */
static char *step_req_inx[] = {
	"job_db_inx",
	"id_step",
	"step_het_comp",
	"deleted",
	"time_start",
	"time_end",
	"time_suspended",
	"step_name",
	"nodelist",
	"node_inx",
	"state",
	"kill_requid",
	"exit_code",
	"nodes_alloc",
	"task_cnt",
	"task_dist",
	"user_sec",
	"user_usec",
	"sys_sec",
	"sys_usec",
	"act_cpufreq",
	"consumed_energy",
	"req_cpufreq_min",
	"req_cpufreq",
	"req_cpufreq_gov",
	"submit_line",
	"tres_alloc",
	"tres_usage_in_ave",
	"tres_usage_in_max",
	"tres_usage_in_max_nodeid",
	"tres_usage_in_max_taskid",
	"tres_usage_in_min",
	"tres_usage_in_min_nodeid",
	"tres_usage_in_min_taskid",
	"tres_usage_in_tot",
	"tres_usage_out_ave",
	"tres_usage_out_max",
	"tres_usage_out_max_nodeid",
	"tres_usage_out_max_taskid",
	"tres_usage_out_min",
	"tres_usage_out_min_nodeid",
	"tres_usage_out_min_taskid",
	"tres_usage_out_tot",
};


enum {
	STEP_REQ_DB_INX,
	STEP_REQ_STEPID,
	STEP_REQ_STEP_HET_COMP,
	STEP_REQ_DELETED,
	STEP_REQ_START,
	STEP_REQ_END,
	STEP_REQ_SUSPENDED,
	STEP_REQ_NAME,
	STEP_REQ_NODELIST,
	STEP_REQ_NODE_INX,
	STEP_REQ_STATE,
	STEP_REQ_KILL_REQUID,
	STEP_REQ_EXIT_CODE,
	STEP_REQ_NODES,
	STEP_REQ_TASKS,
	STEP_REQ_TASKDIST,
	STEP_REQ_USER_SEC,
	STEP_REQ_USER_USEC,
	STEP_REQ_SYS_SEC,
	STEP_REQ_SYS_USEC,
	STEP_REQ_ACT_CPUFREQ,
	STEP_REQ_CONSUMED_ENERGY,
	STEP_REQ_REQ_CPUFREQ_MIN,
	STEP_REQ_REQ_CPUFREQ_MAX,
	STEP_REQ_REQ_CPUFREQ_GOV,
	STEP_REQ_SUBMIT_LINE,
	STEP_REQ_TRES,
	STEP_TRES_USAGE_IN_AVE,
	STEP_TRES_USAGE_IN_MAX,
	STEP_TRES_USAGE_IN_MAX_NODEID,
	STEP_TRES_USAGE_IN_MAX_TASKID,
	STEP_TRES_USAGE_IN_MIN,
	STEP_TRES_USAGE_IN_MIN_NODEID,
	STEP_TRES_USAGE_IN_MIN_TASKID,
	STEP_TRES_USAGE_IN_TOT,
	STEP_TRES_USAGE_OUT_AVE,
	STEP_TRES_USAGE_OUT_MAX,
	STEP_TRES_USAGE_OUT_MAX_NODEID,
	STEP_TRES_USAGE_OUT_MAX_TASKID,
	STEP_TRES_USAGE_OUT_MIN,
	STEP_TRES_USAGE_OUT_MIN_NODEID,
	STEP_TRES_USAGE_OUT_MIN_TASKID,
	STEP_TRES_USAGE_OUT_TOT,
	STEP_REQ_COUNT,
};

/* if this changes you will need to edit the corresponding enum below */
static char *suspend_req_inx[] = {
	"job_db_inx",
	"id_assoc",
	"time_start",
	"time_end",
};

enum {
	SUSPEND_REQ_DB_INX,
	SUSPEND_REQ_ASSOCID,
	SUSPEND_REQ_START,
	SUSPEND_REQ_END,
	SUSPEND_REQ_COUNT
};

/* if this changes you will need to edit the corresponding enum below */
static char *txn_req_inx[] = {
	"id",
	"timestamp",
	"action",
	"name",
	"actor",
	"info",
	"cluster"
};

enum {
	TXN_REQ_ID,
	TXN_REQ_TS,
	TXN_REQ_ACTION,
	TXN_REQ_NAME,
	TXN_REQ_ACTOR,
	TXN_REQ_INFO,
	TXN_REQ_CLUSTER,
	TXN_REQ_COUNT
};

/* if this changes you will need to edit the corresponding enum below */
char *usage_req_inx[] = {
	"id",
	"id_tres",
	"time_start",
	"alloc_secs",
	"creation_time",
	"mod_time",
	"deleted"
};

enum {
	USAGE_ID,
	USAGE_TRES,
	USAGE_START,
	USAGE_ALLOC,
	USAGE_CREATION_TIME,
	USAGE_MOD_TIME,
	USAGE_DELETED,
	USAGE_COUNT
};

/* if this changes you will need to edit the corresponding enum below */
char *cluster_req_inx[] = {
	"id_tres",
	"time_start",
	"count",
	"alloc_secs",
	"down_secs",
	"pdown_secs",
	"idle_secs",
	"plan_secs",
	"over_secs",
	"creation_time",
	"mod_time",
	"deleted"
};

enum {
	CLUSTER_TRES,
	CLUSTER_START,
	CLUSTER_CNT,
	CLUSTER_ACPU,
	CLUSTER_DCPU,
	CLUSTER_PDCPU,
	CLUSTER_ICPU,
	CLUSTER_PCPU,
	CLUSTER_OCPU,
	CLUSTER_CREATION_TIME,
	CLUSTER_MOD_TIME,
	CLUSTER_DELETED,
	CLUSTER_COUNT
};

typedef enum {
	PURGE_EVENT,
	PURGE_SUSPEND,
	PURGE_RESV,
	PURGE_JOB,
	PURGE_STEP,
	PURGE_TXN,
	PURGE_USAGE,
	PURGE_CLUSTER_USAGE
} purge_type_t;

static uint32_t _archive_table(purge_type_t type, mysql_conn_t *mysql_conn,
			       char *cluster_name, time_t period_end,
			       char *arch_dir, uint32_t archive_period,
			       char *sql_table, uint32_t usage_info);

static uint32_t high_buffer_size = (1024 * 1024);

static void _pack_local_event(local_event_t *object, uint16_t rpc_version,
			      buf_t *buffer)
{
	packstr(object->cluster_nodes, buffer);
	packstr(object->node_name, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
	packstr(object->reason, buffer);
	packstr(object->reason_uid, buffer);
	packstr(object->state, buffer);
	packstr(object->tres_str, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_event(local_event_t *object, uint16_t rpc_version,
			       buf_t *buffer)
{
	uint32_t tmp32;
	char *tmp_char;

	if (rpc_version >= SLURM_15_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->cluster_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->reason, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->reason_uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_str, &tmp32, buffer);
	} else {
		safe_unpackstr_xmalloc(&object->cluster_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		object->tres_str = xstrdup_printf("%d=%s", TRES_CPU, tmp_char);
		xfree(tmp_char);
		safe_unpackstr_xmalloc(&object->node_name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->reason, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->reason_uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	_free_local_event_members(object);
	return SLURM_ERROR;
}

static void _pack_local_job(local_job_t *object, uint16_t rpc_version,
			    buf_t *buffer)
{
	packstr(object->account, buffer);
	packstr(object->admin_comment, buffer);
	packstr(object->alloc_nodes, buffer);
	packstr(object->associd, buffer);
	packstr(object->array_jobid, buffer);
	packstr(object->array_max_tasks, buffer);
	packstr(object->array_taskid, buffer);
	packstr(object->array_task_pending, buffer);
	packstr(object->array_task_str, buffer);
	packstr(object->script, buffer);
	packstr(object->blockid, buffer);
	packstr(object->constraints, buffer);
	packstr(object->deleted, buffer);
	packstr(object->derived_ec, buffer);
	packstr(object->derived_es, buffer);
	packstr(object->env, buffer);
	packstr(object->exit_code, buffer);
	packstr(object->flags, buffer);
	packstr(object->timelimit, buffer);
	packstr(object->eligible, buffer);
	packstr(object->end, buffer);
	packstr(object->gid, buffer);
	packstr(object->gres_used, buffer);
	packstr(object->job_db_inx, buffer);
	packstr(object->jobid, buffer);
	packstr(object->kill_requid, buffer);
	packstr(object->mcs_label, buffer);
	packstr(object->mod_time, buffer);
	packstr(object->name, buffer);
	packstr(object->nodelist, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->het_job_id, buffer);
	packstr(object->het_job_offset, buffer);
	packstr(object->partition, buffer);
	packstr(object->priority, buffer);
	packstr(object->qos, buffer);
	packstr(object->req_cpus, buffer);
	packstr(object->req_mem, buffer);
	packstr(object->resvid, buffer);
	packstr(object->start, buffer);
	packstr(object->state, buffer);
	packstr(object->state_reason_prev, buffer);
	packstr(object->submit, buffer);
	packstr(object->suspended, buffer);
	packstr(object->system_comment, buffer);
	packstr(object->track_steps, buffer);
	packstr(object->tres_alloc_str, buffer);
	packstr(object->tres_req_str, buffer);
	packstr(object->uid, buffer);
	packstr(object->wckey, buffer);
	packstr(object->wckey_id, buffer);
	packstr(object->work_dir, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_job(local_job_t *object, uint16_t rpc_version,
			     buf_t *buffer)
{
	uint32_t tmp32;
	char *tmp_char = NULL;

	memset(object, 0, sizeof(local_job_t));

	/* For protocols <= 14_11, job_req_inx and it's corresponding enum,
	 * were out of sync. This caused the following variables to have the
	 * corresponding values:
	 * job->partition = priority
	 * job->priority  = qos
	 * job->qos       = req_cpus
	 * job->req_cpus  = req_mem
	 * job->req_mem   = resvid
	 * job->resvid    = partition
	 *
	 * The values were packed in the above order. To unpack the values
	 * into the correct variables, the unpacking order is changed to
	 * accomodate the shift in values. job->partition is unpacked before
	 * job->start instead of after job->node_inx.
	 *
	 * 15.08: job_req_inx and the it's corresponding enum were synced up
	 * and it unpacks in the expected order.
	 */

	if (rpc_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->admin_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_task_pending, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_task_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->script, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->constraints, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->env, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gres_used, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mcs_label, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mod_time, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_offset, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_mem, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state_reason_prev, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->system_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->work_dir, &tmp32, buffer);
	} else if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->admin_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_task_pending, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_task_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->constraints, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gres_used, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mcs_label, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mod_time, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_offset, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_mem, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state_reason_prev, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->system_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->work_dir, &tmp32, buffer);
	} else if (rpc_version >= SLURM_19_05_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->admin_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->constraints, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mcs_label, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_offset, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_mem, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state_reason_prev, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->system_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->work_dir, &tmp32, buffer);
	} else if (rpc_version >= SLURM_18_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->admin_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mcs_label, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_offset, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_mem, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->system_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->work_dir, &tmp32, buffer);
	} else if (rpc_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->admin_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mcs_label, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->het_job_offset, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_mem, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->work_dir, &tmp32, buffer);
	} else if (rpc_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->admin_comment, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		object->het_job_id = xstrdup("0");
		object->het_job_offset = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		if (tmp_char) {
			uint64_t tmp_uint64 = slurm_atoull(tmp_char);
			if ((tmp_uint64 & 0x80000000) &&
			    (tmp_uint64 < 0x100000000)) {
				/*
				 * Handle old conversion of memory
				 * stored incorrectly in the database.
				 * This will be fixed in 17.11 and we
				 * can remove this check.  0x80000000
				 * was the old value of MEM_PER_CPU
				 */
				tmp_uint64 &= (~0x80000000);
				tmp_uint64 |= MEM_PER_CPU;
				object->req_mem = xstrdup_printf("%"PRIu64,
								 tmp_uint64);
				xfree(tmp_char);
			} else {
				object->req_mem = tmp_char;
				tmp_char = NULL;
			}
		}
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
	} else if (rpc_version >= SLURM_15_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		object->het_job_id = xstrdup("0");
		object->het_job_offset = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		if (tmp_char) {
			uint64_t tmp_uint64 = slurm_atoull(tmp_char);
			if ((tmp_uint64 & 0x80000000) &&
			    (tmp_uint64 < 0x100000000)) {
				/*
				 * Handle old conversion of memory
				 * stored incorrectly in the database.
				 * This will be fixed in 17.11 and we
				 * can remove this check.  0x80000000
				 * was the old value of MEM_PER_CPU
				 */
				tmp_uint64 &= (~0x80000000);
				tmp_uint64 |= MEM_PER_CPU;
				object->req_mem = xstrdup_printf("%"PRIu64,
								 tmp_uint64);
				xfree(tmp_char);
			} else {
				object->req_mem = tmp_char;
				tmp_char = NULL;
			}
		}
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_req_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
	} else if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		object->tres_alloc_str = xstrdup_printf(
			"%d=%s", TRES_CPU, tmp_char);
		xfree(tmp_char);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_max_tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->array_taskid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		if (tmp_char) {
			uint64_t tmp_uint64 = slurm_atoull(tmp_char);
			if ((tmp_uint64 & 0x80000000) &&
			    (tmp_uint64 < 0x100000000)) {
				/*
				 * Handle old conversion of memory
				 * stored incorrectly in the database.
				 * This will be fixed in 17.11 and we
				 * can remove this check.  0x80000000
				 * was the old value of MEM_PER_CPU
				 */
				tmp_uint64 &= (~0x80000000);
				tmp_uint64 |= MEM_PER_CPU;
				object->req_mem = xstrdup_printf("%"PRIu64,
								 tmp_uint64);
				xfree(tmp_char);
			} else {
				object->req_mem = tmp_char;
				tmp_char = NULL;
			}
		}
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		object->het_job_id = xstrdup("0");
		object->het_job_offset = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		object->tres_alloc_str = xstrdup_printf(
			"%d=%s", TRES_CPU, tmp_char);
		xfree(tmp_char);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		object->array_taskid = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		if (tmp_char) {
			uint64_t tmp_uint64 = slurm_atoull(tmp_char);
			if ((tmp_uint64 & 0x80000000) &&
			    (tmp_uint64 < 0x100000000)) {
				/*
				 * Handle old conversion of memory
				 * stored incorrectly in the database.
				 * This will be fixed in 17.11 and we
				 * can remove this check.  0x80000000
				 * was the old value of MEM_PER_CPU
				 */
				tmp_uint64 &= (~0x80000000);
				tmp_uint64 |= MEM_PER_CPU;
				object->req_mem = xstrdup_printf("%"PRIu64,
								 tmp_uint64);
				xfree(tmp_char);
			} else {
				object->req_mem = tmp_char;
				tmp_char = NULL;
			}
		}
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		object->het_job_id = xstrdup("0");
		object->het_job_offset = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
	} else {
		safe_unpackstr_xmalloc(&object->account, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		object->tres_alloc_str = xstrdup_printf(
			"%d=%s", TRES_CPU, tmp_char);
		xfree(tmp_char);
		safe_unpackstr_xmalloc(&object->alloc_nodes, &tmp32, buffer);
		object->array_taskid = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->blockid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_ec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->derived_es, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->timelimit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->eligible, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->gid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->jobid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->priority, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->qos, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpus, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->resvid, &tmp32, buffer);
		object->het_job_id = xstrdup("0");
		object->het_job_offset = xstrdup("4294967294");
		safe_unpackstr_xmalloc(&object->partition, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->track_steps, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->uid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->wckey_id, &tmp32, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	_free_local_job_members(object);
	return SLURM_ERROR;
}

static void _pack_local_resv(local_resv_t *object, uint16_t rpc_version,
			     buf_t *buffer)
{
	packstr(object->assocs, buffer);
	packstr(object->deleted, buffer);
	packstr(object->flags, buffer);
	packstr(object->id, buffer);
	packstr(object->name, buffer);
	packstr(object->nodes, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->time_end, buffer);
	packstr(object->time_start, buffer);
	packstr(object->tres_str, buffer);
	packstr(object->unused_wall, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_resv(local_resv_t *object, uint16_t rpc_version,
			      buf_t *buffer)
{
	uint32_t tmp32;
	char *tmp_char;

	if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->assocs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->unused_wall, &tmp32, buffer);
	} else if (rpc_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->assocs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->unused_wall, &tmp32, buffer);
	} else if (rpc_version >= SLURM_15_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->assocs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_str, &tmp32, buffer);
	} else {
		safe_unpackstr_xmalloc(&object->assocs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		object->tres_str = xstrdup_printf("%d=%s", TRES_CPU, tmp_char);
		xfree(tmp_char);
		safe_unpackstr_xmalloc(&object->flags, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	_free_local_resv_members(object);
	return SLURM_ERROR;
}

static void _pack_local_step(local_step_t *object, uint16_t rpc_version,
			     buf_t *buffer)
{
	packstr(object->act_cpufreq, buffer);
	packstr(object->deleted, buffer);
	packstr(object->exit_code, buffer);
	packstr(object->consumed_energy, buffer);
	packstr(object->job_db_inx, buffer);
	packstr(object->kill_requid, buffer);
	packstr(object->name, buffer);
	packstr(object->nodelist, buffer);
	packstr(object->nodes, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
	packstr(object->period_suspended, buffer);
	packstr(object->req_cpufreq_min, buffer);
	packstr(object->req_cpufreq_max, buffer);
	packstr(object->req_cpufreq_gov, buffer);
	packstr(object->state, buffer);
	packstr(object->stepid, buffer);
	packstr(object->step_het_comp, buffer);
	packstr(object->submit_line, buffer);
	packstr(object->sys_sec, buffer);
	packstr(object->sys_usec, buffer);
	packstr(object->tasks, buffer);
	packstr(object->task_dist, buffer);
	packstr(object->tres_alloc_str, buffer);
	packstr(object->tres_usage_in_ave, buffer);
	packstr(object->tres_usage_in_max, buffer);
	packstr(object->tres_usage_in_max_nodeid, buffer);
	packstr(object->tres_usage_in_max_taskid, buffer);
	packstr(object->tres_usage_in_min, buffer);
	packstr(object->tres_usage_in_min_nodeid, buffer);
	packstr(object->tres_usage_in_min_taskid, buffer);
	packstr(object->tres_usage_in_tot, buffer);
	packstr(object->tres_usage_out_ave, buffer);
	packstr(object->tres_usage_out_max, buffer);
	packstr(object->tres_usage_out_max_nodeid, buffer);
	packstr(object->tres_usage_out_max_taskid, buffer);
	packstr(object->tres_usage_out_min, buffer);
	packstr(object->tres_usage_out_min_nodeid, buffer);
	packstr(object->tres_usage_out_min_taskid, buffer);
	packstr(object->tres_usage_out_tot, buffer);
	packstr(object->user_sec, buffer);
	packstr(object->user_usec, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_step(local_step_t *object, uint16_t rpc_version,
			      buf_t *buffer)
{
	uint32_t tmp32;
	char *tmp_char;

	if (rpc_version >= SLURM_21_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->act_cpufreq, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->consumed_energy,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_suspended,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_min,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_max,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_gov,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->stepid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->step_het_comp, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->submit_line, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_usec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->task_dist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_ave,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_tot,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_ave,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_tot,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_usec, &tmp32, buffer);
	} else if (rpc_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->act_cpufreq, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->consumed_energy,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_suspended,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_min,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_max,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_gov,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->stepid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->step_het_comp, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_usec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->task_dist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_ave,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_tot,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_ave,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_nodeid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_taskid,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_tot,
				       &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_usec, &tmp32, buffer);
	} else if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->act_cpufreq, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->consumed_energy, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_gov, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->stepid, &tmp32, buffer);
		_convert_old_step_id(&object->stepid);
		safe_unpackstr_xmalloc(&object->sys_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_usec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->task_dist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_ave, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_tot, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_ave, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_tot, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_usec, &tmp32, buffer);
	} else if (rpc_version >= SLURM_18_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->act_cpufreq, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->consumed_energy, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_gov, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->stepid, &tmp32, buffer);
		_convert_old_step_id(&object->stepid);
		safe_unpackstr_xmalloc(&object->sys_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_usec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->task_dist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_ave, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_max_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_min_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_in_tot, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_ave, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_max_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_nodeid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_min_taskid, &tmp32,
			      buffer);
		safe_unpackstr_xmalloc(&object->tres_usage_out_tot, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_usec, &tmp32, buffer);
	} else if (rpc_version >= SLURM_15_08_PROTOCOL_VERSION) {
		char *ave_cpu;
		char *ave_disk_read;
		char *ave_disk_write;
		char *ave_pages;
		char *ave_rss;
		char *ave_vsize;
		char *max_disk_read;
		char *max_disk_read_node;
		char *max_disk_read_task;
		char *max_disk_write;
		char *max_disk_write_node;
		char *max_disk_write_task;
		char *max_pages;
		char *max_pages_node;
		char *max_pages_task;
		char *max_rss;
		char *max_rss_node;
		char *max_rss_task;
		char *max_vsize;
		char *max_vsize_node;
		char *max_vsize_task;
		char *min_cpu;
		char *min_cpu_node;
		char *min_cpu_task;

		safe_unpackstr_xmalloc(&object->act_cpufreq, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_cpu, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_disk_read, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_disk_write, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_pages, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_rss, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_vsize, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->consumed_energy, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_read, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_read_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_read_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_write, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_write_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_write_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_pages, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_pages_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_pages_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_rss, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_rss_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_rss_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_vsize, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_vsize_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_vsize_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&min_cpu, &tmp32, buffer);
		safe_unpackstr_xmalloc(&min_cpu_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&min_cpu_task, &tmp32, buffer);

		if (atol(min_cpu) != NO_VAL) {
			object->tres_usage_in_ave = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_CPU, ave_cpu,
				TRES_MEM, ave_rss,
				TRES_VMEM, ave_vsize,
				TRES_PAGES, ave_pages,
				TRES_FS_DISK, ave_disk_read);
			object->tres_usage_out_ave = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, ave_disk_write);

			object->tres_usage_in_max = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_MEM, max_rss,
				TRES_VMEM, max_vsize,
				TRES_PAGES, max_pages,
				TRES_FS_DISK, max_disk_read);
			object->tres_usage_out_max = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, max_disk_write);

			object->tres_usage_in_max_nodeid = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_MEM, max_rss_node,
				TRES_VMEM, max_vsize_node,
				TRES_PAGES, max_pages_node,
				TRES_FS_DISK, max_disk_read_node);
			object->tres_usage_out_max_nodeid = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, max_disk_write_node);

			object->tres_usage_in_max_taskid = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_CPU, min_cpu_task,
				TRES_MEM, max_rss_task,
				TRES_VMEM, max_vsize_task,
				TRES_PAGES, max_pages_task,
				TRES_FS_DISK, max_disk_read_task);
			object->tres_usage_out_max_taskid = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, max_disk_write_task);

			object->tres_usage_in_min = xstrdup_printf(
				"%d=%s",
				TRES_CPU, min_cpu);
			object->tres_usage_in_min_nodeid = xstrdup_printf(
				"%d=%s",
				TRES_CPU, min_cpu_node);
			object->tres_usage_in_min_taskid = xstrdup_printf(
				"%d=%s",
				TRES_CPU, min_cpu_task);
		}

		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_min, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_gov, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->stepid, &tmp32, buffer);
		_convert_old_step_id(&object->stepid);
		safe_unpackstr_xmalloc(&object->sys_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_usec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->task_dist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_alloc_str, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_usec, &tmp32, buffer);

		xfree(ave_cpu);
		xfree(ave_disk_read);
		xfree(ave_disk_write);
		xfree(ave_pages);
		xfree(ave_rss);
		xfree(ave_vsize);
		xfree(max_disk_read);
		xfree(max_disk_read_node);
		xfree(max_disk_read_task);
		xfree(max_disk_write);
		xfree(max_disk_write_node);
		xfree(max_disk_write_task);
		xfree(max_pages);
		xfree(max_pages_node);
		xfree(max_pages_task);
		xfree(max_rss);
		xfree(max_rss_node);
		xfree(max_rss_task);
		xfree(max_vsize);
		xfree(max_vsize_node);
		xfree(max_vsize_task);
		xfree(min_cpu);
		xfree(min_cpu_node);
		xfree(min_cpu_task);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		char *ave_cpu;
		char *ave_disk_read;
		char *ave_disk_write;
		char *ave_pages;
		char *ave_rss;
		char *ave_vsize;
		char *max_disk_read;
		char *max_disk_read_node;
		char *max_disk_read_task;
		char *max_disk_write;
		char *max_disk_write_node;
		char *max_disk_write_task;
		char *max_pages;
		char *max_pages_node;
		char *max_pages_task;
		char *max_rss;
		char *max_rss_node;
		char *max_rss_task;
		char *max_vsize;
		char *max_vsize_node;
		char *max_vsize_task;
		char *min_cpu;
		char *min_cpu_node;
		char *min_cpu_task;

		safe_unpackstr_xmalloc(&object->act_cpufreq, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_cpu, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_disk_read, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_disk_write, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_pages, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_rss, &tmp32, buffer);
		safe_unpackstr_xmalloc(&ave_vsize, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->exit_code, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->consumed_energy, &tmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_char, &tmp32, buffer);
		object->tres_alloc_str = xstrdup_printf(
			"%d=%s", TRES_CPU, tmp_char);
		xfree(tmp_char);
		safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->kill_requid, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_read, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_read_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_read_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_write, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_write_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_disk_write_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_pages, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_pages_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_pages_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_rss, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_rss_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_rss_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_vsize, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_vsize_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&max_vsize_task, &tmp32, buffer);
		safe_unpackstr_xmalloc(&min_cpu, &tmp32, buffer);
		safe_unpackstr_xmalloc(&min_cpu_node, &tmp32, buffer);
		safe_unpackstr_xmalloc(&min_cpu_task, &tmp32, buffer);

		if (atol(min_cpu) != NO_VAL) {
			object->tres_usage_in_ave = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_CPU, ave_cpu,
				TRES_MEM, ave_rss,
				TRES_VMEM, ave_vsize,
				TRES_PAGES, ave_pages,
				TRES_FS_DISK, ave_disk_read);
			object->tres_usage_out_ave = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, ave_disk_write);

			object->tres_usage_in_max = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_MEM, max_rss,
				TRES_VMEM, max_vsize,
				TRES_PAGES, max_pages,
				TRES_FS_DISK, max_disk_read);
			object->tres_usage_out_max = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, max_disk_write);

			object->tres_usage_in_max_nodeid = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_MEM, max_rss_node,
				TRES_VMEM, max_vsize_node,
				TRES_PAGES, max_pages_node,
				TRES_FS_DISK, max_disk_read_node);
			object->tres_usage_out_max_nodeid = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, max_disk_write_node);

			object->tres_usage_in_max_taskid = xstrdup_printf(
				"%d=%s,%d=%s,%d=%s,%d=%s,%d=%s",
				TRES_CPU, min_cpu_task,
				TRES_MEM, max_rss_task,
				TRES_VMEM, max_vsize_task,
				TRES_PAGES, max_pages_task,
				TRES_FS_DISK, max_disk_read_task);
			object->tres_usage_out_max_taskid = xstrdup_printf(
				"%d=%s",
				TRES_FS_DISK, max_disk_write_task);

			object->tres_usage_in_min = xstrdup_printf(
				"%d=%s",
				TRES_CPU, min_cpu);
			object->tres_usage_in_min_nodeid = xstrdup_printf(
				"%d=%s",
				TRES_CPU, min_cpu_node);
			object->tres_usage_in_min_taskid = xstrdup_printf(
				"%d=%s",
				TRES_CPU, min_cpu_task);
		}

		safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodelist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->nodes, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->node_inx, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->period_suspended, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->req_cpufreq_max, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->state, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->stepid, &tmp32, buffer);
		_convert_old_step_id(&object->stepid);
		safe_unpackstr_xmalloc(&object->sys_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->sys_usec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tasks, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->task_dist, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_sec, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->user_usec, &tmp32, buffer);

		xfree(ave_cpu);
		xfree(ave_disk_read);
		xfree(ave_disk_write);
		xfree(ave_pages);
		xfree(ave_rss);
		xfree(ave_vsize);
		xfree(max_disk_read);
		xfree(max_disk_read_node);
		xfree(max_disk_read_task);
		xfree(max_disk_write);
		xfree(max_disk_write_node);
		xfree(max_disk_write_task);
		xfree(max_pages);
		xfree(max_pages_node);
		xfree(max_pages_task);
		xfree(max_rss);
		xfree(max_rss_node);
		xfree(max_rss_task);
		xfree(max_vsize);
		xfree(max_vsize_node);
		xfree(max_vsize_task);
		xfree(min_cpu);
		xfree(min_cpu_node);
		xfree(min_cpu_task);
	} else {
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	_free_local_step_members(object);
	return SLURM_ERROR;
}

static void _pack_local_suspend(local_suspend_t *object, uint16_t rpc_version,
				buf_t *buffer)
{
	packstr(object->associd, buffer);
	packstr(object->job_db_inx, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_suspend(local_suspend_t *object, uint16_t rpc_version,
				 buf_t *buffer)
{
	uint32_t tmp32;

	safe_unpackstr_xmalloc(&object->associd, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->job_db_inx, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->period_end, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->period_start, &tmp32, buffer);

	return SLURM_SUCCESS;

unpack_error:
	_free_local_suspend_members(object);
	return SLURM_ERROR;
}

static void _pack_local_txn(local_txn_t *object, uint16_t rpc_version,
			    buf_t *buffer)
{
	packstr(object->id, buffer);
	packstr(object->timestamp, buffer);
	packstr(object->action, buffer);
	packstr(object->name, buffer);
	packstr(object->actor, buffer);
	packstr(object->info, buffer);
	packstr(object->cluster, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_txn(local_txn_t *object, uint16_t rpc_version,
			     buf_t *buffer)
{
	uint32_t tmp32;

	safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->timestamp, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->action, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->name, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->actor, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->info, &tmp32, buffer);
	safe_unpackstr_xmalloc(&object->cluster, &tmp32, buffer);

	return SLURM_SUCCESS;

unpack_error:
	_free_local_txn_members(object);
	return SLURM_ERROR;
}

static void _pack_local_usage(local_usage_t *object, uint16_t rpc_version,
			      buf_t *buffer)
{
	packstr(object->id, buffer);
	packstr(object->tres_id, buffer);
	packstr(object->time_start, buffer);
	packstr(object->alloc_secs, buffer);
	packstr(object->creation_time, buffer);
	packstr(object->mod_time, buffer);
	packstr(object->deleted, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_usage(local_usage_t *object, uint16_t rpc_version,
			       buf_t *buffer)
{
	uint32_t tmp32;

	if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->creation_time, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mod_time, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
	} else {
		safe_unpackstr_xmalloc(&object->id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_secs, &tmp32, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	_free_local_usage_members(object);
	return SLURM_ERROR;
}

static void _pack_local_cluster_usage(local_cluster_usage_t *object,
				      uint16_t rpc_version, buf_t *buffer)
{
	packstr(object->tres_id, buffer);
	packstr(object->time_start, buffer);
	packstr(object->tres_cnt, buffer);
	packstr(object->alloc_secs, buffer);
	packstr(object->down_secs, buffer);
	packstr(object->pdown_secs, buffer);
	packstr(object->idle_secs, buffer);
	packstr(object->plan_secs, buffer);
	packstr(object->over_secs, buffer);
	packstr(object->creation_time, buffer);
	packstr(object->mod_time, buffer);
	packstr(object->deleted, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_cluster_usage(local_cluster_usage_t *object,
				       uint16_t rpc_version, buf_t *buffer)
{
	uint32_t tmp32;

	if (rpc_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object->tres_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_cnt, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->down_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->pdown_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->idle_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->plan_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->over_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->creation_time, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->mod_time, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->deleted, &tmp32, buffer);
	} else {
		safe_unpackstr_xmalloc(&object->tres_id, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->time_start, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->tres_cnt, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->alloc_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->down_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->idle_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->plan_secs, &tmp32, buffer);
		safe_unpackstr_xmalloc(&object->over_secs, &tmp32, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	_free_local_cluster_members(object);
	return SLURM_ERROR;
}

static int _process_old_sql_line(const char *data_in,
				 char **cluster_name, char **data_full_out)
{
	int start = 0, i = 0;
	char *beginning = NULL;
	char *ending = NULL;
	char *data_out = *data_full_out;
	char *table = NULL;
	char *fields = NULL;
	char *new_vals = NULL;
	char *vals = NULL;
	char *new_cluster_name = NULL;
	int rc = SLURM_SUCCESS;
	int cnt = 0, cluster_inx = -1, ending_start = 0, ending_end = 0;
	bool delete = 0;
	bool new_cluster = 0;

	while (data_in[i]) {
		if (!xstrncmp("insert into ", data_in+i, 12)) {
			beginning = xstrndup(data_in+i, 11);
			i+=12;
			break;
		} else if (!xstrncmp("delete from ", data_in+i, 12)) {
			beginning = xstrndup(data_in+i, 11);
			i+=12;
			delete = 1;
			break;
		} else if (!xstrncmp("drop table ", data_in+i, 11)) {
			start = i;
			i+=11;
			while (data_in[i] && data_in[i-1] != ';')
				i++;
			xstrncat(data_out, data_in+start, i-start);
			goto end_it;
		} else if (!xstrncmp("truncate table ", data_in+i, 15)) {
			start = i;
			i+=15;
			while (data_in[i] && data_in[i-1] != ';')
				i++;
			xstrncat(data_out, data_in+start, i-start);
			goto end_it;
		}
		i++;
	}

	if (!data_in[i])
		goto end_it;

	//info("processing %s", data_in);
	/* get table name */
	if (!xstrncmp("cluster_event_table", data_in+i, 19)) {
		i+=19;
		table = event_table;
	} else if (!xstrncmp("job_table", data_in+i, 9)) {
		i+=9;
		table = job_table;
	} else if (!xstrncmp("step_table", data_in+i, 10)) {
		i+=10;
		table = step_table;
	} else if (!xstrncmp("suspend_table", data_in+i, 13)) {
		i+=13;
		table = suspend_table;
	} else if (!xstrncmp("txn_table", data_in+i, 9)) {
		i+=9;
		table = txn_table;
	} else if (!xstrncmp("cluster_day_usage_table", data_in+i, 23)) {
		i+=23;
		table = cluster_day_table;
	} else if (!xstrncmp("cluster_hour_usage_table", data_in+i, 24)) {
		i+=24;
		table = cluster_hour_table;
	} else if (!xstrncmp("cluster_month_usage_table", data_in+i, 25)) {
		i+=25;
		table = cluster_month_table;
	} else if (!xstrncmp("assoc_day_usage_table", data_in+i, 21)) {
		i+=21;
		table = assoc_day_table;
	} else if (!xstrncmp("assoc_hour_usage_table", data_in+i, 22)) {
		i+=22;
		table = assoc_hour_table;
	} else if (!xstrncmp("assoc_month_usage_table", data_in+i, 23)) {
		i+=23;
		table = assoc_month_table;
	} else if (!xstrncmp("wckey_day_usage_table", data_in+i, 21)) {
		i+=21;
		table = wckey_day_table;
	} else if (!xstrncmp("wckey_hour_usage_table", data_in+i, 22)) {
		i+=22;
		table = wckey_hour_table;
	} else if (!xstrncmp("wckey_month_usage_table", data_in+i, 23)) {
		i+=23;
		table = wckey_month_table;
	} else {
		error("unknown table in sql '%s'", data_in);
		rc = SLURM_ERROR;
		goto end_it;
	}
	/* get to the columns */
	if (!delete)
		while (data_in[i] && data_in[i-1] != '(' && data_in[i-1] != ';')
			i++;
	else
		while (data_in[i] && data_in[i-1] != ' ')
			i++;
	//info("table is %s '%s'", table, data_in+i);
	while (data_in[i] && data_in[i] != ')') {
		if (delete && !xstrncmp("where ", data_in+i, 6)) {
			i+=6;
			continue;
		} else if (!xstrncmp("period_start", data_in+i, 12)) {
			xstrcat(fields, "time_start");
			i+=12;
		} else if (!xstrncmp("period_end", data_in+i, 10)) {
			xstrcat(fields, "time_end");
			i+=10;
		} else if (!xstrncmp("cpu_count", data_in+i, 9)) {
			xstrcat(fields, "count");
			i+=9;
		} else if (!xstrncmp("jobid", data_in+i, 5)) {
			xstrcat(fields, "id_job");
			i+=5;
		} else if (!xstrncmp("stepid", data_in+i, 6)) {
			xstrcat(fields, "id_step");
			i+=6;
		} else if (!xstrncmp("associd", data_in+i, 7)) {
			xstrcat(fields, "id_assoc");
			i+=7;
		} else if (!xstrncmp("blockid", data_in+i, 7)) {
			xstrcat(fields, "id_block");
			i+=7;
		} else if (!xstrncmp("wckeyid", data_in+i, 7)) {
			xstrcat(fields, "id_wckey");
			i+=7;
		} else if (!xstrncmp("qos", data_in+i, 3)) {
			xstrcat(fields, "id_qos");
			i+=3;
		} else if (!xstrncmp("uid", data_in+i, 3)) {
			xstrcat(fields, "id_user");
			i+=3;
		} else if (!xstrncmp("gid", data_in+i, 3)) {
			xstrcat(fields, "id_group");
			i+=3;
		} else if (!xstrncmp("submit", data_in+i, 6)) {
			xstrcat(fields, "time_submit");
			i+=6;
		} else if (!xstrncmp("eligible", data_in+i, 8)) {
			xstrcat(fields, "time_eligible");
			i+=8;
		} else if (!xstrncmp("start", data_in+i, 5)) {
			xstrcat(fields, "time_start");
			i+=5;
		} else if (!xstrncmp("suspended", data_in+i, 9)) {
			xstrcat(fields, "time_suspended");
			i+=9;
		} else if (!xstrncmp("end", data_in+i, 3)) {
			xstrcat(fields, "time_end");
			i+=3;
		} else if (!xstrncmp("comp_code", data_in+i, 9)) {
			xstrcat(fields, "exit_code");
			i+=9;
		} else if (!xstrncmp("alloc_cpus", data_in+i, 10)) {
			xstrcat(fields, "cpus_alloc");
			i+=10;
		} else if (!xstrncmp("req_cpus", data_in+i, 8)) {
			xstrcat(fields, "cpus_req");
			i+=8;
		} else if (!xstrncmp("alloc_nodes", data_in+i, 11)) {
			i+=11;
			if (!delete) {
				xstrcat(fields, "nodes_alloc");
			} else {
				char *nodes =  NULL;
				while (data_in[i] && data_in[i-1] != '\'')
					i++;
				start = i;
				while (data_in[i] && data_in[i] != '\'')
					i++;
				if (!data_in[i]) {
					error("returning here nodes_alloc");
					rc = SLURM_ERROR;
					goto end_it;
				}
				xstrncat(nodes, data_in+start, (i-start));
				if (!fields)
					xstrcat(fields, "where ");
				xstrfmtcat(fields, "nodes_alloc='%s'", nodes);
				xfree(nodes);
				i++;
			}
		} else if (!xstrncmp("name", data_in+i, 4)) {
			if (table == job_table)
				xstrcat(fields, "job_name");
			else if (table == step_table)
				xstrcat(fields, "step_name");
			i+=4;
		} else if (!xstrncmp("tres_id", data_in+i, 7)) {
			start = i;
			while (data_in[i]
			       && data_in[i] != ',' && data_in[i] != ')') {
				i++;
			}
			if (!data_in[i]) {
				error("returning here end");
				rc = SLURM_ERROR;
				goto end_it;
			}
			xstrncat(fields, data_in+start, (i-start));
		} else if (!xstrncmp("id", data_in+i, 2)) {
			i+=2;
			if ((table == assoc_day_table)
			    || (table == assoc_hour_table)
			    || (table == assoc_month_table)) {
				char *id_assoc = NULL;
				while (data_in[i] && data_in[i-1] != '=') {
					i++;
				}
				start = i;
				while (data_in[i]
				       && data_in[i] != ' '
				       && data_in[i] != ';') {
					i++;
				}
				if (!data_in[i]) {
					error("returning at id_assoc");
					rc = SLURM_ERROR;
					goto end_it;
				}
				if (data_in[i] == ' ') {
					while (data_in[i] && data_in[i] == ' ')
						i++;
					while (data_in[i] && data_in[i] == '|')
						i++;
					while (data_in[i] && data_in[i] == ' ')
						i++;
				}
				xstrncat(id_assoc, data_in+start, (i-start));
				if (!fields)
					xstrcat(fields, "where ");
				xstrfmtcat(fields, "id_assoc=%s", id_assoc);
				xfree(id_assoc);
			} else
				xstrcat(fields, "job_db_inx");
		} else if (!xstrncmp("cluster_nodes", data_in+i, 13)) {
			/* this is here just to make it easier to
			   handle the cluster field. */
			xstrcat(fields, "cluster_nodes");
			i+=13;
		} else if (!xstrncmp("cluster", data_in+i, 7)) {
			i+=7;
			if (!delete) {
				cluster_inx = cnt;
				if (cnt)
					fields[strlen(fields)-2] = '\0';
			} else {
				while (data_in[i] && data_in[i-1] != '\'')
					i++;
				start = i;
				while (data_in[i] && data_in[i] != '\'')
					i++;
				if (!data_in[i]) {
					error("returning here cluster");
					rc = SLURM_ERROR;
					goto end_it;
				}
				xfree(*cluster_name);
				*cluster_name = xstrndup(data_in+start,
							 (i-start));
				i++;
			}
		} else {
			start = i;
			while (data_in[i]
			       && data_in[i] != ',' && data_in[i] != ')') {
				i++;
			}
			if (!data_in[i]) {
				error("returning here end");
				rc = SLURM_ERROR;
				goto end_it;
			}
			xstrncat(fields, data_in+start, (i-start));
		}
		if (data_in[i]) {
			if (!delete || ((table != assoc_day_table)
					&& (table != assoc_hour_table)
					&& (table != assoc_month_table)
					&& (table != job_table))) {
				if (data_in[i] == ',')
					xstrcat(fields, ", ");
				else if (data_in[i] == ')'
					 || data_in[i] == ';') {
					break;
				} else {
					error("unknown char '%s'", data_in+i);
					rc = SLURM_ERROR;
					goto end_it;
				}
				i++;
			} else {
				if (data_in[i] == ';')
					break;
			}
			while (data_in[i] && data_in[i] == ' ')
				i++;
		}
		cnt++;
	}

	if (data_in[i] && data_in[i] == ')') {
		ending_end = i;
		ending_start = 0;
		while (data_in[ending_end] && data_in[ending_end-1] != ';') {
			if (!xstrncmp(data_in+ending_end,
				     "on duplicate key", 16)) {
				ending_start = ending_end;
			}
			if (ending_start) {
				if (!xstrncmp("period_start",
					      data_in+ending_end, 12)) {
					xstrcat(ending, "time_start");
					ending_end+=12;
				} else if (!xstrncmp("period_end",
						     data_in+ending_end, 10)) {
					xstrcat(ending, "time_end");
					ending_end+=10;
				} else if (!xstrncmp("jobid",
						     data_in+ending_end, 5)) {
					xstrcat(ending, "id_job");
					ending_end+=5;
				} else if (!xstrncmp("stepid",
						     data_in+ending_end, 6)) {
					xstrcat(ending, "id_step");
					ending_end+=6;
				} else if (!xstrncmp("associd",
						     data_in+ending_end, 7)) {
					xstrcat(ending, "id_assoc");
					ending_end+=7;
				} else if (!xstrncmp("blockid",
						     data_in+ending_end, 7)) {
					xstrcat(ending, "id_block");
					ending_end+=7;
				} else if (!xstrncmp("wckeyid",
						     data_in+ending_end, 7)) {
					xstrcat(ending, "id_wckey");
					ending_end+=7;
				} else if (!xstrncmp("uid",
						     data_in+ending_end, 3)) {
					xstrcat(ending, "id_user");
					ending_end+=3;
				} else if (!xstrncmp("gid",
						     data_in+ending_end, 3)) {
					xstrcat(ending, "id_group");
					ending_end+=3;
				} else if (!xstrncmp("submit",
						     data_in+ending_end, 6)) {
					xstrcat(ending, "time_submit");
					ending_end+=6;
				} else if (!xstrncmp("eligible",
						     data_in+ending_end, 8)) {
					xstrcat(ending, "time_eligible");
					ending_end+=8;
				} else if (!xstrncmp("start",
						     data_in+ending_end, 5)) {
					xstrcat(ending, "time_start");
					ending_end+=5;
				} else if (!xstrncmp("suspended",
						     data_in+ending_end, 9)) {
					xstrcat(ending, "time_suspended");
					ending_end+=9;
				} else if (!xstrncmp("end",
						     data_in+ending_end, 3)) {
					xstrcat(ending, "time_end");
					ending_end+=3;
				} else if (!xstrncmp("comp_code",
						     data_in+ending_end, 9)) {
					xstrcat(ending, "exit_code");
					ending_end+=9;
				} else if (!xstrncmp("alloc_cpus",
						     data_in+ending_end, 10)) {
					xstrcat(ending, "cpus_alloc");
					ending_end+=10;
				} else if (!xstrncmp("req_cpus",
						     data_in+ending_end, 8)) {
					xstrcat(ending, "cpus_req");
					ending_end+=8;
				} else if (!xstrncmp("alloc_nodes",
						     data_in+ending_end, 11)) {
					xstrcat(ending, "nodes_alloc");
					ending_end+=11;
				} else if (!xstrncmp("name",
						     data_in+ending_end, 4)) {
					if (table == job_table)
						xstrcat(ending, "job_name");
					else if (table == step_table)
						xstrcat(ending, "step_name");
					ending_end+=4;
				} else if (!xstrncmp("id",
						     data_in+ending_end, 2)) {
					if ((table == assoc_day_table)
					    || (table == assoc_hour_table)
					    || (table == assoc_month_table))
						xstrcat(ending, "id_assoc");
					else
						xstrcat(ending, "job_db_inx");
					ending_end+=2;
				}

				if (data_in[ending_end])
					xstrcatchar(ending,
						    data_in[ending_end]);
			}
			ending_end++;
		}

		/* get values */
		while (i < ending_start) {
			/* get to the start of the values */
			while ((i < ending_start) && data_in[i-1] != '(')
				i++;

			/* find the values */
			cnt = 0;
			while ((i < ending_start) && data_in[i] != ')') {
				start = i;
				while (i < ending_start) {
					if (data_in[i] == ',' ||
					    (data_in[i] == ')' &&
					     data_in[i-1] != '('))
						break;
					i++;
				}
				if (!data_in[i]) {
					rc = SLURM_ERROR;
					goto end_it;
				}
				if (cnt == cluster_inx) {
					/* get the cluster name and remove the
					   ticks */
					xstrncat(new_cluster_name,
						 data_in+start+1, (i-start-2));
					if (*cluster_name) {
						if (xstrcmp(*cluster_name,
							    new_cluster_name))
							new_cluster = 1;
						else
							xfree(new_cluster_name);
					} else {
						xfree(*cluster_name);
						*cluster_name =
							new_cluster_name;
						new_cluster_name = NULL;
					}
				} else {
					xstrncat(new_vals, data_in+start,
						 (i-start));

					if (data_in[i]) {
						if (data_in[i] == ',')
							xstrcat(new_vals, ", ");
						else if (data_in[i] == ')'
							 || data_in[i] == ';') {
							i++;
							break;
						} else {
							error("unknown char "
							      "'%s'",
							      data_in+i);
							rc = SLURM_ERROR;
							goto end_it;
						}
					}
				}
				i++;
				while ((i < ending_start) && data_in[i] == ' ')
					i++;
				cnt++;
			}
			if (new_cluster) {
				/* info("new cluster, adding insert\n%s " */
				/*      "\"%s_%s\" (%s) values %s %s", */
				/*      beginning, cluster_name, table, */
				/*      fields, vals, ending); */
				xstrfmtcat(data_out,
					   "%s \"%s_%s\" (%s) values %s %s",
					   beginning, *cluster_name,
					   table, fields, vals, ending);
				new_cluster = 0;
				xfree(vals);
				xfree(*cluster_name);
				*cluster_name = new_cluster_name;
				new_cluster_name = NULL;
			}

			if (new_vals) {
				if (vals)
					xstrfmtcat(vals, ", (%s)", new_vals);
				else
					xstrfmtcat(vals, "(%s)", new_vals);
				xfree(new_vals);
			}
		}
		i = ending_end;
	}

	if (!*cluster_name) {
		error("No cluster given for %s", table);
		goto end_it;
	}

	if (!delete) {
		/* info("adding insert\n%s \"%s_%s\" (%s) values %s %s",
		   beginning, cluster_name, table, fields, vals, ending); */
		xstrfmtcat(data_out, "%s \"%s_%s\" (%s) values %s %s",
			   beginning, *cluster_name, table, fields,
			   vals, ending);
	} else {
		if (fields) {
			/* info("adding delete\n%s \"%s_%s\" %s", */
			/*      beginning, cluster_name, table, fields); */
			xstrfmtcat(data_out, "%s \"%s_%s\" %s",
				   beginning, *cluster_name, table, fields);
		} else {
			/* info("adding drop\ndrop table \"%s_%s\";", */
			/*      cluster_name, table); */
			xstrfmtcat(data_out, "drop table \"%s_%s\";",
				   *cluster_name, table);
		}
	}

end_it:
	xfree(beginning);
	xfree(ending);
	xfree(fields);
	xfree(vals);
	*data_full_out = data_out;
	//info("returning\n%s", data_out);
	if (rc == SLURM_ERROR)
		return -1;
	return i;
}

static int _process_old_sql(char **data)
{
	int i = 0;
	char *data_in = *data;
	char *data_out = NULL;
	int rc = SLURM_SUCCESS;
	char *cluster_name = NULL;

	while (data_in[i]) {
		if ((rc = _process_old_sql_line(
			     data_in+i, &cluster_name, &data_out)) == -1)
			break;
		i += rc;
	}
	//rc = -1;
	xfree(cluster_name);
	xfree(data_in);
	if (rc == -1)
		xfree(data_out);
	//info("returning\n%s", data_out);
	*data = data_out;
	return rc;
}

static char *_get_archive_columns(purge_type_t type)
{
	char **cols = NULL;
	char *tmp = NULL;
	int col_count = 0, i = 0;

	xfree(cols);

	switch (type) {
	case PURGE_EVENT:
		cols      = event_req_inx;
		col_count = EVENT_REQ_COUNT;
		break;
	case PURGE_SUSPEND:
		cols      = suspend_req_inx;
		col_count = SUSPEND_REQ_COUNT;
		break;
	case PURGE_RESV:
		cols      = resv_req_inx;
		col_count = RESV_REQ_COUNT;
		break;
	case PURGE_JOB:
		cols      = job_req_inx;
		col_count = JOB_REQ_COUNT;
		break;
	case PURGE_STEP:
		cols      = step_req_inx;
		col_count = STEP_REQ_COUNT;
		break;
	case PURGE_TXN:
		cols      = txn_req_inx;
		col_count = TXN_REQ_COUNT;
		break;
	case PURGE_USAGE:
		cols      = usage_req_inx;
		col_count = USAGE_COUNT;
		break;
	case PURGE_CLUSTER_USAGE:
		cols      = cluster_req_inx;
		col_count = CLUSTER_COUNT;
		break;
	default:
		xassert(0);
		return NULL;
	}

	xstrfmtcat(tmp, "%s", cols[0]);
	for (i=1; i<col_count; i++) {
		xstrfmtcat(tmp, ", %s", cols[i]);
	}

	return tmp;
}


static buf_t *_pack_archive_events(MYSQL_RES *result, char *cluster_name,
				   uint32_t cnt, uint32_t usage_info,
				   time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_event_t event;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_EVENTS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[EVENT_REQ_START]);

		memset(&event, 0, sizeof(local_event_t));

		event.cluster_nodes = row[EVENT_REQ_CNODES];
		event.node_name = row[EVENT_REQ_NODE];
		event.period_end = row[EVENT_REQ_END];
		event.period_start = row[EVENT_REQ_START];
		event.reason = row[EVENT_REQ_REASON];
		event.reason_uid = row[EVENT_REQ_REASON_UID];
		event.state = row[EVENT_REQ_STATE];
		event.tres_str = row[EVENT_REQ_TRES];

		_pack_local_event(&event, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_events(uint16_t rpc_version, buf_t *buffer,
			  char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_event_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, event_table, event_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", event_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");

	for (i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_event_t));
		if (_unpack_local_event(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.period_start,
			   object.period_end,
			   object.node_name,
			   object.cluster_nodes,
			   object.reason,
			   object.reason_uid,
			   object.state,
			   object.tres_str);


		_free_local_event_members(&object);
	}
//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static buf_t *_pack_archive_jobs(MYSQL_RES *result, char *cluster_name,
				 uint32_t cnt, uint32_t usage_info,
				 time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_job_t job;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_JOBS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[JOB_REQ_SUBMIT]);

		memset(&job, 0, sizeof(local_job_t));

		job.account = row[JOB_REQ_ACCOUNT];
		job.admin_comment = row[JOB_REQ_ADMIN_COMMENT];
		job.alloc_nodes = row[JOB_REQ_ALLOC_NODES];
		job.associd = row[JOB_REQ_ASSOCID];
		job.array_jobid = row[JOB_REQ_ARRAYJOBID];
		job.array_max_tasks = row[JOB_REQ_ARRAY_MAX];
		job.array_taskid = row[JOB_REQ_ARRAYTASKID];
		job.array_task_pending = row[JOB_REQ_ARRAY_TASK_PENDING];
		job.array_task_str = row[JOB_REQ_ARRAY_TASK_STR];
		job.script = row[JOB_REQ_SCRIPT];
		job.blockid = row[JOB_REQ_BLOCKID];
		job.constraints = row[JOB_REQ_CONSTRAINTS];
		job.deleted = row[JOB_REQ_DELETED];
		job.derived_ec = row[JOB_REQ_DERIVED_EC];
		job.derived_es = row[JOB_REQ_DERIVED_ES];
		job.env = row[JOB_REQ_ENV];
		job.exit_code = row[JOB_REQ_EXIT_CODE];
		job.flags = row[JOB_REQ_FLAGS];
		job.timelimit = row[JOB_REQ_TIMELIMIT];
		job.eligible = row[JOB_REQ_ELIGIBLE];
		job.end = row[JOB_REQ_END];
		job.gid = row[JOB_REQ_GID];
		job.gres_used = row[JOB_REQ_GRES_USED];
		job.het_job_id = row[JOB_REQ_HET_JOB_ID];
		job.het_job_offset = row[JOB_REQ_HET_JOB_OFFSET];
		job.job_db_inx = row[JOB_REQ_DB_INX];
		job.jobid = row[JOB_REQ_JOBID];
		job.kill_requid = row[JOB_REQ_KILL_REQUID];
		job.mcs_label = row[JOB_REQ_MCS_LABEL];
		job.mod_time = row[JOB_REQ_MOD_TIME];
		job.name = row[JOB_REQ_NAME];
		job.nodelist = row[JOB_REQ_NODELIST];
		job.node_inx = row[JOB_REQ_NODE_INX];
		job.partition = row[JOB_REQ_PARTITION];
		job.priority = row[JOB_REQ_PRIORITY];
		job.qos = row[JOB_REQ_QOS];
		job.req_cpus = row[JOB_REQ_REQ_CPUS];
		job.req_mem = row[JOB_REQ_REQ_MEM];
		job.resvid = row[JOB_REQ_RESVID];
		job.start = row[JOB_REQ_START];
		job.state = row[JOB_REQ_STATE];
		job.state_reason_prev = row[JOB_REQ_STATE_REASON];
		job.submit = row[JOB_REQ_SUBMIT];
		job.suspended = row[JOB_REQ_SUSPENDED];
		job.system_comment = row[JOB_REQ_SYSTEM_COMMENT];
		job.track_steps = row[JOB_REQ_TRACKSTEPS];
		job.tres_alloc_str = row[JOB_REQ_TRESA];
		job.tres_req_str = row[JOB_REQ_TRESR];
		job.uid = row[JOB_REQ_UID];
		job.wckey = row[JOB_REQ_WCKEY];
		job.wckey_id = row[JOB_REQ_WCKEYID];
		job.work_dir = row[JOB_REQ_WORK_DIR];

		_pack_local_job(&job, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_jobs(uint16_t rpc_version, buf_t *buffer,
			char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	int safe_attributes[] = {
		JOB_REQ_ARRAY_MAX,
		JOB_REQ_ARRAY_TASK_PENDING,
		JOB_REQ_ALLOC_NODES,
		JOB_REQ_ASSOCID,
		JOB_REQ_ARRAYJOBID,
		JOB_REQ_ARRAYTASKID,
		JOB_REQ_DELETED,
		JOB_REQ_DERIVED_EC,
		JOB_REQ_EXIT_CODE,
		JOB_REQ_FLAGS,
		JOB_REQ_TIMELIMIT,
		JOB_REQ_ELIGIBLE,
		JOB_REQ_END,
		JOB_REQ_GID,
		JOB_REQ_GRES_USED,
		JOB_REQ_HET_JOB_ID,
		JOB_REQ_HET_JOB_OFFSET,
		JOB_REQ_DB_INX,
		JOB_REQ_JOBID,
		JOB_REQ_KILL_REQUID,
		JOB_REQ_MOD_TIME,
		JOB_REQ_NAME,
		JOB_REQ_PARTITION,
		JOB_REQ_PRIORITY,
		JOB_REQ_QOS,
		JOB_REQ_REQ_CPUS,
		JOB_REQ_REQ_MEM,
		JOB_REQ_RESVID,
		JOB_REQ_START,
		JOB_REQ_STATE,
		JOB_REQ_STATE_REASON,
		JOB_REQ_SUBMIT,
		JOB_REQ_SUSPENDED,
		JOB_REQ_TRACKSTEPS,
		JOB_REQ_UID,
		JOB_REQ_WCKEY,
		JOB_REQ_WCKEYID,
		JOB_REQ_WORK_DIR,
		JOB_REQ_TRESA,
		JOB_REQ_TRESR,
		JOB_REQ_COUNT };

	/* Sync w/ job_table_fields where text/tinytext can be NULL */
	int null_attributes[] = {
		JOB_REQ_ACCOUNT,
		JOB_REQ_ADMIN_COMMENT,
		JOB_REQ_ARRAY_TASK_STR,
		JOB_REQ_SCRIPT,
		JOB_REQ_BLOCKID,
		JOB_REQ_CONSTRAINTS,
		JOB_REQ_DERIVED_ES,
		JOB_REQ_ENV,
		JOB_REQ_MCS_LABEL,
		JOB_REQ_NODELIST,
		JOB_REQ_NODE_INX,
		JOB_REQ_SYSTEM_COMMENT,
		JOB_REQ_COUNT };

	local_job_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, job_table,job_req_inx[safe_attributes[0]]);
	for (i = 1; safe_attributes[i] < JOB_REQ_COUNT; i++)
		xstrfmtcat(insert, ", %s", job_req_inx[safe_attributes[i]]);
	/* Some attributes that might be NULL require special handling */
	for (i = 0; null_attributes[i] < JOB_REQ_COUNT; i++)
		xstrfmtcat(insert, ", %s", job_req_inx[null_attributes[i]]);
	xstrcat(insert, ") values ");

	for (i = 0; i < rec_cnt; i++) {

		if (_unpack_local_job(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrcat(format, "('%s'");
		for(int j = 1; safe_attributes[j] < JOB_REQ_COUNT; j++) {
			xstrcat(format, ", '%s'");
		}

		/* special handling for NULL attributes */
		if (object.account == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.admin_comment == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.array_task_str == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.script == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.blockid == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.constraints == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.derived_es == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.env == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.mcs_label == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.nodelist == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.node_inx == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");
		if (object.system_comment == NULL)
			xstrcat(format, ", %s");
		else
			xstrcat(format, ", '%s'");

		xstrcat(format, ")");

		xstrfmtcat(insert, format,
			   object.array_max_tasks,
			   object.array_task_pending,
			   object.alloc_nodes,
			   object.associd,
			   object.array_jobid,
			   object.array_taskid,
			   object.deleted,
			   object.derived_ec,
			   object.exit_code,
			   object.flags,
			   object.timelimit,
			   object.eligible,
			   object.end,
			   object.gid,
			   object.gres_used,
			   object.het_job_id,
			   object.het_job_offset,
			   object.job_db_inx,
			   object.jobid,
			   object.kill_requid,
			   object.mod_time,
			   object.name,
			   object.partition,
			   object.priority,
			   object.qos,
			   object.req_cpus,
			   object.req_mem,
			   object.resvid,
			   object.start,
			   object.state,
			   object.state_reason_prev,
			   object.submit,
			   object.suspended,
			   object.track_steps,
			   object.uid,
			   object.wckey,
			   object.wckey_id,
			   object.work_dir,
			   object.tres_alloc_str,
			   object.tres_req_str,
			   (object.account == NULL) ?
				"NULL" : object.account,
			   (object.admin_comment == NULL) ?
				"NULL" : object.admin_comment,
			   (object.array_task_str == NULL) ?
				"NULL" : object.array_task_str,
			   (object.script == NULL) ?
				"NULL" : object.script,
			   (object.blockid == NULL) ?
				"NULL" : object.blockid,
			   (object.constraints == NULL) ?
				"NULL" : object.constraints,
			   (object.derived_es == NULL) ?
				"NULL" : object.derived_es,
			   (object.env == NULL) ?
				"NULL" : object.env,
			   (object.mcs_label == NULL) ?
				"NULL" : object.mcs_label,
			   (object.nodelist == NULL) ?
				"NULL" : object.nodelist,
			   (object.node_inx == NULL) ?
				"NULL" : object.node_inx,
			   (object.system_comment == NULL) ?
				"NULL" : object.system_comment);

		_free_local_job_members(&object);
		xfree(format);
	}
//	END_TIMER2("step query");
//	info("job query took %s", TIME_STR);

	return insert;
}

static buf_t *_pack_archive_resvs(MYSQL_RES *result, char *cluster_name,
				  uint32_t cnt, uint32_t usage_info,
				  time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_resv_t resv;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_RESVS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[RESV_REQ_START]);

		memset(&resv, 0, sizeof(local_resv_t));

		resv.assocs = row[RESV_REQ_ASSOCS];
		resv.deleted = row[RESV_REQ_DELETED];
		resv.flags = row[RESV_REQ_FLAGS];
		resv.id = row[RESV_REQ_ID];
		resv.name = row[RESV_REQ_NAME];
		resv.nodes = row[RESV_REQ_NODES];
		resv.node_inx = row[RESV_REQ_NODE_INX];
		resv.time_end = row[RESV_REQ_END];
		resv.time_start = row[RESV_REQ_START];
		resv.tres_str = row[RESV_REQ_TRES];
		resv.unused_wall = row[RESV_REQ_UNUSED];

		_pack_local_resv(&resv, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_resvs(uint16_t rpc_version, buf_t *buffer,
			 char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_resv_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, resv_table, resv_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", resv_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_resv_t));
		if (_unpack_local_resv(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.id,
			   object.deleted,
			   object.assocs,
			   object.flags,
			   object.tres_str,
			   object.nodes,
			   object.node_inx,
			   object.name,
			   object.time_start,
			   object.time_end,
			   object.unused_wall);

		_free_local_resv_members(&object);
	}
//	END_TIMER2("step query");
//	info("resv query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static buf_t *_pack_archive_steps(MYSQL_RES *result, char *cluster_name,
				  uint32_t cnt, uint32_t usage_info,
				  time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_step_t step;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_STEP_START, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[STEP_REQ_START]);

		memset(&step, 0, sizeof(local_step_t));

		step.act_cpufreq = row[STEP_REQ_ACT_CPUFREQ];
		step.deleted = row[STEP_REQ_DELETED];
		step.consumed_energy = row[STEP_REQ_CONSUMED_ENERGY];
		step.exit_code = row[STEP_REQ_EXIT_CODE];
		step.job_db_inx = row[STEP_REQ_DB_INX];
		step.kill_requid = row[STEP_REQ_KILL_REQUID];
		step.name = row[STEP_REQ_NAME];
		step.nodelist = row[STEP_REQ_NODELIST];
		step.nodes = row[STEP_REQ_NODES];
		step.node_inx = row[STEP_REQ_NODE_INX];
		step.period_end = row[STEP_REQ_END];
		step.period_start = row[STEP_REQ_START];
		step.period_suspended = row[STEP_REQ_SUSPENDED];
		step.req_cpufreq_min = row[STEP_REQ_REQ_CPUFREQ_MIN];
		step.req_cpufreq_max = row[STEP_REQ_REQ_CPUFREQ_MAX];
		step.req_cpufreq_gov = row[STEP_REQ_REQ_CPUFREQ_GOV];
		step.state = row[STEP_REQ_STATE];
		step.stepid = row[STEP_REQ_STEPID];
		step.step_het_comp = row[STEP_REQ_STEP_HET_COMP];
		step.submit_line = row[STEP_REQ_SUBMIT_LINE];
		step.sys_sec = row[STEP_REQ_SYS_SEC];
		step.sys_usec = row[STEP_REQ_SYS_USEC];
		step.tasks = row[STEP_REQ_TASKS];
		step.task_dist = row[STEP_REQ_TASKDIST];
		step.tres_alloc_str = row[STEP_REQ_TRES];
		step.tres_usage_in_ave = row[STEP_TRES_USAGE_IN_AVE];
		step.tres_usage_in_max = row[STEP_TRES_USAGE_IN_MAX];
		step.tres_usage_in_max_nodeid =
			row[STEP_TRES_USAGE_IN_MAX_NODEID];
		step.tres_usage_in_max_taskid =
			row[STEP_TRES_USAGE_IN_MAX_TASKID];
		step.tres_usage_in_min = row[STEP_TRES_USAGE_IN_MIN];
		step.tres_usage_in_min_nodeid =
			row[STEP_TRES_USAGE_IN_MIN_NODEID];
		step.tres_usage_in_min_taskid =
			row[STEP_TRES_USAGE_IN_MIN_TASKID];
		step.tres_usage_in_tot = row[STEP_TRES_USAGE_IN_TOT];
		step.tres_usage_out_ave = row[STEP_TRES_USAGE_OUT_AVE];
		step.tres_usage_out_max = row[STEP_TRES_USAGE_OUT_MAX];
		step.tres_usage_out_max_nodeid =
			row[STEP_TRES_USAGE_OUT_MAX_NODEID];
		step.tres_usage_out_max_taskid =
			row[STEP_TRES_USAGE_OUT_MAX_TASKID];
		step.tres_usage_out_min = row[STEP_TRES_USAGE_OUT_MAX];
		step.tres_usage_out_min_nodeid =
			row[STEP_TRES_USAGE_OUT_MIN_NODEID];
		step.tres_usage_out_min_taskid =
			row[STEP_TRES_USAGE_OUT_MIN_TASKID];
		step.tres_usage_out_tot = row[STEP_TRES_USAGE_OUT_TOT];
		step.user_sec = row[STEP_REQ_USER_SEC];
		step.user_usec = row[STEP_REQ_USER_USEC];

		_pack_local_step(&step, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_steps(uint16_t rpc_version, buf_t *buffer,
			 char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_step_t object;
	int i;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, step_table, step_req_inx[0]);
	xstrcat(format, "('%s'");
	for (i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", step_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for (i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_step_t));
		if (_unpack_local_step(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		if (!object.step_het_comp)
			object.step_het_comp = xstrdup_printf("%u", NO_VAL);

		xstrfmtcat(insert, format,
			   object.job_db_inx,
			   object.stepid,
			   object.step_het_comp,
			   object.deleted,
			   object.period_start,
			   object.period_end,
			   object.period_suspended,
			   object.name,
			   object.nodelist,
			   object.node_inx,
			   object.state,
			   object.kill_requid,
			   object.exit_code,
			   object.nodes,
			   object.tasks,
			   object.task_dist,
			   object.user_sec,
			   object.user_usec,
			   object.sys_sec,
			   object.sys_usec,
			   object.act_cpufreq,
			   object.consumed_energy,
			   object.req_cpufreq_max,
			   object.req_cpufreq_min,
			   object.req_cpufreq_gov,
			   object.submit_line,
			   object.tres_alloc_str,
			   object.tres_usage_in_ave,
			   object.tres_usage_in_max,
			   object.tres_usage_in_max_nodeid,
			   object.tres_usage_in_max_taskid,
			   object.tres_usage_in_min,
			   object.tres_usage_in_min_nodeid,
			   object.tres_usage_in_min_taskid,
			   object.tres_usage_in_tot,
			   object.tres_usage_out_ave,
			   object.tres_usage_out_max,
			   object.tres_usage_out_max_nodeid,
			   object.tres_usage_out_max_taskid,
			   object.tres_usage_out_min,
			   object.tres_usage_out_min_nodeid,
			   object.tres_usage_out_min_taskid,
			   object.tres_usage_out_tot);

		_free_local_step_members(&object);
	}
//	END_TIMER2("step query");
//	info("step query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static buf_t *_pack_archive_suspends(MYSQL_RES *result, char *cluster_name,
				     uint32_t cnt, uint32_t usage_info,
				     time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_suspend_t suspend;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_JOB_SUSPEND, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[SUSPEND_REQ_START]);

		memset(&suspend, 0, sizeof(local_suspend_t));

		suspend.job_db_inx = row[SUSPEND_REQ_DB_INX];
		suspend.associd = row[SUSPEND_REQ_ASSOCID];
		suspend.period_start = row[SUSPEND_REQ_START];
		suspend.period_end = row[SUSPEND_REQ_END];

		_pack_local_suspend(&suspend, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}


/* returns sql statement from archived data or NULL on error */
static char *_load_suspend(uint16_t rpc_version, buf_t *buffer,
			   char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_suspend_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, suspend_table, suspend_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", suspend_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_suspend_t));
		if (_unpack_local_suspend(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.job_db_inx,
			   object.associd,
			   object.period_start,
			   object.period_end);

		_free_local_suspend_members(&object);
	}
//	END_TIMER2("suspend query");
//	info("suspend query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static buf_t *_pack_archive_txns(MYSQL_RES *result, char *cluster_name,
				 uint32_t cnt, uint32_t usage_info,
				 time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_txn_t txn;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_TXN, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[TXN_REQ_TS]);

		memset(&txn, 0, sizeof(local_txn_t));

		txn.id = row[TXN_REQ_ID];
		txn.timestamp = row[TXN_REQ_TS];
		txn.action = row[TXN_REQ_ACTION];
		txn.name = row[TXN_REQ_NAME];
		txn.actor = row[TXN_REQ_ACTOR];
		txn.info = row[TXN_REQ_INFO];
		txn.cluster = row[TXN_REQ_CLUSTER];

		_pack_local_txn(&txn, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}


/* returns sql statement from archived data or NULL on error */
static char *_load_txn(uint16_t rpc_version, buf_t *buffer,
		       char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_txn_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s\" (%s",
		   txn_table, txn_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<TXN_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", txn_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_txn_t));
		if (_unpack_local_txn(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.id,
			   object.timestamp,
			   object.action,
			   object.name,
			   object.actor,
			   object.info,
			   object.cluster);

		_free_local_txn_members(&object);
	}
//	END_TIMER2("txn query");
//	info("txn query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static buf_t *_pack_archive_usage(MYSQL_RES *result, char *cluster_name,
				  uint32_t cnt, uint32_t usage_info,
				  time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_usage_t usage;
	uint16_t type = usage_info & 0x0000ffff;
	uint16_t period = usage_info >> 16;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(type, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);
	pack16(period, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[USAGE_START]);

		memset(&usage, 0, sizeof(local_usage_t));

		usage.id = row[USAGE_ID];
		usage.tres_id = row[USAGE_TRES];
		usage.time_start = row[USAGE_START];
		usage.alloc_secs = row[USAGE_ALLOC];
		usage.creation_time = row[USAGE_CREATION_TIME];
		usage.mod_time = row[USAGE_MOD_TIME];
		usage.deleted = row[USAGE_DELETED];

		_pack_local_usage(&usage, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_usage(uint16_t rpc_version, buf_t *buffer,
			 char *cluster_name, uint16_t type, uint16_t period,
			 uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL, *my_usage_table = NULL;
	local_usage_t object;
	int i = 0;

	switch (type) {
	case DBD_GOT_ASSOC_USAGE:
		switch (period) {
		case DBD_ROLLUP_HOUR:
			my_usage_table = assoc_hour_table;
			break;
		case DBD_ROLLUP_DAY:
			my_usage_table = assoc_day_table;
			break;
		case DBD_ROLLUP_MONTH:
			my_usage_table = assoc_month_table;
			break;
		default:
			error("Unknown period");
			return NULL;
			break;
		}
		break;
	case DBD_GOT_WCKEY_USAGE:
		switch (period) {
		case DBD_ROLLUP_HOUR:
			my_usage_table = wckey_hour_table;
			break;
		case DBD_ROLLUP_DAY:
			my_usage_table = wckey_day_table;
			break;
		case DBD_ROLLUP_MONTH:
			my_usage_table = wckey_month_table;
			break;
		default:
			error("Unknown period");
			return NULL;
			break;
		}
		break;
	default:
		error("Unknown usage type %d", type);
		return NULL;
		break;
	}

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, my_usage_table, usage_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<USAGE_COUNT; i++) {
		xstrfmtcat(insert, ", %s", usage_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_usage_t));
		if (_unpack_local_usage(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.id,
			   object.tres_id,
			   object.time_start,
			   object.alloc_secs,
			   object.creation_time,
			   object.mod_time,
			   object.deleted);

		_free_local_usage_members(&object);
	}
//	END_TIMER2("usage query");
//	info("usage query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static buf_t *_pack_archive_cluster_usage(MYSQL_RES *result, char *cluster_name,
					  uint32_t cnt, uint32_t usage_info,
					  time_t *period_start)
{
	MYSQL_ROW row;
	buf_t *buffer;
	local_cluster_usage_t usage;
	uint16_t period = usage_info >> 16;

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_CLUSTER_USAGE, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);
	pack16(period, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (period_start && !*period_start)
			*period_start = slurm_atoul(row[CLUSTER_START]);

		memset(&usage, 0, sizeof(local_cluster_usage_t));

		usage.tres_id = row[CLUSTER_TRES];
		usage.time_start = row[CLUSTER_START];
		usage.tres_cnt = row[CLUSTER_CNT];
		usage.alloc_secs = row[CLUSTER_ACPU];
		usage.down_secs = row[CLUSTER_DCPU];
		usage.pdown_secs = row[CLUSTER_PDCPU];
		usage.idle_secs = row[CLUSTER_ICPU];
		usage.plan_secs = row[CLUSTER_PCPU];
		usage.over_secs = row[CLUSTER_OCPU];
		usage.creation_time = row[CLUSTER_CREATION_TIME];
		usage.mod_time = row[CLUSTER_MOD_TIME];
		usage.deleted = row[CLUSTER_DELETED];

		_pack_local_cluster_usage(
			&usage, SLURM_PROTOCOL_VERSION, buffer);
	}

	return buffer;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_cluster_usage(uint16_t rpc_version, buf_t *buffer,
				 char *cluster_name, uint16_t period,
				 uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL, *my_usage_table = NULL;
	local_cluster_usage_t object;
	int i = 0;

	switch (period) {
	case DBD_ROLLUP_HOUR:
		my_usage_table = cluster_hour_table;
		break;
	case DBD_ROLLUP_DAY:
		my_usage_table = cluster_day_table;
		break;
	case DBD_ROLLUP_MONTH:
		my_usage_table = cluster_month_table;
		break;
	default:
		error("Unknown period");
		return NULL;
		break;
	}

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, my_usage_table, cluster_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<CLUSTER_COUNT; i++) {
		xstrfmtcat(insert, ", %s", cluster_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_cluster_usage_t));
		if (_unpack_local_cluster_usage(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}

		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.tres_id,
			   object.time_start,
			   object.tres_cnt,
			   object.alloc_secs,
			   object.down_secs,
			   object.pdown_secs,
			   object.idle_secs,
			   object.plan_secs,
			   object.over_secs,
			   object.creation_time,
			   object.mod_time,
			   object.deleted);

		_free_local_cluster_members(&object);
	}
//	END_TIMER2("usage query");
//	info("usage query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_table(purge_type_t type, mysql_conn_t *mysql_conn,
			       char *cluster_name, time_t period_end,
			       char *arch_dir, uint32_t archive_period,
			       char *sql_table, uint32_t usage_info)
{
	MYSQL_RES *result = NULL;
	char *cols = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	buf_t *buffer;
	int error_code = 0;
	buf_t *(*pack_func)(MYSQL_RES *result, char *cluster_name,
			    uint32_t cnt, uint32_t usage_info,
			    time_t *period_start);

	cols = _get_archive_columns(type);

	switch (type) {
	case PURGE_EVENT:
		pack_func = &_pack_archive_events;
		break;
	case PURGE_SUSPEND:
		pack_func = &_pack_archive_suspends;
		break;
	case PURGE_RESV:
		pack_func = &_pack_archive_resvs;
		break;
	case PURGE_JOB:
		pack_func = &_pack_archive_jobs;
		break;
	case PURGE_STEP:
		pack_func = &_pack_archive_steps;
		break;
	case PURGE_TXN:
		pack_func = &_pack_archive_txns;
		break;
	case PURGE_USAGE:
		pack_func = &_pack_archive_usage;
		break;
	case PURGE_CLUSTER_USAGE:
		pack_func = &_pack_archive_cluster_usage;
		break;
	default:
		fatal("Unknown purge type: %d", type);
		return SLURM_ERROR;
	}

	switch (type) {
	case PURGE_TXN:
		query = xstrdup_printf("select %s from \"%s\" where "
				       "timestamp <= %ld && cluster='%s' "
				       "order by timestamp asc LIMIT %d",
				       cols, sql_table,
				       period_end, cluster_name,
				       MAX_PURGE_LIMIT);
		break;
	case PURGE_USAGE:
	case PURGE_CLUSTER_USAGE:
		query = xstrdup_printf("select %s from \"%s_%s\" where "
				       "time_start <= %ld "
				       "order by time_start asc LIMIT %d",
				       cols, cluster_name, sql_table,
				       period_end, MAX_PURGE_LIMIT);
		break;
	case PURGE_JOB:
		query = xstrdup_printf("select %s from \"%s_%s\" where "
				       "time_submit <= %ld && time_end != 0 "
				       "order by time_submit asc LIMIT %d",
				       cols, cluster_name, job_table,
				       period_end, MAX_PURGE_LIMIT);
		break;
	default:
		query = xstrdup_printf("select %s from \"%s_%s\" where "
				       "time_start <= %ld && time_end != 0 "
				       "order by time_start asc LIMIT %d",
				       cols, cluster_name, sql_table,
				       period_end, MAX_PURGE_LIMIT);
		break;
	}

	xfree(cols);

	DB_DEBUG(DB_ARCHIVE, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = (*pack_func)(result, cluster_name, cnt, usage_info,
			      &period_start);
	mysql_free_result(result);

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, sql_table,
					archive_period);
	free_buf(buffer);

	if (error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

uint32_t _get_begin_next_month(time_t start)
{
	struct tm parts;

	localtime_r(&start, &parts);

	parts.tm_mon++;
	parts.tm_mday  = 1;
	parts.tm_hour  = 0;
	parts.tm_min   = 0;
	parts.tm_sec   = 0;

	if (parts.tm_mon > 11) {
		parts.tm_year++;
		parts.tm_mon = 0;
	}

	return slurm_mktime(&parts);
}

/* Get the oldest purge'able record.
 * Returns SLURM_ERROR for mysql error, 0 no purge'able records found,
 * 1 found purgeable record.
 */
static int _get_oldest_record(mysql_conn_t *mysql_conn, char *cluster,
			      char *table, purge_type_t type, char *col_name,
			      time_t period_end, time_t *record_start)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;

	if (record_start == NULL)
		return SLURM_ERROR;

	/* get oldest record */
	switch (type) {
	case PURGE_TXN:
		query = xstrdup_printf(
			"select %s from \"%s\" where %s <= %ld "
			"&& cluster='%s' order by %s asc LIMIT 1",
			col_name, table, col_name, period_end, cluster,
			col_name);
		break;
	case PURGE_USAGE:
	case PURGE_CLUSTER_USAGE:
		query = xstrdup_printf(
			"select %s from \"%s_%s\" where %s <= %ld "
			"order by %s asc LIMIT 1",
			col_name, cluster, table, col_name, period_end,
			col_name);
		break;
	default:
		query = xstrdup_printf(
			"select %s from \"%s_%s\" where %s <= %ld "
			"&& time_end != 0 order by %s asc LIMIT 1",
			col_name, cluster, table, col_name, period_end,
			col_name);
		break;
	}

	DB_DEBUG(DB_ARCHIVE, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}
	row = mysql_fetch_row(result);
	*record_start = slurm_atoul(row[0]);
	mysql_free_result(result);

	return 1; /* found one record */
}

/* Archive and purge a table.
 *
 * Returns SLURM_ERROR on error and SLURM_SUCCESS on success.
 */
static int _archive_purge_table(purge_type_t purge_type, uint32_t usage_info,
				mysql_conn_t *mysql_conn, char *cluster_name,
				slurmdb_archive_cond_t *arch_cond)
{
	int      rc          = SLURM_SUCCESS;
	uint32_t purge_attr  = 0;
	uint16_t type, period;
	time_t   last_submit = time(NULL);
	time_t   curr_end    = 0, tmp_end = 0, record_start = 0;
	char    *query = NULL, *sql_table = NULL,
		*col_name = NULL;
	uint32_t tmp_archive_period;

	switch (purge_type) {
	case PURGE_EVENT:
		purge_attr = arch_cond->purge_event;
		sql_table  = event_table;
		col_name   = event_req_inx[EVENT_REQ_START];
		break;
	case PURGE_SUSPEND:
		purge_attr = arch_cond->purge_suspend;
		sql_table  = suspend_table;
		col_name   = suspend_req_inx[SUSPEND_REQ_START];
		break;
	case PURGE_RESV:
		purge_attr = arch_cond->purge_resv;
		sql_table  = resv_table;
		col_name   = step_req_inx[STEP_REQ_START];
		break;
	case PURGE_JOB:
		purge_attr = arch_cond->purge_job;
		sql_table  = job_table;
		col_name   = job_req_inx[JOB_REQ_SUBMIT];
		break;
	case PURGE_STEP:
		purge_attr = arch_cond->purge_step;
		sql_table  = step_table;
		col_name   = step_req_inx[STEP_REQ_START];
		break;
	case PURGE_TXN:
		purge_attr = arch_cond->purge_txn;
		sql_table  = txn_table;
		col_name   = txn_req_inx[TXN_REQ_TS];
		break;
	case PURGE_USAGE:
		type = usage_info & 0x0000ffff;
		period = usage_info >> 16;

		switch (type) {
		case DBD_GOT_ASSOC_USAGE:
			switch (period) {
			case DBD_ROLLUP_HOUR:
				sql_table = assoc_hour_table;
				break;
			case DBD_ROLLUP_DAY:
				sql_table = assoc_day_table;
				break;
			case DBD_ROLLUP_MONTH:
				sql_table = assoc_month_table;
				break;
			default:
				error("Unknown period");
				return SLURM_ERROR;
				break;
			}
			break;
		case DBD_GOT_WCKEY_USAGE:
			switch (period) {
			case DBD_ROLLUP_HOUR:
				sql_table = wckey_hour_table;
				break;
			case DBD_ROLLUP_DAY:
				sql_table = wckey_day_table;
				break;
			case DBD_ROLLUP_MONTH:
				sql_table = wckey_month_table;
				break;
			default:
				error("Unknown period");
				return SLURM_ERROR;
				break;
			}
			break;
		default:
			error("Unknown usage type %d", type);
			return SLURM_ERROR;
			break;
		}

		purge_attr = arch_cond->purge_usage;
		col_name   = usage_req_inx[USAGE_START];
		break;
	case PURGE_CLUSTER_USAGE:
		period = usage_info >> 16;

		switch (period) {
		case DBD_ROLLUP_HOUR:
			sql_table = cluster_hour_table;
			break;
		case DBD_ROLLUP_DAY:
			sql_table = cluster_day_table;
			break;
		case DBD_ROLLUP_MONTH:
			sql_table = cluster_month_table;
			break;
		default:
			error("Unknown period");
			return SLURM_ERROR;
			break;
		}

		purge_attr = arch_cond->purge_usage;
		col_name   = cluster_req_inx[CLUSTER_START];
		break;
	default:
		fatal("Unknown purge type: %d", purge_type);
		return SLURM_ERROR;
	}

	if (!(curr_end = archive_setup_end_time(last_submit, purge_attr))) {
		error("Parsing purge %s_%s", cluster_name, sql_table);
		return SLURM_ERROR;
	}

	/* continue archive/purge until no records in the period are found */
	while (1) {
		rc = _get_oldest_record(mysql_conn, cluster_name, sql_table,
					purge_type, col_name,
					curr_end, &record_start);
		if (!rc) /* no purgeable records found - base case */
			break;
		else if (rc == SLURM_ERROR)
			return rc;

		tmp_archive_period = purge_attr;

		if (curr_end - record_start > MAX_ARCHIVE_AGE) {
			time_t begin_next = _get_begin_next_month(record_start);
			/* old stuff, catch up by archiving by month */
			tmp_archive_period = SLURMDB_PURGE_MONTHS;
			tmp_end = MIN(curr_end, begin_next);
		} else
			tmp_end = curr_end;

		log_flag(DB_ARCHIVE, "Purging %s_%s before %ld",
			 cluster_name, sql_table, tmp_end);

		/* Do archive */
		if (SLURMDB_PURGE_ARCHIVE_SET(purge_attr)) {
			rc = _archive_table(purge_type, mysql_conn,
					    cluster_name, tmp_end,
					    arch_cond->archive_dir,
					    tmp_archive_period,
					    sql_table, usage_info);
			if (!rc) { /* no records archived */
				error("%s: No records archived for %s before %ld but we found some records",
				      __func__, sql_table, tmp_end);
				return SLURM_ERROR;
			} else if (rc == SLURM_ERROR)
				return rc;
		}

		/*
		 * The purge query should have the same where clause as the
		 * archive query. The order by is very important so we get
		 * records in the same order as we do when archiving, since we
		 * only want to delete records that have been archived (if
		 * archiving is enabled).
		 */
		switch (purge_type) {
		case PURGE_TXN:
			query = xstrdup_printf(
				"delete from \"%s\" where "
				"%s <= %ld && cluster='%s' order by %s asc LIMIT %d",
				sql_table, col_name, tmp_end, cluster_name,
				col_name, MAX_PURGE_LIMIT);
			break;
		case PURGE_USAGE:
		case PURGE_CLUSTER_USAGE:
			query = xstrdup_printf(
				"delete from \"%s_%s\" where "
				"%s <= %ld order by %s asc LIMIT %d",
				cluster_name, sql_table, col_name,
				tmp_end, col_name, MAX_PURGE_LIMIT);
			break;
		default:
			query = xstrdup_printf(
				"delete from \"%s_%s\" where "
				"%s <= %ld && time_end != 0 order by %s asc LIMIT %d",
				cluster_name, sql_table, col_name,
				tmp_end, col_name, MAX_PURGE_LIMIT);
			break;
		}
		DB_DEBUG(DB_ARCHIVE, mysql_conn->conn, "query\n%s", query);

		/*
		 * Don't loop this query, just do it once, since we are only
		 * archiving and purging MAX_PURGE_LIMIT rows at a time.
		 * mysql_db_delete_affected_rows will return < 0 on failure or
		 * 0 if no records are affected.
		 */
		if ((rc = mysql_db_delete_affected_rows(
				mysql_conn, query)) > 0) {
			/* Commit here every time since this could create a huge
			 * transaction.
			 */
			if ((rc = mysql_db_commit(mysql_conn)))
				error("Couldn't commit cluster (%s) purge",
				      cluster_name);
		}

		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't remove old data from %s table",
			      sql_table);
			return SLURM_ERROR;
		} else if (mysql_db_commit(mysql_conn)) {
			error("Couldn't commit cluster (%s) purge",
			      cluster_name);
			break;
		}
	}

	return SLURM_SUCCESS;
}

static int _execute_archive(mysql_conn_t *mysql_conn,
			    char *cluster_name,
			    slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	time_t last_submit = time(NULL);

	if (arch_cond->archive_script)
		return archive_run_script(arch_cond, cluster_name, last_submit);
	else if (!arch_cond->archive_dir) {
		error("No archive dir given, can't process");
		return SLURM_ERROR;
	}

	if (arch_cond->purge_event != NO_VAL) {
		if ((rc = _archive_purge_table(PURGE_EVENT, 0, mysql_conn,
					       cluster_name, arch_cond)))
			return rc;
	}

	if (arch_cond->purge_suspend != NO_VAL) {
		if ((rc = _archive_purge_table(PURGE_SUSPEND, 0, mysql_conn,
					       cluster_name, arch_cond)))
			return rc;
	}

	if (arch_cond->purge_step != NO_VAL) {
		if ((rc = _archive_purge_table(PURGE_STEP, 0, mysql_conn,
					       cluster_name, arch_cond)))
			return rc;
	}

	if (arch_cond->purge_job != NO_VAL) {
		if ((rc = _archive_purge_table(PURGE_JOB, 0, mysql_conn,
					       cluster_name, arch_cond)))
			return rc;
	}

	if (arch_cond->purge_resv != NO_VAL) {
		if ((rc = _archive_purge_table(PURGE_RESV, 0, mysql_conn,
					       cluster_name, arch_cond)))
			return rc;
	}

	if (arch_cond->purge_txn != NO_VAL) {
		if ((rc = _archive_purge_table(PURGE_TXN, 0, mysql_conn,
					       cluster_name, arch_cond)))
			return rc;
	}

	if (arch_cond->purge_usage != NO_VAL) {
		int i;
		for (i = 0; i < DBD_ROLLUP_COUNT; i++) {
			uint32_t usage_info = i << 16;
			if ((rc = _archive_purge_table(
				     PURGE_USAGE,
				     usage_info + DBD_GOT_ASSOC_USAGE,
				     mysql_conn, cluster_name, arch_cond)))
			return rc;

			if ((rc = _archive_purge_table(
				     PURGE_USAGE,
				     usage_info + DBD_GOT_WCKEY_USAGE,
				     mysql_conn, cluster_name, arch_cond)))
			return rc;

			if ((rc = _archive_purge_table(
				     PURGE_CLUSTER_USAGE,
				     usage_info + DBD_GOT_CLUSTER_USAGE,
				     mysql_conn, cluster_name, arch_cond)))
			return rc;
		}
	}

	return SLURM_SUCCESS;
}

extern int as_mysql_jobacct_process_archive(mysql_conn_t *mysql_conn,
					    slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *cluster_name = NULL;
	List use_cluster_list;
	bool new_cluster_list = false;
	ListIterator itr = NULL;

	if (!arch_cond) {
		error("No arch_cond was given to archive from.  returning");
		return SLURM_ERROR;
	}

	if (arch_cond->job_cond && arch_cond->job_cond->cluster_list
	    && list_count(arch_cond->job_cond->cluster_list)) {
		use_cluster_list = arch_cond->job_cond->cluster_list;
	} else {
		/* execute_archive may take a long time to run, so
		 * don't keep the as_mysql_cluster_list_lock locked
		 * the whole time, just copy the list and work off
		 * that.
		 */
		new_cluster_list = true;
		use_cluster_list = list_create(xfree_ptr);
		slurm_rwlock_rdlock(&as_mysql_cluster_list_lock);
		itr = list_iterator_create(as_mysql_cluster_list);
		while ((cluster_name = list_next(itr)))
			list_append(use_cluster_list, xstrdup(cluster_name));
		list_iterator_destroy(itr);
		slurm_rwlock_unlock(&as_mysql_cluster_list_lock);
	}

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		if ((rc = _execute_archive(mysql_conn, cluster_name, arch_cond))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);

	if (new_cluster_list)
		FREE_NULL_LIST(use_cluster_list);

	return rc;
}

extern int as_mysql_jobacct_process_archive_load(
	mysql_conn_t *mysql_conn, slurmdb_archive_rec_t *arch_rec)
{
	char *data = NULL, *cluster_name = NULL;
	int error_code = SLURM_SUCCESS;
	buf_t *buffer = NULL;
	time_t buf_time;
	uint16_t type = 0, ver = 0, period = 0;
	uint32_t data_size = 0, rec_cnt = 0, tmp32 = 0;
	uint32_t rec_cnt_total = 0, rec_cnt_left = 0, pass_cnt = 0;

	/* Ensure that the connection is not set in autocommit mode. */
	xassert(mysql_conn->rollback);

	if (!arch_rec) {
		error("We need a slurmdb_archive_rec to load anything.");
		return SLURM_ERROR;
	}

	if (arch_rec->insert) {
		data = xstrdup(arch_rec->insert);
	} else if (arch_rec->archive_file) {
		int data_allocated, data_read = 0;
		int state_fd = open(arch_rec->archive_file, O_RDONLY);
		if (state_fd < 0) {
			info("Could not open archive file `%s`: %m",
			     arch_rec->archive_file);
			error_code = errno;
		} else {
			data_allocated = BUF_SIZE + 1;
			data = xmalloc_nz(data_allocated);
			while (1) {
				data_read = read(state_fd, &data[data_size],
						 BUF_SIZE);
				if (data_read < 0) {
					data[data_size] = '\0';
					if (errno == EINTR)
						continue;
					else {
						error("Read error on %s: %m",
						      arch_rec->archive_file);
						break;
					}
				}
				data[data_size + data_read] = '\0';
				if (data_read == 0)	/* eof */
					break;
				data_size      += data_read;
				data_allocated += data_read;
				xrealloc_nz(data, data_allocated);
			}
			close(state_fd);
		}
		if (error_code != SLURM_SUCCESS) {
			xfree(data);
			return error_code;
		}
	} else {
		error("Nothing was set in your "
		      "slurmdb_archive_rec so I am unable to process.");
		xfree(data);
		return SLURM_ERROR;
	}

	if (!data) {
		error("It doesn't appear we have anything to load.");
		return SLURM_ERROR;
	}

	/*
	 * this is the old version of an archive file where the file
	 * was straight sql.
	 */
	if ((strlen(data) >= 12)
	    && (!xstrncmp("insert into ", data, 12)
		|| !xstrncmp("delete from ", data, 12)
		|| !xstrncmp("drop table ", data, 11)
		|| !xstrncmp("truncate table ", data, 15))) {
		_process_old_sql(&data);
		goto got_sql;
	}

	buffer = create_buf(data, data_size);
	data = NULL;	/* Moved to "buffer" */

	safe_unpack16(&ver, buffer);
	DB_DEBUG(DB_ARCHIVE, mysql_conn->conn,
	         "Version in archive header is %u", ver);
	/*
	 * Don't verify the lower limit as we should be keeping all
	 * older versions around here just to support super old
	 * archive files since they don't get regenerated all the time.
	 */
	if (ver > SLURM_PROTOCOL_VERSION) {
		error("***********************************************");
		error("Can not recover archive file, incompatible version, "
		      "got %u need <= %u", ver,
		      SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		FREE_NULL_BUFFER(buffer);
		return EFAULT;
	}
	safe_unpack_time(&buf_time, buffer);
	safe_unpack16(&type, buffer);
	safe_unpackstr_xmalloc(&cluster_name, &tmp32, buffer);
	safe_unpack32(&rec_cnt, buffer);

	if (!rec_cnt) {
		error("we didn't get any records from this file of type '%s'",
		      slurmdbd_msg_type_2_str(type, 0));
		goto got_sql;
	}

	rec_cnt_left = rec_cnt;
	rec_cnt_total = rec_cnt;
pass:
	rec_cnt = MIN(rec_cnt_left, RECORDS_PER_PASS);

	DB_DEBUG(DB_ARCHIVE, mysql_conn->conn,
	         "%s: Pass %u: loaded %u/%u records. Attempting partial load %u.",
	         __func__, pass_cnt, rec_cnt_total - rec_cnt_left,
	         rec_cnt_total, rec_cnt);

	rec_cnt_left -= rec_cnt;

	switch (type) {
	case DBD_GOT_EVENTS:
		data = _load_events(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_GOT_JOBS:
		data = _load_jobs(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_GOT_RESVS:
		data = _load_resvs(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_STEP_START:
		data = _load_steps(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_JOB_SUSPEND:
		data = _load_suspend(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_GOT_TXN:
		data = _load_txn(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_GOT_ASSOC_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		if (pass_cnt == 0)
			safe_unpack16(&period, buffer);
		data = _load_usage(ver, buffer, cluster_name, type, period,
				   rec_cnt);
		break;
	case DBD_GOT_CLUSTER_USAGE:
		if (pass_cnt == 0)
			safe_unpack16(&period, buffer);
		data = _load_cluster_usage(ver, buffer, cluster_name, period,
					   rec_cnt);
		break;
	default:
		error("Unknown type '%u' to load from archive", type);
		break;
	}

got_sql:
	if (!data) {
		error("No data to load");
		error_code = SLURM_ERROR;
		goto cleanup;
	}
	if (slurm_conf.debug_flags & DEBUG_FLAG_DB_ARCHIVE)
		DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", data);
	error_code = mysql_db_query_check_after(mysql_conn, data);
	xfree(data);
	if (error_code != SLURM_SUCCESS) {
unpack_error:
		error("Couldn't load old data");
		if (error_code == SLURM_SUCCESS) {
			/* This happens on unpack_error */
			error_code = SLURM_ERROR;
		}
		goto cleanup;
	}

	if (rec_cnt_left) {
		pass_cnt++;
		goto pass;
	}

cleanup:
	xfree(cluster_name);
	FREE_NULL_BUFFER(buffer);

	if (error_code)
		error("%s: failure loading archive: %s", __func__,
		      slurm_strerror(error_code));
	else
		DB_DEBUG(DB_ARCHIVE, mysql_conn->conn,
		         "%s: archive loaded successfully.", __func__);

	return error_code;
}
