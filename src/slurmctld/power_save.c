/*****************************************************************************\
 *  power_save.c - support node power saving mode. Nodes which have been
 *  idle for an extended period of time will be placed into a power saving
 *  mode by running an arbitrary script. This script can lower the voltage
 *  or frequency of the nodes or can completely power the nodes off.
 *  When the node is restored to normal operation, another script will be
 *  executed. Many parameters are available to control this mode of operation.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#define _GNU_SOURCE

#include <limits.h>	/* For LONG_MIN, LONG_MAX */
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/xstring.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"

#define MAX_SHUTDOWN_DELAY	10	/* seconds to wait for child procs
					 * to exit after daemon shutdown
					 * request, then orphan or kill proc */

/* Records for tracking processes forked to suspend/resume nodes */
typedef struct proc_track_struct {
	pid_t  child_pid;	/* pid of process		*/
	time_t child_time;	/* start time of process	*/
} proc_track_struct_t;
static List proc_track_list = NULL;

pthread_cond_t power_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t power_mutex = PTHREAD_MUTEX_INITIALIZER;
bool power_save_config = false;
bool power_save_enabled = false;
bool power_save_started = false;
bool power_save_debug = false;

int idle_time, suspend_rate, resume_timeout, resume_rate, suspend_timeout;
char *suspend_prog = NULL, *resume_prog = NULL, *resume_fail_prog = NULL;
char *exc_nodes = NULL, *exc_parts = NULL;
time_t last_config = (time_t) 0;
time_t last_log = (time_t) 0, last_work_scan = (time_t) 0;
uint16_t slurmd_timeout;
static bool idle_on_node_suspend = false;

typedef struct exc_node_partital {
	int exc_node_cnt;
	bitstr_t *exc_node_cnt_bitmap;
} exc_node_partital_t;
List partial_node_list;

bitstr_t *exc_node_bitmap = NULL;

bitstr_t *resume_node_bitmap = NULL;
int   suspend_cnt,   resume_cnt;
float suspend_cnt_f, resume_cnt_f;

static void  _clear_power_config(void);
static void  _do_failed_nodes(char *hosts);
static void  _do_power_work(time_t now);
static void  _do_resume(char *host);
static void  _do_suspend(char *host);
static int   _init_power_config(void);
static void *_init_power_save(void *arg);
static int   _kill_procs(void);
static void  _reap_procs(void);
static void  _re_wake(void);
static pid_t _run_prog(char *prog, char *arg1, char *arg2, uint32_t job_id);
static void  _shutdown_power(void);
static bool  _valid_prog(char *file_name);

static void _proc_track_list_del(void *x)
{
	xfree(x);
}

static void _exc_node_part_free(void *x)
{
	exc_node_partital_t *ext_part_struct = (exc_node_partital_t *) x;
	FREE_NULL_BITMAP(ext_part_struct->exc_node_cnt_bitmap);
	xfree(ext_part_struct);
}

static int _parse_exc_nodes(void)
{
	int rc = SLURM_SUCCESS;
	char *end_ptr = NULL, *save_ptr = NULL, *sep, *tmp, *tok;

	sep = strchr(exc_nodes, ':');
	if (!sep)
		return node_name2bitmap(exc_nodes, false, &exc_node_bitmap);

	partial_node_list = list_create(_exc_node_part_free);
	tmp = xstrdup(exc_nodes);
	tok = strtok_r(tmp, ":", &save_ptr);
	while (tok) {
		bitstr_t *exc_node_cnt_bitmap = NULL;
		long ext_node_cnt = 0;
		exc_node_partital_t *ext_part_struct;

		rc = node_name2bitmap(tok, false, &exc_node_cnt_bitmap);
		if ((rc != SLURM_SUCCESS) || !exc_node_cnt_bitmap)
			break;
		tok = strtok_r(NULL, ",", &save_ptr);
		if (tok) {
			ext_node_cnt = strtol(tok, &end_ptr, 10);
			if ((end_ptr[0] != '\0') || (ext_node_cnt < 1) ||
			    (ext_node_cnt >
			     bit_set_count(exc_node_cnt_bitmap))) {
				FREE_NULL_BITMAP(exc_node_cnt_bitmap);
				rc = SLURM_ERROR;
				break;
			}
		} else {
			ext_node_cnt = bit_set_count(exc_node_cnt_bitmap);
		}	
		ext_part_struct = xmalloc(sizeof(exc_node_partital_t));
		ext_part_struct->exc_node_cnt = (int) ext_node_cnt;
		ext_part_struct->exc_node_cnt_bitmap = exc_node_cnt_bitmap;
		list_append(partial_node_list, ext_part_struct);
		tok = strtok_r(NULL, ":", &save_ptr);
	}
	xfree(tmp);
	if (rc != SLURM_SUCCESS)
		FREE_NULL_LIST(partial_node_list);

	return rc;
}

