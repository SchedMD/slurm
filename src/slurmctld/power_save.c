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

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

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
#include "src/common/data.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/serializer.h"

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/trigger_mgr.h"

pthread_cond_t power_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t power_mutex = PTHREAD_MUTEX_INITIALIZER;
bool power_save_config = false;
bool power_save_enabled = false;
bool power_save_started = false;
bool power_save_debug = false;

int suspend_rate, resume_rate, max_timeout;
char *suspend_prog = NULL, *resume_prog = NULL, *resume_fail_prog = NULL;
time_t last_log = (time_t) 0, last_work_scan = (time_t) 0;
uint16_t slurmd_timeout;
static bool idle_on_node_suspend = false;
static uint16_t power_save_interval = 10;
static uint16_t power_save_min_interval = 0;

bool cloud_reg_addrs = false;
List resume_job_list = NULL;

typedef struct exc_node_partital {
	int exc_node_cnt;
	bitstr_t *exc_node_cnt_bitmap;
} exc_node_partital_t;
List partial_node_list;

bitstr_t *exc_node_bitmap = NULL;

/* Possible SuspendExcStates */
static bool suspend_exc_down;
static uint32_t suspend_exc_state_flags;

int   suspend_cnt,   resume_cnt;
float suspend_cnt_f, resume_cnt_f;

static void  _clear_power_config(void);
static void  _do_failed_nodes(char *hosts);
static void  _do_power_work(time_t now);
static void  _do_resume(char *host, char *json);
static void  _do_suspend(char *host);
static int   _init_power_config(void);
static void *_init_power_save(void *arg);
static bool  _valid_prog(char *file_name);

static void _exc_node_part_free(void *x)
{
	exc_node_partital_t *ext_part_struct = (exc_node_partital_t *) x;
	FREE_NULL_BITMAP(ext_part_struct->exc_node_cnt_bitmap);
	xfree(ext_part_struct);
}

static int _parse_exc_nodes(void)
{
	int rc = SLURM_SUCCESS;
	char *save_ptr = NULL, *sep, *tmp, *tok, *node_cnt_str;

	/* Shortcut if ":<node_cnt>" is not used */
	sep = strchr(slurm_conf.suspend_exc_nodes, ':');
	if (!sep)
		return node_name2bitmap(slurm_conf.suspend_exc_nodes, false,
					&exc_node_bitmap);

	FREE_NULL_LIST(partial_node_list);
	partial_node_list = list_create(_exc_node_part_free);
	tmp = xstrdup(slurm_conf.suspend_exc_nodes);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		bitstr_t *exc_node_cnt_bitmap = NULL;
		long ext_node_cnt = 0;
		exc_node_partital_t *ext_part_struct;

		if ((node_cnt_str = xstrstr(tok, ":"))) {
			*node_cnt_str = '\0';
			ext_node_cnt = strtol(node_cnt_str + 1, NULL, 10);
		}
		rc = node_name2bitmap(tok, false, &exc_node_cnt_bitmap);

		if (!ext_node_cnt) {
			ext_node_cnt = bit_set_count(exc_node_cnt_bitmap);
		}
		if (bit_set_count(exc_node_cnt_bitmap)) {
			ext_part_struct = xmalloc(sizeof(exc_node_partital_t));
			ext_part_struct->exc_node_cnt = (int) ext_node_cnt;
			ext_part_struct->exc_node_cnt_bitmap =
				exc_node_cnt_bitmap;
			list_append(partial_node_list, ext_part_struct);
		} else
			FREE_NULL_BITMAP(exc_node_cnt_bitmap);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
	if (list_is_empty(partial_node_list))
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
	log_flag(POWER, "exclude %d nodes from %s",
		 ext_part_struct->exc_node_cnt, tmp);
	xfree(tmp);
	return 0;

}

