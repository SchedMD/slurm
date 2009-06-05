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
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#if defined (HAVE_DECL_STRSIGNAL) && !HAVE_DECL_STRSIGNAL
#  ifndef strsignal
     extern char *strsignal(int);
#  endif
#endif /* defined HAVE_DECL_STRSIGNAL && !HAVE_DECL_STRSIGNAL */

#define _DEBUG			0
#define PID_CNT			10
#define MAX_SHUTDOWN_DELAY	120	/* seconds to wait for child procs
					 * to exit after daemon shutdown 
					 * request, then orphan or kill proc */

/* Records for tracking processes forked to suspend/resume nodes */
pid_t  child_pid[PID_CNT];	/* pid of process		*/
time_t child_time[PID_CNT];	/* start time of process	*/

int idle_time, suspend_rate, resume_timeout, resume_rate, suspend_timeout;
char *suspend_prog = NULL, *resume_prog = NULL;
char *exc_nodes = NULL, *exc_parts = NULL;
time_t last_config = (time_t) 0, last_suspend = (time_t) 0;
uint16_t slurmd_timeout;

bitstr_t *exc_node_bitmap = NULL, *suspend_node_bitmap = NULL;
int   suspend_cnt,   resume_cnt;
float suspend_cnt_f, resume_cnt_f;