/*
 * Print elements of the excluded nodes with counts
 */
static int _list_part_node_lists(void *x, void *arg)
{
	exc_node_partital_t *ext_part_struct = (exc_node_partital_t *) x;
	char *tmp = bitmap2node_name(ext_part_struct->exc_node_cnt_bitmap);
	info("power_save module, exclude %d nodes from %s",
	     ext_part_struct->exc_node_cnt, tmp);
	xfree(tmp);
	return 0;

}

/*
 * Select the nodes specific nodes to be excluded from consideration for
 * suspension based upon the node states and specified count. Nodes which
 * can not be used (e.g. ALLOCATED, DOWN, DRAINED, etc.).
 */
static int _pick_exc_nodes(void *x, void *arg)
{
	bitstr_t **orig_exc_nodes = (bitstr_t **) arg;
	exc_node_partital_t *ext_part_struct = (exc_node_partital_t *) x;
	bitstr_t *exc_node_cnt_bitmap;
	int i, i_first, i_last;
	int avail_node_cnt, exc_node_cnt;
	struct node_record *node_ptr;


	avail_node_cnt = bit_set_count(ext_part_struct->exc_node_cnt_bitmap);
	if (ext_part_struct->exc_node_cnt >= avail_node_cnt) {
		/* Exclude all nodes in this set */
		exc_node_cnt_bitmap =
			bit_copy(ext_part_struct->exc_node_cnt_bitmap);
	} else {
		i = bit_size(ext_part_struct->exc_node_cnt_bitmap);
		exc_node_cnt_bitmap = bit_alloc(i);
		i_first = bit_ffs(ext_part_struct->exc_node_cnt_bitmap);
		if (i_first >= 0)
			i_last = bit_fls(ext_part_struct->exc_node_cnt_bitmap);
		else
			i_last = i_first - 1;
		exc_node_cnt = ext_part_struct->exc_node_cnt;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(ext_part_struct->exc_node_cnt_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (!IS_NODE_IDLE(node_ptr)			||
			    IS_NODE_COMPLETING(node_ptr)		||
			    IS_NODE_DOWN(node_ptr)			||
			    IS_NODE_DRAIN(node_ptr)			||
			    IS_NODE_POWER_UP(node_ptr)			||
			    IS_NODE_POWER_SAVE(node_ptr)		||
			    (node_ptr->sus_job_cnt > 0))
				continue;
			bit_set(exc_node_cnt_bitmap, i);
			if (--exc_node_cnt <= 0)
				break;
		}
	}

	if (*orig_exc_nodes == NULL) {
		*orig_exc_nodes = exc_node_cnt_bitmap;
	} else {
		bit_or(*orig_exc_nodes, exc_node_cnt_bitmap);
		FREE_NULL_BITMAP(exc_node_cnt_bitmap);
	}

	if (power_save_debug) {
		char *tmp = bitmap2node_name(*orig_exc_nodes);
		info("power_save module, excluded nodes %s", tmp);
		xfree(tmp);
	}

	return 0;
}