static void _parse_exc_states(void)
{
	char *buf, *tok, *saveptr;
	/* Flags in _node_state_suspendable() are already excluded */
	uint32_t excludable_state_flags = NODE_STATE_CLOUD |
					  NODE_STATE_DRAIN |
					  NODE_STATE_DYNAMIC_FUTURE |
					  NODE_STATE_DYNAMIC_NORM |
					  NODE_STATE_FAIL |
					  NODE_STATE_INVALID_REG |
					  NODE_STATE_MAINT |
					  NODE_STATE_NET |
					  NODE_STATE_NO_RESPOND |
					  NODE_STATE_PLANNED |
					  NODE_STATE_RES;

	buf = xstrdup(slurm_conf.suspend_exc_states);
	for (tok = strtok_r(buf, ",", &saveptr); tok;
	     tok = strtok_r(NULL, ",", &saveptr)) {
		uint32_t flag = 0;

		/* Base node states */
		if (!xstrncasecmp(tok, "DOWN", MAX(strlen(tok), 2))){
			suspend_exc_down = true;
			continue;
		}

		/* Flag node states */
		flag = parse_node_state_flag(tok);
		if (flag & excludable_state_flags) {
			suspend_exc_state_flags |= flag;
			continue;
		}

		error("Invalid SuspendExcState %s", tok);
	}
	xfree(buf);

	if (power_save_debug) {
		char *exc_states_str =
			node_state_string_complete(suspend_exc_state_flags);
		log_flag(POWER, "suspend_exc_down=%d suspend_exc_state_flags=%s",
			 suspend_exc_down, exc_states_str);
		xfree(exc_states_str);
	}
}

/*
 * Is it possible to suspend this node
 */
static bool _node_state_suspendable(node_record_t *node_ptr)
{
	/* Must have idle or down base state */
	if (!IS_NODE_IDLE(node_ptr) && !IS_NODE_DOWN(node_ptr))
		return false;

	/* Must not have these flags */
	if (IS_NODE_COMPLETING(node_ptr) ||
	    IS_NODE_POWERING_UP(node_ptr) ||
	    IS_NODE_POWERED_DOWN(node_ptr) ||
	    IS_NODE_POWERING_DOWN(node_ptr) ||
	    IS_NODE_REBOOT_ISSUED(node_ptr) ||
	    IS_NODE_REBOOT_REQUESTED(node_ptr))
	    return false;

	return true;
}

/*
 * Should this node be suspended after SuspendTime has elapsed
 */
