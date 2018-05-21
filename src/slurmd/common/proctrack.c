/*****************************************************************************\
 *  proctrack.c - Process tracking plugin stub.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Copyright (C) 2013 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_LINUX_SCHED_H
#  include <linux/sched.h>
#endif

/* This is suppose to be defined in linux/sched.h but we have found it
 * is a very rare occation this is the case, so we define it here.
 */
#ifndef PF_DUMPCORE
#define PF_DUMPCORE     0x00000200      /* dumped core */
#endif


#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_proctrack_ops {
	int              (*create)    (stepd_step_rec_t * job);
	int              (*add)       (stepd_step_rec_t * job, pid_t pid);
	int              (*signal)    (uint64_t id, int signal);
	int              (*destroy)   (uint64_t id);
	uint64_t         (*find_cont) (pid_t pid);
	bool             (*has_pid)   (uint64_t id, pid_t pid);
	int              (*wait)      (uint64_t id);
	int              (*get_pids)  (uint64_t id, pid_t ** pids, int *npids);
} slurm_proctrack_ops_t;

/*
 * Must be synchronized with slurm_proctrack_ops_t above.
 */
static const char *syms[] = {
	"proctrack_p_create",
	"proctrack_p_add",
	"proctrack_p_signal",
	"proctrack_p_destroy",
	"proctrack_p_find",
	"proctrack_p_has_pid",
	"proctrack_p_wait",
	"proctrack_p_get_pids"
};

static slurm_proctrack_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * The proctrack plugin can only be changed by restarting slurmd
 * without preserving state (-c option).
 */
extern int slurm_proctrack_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "proctrack";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_proctrack_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);
	return retval;
}

extern int slurm_proctrack_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

/*
 * Create a container
 * job IN - stepd_step_rec_t structure
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in proctrack_g_add()
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_create(stepd_step_rec_t * job)
{
	if (slurm_proctrack_init() < 0)
		return 0;

	return (*(ops.create)) (job);
}

/*
 * Add a process to the specified container
 * job IN - stepd_step_rec_t structure
 * pid IN      - process ID to be added to the container
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in proctrack_g_create()
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_add(stepd_step_rec_t * job, pid_t pid)
{
	int i = 0, max_retry = 3, rc;

	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	/* Sometimes a plugin is transient in adding a pid, so lets
	 * try a few times before we call it quits.
	 */
	while ((rc = (*(ops.add)) (job, pid)) != SLURM_SUCCESS) {
		if (i++ > max_retry)
			break;
		debug("%s: %u.%u couldn't add pid %u, sleeping and trying again",
		      __func__, job->jobid, job->stepid, pid);
		sleep(1);
	}

	return rc;
}

/* Determine if core dump in progress
 * stat_fname - Pathname of the form /proc/<PID>/stat
 * RET - True if core dump in progress, otherwise false
 */
