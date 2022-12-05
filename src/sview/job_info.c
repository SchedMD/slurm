/*****************************************************************************\
 *  job_info.c - Functions related to job display mode of sview.
 *****************************************************************************
 *  Portions Copyright (C) 2011-2014 SchedMD LLC
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
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
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/interfaces/select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "src/sview/sview.h"

#define _DEBUG 0
#define MAX_CANCEL_RETRY 10
#define SIZE(a) (sizeof(a)/sizeof(a[0]))

/* We do not read the node table here, but allocated space for up to
 * MAX_NODE_SPACE nodes and generate fatal error if we go higher. Increase
 * this value if needed */
#ifndef SVIEW_MAX_NODE_SPACE
#define SVIEW_MAX_NODE_SPACE (24 * 1024)
#endif

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int color_inx;
	GtkTreeIter iter_ptr;
	bool iter_set;
	uint32_t job_id;
	char *job_id_str;
	job_info_t *job_ptr;
	int node_cnt;
	char *nodes;
	int pos;
	List step_list;
	hostlist_t task_hl;
	char *task_hl_str;
	hostlist_t task_pending_hl;
	char *task_pending_hl_str;
	List task_list;
	List task_pending_list;
} sview_job_info_t;

static List foreach_list = NULL;
static char *stacked_job_list = NULL;

typedef struct {
	int state;
	slurm_step_id_t step_id;
	int array_job_id;
	int array_task_id;
	int het_job_id;
	int het_job_offset;
} jobs_foreach_t;

typedef struct {
	int edit_type;
	GtkWidget *entry;
	job_desc_msg_t *job_msg;
	char *type;
} jobs_foreach_common_t;