static bool _node_state_should_suspend(node_record_t *node_ptr)
{
	/* SuspendExcStates */
	if (suspend_exc_down && IS_NODE_DOWN(node_ptr))
		return false;
	if (suspend_exc_state_flags & node_ptr->node_state)
		return false;

	return true;
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
	int avail_node_cnt, exc_node_cnt;
	node_record_t *node_ptr;

	avail_node_cnt = bit_set_count(ext_part_struct->exc_node_cnt_bitmap);
	if (ext_part_struct->exc_node_cnt >= avail_node_cnt) {
		/* Exclude all nodes in this set */
		exc_node_cnt_bitmap =
			bit_copy(ext_part_struct->exc_node_cnt_bitmap);
	} else {
		exc_node_cnt_bitmap = bit_alloc(
			bit_size(ext_part_struct->exc_node_cnt_bitmap));
		exc_node_cnt = ext_part_struct->exc_node_cnt;
		for (int i = 0;
		     (node_ptr =
		      next_node_bitmap(ext_part_struct->exc_node_cnt_bitmap,
				       &i));
		     i++) {
			if (!_node_state_suspendable(node_ptr)		||
			    IS_NODE_DOWN(node_ptr)			||
			    IS_NODE_DRAIN(node_ptr)			||
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

	return 0;
}

/* Perform any power change work to nodes */
static void _do_power_work(time_t now)
{
	int i, susp_total = 0;
	time_t delta_t;
	uint32_t susp_state;
	bitstr_t *avoid_node_bitmap = NULL, *failed_node_bitmap = NULL;
	bitstr_t *wake_node_bitmap = NULL, *sleep_node_bitmap = NULL;
	node_record_t *node_ptr;
	data_t *resume_json_data = NULL;
	data_t *jobs_data = NULL;
	ListIterator iter;
	bitstr_t *job_power_node_bitmap;
	uint32_t *job_id_ptr;
	bool nodes_updated = false;

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

	if (avoid_node_bitmap && power_save_debug &&
	    (get_log_level() >= LOG_LEVEL_DEBUG)) {
		char *tmp = bitmap2node_name(avoid_node_bitmap);
		debug("avoid nodes %s", tmp);
		xfree(tmp);
	}

	/*
	 * Buid job to node mapping for json output
	 * all_nodes = all nodes that need to be resumed this iteration
	 * jobs[] - list of job to node mapping of nodes that the job needs to
	 * be resumed for job. Multiple jobs can request the same nodes. Report
	 * all jobs to node mapping for this iteration.
	 * e.g.
	 * {
	 * all_nodes: n[1-3]
	 * jobs: [{job_id:123, nodes:n[1-3]}, {job_id:124, nodes:n[1-3]}]
	 * }
	 */
	resume_json_data = data_set_dict(data_new());
	jobs_data = data_set_list(data_key_set(resume_json_data, "jobs"));

	job_power_node_bitmap = bit_alloc(node_record_count);

	iter = list_iterator_create(resume_job_list);
	while ((job_id_ptr = list_next(iter))) {
		char *nodes;
		job_record_t *job_ptr;
		data_t *job_node_data;
		bitstr_t *need_resume_bitmap, *to_resume_bitmap;

		if ((resume_rate > 0) && (resume_cnt >= resume_rate)) {
			log_flag(POWER, "resume rate reached");
			break;
		}

		if (!(job_ptr = find_job_record(*job_id_ptr))) {
			log_flag(POWER, "%pJ needed resuming but is gone now",
				 job_ptr);
			list_delete_item(iter);
			continue;
		}
		if (!IS_JOB_CONFIGURING(job_ptr)) {
			log_flag(POWER, "%pJ needed resuming but isn't configuring anymore",
				 job_ptr);
			list_delete_item(iter);
			continue;
		}
		if (!bit_overlap_any(job_ptr->node_bitmap, power_node_bitmap)) {
			log_flag(POWER, "%pJ needed resuming but nodes aren't power_save anymore",
				 job_ptr);
			list_delete_item(iter);
			continue;
		}

		to_resume_bitmap = bit_alloc(node_record_count);

		need_resume_bitmap = bit_copy(job_ptr->node_bitmap);
		bit_and(need_resume_bitmap, power_node_bitmap);

		for (int i = 0; next_node_bitmap(need_resume_bitmap, &i); i++) {
			if ((resume_rate == 0) || (resume_cnt < resume_rate)) {
				resume_cnt++;
				resume_cnt_f++;

				bit_set(job_power_node_bitmap, i);
				bit_set(to_resume_bitmap, i);
				bit_clear(need_resume_bitmap, i);
			}
		}

		job_node_data = data_set_dict(data_list_append(jobs_data));
		data_set_string(data_key_set(job_node_data, "extra"),
				job_ptr->extra);
		data_set_int(data_key_set(job_node_data, "job_id"),
			     job_ptr->job_id);
		data_set_string(data_key_set(job_node_data, "features"),
				job_ptr->details->features_use);
		data_set_string_own(data_key_set(job_node_data, "nodes_alloc"),
				    bitmap2node_name(job_ptr->node_bitmap));
		nodes = bitmap2node_name(to_resume_bitmap);
		data_set_string_own(data_key_set(job_node_data, "nodes_resume"),
				    nodes);
		data_set_string(data_key_set(job_node_data, "oversubscribe"),
				job_share_string(get_job_share_value(job_ptr)));
		data_set_string(data_key_set(job_node_data, "partition"),
				job_ptr->part_ptr->name);
		data_set_string(data_key_set(job_node_data, "reservation"),
				job_ptr->resv_name);

		/* No more nodes to power up, remove job from list */
		if (!bit_set_count(need_resume_bitmap)) {
			log_flag(POWER, "no more nodes to resume for job %pJ",
				 job_ptr);
			list_delete_item(iter);
		} else if (power_save_debug) {
			char *still_needed_nodes =
				bitmap2node_name(need_resume_bitmap);
			log_flag(POWER, "%s still left to boot for %pJ",
				 still_needed_nodes, job_ptr);
			xfree(still_needed_nodes);
		}

		FREE_NULL_BITMAP(need_resume_bitmap);
		FREE_NULL_BITMAP(to_resume_bitmap);
	}

	/* Build bitmaps identifying each node which should change state */
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		susp_state = IS_NODE_POWERED_DOWN(node_ptr);

		if (susp_state)
			susp_total++;

		/* Resume nodes as appropriate */
		if ((bit_test(job_power_node_bitmap, node_ptr->index)) ||
		    (susp_state &&
		    ((resume_rate == 0) || (resume_cnt < resume_rate))	&&
		    !IS_NODE_POWERING_DOWN(node_ptr) &&
		    IS_NODE_POWER_UP(node_ptr))) {
			if (wake_node_bitmap == NULL) {
				wake_node_bitmap =
					bit_alloc(node_record_count);
			}
			if (!(bit_test(job_power_node_bitmap,
				       node_ptr->index))) {
				resume_cnt++;
				resume_cnt_f++;
			}
			node_ptr->node_state &= (~NODE_STATE_POWER_UP);
			node_ptr->node_state &= (~NODE_STATE_POWERED_DOWN);
			node_ptr->node_state |=   NODE_STATE_POWERING_UP;
			node_ptr->node_state |=   NODE_STATE_NO_RESPOND;
			bit_clear(power_node_bitmap, node_ptr->index);
			node_ptr->boot_req_time = now;
			bit_set(booting_node_bitmap, node_ptr->index);
			bit_set(wake_node_bitmap,    node_ptr->index);

			bit_clear(job_power_node_bitmap, node_ptr->index);

			clusteracct_storage_g_node_up(acct_db_conn, node_ptr,
						      now);
			nodes_updated = true;
		}

		/* Suspend nodes as appropriate */
		if (_node_state_suspendable(node_ptr) &&
		    ((suspend_rate == 0) || (suspend_cnt < suspend_rate)) &&
		    (node_ptr->sus_job_cnt == 0)			&&
		    (IS_NODE_POWER_DOWN(node_ptr) ||
		     ((node_ptr->last_busy != 0) &&
		      (node_ptr->last_busy < (now - node_ptr->suspend_time)) &&
		      _node_state_should_suspend(node_ptr) &&
		      ((avoid_node_bitmap == NULL) ||
		       (bit_test(avoid_node_bitmap, node_ptr->index) == 0))))) {
			if (sleep_node_bitmap == NULL) {
				sleep_node_bitmap =
					bit_alloc(node_record_count);
			}

			/* Clear power_down_asap */
			if (IS_NODE_POWER_DOWN(node_ptr) &&
			    IS_NODE_DRAIN(node_ptr)) {
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
			}

			suspend_cnt++;
			suspend_cnt_f++;
			node_ptr->node_state |= NODE_STATE_POWERING_DOWN;
			node_ptr->node_state &= (~NODE_STATE_POWER_DOWN);
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
			bit_set(power_node_bitmap,   node_ptr->index);
			bit_set(sleep_node_bitmap,   node_ptr->index);

			/* Don't allocate until after SuspendTimeout */
			bit_clear(avail_node_bitmap, node_ptr->index);
			node_ptr->power_save_req_time = now;

			if (idle_on_node_suspend) {
				if (IS_NODE_DOWN(node_ptr)) {
					trigger_node_up(node_ptr);
				}

				node_ptr->node_state =
					NODE_STATE_IDLE |
					(node_ptr->node_state & NODE_STATE_FLAGS);
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
			}
			nodes_updated = true;
		}

		if (IS_NODE_POWERING_DOWN(node_ptr) &&
		    ((node_ptr->power_save_req_time + node_ptr->suspend_timeout)
		     < now)) {
			node_ptr->node_state &= (~NODE_STATE_INVALID_REG);
			node_ptr->node_state &= (~NODE_STATE_POWERING_DOWN);
			node_ptr->node_state |= NODE_STATE_POWERED_DOWN;

			if (IS_NODE_CLOUD(node_ptr) && cloud_reg_addrs) {
				/* Reset hostname and addr to node's name. */
				set_node_comm_name(node_ptr, NULL,
						   node_ptr->name);
			}

			if (!IS_NODE_DOWN(node_ptr) &&
			    !IS_NODE_DRAIN(node_ptr) &&
			    !IS_NODE_FAIL(node_ptr))
				make_node_avail(node_ptr);

			node_ptr->last_busy = 0;
			node_ptr->power_save_req_time = 0;

			reset_node_active_features(node_ptr);

			clusteracct_storage_g_node_down(
				acct_db_conn, node_ptr, now,
				"Powered down after SuspendTimeout",
				node_ptr->reason_uid);
			nodes_updated = true;
		}

		/*
		 * Down nodes as if not resumed by ResumeTimeout
		 */
		if (bit_test(booting_node_bitmap, node_ptr->index) &&
		    (now >
		     (node_ptr->boot_req_time + node_ptr->resume_timeout)) &&
		    IS_NODE_POWERING_UP(node_ptr) &&
		    IS_NODE_NO_RESPOND(node_ptr)) {
			info("node %s not resumed by ResumeTimeout(%d) - marking down and power_save",
			     node_ptr->name, node_ptr->resume_timeout);
			node_ptr->node_state &= (~NODE_STATE_DRAIN);
			node_ptr->node_state &= (~NODE_STATE_POWER_DOWN);
			node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
			node_ptr->node_state |= NODE_STATE_POWERED_DOWN;

			reset_node_active_features(node_ptr);

			/*
			 * set_node_down_ptr() will remove the node from the
			 * avail_node_bitmap.
			 *
			 * Call AFTER setting state adding POWERED_DOWN so that
			 * the node is marked as "planned down" in the usage
			 * tables becase:
			 * set_node_down_ptr()->_make_node_down()->
			 * clusteracct_storage_g_node_down().
			 */
			set_node_down_ptr(node_ptr, "ResumeTimeout reached");
			bit_set(power_node_bitmap, node_ptr->index);
			bit_clear(booting_node_bitmap, node_ptr->index);
			node_ptr->last_busy = 0;
			node_ptr->boot_req_time = 0;

			if (resume_fail_prog) {
				if (!failed_node_bitmap) {
					failed_node_bitmap =
						bit_alloc(node_record_count);
				}
				bit_set(failed_node_bitmap, node_ptr->index);
			}
			nodes_updated = true;
		}
	}
	FREE_NULL_BITMAP(avoid_node_bitmap);
	if (power_save_debug && ((now - last_log) > 600) && (susp_total > 0)) {
		log_flag(POWER, "Power save mode: %d nodes", susp_total);
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
		nodes_updated = true;
	}

	if (wake_node_bitmap) {
		char *nodes, *json = NULL;
		nodes = bitmap2node_name(wake_node_bitmap);

		data_set_string(data_key_set(resume_json_data,
					     "all_nodes_resume"),
				nodes);
		if (serialize_g_data_to_string(&json, NULL, resume_json_data,
					       MIME_TYPE_JSON,
					       SER_FLAGS_COMPACT))
			error("failed to generate json for resume job/node list");

		if (nodes)
			_do_resume(nodes, json);
		else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		xfree(json);
		FREE_NULL_BITMAP(wake_node_bitmap);
		nodes_updated = true;
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
		nodes_updated = true;
	}

	if (nodes_updated)
		last_node_update = time(NULL);

	FREE_NULL_DATA(resume_json_data);
	FREE_NULL_BITMAP(job_power_node_bitmap);
}

extern int power_job_reboot(bitstr_t *node_bitmap, job_record_t *job_ptr,
			    char *features)
{
	int rc = SLURM_SUCCESS;
	char *nodes;

	nodes = bitmap2node_name(node_bitmap);
	if (nodes) {
		slurmscriptd_run_power(resume_prog, nodes, features,
				       job_ptr->job_id, "resumeprog_reboot",
				       max_timeout, NULL, NULL);
		log_flag(POWER, "%s: reboot nodes %s features %s",
			 __func__, nodes, features);
	} else {
		error("%s: bitmap2nodename", __func__);
		rc = SLURM_ERROR;
	}
	xfree(nodes);

	return rc;
}

static void _do_failed_nodes(char *hosts)
{
	slurmscriptd_run_power(resume_fail_prog, hosts, NULL, 0,
			       "resumefailprog", max_timeout, NULL, NULL);
	log_flag(POWER, "power_save: handle failed nodes %s", hosts);
}

static void _do_resume(char *host, char *json)
{
	slurmscriptd_run_power(resume_prog, host, NULL, 0, "resumeprog",
			       max_timeout, "SLURM_RESUME_FILE", json);
	log_flag(POWER, "power_save: waking nodes %s", host);
}

static void _do_suspend(char *host)
{
	slurmscriptd_run_power(suspend_prog, host, NULL, 0, "suspendprog",
			       max_timeout, NULL, NULL);
	log_flag(POWER, "power_save: suspending nodes %s", host);
}

/* Free all allocated memory */
static void _clear_power_config(void)
{
	xfree(suspend_prog);
	xfree(resume_prog);
	xfree(resume_fail_prog);
	suspend_exc_down = false;
	suspend_exc_state_flags = 0;
	FREE_NULL_BITMAP(exc_node_bitmap);
	FREE_NULL_LIST(partial_node_list);
}

static int _set_partition_options(void *x, void *arg)
{
	part_record_t *part_ptr = (part_record_t *)x;
	node_record_t *node_ptr;
	bool *suspend_time_set = (bool *)arg;

	if (suspend_time_set &&
	    (part_ptr->suspend_time != INFINITE) &&
	    (part_ptr->suspend_time != NO_VAL))
		*suspend_time_set = true;

	if (part_ptr->resume_timeout != NO_VAL16)
		max_timeout = MAX(max_timeout, part_ptr->resume_timeout);

	if (part_ptr->suspend_timeout != NO_VAL16)
		max_timeout = MAX(max_timeout, part_ptr->resume_timeout);

	for (int i = 0;
	     (node_ptr = next_node_bitmap(part_ptr->node_bitmap, &i)); i++) {
		if (node_ptr->suspend_time == NO_VAL)
			node_ptr->suspend_time = part_ptr->suspend_time;
		else if (part_ptr->suspend_time != NO_VAL)
			node_ptr->suspend_time = MAX(node_ptr->suspend_time,
						     part_ptr->suspend_time);

		if (node_ptr->resume_timeout == NO_VAL16)
			node_ptr->resume_timeout = part_ptr->resume_timeout;
		else if (part_ptr->resume_timeout != NO_VAL16)
			node_ptr->resume_timeout = MAX(
				node_ptr->resume_timeout,
				part_ptr->resume_timeout);

		if (node_ptr->suspend_timeout == NO_VAL16)
			node_ptr->suspend_timeout = part_ptr->suspend_timeout;
		else if (part_ptr->suspend_timeout != NO_VAL16)
			node_ptr->suspend_timeout = MAX(
				node_ptr->suspend_timeout,
				part_ptr->suspend_timeout);
	}

	return 0;
}

/*
 * Parse settings for excluding nodes, partitions and states from being
 * suspended.
 *
 * This creates node bitmaps. Must be done again when node bitmaps change.
 */
extern void power_save_exc_setup(void)
{
	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	FREE_NULL_BITMAP(exc_node_bitmap);

	if (slurm_conf.suspend_exc_nodes &&
	    (_parse_exc_nodes() != SLURM_SUCCESS))
		error("Invalid SuspendExcNodes %s some nodes may be ignored.",
		      slurm_conf.suspend_exc_nodes);

	if (slurm_conf.suspend_exc_parts) {
		char *tmp = NULL, *one_part = NULL, *part_list = NULL;
		part_record_t *part_ptr = NULL;

		part_list = xstrdup(slurm_conf.suspend_exc_parts);
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

	if (slurm_conf.suspend_exc_states)
		_parse_exc_states();

	if (power_save_debug) {
		if (exc_node_bitmap) {
			char *tmp = bitmap2node_name(exc_node_bitmap);
			log_flag(POWER, "excluded nodes %s", tmp);
			xfree(tmp);
		}
		if (partial_node_list) {
			(void) list_for_each(partial_node_list,
					     _list_part_node_lists, NULL);
		}
	}
}

/*
 * Initialize power_save module parameters.
 * Return 0 on valid configuration to run power saving,
 * otherwise log the problem and return -1
 */
static int _init_power_config(void)
{
	char *tmp_ptr;
	bool partition_suspend_time_set = false;

	last_work_scan  = 0;
	last_log	= 0;
	suspend_rate = slurm_conf.suspend_rate;
	resume_rate = slurm_conf.resume_rate;
	slurmd_timeout = slurm_conf.slurmd_timeout;
	max_timeout = MAX(slurm_conf.suspend_timeout,
			  slurm_conf.resume_timeout);
	_clear_power_config();
	if (slurm_conf.suspend_program)
		suspend_prog = xstrdup(slurm_conf.suspend_program);
	if (slurm_conf.resume_fail_program)
		resume_fail_prog = xstrdup(slurm_conf.resume_fail_program);
	if (slurm_conf.resume_program)
		resume_prog = xstrdup(slurm_conf.resume_program);

	cloud_reg_addrs = xstrcasestr(slurm_conf.slurmctld_params,
				      "cloud_reg_addrs");
	idle_on_node_suspend = xstrcasestr(slurm_conf.slurmctld_params,
					   "idle_on_node_suspend");
	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "power_save_interval="))) {
		power_save_interval =
			strtol(tmp_ptr + strlen("power_save_interval="), NULL,
			       10);
	}
	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "power_save_min_interval="))) {
		power_save_min_interval =
			strtol(tmp_ptr + strlen("power_save_min_interval="),
			       NULL, 10);
	}

	power_save_set_timeouts(&partition_suspend_time_set);

	if ((slurm_conf.suspend_time == INFINITE) &&
	    !partition_suspend_time_set) { /* not an error */
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

	if (slurm_conf.debug_flags & DEBUG_FLAG_POWER)
		power_save_debug = true;
	else
		power_save_debug = false;

	if (resume_fail_prog && !_valid_prog(resume_fail_prog)) {
		/* error's already reported in _valid_prog() */
		xfree(resume_fail_prog);
	}

	power_save_exc_setup();

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

extern void config_power_mgr(void)
{
	slurm_mutex_lock(&power_mutex);
	if (_init_power_config()) {
		if (power_save_enabled) {
			/* transition from enabled to disabled */
			info("power_save mode has been disabled due to configuration changes");
		}
		power_save_enabled = false;
		if (node_features_g_node_power()) {
			fatal("PowerSave required with NodeFeatures plugin, but not fully configured (SuspendProgram, ResumeProgram and SuspendTime all required)");
		}
	} else {
		power_save_enabled = true;
	}
	power_save_config = true;
	slurm_cond_signal(&power_cond);
	slurm_mutex_unlock(&power_mutex);
}

/*
 * start_power_mgr - Start power management thread as needed. The thread
 *	terminates automatically at slurmctld shutdown time or on config change
 *	disabling power_save mode.
 * IN thread_id - pointer to thread ID of the started pthread.
 */
extern void start_power_mgr(pthread_t *thread_id)
{
	slurm_mutex_lock(&power_mutex);
	if (power_save_started || !power_save_enabled) {
		if (!power_save_enabled && *thread_id) {
			slurm_mutex_unlock(&power_mutex);
			pthread_join(*thread_id, NULL);
			*thread_id = 0;
			return;
		}
		slurm_mutex_unlock(&power_mutex);
		return;
	}
	power_save_started = true;
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
		FREE_NULL_LIST(resume_job_list);
	}
	slurm_mutex_unlock(&power_mutex);
}