static void  _clear_power_config(void);
static void  _do_power_work(void);
static void  _do_resume(char *host);
static void  _do_suspend(char *host);
static int   _init_power_config(void);
static int   _kill_procs(void);
static int   _reap_procs(void);
static void  _re_wake(void);
static pid_t _run_prog(char *prog, char *arg);
static void  _shutdown_power(void);
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
	bool run_suspend = false;

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

	if (now > (last_suspend + suspend_timeout)) {
		/* ready to start another round of node suspends */
		run_suspend = true;
		if (last_suspend) {
			bit_nclear(suspend_node_bitmap, 0, 
				   (node_record_count - 1));
			last_suspend = (time_t) 0;
		}
	}

	last_work_scan = now;

	/* Build bitmaps identifying each node which should change state */
	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		susp_state = node_ptr->node_state & NODE_STATE_POWER_SAVE;
		comp_state = node_ptr->node_state & NODE_STATE_COMPLETING;

		if (susp_state)
			susp_total++;

		/* Resume nodes as appropriate */
		if (susp_state &&
		    ((resume_rate == 0) || (resume_cnt < resume_rate))	&&
		    (bit_test(suspend_node_bitmap, i) == 0)		&&
		    ((base_state == NODE_STATE_ALLOCATED) ||
		     (node_ptr->last_idle > (now - idle_time)))) {
			if (wake_node_bitmap == NULL) {
				wake_node_bitmap = 
					bit_alloc(node_record_count);
			}
			wake_cnt++;
			resume_cnt++;
			resume_cnt_f++;
			node_ptr->node_state &= (~NODE_STATE_POWER_SAVE);
			bit_clear(power_node_bitmap, i);
			node_ptr->node_state   |= NODE_STATE_NO_RESPOND;
			node_ptr->last_response = now + resume_timeout;
			bit_set(wake_node_bitmap, i);
		}

		/* Suspend nodes as appropriate */
		if (run_suspend 					&& 
		    (susp_state == 0)					&&
		    ((suspend_rate == 0) || (suspend_cnt < suspend_rate)) &&
		    (base_state == NODE_STATE_IDLE)			&&
		    (comp_state == 0)					&&
		    (node_ptr->last_idle < (now - idle_time))		&&
		    ((exc_node_bitmap == NULL) || 
		     (bit_test(exc_node_bitmap, i) == 0))) {
			if (sleep_node_bitmap == NULL) {
				sleep_node_bitmap = 
					bit_alloc(node_record_count);
			}
			sleep_cnt++;
			suspend_cnt++;
			suspend_cnt_f++;
			node_ptr->node_state |= NODE_STATE_POWER_SAVE;
			bit_set(power_node_bitmap, i);
			bit_set(sleep_node_bitmap,   i);
			bit_set(suspend_node_bitmap, i);
			last_suspend = now;
		}
	}
	if (((now - last_log) > 600) && (susp_total > 0)) {
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

/* If slurmctld crashes, the node state that it recovers could differ
 * from the actual hardware state (e.g. ResumeProgram failed to complete).
 * To address that, when a node that should be powered up for a running 
 * job is not responding, they try running ResumeProgram again. */
static void _re_wake(void)
{
	uint16_t base_state;
	struct node_record *node_ptr;
	bitstr_t *wake_node_bitmap = NULL;
	int i;

	node_ptr = node_record_table_ptr;
	for (i=0; i<node_record_count; i++, node_ptr++) {
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if ((base_state == NODE_STATE_ALLOCATED)		  &&
		    (node_ptr->node_state & NODE_STATE_NO_RESPOND)	  &&
		    ((node_ptr->node_state & NODE_STATE_POWER_SAVE) == 0) &&
		    (bit_test(suspend_node_bitmap, i) == 0)) {
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
			info("power_save: rewaking nodes %s", nodes);
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
	verbose("power_save: waking nodes %s", host);
#endif
	_run_prog(resume_prog, host);	
}

static void _do_suspend(char *host)
{
#if _DEBUG
	info("power_save: suspending nodes %s", host);
#else
	verbose("power_save: suspending nodes %s", host);
#endif
	_run_prog(suspend_prog, host);	
}

/* run a suspend or resume program
 * prog IN	- program to run
 * arg IN	- program arguments, the hostlist expression
 */
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
		setpgrp();
		execl(program, arg0, arg1, NULL);
		exit(1);
	} else if (child < 0) {
		error("fork: %m");
	} else {
		/* save the pid */
		for (i=0; i<PID_CNT; i++) {
			if (child_pid[i])
				continue;
			child_pid[i]  = child;
			child_time[i] = time(NULL);
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
	int empties = 0, delay, i, max_timeout, rc, status;

	max_timeout = MAX(suspend_timeout, resume_timeout);
	for (i=0; i<PID_CNT; i++) {
		if (child_pid[i] == 0) {
			empties++;
			continue;
		}
		rc = waitpid(child_pid[i], &status, WNOHANG);
		if (rc == 0)
			continue;

		delay = difftime(time(NULL), child_time[i]);
		if (delay > max_timeout) {
			info("power_save: program %d ran for %d sec", 
			     (int) child_pid[i], delay);
		}

		if (WIFEXITED(status)) {
			rc = WEXITSTATUS(status);
			if (rc != 0) {
				error("power_save: program exit status of %d", 
				      rc);
			}
		} else if (WIFSIGNALED(status)) {
			error("power_save: program signalled: %s",
			      strsignal(WTERMSIG(status)));
		}

		child_pid[i]  = 0;
		child_time[i] = (time_t) 0;
	}
	return empties;
}

/* kill (or orphan) child processes previously forked to modify node state.
 * return the count of killed/orphaned processes */
static int  _kill_procs(void)
{
	int killed = 0, i, rc, status;

	for (i=0; i<PID_CNT; i++) {
		if (child_pid[i] == 0)
			continue;

		rc = waitpid(child_pid[i], &status, WNOHANG);
		if (rc == 0) {
#ifdef  POWER_SAVE_KILL_PROCS
			error("power_save: killing process %d",
			      child_pid[i]);
			kill((0-child_pid[i]), SIGKILL);
#else
			error("power_save: orphaning process %d",
			      child_pid[i]);
#endif
			killed++;
		} else {
			/* process already completed */
		}
		child_pid[i]  = 0;
		child_time[i] = (time_t) 0;
	}
	return killed;
}

static void _shutdown_power(void)
{
	int i, proc_cnt, max_timeout;

	max_timeout = MAX(suspend_timeout, resume_timeout);
	/* Try to avoid orphan processes */
	for (i=0; ; i++) {
		proc_cnt = PID_CNT - _reap_procs();
		if (proc_cnt == 0)	/* all procs completed */
			break;
		if (i >= max_timeout) {
			error("power_save: orphaning %d processes which are "
			      "not terminating so slurmctld can exit", 
			      proc_cnt);
			_kill_procs();
			break;
		} else if (i == 2) {
			info("power_save: waiting for %d processes to "
			     "complete", proc_cnt);
		} else if (i % 5 == 0) {
			debug("power_save: waiting for %d processes to "
			      "complete", proc_cnt);
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
}

/* Initialize power_save module paramters.
 * Return 0 on valid configuration to run power saving,
 * otherwise log the problem and return -1 */
static int _init_power_config(void)
{
	slurm_ctl_conf_t *conf = slurm_conf_lock();

	last_config     = slurmctld_conf.last_update;
	idle_time       = conf->suspend_time - 1;
	suspend_rate    = conf->suspend_rate;
	resume_timeout  = conf->resume_timeout;
	resume_rate     = conf->resume_rate;
	slurmd_timeout  = conf->slurmd_timeout;
	suspend_timeout = conf->suspend_timeout;
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
		debug("power_save module disabled, SuspendTime < 0");
		return -1;
	}
	if (suspend_rate < 1) {
		error("power_save module disabled, SuspendRate < 1");
		return -1;
	}
	if (resume_rate < 1) {
		error("power_save module disabled, ResumeRate < 1");
		return -1;
	}
	if (suspend_prog == NULL) {
		error("power_save module disabled, NULL SuspendProgram");
		return -1;
	} else if (!_valid_prog(suspend_prog)) {
		error("power_save module disabled, invalid SuspendProgram %s",
		      suspend_prog);
		return -1;
	}
	if (resume_prog == NULL) {
		error("power_save module disabled, NULL ResumeProgram");
		return -1;
	} else if (!_valid_prog(resume_prog)) {
		error("power_save module disabled, invalid ResumeProgram %s",
		      resume_prog);
		return -1;
	}

	if (exc_nodes &&
	    (node_name2bitmap(exc_nodes, false, &exc_node_bitmap))) {
		error("power_save module disabled, "
		      "invalid SuspendExcNodes %s", exc_nodes);
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
					"invalid SuspendExcPart %s",
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
		debug("power_save program %s not absolute pathname", 
		     file_name);
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
        /* Locks: Read nodes */
        slurmctld_lock_t node_read_lock = {
                NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
        /* Locks: Write nodes */
        slurmctld_lock_t node_write_lock = {
                NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	time_t now, boot_time = 0, last_power_scan = 0;

	if (_init_power_config())
		goto fini;

	suspend_node_bitmap = bit_alloc(node_record_count);
	if (suspend_node_bitmap == NULL)
		fatal("power_save: malloc error");

	while (slurmctld_config.shutdown_time == 0) {
		sleep(1);

		if (_reap_procs() < 2) {
			debug("power_save programs getting backlogged");
			continue;
		}

		if ((last_config != slurmctld_conf.last_update) &&
		    (_init_power_config())) {
			info("power_save mode has been disabled due to "
			     "configuration changes");
			goto fini;
		}

		now = time(NULL);
		if (boot_time == 0)
			boot_time = now;

		/* Only run every 60 seconds or after a node state change,
		 *  whichever happens first */
		if ((last_node_update >= last_power_scan) ||
		    (now >= (last_power_scan + 60))) {
			lock_slurmctld(node_write_lock);
			_do_power_work();
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
	FREE_NULL_BITMAP(suspend_node_bitmap);
	_shutdown_power();
	return NULL;
}