enum {
	EDIT_SIGNAL = 1,
	EDIT_SIGNAL_USER,
	EDIT_CANCEL,
	EDIT_CANCEL_USER,
	EDIT_REQUEUE,
	EDIT_SUSPEND,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ACCOUNT,
	SORTID_ACTION,
	SORTID_ALLOC,
	SORTID_ALLOC_NODE,
	SORTID_ARRAY_JOB_ID,
	SORTID_ARRAY_TASK_ID,
	SORTID_BATCH,
	SORTID_BATCH_HOST,
	SORTID_BURST_BUFFER,
	SORTID_CLUSTER_NAME,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_COMMAND,
	SORTID_COMMENT,
	SORTID_CONTIGUOUS,
	SORTID_CORE_SPEC,
/* 	SORTID_CORES_MAX, */
/* 	SORTID_CORES_MIN, */
	SORTID_CPUS,
	SORTID_CPUS_PER_TRES,
	SORTID_CPU_MAX,
	SORTID_CPU_MIN,
	SORTID_CPUS_PER_TASK,
	SORTID_DEADLINE,
	SORTID_DEPENDENCY,
	SORTID_DERIVED_EC,
	SORTID_EXIT_CODE,
	SORTID_EXTRA,
	SORTID_FEATURES,
	SORTID_FED_ACTIVE_SIBS,
	SORTID_FED_ORIGIN,
	SORTID_FED_VIABLE_SIBS,
	SORTID_GRES,
	SORTID_GROUP_ID,
	SORTID_HET_JOB_ID,
	SORTID_HET_JOB_ID_SET,
	SORTID_HET_JOB_OFFSET,
	SORTID_JOBID,
	SORTID_JOBID_FORMATTED,
	SORTID_LAST_SCHED_EVAL,
	SORTID_LICENSES,
	SORTID_MCS_LABEL,
	SORTID_CPU_REQ,
	SORTID_MEM_MIN,
	SORTID_MEM_PER_TRES,
	SORTID_TMP_DISK,
	SORTID_NAME,
	SORTID_NETWORK,
	SORTID_NICE,
	SORTID_NODELIST,
	SORTID_NODELIST_EXC,
	SORTID_NODELIST_REQ,
	SORTID_NODELIST_SCHED,
	SORTID_NODE_INX,
	SORTID_NODES,
	SORTID_NODES_MAX,
	SORTID_NODES_MIN,
/* 	SORTID_NTASKS_PER_CORE, */
/* 	SORTID_NTASKS_PER_NODE, */
/* 	SORTID_NTASKS_PER_SOCKET, */
	SORTID_OVER_SUBSCRIBE,
	SORTID_PARTITION,
	SORTID_PREEMPT_TIME,
	SORTID_PREFER,
	SORTID_PRIORITY,
	SORTID_QOS,
	SORTID_REASON,
	SORTID_REBOOT,
	SORTID_REQUEUE,
	SORTID_RESV_NAME,
	SORTID_RESTARTS,
/* 	SORTID_SOCKETS_MAX, */
/* 	SORTID_SOCKETS_MIN, */
	SORTID_STATE,
	SORTID_STATE_NUM,
	SORTID_STD_ERR,
	SORTID_STD_IN,
	SORTID_STD_OUT,
	SORTID_SWITCHES,
	SORTID_TASKS,
	SORTID_THREAD_SPEC,
/* 	SORTID_THREADS_MAX, */
/* 	SORTID_THREADS_MIN, */
	SORTID_TIME_ACCRUE,
	SORTID_TIME_ELIGIBLE,
	SORTID_TIME_END,
	SORTID_TIMELIMIT,
	SORTID_TIME_RESIZE,
	SORTID_TIME_RUNNING,
	SORTID_TIME_START,
	SORTID_TIME_SUBMIT,
	SORTID_TIME_SUSPEND,
	SORTID_TRES_ALLOC,
	SORTID_TRES_BIND,
	SORTID_TRES_FREQ,
	SORTID_TRES_PER_JOB,
	SORTID_TRES_PER_NODE,
	SORTID_TRES_PER_SOCKET,
	SORTID_TRES_PER_TASK,
	SORTID_UPDATED,
	SORTID_USER_ID,
	SORTID_WCKEY,
	SORTID_WORKDIR,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.
 */
static char *_initial_page_opts = ("JobID,Partition,UserID,Name,"
				   "State,Time_Running,Node_Count,NodeList");

static display_data_t display_data_job[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_JOBID, "JobID", false, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_JOBID_FORMATTED, NULL, false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_COLOR, NULL, true, EDIT_COLOR,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ACTION, "Action", false,
	 EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_ALLOC, NULL, false, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ARRAY_JOB_ID, "Array_Job_ID", false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ARRAY_TASK_ID, "Array_Task_ID", false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_HET_JOB_ID, "Hetjob JobID", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_HET_JOB_ID_SET, "Hetjob JobID Set", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_HET_JOB_OFFSET, "Hetjob Offset", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_USER_ID, "UserID", false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_GROUP_ID, "GroupID", false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_WCKEY, "WCKey", false, EDIT_TEXTBOX, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NAME, "Name", false, EDIT_TEXTBOX, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_STATE, "State", false, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_STATE_NUM, NULL, false, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_PREEMPT_TIME, "Preempt Time", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_RESIZE, "Time Resize", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_RUNNING, "Time Running", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_SUBMIT, "Time Submit", false,
	 EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_ACCRUE, "Time Accrue", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_ELIGIBLE, "Time Eligible", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_START, "Time Start", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_END, "Time End", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_DEADLINE, "Deadline", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_SUSPEND, "Time Suspended", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES, "Node Count", false, EDIT_TEXTBOX,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPUS, "CPU Count",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_EXC, "NodeList Excluded",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_REQ, "NodeList Requested",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_SCHED, "NodeList Scheduled",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CONTIGUOUS, "Contiguous", false, EDIT_MODEL,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CORE_SPEC, "CoreSpec", false, EDIT_TEXTBOX,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_THREAD_SPEC, "ThreadSpec", false, EDIT_TEXTBOX,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REBOOT, "Reboot", false, EDIT_MODEL,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REQUEUE, "Requeue", false, EDIT_MODEL,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_RESTARTS, "Restart Count", false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	/* Priority is a string so we can edit using a text box */
	{G_TYPE_STRING, SORTID_PRIORITY, "Priority", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_DERIVED_EC, "Derived Exit Code", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_EXIT_CODE, "Exit Code", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_BATCH, "Batch Flag", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_BATCH_HOST, "Batch Host", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_BURST_BUFFER, "Burst Buffer", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CLUSTER_NAME, "ClusterName", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPU_MIN, "CPUs Min",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPU_MAX, "CPUs Max",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TASKS, "Task Count",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_OVER_SUBSCRIBE, "OverSubscribe", false,
	 EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_STD_ERR, "Standard Error",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_STD_IN, "Standard In",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_STD_OUT, "Standard Out",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPUS_PER_TASK, "CPUs per Task",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_RESV_NAME, "Reservation Name",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES_MIN, "Nodes (minimum)",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES_MAX, "Nodes Max",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPU_REQ, "Min CPUs Per Node",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MEM_MIN, "Min Memory",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TMP_DISK, "Min Tmp Disk Per Node",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	/* Nice is a string so we can edit using a text box */
	{G_TYPE_STRING, SORTID_NICE, "Nice", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ACCOUNT, "Account", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_QOS, "QOS", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REASON, "Reason Waiting",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_SWITCHES, "Switches",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_FEATURES, "Features",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_PREFER, "Prefer",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_FED_ACTIVE_SIBS, "FedActiveSiblings",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_FED_ORIGIN, "FedOrigin",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_FED_VIABLE_SIBS, "FedViableSiblings",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	/* "gres" replaced by "tres_per_node" in v18.08 */
	{G_TYPE_STRING, SORTID_GRES, "Gres",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_LAST_SCHED_EVAL, "Last Sched Eval",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_LICENSES, "Licenses",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MCS_LABEL, "MCS_Label",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_DEPENDENCY, "Dependency",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ALLOC_NODE, "Alloc Node : Sid",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NETWORK, "Network",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_COMMAND, "Command",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_COMMENT, "Comment",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_EXTRA, "Extra",
	 false, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_WORKDIR, "Work Dir",
	 false, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_POINTER, SORTID_NODE_INX, NULL, false, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPUS_PER_TRES, "CPUs per TRES",  false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MEM_PER_TRES, "Mem per TRES", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_ALLOC, "TRES Alloc", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_BIND, "TRES Bind", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_FREQ, "TRES Freq", false,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_PER_JOB, "TRES Per Job", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_PER_NODE, "TRES Per Node", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_PER_SOCKET, "TRES Per Socket", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TRES_PER_TASK, "TRES Per Task", false,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_UPDATED, NULL, false, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t options_data_job[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", true, JOB_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Signal", true, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Requeue", true, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Cancel", true, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Suspend/Resume", true, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Edit Job", true, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partition", true, JOB_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", true, JOB_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Reservation", true, JOB_PAGE},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

struct signv {
	char *name;
	uint16_t val;
} sig_name_num[ ] = {
	{ "HUP",	SIGHUP  },
	{ "INT",	SIGINT  },
	{ "QUIT",	SIGQUIT },
	{ "ABRT",	SIGABRT },
	{ "KILL",	SIGKILL },
	{ "ALRM",	SIGALRM },
	{ "TERM",	SIGTERM },
	{ "USR1",	SIGUSR1 },
	{ "USR2",	SIGUSR2 },
	{ "CONT",	SIGCONT },
	{ "STOP",	SIGSTOP },
	{ "TSTP",	SIGTSTP },
	{ "TTIN",	SIGTTIN },
	{ "TTOU",	SIGTTOU },
	{ "XCPU",	SIGXCPU },
	{ "SIGHUP",	SIGHUP  },
	{ "SIGINT",	SIGINT  },
	{ "SIGQUIT",	SIGQUIT },
	{ "SIGABRT",	SIGABRT },
	{ "SIGKILL",	SIGKILL },
	{ "SIGALRM",	SIGALRM },
	{ "SIGTERM",	SIGTERM },
	{ "SIGUSR1",	SIGUSR1 },
	{ "SIGUSR2",	SIGUSR2 },
	{ "SIGCONT",	SIGCONT },
	{ "SIGSTOP",	SIGSTOP },
	{ "SIGTSTP",	SIGTSTP },
	{ "SIGTTIN",	SIGTTIN },
	{ "SIGTTOU",	SIGTTOU },
	{ "SIGXCPU",	SIGXCPU },
};

static display_data_t *local_display_data = NULL;
static char *got_edit_signal = NULL;
static GtkTreeModel *last_model = NULL;

static void _update_info_task(sview_job_info_t *sview_job_info_ptr,
			      GtkTreeModel *model,
			      GtkTreeIter *task_iter,
			      GtkTreeIter *iter, bool handle_pending);

static void _update_info_step(sview_job_info_t *sview_job_info_ptr,
			      GtkTreeModel *model,
			      GtkTreeIter *step_iter,
			      GtkTreeIter *iter);

static char *_read_file(const char *f_name)
{
	int fd, f_size, offset = 0;
	ssize_t rd_size;
	struct stat f_stat;
	char *buf;

	fd = open(f_name, 0);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &f_stat)) {
		close(fd);
		return NULL;
	}
	f_size = f_stat.st_size;
	buf = xmalloc(f_size + 1);
	while (offset < f_size) {
		rd_size = read(fd, buf+offset, f_size-offset);
		if (rd_size < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			xfree(buf);
			break;
		}
		offset += rd_size;
	}
	close(fd);
	buf[f_size] = '\0';
	return buf;
}

static void _job_info_free(sview_job_info_t *sview_job_info)
{
	if (sview_job_info) {
		xfree(sview_job_info->job_id_str);
		xfree(sview_job_info->nodes);

		FREE_NULL_LIST(sview_job_info->step_list);
		FREE_NULL_LIST(sview_job_info->task_list);
		FREE_NULL_LIST(sview_job_info->task_pending_list);

		xfree(sview_job_info->task_hl_str);
		xfree(sview_job_info->task_pending_hl_str);

		FREE_NULL_HOSTLIST(sview_job_info->task_hl);
		FREE_NULL_HOSTLIST(sview_job_info->task_pending_hl);
	}
}

static void _job_info_list_del(void *object)
{
	sview_job_info_t *sview_job_info = (sview_job_info_t *)object;

	if (sview_job_info) {
		_job_info_free(sview_job_info);
		xfree(sview_job_info);
	}
}

/* translate signal name to number */
static uint16_t _xlate_signal_name(const char *signal_name)
{
	uint16_t sig_num = NO_VAL16;
	char *end_ptr, *sig_names = NULL;
	int i;

	sig_num = (uint16_t) strtol(signal_name, &end_ptr, 10);

	if ((*end_ptr == '\0') && (sig_num != 0))
		return sig_num;

	for (i=0; i<SIZE(sig_name_num); i++) {
		if (xstrcasecmp(sig_name_num[i].name, signal_name) == 0) {
			xfree(sig_names);
			return sig_name_num[i].val;
		}
		if (i == 0)
			sig_names = xstrdup(sig_name_num[i].name);
		else {
			xstrcat(sig_names, ",");
			xstrcat(sig_names, sig_name_num[i].name);
		}
	}
	xfree(sig_names);
	return NO_VAL16;
}

static int _cancel_job_id (uint32_t job_id, uint16_t signal)
{
	int error_code = SLURM_SUCCESS, i;
	char *temp = NULL;

	if (signal == (uint16_t)-1)
		signal = SIGKILL;
	for (i = 0; i < MAX_CANCEL_RETRY; i++) {
		/* NOTE: RPC always sent to slurmctld rather than directly
		 * to slurmd daemons */
		error_code = slurm_kill_job(job_id, signal, false);
		if (error_code == 0
		    || (errno != ESLURM_TRANSITION_STATE_NO_UPDATE
			&& errno != ESLURM_JOB_PENDING))
			break;
		temp = g_strdup_printf("Sending signal %u to job %u",
				       signal, job_id);
		display_edit_note(temp);
		g_free(temp);
		sleep ( 5 + i );
	}
	if (error_code) {
		error_code = slurm_get_errno();
		if ((error_code != ESLURM_ALREADY_DONE) &&
		    (error_code != ESLURM_INVALID_JOB_ID)) {
			temp = g_strdup_printf(
				"Kill job error on job id %u: %s",
				job_id, slurm_strerror(slurm_get_errno()));
			display_edit_note(temp);
			g_free(temp);
		} else {
			display_edit_note(slurm_strerror(slurm_get_errno()));
		}
	}

	return error_code;
}

static int _cancel_step_id(uint32_t job_id, uint32_t step_id,
			   uint16_t signal)
{
	int error_code = SLURM_SUCCESS, i;
	char *temp = NULL;
	char tmp_char[45];
	slurm_step_id_t step_id_tmp = {
		.job_id = job_id,
		.step_het_comp = NO_VAL,
		.step_id = step_id,
	};

	log_build_step_id_str(&step_id_tmp, tmp_char, sizeof(tmp_char),
			      STEP_ID_FLAG_NONE);

	if (signal == (uint16_t)-1)
		signal = SIGKILL;

	for (i = 0; i < MAX_CANCEL_RETRY; i++) {
		/* NOTE: RPC always sent to slurmctld rather than directly
		 * to slurmd daemons */
		error_code = slurm_kill_job_step(job_id, step_id, signal);

		if (error_code == 0
		    || (errno != ESLURM_TRANSITION_STATE_NO_UPDATE
			&& errno != ESLURM_JOB_PENDING))
			break;
		temp = g_strdup_printf("Sending signal %u to %s",
				       signal, tmp_char);
		display_edit_note(temp);
		g_free(temp);
		sleep ( 5 + i );
	}

	if (error_code) {
		error_code = slurm_get_errno();
		if (error_code != ESLURM_ALREADY_DONE) {
			temp = g_strdup_printf(
				"Kill job error on %s: %s",
		 		tmp_char,
				slurm_strerror(slurm_get_errno()));
			display_edit_note(temp);
			g_free(temp);
		} else {
			display_edit_note(slurm_strerror(slurm_get_errno()));
		}
	}

	return error_code;
}

static void _set_active_combo_job(GtkComboBox *combo,
				  GtkTreeModel *model, GtkTreeIter *iter,
				  int type)
{
	char *temp_char = NULL;
	int action = 0;

	if (model)
		gtk_tree_model_get(model, iter, type, &temp_char, -1);
	if (!temp_char)
		goto end_it;
	switch(type) {
	case SORTID_ACTION:
		if (!xstrcasecmp(temp_char, "None"))
			action = 0;
		else if (!xstrcasecmp(temp_char, "Cancel"))
			action = 1;
		else if (!xstrcasecmp(temp_char, "Suspend"))
			action = 2;
		else if (!xstrcasecmp(temp_char, "Resume"))
			action = 3;
		else if (!xstrcasecmp(temp_char, "Requeue"))
			action = 5;
		else
			action = 0;

		break;
	case SORTID_CONTIGUOUS:
	case SORTID_REBOOT:
	case SORTID_REQUEUE:
	case SORTID_OVER_SUBSCRIBE:
		if (!xstrcasecmp(temp_char, "yes"))
			action = 0;
		else if (!xstrcasecmp(temp_char, "no"))
			action = 1;
		else
			action = 0;
		break;
	default:
		break;
	}
	g_free(temp_char);
end_it:
	gtk_combo_box_set_active(combo, action);

}


/* don't free this char */
static const char *_set_job_msg(job_desc_msg_t *job_msg, const char *new_text,
				int column)
{
	char *type = "";
	int temp_int = 0;
	long long int temp_ll = 0;
	char *p;
	char *token;
	char *sep_char;
	int j;

	/* need to clear global_edit_error here (just in case) */
	global_edit_error = 0;
	if (!job_msg)
		return NULL;

	switch (column) {
	case SORTID_ACTION:
		xfree(got_edit_signal);
		if (!xstrcasecmp(new_text, "None"))
			got_edit_signal = NULL;
		else
			got_edit_signal = xstrdup(new_text);
		break;
	case SORTID_COMMENT:
		job_msg->comment = xstrdup(new_text);
		type = "comment";
		break;
	case SORTID_TIMELIMIT:
		if (!xstrcasecmp(new_text, "infinite"))
			temp_int = INFINITE;
		else
			temp_int = time_str2mins((char *)new_text);

		type = "timelimit";
		if (temp_int <= 0 && temp_int != INFINITE)
			goto return_error;
		job_msg->time_limit = (uint32_t)temp_int;
		break;
	case SORTID_PRIORITY:
		if (!xstrcasecmp(new_text, "infinite"))
			temp_int = INFINITE;
		else
			temp_int = strtol(new_text, (char **)NULL, 10);

		type = "priority";
		if ((temp_int < 0) && (temp_int != INFINITE))
			goto return_error;
		job_msg->priority = (uint32_t)temp_int;
		break;
	case SORTID_NICE:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "nice";
		if (abs(temp_int) > NICE_OFFSET) {
			//error("Invalid nice value, must be between "
			//      "-%d and %d", NICE_OFFSET, NICE_OFFSET);
			goto return_error;
		}
		job_msg->nice = NICE_OFFSET + temp_int;
		break;
	case SORTID_CPU_REQ:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if (*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "min cpus per node";
		if (temp_int <= 0)
			goto return_error;
		job_msg->pn_min_cpus = (uint32_t)temp_int;
		break;
	case SORTID_TASKS:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "requested tasks";
		if (temp_int <= 0)
			goto return_error;
		job_msg->num_tasks = (uint32_t)temp_int;
		break;
	case SORTID_CPUS_PER_TASK:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "cpus per task";
		if (temp_int <= 0)
			goto return_error;
		job_msg->cpus_per_task = (uint32_t)temp_int;
		break;
	case SORTID_RESV_NAME:
		job_msg->reservation = xstrdup(new_text);
		type = "reservation name";
		break;
	case SORTID_NODES_MIN:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if (*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "min nodes";
		if (temp_int <= 0)
			goto return_error;
		job_msg->min_nodes = (uint32_t)temp_int;
		break;
	case SORTID_NODES:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if (*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "node count";
		if (temp_int <= 0)
			goto return_error;
		job_msg->min_nodes = job_msg->max_nodes = (uint32_t)temp_int;
		break;
	case SORTID_NODES_MAX:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if (*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "max nodes";
		if (temp_int <= 0)
			goto return_error;
		job_msg->max_nodes = (uint32_t)temp_int;
		break;
	case SORTID_MEM_MIN:
		temp_ll = strtoll(new_text, &p, 10);
		if (*p == 'g' || *p == 'G')
			temp_ll *= 1024;
		else if (*p == 't' || *p == 'T')
			temp_ll *= 1048576;

		p = xstrcasestr((char *)new_text, "cpu");
		if (p)
			type = "min memory per cpu";
		else
			type = "min memory per node";

		if (temp_ll <= 0)
			goto return_error;
		job_msg->pn_min_memory = (uint64_t) temp_ll;
		if (p)
			job_msg->pn_min_memory |= MEM_PER_CPU;
		break;
	case SORTID_TMP_DISK:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'g' || *p == 'G')
			temp_int *= 1024;
		else if (*p == 't' || *p == 'T')
			temp_int *= 1048576;

		type = "min tmp disk per node";
		if (temp_int <= 0)
			goto return_error;
		job_msg->pn_min_tmp_disk = (uint32_t)temp_int;
		break;
	case SORTID_PARTITION:
		job_msg->partition = xstrdup(new_text);
		type = "partition";
		break;
	case SORTID_NAME:
		job_msg->name = xstrdup(new_text);
		type = "name";
		break;
	case SORTID_HET_JOB_ID:
		job_msg->name = xstrdup(new_text);
		type = "hetjob jobid";
		break;
	case SORTID_HET_JOB_ID_SET:
		job_msg->name = xstrdup(new_text);
		type = "hetjob id set";
		break;
	case SORTID_HET_JOB_OFFSET:
		job_msg->name = xstrdup(new_text);
		type = "hetjob offset";
		break;
	case SORTID_WCKEY:
		job_msg->wckey = xstrdup(new_text);
		type = "wckey";
		break;
	case SORTID_OVER_SUBSCRIBE:
		if (!xstrcasecmp(new_text, "yes"))
			job_msg->shared = 1;
		else
			job_msg->shared = 0;

		type = "oversubscribe";
		break;
	case SORTID_CONTIGUOUS:
		if (!xstrcasecmp(new_text, "yes"))
			job_msg->contiguous = 1;
		else
			job_msg->contiguous = 0;

		type = "contiguous";
		break;
	case SORTID_CORE_SPEC:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "specialized cores";
		if (temp_int <= 0)
			goto return_error;
		job_msg->core_spec = (uint16_t)temp_int;
		break;
	case SORTID_THREAD_SPEC:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "specialized threads";
		if (temp_int <= 0)
			goto return_error;
		job_msg->core_spec = (uint16_t)temp_int | CORE_SPEC_THREAD;
		break;
	case SORTID_REBOOT:
		if (!xstrcasecmp(new_text, "yes"))
			job_msg->reboot = 1;
		else
			job_msg->reboot = 0;
		type = "reboot";
		break;
	case SORTID_REQUEUE:
		if (!xstrcasecmp(new_text, "yes"))
			job_msg->requeue = 1;
		else
			job_msg->requeue = 0;
		type = "requeue";
		break;
	case SORTID_NODELIST_REQ:
		job_msg->req_nodes = xstrdup(new_text);
		type = "requested nodelist";
		break;
	case SORTID_NODELIST_EXC:
		job_msg->exc_nodes = xstrdup(new_text);
		type = "excluded nodelist";
		break;
	case SORTID_FEATURES:
		job_msg->features = xstrdup(new_text);
		type = "features";
		break;
	case SORTID_PREFER:
		job_msg->prefer = xstrdup(new_text);
		type = "prefer";
		break;
	case SORTID_CPUS_PER_TRES:
		job_msg->cpus_per_tres = xstrdup(new_text);
		type = "cpus_per_tres";
		break;
	case SORTID_MEM_PER_TRES:
		job_msg->mem_per_tres = xstrdup(new_text);
		type = "mem_per_tres";
		break;
	case SORTID_TRES_PER_JOB:
		job_msg->tres_per_job = xstrdup(new_text);
		type = "tres_per_job";
		break;
	case SORTID_TRES_PER_NODE:
		job_msg->tres_per_node = xstrdup(new_text);
		type = "tres_per_node";
		break;
	case SORTID_TRES_PER_SOCKET:
		job_msg->tres_per_socket = xstrdup(new_text);
		type = "tres_per_socket";
		break;
	case SORTID_TRES_PER_TASK:
		job_msg->tres_per_task = xstrdup(new_text);
		type = "tres_per_task";
		break;
	case SORTID_LICENSES:
		job_msg->licenses = xstrdup(new_text);
		type = "licenses";
		break;
	case SORTID_MCS_LABEL:
		job_msg->mcs_label = xstrdup(new_text);
		type = "mcs_label";
		break;
	case SORTID_ACCOUNT:
		job_msg->account = xstrdup(new_text);
		type = "account";
		break;
	case SORTID_BURST_BUFFER:
		job_msg->burst_buffer = xstrdup(new_text);
		type = "burst buffer";
		break;
	case SORTID_QOS:
		job_msg->qos = xstrdup(new_text);
		type = "qos";
		break;
	case SORTID_COMMAND:
		type = "script_file";
		xfree(job_msg->script);
		job_msg->script = _read_file(new_text);
		if (job_msg->script == NULL)
			goto return_error;
		if (job_msg->argc) {
			for (j = 0; j < job_msg->argc; j++)
				xfree(job_msg->argv[j]);
		}
		xfree(job_msg->argv);
		xfree(job_msg->name);
		job_msg->argc = 1;
		job_msg->argv = xmalloc(sizeof(char *) * job_msg->argc);
		if (new_text[0] == '/') {
			job_msg->argv[0] = xstrdup(new_text);
			token = strrchr(new_text, (int) '/');
			if (token)
				job_msg->name = xstrdup(token + 1);
		} else {
			job_msg->argv[0] = xmalloc(1024);
			if (!getcwd(job_msg->argv[0], 1024))
				goto return_error;
			xstrcat(job_msg->argv[0], "/");
			xstrcat(job_msg->argv[0], new_text);
			job_msg->name = xstrdup(new_text);
		}
		break;
	case SORTID_DEPENDENCY:
		job_msg->dependency = xstrdup(new_text);
		type = "dependency";
		break;
	case SORTID_TIME_ELIGIBLE:
	case SORTID_TIME_START:
		type = "start time";
		job_msg->begin_time = parse_time((char *)new_text, 0);
		if (!job_msg->begin_time)
			goto return_error;

		if (job_msg->begin_time < time(NULL))
			job_msg->begin_time = time(NULL);
		break;
	case SORTID_DEADLINE:
		type = "deadline";
		job_msg->deadline = parse_time((char *)new_text, 0);
		if (!job_msg->deadline)
			goto return_error;
		if (job_msg->deadline < time(NULL))
			goto return_error;
		break;
	case SORTID_EXTRA:
		job_msg->extra = xstrdup(new_text);
		type = "extra";
		break;
	case SORTID_STD_OUT:
		type = "StdOut";
		job_msg->std_out = xstrdup(new_text);
		break;
	case SORTID_SWITCHES:
		type = "switches";
		job_msg->req_switch =
			(uint32_t) strtol(new_text, &sep_char, 10);
		if (sep_char && sep_char[0] == '@') {
			job_msg->wait4switch = time_str2mins(sep_char+1) * 60;
		}
		break;
	default:
		type = "unknown";
		break;
	}

	if (xstrcmp(type, "unknown"))
		global_send_update_msg = 1;

	return type;

return_error:
	global_edit_error = 1;
	return type;
}

static void _admin_edit_combo_box_job(GtkComboBox *combo,
				      job_desc_msg_t *job_msg)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;

	if (!job_msg)
		return;

	if (!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if (!model) {
		g_print("nothing selected\n");
		return;
	}

	gtk_tree_model_get(model, &iter, 0, &name, -1);
	gtk_tree_model_get(model, &iter, 1, &column, -1);

	_set_job_msg(job_msg, name, column);

	g_free(name);
}

static gboolean _admin_focus_out_job(GtkEntry *entry,
				     GdkEventFocus *event,
				     job_desc_msg_t *job_msg)
{
	if (global_entry_changed) {
		const char *col_name = NULL;
		int type = gtk_entry_get_max_length(entry);
		const char *name = gtk_entry_get_text(entry);
		type -= DEFAULT_ENTRY_LENGTH;

		col_name = _set_job_msg(job_msg, name, type);
		if (global_edit_error) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup_printf(
				"Job %d %s can't be set to %s",
				job_msg->job_id,
				col_name,
				name);
		}
		global_entry_changed = 0;
	}
	return false;
}

static GtkWidget *_admin_full_edit_job(job_desc_msg_t *job_msg,
				       GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	display_data_t *display_data = display_data_job;

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);

	gtk_table_set_homogeneous(table, false);

	for(i = 0; i < SORTID_CNT; i++) {
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (!display_data->name)
				continue;
			if (display_data->id != i)
				continue;
			display_admin_edit(
				table, job_msg, &row, model, iter,
				display_data,
				G_CALLBACK(_admin_edit_combo_box_job),
				G_CALLBACK(_admin_focus_out_job),
				_set_active_combo_job);
			break;
		}
		display_data = display_data_job;
	}
	gtk_table_resize(table, row, 2);

	return GTK_WIDGET(window);
}

static int _nodes_in_list(char *node_list)
{
	hostset_t host_set = hostset_create(node_list);
	int count = hostset_count(host_set);
	hostset_destroy(host_set);
	return count;
}

/* this needs to be freed by xfree() */
static void _convert_char_to_job_and_step(const char *data,
					  int *jobid, int *stepid)
{
	int i = 0;

	if (!data)
		return;
	*jobid = atoi(data);
	*stepid = NO_VAL;
	while (data[i]) {
		if (data[i] == '.') {
			i++;
			if (data[i])
				*stepid = atoi(&data[i]);
			break;
		}
		i++;
	}

	return;
}

static void _layout_job_record(GtkTreeView *treeview,
			       sview_job_info_t *sview_job_info_ptr,
			       int update)
{
	char *nodes = NULL, *reason = NULL, *uname = NULL;
	char tmp_char[256];
	char time_buf[32];
	char tmp1[128];
	char running_char[50];
	time_t now_time = time(NULL);
	int suspend_secs = 0;
	job_info_t *job_ptr = sview_job_info_ptr->job_ptr;
	struct group *group_info = NULL;
	uint16_t term_code = 0, term_sig = 0;
	uint64_t min_mem = 0;

	GtkTreeIter iter;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if (!treestore)
		return;

	if (!job_ptr->nodes || IS_JOB_PENDING(job_ptr) ||
	    !xstrcasecmp(job_ptr->nodes,"waiting...")) {
		sprintf(running_char,"00:00:00");
		nodes = xstrdup("waiting...");
	} else {
		if (IS_JOB_SUSPENDED(job_ptr))
			now_time = job_ptr->pre_sus_time;
		else {
			if (!IS_JOB_RUNNING(job_ptr) &&
			    (job_ptr->end_time != 0))
				now_time = job_ptr->end_time;
			if (job_ptr->suspend_time) {
				now_time = (time_t)
					(difftime(now_time,
						  job_ptr->suspend_time)
					 + job_ptr->pre_sus_time);
			} else
				now_time = (time_t)difftime(
					now_time, job_ptr->start_time);
		}
		suspend_secs = (time(NULL) - job_ptr->start_time) - now_time;
		secs2time_str(now_time, running_char, sizeof(running_char));

		nodes = slurm_sort_node_list_str(sview_job_info_ptr->nodes);
	}

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_ACCOUNT),
				   job_ptr->account);

	snprintf(tmp_char, sizeof(tmp_char), "%s:%u",
		 job_ptr->alloc_node, job_ptr->alloc_sid);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_ALLOC_NODE),
				   tmp_char);