static int _build_resume_job_list(void *object, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)object;

	if (IS_JOB_CONFIGURING(job_ptr) &&
	    bit_overlap_any(job_ptr->node_bitmap,
			    power_node_bitmap)) {
		uint32_t *tmp = xmalloc(sizeof(uint32_t));
		*tmp = job_ptr->job_id;
		list_append(resume_job_list, tmp);
	}

	return SLURM_SUCCESS;
}

/*
 * init_power_save - Initialize the power save module. Started as a
 *	pthread. Terminates automatically at slurmctld shutdown time.
 *	Input and output are unused.
 */
static void *_init_power_save(void *arg)
{
        /* Locks: Write jobs and nodes */
        slurmctld_lock_t node_write_lock = {
                NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	time_t now, last_power_scan = 0;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "powersave", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "powersave");
	}
#endif

	/*
	 * Build up resume_job_list list in case shut down before resuming
	 * jobs/nodes without having to state save the list.
	 */
	if (!resume_job_list) {
		resume_job_list = list_create(xfree_ptr);

		lock_slurmctld(node_write_lock);
		list_for_each(job_list, _build_resume_job_list, NULL);
		unlock_slurmctld(node_write_lock);
	}

	while (slurmctld_config.shutdown_time == 0) {
		sleep(1);

		if (!power_save_enabled) {
			debug("power_save mode not enabled, stopping power_save thread");
			goto fini;
		}

		now = time(NULL);
		if ((now >= (last_power_scan + power_save_min_interval)) &&
		    ((last_node_update >= last_power_scan) ||
		     (now >= (last_power_scan + power_save_interval)))) {
			lock_slurmctld(node_write_lock);
			_do_power_work(now);
			unlock_slurmctld(node_write_lock);
			last_power_scan = now;
		}
	}