/* Perform any power change work to nodes */
static void _do_power_work(time_t now)
{
	int i, wake_cnt = 0, susp_total = 0;
	time_t delta_t;
	uint32_t susp_state;
	bitstr_t *avoid_node_bitmap = NULL, *failed_node_bitmap = NULL;
	bitstr_t *wake_node_bitmap = NULL, *sleep_node_bitmap = NULL;
	struct node_record *node_ptr;

	if (last_work_scan == 0) {
		if (exc_nodes && (_parse_exc_nodes() != SLURM_SUCCESS))
			error("Invalid SuspendExcNodes %s ignored", exc_nodes);

		if (exc_parts) {
			char *tmp = NULL, *one_part = NULL, *part_list = NULL;
			struct part_record *part_ptr = NULL;

			part_list = xstrdup(exc_parts);
			one_part = strtok_r(part_list, ",", &tmp);
			while (one_part != NULL) {
				part_ptr = find_part_record(one_part);
				if (!part_ptr) {
					error("Invalid SuspendExcPart %s ignored",
					      one_part);
				} else if (exc_node_bitmap) {
					bit_or(exc_node_bitmap,
					       part_ptr->node_bitmap);
				} else {
					exc_node_bitmap =
						bit_copy(part_ptr->node_bitmap);
				}
				one_part = strtok_r(NULL, ",", &tmp);
			}
			xfree(part_list);
		}

		if (exc_node_bitmap && power_save_debug) {
			char *tmp = bitmap2node_name(exc_node_bitmap);
			info("power_save module, excluded nodes %s", tmp);
			xfree(tmp);
		}
		if (partial_node_list && power_save_debug) {
			(void) list_for_each(partial_node_list,
					     _list_part_node_lists, NULL);

		}
	}

	/* Set limit on counts of nodes to have state changed */
	delta_t = now - last_work_scan;
	if (delta_t >= 60) {
		suspend_cnt_f = 0.0;
		resume_cnt_f  = 0.0;
	} else {
		float rate = (60 - delta_t) / 60.0;
		suspend_cnt_f *= rate;
		resume_cnt_f  *= rate;
	}
	suspend_cnt = (suspend_cnt_f + 0.5);
	resume_cnt  = (resume_cnt_f  + 0.5);

	last_work_scan = now;

	/* Identify nodes to avoid considering for suspend */
	if (partial_node_list) {
		(void) list_for_each(partial_node_list, _pick_exc_nodes,
				     &avoid_node_bitmap);
	}
	if (exc_node_bitmap) {
		if (avoid_node_bitmap)
			bit_or(avoid_node_bitmap, exc_node_bitmap);
		else
			avoid_node_bitmap = bit_copy(exc_node_bitmap);
	}

	/* Build bitmaps identifying each node which should change state */
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		susp_state = IS_NODE_POWER_SAVE(node_ptr);

		if (susp_state)
			susp_total++;

		/* Resume nodes as appropriate */
		if (susp_state &&
		    ((resume_rate == 0) || (resume_cnt < resume_rate))	&&
		    !IS_NODE_POWERING_DOWN(node_ptr) &&
		    (IS_NODE_ALLOCATED(node_ptr) ||
		     (node_ptr->last_idle > (now - idle_time)))) {
			if (wake_node_bitmap == NULL) {
				wake_node_bitmap =
					bit_alloc(node_record_count);
			}
			wake_cnt++;
			resume_cnt++;
			resume_cnt_f++;
			node_ptr->node_state &= (~NODE_STATE_POWER_SAVE);
			node_ptr->node_state |=   NODE_STATE_POWER_UP;
			node_ptr->node_state |=   NODE_STATE_NO_RESPOND;
			bit_clear(power_node_bitmap, i);
			node_ptr->boot_req_time = now;
			node_ptr->last_response = now + resume_timeout;
			bit_set(booting_node_bitmap, i);
			bit_set(resume_node_bitmap,  i);
			bit_set(wake_node_bitmap,    i);
		}

		/* Suspend nodes as appropriate */
		if ((susp_state == 0)					&&
		    ((suspend_rate == 0) || (suspend_cnt < suspend_rate)) &&
		    (IS_NODE_IDLE(node_ptr) || IS_NODE_DOWN(node_ptr))	&&
		    (node_ptr->sus_job_cnt == 0)			&&
		    (!IS_NODE_COMPLETING(node_ptr))			&&
		    (!IS_NODE_POWER_UP(node_ptr))			&&
		    (node_ptr->last_idle != 0)				&&
		    (node_ptr->last_idle < (now - idle_time))		&&
		    ((avoid_node_bitmap == NULL) ||
		     (bit_test(avoid_node_bitmap, i) == 0))) {
			if (sleep_node_bitmap == NULL) {
				sleep_node_bitmap =
					bit_alloc(node_record_count);
			}
			suspend_cnt++;
			suspend_cnt_f++;
			node_ptr->node_state |= NODE_STATE_POWER_SAVE;
			node_ptr->node_state |= NODE_STATE_POWERING_DOWN;
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
			bit_set(power_node_bitmap,   i);
			bit_set(sleep_node_bitmap,   i);

			/* Don't allocate until after SuspendTimeout */
			bit_clear(avail_node_bitmap, i);
			node_ptr->last_response = now + suspend_timeout;

			if (idle_on_node_suspend) {
				if (IS_NODE_DOWN(node_ptr)) {
					trigger_node_up(node_ptr);
					clusteracct_storage_g_node_up(
						acct_db_conn, node_ptr, now);
				} else if (IS_NODE_IDLE(node_ptr) &&
					   (IS_NODE_DRAIN(node_ptr) ||
					    IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn, node_ptr, now);
				}

				node_ptr->node_state =
					NODE_STATE_IDLE |
					(node_ptr->node_state & NODE_STATE_FLAGS);
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
			}
		}

		if (IS_NODE_POWERING_DOWN(node_ptr) &&
		    (node_ptr->last_response < now)) {

			node_ptr->node_state &= (~NODE_STATE_POWERING_DOWN);

			if (!IS_NODE_DOWN(node_ptr) &&
			    !IS_NODE_DRAIN(node_ptr) &&
			    !IS_NODE_FAIL(node_ptr))
				make_node_avail(i);

			node_ptr->last_idle = 0;
		}

		/*
		 * Down nodes as if not resumed by ResumeTimeout
		 */
		if (bit_test(booting_node_bitmap, i) &&
		    (now > node_ptr->last_response)  &&
		    IS_NODE_POWER_UP(node_ptr) &&
		    IS_NODE_NO_RESPOND(node_ptr)) {
			info("node %s not resumed by ResumeTimeout(%d) - marking down and power_save",
			     node_ptr->name, resume_timeout);
			/*
			 * set_node_down_ptr() will remove the node from the
			 * avail_node_bitmap.
			 */
			set_node_down_ptr(node_ptr, "ResumeTimeout reached");
			node_ptr->node_state &= (~NODE_STATE_POWER_UP);
			node_ptr->node_state |= NODE_STATE_POWER_SAVE;
			bit_set(power_node_bitmap, i);
			bit_clear(booting_node_bitmap, i);
			bit_clear(resume_node_bitmap, i);
			node_ptr->last_idle = 0;

			if (resume_fail_prog) {
				if (!failed_node_bitmap) {
					failed_node_bitmap =
						bit_alloc(node_record_count);
				}
				bit_set(failed_node_bitmap, i);
			}
		}
	}
	FREE_NULL_BITMAP(avoid_node_bitmap);
	if (power_save_debug && ((now - last_log) > 600) && (susp_total > 0)) {
		info("Power save mode: %d nodes", susp_total);
		last_log = now;
	}

	if (sleep_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(sleep_node_bitmap);
		if (nodes)
			_do_suspend(nodes);
		else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		FREE_NULL_BITMAP(sleep_node_bitmap);
		/* last_node_update could be changed already by another thread!
		last_node_update = now; */
	}

	if (wake_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(wake_node_bitmap);
		if (nodes)
			_do_resume(nodes);
		else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		FREE_NULL_BITMAP(wake_node_bitmap);
		/* last_node_update could be changed already by another thread!
		last_node_update = now; */
	}

	if (failed_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(failed_node_bitmap);
		if (nodes)
			_do_failed_nodes(nodes);
		else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		FREE_NULL_BITMAP(failed_node_bitmap);
	}
}

