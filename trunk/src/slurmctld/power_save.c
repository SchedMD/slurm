/*****************************************************************************\
 *  power_save.c - support node power saving mode. Nodes which have been 
 *  idle for an extended period of time will be placed into a power saving 
 *  mode by running an arbitrary script. This script can lower the voltage
 *  or frequency of the nodes or can completely power the nodes off.
 *  When the node is restored to normal operation, another script will be 
 *  executed. Many parameters are available to control this mode of operation.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

#define _DEBUG 0
#define PID_CNT 10

pid_t child_pid[PID_CNT];
int idle_time, suspend_rate, resume_rate;
char *suspend_prog = NULL, *resume_prog = NULL;
char *exc_nodes = NULL, *exc_parts = NULL;
time_t last_config = (time_t) 0;

bitstr_t *exc_node_bitmap = NULL;
int suspend_cnt, resume_cnt;

static void  _clear_power_config(void);
static void  _do_power_work(void);
static void  _do_resume(char *host);
static void  _do_suspend(char *host);
static int   _init_power_config(void);
static int   _reap_procs(void);
static void  _re_wake(void);
static pid_t _run_prog(char *prog, char *arg);
static bool  _valid_prog(char *file_name);

/* Perform any power change work to nodes */
static void _do_power_work(void)
{
	static time_t last_log = 0, last_work_scan = 0;
	int i, wake_cnt = 0, sleep_cnt = 0, susp_total = 0;
	time_t now = time(NULL), delta_t;
	uint16_t base_state, comp_state, susp_state;
	bitstr_t *wake_node_bitmap = NULL, *sleep_node_bitmap = NULL;
	struct node_record *node_ptr;

	/* Set limit on counts of nodes to have state changed */
	delta_t = now - last_work_scan;
	if (delta_t >= 60) {
		suspend_cnt = 0;
		resume_cnt  = 0;
	} else {
		float rate = (60 - delta_t) / 60.0;
		suspend_cnt *= rate;
		resume_cnt  *= rate;
	}
	last_work_scan = now;

	/* Build bitmaps identifying each node which should change state */
	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		susp_state = node_ptr->node_state & NODE_STATE_POWER_SAVE;
		comp_state = node_ptr->node_state & JOB_COMPLETING;

		if (susp_state)
			susp_total++;
		if (susp_state
		&&  ((suspend_rate == 0) || (suspend_cnt <= suspend_rate))
		&&  ((base_state == NODE_STATE_ALLOCATED)
		||   (node_ptr->last_idle > (now - idle_time)))) {
			if (wake_node_bitmap == NULL)
				wake_node_bitmap = bit_alloc(node_record_count);
			wake_cnt++;
			suspend_cnt++;
			node_ptr->node_state &= (~NODE_STATE_POWER_SAVE);
			node_ptr->node_state |=   NODE_STATE_NO_RESPOND;
			node_ptr->last_response = now;
			bit_set(wake_node_bitmap, i);
		}
		if ((susp_state == 0)
		&&  ((resume_rate == 0) || (resume_cnt <= resume_rate))
		&&  (base_state == NODE_STATE_IDLE)
		&&  (comp_state == 0)
		&&  (node_ptr->last_idle < (now - idle_time))
		&&  ((exc_node_bitmap == NULL) || 
		     (bit_test(exc_node_bitmap, i) == 0))) {
			if (sleep_node_bitmap == NULL) {
				sleep_node_bitmap = 
					bit_alloc(node_record_count);
			}
			sleep_cnt++;
			resume_cnt++;
			node_ptr->node_state |= NODE_STATE_POWER_SAVE;
			bit_set(sleep_node_bitmap, i);
		}
	}
	if ((now - last_log) > 600) {
		info("Power save mode: %d nodes", susp_total);
		last_log = now;
	}

	if ((wake_cnt == 0) && (sleep_cnt == 0))
		_re_wake();	/* No work to be done now */

	if (sleep_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(sleep_node_bitmap);
		if (nodes)
			_do_suspend(nodes);
		else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		bit_free(sleep_node_bitmap);
		last_node_update = now;
	}

	if (wake_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(wake_node_bitmap);
		if (nodes)
			_do_resume(nodes);
		else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		bit_free(wake_node_bitmap);
		last_node_update = now;
	}
}