	if (job_ptr->array_task_str ||
	    (job_ptr->array_task_id != NO_VAL)) {
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 job_ptr->array_job_id);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "N/A");
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_ARRAY_JOB_ID),
				   tmp_char);

	if (job_ptr->array_task_str) {
		snprintf(tmp_char, sizeof(tmp_char), "[%s]",
			 job_ptr->array_task_str);
	} else if (job_ptr->array_task_id != NO_VAL) {
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 job_ptr->array_task_id);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "N/A");
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_ARRAY_TASK_ID),
				   tmp_char);

	if (job_ptr->batch_flag)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_BATCH),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_BATCH_HOST),
				   job_ptr->batch_host);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_BURST_BUFFER),
				   job_ptr->burst_buffer);

	if (job_ptr->cluster) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_CLUSTER_NAME),
					   job_ptr->cluster);
	}

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_COMMAND),
				   job_ptr->command);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_COMMENT),
				   job_ptr->comment);

	if (job_ptr->contiguous)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CONTIGUOUS),
				   tmp_char);

	if ((job_ptr->core_spec == NO_VAL16) ||
	    (job_ptr->core_spec & CORE_SPEC_THREAD)) {
		sprintf(tmp_char, "N/A");
	} else {
		sprintf(tmp_char, "%u", job_ptr->core_spec);
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CORE_SPEC),
				   tmp_char);

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->num_cpus);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS),
				   tmp_char);

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->max_cpus);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_MAX),
				   tmp_char);

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->num_cpus);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_MIN),
				   tmp_char);

	if (job_ptr->cpus_per_task == NO_VAL16)
		sprintf(tmp_char, "N/A");
	else
		sprintf(tmp_char, "%u", job_ptr->cpus_per_task);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS_PER_TASK),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS_PER_TRES),
				   job_ptr->cpus_per_tres);

	if (job_ptr->deadline)
		slurm_make_time_str((time_t *)&job_ptr->deadline, tmp_char,
				     sizeof(tmp_char));
	else
		sprintf(tmp_char, "N/A");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_DEADLINE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_DEPENDENCY),
				   job_ptr->dependency);

	if (WIFEXITED(job_ptr->derived_ec))
		term_code = WEXITSTATUS(job_ptr->derived_ec);
	else
		term_code = 0;
	if (WIFSIGNALED(job_ptr->derived_ec))
		term_sig = WTERMSIG(job_ptr->derived_ec);
	else
		term_sig = 0;
	snprintf(tmp_char, sizeof(tmp_char), "%u:%u", term_code, term_sig);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_DERIVED_EC),
				   tmp_char);

	if (WIFEXITED(job_ptr->exit_code))
		term_code = WEXITSTATUS(job_ptr->exit_code);
	else
		term_code = 0;
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	else
		term_sig = 0;
	snprintf(tmp_char, sizeof(tmp_char), "%u:%u", term_code, term_sig);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_EXIT_CODE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_EXTRA),
				   job_ptr->extra);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_FEATURES),
				   job_ptr->features);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PREFER),
				   job_ptr->prefer);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_FED_ACTIVE_SIBS),
				   job_ptr->fed_siblings_active_str);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_FED_ORIGIN),
				   job_ptr->fed_origin_str);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_FED_VIABLE_SIBS),
				   job_ptr->fed_siblings_viable_str);

	/* "gres" replaced by "tres_per_node" in v18.08 */
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_GRES),
				   job_ptr->tres_per_node);

	group_info = getgrgid((gid_t)job_ptr->group_id);
	if (group_info && group_info->gr_name[0])
		snprintf(tmp_char, sizeof(tmp_char), "%s", group_info->gr_name);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->group_id);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_GROUP_ID),
				   tmp_char);

	if (job_ptr->array_task_str) {
		snprintf(tmp_char, sizeof(tmp_char), "%u_[%s] (%u)",
			 job_ptr->array_job_id, job_ptr->array_task_str,
			 job_ptr->job_id);
	} else if (job_ptr->array_task_id != NO_VAL) {
		snprintf(tmp_char, sizeof(tmp_char), "%u_%u (%u)",
			 job_ptr->array_job_id, job_ptr->array_task_id,
			 job_ptr->job_id);
	} else if (job_ptr->het_job_id) {
		snprintf(tmp_char, sizeof(tmp_char), "%u+%u (%u)",
			 job_ptr->het_job_id,
			 job_ptr->het_job_offset,
			 job_ptr->job_id);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->job_id);
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_JOBID),
				   tmp_char);

	slurm_make_time_str(&job_ptr->last_sched_eval, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_LAST_SCHED_EVAL),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_LICENSES),
				   job_ptr->licenses);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_MCS_LABEL),
				   (job_ptr->mcs_label==NULL) ? "N/A" :
						 job_ptr->mcs_label);

	convert_num_unit((float)job_ptr->pn_min_cpus, tmp_char,
			 sizeof(tmp_char), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_REQ),
				   tmp_char);

	min_mem = job_ptr->pn_min_memory;
	if (min_mem & MEM_PER_CPU)
		min_mem &= (~MEM_PER_CPU);

	if (min_mem > 0) {
		int len;
		convert_num_unit((float)min_mem, tmp_char, sizeof(tmp_char),
				 UNIT_MEGA, NO_VAL,
				 working_sview_config.convert_flags);
		len = strlen(tmp_char);
		if (job_ptr->pn_min_memory & MEM_PER_CPU)
			sprintf(tmp_char+len, " Per CPU");
		else
			sprintf(tmp_char+len, " Per Node");
	} else
		sprintf(tmp_char, " ");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_MEM_MIN),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_MEM_PER_TRES),
				   job_ptr->mem_per_tres);

	if (job_ptr->pn_min_tmp_disk > 0)
		convert_num_unit((float)job_ptr->pn_min_tmp_disk, tmp_char,
				 sizeof(tmp_char), UNIT_MEGA, NO_VAL,
				 working_sview_config.convert_flags);
	else
		sprintf(tmp_char, " ");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TMP_DISK),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NAME),
				   job_ptr->name);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NETWORK),
				   job_ptr->network);

	snprintf(tmp_char, sizeof(tmp_char), "%"PRIi64,
		 (((int64_t)job_ptr->nice) - NICE_OFFSET));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NICE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODELIST),
				   nodes);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODELIST_EXC),
				   job_ptr->exc_nodes);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODELIST_REQ),
				   job_ptr->req_nodes);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODELIST_SCHED),
				   job_ptr->sched_nodes);

	snprintf(tmp_char, sizeof(tmp_char), "%u",
		 sview_job_info_ptr->node_cnt);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODES),
				   tmp_char);

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->max_nodes);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODES_MAX),
				   tmp_char);

	snprintf(tmp_char, sizeof(tmp_char), "%u",
		 sview_job_info_ptr->node_cnt);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODES_MIN),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_OVER_SUBSCRIBE),
				   job_share_string(job_ptr->shared));

	if (job_ptr->het_job_id) {
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 job_ptr->het_job_id);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "N/A");
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_HET_JOB_ID),
				   tmp_char);

	if (job_ptr->het_job_offset) {
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 job_ptr->het_job_offset);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "N/A");
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_HET_JOB_OFFSET),
				   tmp_char);

	if (job_ptr->het_job_id_set) {
		snprintf(tmp_char, sizeof(tmp_char), "%s",
			 job_ptr->het_job_id_set);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "N/A");
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_HET_JOB_ID_SET),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PARTITION),
				   job_ptr->partition);

	if (job_ptr->preempt_time) {
		slurm_make_time_str((time_t *)&job_ptr->preempt_time, tmp_char,
				    sizeof(tmp_char));
	} else
		sprintf(tmp_char, "N/A");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PREEMPT_TIME),
				   tmp_char);

	sprintf(tmp_char, "%u", job_ptr->priority);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PRIORITY),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_QOS),
				   job_ptr->qos);

	if (job_ptr->state_desc)
		reason = job_ptr->state_desc;
	else
		reason = job_reason_string(job_ptr->state_reason);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_REASON), reason);

	if (job_ptr->reboot)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_REBOOT),
				   tmp_char);

	if (job_ptr->requeue)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_REQUEUE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_RESV_NAME),
				   job_ptr->resv_name);

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->restart_cnt);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_RESTARTS),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STATE),
				   job_state_string(job_ptr->job_state));

	slurm_get_job_stderr(tmp1, sizeof(tmp1), job_ptr);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STD_ERR),
				   tmp1);

	slurm_get_job_stdin(tmp1, sizeof(tmp1), job_ptr);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STD_IN),
				   tmp1);

	slurm_get_job_stdout(tmp1, sizeof(tmp1), job_ptr);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STD_OUT),
				   tmp1);

	secs2time_str((time_t) job_ptr->wait4switch, time_buf,
			sizeof(time_buf));
	snprintf(tmp_char, sizeof(tmp_char), "%u@%s",
			job_ptr->req_switch, time_buf);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_SWITCHES),
				   tmp_char);

	if ((job_ptr->core_spec == NO_VAL16) ||
	    ((job_ptr->core_spec & CORE_SPEC_THREAD) == 0)) {
		sprintf(tmp_char, "N/A");
	} else {
		sprintf(tmp_char, "%u",
			job_ptr->core_spec & (~CORE_SPEC_THREAD));
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_THREAD_SPEC),
				   tmp_char);

	slurm_make_time_str((time_t *)&job_ptr->accrue_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_ACCRUE),
				   tmp_char);
	slurm_make_time_str((time_t *)&job_ptr->eligible_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_ELIGIBLE),
				   tmp_char);
	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		sprintf(tmp_char, "Unknown");
	else
		slurm_make_time_str((time_t *)&job_ptr->end_time, tmp_char,
				    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_END),
				   tmp_char);

	if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_char, "Partition Limit");
	else if (job_ptr->time_limit == INFINITE)
		sprintf(tmp_char, "Infinite");
	else
		secs2time_str((job_ptr->time_limit * 60),
			      tmp_char, sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIMELIMIT),
				   tmp_char);

	if (job_ptr->resize_time) {
		slurm_make_time_str((time_t *)&job_ptr->resize_time, tmp_char,
				    sizeof(tmp_char));
	} else
		sprintf(tmp_char, "N/A");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_RESIZE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_RUNNING),
				   running_char);

	slurm_make_time_str((time_t *)&job_ptr->start_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_START),
				   tmp_char);
	slurm_make_time_str((time_t *)&job_ptr->submit_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_SUBMIT),
				   tmp_char);
	secs2time_str(suspend_secs, tmp_char, sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_SUSPEND),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_ALLOC),
				   job_ptr->tres_alloc_str);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_BIND),
				   job_ptr->tres_bind);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_FREQ),
				   job_ptr->tres_freq);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_JOB),
				   job_ptr->tres_per_job);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_NODE),
				   job_ptr->tres_per_node);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_SOCKET),
				   job_ptr->tres_per_socket);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_TASK),
				   job_ptr->tres_per_task);

	uname = uid_to_string_cached((uid_t)job_ptr->user_id);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_USER_ID), uname);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_WCKEY),
				   job_ptr->wckey);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_WORKDIR),
				   job_ptr->work_dir);

	xfree(nodes);
}