/*
 * power_job_reboot - Reboot compute nodes for a job from the head node.
 * Also change the modes of KNL nodes for node_features/knl_cray plugin.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
extern int power_job_reboot(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	int i, i_first, i_last;
	struct node_record *node_ptr;
	bitstr_t *boot_node_bitmap = NULL, *feature_node_bitmap = NULL;
	time_t now = time(NULL);
	char *nodes, *reboot_features = NULL;
	pid_t pid;

/*
 *	NOTE: See reboot_job_reboot() in job_scheduler.c for similar logic
 *	used by node_features/knl_generic plugin.
 */
	if (job_ptr->reboot)
		boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
	else
		boot_node_bitmap = node_features_reboot(job_ptr);
	if (boot_node_bitmap == NULL) {
		/* At minimum, the powered down nodes require reboot */
		if (bit_overlap(power_node_bitmap, job_ptr->node_bitmap) ||
		    bit_overlap(booting_node_bitmap, job_ptr->node_bitmap)) {
			job_ptr->job_state |= JOB_CONFIGURING;
			job_ptr->bit_flags |= NODE_REBOOT;
		}
		return SLURM_SUCCESS;
	}

	/* Modify state information for all nodes, KNL and others */
	i_first = bit_ffs(boot_node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(boot_node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(boot_node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		resume_cnt++;
		resume_cnt_f++;
		node_ptr->node_state &= (~NODE_STATE_POWER_SAVE);
		node_ptr->node_state |=   NODE_STATE_POWER_UP;
		node_ptr->node_state |=   NODE_STATE_NO_RESPOND;
		bit_clear(power_node_bitmap, i);
		bit_clear(avail_node_bitmap, i);
		node_ptr->boot_req_time = now;
		node_ptr->last_response = now + resume_timeout;
		bit_set(booting_node_bitmap, i);
		bit_set(resume_node_bitmap,  i);
	}

	if (job_ptr->details && job_ptr->details->features &&
	    node_features_g_user_update(job_ptr->user_id)) {
		reboot_features = node_features_g_job_xlate(
					job_ptr->details->features);
		if (reboot_features)
			feature_node_bitmap = node_features_g_get_node_bitmap();
		if (feature_node_bitmap)
			bit_and(feature_node_bitmap, boot_node_bitmap);
		if (!feature_node_bitmap ||
		    (bit_ffs(feature_node_bitmap) == -1)) {
			/* No KNL nodes to reboot */
			FREE_NULL_BITMAP(feature_node_bitmap);
		} else {
			bit_and_not(boot_node_bitmap, feature_node_bitmap);
			if (bit_ffs(boot_node_bitmap) == -1) {
				/* No non-KNL nodes to reboot */
				FREE_NULL_BITMAP(boot_node_bitmap);
			}
		}
	}

	if (feature_node_bitmap) {
		/* Reboot nodes to change KNL NUMA and/or MCDRAM mode */
		nodes = bitmap2node_name(feature_node_bitmap);
		if (nodes) {
			job_ptr->job_state |= JOB_CONFIGURING;
			job_ptr->wait_all_nodes = 1;
			job_ptr->bit_flags |= NODE_REBOOT;
			pid = _run_prog(resume_prog, nodes, reboot_features,
					job_ptr->job_id);
			if (power_save_debug)
				info("%s: pid %d reboot nodes %s features %s",
				     __func__, (int) pid, nodes,
				     reboot_features);
		} else {
			error("%s: bitmap2nodename", __func__);
			rc = SLURM_ERROR;
		}
		xfree(nodes);
		FREE_NULL_BITMAP(feature_node_bitmap);
	}
	if (boot_node_bitmap) {
		/* Reboot nodes with no feature changes */
		nodes = bitmap2node_name(boot_node_bitmap);
		if (nodes) {
			job_ptr->job_state |= JOB_CONFIGURING;
			job_ptr->wait_all_nodes = 1;
			job_ptr->bit_flags |= NODE_REBOOT;
			pid = _run_prog(resume_prog, nodes, NULL,
					job_ptr->job_id);
			if (power_save_debug)
				info("%s: pid %d reboot nodes %s",
				     __func__, (int) pid, nodes);
		} else {
			error("%s: bitmap2nodename", __func__);
			rc = SLURM_ERROR;
		}
		xfree(nodes);
	}
	FREE_NULL_BITMAP(boot_node_bitmap);
	xfree(reboot_features);

	last_node_update = now;

	return rc;
}

/* If slurmctld crashes, the node state that it recovers could differ
 * from the actual hardware state (e.g. ResumeProgram failed to complete).
 * To address that, when a node that should be powered up for a running
 * job is not responding, they try running ResumeProgram again. */
static void _re_wake(void)
{
	struct node_record *node_ptr;
	bitstr_t *wake_node_bitmap = NULL;
	int i;

	node_ptr = node_record_table_ptr;
	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (IS_NODE_ALLOCATED(node_ptr)   &&
		    IS_NODE_NO_RESPOND(node_ptr)  &&
		    !IS_NODE_POWER_SAVE(node_ptr) &&
		    (bit_test(resume_node_bitmap,  i) == 0)) {
			if (wake_node_bitmap == NULL) {
				wake_node_bitmap =
					bit_alloc(node_record_count);
			}
			bit_set(wake_node_bitmap, i);
		}
	}

	if (wake_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(wake_node_bitmap);
		if (nodes) {
			pid_t pid = _run_prog(resume_prog, nodes, NULL, 0);
			if (power_save_debug)
				info("power_save: pid %d rewaking nodes %s",
				     (int) pid, nodes);
		} else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		FREE_NULL_BITMAP(wake_node_bitmap);
	}
}

