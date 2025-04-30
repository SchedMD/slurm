/*****************************************************************************\
 *  proctrack_cgroup.c - process tracking via linux cgroup containers
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/xstring.h"
#include "src/interfaces/cgroup.h"
#include "src/common/read_config.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]      = "Process tracking via linux cgroup freezer subsystem";
const char plugin_type[]      = "proctrack/cgroup";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static pthread_mutex_t monitor_setup_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t monitor_setup_cond = PTHREAD_COND_INITIALIZER;
static bool monitor_setup = false;

static pthread_mutex_t ended_task_check_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ended_task_check_cond = PTHREAD_COND_INITIALIZER;
static bool ended_task_check_complete = false;

typedef struct {
	stepd_step_rec_t *step;
	uint32_t task_offset;
	stepd_step_task_info_t **ended_task;
	int end_fd;
} ended_task_monitor_args_t;

int
_slurm_cgroup_is_pid_a_slurm_task(uint64_t id, pid_t pid)
{
	int fstatus = -1;
	int fd;
	pid_t ppid;
	char file_path[PATH_MAX], buf[2048] = {0};

	if (snprintf(file_path, PATH_MAX, "/proc/%ld/stat",
		     (long)pid) >= PATH_MAX) {
		debug2("unable to build pid '%d' stat file: %m ", pid);
		return fstatus;
	}

	if ((fd = open(file_path, O_RDONLY)) < 0) {
		debug2("unable to open '%s' : %m ", file_path);
		return fstatus;
	}
	if (read(fd, buf, 2048) <= 0) {
		debug2("unable to read '%s' : %m ", file_path);
		close(fd);
		return fstatus;
	}
	close(fd);

	if (sscanf(buf, "%*d %*s %*s %d", &ppid) != 1) {
		debug2("unable to get ppid of pid '%d', %m", pid);
		return fstatus;
	}

	/*
	 * assume that any child of slurmstepd is a slurm task
	 * they will get all signals, inherited processes will
	 * only get SIGKILL
	 */
	if (ppid == (pid_t) id)
		fstatus = 1;
	else
		fstatus = 0;

	return fstatus;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init (void)
{
	/* initialize cgroup internal data */
	if (cgroup_g_initialize(CG_TRACK) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini (void)
{
	return SLURM_SUCCESS;
}

/*
 * Uses slurmd job-step manager's pid as the unique container id.
 */
extern int proctrack_p_create(stepd_step_rec_t *step)
{
	int rc;

	if ((rc = cgroup_g_step_create(CG_TRACK, step)) != SLURM_SUCCESS)
		return rc;

	/* Use slurmstepd pid as the id of the container. */
	step->cont_id = (uint64_t)step->jmgr_pid;

	return cgroup_g_step_addto(CG_TRACK, &step->jmgr_pid, 1);
}

extern int proctrack_p_add(stepd_step_rec_t *step, pid_t pid)
{
	return cgroup_g_step_addto(CG_TRACK, &pid, 1);
}

extern int proctrack_p_signal (uint64_t id, int signal)
{
	pid_t* pids = NULL;
	int npids = 0;
	int i;
	int slurm_task;

	/* Currently the cgroup.kill feature only supports SIGKILL */
	if ((signal == SIGKILL) && cgroup_g_has_feature(CG_KILL_BUTTON)) {
		return cgroup_g_signal(signal);
	}

	/* get all the pids associated with the step */
	if (cgroup_g_step_get_pids(&pids, &npids) != SLURM_SUCCESS) {
		debug3("unable to get pids list for cont_id=%"PRIu64"", id);
		/* that could mean that all the processes already exit */
		/* the container so return success */
		return SLURM_SUCCESS;
	}

	/* directly manage SIGSTOP using cgroup freezer subsystem */
	if (signal == SIGSTOP) {
		xfree(pids);
		return cgroup_g_step_suspend();
	}

	/* start by resuming in case of SIGKILL */
	if (signal == SIGKILL) {
		cgroup_g_step_resume();
	}

	for (i = 0 ; i<npids ; i++) {
		/*
		 * Be on the safe side and do not kill slurmstepd (ourselves),
		 * since a call to proctrack_g_get_pids can return all the pids
		 * in the container which can include us, depending on the
		 * plugin used (e.g. cgroup/v2).
		 */
		if (pids[i] == (pid_t)id)
			continue;

		slurm_task = _slurm_cgroup_is_pid_a_slurm_task(id, pids[i]);
		if (slurm_cgroup_conf.signal_children_processes ||
		    (slurm_task == 1) || (signal == SIGKILL)) {
			debug2("sending process %d (%s) signal %d",
			       pids[i],
			       (slurm_task==1)?"slurm_task":"inherited_task",
			       signal);
			kill(pids[i], signal);
		}
	}

	xfree(pids);

	/* resume tasks after signaling slurm tasks with SIGCONT to be sure */
	/* that SIGTSTP received at suspend time is removed */
	if (signal == SIGCONT) {
		return cgroup_g_step_resume();
	}

	return SLURM_SUCCESS;
}

extern int proctrack_p_destroy (uint64_t id)
{
	return cgroup_g_step_destroy(CG_TRACK);
}

extern uint64_t proctrack_p_find(pid_t pid)
{
	/* not provided for now */
	return 0;
}

extern bool proctrack_p_has_pid(uint64_t cont_id, pid_t pid)
{
	return cgroup_g_has_pid(pid);
}


extern int proctrack_p_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	return cgroup_g_step_get_pids(pids, npids);
}

extern int proctrack_p_wait(uint64_t cont_id)
{
	int delay = 1;
	time_t start = time(NULL), now;
	pid_t *pids = NULL;
	int npids = 0, rc;

	if (cont_id == 0 || cont_id == 1)
		return SLURM_ERROR;

	/*
	 * Spin until the container is empty. This indicates that all tasks have
	 * exited the container.
	 */
	rc = proctrack_p_get_pids(cont_id, &pids, &npids);
	while ((rc == SLURM_SUCCESS) && npids) {
		if ((npids == 1) && (pids[0] == cont_id))
			break;

		now = time(NULL);
		if (now > (start + slurm_conf.unkillable_timeout)) {
			error("Container %"PRIu64" in cgroup plugin has %d processes, giving up after %lu sec",
			      cont_id, npids, (now - start));
			break;
		}
		/*
		 * The call to proctrack_p_signal is safe since it takes care of
		 * not killing slurmstepd processes (ourselves).
		 */
		proctrack_p_signal(cont_id, SIGKILL);
		sleep(delay);
		if (delay < 32)
			delay *= 2;
		xfree(pids);
		rc = proctrack_p_get_pids(cont_id, &pids, &npids);
	}
	xfree(pids);
	return SLURM_SUCCESS;
}

/*
 * Check if a task cgroup is empty
 *
 * IN task - Check if this task is empty
 * IN task_offset - task offset used for het jobs
 * OUT ended_task - Pointer to ended task info. Only set if a task ended
 *
 * RET SLURM_SUCCESS or error
 */
static int _check_if_task_cg_empty(stepd_step_task_info_t *task,
				   uint32_t task_offset,
				   stepd_step_task_info_t **ended_task)
{
	int cgroup_state;

	cgroup_state = cgroup_g_is_task_empty(task->gtid + task_offset);

	switch (cgroup_state) {
	case CGROUP_EMPTY:
		if (!(*ended_task)) {
			int pid;

			/* Get primary pid exit code */
			pid = wait4(task->pid, &task->estatus, WNOHANG,
				    &task->rusage);
			if (pid == 0) {
				error("Task %d's primary pid %d is running but task cgroup says it is empty. Unable to get exit code for this task",
				      task->gtid + task_offset, task->pid);
			} else if (pid == -1) {
				/*
				 * wait4 may not find the process specified by
				 * pid and set ECHILD. This likely means the
				 * non-zero exit monitor already recorded
				 * wstatus and rusage.
				 */
				if (!(errno == ECHILD)) {
					error("wait4() failed for pid %d task %d. Unable to get exit code for this task: %m",
					      task->pid,
					      task->gtid + task_offset);
				}
			}

			*ended_task = task;
		}
		return SLURM_SUCCESS;
	case CGROUP_POPULATED:
		/* processes still running in task */
		return SLURM_SUCCESS;
	default:
		error("Could not determine if task %d cgroup is empty",
		      task->gtid + task_offset);
		break;
	}

	return SLURM_ERROR;
}

/*
 * Check for any empty task cgroups in a step
 *
 * IN step - Step record in which we are checking for any ended tasks
 * IN task_offset - task offset used for het jobs
 * OUT ended_task - Pointer to ended task info. Only set if a task ended
 *
 * RET SLURM_SUCCESS or error
 */
static int _check_for_any_empty_task_cg(stepd_step_rec_t *step,
					uint32_t task_offset,
					stepd_step_task_info_t **ended_task)
{
	for (int i = 0; i < step->node_tasks; i++) {
		/* skip tasks that we already know have exited */
		if (step->task[i]->exited)
			continue;

		if (_check_if_task_cg_empty(step->task[i], task_offset,
					    ended_task)) {
			return SLURM_ERROR;
		}
		if (*ended_task) {
			return SLURM_SUCCESS;
		}
	}

	return SLURM_SUCCESS;
}

/*
 * Check whether any child processes exited non-zero
 *
 * OUT pid - pointer to rc from wait3()
 * OUT wstatus - wstatus output from wait3()
 * OUT rusage - rusage output from wait3()
 * in block - if true, use WNOHANG for wait3()
 *
 * RET SLURM_SUCCESS or error
 */
static int _wait_for_any_child(int *pid, int *wstatus, struct rusage *rusage,
			       bool block)
{
	*pid = wait3(wstatus, block ? 0 : WNOHANG, rusage);

	if (*pid == -1) {
		if (errno == EINTR) {
			debug2("wait3 was interrupted");
			return SLURM_SUCCESS;
		}
		if (errno == ECHILD) {
			debug2("wait3 returned ECHILD, no more child processes");
			return SLURM_ERROR;
		}
		error("wait3() failed: %m");
		return SLURM_ERROR;
	}
	if (*pid == 0) {
		/*
		 * WNOHANG was used, one or more children exist but have not yet
		 * changed state.
		 */
		return SLURM_SUCCESS;
	}

	debug2("wait3 reaped pid %d", *pid);

	return SLURM_SUCCESS;
}

/*
 * Check whether any child processes exited non-zero
 *
 * IN step - Step record in which we are checking for any ended tasks
 * IN task_offset - task offset used for het jobs
 * OUT ended_task - Pointer to ended task info. Only set if a task ended
 *
 * RET SLURM_SUCCESS or error
 */
static int _check_for_child_non_zero_exit(stepd_step_rec_t *step,
					  uint32_t task_offset,
					  stepd_step_task_info_t **ended_task)
{
	int pid = 0;
	int wstatus = 0;
	struct rusage rusage;

	/*
	 * Read all process changes until we find a child that exited non-zero
	 * or until there are no more changes left to read.
	 */
	while (1) {
		stepd_step_task_info_t *task;

		if (_wait_for_any_child(&pid, &wstatus, &rusage, false)) {
			/*
			 * We are only checking for children who have exited
			 * non-zero in this function. If there are no more
			 * children left, then there aren't any children who can
			 * exit non-zero.
			 */
			if (errno == ECHILD)
				return SLURM_SUCCESS;
			else
				return SLURM_ERROR;
		}

		/* No more process state changes */
		if (pid == 0) {
			return SLURM_SUCCESS;
		}

		if (!(task = job_task_info_by_pid(step, pid)))
			return SLURM_ERROR;

		/* save wstatus and rusage from wait */
		task->estatus = wstatus;
		memcpy(&task->rusage, &rusage, sizeof(rusage));

		/*
		 * Check if a task primary pid exited with
		 * non-zero exit code
		 */
		if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus)) {
			if (!(*ended_task = task)) {
				error("%s: Could not find pid %d in any task",
				      __func__, pid);
				return SLURM_ERROR;
			}

			debug2("pid %d exited non-zero (%d). task %d will now be considered ended",
			       pid, WEXITSTATUS(wstatus),
			       (*ended_task)->gtid + task_offset);

			return SLURM_SUCCESS;
		}
		if (get_log_level() >= LOG_LEVEL_DEBUG2) {
			stepd_step_task_info_t *task = NULL;

			if (!(task = job_task_info_by_pid(step, pid))) {
				debug2("Could not find pid %d in any task",
				       pid);
			} else {
				debug2("Child pid %d for task %d exited without any error codes. Ignoring because --wait-for-children was set",
				       pid, (task->gtid + task_offset));
			}
		}
	}

	return SLURM_SUCCESS;
}