static void _update_job_record(sview_job_info_t *sview_job_info_ptr,
			       GtkTreeStore *treestore,
			       GtkTreeIter *iter, bool check_task,
			       bool handle_pending)
{
	char tmp_array_job_id[20], tmp_array_task_id[20];
	char tmp_time_run[40],  tmp_time_resize[256], tmp_time_submit[256];
	char tmp_time_elig[256], tmp_time_start[256],  tmp_time_end[256];
	char tmp_time_sus[40],  tmp_time_limit[40],  tmp_alloc_node[40];
	char tmp_exit[40],      tmp_group_id[40],    tmp_derived_ec[40];
	char tmp_cpu_cnt[40],   tmp_node_cnt[40],    tmp_disk[40];
	char tmp_cpus_max[40],  tmp_mem_min[40],     tmp_cpu_req[40];
	char tmp_nodes_min[40], tmp_nodes_max[40],   tmp_cpus_per_task[40];
	char tmp_prio[40], tmp_nice[40], tmp_preempt_time[256];
	char tmp_rqswitch[40],  tmp_core_spec[40],   tmp_job_id[40];
	char tmp_std_err[128],  tmp_std_in[128],     tmp_std_out[128];
	char tmp_thread_spec[40], tmp_time_deadline[256], tmp_het_job_id[40];
	char tmp_het_job_id_set[40], tmp_het_job_offset[40];
	char tmp_time_accrue[256];
	char *tmp_batch,  *tmp_cont, *tmp_requeue, *tmp_uname;
	char *tmp_reboot, *tmp_reason, *tmp_nodes;
	char time_buf[32];
	time_t now_time = time(NULL);
	int suspend_secs = 0;
	GtkTreeIter step_iter;
	job_info_t *job_ptr = sview_job_info_ptr->job_ptr;
	struct group *group_info = NULL;
	uint16_t term_code = 0, term_sig = 0;
	uint64_t min_mem = 0;

	if (!iter)
		iter = &sview_job_info_ptr->iter_ptr;

	snprintf(tmp_alloc_node, sizeof(tmp_alloc_node), "%s:%u",
		 job_ptr->alloc_node, job_ptr->alloc_sid);

	/*
	 * These need to be set up first, or they could be NULL when
	 * they need to be set.  Since we do a xstrcmp on these later
	 * we need to make sure they are exactly the same (length wise).
	 */
	if (!sview_job_info_ptr->task_hl_str && sview_job_info_ptr->task_hl) {
		sview_job_info_ptr->task_hl_str =
			hostlist_ranged_string_xmalloc(
				sview_job_info_ptr->task_hl);
		if (!job_ptr->het_job_id) {
			snprintf(tmp_job_id, sizeof(tmp_job_id), "%u_%s",
				 job_ptr->array_job_id,
				 sview_job_info_ptr->task_hl_str);
		} else {
			snprintf(tmp_job_id, sizeof(tmp_job_id), "%u+%s",
				 job_ptr->het_job_id,
				 sview_job_info_ptr->task_hl_str);
		}
		xfree(sview_job_info_ptr->task_hl_str);
		sview_job_info_ptr->task_hl_str = xstrdup(tmp_job_id);
	}

	if (!sview_job_info_ptr->task_pending_hl_str
	    && sview_job_info_ptr->task_pending_hl) {
		sview_job_info_ptr->task_pending_hl_str =
			hostlist_ranged_string_xmalloc(
				sview_job_info_ptr->task_pending_hl);
		if (!job_ptr->het_job_id) {
			snprintf(tmp_job_id, sizeof(tmp_job_id), "%u_%s",
				 job_ptr->array_job_id,
				 sview_job_info_ptr->task_pending_hl_str);
		} else {
			snprintf(tmp_job_id, sizeof(tmp_job_id), "%u+%s",
				 job_ptr->het_job_id,
				 sview_job_info_ptr->task_pending_hl_str);
		}
		xfree(sview_job_info_ptr->task_pending_hl_str);
		sview_job_info_ptr->task_pending_hl_str =
			xstrdup(tmp_job_id);
	}

	if (handle_pending &&
	    (job_ptr->het_job_id || job_ptr->array_job_id)) {
		if (job_ptr->array_task_str ||
		    (job_ptr->array_task_id != NO_VAL)) {
			snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
				 sview_job_info_ptr->task_pending_hl_str);
			snprintf(tmp_array_job_id,
				 sizeof(tmp_array_job_id),  "N/A");
			snprintf(tmp_array_task_id,
				 sizeof(tmp_array_task_id), "%s",
				 sview_job_info_ptr->task_pending_hl_str);
		} else if (job_ptr->het_job_id) {
			snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
				 sview_job_info_ptr->task_pending_hl_str);
		}
	} else if (check_task &&
		   (job_ptr->het_job_id || job_ptr->array_job_id)) {
		if (job_ptr->array_task_str ||
		    (job_ptr->array_task_id != NO_VAL)) {
			snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
				 sview_job_info_ptr->task_hl_str);
			snprintf(tmp_array_job_id,
				 sizeof(tmp_array_job_id),  "N/A");
			snprintf(tmp_array_task_id,
				 sizeof(tmp_array_task_id), "%s",
				 sview_job_info_ptr->task_hl_str);
		} else if (job_ptr->het_job_id)
			snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
					sview_job_info_ptr->task_hl_str);
	} else if (job_ptr->array_task_str) {
		snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
			 sview_job_info_ptr->job_id_str);
		snprintf(tmp_array_job_id,  sizeof(tmp_array_job_id),  "%u",
			 job_ptr->array_job_id);
		snprintf(tmp_array_task_id, sizeof(tmp_array_task_id), "[%s]",
			 job_ptr->array_task_str);
	} else if (job_ptr->array_task_id != NO_VAL) {
		snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
			 sview_job_info_ptr->job_id_str);
		snprintf(tmp_array_job_id,  sizeof(tmp_array_job_id),  "%u",
			 job_ptr->array_job_id);
		snprintf(tmp_array_task_id, sizeof(tmp_array_task_id), "%u",
			 job_ptr->array_task_id);
	} else if (job_ptr->het_job_id) {
		snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
			 sview_job_info_ptr->job_id_str);
		snprintf(tmp_het_job_id,      sizeof(tmp_het_job_id),
			 "%u", job_ptr->het_job_id);
		snprintf(tmp_het_job_id_set,  sizeof(tmp_het_job_id_set),
			 "%s", job_ptr->het_job_id_set);
		snprintf(tmp_het_job_offset,  sizeof(tmp_het_job_offset),
			 "%u", job_ptr->het_job_offset);
	 } else {
		snprintf(tmp_job_id, sizeof(tmp_job_id),  "%s",
			 sview_job_info_ptr->job_id_str);
	}


	if (!job_ptr->het_job_id) {
		snprintf(tmp_het_job_id,
			 sizeof(tmp_het_job_id),      "N/A");
		snprintf(tmp_het_job_id_set,
			 sizeof(tmp_het_job_id_set),  "N/A");
		snprintf(tmp_het_job_offset,
			 sizeof(tmp_het_job_offset),  "N/A");
	 }

	if (!job_ptr->array_job_id) {
		snprintf(tmp_array_job_id,
			 sizeof(tmp_array_job_id),     "N/A");
		snprintf(tmp_array_task_id,
			 sizeof(tmp_array_task_id),    "N/A");
	}

	if (job_ptr->batch_flag)
		tmp_batch = "yes";
	else
		tmp_batch = "no";

	if (job_ptr->contiguous)
		tmp_cont = "yes";
	else
		tmp_cont = "no";

	if ((job_ptr->core_spec == NO_VAL16) ||
	    (job_ptr->core_spec & CORE_SPEC_THREAD)) {
		sprintf(tmp_core_spec, "N/A");
	} else {
		sprintf(tmp_core_spec, "%u", job_ptr->core_spec);
	}
	if ((job_ptr->core_spec == NO_VAL16) ||
	    ((job_ptr->core_spec & CORE_SPEC_THREAD) == 0)) {
		sprintf(tmp_thread_spec, "N/A");
	} else {
		sprintf(tmp_thread_spec, "%u",
			job_ptr->core_spec & (~CORE_SPEC_THREAD));
	}


	if (job_ptr->cpus_per_task == NO_VAL16)
		sprintf(tmp_cpus_per_task, "N/A");
	else
		sprintf(tmp_cpus_per_task, "%u", job_ptr->cpus_per_task);

	snprintf(tmp_cpu_cnt, sizeof(tmp_cpu_cnt), "%u", job_ptr->num_cpus);
	convert_num_unit((float)job_ptr->pn_min_cpus, tmp_cpu_req,
			 sizeof(tmp_cpu_req), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	snprintf(tmp_cpus_max, sizeof(tmp_cpus_max), "%u",
		 job_ptr->max_cpus);
	convert_num_unit((float)job_ptr->pn_min_tmp_disk, tmp_disk,
			 sizeof(tmp_disk), UNIT_MEGA, NO_VAL,
			 working_sview_config.convert_flags);

	if (WIFEXITED(job_ptr->derived_ec))
		term_code = WEXITSTATUS(job_ptr->derived_ec);
	else
		term_code = 0;
	if (WIFSIGNALED(job_ptr->derived_ec))
		term_sig = WTERMSIG(job_ptr->derived_ec);
	else
		term_sig = 0;
	snprintf(tmp_derived_ec, sizeof(tmp_derived_ec), "%u:%u",
		 term_code, term_sig);

	if (WIFEXITED(job_ptr->exit_code))
		term_code = WEXITSTATUS(job_ptr->exit_code);
	else
		term_code = 0;
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	else
		term_sig = 0;
	snprintf(tmp_exit, sizeof(tmp_exit), "%u:%u", term_code, term_sig);

	group_info = getgrgid((gid_t) job_ptr->group_id);
	if ( group_info && group_info->gr_name[ 0 ] ) {
		snprintf(tmp_group_id, sizeof(tmp_group_id), "%s",
			 group_info->gr_name);
	} else {
		snprintf(tmp_group_id, sizeof(tmp_group_id), "%u",
			 job_ptr->group_id);
	}

	min_mem = job_ptr->pn_min_memory;
	if (min_mem & MEM_PER_CPU)
		min_mem &= (~MEM_PER_CPU);

	if (min_mem > 0) {
		int len;
		convert_num_unit((float)min_mem, tmp_mem_min,
				 sizeof(tmp_mem_min), UNIT_MEGA, NO_VAL,
				 working_sview_config.convert_flags);
		len = strlen(tmp_mem_min);
		if (job_ptr->pn_min_memory & MEM_PER_CPU)
			sprintf(tmp_mem_min+len, " Per CPU");
		else
			sprintf(tmp_mem_min+len, " Per Node");
	} else
		sprintf(tmp_mem_min, " ");

	sprintf(tmp_node_cnt, "%u", sview_job_info_ptr->node_cnt);

	sprintf(tmp_nodes_min, "%u", sview_job_info_ptr->node_cnt);

	if (job_ptr->state_desc)
		tmp_reason = job_ptr->state_desc;
	else
		tmp_reason = job_reason_string(job_ptr->state_reason);

	if (job_ptr->reboot)
		tmp_reboot = "yes";
	else
		tmp_reboot =  "no";

	if (job_ptr->requeue)
		tmp_requeue = "yes";
	else
		tmp_requeue =  "no";

	snprintf(tmp_nice, sizeof(tmp_nice), "%"PRIi64,
		 (((int64_t)job_ptr->nice) - NICE_OFFSET));

	if (!job_ptr->nodes || IS_JOB_PENDING(job_ptr) ||
	    !xstrcasecmp(job_ptr->nodes,"waiting...")) {
		sprintf(tmp_time_run,"00:00:00");
		tmp_nodes = xstrdup("waiting...");
	} else {
		if (IS_JOB_SUSPENDED(job_ptr))
			now_time = job_ptr->pre_sus_time;
		else {
			if (!IS_JOB_RUNNING(job_ptr) &&
			    (job_ptr->end_time != 0))
				now_time = job_ptr->end_time;
			if (job_ptr->suspend_time) {
				now_time = (time_t)
					(difftime(now_time,
						  job_ptr->suspend_time)
					 + job_ptr->pre_sus_time);
			} else
				now_time = (time_t)difftime(
					now_time, job_ptr->start_time);
		}
		suspend_secs = (time(NULL) - job_ptr->start_time) - now_time;
		secs2time_str(now_time, tmp_time_run, sizeof(tmp_time_run));
		tmp_nodes = slurm_sort_node_list_str(sview_job_info_ptr->nodes);
	}

	if (job_ptr->max_nodes > 0)
		sprintf(tmp_nodes_max, "%u", sview_job_info_ptr->node_cnt);
	else
		tmp_nodes_max[0] = '\0';

	sprintf(tmp_prio, "%u", job_ptr->priority);

	slurm_make_time_str((time_t *)&job_ptr->accrue_time, tmp_time_accrue,
			    sizeof(tmp_time_accrue));

	slurm_make_time_str((time_t *)&job_ptr->eligible_time, tmp_time_elig,
			    sizeof(tmp_time_elig));

	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		sprintf(tmp_time_end, "Unknown");
	else
		slurm_make_time_str((time_t *)&job_ptr->end_time, tmp_time_end,
				    sizeof(tmp_time_end));

	if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_time_limit, "Partition Limit");
	else if (job_ptr->time_limit == INFINITE)
		sprintf(tmp_time_limit, "Infinite");
	else
		secs2time_str((job_ptr->time_limit * 60),
			      tmp_time_limit, sizeof(tmp_time_limit));

	if (job_ptr->preempt_time) {
		slurm_make_time_str((time_t *)&job_ptr->preempt_time,
				    tmp_preempt_time, sizeof(tmp_preempt_time));
	} else
		sprintf(tmp_preempt_time, "N/A");

	if (job_ptr->resize_time) {
		slurm_make_time_str((time_t *)&job_ptr->resize_time,
				    tmp_time_resize, sizeof(tmp_time_resize));
	} else
		sprintf(tmp_time_resize, "N/A");

	slurm_make_time_str((time_t *)&job_ptr->start_time, tmp_time_start,
			    sizeof(tmp_time_start));

	slurm_make_time_str((time_t *)&job_ptr->submit_time, tmp_time_submit,
			    sizeof(tmp_time_submit));

	if (job_ptr->deadline)
		slurm_make_time_str((time_t *)&job_ptr->deadline,
				    tmp_time_deadline,
				    sizeof(tmp_time_deadline));
	else
		sprintf(tmp_time_deadline, "N/A");

	slurm_get_job_stderr(tmp_std_err, sizeof(tmp_std_err), job_ptr);

	slurm_get_job_stdin(tmp_std_in, sizeof(tmp_std_in), job_ptr);

	slurm_get_job_stdout(tmp_std_out, sizeof(tmp_std_out), job_ptr);

	secs2time_str(suspend_secs, tmp_time_sus, sizeof(tmp_time_sus));

	if (job_ptr->req_switch != NO_VAL) {
		if (job_ptr->wait4switch != NO_VAL) {
			secs2time_str((time_t) job_ptr->wait4switch, time_buf,
					sizeof(time_buf));
			sprintf(tmp_rqswitch, "%u@%s",
					job_ptr->req_switch, time_buf);
		} else {
			sprintf(tmp_rqswitch, "%u", job_ptr->req_switch);
		}

	} else {
		sprintf(tmp_rqswitch, "N/A");
	}

	tmp_uname = uid_to_string_cached((uid_t)job_ptr->user_id);

	if ((handle_pending || check_task) &&
	    (job_ptr->array_task_str || (job_ptr->array_task_id != NO_VAL) ||
	     job_ptr->het_job_id)) {
		gtk_tree_store_set(treestore, iter,
				   SORTID_ACCOUNT,      job_ptr->account,
				   SORTID_ALLOC,        1,
				   SORTID_ALLOC_NODE,   tmp_alloc_node,
				   SORTID_ARRAY_JOB_ID, tmp_array_job_id,
				   SORTID_ARRAY_TASK_ID,tmp_array_task_id,
				   SORTID_BATCH,        tmp_batch,
				   SORTID_BATCH_HOST,   job_ptr->batch_host,
				   SORTID_BURST_BUFFER, job_ptr->burst_buffer,
				   SORTID_CLUSTER_NAME, job_ptr->cluster,
				   SORTID_COLOR,
				   sview_colors[sview_job_info_ptr->color_inx],
				   SORTID_COLOR_INX,
				   sview_job_info_ptr->color_inx,
				   SORTID_COMMAND,      job_ptr->command,
				   SORTID_COMMENT,      job_ptr->comment,
				   SORTID_CONTIGUOUS,   tmp_cont,
				   SORTID_EXTRA, job_ptr->extra,
				   SORTID_JOBID,        tmp_job_id,
				   SORTID_JOBID_FORMATTED, tmp_job_id,
				   SORTID_HET_JOB_ID,     tmp_het_job_id,
				   SORTID_HET_JOB_ID_SET, tmp_het_job_id_set,
				   SORTID_HET_JOB_OFFSET, tmp_het_job_offset,
				   SORTID_PARTITION,    job_ptr->partition,
				   SORTID_UPDATED,      1,
				   SORTID_USER_ID,      tmp_uname,
				   -1);
		if (handle_pending)
			gtk_tree_store_set(treestore, iter,
					   SORTID_ACCOUNT, job_ptr->account,
					   SORTID_STATE,
					   job_state_string(JOB_PENDING),
					   -1);
	} else {
		gtk_tree_store_set(treestore, iter,
				   SORTID_ACCOUNT,      job_ptr->account,
				   SORTID_ALLOC,        1,
				   SORTID_ALLOC_NODE,   tmp_alloc_node,
				   SORTID_ARRAY_JOB_ID, tmp_array_job_id,
				   SORTID_ARRAY_TASK_ID,tmp_array_task_id,
				   SORTID_BATCH,        tmp_batch,
				   SORTID_BATCH_HOST,   job_ptr->batch_host,
				   SORTID_BURST_BUFFER, job_ptr->burst_buffer,
				   SORTID_CLUSTER_NAME, job_ptr->cluster,
				   SORTID_COLOR,
				   sview_colors[sview_job_info_ptr->color_inx],
				   SORTID_COLOR_INX,
				   sview_job_info_ptr->color_inx,
				   SORTID_COMMAND,      job_ptr->command,
				   SORTID_COMMENT,      job_ptr->comment,
				   SORTID_CONTIGUOUS,   tmp_cont,
				   SORTID_CORE_SPEC,    tmp_core_spec,
				   SORTID_CPUS,         tmp_cpu_cnt,
				   SORTID_CPU_MAX,      tmp_cpus_max,
				   SORTID_CPU_MIN,      tmp_cpu_cnt,
				   SORTID_CPUS_PER_TASK,tmp_cpus_per_task,
				   SORTID_CPUS_PER_TRES,job_ptr->cpus_per_tres,
				   SORTID_CPU_REQ,      tmp_cpu_req,
				   SORTID_DEADLINE,     tmp_time_deadline,
				   SORTID_DEPENDENCY,   job_ptr->dependency,
				   SORTID_DERIVED_EC,   tmp_derived_ec,
				   SORTID_EXIT_CODE,    tmp_exit,
				   SORTID_EXTRA, job_ptr->extra,
				   SORTID_FEATURES,     job_ptr->features,
				   SORTID_PREFER, job_ptr->prefer,
				   SORTID_FED_ACTIVE_SIBS,
				   job_ptr->fed_siblings_active_str,
				   SORTID_FED_ORIGIN,   job_ptr->fed_origin_str,
				   SORTID_FED_VIABLE_SIBS,
				   job_ptr->fed_siblings_viable_str,
				   SORTID_GROUP_ID,     tmp_group_id,
				   SORTID_JOBID,        tmp_job_id,
				   SORTID_JOBID_FORMATTED, tmp_job_id,
				   SORTID_LICENSES,     job_ptr->licenses,
				   SORTID_MCS_LABEL,	job_ptr->mcs_label,
				   SORTID_MEM_MIN,      tmp_mem_min,
				   SORTID_MEM_PER_TRES, job_ptr->cpus_per_tres,
				   SORTID_NAME,         job_ptr->name,
				   SORTID_NICE,         tmp_nice,
				   SORTID_NODE_INX,     job_ptr->node_inx,
				   SORTID_NODELIST,     tmp_nodes,
				   SORTID_NODELIST_EXC, job_ptr->exc_nodes,
				   SORTID_NODELIST_REQ, job_ptr->req_nodes,
				   SORTID_NODELIST_SCHED,
				   job_ptr->sched_nodes,
				   SORTID_NODES,        tmp_node_cnt,
				   SORTID_NODES_MAX,    tmp_nodes_max,
				   SORTID_NODES_MIN,    tmp_nodes_min,
				   SORTID_OVER_SUBSCRIBE,
				   job_share_string(job_ptr->shared),
				   SORTID_HET_JOB_ID,     tmp_het_job_id,
				   SORTID_HET_JOB_ID_SET, tmp_het_job_id_set,
				   SORTID_HET_JOB_OFFSET, tmp_het_job_offset,
				   SORTID_PARTITION,    job_ptr->partition,
				   SORTID_PREEMPT_TIME, tmp_preempt_time,
				   SORTID_PRIORITY,     tmp_prio,
				   SORTID_QOS,          job_ptr->qos,
				   SORTID_REASON,       tmp_reason,
				   SORTID_REBOOT,       tmp_reboot,
				   SORTID_REQUEUE,      tmp_requeue,
				   SORTID_RESTARTS,     job_ptr->restart_cnt,
				   SORTID_RESV_NAME,    job_ptr->resv_name,
				   SORTID_STATE,
				   job_state_string(job_ptr->job_state),
				   SORTID_STATE_NUM,    job_ptr->job_state,
				   SORTID_STD_ERR,      tmp_std_err,
				   SORTID_STD_IN,       tmp_std_in,
				   SORTID_STD_OUT,      tmp_std_out,
				   SORTID_SWITCHES,     tmp_rqswitch,
				   SORTID_THREAD_SPEC,  tmp_thread_spec,
				   SORTID_TIME_ACCRUE,  tmp_time_accrue,
				   SORTID_TIME_ELIGIBLE,tmp_time_elig,
				   SORTID_TIME_END,     tmp_time_end,
				   SORTID_TIME_RESIZE,  tmp_time_resize,
				   SORTID_TIME_RUNNING, tmp_time_run,
				   SORTID_TIME_START,   tmp_time_start,
				   SORTID_TIME_SUBMIT,  tmp_time_submit,
				   SORTID_TIME_SUSPEND, tmp_time_sus,
				   SORTID_TIMELIMIT,    tmp_time_limit,
				   SORTID_TMP_DISK,     tmp_disk,
				   SORTID_TRES_ALLOC,   job_ptr->tres_alloc_str,
				   SORTID_TRES_BIND,    job_ptr->tres_bind,
				   SORTID_TRES_FREQ,    job_ptr->tres_freq,
				   SORTID_TRES_PER_JOB, job_ptr->tres_per_job,
				   SORTID_TRES_PER_NODE,job_ptr->tres_per_node,
				   SORTID_TRES_PER_SOCKET,
				   job_ptr->tres_per_socket,
				   SORTID_TRES_PER_TASK,job_ptr->tres_per_task,
				   SORTID_UPDATED,      1,
				   SORTID_USER_ID,      tmp_uname,
				   SORTID_WCKEY,        job_ptr->wckey,
				   SORTID_WORKDIR,      job_ptr->work_dir,
				   -1);
	}

	gtk_tree_store_set(treestore, iter,
			   SORTID_NETWORK, job_ptr->network, -1);

	if (check_task &&
	    (job_ptr->array_task_str ||
	     ((job_ptr->array_task_id != NO_VAL) || job_ptr->het_job_id))) {
		if (gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						 &step_iter,
						 iter))
			_update_info_task(sview_job_info_ptr,
					  GTK_TREE_MODEL(treestore), &step_iter,
					  iter, false);
		else
			_update_info_task(sview_job_info_ptr,
					  GTK_TREE_MODEL(treestore), NULL,
					  iter, false);
	} else if (handle_pending &&
		   (job_ptr->array_task_str ||
		    (job_ptr->array_task_id != NO_VAL ||
		     job_ptr->het_job_id))) {
		if (gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						 &step_iter,
						 iter))
			_update_info_task(sview_job_info_ptr,
					  GTK_TREE_MODEL(treestore), &step_iter,
					  iter, true);
		else
			_update_info_task(sview_job_info_ptr,
					  GTK_TREE_MODEL(treestore), NULL,
					  iter, true);
	} else {
		if (gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						 &step_iter,
						 iter))
			_update_info_step(sview_job_info_ptr,
					  GTK_TREE_MODEL(treestore), &step_iter,
					  iter);
		else
			_update_info_step(sview_job_info_ptr,
					  GTK_TREE_MODEL(treestore), NULL,
					  iter);
	}

	xfree(tmp_nodes);

	return;
}