static void _do_failed_nodes(char *hosts)
{
	pid_t pid = _run_prog(resume_fail_prog, hosts, NULL, 0);
	if (power_save_debug)
		info("power_save: pid %d handle failed nodes %s",
		     (int)pid, hosts);
}

static void _do_resume(char *host)
{
	pid_t pid = _run_prog(resume_prog, host, NULL, 0);
	if (power_save_debug)
		info("power_save: pid %d waking nodes %s",
		     (int) pid, host);
}

static void _do_suspend(char *host)
{
	pid_t pid = _run_prog(suspend_prog, host, NULL, 0);
	if (power_save_debug)
		info("power_save: pid %d suspending nodes %s",
		     (int) pid, host);
}

/* run a suspend or resume program
 * prog IN	- program to run
 * arg1 IN	- first program argument, the hostlist expression
 * arg2 IN	- second program argumentor NULL
 * job_id IN	- Passed as SLURM_JOB_ID environment variable
 */
static pid_t _run_prog(char *prog, char *arg1, char *arg2, uint32_t job_id)
{
	int i;
	char *argv[4], job_id_str[32], *pname;
	pid_t child;

	if (prog == NULL)	/* disabled, useful for testing */
		return -1;

	if (job_id)
		snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);
	pname = strrchr(prog, '/');
	if (pname == NULL)
		argv[0] = prog;
	else
		argv[0] = pname + 1;
	argv[1] = arg1;
	argv[2] = arg2;
	argv[3] = NULL;

	child = fork();
	if (child == 0) {
		for (i = 0; i < 1024; i++)
			(void) close(i);
		setpgid(0, 0);
		setenv("SLURM_CONF", slurmctld_conf.slurm_conf, 1);
		if (job_id)
			setenv("SLURM_JOB_ID", job_id_str, 1);
		execv(prog, argv);
		exit(1);
	} else if (child < 0) {
		error("fork: %m");
	} else {
		/* save the pid */
		proc_track_struct_t *proc_track;
		proc_track = xmalloc(sizeof(proc_track_struct_t));
		proc_track->child_pid = child;
		proc_track->child_time = time(NULL);
		list_append(proc_track_list, proc_track);
	}
	return child;
}