static bool _test_core_dumping(char* stat_fname)
{
	int pid, ppid, pgrp, session, tty, tpgid;
	char cmd[16], state[1];
	long unsigned flags, min_flt, cmin_flt, maj_flt, cmaj_flt;
	long unsigned utime, stime;
	long cutime, cstime, priority, nice, timeout, it_real_value;
	long resident_set_size;
	long unsigned start_time, vsize;
	long unsigned resident_set_size_rlim, start_code, end_code;
	long unsigned start_stack, kstk_esp, kstk_eip;
	long unsigned w_chan, n_swap, sn_swap;
	int  l_proc;
	int num;
	char *str_ptr, *proc_stat;
	int proc_fd, proc_stat_size = BUF_SIZE;
	bool dumping_results = false;

	proc_fd = open(stat_fname, O_RDONLY, 0);
	if (proc_fd == -1)
		return false;  /* process is now gone */
	proc_stat = xmalloc_nz(proc_stat_size + 1);
	while (1) {
		num = read(proc_fd, proc_stat, proc_stat_size);
		if (num <= 0) {
			proc_stat[0] = '\0';
			break;
		}
		proc_stat[num] = '\0';
		if (num < proc_stat_size)
			break;
		proc_stat_size += BUF_SIZE;
		xrealloc_nz(proc_stat, proc_stat_size + 1);
		if (lseek(proc_fd, (off_t) 0, SEEK_SET) != 0)
			break;
	}
	close(proc_fd);

	/* race condition at process termination */
	if (proc_stat[0] == '\0') {
		debug("%s: %s is empty", __func__, stat_fname);
		xfree(proc_stat);
		return false;
	}

	/* split into "PID (cmd" and "<rest>" */
	str_ptr = (char *)strrchr(proc_stat, ')');
	if (str_ptr == NULL) {
		error("%s: unexpected format of %s (%s) bracket missing?",
		      __func__, stat_fname, proc_stat);
		xfree(proc_stat);
		return false;
	}
	*str_ptr = '\0';		/* replace trailing ')' with NULL */
	/* parse these two strings separately, skipping the leading "(". */
	memset (cmd, 0, sizeof(cmd));
	sscanf (proc_stat, "%d (%15c", &pid, cmd);   /* comm[16] in kernel */
	num = sscanf(str_ptr + 2,		/* skip space after ')' too */
		"%c "
		"%d %d %d %d %d "
		"%lu %lu %lu %lu %lu %lu %lu "
		"%ld %ld %ld %ld %ld %ld "
		"%lu %lu "
		"%ld "
		"%lu %lu %lu "
		"%lu %lu %lu "
		"%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
		"%lu %lu %lu %*d %d",
		state,
		&ppid, &pgrp, &session, &tty, &tpgid,
		&flags, &min_flt, &cmin_flt, &maj_flt, &cmaj_flt, &utime, &stime,
		&cutime, &cstime, &priority, &nice, &timeout, &it_real_value,
		&start_time, &vsize,
		&resident_set_size,
		&resident_set_size_rlim, &start_code, &end_code,
		&start_stack, &kstk_esp, &kstk_eip,
/*		&signal, &blocked, &sig_ignore, &sig_catch, */ /* can't use */
		&w_chan, &n_swap, &sn_swap /* , &Exit_signal  */, &l_proc);

	if (num < 13)
		error("/proc entry too short (%s)", proc_stat);
	else if (flags & PF_DUMPCORE)
		dumping_results = true;
	xfree(proc_stat);

	return dumping_results;
}

typedef struct agent_arg {
	uint64_t cont_id;
	int signal;
} agent_arg_t;

static void *_sig_agent(void *args)
{
	bool hung_pids = false;
	agent_arg_t *agent_arg_ptr = args;

	while (1) {
		pid_t *pids = NULL;
		int i, npids = 0;
		char *stat_fname = NULL;

		if (hung_pids)
			sleep(5);

		hung_pids = false;

		if (proctrack_g_get_pids(agent_arg_ptr->cont_id, &pids,
					     &npids) == SLURM_SUCCESS) {
			/*
			 * Check if any processes are core dumping.
			 * If so, do not signal any of them, instead
			 * jump back to the sleep and wait for the core
			 * dump to finish.
			 *
			 * This works around an issue with OpenMP
			 * applications failing to write a full core
			 * file out - only one of the processes will
			 * be marked are core dumping, but killing any
			 * of them will terminate the application.
			 */
			for (i = 0; i < npids; i++) {
				xstrfmtcat(stat_fname, "/proc/%d/stat",
					   (int) pids[i]);
				if (_test_core_dumping(stat_fname)) {
					debug("Process %d continuing core dump",
					      (int) pids[i]);
					hung_pids = true;
					xfree(stat_fname);
					break;
				}
				xfree(stat_fname);
			}

			if (hung_pids) {
				xfree(pids);
				continue;
			}

			for (i = 0; i < npids; i++) {
				/* Kill processes */
				kill(pids[i], agent_arg_ptr->signal);
			}
			xfree(pids);
		}

		break;
	}

	(void) (*(ops.signal)) (agent_arg_ptr->cont_id, agent_arg_ptr->signal);
	xfree(args);
	return NULL;
}