static void _get_step_nodelist(job_step_info_t *step_ptr, char *buf,
			       int buf_size)
{
	char *sorted_nodelist;

	sorted_nodelist = slurm_sort_node_list_str(step_ptr->nodes);
	snprintf(buf, buf_size, "%s", sorted_nodelist);
	xfree(sorted_nodelist);
}

static int _id_from_stepstr(char *str) {
	char *end = NULL;
	int id = strtol(str, &end, 10);
	/* if no digits found, it must be text */
	if (end == str) {
		if (!strcasecmp(str, "TBD"))
			id = SLURM_PENDING_STEP;
		else if (!strcasecmp(str, "Batch"))
			id = SLURM_BATCH_SCRIPT;
		else if (!strcasecmp(str, "Extern"))
			id = SLURM_EXTERN_CONT;
		else if (!strcasecmp(str, "Interactive"))
			id = SLURM_INTERACTIVE_STEP;
		else
			id = NO_VAL;
	}
	return id;
}

static void _stepstr_from_step(job_step_info_t *step_ptr, char *dest,
			       uint32_t len) {
	if (step_ptr->step_id.step_id == SLURM_PENDING_STEP)
		snprintf(dest, len, "TBD");
	else if (step_ptr->step_id.step_id == SLURM_EXTERN_CONT)
		snprintf(dest, len, "Extern");
	else if (step_ptr->step_id.step_id == SLURM_INTERACTIVE_STEP)
		snprintf(dest, len, "Interactive");
	else if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT)
		snprintf(dest, len, "Batch");
	else
		snprintf(dest, len, "%u",
			 step_ptr->step_id.step_id);
}

static void _layout_step_record(GtkTreeView *treeview,
				job_step_info_t *step_ptr,
				int update, bool suspended)
{
	char *uname;
	char tmp_char[100], tmp_str[50], tmp_nodes[50], tmp_time[50];
	GtkTreeIter iter;
	uint32_t state;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if (!treestore)
		return;

	convert_num_unit((float)step_ptr->num_cpus, tmp_char, sizeof(tmp_char),
			 UNIT_NONE, NO_VAL, working_sview_config.convert_flags);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS),
				   tmp_char);

	uname = uid_to_string_cached((uid_t)step_ptr->user_id);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_USER_ID), uname);

	if (step_ptr->array_job_id) {
		snprintf(tmp_char, sizeof(tmp_char), "%u_%u.%u (%u.%u)",
			 step_ptr->array_job_id, step_ptr->array_task_id,
			 step_ptr->step_id.step_id,
			 step_ptr->step_id.job_id, step_ptr->step_id.step_id);
//	} else if (step_ptr->het_job_id) {
//		snprintf(tmp_char, sizeof(tmp_char), "%u+%u.%u (%u.%u)",
//			 step_ptr->het_job_id, step_ptr->het_job_offset,
//			 step_ptr->step_id.step_id,
//			 step_ptr->step_id.job_id, step_ptr->step_id.step_id);
	} else {
		_stepstr_from_step(step_ptr, tmp_str, sizeof(tmp_str));
		snprintf(tmp_char, sizeof(tmp_char), "%u.%s",
			 step_ptr->step_id.job_id, tmp_str);
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_JOBID),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS_PER_TRES),
				   step_ptr->cpus_per_tres);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_MEM_PER_TRES),
				   step_ptr->mem_per_tres);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_BIND),
				   step_ptr->tres_bind);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_FREQ),
				   step_ptr->tres_freq);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_JOB),
				   step_ptr->tres_per_step);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_NODE),
				   step_ptr->tres_per_node);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_SOCKET),
				   step_ptr->tres_per_socket);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_PER_TASK),
				   step_ptr->tres_per_task);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NAME),
				   step_ptr->name);


	if (suspended)
		state = JOB_SUSPENDED;
	else
		state = step_ptr->state;

	if (!step_ptr->nodes
	    || !xstrcasecmp(step_ptr->nodes, "waiting...")) {
		sprintf(tmp_time,"00:00:00");
		snprintf(tmp_nodes, sizeof(tmp_nodes), "waiting...");
		state = JOB_PENDING;
	} else {
		secs2time_str(step_ptr->run_time, tmp_time, sizeof(tmp_time));
		_get_step_nodelist(step_ptr, tmp_nodes, sizeof(tmp_nodes));
		convert_num_unit((float)_nodes_in_list(tmp_nodes),
				 tmp_char, sizeof(tmp_char), UNIT_NONE,
				 NO_VAL,
				 working_sview_config.convert_flags);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODES),
					   tmp_char);
	}

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODELIST),
				   tmp_nodes);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PARTITION),
				   step_ptr->partition);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STATE),
				   job_state_string(state));

	if (step_ptr->time_limit == NO_VAL)
		sprintf(tmp_char, "Partition Limit");
	else if (step_ptr->time_limit == INFINITE)
		sprintf(tmp_char, "Infinite");
	else
		secs2time_str((step_ptr->time_limit * 60),
			      tmp_char, sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIMELIMIT),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_RUNNING),
				   tmp_time);

	convert_num_unit((float)step_ptr->num_tasks, tmp_char, sizeof(tmp_char),
			 UNIT_NONE, NO_VAL, working_sview_config.convert_flags);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TASKS),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TRES_ALLOC),
				   step_ptr->tres_alloc_str);
}

static void _update_step_record(job_step_info_t *step_ptr,
				GtkTreeStore *treestore,
				GtkTreeIter *iter, bool suspended)
{
	char *tmp_uname;
	char tmp_nodes[50];
	char tmp_cpu_min[40],  tmp_time_run[40],   tmp_time_limit[40];
	char tmp_node_cnt[40], tmp_time_start[256], tmp_task_cnt[40];
	char tmp_step_id[40], tmp_job_id[400];
	char tmp_fmt_stepid[40];
	uint32_t state;
	int color_inx = step_ptr->step_id.step_id % sview_colors_cnt;

	convert_num_unit((float)step_ptr->num_cpus, tmp_cpu_min,
			 sizeof(tmp_cpu_min), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	if (suspended)
		state = JOB_SUSPENDED;
	else
		state = step_ptr->state;

	if (!step_ptr->nodes ||
	    !xstrcasecmp(step_ptr->nodes,"waiting...")) {
		sprintf(tmp_time_run, "00:00:00");
		snprintf(tmp_nodes, sizeof(tmp_nodes), "waiting...");
		tmp_node_cnt[0] = '\0';
		state = JOB_PENDING;
	} else {
		secs2time_str(step_ptr->run_time,
			      tmp_time_run, sizeof(tmp_time_run));
		_get_step_nodelist(step_ptr, tmp_nodes, sizeof(tmp_nodes));
		convert_num_unit((float)_nodes_in_list(tmp_nodes),
				 tmp_node_cnt, sizeof(tmp_node_cnt),
				 UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
	}

	convert_num_unit((float)step_ptr->num_tasks, tmp_task_cnt,
			 sizeof(tmp_task_cnt), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	if ((step_ptr->time_limit == NO_VAL) ||
	    (step_ptr->time_limit == INFINITE)) {
		sprintf(tmp_time_limit, "Job Limit");
	} else {
		secs2time_str((step_ptr->time_limit * 60),
			      tmp_time_limit, sizeof(tmp_time_limit));
	}

	slurm_make_time_str((time_t *)&step_ptr->start_time, tmp_time_start,
			    sizeof(tmp_time_start));

	_stepstr_from_step(step_ptr, tmp_fmt_stepid, sizeof(tmp_fmt_stepid));
	snprintf(tmp_step_id, sizeof(tmp_step_id), "%u",
		 step_ptr->step_id.step_id);

	if (step_ptr->array_job_id) {
		snprintf(tmp_job_id, sizeof(tmp_job_id), "%u_%u.%u (%u.%u)",
			 step_ptr->array_job_id, step_ptr->array_task_id,
			 step_ptr->step_id.step_id,
			 step_ptr->step_id.job_id, step_ptr->step_id.step_id);
//	} else if (step_ptr->het_job_id) {
//		snprintf(tmp_job_id, sizeof(tmp_job_id), "%u+%u.%u (%u.%u)",
//			 step_ptr->het_job_id, step_ptr->het_job_offset,
//			 step_ptr->step_id.step_id,
//			 step_ptr->step_id.job_id, step_ptr->step_id.step_id);
	} else {
		snprintf(tmp_job_id, sizeof(tmp_job_id), "%u.%s",
			 step_ptr->step_id.job_id, tmp_fmt_stepid);
	}

	tmp_uname = uid_to_string_cached((uid_t)step_ptr->user_id);

	gtk_tree_store_set(treestore, iter,
			   SORTID_ALLOC,        0,
			   SORTID_COLOR,	sview_colors[color_inx],
			   SORTID_COLOR_INX,    color_inx,
			   SORTID_CPUS,         tmp_cpu_min,
			   SORTID_CPUS_PER_TRES, step_ptr->cpus_per_tres,
			   SORTID_JOBID,        tmp_fmt_stepid,
			   SORTID_JOBID_FORMATTED, tmp_job_id,
			   SORTID_MEM_PER_TRES,	step_ptr->mem_per_tres,
			   SORTID_NAME,         step_ptr->name,
			   SORTID_NODE_INX,     step_ptr->node_inx,
			   SORTID_NODELIST,     tmp_nodes,
			   SORTID_NODES,        tmp_node_cnt,
			   SORTID_PARTITION,    step_ptr->partition,
			   SORTID_STATE,        job_state_string(state),
			   SORTID_TASKS,        tmp_task_cnt,
			   SORTID_TIME_RUNNING, tmp_time_run,
			   SORTID_TIME_START,   tmp_time_start,
			   SORTID_TIMELIMIT,    tmp_time_limit,
			   SORTID_TRES_ALLOC,   step_ptr->tres_alloc_str,
			   SORTID_TRES_BIND,	step_ptr->tres_bind,
			   SORTID_TRES_FREQ,	step_ptr->tres_freq,
			   SORTID_TRES_PER_JOB,	step_ptr->tres_per_step,
			   SORTID_TRES_PER_NODE, step_ptr->tres_per_node,
			   SORTID_TRES_PER_SOCKET, step_ptr->tres_per_socket,
			   SORTID_TRES_PER_TASK, step_ptr->tres_per_task,
			   SORTID_UPDATED,      1,
			   SORTID_USER_ID,      tmp_uname,
			   -1);


	return;
}

static void _append_job_record(sview_job_info_t *sview_job_info_ptr,
			       GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_job_info_ptr->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &sview_job_info_ptr->iter_ptr, SORTID_POS,
			   sview_job_info_ptr->pos, -1);
	_update_job_record(sview_job_info_ptr, treestore, NULL, true, false);
}

static void _append_task_record(sview_job_info_t *sview_job_info_ptr,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				bool handle_pending)
{
	GtkTreeIter task_iter;

	gtk_tree_store_append(treestore, &task_iter, iter);
	gtk_tree_store_set(treestore, &task_iter, SORTID_POS,
			   sview_job_info_ptr->pos, -1);
	_update_job_record(sview_job_info_ptr, treestore, &task_iter, false,
			   handle_pending);
}


static void _append_step_record(job_step_info_t *step_ptr,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int jobid, bool suspended)
{
	GtkTreeIter step_iter;

	gtk_tree_store_append(treestore, &step_iter, iter);
	gtk_tree_store_set(treestore, &step_iter, SORTID_POS, jobid, -1);
	_update_step_record(step_ptr, treestore, &step_iter, suspended);
}

static void _handle_task_check(sview_job_info_t *task_ptr,
			       GtkTreeModel *model,
			       GtkTreeIter **task_iter,
			       GtkTreeIter *first_task_iter,
			       GtkTreeIter *iter,
			       bool handle_pending)
{
	/* get the iter, or find out the list is empty goto add */
	if (!*task_iter)
		goto adding;
	else
		memcpy(*task_iter, first_task_iter, sizeof(GtkTreeIter));

	while (1) {
		char *tmp_taskid, *use_id;
		/* search for the jobid and check to see if
		 * it is in the list */
		gtk_tree_model_get(model, *task_iter, SORTID_JOBID,
				   &tmp_taskid, -1);

		if (handle_pending)
			use_id = task_ptr->task_pending_hl_str;
		else
			use_id = task_ptr->job_id_str;

		if (tmp_taskid && use_id && !xstrcmp(tmp_taskid, use_id)) {
			/* update with new info */
			_update_job_record(task_ptr,
					   GTK_TREE_STORE(model),
					   *task_iter, false, handle_pending);
			g_free(tmp_taskid);
			goto found;
		}
		g_free(tmp_taskid);

		if (!gtk_tree_model_iter_next(model, *task_iter)) {
			*task_iter = NULL;
			break;
		}
	}
adding:
	_append_task_record(task_ptr, GTK_TREE_STORE(model),
			    iter, handle_pending);
found:
	return;


}


static void _update_info_task(sview_job_info_t *sview_job_info_ptr,
			      GtkTreeModel *model,
			      GtkTreeIter *task_iter,
			      GtkTreeIter *iter, bool handle_pending)
{
	int i;
	GtkTreeIter first_task_iter;
	int set = 0;
	ListIterator itr = NULL;
	sview_job_info_t *task_ptr = NULL;

	memset(&first_task_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the tasks are still here */
	if (task_iter) {
		first_task_iter = *task_iter;
		while (1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), task_iter,
					   SORTID_UPDATED, 0, -1);
			if (!gtk_tree_model_iter_next(model, task_iter)) {
				break;
			}
		}
		memcpy(task_iter, &first_task_iter, sizeof(GtkTreeIter));
		set = 1;
	}
	if (handle_pending)
		itr = list_iterator_create(
			sview_job_info_ptr->task_pending_list);
	else {
		/* First handle the pending list if one exists.  This
		   will make a dummy job record that holds all the
		   pending jobs.
		*/
		if (sview_job_info_ptr->task_pending_list &&
		    list_count(sview_job_info_ptr->task_pending_list))
			_handle_task_check(sview_job_info_ptr, model,
					   &task_iter, &first_task_iter,
					   iter, true);
		itr = list_iterator_create(sview_job_info_ptr->task_list);
	}
	while ((task_ptr = list_next(itr)))
		_handle_task_check(task_ptr, model, &task_iter,
				   &first_task_iter, iter, false);
	list_iterator_destroy(itr);

	if (set) {
		task_iter = &first_task_iter;
		/* clear all tasks that aren't active */
		while (1) {
			gtk_tree_model_get(model, task_iter,
					   SORTID_UPDATED, &i, -1);
			if (!i) {
				if (!gtk_tree_store_remove(
					    GTK_TREE_STORE(model),
					    task_iter))
					break;
				else
					continue;
			}
			if (!gtk_tree_model_iter_next(model, task_iter)) {
				break;
			}
		}
	}
	return;
}

static void _update_info_step(sview_job_info_t *sview_job_info_ptr,
			      GtkTreeModel *model,
			      GtkTreeIter *step_iter,
			      GtkTreeIter *iter)
{
	int stepid = 0;
	int i;
	GtkTreeIter first_step_iter;
	int set = 0;
	ListIterator itr = NULL;
	job_step_info_t *step_ptr = NULL;

	memset(&first_step_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the steps are still here */
	if (step_iter) {
		first_step_iter = *step_iter;
		while (1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), step_iter,
					   SORTID_UPDATED, 0, -1);
			if (!gtk_tree_model_iter_next(model, step_iter)) {
				break;
			}
		}
		memcpy(step_iter, &first_step_iter, sizeof(GtkTreeIter));
		set = 1;
	}
	itr = list_iterator_create(sview_job_info_ptr->step_list);
	while ((step_ptr = list_next(itr))) {
		/* get the iter, or find out the list is empty goto add */
		if (!step_iter) {
			goto adding;
		} else {
			memcpy(step_iter, &first_step_iter,
			       sizeof(GtkTreeIter));
		}
		while (1) {
			char *tmp_stepid;
			/* search for the jobid and check to see if
			 * it is in the list */
			gtk_tree_model_get(model, step_iter, SORTID_JOBID,
					   &tmp_stepid, -1);
			stepid = atoi(tmp_stepid);
			g_free(tmp_stepid);
			if (stepid == (int)step_ptr->step_id.step_id) {
				/* update with new info */
				_update_step_record(
					step_ptr, GTK_TREE_STORE(model),
					step_iter, IS_JOB_SUSPENDED(
						sview_job_info_ptr->job_ptr));
				goto found;
			}

			if (!gtk_tree_model_iter_next(model, step_iter)) {
				step_iter = NULL;
				break;
			}
		}
	adding:
		_append_step_record(step_ptr, GTK_TREE_STORE(model),
				    iter, sview_job_info_ptr->job_ptr->job_id,
				    IS_JOB_SUSPENDED(
					    sview_job_info_ptr->job_ptr));
	found:
		;
	}
	list_iterator_destroy(itr);

	if (set) {
		step_iter = &first_step_iter;
		/* clear all steps that aren't active */
		while (1) {
			gtk_tree_model_get(model, step_iter,
					   SORTID_UPDATED, &i, -1);
			if (!i) {
				if (!gtk_tree_store_remove(
					    GTK_TREE_STORE(model),
					    step_iter))
					break;
				else
					continue;
			}
			if (!gtk_tree_model_iter_next(model, step_iter)) {
				break;
			}
		}
	}
	return;
}