/* reap child processes previously forked to modify node state. */
static void _reap_procs(void)
{
	int delay, max_timeout, rc, status;
	ListIterator iter;
	proc_track_struct_t *proc_track;

	max_timeout = MAX(suspend_timeout, resume_timeout);
	iter = list_iterator_create(proc_track_list);
	while ((proc_track = list_next(iter))) {
		rc = waitpid(proc_track->child_pid, &status, WNOHANG);
		if (rc == 0)
			continue;

		delay = difftime(time(NULL), proc_track->child_time);
		if (power_save_debug && (delay > max_timeout)) {
			info("power_save: program %d ran for %d sec",
			     (int) proc_track->child_pid, delay);
		}

		if (WIFEXITED(status)) {
			rc = WEXITSTATUS(status);
			if (rc != 0) {
				error("power_save: program exit status of %d",
				      rc);
			} else
				ping_nodes_now = true;
		} else if (WIFSIGNALED(status)) {
			error("power_save: program signaled: %s",
			      strsignal(WTERMSIG(status)));
		}

		list_delete_item(iter);
	}
	list_iterator_destroy(iter);
}

/* kill (or orphan) child processes previously forked to modify node state.
 * return the count of killed/orphaned processes */
static int  _kill_procs(void)
{
	int killed = 0, rc, status;
	ListIterator iter;
	proc_track_struct_t *proc_track;

	iter = list_iterator_create(proc_track_list);
	while ((proc_track = list_next(iter))) {
		rc = waitpid(proc_track->child_pid, &status, WNOHANG);
		if (rc == 0) {
#ifdef  POWER_SAVE_KILL_PROCS
			error("power_save: killing process %d",
			      proc_track->child_pid);
			kill((0 - proc_track->child_pid), SIGKILL);
#else
			error("power_save: orphaning process %d",
			      proc_track->child_pid);
#endif
			killed++;
		} else {
			/* process already completed */
		}
		list_delete_item(iter);
	}
	list_iterator_destroy(iter);

	return killed;
}