static void _spawn_signal_thread(uint64_t cont_id, int signal)
{
	agent_arg_t *agent_arg_ptr;

	agent_arg_ptr = xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->cont_id = cont_id;
	agent_arg_ptr->signal  = signal;

	slurm_thread_create_detached(NULL, _sig_agent, agent_arg_ptr);
}

/*
 * Signal all processes within a container
 * cont_id IN - container ID as returned by proctrack_g_create()
 * signal IN  - signal to send, if zero then perform error checking
 *              but do not send signal
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_signal(uint64_t cont_id, int signal)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	if (signal == SIGKILL) {
		pid_t *pids = NULL;
		int i, j, npids = 0, hung_pids = 0;
		char *stat_fname = NULL;
		if (proctrack_g_get_pids(cont_id, &pids, &npids) ==
		    SLURM_SUCCESS) {
			/* NOTE: proctrack_g_get_pids() is not supported
			 * by the proctrack/pgid plugin */
			for (j = 0; j < 2; j++) {
				if (j)
					sleep(2);
				hung_pids = 0;
				for (i = 0; i < npids; i++) {
					if (!pids[i])
						continue;
					xstrfmtcat(stat_fname, "/proc/%d/stat",
						   (int) pids[i]);
					if (_test_core_dumping(stat_fname)) {
						debug("Process %d continuing "
						      "core dump",
						      (int) pids[i]);
						hung_pids++;
					} else {
						/* Don't test this PID again */
						pids[i] = 0;
					}
					xfree(stat_fname);
				}
				if (hung_pids == 0)
					break;
			}
			xfree(pids);
			if (hung_pids) {
				info("Defering sending signal, processes in "
				     "job are currently core dumping");
				_spawn_signal_thread(cont_id, signal);
				return SLURM_SUCCESS;
			}
		}
	}

	return (*(ops.signal)) (cont_id, signal);
}

/*
 * Destroy a container, any processes within the container are not effected
 * cont_id IN - container ID as returned by proctrack_g_create()
 *
 * Returns a Slurm errno.
*/
extern int proctrack_g_destroy(uint64_t cont_id)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.destroy)) (cont_id);
}

/*
 * Get container ID for given process ID
 *
 * Returns zero if no container found for the given pid.
 */
extern uint64_t proctrack_g_find(pid_t pid)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.find_cont)) (pid);
}

/*
 * Return "true" if the container "cont_id" contains the process with
 * ID "pid".
 */
extern bool proctrack_g_has_pid(uint64_t cont_id, pid_t pid)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.has_pid)) (cont_id, pid);
}

/*
 * Wait for all processes within a container to exit.
 *
 * When proctrack_g_wait returns SLURM_SUCCESS, the container is considered
 * destroyed.  There is no need to call proctrack_g_destroy after
 * a successful call to proctrack_g_wait, and in fact it will trigger
 * undefined behavior.
 *
 * Return SLURM_SUCCESS or SLURM_ERROR.
 */
extern int proctrack_g_wait(uint64_t cont_id)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.wait)) (cont_id);
}

/*
 * Get all process IDs within a container.
 *
 * IN cont_id - Container ID.
 * OUT pids - a pointer to an xmalloc'ed array of process ids, of
 *	length "npids".  Caller must free array with xfree().
 * OUT npids - number of process IDs in the returned "pids" array.
 *
 * Return SLURM_SUCCESS if container exists (npids may be zero, and
 *   pids NULL), return SLURM_ERROR if container does not exist, or
 *   plugin does not implement the call.
 */
extern int proctrack_g_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.get_pids)) (cont_id, pids, npids);
}