static void _update_info_job(List info_list,
			     GtkTreeView *tree_view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	int jobid = 0;
	job_info_t *job_ptr = NULL;
	ListIterator itr = NULL;
	sview_job_info_t *sview_job_info = NULL;

	set_for_update(model, SORTID_UPDATED);

	itr = list_iterator_create(info_list);
	while ((sview_job_info = list_next(itr))) {
		job_ptr = sview_job_info->job_ptr;

		/* This means the tree_store changed (added new column
		 * or something). */
		if (last_model != model)
			sview_job_info->iter_set = false;

		if (sview_job_info->iter_set) {
			char *tmp_jobid = NULL, *offset = NULL;
			gtk_tree_model_get(model, &sview_job_info->iter_ptr,
					   SORTID_JOBID, &tmp_jobid, -1);
			if (!tmp_jobid)
				continue;

			offset = strchr(tmp_jobid, '(');
			if (offset)
				offset++;
			else
				offset = tmp_jobid;
			jobid = atoi(offset);
			g_free(tmp_jobid);

			if ((jobid != job_ptr->job_id)
			    && (jobid != job_ptr->array_job_id) &&
				(jobid != job_ptr->het_job_id)) {
				/* Bad pointer */
				sview_job_info->iter_set = false;
			}
		}
		if (sview_job_info->iter_set)
			_update_job_record(sview_job_info,
					   GTK_TREE_STORE(model), NULL, true,
					   false);
		else {
			_append_job_record(sview_job_info,
					   GTK_TREE_STORE(model));
			sview_job_info->iter_set = true;
		}
	}
	list_iterator_destroy(itr);

	/* remove all old jobs */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
	return;
}

static int _sview_job_sort_aval_dec(void *s1, void *s2)
{
	sview_job_info_t* rec_a = *(sview_job_info_t **)s1;
	sview_job_info_t* rec_b = *(sview_job_info_t **)s2;
	int size_a;
	int size_b;

	size_a = rec_a->node_cnt;
	size_b = rec_b->node_cnt;

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;

	if (rec_a->nodes && rec_b->nodes) {
		size_a = xstrcmp(rec_a->nodes, rec_b->nodes);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}
	return 0;
}

static int _task_array_match(void *x, void *key)
{
	sview_job_info_t *sview_job_info_ptr = (sview_job_info_t *) x;
	job_info_t *job_ptr = (job_info_t *) key;

	if (sview_job_info_ptr->job_ptr->array_job_id  == job_ptr->array_job_id)
		return 1;

	return 0;
}

static int _het_job_id_match(void *x, void *key)
{
	sview_job_info_t *sview_job_info_ptr = (sview_job_info_t *) x;
	job_info_t *job_ptr = (job_info_t *) key;

	if (sview_job_info_ptr->job_ptr->het_job_id  == job_ptr->het_job_id)
		return 1;

	return 0;
}