/* shutdown power save daemons */
static void _shutdown_power(void)
{
	int i, proc_cnt, max_timeout;

	max_timeout = MAX(suspend_timeout, resume_timeout);
	max_timeout = MIN(max_timeout, MAX_SHUTDOWN_DELAY);
	/* Try to avoid orphan processes */
	for (i = 0; ; i++) {
		_reap_procs();
		proc_cnt = list_count(proc_track_list);
		if (proc_cnt == 0)	/* all procs completed */
			break;
		if (i >= max_timeout) {
			error("power_save: orphaning %d processes which are "
			      "not terminating so slurmctld can exit",
			      proc_cnt);
			_kill_procs();
			break;
		} else if (i == 2) {
			info("power_save: waiting for %d processes to complete",
			     proc_cnt);
		} else if (i % 5 == 0) {
			debug("power_save: waiting for %d processes to complete",
			      proc_cnt);
		}
		sleep(1);
	}
}

/* Free all allocated memory */
static void _clear_power_config(void)
{
	xfree(suspend_prog);
	xfree(resume_prog);
	xfree(exc_nodes);
	xfree(exc_parts);
	FREE_NULL_BITMAP(exc_node_bitmap);
	FREE_NULL_LIST(partial_node_list);
}

/*
 * Initialize power_save module parameters.
 * Return 0 on valid configuration to run power saving,
 * otherwise log the problem and return -1
 */
static int _init_power_config(void)
{
	last_config     = slurmctld_conf.last_update;
	last_work_scan  = 0;
	last_log	= 0;
	idle_time       = slurmctld_conf.suspend_time - 1;
	suspend_rate    = slurmctld_conf.suspend_rate;
	resume_timeout  = slurmctld_conf.resume_timeout;
	resume_rate     = slurmctld_conf.resume_rate;
	slurmd_timeout  = slurmctld_conf.slurmd_timeout;
	suspend_timeout = slurmctld_conf.suspend_timeout;
	_clear_power_config();
	if (slurmctld_conf.suspend_program)
		suspend_prog = xstrdup(slurmctld_conf.suspend_program);
	if (slurmctld_conf.resume_fail_program)
		resume_fail_prog = xstrdup(slurmctld_conf.resume_fail_program);
	if (slurmctld_conf.resume_program)
		resume_prog = xstrdup(slurmctld_conf.resume_program);
	if (slurmctld_conf.suspend_exc_nodes)
		exc_nodes = xstrdup(slurmctld_conf.suspend_exc_nodes);
	if (slurmctld_conf.suspend_exc_parts)
		exc_parts = xstrdup(slurmctld_conf.suspend_exc_parts);

	idle_on_node_suspend = xstrcasestr(slurmctld_conf.slurmctld_params,
					   "idle_on_node_suspend");

	if (idle_time < 0) {	/* not an error */
		debug("power_save module disabled, SuspendTime < 0");
		return -1;
	}
	if (suspend_rate < 0) {
		error("power_save module disabled, SuspendRate < 0");
		test_config_rc = 1;
		return -1;
	}
	if (resume_rate < 0) {
		error("power_save module disabled, ResumeRate < 0");
		test_config_rc = 1;
		return -1;
	}
	if (suspend_prog == NULL) {
		error("power_save module disabled, NULL SuspendProgram");
		test_config_rc = 1;
		return -1;
	} else if (!_valid_prog(suspend_prog)) {
		error("power_save module disabled, invalid SuspendProgram %s",
		      suspend_prog);
		test_config_rc = 1;
		return -1;
	}
	if (resume_prog == NULL) {
		error("power_save module disabled, NULL ResumeProgram");
		test_config_rc = 1;
		return -1;
	} else if (!_valid_prog(resume_prog)) {
		error("power_save module disabled, invalid ResumeProgram %s",
		      resume_prog);
		test_config_rc = 1;
		return -1;
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_POWER_SAVE)
		power_save_debug = true;
	else
		power_save_debug = false;

	if (resume_fail_prog && !_valid_prog(resume_fail_prog)) {
		/* error's already reported in _valid_prog() */
		xfree(resume_fail_prog);
	}

	return 0;
}