/* Just in case some resume calls failed, re-issue the requests
 * periodically for active nodes. We do not increment resume_cnt
 * since there should be no change in power requirements. */
static void _re_wake(void)
{
	static time_t last_wakeup = 0;
	static int last_inx = 0;
	uint16_t base_state;
	time_t now = time(NULL);
	struct node_record *node_ptr;
	bitstr_t *wake_node_bitmap = NULL;
	int i, lim = MIN(node_record_count, 20);

	/* Run at most once every 30 minutes */
	if ((now - last_wakeup) < 1800)
		return;
	last_wakeup = now;

	for (i=0; i<lim; i++) {
		node_ptr = &node_record_table_ptr[last_inx];
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if ((base_state != NODE_STATE_DOWN) &&
		    (base_state != NODE_STATE_FUTURE) &&
		    ((node_ptr->node_state & NODE_STATE_DRAIN) == 0) &&
		    ((node_ptr->node_state & NODE_STATE_POWER_SAVE) == 0)) {
			if (wake_node_bitmap == NULL)
				wake_node_bitmap = bit_alloc(node_record_count);
			bit_set(wake_node_bitmap, last_inx);
		}
		last_inx++;
		if (last_inx >= node_record_count)
			last_inx = 0;
	}

	if (wake_node_bitmap) {
		char *nodes;
		nodes = bitmap2node_name(wake_node_bitmap);
		if (nodes) {
#if _DEBUG
			info("power_save: rewaking nodes %s", nodes);
#else
			debug("power_save: rewaking nodes %s", nodes);
#endif
			_run_prog(resume_prog, nodes);	
		} else
			error("power_save: bitmap2nodename");
		xfree(nodes);
		bit_free(wake_node_bitmap);
	}		
}

static void _do_resume(char *host)
{
#if _DEBUG
	info("power_save: waking nodes %s", host);
#else
	debug("power_save: waking nodes %s", host);
#endif
	_run_prog(resume_prog, host);	
}

static void _do_suspend(char *host)
{
#if _DEBUG
	info("power_save: suspending nodes %s", host);
#else
	debug("power_save: suspending nodes %s", host);
#endif
	_run_prog(suspend_prog, host);	
}

static pid_t _run_prog(char *prog, char *arg)
{
	int i;
	char program[1024], arg0[1024], arg1[1024], *pname;
	pid_t child;

	if (prog == NULL)	/* disabled, useful for testing */
		return -1;

	strncpy(program, prog, sizeof(program));
	pname = strrchr(program, '/');
	if (pname == NULL)
		pname = program;
	else
		pname++;
	strncpy(arg0, pname, sizeof(arg0));
	strncpy(arg1, arg, sizeof(arg1));

	child = fork();
	if (child == 0) {
		for (i=0; i<128; i++)
			close(i);
		execl(program, arg0, arg1, NULL);
		exit(1);
	} else if (child < 0)
		error("fork: %m");
	else {
		/* save the pid */
		for (i=0; i<PID_CNT; i++) {
			if (child_pid[i])
				continue;
			child_pid[i] = child;
			break;
		}
		if (i == PID_CNT)
			error("power_save: filled child_pid array");
	}
	return child;
}

/* reap child processes previously forked to modify node state.
 * return the count of empty slots in the child_pid array */
static int  _reap_procs(void)
{
	int empties = 0, i, rc, status;

	for (i=0; i<PID_CNT; i++) {
		if (child_pid[i] == 0) {
			empties++;
			continue;
		}
		rc = waitpid(child_pid[i], &status, WNOHANG);
		if (rc == 0)
			continue;
		child_pid[i] = 0;
		rc = WEXITSTATUS(status);
		if (rc != 0)
			error("power_save: program exit status of %d", rc);
	}
	return empties;
}

/* Free all allocated memory */
static void _clear_power_config(void)
{
	xfree(suspend_prog);
	xfree(resume_prog);
	xfree(exc_nodes);
	xfree(exc_parts);
	FREE_NULL_BITMAP(exc_node_bitmap);
}

/* Initialize power_save module paramters.
 * Return 0 on valid configuration to run power saving,
 * otherwise log the problem and return -1 */
