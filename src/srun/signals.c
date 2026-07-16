/*****************************************************************************\
 *  signals.c - Signal handler control for srun
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <sys/eventfd.h>

#include "src/common/fd.h"
#include "src/common/probes.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"

#include "src/conmgr/conmgr.h"

#include "src/srun/launch.h"
#include "src/srun/opt.h"
#include "src/srun/signals.h"
#include "src/srun/srun_job.h"

pthread_mutex_t srun_sig_forward_lock = PTHREAD_MUTEX_INITIALIZER;
bool srun_sig_forward = false;

pthread_mutex_t srun_destroy_sig_lock = PTHREAD_MUTEX_INITIALIZER;
int srun_destroy_sig = 0;
bool srun_job_complete_recvd = false;

int srun_sig_eventfd = -1;

#define SRUN_SIGNALS \
	X(SIGINT, sigint) \
	X(SIGQUIT, sigquit) \
	X(SIGCONT, sigcont) \
	X(SIGTERM, sigterm) \
	X(SIGHUP, sighup) \
	X(SIGALRM, sigalrm) \
	X(SIGUSR1, sigusr1) \
	X(SIGUSR2, sigusr2) \
	X(SIGPIPE, sigpipe)

extern bool srun_sig_is_handled(int signo)
{
#define X(sig, str) \
	if (signo == sig) \
		return true;
	SRUN_SIGNALS
#undef X
	return false;
}

/*
 * SIGALRM, SIGCONT, and SIGPIPE are excluded on purpose:
 *  - SIGALRM is used internally by srun for the --wait (max_wait) timer.
 *  - SIGCONT's handler is load-bearing for wake-up semantics; ignoring it
 *    is effectively a no-op at the kernel level but we keep it reserved.
 *  - SIGPIPE triggers the I/O teardown path; ignoring it leaves srun with
 *    a dead stdio socket and no cleanup.
 */
extern bool srun_sig_is_ignorable(int signo)
{
	switch (signo) {
	case SIGALRM:
	case SIGCONT:
	case SIGPIPE:
		return false;
	default:
		return srun_sig_is_handled(signo);
	}
}

#define SRUN_SIGINT_TIMEOUT \
	((timespec_t) { \
		.tv_sec = 1, \
	})

#define SIGNAL_EXIT_BASE 128

static void _on_sigprof(conmgr_callback_args_t conmgr_args, void *arg)
{
	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	(void) probe_run(true, NULL, NULL, __func__);
}

static void _handle_intr(srun_job_t *job)
{
	static timespec_t deadline = { 0, 0 };

	if (sropt.quit_on_intr || (timespec_after_deadline(deadline) > 0)) {
		info("sending Ctrl-C to %ps", &job->step_id);
		launch_fwd_signal(SIGINT);
		job_force_termination(job);
	} else {
		if (sropt.disable_status) {
			info("sending Ctrl-C to %ps", &job->step_id);
			launch_fwd_signal(SIGINT);
		} else if (job->state < SRUN_JOB_CANCELLED) {
			info("interrupt (one more within 1 sec to abort)");
			launch_print_status();
		}
		deadline = timespec_add(timespec_now(), SRUN_SIGINT_TIMEOUT);
	}
}

static void _handle_pipe(void)
{
	static int ending = 0;

	if (ending)
		return;
	ending = 1;
	launch_fwd_signal(SIGKILL);
}

static void _forward_signal(int signo)
{
	if (sropt.ignore_signals & ((uint64_t) 1 << signo)) {
		debug("Ignoring signal %s as requested by --ignore-signals",
		      sig_num2name(signo));
		return;
	}

	switch (signo) {
	case SIGINT:
		slurm_mutex_lock(&srun_first_job_lock);
		_handle_intr(srun_first_job);
		slurm_mutex_unlock(&srun_first_job_lock);
		break;
	case SIGQUIT:
		info("Quit");
		/* continue with slurm_step_launch_abort */
	case SIGTERM:
	case SIGHUP:
		/* No need to call job_force_termination here since we
		 * are ending the job now and we don't need to update
		 * the state. */
		info("forcing job termination");
		launch_fwd_signal(SIGKILL);
		break;
	case SIGCONT:
		info("got SIGCONT");
		break;
	case SIGPIPE:
		_handle_pipe();
		break;
	case SIGALRM:
		slurm_mutex_lock(&srun_max_timer_lock);
		if (srun_max_timer) {
			info("First task exited %ds ago", sropt.max_wait);
			launch_print_status();
			launch_step_terminate();
		}
		slurm_mutex_unlock(&srun_max_timer_lock);
		break;
	default:
		launch_fwd_signal(signo);
		break;
	}
}

static void _on_signal(int signo)
{
	uint64_t val = 1;
	int write_rc = SLURM_ERROR;

	/* Forward signal to job if it is possibly running now */
	slurm_mutex_lock(&srun_sig_forward_lock);
	if (srun_sig_forward) {
		_forward_signal(signo);
		slurm_mutex_unlock(&srun_sig_forward_lock);
		return;
	}
	slurm_mutex_unlock(&srun_sig_forward_lock);

	/*
	 * When not forwarding signals to a running job, all signals in
	 * SRUN_SIGNALS except for SIGCONT indicate completion.
	 */
	if (signo != SIGCONT) {
		slurm_mutex_lock(&srun_destroy_sig_lock);
		srun_destroy_sig = signo;
		slurm_mutex_unlock(&srun_destroy_sig_lock);
	}

	/*
	 * Write event on srun_sig_eventfd to wake up any thread that may be
	 * running poll() on it. This must be done after writing
	 * srun_destroy_sig.
	 */
	xassert(srun_sig_eventfd != -1);

	safe_write(srun_sig_eventfd, &val, sizeof(uint64_t));
	write_rc = SLURM_SUCCESS;
rwfail:
	if (write_rc != SLURM_SUCCESS)
		error("Failed to write event to srun_sig_eventfd");

	if (signo == SIGCONT)
		return;

	debug("Got signal %d", signo);

	/* Tell slurmctld that job is complete */
	slurm_mutex_lock(&pending_job_id_lock);
	if (pending_job_id.job_id != NO_VAL) {
		slurm_complete_job(&pending_job_id, SIGNAL_EXIT_BASE + signo);
	}
	slurm_mutex_unlock(&pending_job_id_lock);
}

#define DEFINE_SRUN_SIGNAL_WORK(sig, str) \
	static void _on_##str(conmgr_callback_args_t conmgr_args, void *arg) \
	{ \
		if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) { \
			log_flag(CONMGR, "Caught %s, but conmgr work cancelled, ignoring.", \
				 #sig); \
			return; \
		} \
		debug2("Caught signal %d", sig); \
		_on_signal(sig); \
		return; \
	}

#define X(sig, str) DEFINE_SRUN_SIGNAL_WORK(sig, str)
SRUN_SIGNALS
#undef X

extern void srun_sig_init(void)
{
	if ((srun_sig_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) == -1) {
		fatal("Could not create eventfd for srun signal handling: %m");
	}

#define X(sig, str) conmgr_add_work_signal(sig, _on_##str, NULL);
	SRUN_SIGNALS
#undef X

	conmgr_add_work_signal(SIGPROF, _on_sigprof, NULL);

	return;
}