static bool _valid_prog(char *file_name)
{
	struct stat buf;

	if (file_name[0] != '/') {
		error("power_save program %s not absolute pathname", file_name);
		return false;
	}

	if (access(file_name, X_OK) != 0) {
		error("power_save program %s not executable", file_name);
		return false;
	}

	if (stat(file_name, &buf)) {
		error("power_save program %s not found", file_name);
		return false;
	}
	if (buf.st_mode & 022) {
		error("power_save program %s has group or "
		      "world write permission", file_name);
		return false;
	}

	return true;
}

/*
 * config_power_mgr - Read power management configuration
 */
extern void config_power_mgr(void)
{
	slurm_mutex_lock(&power_mutex);
	if (!power_save_config) {
		if (_init_power_config() == 0)
			power_save_enabled = true;
		power_save_config = true;
	}
	slurm_cond_signal(&power_cond);
	slurm_mutex_unlock(&power_mutex);
}

/*
 * start_power_mgr - Start power management thread as needed. The thread
 *	terminates automatically at slurmctld shutdown time.
 * IN thread_id - pointer to thread ID of the started pthread.
 */
extern void start_power_mgr(pthread_t *thread_id)
{
	slurm_mutex_lock(&power_mutex);
	if (power_save_started) {     /* Already running */
		slurm_mutex_unlock(&power_mutex);
		return;
	}
	power_save_started = true;
	proc_track_list = list_create(_proc_track_list_del);
	slurm_mutex_unlock(&power_mutex);

	slurm_thread_create(thread_id, _init_power_save, NULL);
}

/* Report if node power saving is enabled */
extern bool power_save_test(void)
{
	bool rc;

	slurm_mutex_lock(&power_mutex);
	while (!power_save_config) {
		slurm_cond_wait(&power_cond, &power_mutex);
	}
	rc = power_save_enabled;
	slurm_mutex_unlock(&power_mutex);

	return rc;
}

/* Free module's allocated memory */
extern void power_save_fini(void)
{
	slurm_mutex_lock(&power_mutex);
	if (power_save_started) {     /* Already running */
		power_save_started = false;
		FREE_NULL_LIST(proc_track_list);
	}
	slurm_mutex_unlock(&power_mutex);
}

/*
 * init_power_save - Initialize the power save module. Started as a
 *	pthread. Terminates automatically at slurmctld shutdown time.
 *	Input and output are unused.
 */
static void *_init_power_save(void *arg)
{
        /* Locks: Read nodes */
        slurmctld_lock_t node_read_lock = {
                NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
        /* Locks: Write nodes */
        slurmctld_lock_t node_write_lock = {
                NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	time_t now, boot_time = 0, last_power_scan = 0;

	if (power_save_config && !power_save_enabled) {
		debug("power_save mode not enabled");
		goto fini;
	}

	resume_node_bitmap  = bit_alloc(node_record_count);

	while (slurmctld_config.shutdown_time == 0) {
		sleep(1);

		_reap_procs();

		if ((last_config != slurmctld_conf.last_update) &&
		    (_init_power_config())) {
			info("power_save mode has been disabled due to "
			     "configuration changes");
			goto fini;
		}

		now = time(NULL);
		if (boot_time == 0)
			boot_time = now;

		/*
		 * Only run every 10 seconds or after a node state change,
		 * whichever happens first
		 */
		if ((last_node_update >= last_power_scan) ||
		    (now >= (last_power_scan + 10))) {
			lock_slurmctld(node_write_lock);
			_do_power_work(now);
			unlock_slurmctld(node_write_lock);
			last_power_scan = now;
		}

		if (slurmd_timeout &&
		    (now > (boot_time + (slurmd_timeout / 2)))) {
			lock_slurmctld(node_read_lock);
			_re_wake();
			unlock_slurmctld(node_read_lock);
			/* prevent additional executions */
			boot_time += (365 * 24 * 60 * 60);
			slurmd_timeout = 0;
		}
	}

fini:	_clear_power_config();
	FREE_NULL_BITMAP(resume_node_bitmap);
	_shutdown_power();
	slurm_mutex_lock(&power_mutex);
	list_destroy(proc_track_list);
	proc_track_list = NULL;
	power_save_enabled = false;
	power_save_started = false;
	slurm_cond_signal(&power_cond);
	slurm_mutex_unlock(&power_mutex);
	pthread_exit(NULL);
	return NULL;
}