fini:	_clear_power_config();
	slurm_mutex_lock(&power_mutex);
	power_save_enabled = false;
	power_save_started = false;
	slurm_cond_signal(&power_cond);
	slurm_mutex_unlock(&power_mutex);
	return NULL;
}

extern void power_save_set_timeouts(bool *partition_suspend_time_set)

{
	node_record_t *node_ptr;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	/* Reset timeouts so new values can be caluclated. */
	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		node_ptr->suspend_time = NO_VAL;
		node_ptr->suspend_timeout = NO_VAL16;
		node_ptr->resume_timeout = NO_VAL16;
	}

	/* Figure out per-partition options and push to node level. */
	list_for_each(part_list, _set_partition_options,
		      partition_suspend_time_set);

	/* Apply global options to node level if not set at partition level. */
	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		node_ptr->suspend_time =
			((node_ptr->suspend_time == NO_VAL) ?
				slurm_conf.suspend_time :
				node_ptr->suspend_time);
		node_ptr->suspend_timeout =
			((node_ptr->suspend_timeout == NO_VAL16) ?
				slurm_conf.suspend_timeout :
				node_ptr->suspend_timeout);
		node_ptr->resume_timeout =
			((node_ptr->resume_timeout == NO_VAL16) ?
				slurm_conf.resume_timeout :
				node_ptr->resume_timeout);
	}
}