/*
 * Handle SIGCHLD signal events
 *
 * IN child_sig_fd - signalfd with SIGCHLD mask
 * IN step - Step record in which we are checking for any ended tasks
 * IN task_offset - task offset used for het jobs
 * OUT ended_task - Pointer to ended task info. Only set if a task ended
 *
 * RET SLURM_SUCCESS or error
 */
static int _handle_child_signalfd(int child_sig_fd, stepd_step_rec_t *step,
				  uint32_t task_offset,
				  stepd_step_task_info_t **ended_task)

{
	struct signalfd_siginfo fdsi;
	ssize_t r;

	r = read(child_sig_fd, &fdsi, sizeof(fdsi));
	if ((r == -1) && ((errno == EAGAIN) || (errno == EAGAIN))) {
		debug2("%s: read from child_sig_fd would block. go back to poll()",
		       __func__);
		return SLURM_SUCCESS;
	}
	if (r != sizeof(fdsi)) {
		error("Incorrect bytes (%ld) returned by signalfd() fd. Expected %ld bytes",
		      r, sizeof(fdsi));
		return SLURM_ERROR;
	}
	if (!(fdsi.ssi_signo == SIGCHLD)) {
		error("signalfd accepted signal other than SIGCHLD");
		return SLURM_ERROR;
	}

	return _check_for_child_non_zero_exit(step, task_offset, ended_task);
}

