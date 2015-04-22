/*****************************************************************************\
 *  as_mysql_archive.c - functions dealing with the archiving.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "as_mysql_archive.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/env.h"

#define SLURMDBD_2_5_VERSION   11	/* slurm version 2.5 */

#define MAX_PURGE_LIMIT 50000 /* Number of records that are purged at a time
				 so that locks can be periodically released. */

typedef struct {
	char *cluster_nodes;
	char *cpu_count;
	char *node_name;
	char *period_end;
	char *period_start;
	char *reason;
	char *reason_uid;
	char *state;
} local_event_t;

typedef struct {
	char *account;
	char *alloc_cpus;
	char *alloc_nodes;
	char *associd;
	char *array_jobid;
	char *array_max_tasks;
	char *array_taskid;
	char *blockid;
	char *derived_ec;
	char *derived_es;
	char *exit_code;
	char *eligible;
	char *end;
	char *gid;
	char *id;
	char *jobid;
	char *kill_requid;
	char *name;
	char *nodelist;
	char *node_inx;
	char *partition;
	char *priority;
	char *qos;
	char *req_cpus;
	char *req_mem;
	char *resvid;
	char *start;
	char *state;
	char *submit;
	char *suspended;
	char *timelimit;
	char *track_steps;
	char *uid;
	char *wckey;
	char *wckey_id;
} local_job_t;

typedef struct {
	char *assocs;
	char *cpus;
	char *flags;
	char *id;
	char *name;
	char *nodes;
	char *node_inx;
	char *time_end;
	char *time_start;
} local_resv_t;

typedef struct {
	char *act_cpufreq;
	char *ave_cpu;
	char *ave_disk_read;
	char *ave_disk_write;
	char *ave_pages;
	char *ave_rss;
	char *ave_vsize;
	char *exit_code;
	char *consumed_energy;
	char *cpus;
	char *id;
	char *kill_requid;
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
	char *name;
	char *nodelist;
	char *nodes;
	char *node_inx;
	char *period_end;
	char *period_start;
	char *period_suspended;
	char *req_cpufreq;
	char *state;
	char *stepid;
	char *sys_sec;
	char *sys_usec;
	char *tasks;
	char *task_dist;
	char *user_sec;
	char *user_usec;
} local_step_t;

typedef struct {
	char *associd;
	char *id;
	char *period_end;
	char *period_start;
} local_suspend_t;

/* if this changes you will need to edit the corresponding
 * enum below */
char *event_req_inx[] = {
	"time_start",
	"time_end",
	"node_name",
	"cluster_nodes",
	"cpu_count",
	"reason",
	"reason_uid",
	"state",
};

