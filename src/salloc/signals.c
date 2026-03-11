/*****************************************************************************\
 *  signals.c - Signal handler control for salloc
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

#include <signal.h>
#include <sys/eventfd.h>

#include "src/common/fd.h"
#include "src/common/read_config.h"
#include "src/conmgr/conmgr.h"
#include "src/salloc/salloc.h"
#include "src/salloc/signals.h"

pthread_mutex_t salloc_destroy_sig_lock = PTHREAD_MUTEX_INITIALIZER;
int salloc_destroy_sig = 0;

int salloc_sig_eventfd = -1;

#define SALLOC_SIGNALS \
	X(SIGHUP, sighup) \
	X(SIGINT, sigint) \
	X(SIGQUIT, sigquit) \
	X(SIGPIPE, sigpipe) \
	X(SIGTERM, sigterm) \
	X(SIGUSR1, sigusr1) \
	X(SIGUSR2, sigusr2)

#define SIGNAL_EXIT_BASE 128

static void _on_signal(int signo)
{
	uint64_t val = 1;
	int write_rc = SLURM_ERROR;
	int tmp_command_pid = -1;

	slurm_mutex_lock(&salloc_destroy_sig_lock);
	salloc_destroy_sig = signo;
	slurm_mutex_unlock(&salloc_destroy_sig_lock);

	/*
	 * Write event on salloc_sig_eventfd to wake up any thread that may be
	 * running poll() on it. This must be done after writing
	 * salloc_destroy_sig.
	 */
	xassert(salloc_sig_eventfd != -1);

	safe_write(salloc_sig_eventfd, &val, sizeof(uint64_t));
	write_rc = SLURM_SUCCESS;
rwfail:
	if (write_rc != SLURM_SUCCESS)
		error("Failed to write event to salloc_sig_eventfd");

	debug("Got signal %d", signo);

	slurm_mutex_lock(&command_pid_lock);
	tmp_command_pid = command_pid;
	slurm_mutex_unlock(&command_pid_lock);

	/* Special handling for SIGHUP after command is started */
	if ((tmp_command_pid > 0) && (signo == SIGHUP)) {
		salloc_sig_forward(signo);

		exit_flag = true;
		return;
	}

	/* Tell slurmctld that job is complete */
	if (my_job_id.job_id != NO_VAL) {
		slurm_complete_job(&my_job_id, SIGNAL_EXIT_BASE + signo);
	}
}

#define DEFINE_SALLOC_SIGNAL_WORK(sig, str) \
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

#define X(sig, str) DEFINE_SALLOC_SIGNAL_WORK(sig, str)
SALLOC_SIGNALS
#undef X

extern void salloc_sig_init(void)
{
	if ((salloc_sig_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) ==
	    -1) {
		fatal("Could not create eventfd for salloc signal handling: %m");
	}

#define X(sig, str) conmgr_add_work_signal(sig, _on_##str, NULL);
	SALLOC_SIGNALS
#undef X

	return;
}

extern void salloc_sig_forward(int signo)
{
	int tmp_command_pid = 0;

	slurm_mutex_lock(&command_pid_lock);
	tmp_command_pid = command_pid;
	slurm_mutex_unlock(&command_pid_lock);

	if (tmp_command_pid > 0)
		killpg(tmp_command_pid, signo);
}