static List _create_job_info_list(job_info_msg_t *job_info_ptr,
				  job_step_info_response_msg_t *step_info_ptr,
				  int want_odd_states)
{
	static List info_list = NULL;
	static List odd_info_list = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	static job_info_msg_t *last_job_info_ptr = NULL;
	static job_step_info_response_msg_t *last_step_info_ptr = NULL;
	int i = 0, j = 0;
	sview_job_info_t *sview_job_info_ptr = NULL;
	job_info_t *job_ptr = NULL;
	job_step_info_t *step_ptr = NULL;

	if (info_list && (job_info_ptr == last_job_info_ptr)
	    && (step_info_ptr == last_step_info_ptr))
		goto update_color;

	last_job_info_ptr = job_info_ptr;
	last_step_info_ptr = step_info_ptr;

	if (info_list) {
		list_flush(info_list);
		last_list = odd_info_list;
		odd_info_list = list_create(_job_info_list_del);
	} else {
		info_list = list_create(NULL);
		odd_info_list = list_create(_job_info_list_del);
	}
	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for (i=0; i<job_info_ptr->record_count; i++) {
		bool added_task = false;

		job_ptr = &(job_info_ptr->job_array[i]);
		if (job_ptr->job_id == 0)
			continue;

		sview_job_info_ptr = NULL;

		if (last_list_itr) {
			while ((sview_job_info_ptr =
				list_next(last_list_itr))) {
				if (sview_job_info_ptr->job_id ==
				    job_ptr->job_id) {
					list_remove(last_list_itr);
					_job_info_free(sview_job_info_ptr);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}

		if (!sview_job_info_ptr)
			sview_job_info_ptr = xmalloc(sizeof(sview_job_info_t));

		sview_job_info_ptr->job_ptr = job_ptr;
		sview_job_info_ptr->job_id = job_ptr->job_id;

		if (job_ptr->array_task_str ||
		    (job_ptr->array_task_id != NO_VAL)) {
			char task_str[64];
			sview_job_info_t *first_job_info_ptr =
				list_find_first(info_list,
						_task_array_match, job_ptr);
			if (job_ptr->array_task_str) {
				snprintf(task_str, sizeof(task_str), "[%s]",
					 job_ptr->array_task_str);
			} else {
				snprintf(task_str, sizeof(task_str), "%u",
					 job_ptr->array_task_id);
			}

			if (!first_job_info_ptr) {
				sview_job_info_ptr->task_list =
					list_create(NULL);
				sview_job_info_ptr->task_hl =
					hostlist_create(NULL);
				first_job_info_ptr = sview_job_info_ptr;
			} else if (!IS_JOB_COMPLETED(job_ptr))
				added_task = true;

			hostlist_push_host(first_job_info_ptr->task_hl,
					   task_str);

			if (IS_JOB_PENDING(job_ptr)) {
				if (!first_job_info_ptr->task_pending_list)
					first_job_info_ptr->task_pending_list =
						list_create(NULL);
				if (!first_job_info_ptr->task_pending_hl)
					first_job_info_ptr->task_pending_hl =
						hostlist_create(NULL);
				hostlist_push_host(
					first_job_info_ptr->task_pending_hl,
					task_str);
				list_append(first_job_info_ptr->
					    task_pending_list,
					    sview_job_info_ptr);
			} else if (!IS_JOB_COMPLETED(job_ptr))
				list_append(first_job_info_ptr->task_list,
					    sview_job_info_ptr);
			if (job_ptr->array_task_str) {
				sview_job_info_ptr->job_id_str =
					xstrdup_printf("%u_[%s] (%u)",
					 job_ptr->array_job_id,
					 job_ptr->array_task_str,
					 job_ptr->job_id);
			} else {
				sview_job_info_ptr->job_id_str =
					xstrdup_printf("%u_%u (%u)",
					 job_ptr->array_job_id,
					 job_ptr->array_task_id,
					 job_ptr->job_id);
			}
		} else if (job_ptr->het_job_id) {
			char comp_str[64];
			snprintf(comp_str, sizeof(comp_str), "%u",
				 job_ptr->het_job_offset);
			sview_job_info_t *first_job_info_ptr =
				list_find_first(info_list,
						_het_job_id_match, job_ptr);
			if (!first_job_info_ptr) {
				sview_job_info_ptr->task_list =
					list_create(NULL);
				sview_job_info_ptr->task_hl =
					hostlist_create(NULL);
				first_job_info_ptr = sview_job_info_ptr;
			} else if (!IS_JOB_COMPLETED(job_ptr))
				added_task = true;

			hostlist_push_host(first_job_info_ptr->task_hl,
					   comp_str);
			if (!IS_JOB_COMPLETED(job_ptr))
				list_append(first_job_info_ptr->task_list,
					    sview_job_info_ptr);
			sview_job_info_ptr->job_id_str =
				xstrdup_printf("%u+%u (%u)",
				 job_ptr->het_job_id,
				 job_ptr->het_job_offset,
				 job_ptr->job_id);
		} else
			sview_job_info_ptr->job_id_str =
				xstrdup_printf("%u", job_ptr->job_id);

		sview_job_info_ptr->step_list = list_create(NULL);
		sview_job_info_ptr->pos = i;
		sview_job_info_ptr->node_cnt = 0;
		sview_job_info_ptr->color_inx =
			job_ptr->job_id % sview_colors_cnt;
		sview_job_info_ptr->nodes = xstrdup(job_ptr->nodes);
		sview_job_info_ptr->node_cnt = job_ptr->num_nodes;

		for (j = 0; j < step_info_ptr->job_step_count; j++) {
			step_ptr = &(step_info_ptr->job_steps[j]);
			if ((step_ptr->step_id.job_id == job_ptr->job_id) &&
			    (step_ptr->state == JOB_RUNNING)) {
				list_append(sview_job_info_ptr->step_list,
					    step_ptr);
			}
		}
		if (!added_task)
			list_append(odd_info_list, sview_job_info_ptr);

		if (!IS_JOB_PENDING(job_ptr) &&
		    !IS_JOB_RUNNING(job_ptr) &&
		    !IS_JOB_SUSPENDED(job_ptr) &&
		    !IS_JOB_COMPLETING(job_ptr)) {
			continue;
		}

		if (!added_task)
			list_append(info_list, sview_job_info_ptr);
	}

	list_sort(info_list, (ListCmpF)_sview_job_sort_aval_dec);

	list_sort(odd_info_list, (ListCmpF)_sview_job_sort_aval_dec);

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		FREE_NULL_LIST(last_list);
	}

update_color:

	if (want_odd_states)
		return odd_info_list;
	else
		return info_list;

}

void _display_info_job(List info_list, popup_info_t *popup_win)
{
	job_step_info_t *step_ptr;
	specific_info_t *spec_info = popup_win->spec_info;
	ListIterator itr = NULL, itr2 = NULL;
	sview_job_info_t *sview_job_info = NULL, *sview_job_info2 = NULL;
	int found = 0;
	GtkTreeView *treeview = NULL;
	int update = 0;
	int j, k;

	if (spec_info->search_info->int_data == NO_VAL) {
		/* 	info = xstrdup("No pointer given!"); */
		goto finished;
	}

need_refresh:
	if (!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget =
			g_object_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}

	itr = list_iterator_create(info_list);
	while ((sview_job_info = list_next(itr))) {
		if (sview_job_info->job_ptr->job_id ==
		    spec_info->search_info->int_data)
			break;
		if (sview_job_info->task_list) {
			itr2 = list_iterator_create(sview_job_info->task_list);
			while ((sview_job_info2 = list_next(itr2))) {
				if (sview_job_info2->job_ptr->job_id ==
				    spec_info->search_info->int_data)
					break;
			}
			list_iterator_destroy(itr2);
			if (sview_job_info2) {
				sview_job_info = sview_job_info2;
				break;
			}
		}
		if (sview_job_info->task_pending_list) {
			itr2 = list_iterator_create(
				sview_job_info->task_pending_list);
			while ((sview_job_info2 = list_next(itr2))) {
				if (sview_job_info2->job_ptr->job_id ==
				    spec_info->search_info->int_data)
					break;
			}
			list_iterator_destroy(itr2);
			if (sview_job_info2) {
				sview_job_info = sview_job_info2;
				break;
			}
		}
	}
	list_iterator_destroy(itr);

	if (!sview_job_info) {
		/* not found */
	} else if (spec_info->search_info->int_data2 == NO_VAL) {
		int top_node_inx = 0;
		int array_size = SVIEW_MAX_NODE_SPACE;
		int  *color_inx = xmalloc(sizeof(int) * array_size);
		bool *color_set_flag = xmalloc(sizeof(bool) * array_size);
		j = 0;
		while (sview_job_info->job_ptr->node_inx[j] >= 0) {
			top_node_inx = MAX(top_node_inx,
					   sview_job_info->job_ptr->
					   node_inx[j+1]);
			if (top_node_inx > SVIEW_MAX_NODE_SPACE)
				fatal("Expand SVIEW_MAX_NODE_SPACE in sview");
			for (k = sview_job_info->job_ptr->node_inx[j];
			     k <= sview_job_info->job_ptr->node_inx[j+1];
			     k++) {
				color_set_flag[k] = true;
				color_inx[k] = sview_job_info->
					       color_inx;
			}
			j += 2;
		}
		change_grid_color_array(popup_win->grid_button_list,
					top_node_inx+1, color_inx,
					color_set_flag, true, 0);
		xfree(color_inx);
		xfree(color_set_flag);
		_layout_job_record(treeview, sview_job_info, update);
		found = 1;
	} else {
		int top_node_inx = 0;
		int array_size = SVIEW_MAX_NODE_SPACE;
		int  *color_inx = xmalloc(sizeof(int) * array_size);
		bool *color_set_flag = xmalloc(sizeof(bool) * array_size);
		itr = list_iterator_create(sview_job_info->step_list);
		while ((step_ptr = list_next(itr))) {
			if (step_ptr->step_id.step_id ==
			    spec_info->search_info->int_data2) {
				j = 0;
				while (step_ptr->node_inx[j] >= 0) {
					top_node_inx = MAX(top_node_inx,
							   step_ptr->
							   node_inx[j+1]);
					if (top_node_inx > SVIEW_MAX_NODE_SPACE)
						fatal("Expand "
						      "SVIEW_MAX_NODE_SPACE "
						      "in sview");
					for (k = step_ptr->node_inx[j];
					     k <= step_ptr->node_inx[j+1];
					     k++) {
						color_set_flag[k] = true;
						color_inx[k] = step_ptr->step_id.step_id
							% sview_colors_cnt;
					}
					j += 2;
				}
				change_grid_color_array(
					popup_win->grid_button_list,
					top_node_inx+1, color_inx,
					color_set_flag, false, 0);
				xfree(color_inx);
				xfree(color_set_flag);

				_layout_step_record(
					treeview, step_ptr, update,
					IS_JOB_SUSPENDED(
						sview_job_info->job_ptr));
				found = 1;
				break;
			}
		}
		list_iterator_destroy(itr);
		xfree(color_inx);
		xfree(color_set_flag);
	}
	post_setup_popup_grid_list(popup_win);

	if (!found) {
		if (!popup_win->not_found) {
			char *temp = "JOB ALREADY FINISHED OR NOT FOUND\n";
			GtkTreeIter iter;
			GtkTreeModel *model = NULL;

			/* only time this will be run so no update */
			model = gtk_tree_view_get_model(treeview);
			add_display_treestore_line(0,
						   GTK_TREE_STORE(model),
						   &iter,
						   temp, "");
			if (spec_info->search_info->int_data2 != NO_VAL)
				add_display_treestore_line(
					1,
					GTK_TREE_STORE(model),
					&iter,
					find_col_name(display_data_job,
						      SORTID_STATE),
					job_state_string(JOB_COMPLETE));
		}
		popup_win->not_found = true;
	} else {
		if (popup_win->not_found) {
			popup_win->not_found = false;
			gtk_widget_destroy(spec_info->display_widget);

			goto need_refresh;
		}
	}
	gtk_widget_show_all(spec_info->display_widget);

finished:
	return;
}

extern void refresh_job(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_job(popup_win);
}

extern int get_new_info_job(job_info_msg_t **info_ptr,
			    int force)
{
	job_info_msg_t *new_job_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_NO_CHANGE_IN_DATA, i;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
	static uint16_t last_flags = 0;
	slurm_job_info_t *job_ptr;
	char *local_cluster;

	if (g_job_info_ptr && !force
	    && ((now - last) < working_sview_config.refresh_delay)) {
		if (*info_ptr != g_job_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_job_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;

	if (cluster_flags & CLUSTER_FLAG_FED)
		show_flags |= SHOW_FEDERATION;
	if (working_sview_config.show_hidden)
		show_flags |= SHOW_ALL;
	if (g_job_info_ptr) {
		if (show_flags != last_flags)
			g_job_info_ptr->last_update = 0;
		error_code = slurm_load_jobs(g_job_info_ptr->last_update,
					     &new_job_ptr, show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_job_info_msg(g_job_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_job_ptr = g_job_info_ptr;
			changed = 0;
		}
	} else {
		new_job_ptr = NULL;
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr,
					     show_flags);
		changed = 1;
	}

	/* If job not local, clear node_inx to avoid setting node colors */
	if (working_cluster_rec && working_cluster_rec->name)
		local_cluster = xstrdup(working_cluster_rec->name);
	else
		local_cluster = xstrdup(slurm_conf.cluster_name);
	if (error_code == SLURM_SUCCESS) {
		for (i = 0, job_ptr = new_job_ptr->job_array;
		     i < new_job_ptr->record_count; i++, job_ptr++) {
			if (job_ptr->node_inx && job_ptr->cluster &&
			     xstrcmp(job_ptr->cluster, local_cluster)) {
				job_ptr->node_inx[0] = -1;
			}
		}
	}
	xfree(local_cluster);

	last_flags = show_flags;
	g_job_info_ptr = new_job_ptr;

	if (g_job_info_ptr && (*info_ptr != g_job_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_job_info_ptr;
end_it:
	return error_code;
}

extern int get_new_info_job_step(job_step_info_response_msg_t **info_ptr,
				 int force)
{
	job_step_info_response_msg_t *new_step_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;

	if (g_step_info_ptr && !force
	    && ((now - last) < working_sview_config.refresh_delay)) {
		if (*info_ptr != g_step_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_step_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;

	/* This needs to always be like this even if you are only
	   looking for non-hidden jobs or you will get an error below.
	*/
	show_flags |= SHOW_ALL;
	if (g_step_info_ptr) {
		/* Use a last_update time of NULL so that we can get an updated
		 * run_time for jobs rather than just its start_time */
		error_code = slurm_get_job_steps((time_t) NULL,
						 NO_VAL, NO_VAL, &new_step_ptr,
						 show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_job_step_info_response_msg(g_step_info_ptr);
			changed = 1;
		} else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_step_ptr = g_step_info_ptr;
			changed = 0;
		}
	} else {
		new_step_ptr = NULL;
		error_code = slurm_get_job_steps((time_t) NULL, NO_VAL, NO_VAL,
						 &new_step_ptr, show_flags);
		changed = 1;
	}

	g_step_info_ptr = new_step_ptr;

	if (g_step_info_ptr && (*info_ptr != g_step_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_step_info_ptr;
end_it:
	return error_code;
}

extern GtkListStore *create_model_job(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	last_model = NULL;	/* Reformat display */
	switch(type) {
	case SORTID_ACTION:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "None",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Cancel",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Suspend/Resume",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Requeue",
				   -1);
		break;
	case SORTID_CONTIGUOUS:
	case SORTID_REBOOT:
	case SORTID_REQUEUE:
	case SORTID_OVER_SUBSCRIBE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, type,
				   -1);
		break;
	default:
		break;
	}

	return model;
}

extern void admin_edit_job(GtkCellRendererText *cell,
			   const char *path_string,
			   const char *new_text,
			   gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	job_desc_msg_t *job_msg = xmalloc(sizeof(job_desc_msg_t));
	char *tmp_jobid = NULL, *offset;

	char *temp = NULL;
	char *old_text = NULL;
	const char *type = NULL;
	int stepid = NO_VAL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell),
						       "column"));

	if (!new_text || !xstrcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	slurm_init_job_desc_msg(job_msg);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_JOBID, &tmp_jobid,
			   column, &old_text,
			   -1);
	if (!tmp_jobid)
		goto no_input;

	offset = strchr(tmp_jobid, '(');
	if (offset)
		offset++;
	else
		offset = tmp_jobid;
	job_msg->job_id = atoi(offset);
	g_free(tmp_jobid);

	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_ALLOC, &stepid, -1);
	if (stepid)
		stepid = NO_VAL;
	else {
		stepid = job_msg->job_id;
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
				   SORTID_POS, &job_msg->job_id, -1);
	}

	type = _set_job_msg(job_msg, new_text, column);
	if (global_edit_error)
		goto print_error;

	if (got_edit_signal) {
		temp = got_edit_signal;
		got_edit_signal = NULL;
		admin_job(GTK_TREE_MODEL(treestore), &iter, temp, NULL);
		xfree(temp);
		goto no_input;
	}

	if (old_text && !xstrcmp(old_text, new_text)) {
		temp = g_strdup_printf("No change in value.");
	} else if (slurm_update_job(job_msg)
		   == SLURM_SUCCESS) {
		gtk_tree_store_set(treestore, &iter, column, new_text, -1);
		temp = g_strdup_printf("Job %d %s changed to %s",
				       job_msg->job_id,
				       type,
				       new_text);
	} else if (errno == ESLURM_DISABLED) {
		temp = g_strdup_printf(
			"Can only edit %s on pending jobs.", type);
	} else {
	print_error:
		temp = g_strdup_printf("Job %d %s can't be "
				       "set to %s",
				       job_msg->job_id,
				       type,
				       new_text);
	}

	display_edit_note(temp);
	g_free(temp);

no_input:
	slurm_free_job_desc_msg(job_msg);

	gtk_tree_path_free (path);
	g_free(old_text);
	g_mutex_unlock(sview_mutex);
}

extern void get_info_job(GtkTable *table, display_data_t *display_data)
{
	int job_error_code = SLURM_SUCCESS;
	int step_error_code = SLURM_SUCCESS;
	static int view = -1;
	static job_info_msg_t *job_info_ptr = NULL;
	static job_step_info_response_msg_t *step_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int j, k;
	sview_job_info_t *sview_job_info_ptr = NULL;
	job_info_t *job_ptr = NULL;
	ListIterator itr = NULL;
	GtkTreePath *path = NULL;
	static bool set_opts = false;

	if (!set_opts)
		set_page_opts(JOB_PAGE, display_data_job,
			      SORTID_CNT, _initial_page_opts);
	set_opts = true;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		job_info_ptr = NULL;
		step_info_ptr = NULL;
		return;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_job->set_menu = local_display_data->set_menu;
		return;
	}
	if (display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	if ((job_error_code = get_new_info_job(&job_info_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA){

	} else if (job_error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_jobs: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = g_object_ref(GTK_WIDGET(label));
		goto end_it;
	}

	if ((step_error_code = get_new_info_job_step(&step_info_ptr,
						     force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA){
		if ((!display_widget || view == ERROR_VIEW)
		    || (step_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
	} else if (step_error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job_step: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = g_object_ref(GTK_WIDGET(label));
		goto end_it;
	}
display_it:

	info_list = _create_job_info_list(job_info_ptr, step_info_ptr, 0);
	if (!info_list)
		goto reset_curs;

	/* set up the grid */
	if (display_widget && GTK_IS_TREE_VIEW(display_widget)
	    && gtk_tree_selection_count_selected_rows(
		    gtk_tree_view_get_selection(
			    GTK_TREE_VIEW(display_widget)))) {
		GtkTreeViewColumn *focus_column = NULL;
		/* highlight the correct nodes from the last selection */
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(display_widget),
					 &path, &focus_column);
	}
	if (!path) {
		int top_node_inx = 0;
		int array_size = SVIEW_MAX_NODE_SPACE;
		int  *color_inx = xmalloc(sizeof(int) * array_size);
		bool *color_set_flag = xmalloc(sizeof(bool) * array_size);
		itr = list_iterator_create(info_list);
		while ((sview_job_info_ptr = list_next(itr))) {
			uint32_t base_state;
			job_ptr = sview_job_info_ptr->job_ptr;
			base_state = job_ptr->job_state & JOB_STATE_BASE;
			if (base_state != JOB_RUNNING)
				continue;
			j = 0;
			while (job_ptr->node_inx[j] >= 0) {
				top_node_inx = MAX(top_node_inx,
						   job_ptr->node_inx[j+1]);
				if (top_node_inx > SVIEW_MAX_NODE_SPACE) {
					fatal("Increase SVIEW_MAX_NODE_SPACE "
					      "in sview");
				}
				for (k = job_ptr->node_inx[j];
				     k <= job_ptr->node_inx[j+1]; k++) {
					color_set_flag[k] = true;
					color_inx[k] = sview_job_info_ptr->
						       color_inx;
				}
				j += 2;
			}
		}
		list_iterator_destroy(itr);
		change_grid_color_array(grid_button_list, top_node_inx+1,
					color_inx, color_set_flag, true, 0);
		xfree(color_inx);
		xfree(color_set_flag);
		change_grid_color(grid_button_list, -1, -1,
				  MAKE_WHITE, true, 0);
	} else {
		highlight_grid(GTK_TREE_VIEW(display_widget),
			       SORTID_NODE_INX, SORTID_COLOR_INX,
			       grid_button_list);
		gtk_tree_path_free(path);
	}

	if (view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if (!display_widget) {
		tree_view = create_treeview(local_display_data,
					    &grid_button_list);
		/* set multiple capability here */
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		display_widget = g_object_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(table),
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view to the
		 * treestore we don't really care about the return value */
		create_treestore(tree_view, display_data_job,
				 SORTID_CNT, SORTID_TIME_SUBMIT, SORTID_COLOR);
	}

	view = INFO_VIEW;
	/* If the system has a large number of jobs then not all lines
	 * will be displayed. You can try different values for the third
	 * argument of gtk_widget_set_size_request() in an attempt to
	 * maximumize the data displayed in your environment. These are my
	 * results: Y=1000 good for 43 lines, Y=-1 good for 1151 lines,
	 *  Y=64000 good for 2781 lines, Y=99000 good for 1453 lines */
	/* gtk_widget_set_size_request(display_widget, -1, -1); */
	_update_info_job(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = false;
	force_refresh = false;

reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;
}

extern void specific_info_job(popup_info_t *popup_win)
{
	int job_error_code = SLURM_SUCCESS;
	int step_error_code = SLURM_SUCCESS;
	static job_info_msg_t *job_info_ptr = NULL;
	static job_step_info_response_msg_t *step_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List info_list = NULL;
	List send_info_list = NULL;
	int j, k;
	sview_job_info_t *sview_job_info_ptr = NULL;
	job_info_t *job_ptr = NULL;
	ListIterator itr = NULL;
	char *uname = NULL;
	hostset_t hostset = NULL;
	int name_diff;
	int top_node_inx, array_size, *color_inx;
	bool *color_set_flag;

	if (!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_job, SORTID_CNT);

	if (spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if ((job_error_code =
	     get_new_info_job(&job_info_ptr, popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {

	} else if (job_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);

		sprintf(error_char, "slurm_load_jobs: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(popup_win->table),
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = g_object_ref(GTK_WIDGET(label));
		goto end_it;
	}

	if ((step_error_code =
	     get_new_info_job_step(&step_info_ptr, popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if ((!spec_info->display_widget
		     || spec_info->view == ERROR_VIEW)
		    || (step_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
	} else if (step_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job_step: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = g_object_ref(GTK_WIDGET(label));
		goto end_it;
	}
display_it:
	info_list = _create_job_info_list(job_info_ptr, step_info_ptr, 1);
	if (!info_list)
		return;

	if (spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}

	if (spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data,
					    &popup_win->grid_button_list);
		/*set multiple capability here*/
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		spec_info->display_widget =
			g_object_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(popup_win->table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view to the
		 * treestore we don't really care about the return value */
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, SORTID_TIME_SUBMIT, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_job(info_list, popup_win);
		goto end_it;
	}


	/* just linking to another list, don't free the inside, just
	 * the list */
	send_info_list = list_create(NULL);
	itr = list_iterator_create(info_list);
	while ((sview_job_info_ptr = list_next(itr))) {
		job_ptr = sview_job_info_ptr->job_ptr;
		switch (spec_info->type) {
		case JOB_PAGE:
			switch(search_info->search_type) {
			case SEARCH_JOB_ID:
				if (search_info->int_data
				    == NO_VAL) {
					if (!search_info->gchar_data)
						continue;
					_convert_char_to_job_and_step(
						search_info->gchar_data,
						&search_info->int_data,
						&search_info->int_data2);
				}
				if (job_ptr->job_id != search_info->int_data) {
					continue;
				}
#if 0
				/* if we ever want to display just the step
				 * this is where we would do it */
				if (spec_info->search_info->int_data2
				    == NO_VAL)
					break;
				step_itr = list_iterator_create(
					sview_job_info->step_list);
				while ((step_ptr = list_next(itr))) {
					if (step_ptr->step_id.step_id ==
					    spec_info->search_info->int_data2) {
						break;
					}
				}
#endif
				break;
			case SEARCH_JOB_USER:
				if (!search_info->gchar_data)
					continue;
				uname = uid_to_string_cached(job_ptr->user_id);
				name_diff = xstrcmp(uname,
						    search_info->gchar_data);
				if (name_diff)
					continue;
				break;
			case SEARCH_JOB_STATE:
				if (search_info->int_data == NO_VAL)
					continue;

				if (job_ptr->job_state != search_info->int_data)
					continue;
				break;
			default:
				break;
			}
			break;
		case PART_PAGE:
			if (xstrcmp(search_info->gchar_data,
				    job_ptr->partition))
				continue;

			if (search_info->cluster_name &&
			    xstrcmp(search_info->cluster_name,
				    job_ptr->cluster))
				continue;
			break;
		case RESV_PAGE:
			if (!job_ptr->resv_name
			    || xstrcmp(search_info->gchar_data,
				       job_ptr->resv_name))
				continue;
			break;
		case NODE_PAGE:
			if (!job_ptr->nodes)
				continue;

			if (!(hostset = hostset_create(
				      search_info->gchar_data)))
				continue;
			if (!hostset_intersects(hostset, job_ptr->nodes)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);
			break;
		default:
			continue;
		}

		list_push(send_info_list, sview_job_info_ptr);
		top_node_inx = 0;
		array_size = SVIEW_MAX_NODE_SPACE;
		color_inx = xmalloc(sizeof(int) * array_size);
		color_set_flag = xmalloc(sizeof(bool) * array_size);
		j = 0;
		while (job_ptr->node_inx[j] >= 0) {
			top_node_inx = MAX(top_node_inx,
					   job_ptr->node_inx[j+1]);
			if (top_node_inx > SVIEW_MAX_NODE_SPACE)
				fatal("Increase SVIEW_MAX_NODE_SPACE in sview");
			for (k = job_ptr->node_inx[j];
			     k <= job_ptr->node_inx[j+1]; k++) {
				color_set_flag[k] = true;
				color_inx[k] = sview_job_info_ptr->color_inx;
			}
			j += 2;
		}
		change_grid_color_array(popup_win->grid_button_list,
					top_node_inx+1, color_inx,
					color_set_flag, true, 0);
		xfree(color_inx);
		xfree(color_set_flag);
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_job(send_info_list,
			 GTK_TREE_VIEW(spec_info->display_widget));

	FREE_NULL_LIST(send_info_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	return;
}

extern void set_menus_job(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_job, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_job);
		break;
	case ROW_LEFT_CLICKED:
		highlight_grid(tree_view, SORTID_NODE_INX,
			       SORTID_COLOR_INX, button_list);
		break;
	case FULL_CLICKED:
	{
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("job error getting iter from model\n");
			break;
		}

		popup_all_job(model, &iter, INFO_PAGE);

		break;
	}
	case POPUP_CLICKED:
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_job(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL, *cluster_name = NULL;
	char title[100] = {0};
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	int jobid = NO_VAL;
	int stepid = NO_VAL;
	GError *error = NULL;
	char *tmp_jobid = NULL, *offset;

	gtk_tree_model_get(model, iter, SORTID_JOBID, &tmp_jobid, -1);
	if (!tmp_jobid)
		return;

	offset = strchr(tmp_jobid, '(');
	if (offset)
		offset++;
	else
		offset = tmp_jobid;
	jobid = _id_from_stepstr(offset);
	g_free(tmp_jobid);
	gtk_tree_model_get(model, iter, SORTID_JOBID_FORMATTED, &tmp_jobid, -1);
	gtk_tree_model_get(model, iter, SORTID_CLUSTER_NAME, &cluster_name, -1);

	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);

	if (stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	switch(id) {
	case PART_PAGE:
		if (stepid == NO_VAL)
			snprintf(title, 100, "Partition with job %s",
				 tmp_jobid);
		else
			snprintf(title, 100, "Partition with job step %s",
				 tmp_jobid);
		break;
	case RESV_PAGE:
		if (stepid == NO_VAL)
			snprintf(title, 100, "Reservation with job %s",
				 tmp_jobid);
		else
			snprintf(title, 100, "Reservation with job step %s",
				 tmp_jobid);
		break;
	case NODE_PAGE:
		if (stepid == NO_VAL)
			snprintf(title, 100,
				 "Node(s) running job %s", tmp_jobid);
		else
			snprintf(title, 100, "Node(s) running job step %s",
				 tmp_jobid);
		break;
	case INFO_PAGE:
		if (stepid == NO_VAL)
			snprintf(title, 100, "Full info for job %s", tmp_jobid);
		else
			snprintf(title, 100, "Full info for job step %s",
				 tmp_jobid);
		break;
	default:
		g_print("jobs got id %d\n", id);
	}

	if (cluster_name && federation_name &&
	    (cluster_flags & CLUSTER_FLAG_FED)) {
		char *tmp_cname =
			xstrdup_printf(" (%s:%s)",
				       federation_name, cluster_name);
		strncat(title, tmp_cname, sizeof(title) - strlen(title) - 1);
		xfree(tmp_cname);
	}

	if (tmp_jobid)
		g_free(tmp_jobid);

	itr = list_iterator_create(popup_list);
	while ((popup_win = list_next(itr))) {
		if (popup_win->spec_info)
			if (!xstrcmp(popup_win->spec_info->title, title)) {
				break;
			}
	}
	list_iterator_destroy(itr);

	if (!popup_win) {
		if (id == INFO_PAGE)
			popup_win = create_popup_info(id, JOB_PAGE, title);
		else
			popup_win = create_popup_info(JOB_PAGE, id, title);
	} else {
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		g_free(cluster_name);
		return;
	}

	/* Pass the model and the structs from the iter so we can always get
	   the current node_inx.
	*/
	popup_win->model = model;
	popup_win->iter = *iter;
	popup_win->node_inx_id = SORTID_NODE_INX;

	if (cluster_flags & CLUSTER_FLAG_FED) {
		popup_win->spec_info->search_info->cluster_name = cluster_name;
		cluster_name = NULL;
	}
	g_free(cluster_name);

	switch(id) {
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case PART_PAGE:
		gtk_tree_model_get(model, iter, SORTID_PARTITION, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case RESV_PAGE:
		gtk_tree_model_get(model, iter, SORTID_RESV_NAME, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case SUBMIT_PAGE:
		break;
	case INFO_PAGE:
		popup_win->spec_info->search_info->int_data = jobid;
		popup_win->spec_info->search_info->int_data2 = stepid;
		break;

	default:
		g_print("jobs got %d\n", id);
	}
	if (!sview_thread_new((gpointer)popup_thr, popup_win, false, &error)) {
		g_printerr ("Failed to create part popup thread: %s\n",
			    error->message);
		return;
	}
}

static void process_foreach_list(jobs_foreach_common_t *jobs_foreach_common)
{
	int jobid;
	int state;
	int stepid;
	uint16_t signal = SIGKILL;
	int response = 0;
	char *tmp_char_ptr = "";
	jobs_foreach_t *job_foreach = NULL;
	ListIterator itr = NULL;

	if (jobs_foreach_common->edit_type == EDIT_SIGNAL) {
		const gchar *entry_txt = gtk_entry_get_text(
			GTK_ENTRY(jobs_foreach_common->entry));
		signal = _xlate_signal_name(entry_txt);
		if (signal == NO_VAL16) {
			tmp_char_ptr = g_strdup_printf(
				"%s is not a valid signal.",
				entry_txt);
			display_edit_note(tmp_char_ptr);
			g_free(tmp_char_ptr);
			goto end_it;
		}
	}

	itr = list_iterator_create(foreach_list);
	while ((job_foreach = list_next(itr))) {
		/*stop processing remaining jobs on any error*/
		if (global_error_code)
			break;

		jobid = job_foreach->step_id.job_id;
		stepid = job_foreach->step_id.step_id;
		state = job_foreach->state;

		switch(jobs_foreach_common->edit_type) {
		case EDIT_SIGNAL:
			/* fall through to the cancel now the signal
			 * is set (Default is SIGKILL from above for
			 * just a regular cancel).
			 */
		case EDIT_CANCEL:
			if (stepid == NO_VAL)
				global_error_code =
					_cancel_job_id(jobid, signal);
			else
				global_error_code =
					_cancel_step_id(jobid,
							stepid, signal);
			break;
		case EDIT_REQUEUE:
			response = slurm_requeue(jobid, 0);

			if (response) {
				/* stop rest of jobs */
				global_error_code = response;
				tmp_char_ptr = g_strdup_printf(
					"Error happened trying "
					"to requeue job %u: %s",
					jobid, slurm_strerror(response));
				display_edit_note(tmp_char_ptr);
				g_free(tmp_char_ptr);
			}
			break;
		case EDIT_SUSPEND:
			//note: derive state from job_foreach..
			if (state == JOB_SUSPENDED)
				response = slurm_resume(jobid);
			else
				response = slurm_suspend(jobid);
			if (!response) {
				/* stop rest of jobs */
				global_error_code = response;
				tmp_char_ptr = g_strdup_printf(
					"Error happened trying to "
					"SUSPEND/RESUME job %u.",
					jobid);
				display_edit_note(tmp_char_ptr);
				g_free(tmp_char_ptr);
			}
			break;
		default:
			break;

		}/*end switch*/
	} /*spin thru selected jobs*/

	if (global_edit_error || global_error_code)
		goto end_it;

	switch (jobs_foreach_common->edit_type) {
	case EDIT_SIGNAL:
		tmp_char_ptr = g_strdup_printf(
			"Signal successfully sent to job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	case EDIT_CANCEL:
		tmp_char_ptr = g_strdup_printf(
			"Cancel successful for job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	case EDIT_REQUEUE:
		tmp_char_ptr = g_strdup_printf(
			"Requeue successful for job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	case EDIT_SUSPEND:
		tmp_char_ptr = g_strdup_printf(
			"SUSPEND/RESUME action successful for job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	default:
		break;

	}/*end switch*/

end_it:
	xfree(stacked_job_list);

} /*process_foreach_list ^^^*/

static void selected_foreach_build_list(GtkTreeModel  *model,
					GtkTreePath   *path,
					GtkTreeIter   *iter,
					gpointer       userdata)
{
	uint32_t jobid = NO_VAL;
	uint32_t stepid = NO_VAL;
	uint32_t array_job_id = NO_VAL, array_task_id = NO_VAL;
	uint32_t het_job_id = NO_VAL,  het_job_offset = NO_VAL;
	int state;
	jobs_foreach_t *fe_ptr = NULL;
	char *tmp_jobid, *offset, *end_ptr;

	gtk_tree_model_get(model, iter, SORTID_JOBID, &tmp_jobid, -1);

	if (!tmp_jobid)
		return;

	offset = strchr(tmp_jobid, '(');
	if (offset) {
		if (strchr(tmp_jobid, '_')) {
			array_job_id  = strtol(tmp_jobid, &end_ptr, 10);
			array_task_id = strtol(end_ptr+1, NULL, 10);
		} else {
			het_job_id  = strtol(tmp_jobid, &end_ptr, 10);
			het_job_offset = strtol(end_ptr+1, NULL, 10);
		}
		offset++;
	} else
		offset = tmp_jobid;
	jobid = atoi(offset);
	g_free(tmp_jobid);

	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);

	if (stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	gtk_tree_model_get(model, iter, SORTID_STATE_NUM, &state, -1);

	/* alc mem for individual job processor target */
	fe_ptr = xmalloc(sizeof(jobs_foreach_t));
	fe_ptr->step_id.job_id = jobid;
	fe_ptr->step_id.step_id = stepid;
	fe_ptr->step_id.step_het_comp = NO_VAL;
	fe_ptr->state = state;
	fe_ptr->array_job_id = array_job_id;
	fe_ptr->array_task_id = array_task_id;
	fe_ptr->het_job_id = het_job_id;
	fe_ptr->het_job_offset = het_job_offset;

	list_append(foreach_list, fe_ptr); /* stuff target away*/

	if (stacked_job_list)
		xstrcat(stacked_job_list, ", ");
	else
		xstrcat(stacked_job_list, ": ");

	if (array_task_id == NO_VAL && het_job_id == NO_VAL)
		xstrfmtcat(stacked_job_list, "%u", jobid);
	else if (het_job_id != NO_VAL)
		xstrfmtcat(stacked_job_list, "%u+%u", het_job_id,
			   het_job_offset);
	else
		xstrfmtcat(stacked_job_list, "%u_%u",
			   array_job_id, array_task_id);
	if (stepid != SLURM_BATCH_SCRIPT)
		xstrfmtcat(stacked_job_list, ".%u", stepid);
}

static void _edit_each_job(GtkTreeModel *model, GtkTreeIter *iter,
			    jobs_foreach_common_t *jobs_foreach_common)
{
	int response;
	GtkWidget *popup;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	char tmp_char[255];
	char *tmp_char_ptr = "";
	jobs_foreach_t *job_foreach = NULL;
	job_desc_msg_t *job_msg;
	ListIterator itr = NULL;

	itr = list_iterator_create(foreach_list);
	while ((job_foreach = list_next(itr))) {
		/*stop processing remaining jobs on any error*/
		if (global_error_code || got_edit_signal)
			break;

		popup = gtk_dialog_new_with_buttons(
				"Edit Job",
				GTK_WINDOW(main_window),
				GTK_DIALOG_MODAL |
				GTK_DIALOG_DESTROY_WITH_PARENT,
				NULL);
		gtk_window_set_type_hint(GTK_WINDOW(popup),
					 GDK_WINDOW_TYPE_HINT_NORMAL);
		gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup), GTK_STOCK_CANCEL,
				      GTK_RESPONSE_CANCEL);
		gtk_dialog_add_button(GTK_DIALOG(popup), "Cancel all",
				      GTK_RESPONSE_DELETE_EVENT);
		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400);
		snprintf(tmp_char, sizeof(tmp_char),
			 "Editing job %u think before you type",
			 job_foreach->step_id.job_id);
		label = gtk_label_new(tmp_char);

		job_msg = xmalloc(sizeof(job_desc_msg_t));
		slurm_init_job_desc_msg(job_msg);
		job_msg->job_id = job_foreach->step_id.job_id;
		entry = _admin_full_edit_job(job_msg, model, iter);
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   label, false, false, 0);
		if (entry)
			gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
					   entry, true, true, 0);
		gtk_widget_show_all(popup);
		response = gtk_dialog_run(GTK_DIALOG(popup));
		gtk_widget_destroy(popup);

		if (got_edit_signal ||
		    (response == GTK_RESPONSE_DELETE_EVENT)) {
			slurm_free_job_desc_msg(job_msg);
			break;
		}

		if (global_edit_error) {
			tmp_char_ptr = global_edit_error_msg;
		} else if (!global_send_update_msg ||
			   (response == GTK_RESPONSE_CANCEL)) {
			tmp_char_ptr = g_strdup_printf("No change detected.");
		} else if (slurm_update_job(job_msg)
			   == SLURM_SUCCESS) {
			tmp_char_ptr = g_strdup_printf(
				"Job %u updated successfully",
				job_foreach->step_id.job_id);
		} else if (errno == ESLURM_DISABLED) {
			tmp_char_ptr = g_strdup_printf(
				"Can't edit that part of non-pending job %u.",
				job_foreach->step_id.job_id);
		} else {
			tmp_char_ptr = g_strdup_printf(
				"Problem updating job %u.",
				job_foreach->step_id.job_id);
		}
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		slurm_free_job_desc_msg(job_msg);
	} /*spin thru selected jobs*/

	xfree(stacked_job_list);

} /*process_foreach_list ^^^*/

static void _edit_jobs(GtkTreeModel *model, GtkTreeIter *iter,
		       char *type, GtkTreeView *treeview)
{
	jobs_foreach_common_t job_foreach_common;
	global_error_code = SLURM_SUCCESS;
	/* setup working_sview_config that applies to ALL selections */
	memset(&job_foreach_common, 0, sizeof(jobs_foreach_common_t));
	job_foreach_common.type = type;
	job_foreach_common.edit_type = EDIT_EDIT;

	/* create a list to stack the selected jobs */
	foreach_list = list_create(xfree_ptr);
	/* build array of job(s) to process */
	if (treeview) {
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(treeview),
			selected_foreach_build_list, NULL);
	} else
		selected_foreach_build_list(model, NULL, iter, NULL);
	/* determine what to do with them/it */
	_edit_each_job(model, iter, &job_foreach_common); /*go do them*/
	FREE_NULL_LIST(foreach_list);

	return;

}

extern void admin_job(GtkTreeModel *model, GtkTreeIter *iter,
		      char *type, GtkTreeView *treeview)
{
	int jobid = NO_VAL;
	int stepid = NO_VAL;
	int response = 0;
	char tmp_char[255];
	int edit_type = 0;
	job_desc_msg_t *job_msg;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *popup;
	char *tmp_jobid, *offset;

	if (xstrcmp(type, "Edit Job") == 0)
		return _edit_jobs(model, iter, type, treeview);

	popup = gtk_dialog_new_with_buttons(
			type,
			GTK_WINDOW(main_window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			NULL);
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	gtk_tree_model_get(model, iter, SORTID_JOBID, &tmp_jobid, -1);

	if (!tmp_jobid)
		return;

	offset = strchr(tmp_jobid, '(');
	if (offset)
		offset++;
	else
		offset = tmp_jobid;
	jobid = atoi(offset);
	g_free(tmp_jobid);

	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);
	if (stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	job_msg = xmalloc(sizeof(job_desc_msg_t));
	slurm_init_job_desc_msg(job_msg);

	if (!xstrcasecmp("Signal", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		entry = create_entry();
		label = gtk_label_new("Signal?");
		edit_type = EDIT_SIGNAL;
	} else if (!xstrcasecmp("Requeue", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to requeue these job(s)?");
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_REQUEUE;
	} else if (!xstrcasecmp("Cancel", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_NO, GTK_RESPONSE_CANCEL);

		if (stepid != SLURM_BATCH_SCRIPT)
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to cancel "
				 "these job step(s)?");
		else
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to cancel "
				 "these job(s)?");
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_CANCEL;
	} else if (!xstrcasecmp("Suspend/Resume", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		if (stepid != SLURM_BATCH_SCRIPT)
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to toggle "
				 "suspend/resume on these job steps?");
		else
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to toggle "
				 "suspend/resume on these jobs?");
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_SUSPEND;
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, false, false, 0);
	if (entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   entry, true, true, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		jobs_foreach_common_t job_foreach_common;
		global_error_code = SLURM_SUCCESS;
		/* setup working_sview_config that applies to ALL selections */
		memset(&job_foreach_common, 0, sizeof(jobs_foreach_common_t));
		job_foreach_common.type = type;
		job_foreach_common.edit_type = edit_type;
		job_foreach_common.entry = entry;

		/* pass this ptr for single job selections */
		job_foreach_common.job_msg = job_msg;

		/* create a list to stack the selected jobs */
		foreach_list = list_create(xfree_ptr);
		/* build array of job(s) to process */
		if (treeview)
			gtk_tree_selection_selected_foreach(
				gtk_tree_view_get_selection(treeview),
				selected_foreach_build_list, NULL);
		else
			selected_foreach_build_list(model, NULL, iter, NULL);
		/* determine what to do with them/it */
		process_foreach_list(&job_foreach_common); /*go do them*/
		FREE_NULL_LIST(foreach_list);
	}/*response OK ^^*/
	/* switch back to standard cursor*/

	global_entry_changed = 0;
	slurm_free_job_desc_msg(job_msg);
	gtk_widget_destroy(popup);
	if (got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		admin_job(model, iter, type, treeview);
		xfree(type);
	}
	return;
}

extern void cluster_change_job(void)
{
	display_data_t *display_data = display_data_job;
	while (display_data++) {
		if (display_data->id == -1)
			break;

		if (cluster_flags & CLUSTER_FLAG_FED) {
			switch(display_data->id) {
			case SORTID_CLUSTER_NAME:
				display_data->show = true;
				break;
			}
		} else {
			switch(display_data->id) {
			case SORTID_CLUSTER_NAME:
				display_data->show = false;
				break;
			}
		}
	}

	get_info_job(NULL, NULL);
}