enum {
	EVENT_REQ_START,
	EVENT_REQ_END,
	EVENT_REQ_NODE,
	EVENT_REQ_CNODES,
	EVENT_REQ_CPU,
	EVENT_REQ_REASON,
	EVENT_REQ_REASON_UID,
	EVENT_REQ_STATE,
	EVENT_REQ_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below */
static char *job_req_inx[] = {
	"account",
	"array_max_tasks",
	"cpus_alloc",
	"nodes_alloc",
	"id_assoc",
	"id_array_job",
	"id_array_task",
	"id_block",
	"derived_ec",
	"derived_es",
	"exit_code",
	"timelimit",
	"time_eligible",
	"time_end",
	"id_group",
	"job_db_inx",
	"id_job",
	"kill_requid",
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
	"time_submit",
	"time_suspended",
	"track_steps",
	"id_user",
	"wckey",
	"id_wckey"
};

enum {
	JOB_REQ_ACCOUNT,
	JOB_REQ_ARRAY_MAX,
	JOB_REQ_ALLOC_CPUS,
	JOB_REQ_ALLOC_NODES,
	JOB_REQ_ASSOCID,
	JOB_REQ_ARRAYJOBID,
	JOB_REQ_ARRAYTASKID,
	JOB_REQ_BLOCKID,
	JOB_REQ_DERIVED_EC,
	JOB_REQ_DERIVED_ES,
	JOB_REQ_EXIT_CODE,
	JOB_REQ_TIMELIMIT,
	JOB_REQ_ELIGIBLE,
	JOB_REQ_END,
	JOB_REQ_GID,
	JOB_REQ_ID,
	JOB_REQ_JOBID,
	JOB_REQ_KILL_REQUID,
	JOB_REQ_NAME,
	JOB_REQ_NODELIST,
	JOB_REQ_NODE_INX,
	JOB_REQ_RESVID,
	JOB_REQ_PARTITION,
	JOB_REQ_PRIORITY,
	JOB_REQ_QOS,
	JOB_REQ_REQ_CPUS,
	JOB_REQ_REQ_MEM,
	JOB_REQ_START,
	JOB_REQ_STATE,
	JOB_REQ_SUBMIT,
	JOB_REQ_SUSPENDED,
	JOB_REQ_TRACKSTEPS,
	JOB_REQ_UID,
	JOB_REQ_WCKEY,
	JOB_REQ_WCKEYID,
	JOB_REQ_COUNT
};

/* if this changes you will need to edit the corresponding enum */
char *resv_req_inx[] = {
	"id_resv",
	"assoclist",
	"cpus",
	"flags",
	"nodelist",
	"node_inx",
	"resv_name",
	"time_start",
	"time_end",
};

enum {
	RESV_REQ_ID,
	RESV_REQ_ASSOCS,
	RESV_REQ_CPUS,
	RESV_REQ_FLAGS,
	RESV_REQ_NODES,
	RESV_REQ_NODE_INX,
	RESV_REQ_NAME,
	RESV_REQ_START,
	RESV_REQ_END,
	RESV_REQ_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below */
static char *step_req_inx[] = {
	"job_db_inx",
	"id_step",
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
	"cpus_alloc",
	"task_cnt",
	"task_dist",
	"user_sec",
	"user_usec",
	"sys_sec",
	"sys_usec",
	"max_vsize",
	"max_vsize_task",
	"max_vsize_node",
	"ave_vsize",
	"max_rss",
	"max_rss_task",
	"max_rss_node",
	"ave_rss",
	"max_pages",
	"max_pages_task",
	"max_pages_node",
	"ave_pages",
	"min_cpu",
	"min_cpu_task",
	"min_cpu_node",
	"ave_cpu",
	"act_cpufreq",
	"consumed_energy",
	"req_cpufreq",
	"max_disk_read",
	"max_disk_read_task",
	"max_disk_read_node",
	"ave_disk_read",
	"max_disk_write",
	"max_disk_write_task",
	"max_disk_write_node",
	"ave_disk_write"
};


enum {
	STEP_REQ_ID,
	STEP_REQ_STEPID,
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
	STEP_REQ_CPUS,
	STEP_REQ_TASKS,
	STEP_REQ_TASKDIST,
	STEP_REQ_USER_SEC,
	STEP_REQ_USER_USEC,
	STEP_REQ_SYS_SEC,
	STEP_REQ_SYS_USEC,
	STEP_REQ_MAX_VSIZE,
	STEP_REQ_MAX_VSIZE_TASK,
	STEP_REQ_MAX_VSIZE_NODE,
	STEP_REQ_AVE_VSIZE,
	STEP_REQ_MAX_RSS,
	STEP_REQ_MAX_RSS_TASK,
	STEP_REQ_MAX_RSS_NODE,
	STEP_REQ_AVE_RSS,
	STEP_REQ_MAX_PAGES,
	STEP_REQ_MAX_PAGES_TASK,
	STEP_REQ_MAX_PAGES_NODE,
	STEP_REQ_AVE_PAGES,
	STEP_REQ_MIN_CPU,
	STEP_REQ_MIN_CPU_TASK,
	STEP_REQ_MIN_CPU_NODE,
	STEP_REQ_AVE_CPU,
	STEP_REQ_ACT_CPUFREQ,
	STEP_REQ_CONSUMED_ENERGY,
	STEP_REQ_REQ_CPUFREQ,
	STEP_REQ_MAX_DISK_READ,
	STEP_REQ_MAX_DISK_READ_TASK,
	STEP_REQ_MAX_DISK_READ_NODE,
	STEP_REQ_AVE_DISK_READ,
	STEP_REQ_MAX_DISK_WRITE,
	STEP_REQ_MAX_DISK_WRITE_TASK,
	STEP_REQ_MAX_DISK_WRITE_NODE,
	STEP_REQ_AVE_DISK_WRITE,
	STEP_REQ_COUNT,
};

/* if this changes you will need to edit the corresponding
 * enum below */
static char *suspend_req_inx[] = {
	"job_db_inx",
	"id_assoc",
	"time_start",
	"time_end",
};

enum {
	SUSPEND_REQ_ID,
	SUSPEND_REQ_ASSOCID,
	SUSPEND_REQ_START,
	SUSPEND_REQ_END,
	SUSPEND_REQ_COUNT
};

static int high_buffer_size = (1024 * 1024);

static void _pack_local_event(local_event_t *object,
			      uint16_t rpc_version, Buf buffer)
{
	packstr(object->cluster_nodes, buffer);
	packstr(object->cpu_count, buffer);
	packstr(object->node_name, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
	packstr(object->reason, buffer);
	packstr(object->reason_uid, buffer);
	packstr(object->state, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_event(local_event_t *object,
			       uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->cluster_nodes, &tmp32, buffer);
	unpackstr_ptr(&object->cpu_count, &tmp32, buffer);
	unpackstr_ptr(&object->node_name, &tmp32, buffer);
	unpackstr_ptr(&object->period_end, &tmp32, buffer);
	unpackstr_ptr(&object->period_start, &tmp32, buffer);
	unpackstr_ptr(&object->reason, &tmp32, buffer);
	unpackstr_ptr(&object->reason_uid, &tmp32, buffer);
	unpackstr_ptr(&object->state, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static void _pack_local_job(local_job_t *object,
			    uint16_t rpc_version, Buf buffer)
{
	packstr(object->account, buffer);
	packstr(object->alloc_cpus, buffer);
	packstr(object->alloc_nodes, buffer);
	packstr(object->associd, buffer);
	packstr(object->array_jobid, buffer);
	packstr(object->array_max_tasks, buffer);
	packstr(object->array_taskid, buffer);
	packstr(object->blockid, buffer);
	packstr(object->derived_ec, buffer);
	packstr(object->derived_es, buffer);
	packstr(object->exit_code, buffer);
	packstr(object->timelimit, buffer);
	packstr(object->eligible, buffer);
	packstr(object->end, buffer);
	packstr(object->gid, buffer);
	packstr(object->id, buffer);
	packstr(object->jobid, buffer);
	packstr(object->kill_requid, buffer);
	packstr(object->name, buffer);
	packstr(object->nodelist, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->partition, buffer); /* priority */
	packstr(object->priority, buffer);  /* qos */
	packstr(object->qos, buffer);       /* req_cpus */
	packstr(object->req_cpus, buffer);  /* req_mem */
	packstr(object->req_mem, buffer);   /* resvid */
	packstr(object->resvid, buffer);    /* partition */
	packstr(object->start, buffer);
	packstr(object->state, buffer);
	packstr(object->submit, buffer);
	packstr(object->suspended, buffer);
	packstr(object->track_steps, buffer);
	packstr(object->uid, buffer);
	packstr(object->wckey, buffer);
	packstr(object->wckey_id, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_job(local_job_t *object,
			     uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

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
	 * job->start instead of after job->node_inx. */

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		unpackstr_ptr(&object->account, &tmp32, buffer);
		unpackstr_ptr(&object->alloc_cpus, &tmp32, buffer);
		unpackstr_ptr(&object->alloc_nodes, &tmp32, buffer);
		unpackstr_ptr(&object->associd, &tmp32, buffer);
		unpackstr_ptr(&object->array_jobid, &tmp32, buffer);
		unpackstr_ptr(&object->array_max_tasks, &tmp32, buffer);
		unpackstr_ptr(&object->array_taskid, &tmp32, buffer);
		unpackstr_ptr(&object->blockid, &tmp32, buffer);
		unpackstr_ptr(&object->derived_ec, &tmp32, buffer);
		unpackstr_ptr(&object->derived_es, &tmp32, buffer);
		unpackstr_ptr(&object->exit_code, &tmp32, buffer);
		unpackstr_ptr(&object->timelimit, &tmp32, buffer);
		unpackstr_ptr(&object->eligible, &tmp32, buffer);
		unpackstr_ptr(&object->end, &tmp32, buffer);
		unpackstr_ptr(&object->gid, &tmp32, buffer);
		unpackstr_ptr(&object->id, &tmp32, buffer);
		unpackstr_ptr(&object->jobid, &tmp32, buffer);
		unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
		unpackstr_ptr(&object->name, &tmp32, buffer);
		unpackstr_ptr(&object->nodelist, &tmp32, buffer);
		unpackstr_ptr(&object->node_inx, &tmp32, buffer);
		unpackstr_ptr(&object->priority, &tmp32, buffer);
		unpackstr_ptr(&object->qos, &tmp32, buffer);
		unpackstr_ptr(&object->req_cpus, &tmp32, buffer);
		unpackstr_ptr(&object->req_mem, &tmp32, buffer);
		unpackstr_ptr(&object->resvid, &tmp32, buffer);
		unpackstr_ptr(&object->partition, &tmp32, buffer);
		unpackstr_ptr(&object->start, &tmp32, buffer);
		unpackstr_ptr(&object->state, &tmp32, buffer);
		unpackstr_ptr(&object->submit, &tmp32, buffer);
		unpackstr_ptr(&object->suspended, &tmp32, buffer);
		unpackstr_ptr(&object->track_steps, &tmp32, buffer);
		unpackstr_ptr(&object->uid, &tmp32, buffer);
		unpackstr_ptr(&object->wckey, &tmp32, buffer);
		unpackstr_ptr(&object->wckey_id, &tmp32, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		unpackstr_ptr(&object->account, &tmp32, buffer);
		unpackstr_ptr(&object->alloc_cpus, &tmp32, buffer);
		unpackstr_ptr(&object->alloc_nodes, &tmp32, buffer);
		unpackstr_ptr(&object->associd, &tmp32, buffer);
		unpackstr_ptr(&object->blockid, &tmp32, buffer);
		unpackstr_ptr(&object->derived_ec, &tmp32, buffer);
		unpackstr_ptr(&object->derived_es, &tmp32, buffer);
		unpackstr_ptr(&object->exit_code, &tmp32, buffer);
		unpackstr_ptr(&object->timelimit, &tmp32, buffer);
		unpackstr_ptr(&object->eligible, &tmp32, buffer);
		unpackstr_ptr(&object->end, &tmp32, buffer);
		unpackstr_ptr(&object->gid, &tmp32, buffer);
		unpackstr_ptr(&object->id, &tmp32, buffer);
		unpackstr_ptr(&object->jobid, &tmp32, buffer);
		unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
		unpackstr_ptr(&object->name, &tmp32, buffer);
		unpackstr_ptr(&object->nodelist, &tmp32, buffer);
		unpackstr_ptr(&object->node_inx, &tmp32, buffer);
		unpackstr_ptr(&object->priority, &tmp32, buffer);
		unpackstr_ptr(&object->qos, &tmp32, buffer);
		unpackstr_ptr(&object->req_cpus, &tmp32, buffer);
		unpackstr_ptr(&object->req_mem, &tmp32, buffer);
		unpackstr_ptr(&object->resvid, &tmp32, buffer);
		unpackstr_ptr(&object->partition, &tmp32, buffer);
		unpackstr_ptr(&object->start, &tmp32, buffer);
		unpackstr_ptr(&object->state, &tmp32, buffer);
		unpackstr_ptr(&object->submit, &tmp32, buffer);
		unpackstr_ptr(&object->suspended, &tmp32, buffer);
		unpackstr_ptr(&object->track_steps, &tmp32, buffer);
		unpackstr_ptr(&object->uid, &tmp32, buffer);
		unpackstr_ptr(&object->wckey, &tmp32, buffer);
		unpackstr_ptr(&object->wckey_id, &tmp32, buffer);
	} else {
		unpackstr_ptr(&object->account, &tmp32, buffer);
		unpackstr_ptr(&object->alloc_cpus, &tmp32, buffer);
		unpackstr_ptr(&object->alloc_nodes, &tmp32, buffer);
		unpackstr_ptr(&object->associd, &tmp32, buffer);
		unpackstr_ptr(&object->blockid, &tmp32, buffer);
		unpackstr_ptr(&object->derived_ec, &tmp32, buffer);
		unpackstr_ptr(&object->derived_es, &tmp32, buffer);
		unpackstr_ptr(&object->exit_code, &tmp32, buffer);
		unpackstr_ptr(&object->timelimit, &tmp32, buffer);
		unpackstr_ptr(&object->eligible, &tmp32, buffer);
		unpackstr_ptr(&object->end, &tmp32, buffer);
		unpackstr_ptr(&object->gid, &tmp32, buffer);
		unpackstr_ptr(&object->id, &tmp32, buffer);
		unpackstr_ptr(&object->jobid, &tmp32, buffer);
		unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
		unpackstr_ptr(&object->name, &tmp32, buffer);
		unpackstr_ptr(&object->nodelist, &tmp32, buffer);
		unpackstr_ptr(&object->node_inx, &tmp32, buffer);
		unpackstr_ptr(&object->priority, &tmp32, buffer);
		unpackstr_ptr(&object->qos, &tmp32, buffer);
		unpackstr_ptr(&object->req_cpus, &tmp32, buffer);
		unpackstr_ptr(&object->resvid, &tmp32, buffer);
		unpackstr_ptr(&object->partition, &tmp32, buffer);
		unpackstr_ptr(&object->start, &tmp32, buffer);
		unpackstr_ptr(&object->state, &tmp32, buffer);
		unpackstr_ptr(&object->submit, &tmp32, buffer);
		unpackstr_ptr(&object->suspended, &tmp32, buffer);
		unpackstr_ptr(&object->track_steps, &tmp32, buffer);
		unpackstr_ptr(&object->uid, &tmp32, buffer);
		unpackstr_ptr(&object->wckey, &tmp32, buffer);
		unpackstr_ptr(&object->wckey_id, &tmp32, buffer);
	}
	return SLURM_SUCCESS;
}

static void _pack_local_resv(local_resv_t *object,
			     uint16_t rpc_version, Buf buffer)
{
	packstr(object->assocs, buffer);
	packstr(object->cpus, buffer);
	packstr(object->flags, buffer);
	packstr(object->id, buffer);
	packstr(object->name, buffer);
	packstr(object->nodes, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->time_end, buffer);
	packstr(object->time_start, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_resv(local_resv_t *object,
			      uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->assocs, &tmp32, buffer);
	unpackstr_ptr(&object->cpus, &tmp32, buffer);
	unpackstr_ptr(&object->flags, &tmp32, buffer);
	unpackstr_ptr(&object->id, &tmp32, buffer);
	unpackstr_ptr(&object->name, &tmp32, buffer);
	unpackstr_ptr(&object->nodes, &tmp32, buffer);
	unpackstr_ptr(&object->node_inx, &tmp32, buffer);
	unpackstr_ptr(&object->time_end, &tmp32, buffer);
	unpackstr_ptr(&object->time_start, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static void _pack_local_step(local_step_t *object,
			     uint16_t rpc_version, Buf buffer)
{
	if (rpc_version >= SLURMDBD_2_6_VERSION) {
		packstr(object->act_cpufreq, buffer);
		packstr(object->ave_cpu, buffer);
		packstr(object->ave_disk_read, buffer);
		packstr(object->ave_disk_write, buffer);
		packstr(object->ave_pages, buffer);
		packstr(object->ave_rss, buffer);
		packstr(object->ave_vsize, buffer);
		packstr(object->exit_code, buffer);
		packstr(object->consumed_energy, buffer);
		packstr(object->cpus, buffer);
		packstr(object->id, buffer);
		packstr(object->kill_requid, buffer);
		packstr(object->max_disk_read, buffer);
		packstr(object->max_disk_read_node, buffer);
		packstr(object->max_disk_read_task, buffer);
		packstr(object->max_disk_write, buffer);
		packstr(object->max_disk_write_node, buffer);
		packstr(object->max_disk_write_task, buffer);
		packstr(object->max_pages, buffer);
		packstr(object->max_pages_node, buffer);
		packstr(object->max_pages_task, buffer);
		packstr(object->max_rss, buffer);
		packstr(object->max_rss_node, buffer);
		packstr(object->max_rss_task, buffer);
		packstr(object->max_vsize, buffer);
		packstr(object->max_vsize_node, buffer);
		packstr(object->max_vsize_task, buffer);
		packstr(object->min_cpu, buffer);
		packstr(object->min_cpu_node, buffer);
		packstr(object->min_cpu_task, buffer);
		packstr(object->name, buffer);
		packstr(object->nodelist, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		packstr(object->period_end, buffer);
		packstr(object->period_start, buffer);
		packstr(object->period_suspended, buffer);
		packstr(object->req_cpufreq, buffer);
		packstr(object->state, buffer);
		packstr(object->stepid, buffer);
		packstr(object->sys_sec, buffer);
		packstr(object->sys_usec, buffer);
		packstr(object->tasks, buffer);
		packstr(object->task_dist, buffer);
		packstr(object->user_sec, buffer);
		packstr(object->user_usec, buffer);
	} else if (rpc_version >= SLURMDBD_2_5_VERSION) {
		packstr(object->act_cpufreq, buffer);
		packstr(object->ave_cpu, buffer);
		packstr(object->ave_pages, buffer);
		packstr(object->ave_rss, buffer);
		packstr(object->ave_vsize, buffer);
		packstr(object->exit_code, buffer);
		packstr(object->consumed_energy, buffer);
		packstr(object->cpus, buffer);
		packstr(object->id, buffer);
		packstr(object->kill_requid, buffer);
		packstr(object->max_pages, buffer);
		packstr(object->max_pages_node, buffer);
		packstr(object->max_pages_task, buffer);
		packstr(object->max_rss, buffer);
		packstr(object->max_rss_node, buffer);
		packstr(object->max_rss_task, buffer);
		packstr(object->max_vsize, buffer);
		packstr(object->max_vsize_node, buffer);
		packstr(object->max_vsize_task, buffer);
		packstr(object->min_cpu, buffer);
		packstr(object->min_cpu_node, buffer);
		packstr(object->min_cpu_task, buffer);
		packstr(object->name, buffer);
		packstr(object->nodelist, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		packstr(object->period_end, buffer);
		packstr(object->period_start, buffer);
		packstr(object->period_suspended, buffer);
		packstr(object->state, buffer);
		packstr(object->stepid, buffer);
		packstr(object->sys_sec, buffer);
		packstr(object->sys_usec, buffer);
		packstr(object->tasks, buffer);
		packstr(object->task_dist, buffer);
		packstr(object->user_sec, buffer);
		packstr(object->user_usec, buffer);
	} else {
		packstr(object->ave_cpu, buffer);
		packstr(object->ave_pages, buffer);
		packstr(object->ave_rss, buffer);
		packstr(object->ave_vsize, buffer);
		packstr(object->exit_code, buffer);
		packstr(object->cpus, buffer);
		packstr(object->id, buffer);
		packstr(object->kill_requid, buffer);
		packstr(object->max_pages, buffer);
		packstr(object->max_pages_node, buffer);
		packstr(object->max_pages_task, buffer);
		packstr(object->max_rss, buffer);
		packstr(object->max_rss_node, buffer);
		packstr(object->max_rss_task, buffer);
		packstr(object->max_vsize, buffer);
		packstr(object->max_vsize_node, buffer);
		packstr(object->max_vsize_task, buffer);
		packstr(object->min_cpu, buffer);
		packstr(object->min_cpu_node, buffer);
		packstr(object->min_cpu_task, buffer);
		packstr(object->name, buffer);
		packstr(object->nodelist, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		packstr(object->period_end, buffer);
		packstr(object->period_start, buffer);
		packstr(object->period_suspended, buffer);
		packstr(object->state, buffer);
		packstr(object->stepid, buffer);
		packstr(object->sys_sec, buffer);
		packstr(object->sys_usec, buffer);
		packstr(object->tasks, buffer);
		packstr(object->task_dist, buffer);
		packstr(object->user_sec, buffer);
		packstr(object->user_usec, buffer);
	}
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_step(local_step_t *object,
			      uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	if (rpc_version >= SLURMDBD_2_6_VERSION) {
		unpackstr_ptr(&object->act_cpufreq, &tmp32, buffer);
		unpackstr_ptr(&object->ave_cpu, &tmp32, buffer);
		unpackstr_ptr(&object->ave_disk_read, &tmp32, buffer);
		unpackstr_ptr(&object->ave_disk_write, &tmp32, buffer);
		unpackstr_ptr(&object->ave_pages, &tmp32, buffer);
		unpackstr_ptr(&object->ave_rss, &tmp32, buffer);
		unpackstr_ptr(&object->ave_vsize, &tmp32, buffer);
		unpackstr_ptr(&object->exit_code, &tmp32, buffer);
		unpackstr_ptr(&object->consumed_energy, &tmp32, buffer);
		unpackstr_ptr(&object->cpus, &tmp32, buffer);
		unpackstr_ptr(&object->id, &tmp32, buffer);
		unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
		unpackstr_ptr(&object->max_disk_read, &tmp32, buffer);
		unpackstr_ptr(&object->max_disk_read_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_disk_read_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_disk_write, &tmp32, buffer);
		unpackstr_ptr(&object->max_disk_write_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_disk_write_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize_task, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu_node, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu_task, &tmp32, buffer);
		unpackstr_ptr(&object->name, &tmp32, buffer);
		unpackstr_ptr(&object->nodelist, &tmp32, buffer);
		unpackstr_ptr(&object->nodes, &tmp32, buffer);
		unpackstr_ptr(&object->node_inx, &tmp32, buffer);
		unpackstr_ptr(&object->period_end, &tmp32, buffer);
		unpackstr_ptr(&object->period_start, &tmp32, buffer);
		unpackstr_ptr(&object->period_suspended, &tmp32, buffer);
		unpackstr_ptr(&object->req_cpufreq, &tmp32, buffer);
		unpackstr_ptr(&object->state, &tmp32, buffer);
		unpackstr_ptr(&object->stepid, &tmp32, buffer);
		unpackstr_ptr(&object->sys_sec, &tmp32, buffer);
		unpackstr_ptr(&object->sys_usec, &tmp32, buffer);
		unpackstr_ptr(&object->tasks, &tmp32, buffer);
		unpackstr_ptr(&object->task_dist, &tmp32, buffer);
		unpackstr_ptr(&object->user_sec, &tmp32, buffer);
		unpackstr_ptr(&object->user_usec, &tmp32, buffer);
	} else if (rpc_version >= SLURMDBD_2_5_VERSION) {
		unpackstr_ptr(&object->act_cpufreq, &tmp32, buffer);
		unpackstr_ptr(&object->ave_cpu, &tmp32, buffer);
		unpackstr_ptr(&object->ave_pages, &tmp32, buffer);
		unpackstr_ptr(&object->ave_rss, &tmp32, buffer);
		unpackstr_ptr(&object->ave_vsize, &tmp32, buffer);
		unpackstr_ptr(&object->exit_code, &tmp32, buffer);
		unpackstr_ptr(&object->consumed_energy, &tmp32, buffer);
		unpackstr_ptr(&object->cpus, &tmp32, buffer);
		unpackstr_ptr(&object->id, &tmp32, buffer);
		unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize_task, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu_node, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu_task, &tmp32, buffer);
		unpackstr_ptr(&object->name, &tmp32, buffer);
		unpackstr_ptr(&object->nodelist, &tmp32, buffer);
		unpackstr_ptr(&object->nodes, &tmp32, buffer);
		unpackstr_ptr(&object->node_inx, &tmp32, buffer);
		unpackstr_ptr(&object->period_end, &tmp32, buffer);
		unpackstr_ptr(&object->period_start, &tmp32, buffer);
		unpackstr_ptr(&object->period_suspended, &tmp32, buffer);
		unpackstr_ptr(&object->state, &tmp32, buffer);
		unpackstr_ptr(&object->stepid, &tmp32, buffer);
		unpackstr_ptr(&object->sys_sec, &tmp32, buffer);
		unpackstr_ptr(&object->sys_usec, &tmp32, buffer);
		unpackstr_ptr(&object->tasks, &tmp32, buffer);
		unpackstr_ptr(&object->task_dist, &tmp32, buffer);
		unpackstr_ptr(&object->user_sec, &tmp32, buffer);
		unpackstr_ptr(&object->user_usec, &tmp32, buffer);
	} else {
		unpackstr_ptr(&object->ave_cpu, &tmp32, buffer);
		unpackstr_ptr(&object->ave_pages, &tmp32, buffer);
		unpackstr_ptr(&object->ave_rss, &tmp32, buffer);
		unpackstr_ptr(&object->ave_vsize, &tmp32, buffer);
		unpackstr_ptr(&object->exit_code, &tmp32, buffer);
		unpackstr_ptr(&object->cpus, &tmp32, buffer);
		unpackstr_ptr(&object->id, &tmp32, buffer);
		unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_pages_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_rss_task, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize_node, &tmp32, buffer);
		unpackstr_ptr(&object->max_vsize_task, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu_node, &tmp32, buffer);
		unpackstr_ptr(&object->min_cpu_task, &tmp32, buffer);
		unpackstr_ptr(&object->name, &tmp32, buffer);
		unpackstr_ptr(&object->nodelist, &tmp32, buffer);
		unpackstr_ptr(&object->nodes, &tmp32, buffer);
		unpackstr_ptr(&object->node_inx, &tmp32, buffer);
		unpackstr_ptr(&object->period_end, &tmp32, buffer);
		unpackstr_ptr(&object->period_start, &tmp32, buffer);
		unpackstr_ptr(&object->period_suspended, &tmp32, buffer);
		unpackstr_ptr(&object->state, &tmp32, buffer);
		unpackstr_ptr(&object->stepid, &tmp32, buffer);
		unpackstr_ptr(&object->sys_sec, &tmp32, buffer);
		unpackstr_ptr(&object->sys_usec, &tmp32, buffer);
		unpackstr_ptr(&object->tasks, &tmp32, buffer);
		unpackstr_ptr(&object->task_dist, &tmp32, buffer);
		unpackstr_ptr(&object->user_sec, &tmp32, buffer);
		unpackstr_ptr(&object->user_usec, &tmp32, buffer);
	}

	return SLURM_SUCCESS;
}

static void _pack_local_suspend(local_suspend_t *object,
				uint16_t rpc_version, Buf buffer)
{
	packstr(object->associd, buffer);
	packstr(object->id, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_suspend(local_suspend_t *object,
				 uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->associd, &tmp32, buffer);
	unpackstr_ptr(&object->id, &tmp32, buffer);
	unpackstr_ptr(&object->period_end, &tmp32, buffer);
	unpackstr_ptr(&object->period_start, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static int _process_old_sql_line(const char *data_in, char **data_full_out)
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
	char *cluster_name = NULL;
	int rc = SLURM_SUCCESS;
	int cnt = 0, cluster_inx = -1, ending_start = 0, ending_end = 0;
	bool delete = 0;
	bool new_cluster = 0;

	while (data_in[i]) {
		if (!strncmp("insert into ", data_in+i, 12)) {
			beginning = xstrndup(data_in+i, 11);
			i+=12;
			break;
		} else if (!strncmp("delete from ", data_in+i, 12)) {
			beginning = xstrndup(data_in+i, 11);
			i+=12;
			delete = 1;
			break;
		} else if (!strncmp("drop table ", data_in+i, 11)) {
			start = i;
			i+=11;
			while (data_in[i] && data_in[i-1] != ';')
				i++;
			xstrncat(data_out, data_in+start, i-start);
			goto end_it;
		} else if (!strncmp("truncate table ", data_in+i, 15)) {
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
	if (!strncmp("cluster_event_table", data_in+i, 19)) {
		i+=19;
		table = event_table;
	} else if (!strncmp("job_table", data_in+i, 9)) {
		i+=9;
		table = job_table;
	} else if (!strncmp("step_table", data_in+i, 10)) {
		i+=10;
		table = step_table;
	} else if (!strncmp("suspend_table", data_in+i, 13)) {
		i+=13;
		table = suspend_table;
	} else if (!strncmp("cluster_day_usage_table", data_in+i, 23)) {
		i+=23;
		table = cluster_day_table;
	} else if (!strncmp("cluster_hour_usage_table", data_in+i, 24)) {
		i+=24;
		table = cluster_hour_table;
	} else if (!strncmp("cluster_month_usage_table", data_in+i, 25)) {
		i+=25;
		table = cluster_month_table;
	} else if (!strncmp("assoc_day_usage_table", data_in+i, 21)) {
		i+=21;
		table = assoc_day_table;
	} else if (!strncmp("assoc_hour_usage_table", data_in+i, 22)) {
		i+=22;
		table = assoc_hour_table;
	} else if (!strncmp("assoc_month_usage_table", data_in+i, 23)) {
		i+=23;
		table = assoc_month_table;
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
		if (delete && !strncmp("where ", data_in+i, 6)) {
			i+=6;
			continue;
		} else if (!strncmp("period_start", data_in+i, 12)) {
			xstrcat(fields, "time_start");
			i+=12;
		} else if (!strncmp("period_end", data_in+i, 10)) {
			xstrcat(fields, "time_end");
			i+=10;
		} else if (!strncmp("jobid", data_in+i, 5)) {
			xstrcat(fields, "id_job");
			i+=5;
		} else if (!strncmp("stepid", data_in+i, 6)) {
			xstrcat(fields, "id_step");
			i+=6;
		} else if (!strncmp("associd", data_in+i, 7)) {
			xstrcat(fields, "id_assoc");
			i+=7;
		} else if (!strncmp("blockid", data_in+i, 7)) {
			xstrcat(fields, "id_block");
			i+=7;
		} else if (!strncmp("wckeyid", data_in+i, 7)) {
			xstrcat(fields, "id_wckey");
			i+=7;
		} else if (!strncmp("qos", data_in+i, 3)) {
			xstrcat(fields, "id_qos");
			i+=3;
		} else if (!strncmp("uid", data_in+i, 3)) {
			xstrcat(fields, "id_user");
			i+=3;
		} else if (!strncmp("gid", data_in+i, 3)) {
			xstrcat(fields, "id_group");
			i+=3;
		} else if (!strncmp("submit", data_in+i, 6)) {
			xstrcat(fields, "time_submit");
			i+=6;
		} else if (!strncmp("eligible", data_in+i, 8)) {
			xstrcat(fields, "time_eligible");
			i+=8;
		} else if (!strncmp("start", data_in+i, 5)) {
			xstrcat(fields, "time_start");
			i+=5;
		} else if (!strncmp("suspended", data_in+i, 9)) {
			xstrcat(fields, "time_suspended");
			i+=9;
		} else if (!strncmp("end", data_in+i, 3)) {
			xstrcat(fields, "time_end");
			i+=3;
		} else if (!strncmp("comp_code", data_in+i, 9)) {
			xstrcat(fields, "exit_code");
			i+=9;
		} else if (!strncmp("alloc_cpus", data_in+i, 10)) {
			xstrcat(fields, "cpus_alloc");
			i+=10;
		} else if (!strncmp("req_cpus", data_in+i, 8)) {
			xstrcat(fields, "cpus_req");
			i+=8;
		} else if (!strncmp("alloc_nodes", data_in+i, 11)) {
			xstrcat(fields, "nodes_alloc");
			i+=11;
		} else if (!strncmp("name", data_in+i, 4)) {
			if (table == job_table)
				xstrcat(fields, "job_name");
			else if (table == step_table)
				xstrcat(fields, "step_name");
			i+=4;
		} else if (!strncmp("id", data_in+i, 2)) {
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
		} else if (!strncmp("cluster_nodes", data_in+i, 13)) {
			/* this is here just to make it easier to
			   handle the cluster field. */
			xstrcat(fields, "cluster_nodes");
			i+=13;
		} else if (!strncmp("cluster", data_in+i, 7)) {
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

				cluster_name = xstrndup(data_in+start,
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
					&& (table != assoc_month_table))) {
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
			if (!strncmp(data_in+ending_end,
				     "on duplicate key", 16)) {
				ending_start = ending_end;
			}
			if (ending_start) {
				if (!strncmp("period_start",
					     data_in+ending_end, 12)) {
					xstrcat(ending, "time_start");
					ending_end+=12;
				} else if (!strncmp("period_end",
						    data_in+ending_end, 10)) {
					xstrcat(ending, "time_end");
					ending_end+=10;
				} else if (!strncmp("jobid",
						    data_in+ending_end, 5)) {
					xstrcat(ending, "id_job");
					ending_end+=5;
				} else if (!strncmp("stepid",
						    data_in+ending_end, 6)) {
					xstrcat(ending, "id_step");
					ending_end+=6;
				} else if (!strncmp("associd",
						    data_in+ending_end, 7)) {
					xstrcat(ending, "id_assoc");
					ending_end+=7;
				} else if (!strncmp("blockid",
						    data_in+ending_end, 7)) {
					xstrcat(ending, "id_block");
					ending_end+=7;
				} else if (!strncmp("wckeyid",
						    data_in+ending_end, 7)) {
					xstrcat(ending, "id_wckey");
					ending_end+=7;
				} else if (!strncmp("uid",
						    data_in+ending_end, 3)) {
					xstrcat(ending, "id_user");
					ending_end+=3;
				} else if (!strncmp("gid",
						    data_in+ending_end, 3)) {
					xstrcat(ending, "id_group");
					ending_end+=3;
				} else if (!strncmp("submit",
						    data_in+ending_end, 6)) {
					xstrcat(ending, "time_submit");
					ending_end+=6;
				} else if (!strncmp("eligible",
						    data_in+ending_end, 8)) {
					xstrcat(ending, "time_eligible");
					ending_end+=8;
				} else if (!strncmp("start",
						    data_in+ending_end, 5)) {
					xstrcat(ending, "time_start");
					ending_end+=5;
				} else if (!strncmp("suspended",
						    data_in+ending_end, 9)) {
					xstrcat(ending, "time_suspended");
					ending_end+=9;
				} else if (!strncmp("end",
						    data_in+ending_end, 3)) {
					xstrcat(ending, "time_end");
					ending_end+=3;
				} else if (!strncmp("comp_code",
						    data_in+ending_end, 9)) {
					xstrcat(ending, "exit_code");
					ending_end+=9;
				} else if (!strncmp("alloc_cpus",
						    data_in+ending_end, 10)) {
					xstrcat(ending, "cpus_alloc");
					ending_end+=10;
				} else if (!strncmp("req_cpus",
						    data_in+ending_end, 8)) {
					xstrcat(ending, "cpus_req");
					ending_end+=8;
				} else if (!strncmp("alloc_nodes",
						    data_in+ending_end, 11)) {
					xstrcat(ending, "nodes_alloc");
					ending_end+=11;
				} else if (!strncmp("name",
						    data_in+ending_end, 4)) {
					if (table == job_table)
						xstrcat(ending, "job_name");
					else if (table == step_table)
						xstrcat(ending, "step_name");
					ending_end+=4;
				} else if (!strncmp("id",
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
		while ((i < ending_start) && i < ending_start) {
			/* get to the start of the values */
			while ((i < ending_start) && data_in[i-1] != '(')
				i++;

			/* find the values */
			cnt = 0;
			while ((i < ending_start) && data_in[i] != ')') {
				start = i;
				while ((i < ending_start)
				       && data_in[i] != ','
				       && data_in[i] != ')') {
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
					if (cluster_name) {
						if (strcmp(cluster_name,
							   new_cluster_name))
							new_cluster = 1;
						else
							xfree(new_cluster_name);
					} else {
						cluster_name = new_cluster_name;
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
					   beginning, cluster_name,
					   table, fields, vals, ending);
				new_cluster = 0;
				xfree(vals);
				xfree(cluster_name);
				cluster_name = new_cluster_name;
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

	if (!cluster_name) {
		error("No cluster given for %s", table);
		goto end_it;
	}

	if (!delete) {
		/* info("adding insert\n%s \"%s_%s\" (%s) values %s %s",
		   beginning, cluster_name, table, fields, vals, ending); */
		xstrfmtcat(data_out, "%s \"%s_%s\" (%s) values %s %s",
			   beginning, cluster_name, table, fields,
			   vals, ending);
	} else {
		if (fields) {
			/* info("adding delete\n%s \"%s_%s\" %s", */
			/*      beginning, cluster_name, table, fields); */
			xstrfmtcat(data_out, "%s \"%s_%s\" %s",
				   beginning, cluster_name, table, fields);
		} else {
			/* info("adding drop\ndrop table \"%s_%s\";", */
			/*      cluster_name, table); */
			xstrfmtcat(data_out, "drop table \"%s_%s\";",
				   cluster_name, table);
		}
	}

end_it:
	xfree(cluster_name);
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

	while (data_in[i]) {
		if ((rc = _process_old_sql_line(data_in+i, &data_out)) == -1)
			break;
		i += rc;
	}
	//rc = -1;

	xfree(data_in);
	if (rc == -1)
		xfree(data_out);
	//info("returning\n%s", data_out);
	*data = data_out;
	return rc;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_events(mysql_conn_t *mysql_conn, char *cluster_name,
				time_t period_end, char *arch_dir,
				uint32_t archive_period)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_event_t event;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", event_req_inx[0]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", event_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from \"%s_%s\" where "
			       "time_start <= %ld "
			       "&& time_end != 0 order by time_start asc",
			       tmp, cluster_name, event_table, period_end);
	xfree(tmp);

//	START_TIMER;
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_EVENTS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (!period_start)
			period_start = slurm_atoul(row[EVENT_REQ_START]);

		memset(&event, 0, sizeof(local_event_t));

		event.cluster_nodes = row[EVENT_REQ_CNODES];
		event.cpu_count = row[EVENT_REQ_CPU];
		event.node_name = row[EVENT_REQ_NODE];
		event.period_end = row[EVENT_REQ_END];
		event.period_start = row[EVENT_REQ_START];
		event.reason = row[EVENT_REQ_REASON];
		event.reason_uid = row[EVENT_REQ_REASON_UID];
		event.state = row[EVENT_REQ_STATE];

		_pack_local_event(&event, SLURM_PROTOCOL_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, "event", archive_period);
	free_buf(buffer);

	if (error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *
_load_events(uint16_t rpc_version, Buf buffer, char *cluster_name,
	     uint32_t rec_cnt)
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
	for(i=0; i<rec_cnt; i++) {
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
			   object.cpu_count,
			   object.reason,
			   object.reason_uid,
			   object.state);

	}
//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of jobs archived or SLURM_ERROR on error */
static uint32_t _archive_jobs(mysql_conn_t *mysql_conn, char *cluster_name,
			      time_t period_end, char *arch_dir,
			      uint32_t archive_period)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_job_t job;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", job_req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", job_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from \"%s_%s\" where "
			       "time_submit < %ld && time_end != 0 && !deleted "
			       "order by time_submit asc",
			       tmp, cluster_name, job_table, period_end);
	xfree(tmp);

//	START_TIMER;
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_JOBS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (!period_start)
			period_start = slurm_atoul(row[JOB_REQ_SUBMIT]);

		memset(&job, 0, sizeof(local_job_t));

		job.account = row[JOB_REQ_ACCOUNT];
		job.alloc_cpus = row[JOB_REQ_ALLOC_CPUS];
		job.alloc_nodes = row[JOB_REQ_ALLOC_NODES];
		job.associd = row[JOB_REQ_ASSOCID];
		job.array_jobid = row[JOB_REQ_ARRAYJOBID];
		job.array_max_tasks = row[JOB_REQ_ARRAY_MAX];
		job.array_taskid = row[JOB_REQ_ARRAYTASKID];
		job.blockid = row[JOB_REQ_BLOCKID];
		job.derived_ec = row[JOB_REQ_DERIVED_EC];
		job.derived_es = row[JOB_REQ_DERIVED_ES];
		job.exit_code = row[JOB_REQ_EXIT_CODE];
		job.timelimit = row[JOB_REQ_TIMELIMIT];
		job.eligible = row[JOB_REQ_ELIGIBLE];
		job.end = row[JOB_REQ_END];
		job.gid = row[JOB_REQ_GID];
		job.id = row[JOB_REQ_ID];
		job.jobid = row[JOB_REQ_JOBID];
		job.kill_requid = row[JOB_REQ_KILL_REQUID];
		job.name = row[JOB_REQ_NAME];
		job.nodelist = row[JOB_REQ_NODELIST];
		job.node_inx = row[JOB_REQ_NODE_INX];
		job.partition = row[JOB_REQ_PARTITION]; /* priority */
		job.priority = row[JOB_REQ_PRIORITY];   /* qos */
		job.qos = row[JOB_REQ_QOS];             /* cpus_req */
		job.req_cpus = row[JOB_REQ_REQ_CPUS];   /* mem_req */
		job.req_mem = row[JOB_REQ_REQ_MEM];     /* id_resv */
		job.resvid = row[JOB_REQ_RESVID];       /* partition */
		job.start = row[JOB_REQ_START];
		job.state = row[JOB_REQ_STATE];
		job.submit = row[JOB_REQ_SUBMIT];
		job.suspended = row[JOB_REQ_SUSPENDED];
		job.track_steps = row[JOB_REQ_TRACKSTEPS];
		job.uid = row[JOB_REQ_UID];
		job.wckey = row[JOB_REQ_WCKEY];
		job.wckey_id = row[JOB_REQ_WCKEYID];

		_pack_local_job(&job, SLURM_PROTOCOL_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, "job", archive_period);
	free_buf(buffer);

	if (error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_jobs(uint16_t rpc_version, Buf buffer,
			char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_job_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, job_table, job_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", job_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_job_t));
		if (_unpack_local_job(&object, rpc_version, buffer)
		    != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}
		if (i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.account,
			   object.array_max_tasks,
			   object.alloc_cpus,
			   object.alloc_nodes,
			   object.associd,
			   object.array_jobid,
			   object.array_taskid,
			   object.blockid,
			   object.derived_ec,
			   object.derived_es,
			   object.exit_code,
			   object.timelimit,
			   object.eligible,
			   object.end,
			   object.gid,
			   object.id,
			   object.jobid,
			   object.kill_requid,
			   object.name,
			   object.nodelist,
			   object.node_inx,
			   object.partition,
			   object.priority,
			   object.qos,
			   object.req_cpus,
			   object.req_mem,
			   object.resvid,
			   object.start,
			   object.state,
			   object.submit,
			   object.suspended,
			   object.track_steps,
			   object.uid,
			   object.wckey,
			   object.wckey_id);

	}
//	END_TIMER2("step query");
//	info("job query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of resvations archived or SLURM_ERROR on error */
static uint32_t _archive_resvs(mysql_conn_t *mysql_conn, char *cluster_name,
			       time_t period_end, char *arch_dir,
			       uint32_t archive_period)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_resv_t resv;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", resv_req_inx[0]);
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", resv_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from \"%s_%s\" where "
			       "time_start <= %ld "
			       "&& time_end != 0 order by time_start asc",
			       tmp, cluster_name, resv_table, period_end);
	xfree(tmp);

//	START_TIMER;
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_RESVS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (!period_start)
			period_start = slurm_atoul(row[RESV_REQ_START]);

		memset(&resv, 0, sizeof(local_resv_t));

		resv.assocs = row[RESV_REQ_ASSOCS];
		resv.cpus = row[RESV_REQ_CPUS];
		resv.flags = row[RESV_REQ_FLAGS];
		resv.id = row[RESV_REQ_ID];
		resv.name = row[RESV_REQ_NAME];
		resv.nodes = row[RESV_REQ_NODES];
		resv.node_inx = row[RESV_REQ_NODE_INX];
		resv.time_end = row[RESV_REQ_END];
		resv.time_start = row[RESV_REQ_START];

		_pack_local_resv(&resv, SLURM_PROTOCOL_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, "resv", archive_period);
	free_buf(buffer);

	if (error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_resvs(uint16_t rpc_version, Buf buffer,
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
			   object.assocs,
			   object.cpus,
			   object.flags,
			   object.nodes,
			   object.node_inx,
			   object.name,
			   object.time_start,
			   object.time_end);
	}
//	END_TIMER2("step query");
//	info("resv query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of steps archived or SLURM_ERROR on error */
static uint32_t _archive_steps(mysql_conn_t *mysql_conn, char *cluster_name,
			       time_t period_end, char *arch_dir,
			       uint32_t archive_period)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_step_t step;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", step_req_inx[0]);
	for(i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", step_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from \"%s_%s\" where "
			       "time_start <= %ld && time_end != 0 "
			       "&& !deleted order by time_start asc",
			       tmp, cluster_name, step_table, period_end);
	xfree(tmp);

//	START_TIMER;
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_STEP_START, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (!period_start)
			period_start = slurm_atoul(row[STEP_REQ_START]);

		memset(&step, 0, sizeof(local_step_t));

		step.ave_cpu = row[STEP_REQ_AVE_CPU];
		step.act_cpufreq = row[STEP_REQ_ACT_CPUFREQ];
		step.consumed_energy = row[STEP_REQ_CONSUMED_ENERGY];
		step.ave_disk_read = row[STEP_REQ_AVE_DISK_READ];
		step.ave_disk_write = row[STEP_REQ_AVE_DISK_WRITE];
		step.ave_pages = row[STEP_REQ_AVE_PAGES];
		step.ave_rss = row[STEP_REQ_AVE_RSS];
		step.ave_vsize = row[STEP_REQ_AVE_VSIZE];
		step.exit_code = row[STEP_REQ_EXIT_CODE];
		step.cpus = row[STEP_REQ_CPUS];
		step.id = row[STEP_REQ_ID];
		step.kill_requid = row[STEP_REQ_KILL_REQUID];
		step.max_disk_read = row[STEP_REQ_MAX_DISK_READ];
		step.max_disk_read_node = row[STEP_REQ_MAX_DISK_READ_NODE];
		step.max_disk_read_task = row[STEP_REQ_MAX_DISK_READ_TASK];
		step.max_disk_write = row[STEP_REQ_MAX_DISK_WRITE];
		step.max_disk_write_node = row[STEP_REQ_MAX_DISK_WRITE_NODE];
		step.max_disk_write_task = row[STEP_REQ_MAX_DISK_WRITE_TASK];
		step.max_pages = row[STEP_REQ_MAX_PAGES];
		step.max_pages_node = row[STEP_REQ_MAX_PAGES_NODE];
		step.max_pages_task = row[STEP_REQ_MAX_PAGES_TASK];
		step.max_rss = row[STEP_REQ_MAX_RSS];
		step.max_rss_node = row[STEP_REQ_MAX_RSS_NODE];
		step.max_rss_task = row[STEP_REQ_MAX_RSS_TASK];
		step.max_vsize = row[STEP_REQ_MAX_VSIZE];
		step.max_vsize_node = row[STEP_REQ_MAX_VSIZE_NODE];
		step.max_vsize_task = row[STEP_REQ_MAX_VSIZE_TASK];
		step.min_cpu = row[STEP_REQ_MIN_CPU];
		step.min_cpu_node = row[STEP_REQ_MIN_CPU_NODE];
		step.min_cpu_task = row[STEP_REQ_MIN_CPU_TASK];
		step.name = row[STEP_REQ_NAME];
		step.nodelist = row[STEP_REQ_NODELIST];
		step.nodes = row[STEP_REQ_NODES];
		step.node_inx = row[STEP_REQ_NODE_INX];
		step.period_end = row[STEP_REQ_END];
		step.period_start = row[STEP_REQ_START];
		step.period_suspended = row[STEP_REQ_SUSPENDED];
		step.req_cpufreq = row[STEP_REQ_REQ_CPUFREQ];
		step.state = row[STEP_REQ_STATE];
		step.stepid = row[STEP_REQ_STEPID];
		step.sys_sec = row[STEP_REQ_SYS_SEC];
		step.sys_usec = row[STEP_REQ_SYS_USEC];
		step.tasks = row[STEP_REQ_TASKS];
		step.task_dist = row[STEP_REQ_TASKDIST];
		step.user_sec = row[STEP_REQ_USER_SEC];
		step.user_usec = row[STEP_REQ_USER_USEC];

		_pack_local_step(&step, SLURM_PROTOCOL_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, "step", archive_period);
	free_buf(buffer);

	if (error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_steps(uint16_t rpc_version, Buf buffer,
			 char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_step_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into \"%s_%s\" (%s",
		   cluster_name, step_table, step_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", step_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
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

		xstrfmtcat(insert, format,
			   object.id,
			   object.stepid,
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
			   object.cpus,
			   object.tasks,
			   object.task_dist,
			   object.user_sec,
			   object.user_usec,
			   object.sys_sec,
			   object.sys_usec,
			   object.max_vsize,
			   object.max_vsize_task,
			   object.max_vsize_node,
			   object.ave_vsize,
			   object.max_rss,
			   object.max_rss_task,
			   object.max_rss_node,
			   object.ave_rss,
			   object.max_pages,
			   object.max_pages_task,
			   object.max_pages_node,
			   object.ave_pages,
			   object.min_cpu,
			   object.min_cpu_task,
			   object.min_cpu_node,
			   object.ave_cpu,
			   object.act_cpufreq,
			   object.consumed_energy,
			   object.req_cpufreq,
			   object.max_disk_read,
			   object.max_disk_read_task,
			   object.max_disk_read_node,
			   object.ave_disk_read,
			   object.max_disk_write,
			   object.max_disk_write_task,
			   object.max_disk_write_node,
			   object.ave_disk_write);

	}
//	END_TIMER2("step query");
//	info("step query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_suspend(mysql_conn_t *mysql_conn, char *cluster_name,
				 time_t period_end, char *arch_dir,
				 uint32_t archive_period)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_suspend_t suspend;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", suspend_req_inx[0]);
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", suspend_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from \"%s_%s\" where "
			       "time_start <= %ld && time_end != 0 "
			       "order by time_start asc",
			       tmp, cluster_name, suspend_table, period_end);
	xfree(tmp);

//	START_TIMER;
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_JOB_SUSPEND, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while ((row = mysql_fetch_row(result))) {
		if (!period_start)
			period_start = slurm_atoul(row[SUSPEND_REQ_START]);

		memset(&suspend, 0, sizeof(local_suspend_t));

		suspend.id = row[SUSPEND_REQ_ID];
		suspend.associd = row[SUSPEND_REQ_ASSOCID];
		suspend.period_start = row[SUSPEND_REQ_START];
		suspend.period_end = row[SUSPEND_REQ_END];

		_pack_local_suspend(&suspend, SLURM_PROTOCOL_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = archive_write_file(buffer, cluster_name,
					period_start, period_end,
					arch_dir, "suspend", archive_period);
	free_buf(buffer);

	if (error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_suspend(uint16_t rpc_version, Buf buffer,
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
			   object.id,
			   object.associd,
			   object.period_start,
			   object.period_end);

	}
//	END_TIMER2("suspend query");
//	info("suspend query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static int _execute_archive(mysql_conn_t *mysql_conn,
			    char *cluster_name,
			    slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	time_t curr_end;
	time_t last_submit = time(NULL);

	if (arch_cond->archive_script)
		return archive_run_script(arch_cond, cluster_name, last_submit);
	else if (!arch_cond->archive_dir) {
		error("No archive dir given, can't process");
		return SLURM_ERROR;
	}

	if (arch_cond->purge_event != NO_VAL) {
		/* remove all data from event table that was older than
		 * period_start * arch_cond->purge_event.
		 */
		if (!(curr_end = archive_setup_end_time(
			      last_submit, arch_cond->purge_event))) {
			error("Parsing purge event");
			return SLURM_ERROR;
		}

		debug4("Purging event entries before %ld for %s",
		       curr_end, cluster_name);

		if (SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_event)) {
			rc = _archive_events(mysql_conn, cluster_name,
					     curr_end, arch_cond->archive_dir,
					     arch_cond->purge_event);
			if (!rc)
				goto exit_events;
			else if (rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("delete from \"%s_%s\" where "
				       "time_start <= %ld && time_end != 0 "
				       "LIMIT %d",
				       cluster_name, event_table, curr_end,
				       MAX_PURGE_LIMIT);
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		while ((rc = mysql_db_delete_affected_rows(
						mysql_conn, query)) > 0);

		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't remove old event data");
			return SLURM_ERROR;
		}
	}

exit_events:

	if (arch_cond->purge_suspend != NO_VAL) {
		/* remove all data from suspend table that was older than
		 * period_start * arch_cond->purge_suspend.
		 */
		if (!(curr_end = archive_setup_end_time(
			      last_submit, arch_cond->purge_suspend))) {
			error("Parsing purge suspend");
			return SLURM_ERROR;
		}

		debug4("Purging suspend entries before %ld for %s",
		       curr_end, cluster_name);

		if (SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_suspend)) {
			rc = _archive_suspend(mysql_conn, cluster_name,
					      curr_end, arch_cond->archive_dir,
					      arch_cond->purge_suspend);
			if (!rc)
				goto exit_suspend;
			else if (rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("delete from \"%s_%s\" where "
				       "time_start <= %ld && time_end != 0 "
				       "LIMIT %d",
				       cluster_name, suspend_table, curr_end,
				       MAX_PURGE_LIMIT);
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		while ((rc = mysql_db_delete_affected_rows(
						mysql_conn, query)) > 0);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't remove old suspend data");
			return SLURM_ERROR;
		}
	}

exit_suspend:

	if (arch_cond->purge_step != NO_VAL) {
		/* remove all data from step table that was older than
		 * start * arch_cond->purge_step.
		 */
		if (!(curr_end = archive_setup_end_time(
			      last_submit, arch_cond->purge_step))) {
			error("Parsing purge step");
			return SLURM_ERROR;
		}

		debug4("Purging step entries before %ld for %s",
		       curr_end, cluster_name);

		if (SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_step)) {
			rc = _archive_steps(mysql_conn, cluster_name,
					    curr_end, arch_cond->archive_dir,
					    arch_cond->purge_step);
			if (!rc)
				goto exit_steps;
			else if (rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from \"%s_%s\" where "
				       "time_start <= %ld && time_end != 0 "
				       "LIMIT %d",
				       cluster_name, step_table, curr_end,
				       MAX_PURGE_LIMIT);
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		while ((rc = mysql_db_delete_affected_rows(
						mysql_conn, query)) > 0);

		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't remove old step data");
			return SLURM_ERROR;
		}
	}
exit_steps:

	if (arch_cond->purge_job != NO_VAL) {
		/* remove all data from job table that was older than
		 * last_submit * arch_cond->purge_job.
		 */
		if (!(curr_end = archive_setup_end_time(
			      last_submit, arch_cond->purge_job))) {
			error("Parsing purge job");
			return SLURM_ERROR;
		}

		debug4("Purging job entries before %ld for %s",
		       curr_end, cluster_name);

		if (SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_job)) {
			rc = _archive_jobs(mysql_conn, cluster_name,
					   curr_end, arch_cond->archive_dir,
					   arch_cond->purge_job);
			if (!rc)
				goto exit_jobs;
			else if (rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from \"%s_%s\" "
				       "where time_submit <= %ld "
				       "&& time_end != 0 LIMIT %d",
				       cluster_name, job_table, curr_end,
				       MAX_PURGE_LIMIT);
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		while ((rc = mysql_db_delete_affected_rows(
						mysql_conn, query)) > 0);

		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't remove old job data");
			return SLURM_ERROR;
		}
	}
exit_jobs:

	if (arch_cond->purge_resv != NO_VAL) {
		/* remove all data from resv table that was older than
		 * last_submit * arch_cond->purge_resv.
		 */
		if (!(curr_end = archive_setup_end_time(
			      last_submit, arch_cond->purge_resv))) {
			error("Parsing purge resv");
			return SLURM_ERROR;
		}

		debug4("Purging resv entries before %ld for %s",
		       curr_end, cluster_name);

		if (SLURMDB_PURGE_ARCHIVE_SET(arch_cond->purge_resv)) {
			rc = _archive_resvs(mysql_conn, cluster_name,
					    curr_end, arch_cond->archive_dir,
					    arch_cond->purge_resv);
			if (!rc)
				goto exit_resvs;
			else if (rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from \"%s_%s\" "
				       "where time_start <= %ld "
				       "&& time_end != 0 LIMIT %d",
				       cluster_name, resv_table, curr_end,
				       MAX_PURGE_LIMIT);
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		while ((rc = mysql_db_delete_affected_rows(
						mysql_conn, query)) > 0);

		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't remove old resv data");
			return SLURM_ERROR;
		}
	}
exit_resvs:
	return SLURM_SUCCESS;
}

extern int as_mysql_jobacct_process_archive(mysql_conn_t *mysql_conn,
					    slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *cluster_name = NULL;
	List use_cluster_list = as_mysql_cluster_list;
	ListIterator itr = NULL;

//	DEF_TIMERS;

	if (!arch_cond) {
		error("No arch_cond was given to archive from.  returning");
		return SLURM_ERROR;
	}

	if (arch_cond->job_cond && arch_cond->job_cond->cluster_list
	    && list_count(arch_cond->job_cond->cluster_list))
		use_cluster_list = arch_cond->job_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		if ((rc = _execute_archive(mysql_conn, cluster_name, arch_cond))
		    != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	return rc;
}

extern int as_mysql_jobacct_process_archive_load(
	mysql_conn_t *mysql_conn, slurmdb_archive_rec_t *arch_rec)
{
	char *data = NULL, *cluster_name = NULL;
	int error_code = SLURM_SUCCESS;
	Buf buffer;
	time_t buf_time;
	uint16_t type = 0, ver = 0;
	uint32_t data_size = 0, rec_cnt = 0, tmp32 = 0;

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
			info("No archive file (%s) to recover",
			     arch_rec->archive_file);
			error_code = ENOENT;
		} else {
			data_allocated = BUF_SIZE;
			data = xmalloc(data_allocated);
			while (1) {
				data_read = read(state_fd, &data[data_size],
						 BUF_SIZE);
				if (data_read < 0) {
					if (errno == EINTR)
						continue;
					else {
						error("Read error on %s: %m",
						      arch_rec->archive_file);
						break;
					}
				} else if (data_read == 0)	/* eof */
					break;
				data_size      += data_read;
				data_allocated += data_read;
				xrealloc(data, data_allocated);
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
		return SLURM_ERROR;
	}

	if (!data) {
		error("It doesn't appear we have anything to load.");
		return SLURM_ERROR;
	}

	/* this is the old version of an archive file where the file
	   was straight sql. */
	if ((strlen(data) >= 12)
	    && (!strncmp("insert into ", data, 12)
		|| !strncmp("delete from ", data, 12)
		|| !strncmp("drop table ", data, 11)
		|| !strncmp("truncate table ", data, 15))) {
		_process_old_sql(&data);
		goto got_sql;
	}

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn,
			 "Version in assoc_mgr_state header is %u", ver);
	/* Don't verify the lower limit as we should be keeping all
	   older versions around here just to support super old
	   archive files since they don't get regenerated all the
	   time.
	*/
	if (ver > SLURM_PROTOCOL_VERSION) {
		error("***********************************************");
		error("Can not recover archive file, incompatible version, "
		      "got %u need <= %u", ver,
		      SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}
	safe_unpack_time(&buf_time, buffer);
	safe_unpack16(&type, buffer);
	unpackstr_ptr(&cluster_name, &tmp32, buffer);
	safe_unpack32(&rec_cnt, buffer);

	if (!rec_cnt) {
		error("we didn't get any records from this file of type '%s'",
		      slurmdbd_msg_type_2_str(type, 0));
		free_buf(buffer);
		goto got_sql;
	}

	switch(type) {
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
	default:
		error("Unknown type '%u' to load from archive", type);
		break;
	}
	free_buf(buffer);

got_sql:
	if (!data) {
		error("No data to load");
		return SLURM_ERROR;
	}
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", data);
	error_code = mysql_db_query_check_after(mysql_conn, data);
	xfree(data);
	if (error_code != SLURM_SUCCESS) {
	unpack_error:
		error("Couldn't load old data");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