static int _init_power_config(void)
{
	slurm_ctl_conf_t *conf = slurm_conf_lock();

	last_config   = slurmctld_conf.last_update;
	idle_time     = conf->suspend_time - 1;
	suspend_rate  = conf->suspend_rate;
	resume_rate   = conf->resume_rate;
	_clear_power_config();
	if (conf->suspend_program)
		suspend_prog = xstrdup(conf->suspend_program);
	if (conf->resume_program)
		resume_prog = xstrdup(conf->resume_program);
	if (conf->suspend_exc_nodes)
		exc_nodes = xstrdup(conf->suspend_exc_nodes);
	if (conf->suspend_exc_parts)
		exc_parts = xstrdup(conf->suspend_exc_parts);
	slurm_conf_unlock();

	if (idle_time < 0) {	/* not an error */
		debug("power_save module disabled, idle_time < 0");
		return -1;
	}
	if (suspend_rate < 1) {
		error("power_save module disabled, suspend_rate < 1");
		return -1;
	}
	if (resume_rate < 1) {
		error("power_save module disabled, resume_rate < 1");
		return -1;
	}
	if (suspend_prog == NULL)
		info("WARNING: power_save module has NULL suspend program");
	else if (!_valid_prog(suspend_prog)) {
		error("power_save module disabled, invalid suspend program %s",
			suspend_prog);
		return -1;
	}
	if (resume_prog == NULL)
		info("WARNING: power_save module has NULL resume program");
	else if (!_valid_prog(resume_prog)) {
		error("power_save module disabled, invalid resume program %s",
			resume_prog);
		return -1;
	}

	if (exc_nodes
	&&  (node_name2bitmap(exc_nodes, false, &exc_node_bitmap))) {
		error("power_save module disabled, "
			"invalid excluded nodes %s", exc_nodes);
		return -1;
	}

	if (exc_parts) {
		char *tmp = NULL, *one_part = NULL, *part_list = NULL;
		struct part_record *part_ptr = NULL;
		int rc = 0;

		part_list = xstrdup(exc_parts);
		one_part = strtok_r(part_list, ",", &tmp);
		while (one_part != NULL) {
			part_ptr = find_part_record(one_part);
			if (!part_ptr) {
				error("power_save module disabled, "
					"invalid excluded partition %s",
					one_part);
				rc = -1;
				break;
			}
			if (exc_node_bitmap)
				bit_or(exc_node_bitmap, part_ptr->node_bitmap);
			else
				exc_node_bitmap = bit_copy(part_ptr->
							   node_bitmap);
			one_part = strtok_r(NULL, ",", &tmp);
		}
		xfree(part_list);
		if (rc)
			return rc;
	}

	if (exc_node_bitmap) {
		char *tmp = bitmap2node_name(exc_node_bitmap);
		debug("power_save module, excluded nodes %s", tmp);
		xfree(tmp);
	}

	return 0;
}

static bool _valid_prog(char *file_name)
{
	struct stat buf;

	if (file_name[0] != '/') {
		debug("power_save program %s not absolute pathname", file_name);
		return false;
	}

	if (access(file_name, X_OK) != 0) {
		debug("power_save program %s not executable", file_name);
		return false;
	}

	if (stat(file_name, &buf)) {
		debug("power_save program %s not found", file_name);
		return false;
	}
	if (buf.st_mode & 022) {
		debug("power_save program %s has group or "
		      "world write permission",
		      file_name);
		return false;
	}

	return true;
}

/*
 * init_power_save - initialize the power save module. Started as a
 *	pthread. Terminates automatically at slurmctld shutdown time.
 *	Input and output are unused.
 */
extern void *init_power_save(void *arg)
{
        /* Locks: Write node, read jobs and partitions */
        slurmctld_lock_t node_write_lock = {
                NO_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK };
	time_t now, last_power_scan = 0;

	if (_init_power_config())
		goto fini;

	while (slurmctld_config.shutdown_time == 0) {
		sleep(1);

		if (_reap_procs() < 2) {
			error("power_save programs not completing quickly");
			continue;
		}

		if ((last_config != slurmctld_conf.last_update)
		&&  (_init_power_config()))
			goto fini;

		/* Only run every 60 seconds or after
		 * a node state change, whichever 
		 * happens first */
		now = time(NULL);
		if ((last_node_update < last_power_scan)
		&&  (now < (last_power_scan + 60)))
			continue;

		lock_slurmctld(node_write_lock);
		_do_power_work();
		unlock_slurmctld(node_write_lock);
		last_power_scan = now;
	}

fini:	_clear_power_config();
	return NULL;
}