/*
 * Handle task cgroup file events
 *
 * IN inotify_fd - inotify instance fd for all tasks
 * IN wd - array of inotify watches
 * IN wd_count - count of inotify watches
 * IN step - Step record in which we are checking for any ended tasks
 * IN task_offset - task offset used for het jobs
 * OUT ended_task - Pointer to ended task info. Only set if a task ended
 *
 * RET SLURM_SUCCESS or error
 */
static int _handle_task_cg_inotify_event(int inotify_fd, int *wd, int wd_count,
					 stepd_step_rec_t *step,
					 uint32_t task_offset,
					 stepd_step_task_info_t **ended_task)
{
	char buf[4096]
		__attribute__((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	ssize_t len;

	while (1) {
		int task_i = -1;
		len = read(inotify_fd, buf, sizeof(buf));
		if ((len == -1) && ((errno == EAGAIN) || (errno == EAGAIN))) {
			debug2("read from inotify_fd would block. go back to poll()");
			return SLURM_SUCCESS;
		}
		if (len == -1) {
			error("Could not read from inotify instance: %m");
			return SLURM_ERROR;
		}
		if (len < 0) {
			break;
		}

		/* Loop over all events in the buffer. */
		for (char *ptr = buf; ptr < (buf + len);
		     ptr += (sizeof(struct inotify_event) + event->len)) {
			event = (const struct inotify_event *) ptr;

			/*
			 * Find which task cg had the inotify event
			 */
			for (int i = 0; i < wd_count; ++i) {
				if (wd[i] == event->wd) {
					task_i = i;
					break;
				}
			}

			if ((task_i < 0) || (task_i >= step->node_tasks)) {
				error("Could not match watch file descriptor from inotify_event");
				return SLURM_ERROR;
			}

			/*
			 * Check if this inotify event was the task cg being
			 * modified to reflect that the task has ended
			 */
			if (_check_if_task_cg_empty(step->task[task_i],
						    task_offset, ended_task)) {
				return SLURM_ERROR;
			}
			if (*ended_task) {
				debug2("cgroup for task id %d is empty",
				       (*ended_task)->gtid + task_offset);
				return SLURM_SUCCESS;
			}
		}
	}

	/* We read all inotify events see any tasks that have ended. */
	return SLURM_SUCCESS;
}

/*
 * Watch and detect these events:
 * - Main task processes (child processes of this process) exiting non-zero
 * - Task cgroups becoming empty
 * - Signal to stop waiting for ended tasks.
 */
static void *_ended_task_cg_monitor(void *arg)
{
	ended_task_monitor_args_t *args = arg;

	stepd_step_rec_t *step = args->step;
	uint32_t task_offset = args->task_offset;
	stepd_step_task_info_t **ended_task = args->ended_task;
	int end_fd = args->end_fd;

	int inotify_fd = -1;
	int *watch_fd;
	struct pollfd fds[3];

	sigset_t mask;
	int child_sig_fd = -1;

	/*
	 * Create an inotify instance which will watch a cgroup file associated
	 * with each task. On any event for each task, the task will then be
	 * checked to see if it still has any processes in it.
	 */
	if ((inotify_fd = inotify_init()) == -1) {
		error("Could not initialize inotify instance: %m");
		return NULL;
	}
	fd_set_nonblocking(inotify_fd);

	watch_fd = xmalloc(step->node_tasks * sizeof(int));

	/* Setup watch for each task */
	for (int i = 0; i < step->node_tasks; i++) {
		bool on_modify = false;
		char *events_file_path;
		stepd_step_task_info_t *task = step->task[i];

		if (task->exited)
			continue;

		if (!(events_file_path = cgroup_g_get_task_empty_event_path(
			      task->gtid + task_offset, &on_modify))) {
			goto cleanup;
		}
		debug2("Adding inotify watch for path \"%s\"",
		       events_file_path);

		watch_fd[i] = inotify_add_watch(inotify_fd, events_file_path,
						IN_MODIFY);

		xfree(events_file_path);

		if (watch_fd[i] == -1) {
			error("Could not add watch to inotify instance: %m");
			goto cleanup;
		}
	}

	/*
	 * Create a signalfd() for any SIGCHLD signals from the main task
	 * processes. This will allow us to check via wait3() if any main task
	 * processes ended non-zero which indicates that the task should be
	 * cleaned up.
	 *
	 * Should already have SIGCHLD block from inherited signal mask from
	 * main thread.
	 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	if ((child_sig_fd = signalfd(-1, &mask, 0)) == -1) {
		error("signalfd() failed: %m");
		goto cleanup;
	}

	/*
	 * Signal parent thread to check for any ended tasks while we were
	 * setting up fd's
	 */
	slurm_mutex_lock(&monitor_setup_lock);
	monitor_setup = true;
	slurm_cond_signal(&monitor_setup_cond);
	slurm_mutex_unlock(&monitor_setup_lock);

	/*
	 * Make sure parent thread is done looking for any ended tasks before we
	 * continue. If it did find any, we'll see data on end_fd and exit. Our
	 * fd's are already setup at this point and will catch anything in case
	 * it happens after the parent thread checks.
	 */
	slurm_mutex_lock(&ended_task_check_lock);
	while (!ended_task_check_complete)
		slurm_cond_wait(&ended_task_check_cond, &ended_task_check_lock);
	slurm_mutex_unlock(&ended_task_check_lock);

	fds[0].fd = end_fd;
	fds[0].events = POLLIN;
	fds[1].fd = child_sig_fd;
	fds[1].events = POLLIN;
	fds[2].fd = inotify_fd;
	fds[2].events = POLLIN;

	while (1) {
		int poll_rc;
		debug2("poll() waiting for task cgroup.events changes and SIGCHLD...");
		poll_rc = poll(fds, 3, -1);
		if (poll_rc == -1) {
			if (errno == EINTR)
				continue;
			error("%s: poll() failed: %m", __func__);
			goto cleanup;
		}
		if (poll_rc == 0) {
			error("%s: poll() timed out: %m", __func__);
			goto cleanup;
		}

		/* end_fd */
		if (fds[0].revents & POLLIN) {
			debug2("end_fd %d received data", end_fd);
			goto cleanup;
		}

		/* child_sig_fd */
		if (fds[1].revents & POLLIN) {
			debug2("child_sig_fd %d received data", child_sig_fd);
			if (_handle_child_signalfd(child_sig_fd, step,
						   task_offset, ended_task)) {
				goto cleanup;
			}
			if (*ended_task)
				goto cleanup;
		}

		/* inotify_fd */
		if (fds[2].revents & POLLIN) {
			debug2("inotify_fd %d received data", inotify_fd);
			if (_handle_task_cg_inotify_event(inotify_fd, watch_fd,
							  step->node_tasks,
							  step, task_offset,
							  ended_task)) {
				goto cleanup;
			}

			if (*ended_task)
				goto cleanup;
		}
	}

cleanup:
	/*
	 * inotify man page:
	 * "When all file descriptors referring to an inotify instance have been
	 * closed ... all associated watches are automatically freed."
	 */
	fd_close(&inotify_fd);
	fd_close(&child_sig_fd);
	xfree(watch_fd);
	return NULL;
}

/*
 * This does a non-blocking check for two things:
 * - Did a child process (main process for a task) exit non-zero?
 * - Are any task cgroups empty?
 *
 * IN step - Step record in which we are checking for any ended tasks
 * IN task_offset - task offset used for het jobs
 * OUT ended_task - Pointer to ended task info. Only set if a task ended
 *
 * RET SLURM_SUCCESS or error
 */
static int _check_for_ended_task(stepd_step_rec_t *step, uint32_t task_offset,
				 stepd_step_task_info_t **ended_task)
{
	if (_check_for_child_non_zero_exit(step, task_offset, ended_task)) {
		return SLURM_ERROR;
	}
	if (*ended_task) {
		return SLURM_SUCCESS;
	}

	if (_check_for_any_empty_task_cg(step, task_offset, ended_task)) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * This function implements the --wait-for-children behavior for srun.
 *
 * This function creates a poll() thread that waits for two events:
 *	- Check for any direct children processes (parent processes of tasks)
 *	  exiting non-zero.
 *	- Check for any changes to the processes in each task cgroup
 *
 * If either of those events happen, *ended_task will be set to the task that
 * was ended, and then function will return. Extra care is taken before/after
 * setting up this poll() thread to ensure that all task endings are accounted
 * for. This means that there is thread communication required to stop the poll
 * if necessary.
 */
extern int proctrack_p_wait_for_any_task(stepd_step_rec_t *step,
					 stepd_step_task_info_t **ended_task,
					 bool block)
{
	int write_rc = 0;
	int end_fd = -1;
	pthread_t ended_task_cg_monitor_tid = 0;
	ended_task_monitor_args_t args = { 0 };
	bool any_task_running = false;
	uint32_t task_offset = 0;

	*ended_task = NULL;

	/*
	 * Check step record for any running tasks. This function does not rely
	 * on wait3 returning ECHLD because it's possible there are still
	 * processes in the cgroup even if all children of this process have
	 * exited. This function depends on the caller properly cleaning up
	 * tasks in the step by setting task->exited to true.
	 */
	for (int i = 0; i < step->node_tasks; i++) {
		if (!step->task[i]->exited) {
			any_task_running = true;
			break;
		}
	}
	if (!any_task_running) {
		errno = ECHILD;
		return SLURM_ERROR;
	}

	if (step->het_job_task_offset != NO_VAL)
		task_offset = step->het_job_task_offset;

	/*
	 * Check if any task has already ended before we start
	 * _ended_task_cg_monitor thread
	 */
	if (_check_for_ended_task(step, task_offset, ended_task)) {
		return SLURM_ERROR;
	}
	if (*ended_task) {
		return (*ended_task)->pid;
	}

	/* match behavior of wait3/waitpid */
	if (!block) {
		return 0;
	}

	/*
	 * Create eventfd that we can use to signal _ended_task_cg_monitor to
	 * end if needed.
	 */
	if ((end_fd = eventfd(0, EFD_SEMAPHORE)) == -1) {
		error("eventfd() failed creating end_fd: %m");
		return SLURM_ERROR;
	}

	args.step = step;
	args.task_offset = task_offset;
	args.ended_task = ended_task;
	args.end_fd = end_fd;

	slurm_thread_create(&ended_task_cg_monitor_tid, _ended_task_cg_monitor,
			    &args);

	/* Wait for monitor to create fd's that listen for ended task events */
	slurm_mutex_lock(&monitor_setup_lock);
	while (!monitor_setup)
		slurm_cond_wait(&monitor_setup_cond, &monitor_setup_lock);
	slurm_mutex_unlock(&monitor_setup_lock);

	/*
	 * Check if any task ended while setting up _ended_task_cg_monitor
	 * thread fd's.
	 */
	if (_check_for_ended_task(step, task_offset, ended_task)) {
		uint32_t inc = 1;

		debug2("Could not check for any tasks ending. Signaling monitor to end.");
		if ((write_rc = write(end_fd, &inc, sizeof(inc)))) {
			debug2("Could not write to end_fd to signal monitor to end, returning without joining.");
		}
	} else if (*ended_task) {
		uint32_t inc = 1;

		debug2("Task id %d ended while monitor was being setup. Signaling monitor to end.",
		       (*ended_task)->gtid + task_offset);
		if ((write_rc = write(end_fd, &inc, sizeof(inc)))) {
			debug2("Could not write to end_fd to signal monitor to end, returning without joining.");
		}
	}

	/* allow monitor thread to poll for ended task events */
	slurm_mutex_lock(&ended_task_check_lock);
	ended_task_check_complete = true;
	slurm_cond_signal(&ended_task_check_cond);
	slurm_mutex_unlock(&ended_task_check_lock);

	/* Wait indefinitely until monitor detects a task that has ended. */
	if (!write_rc)
		slurm_thread_join(ended_task_cg_monitor_tid);

	fd_close(&end_fd);

	if (*ended_task)
		return (*ended_task)->pid;

	return SLURM_ERROR;
}
