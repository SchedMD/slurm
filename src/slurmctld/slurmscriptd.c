/*****************************************************************************\
 *  slurmscriptd.c - Slurm script functions.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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
#include <sys/wait.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"

static int slurmctld_readfd = -1;
static int slurmctld_writefd = -1;
static int slurmscriptd_readfd = -1;
static int slurmscriptd_writefd = -1;
static pid_t slurmscriptd_pid;

static void _slurmscriptd_msg_handler(void)
{
	while (true) {
		;
	}
}

static void _kill_slurmscriptd(void)
{
	int status;

	if (slurmscriptd_pid <= 0) {
		error("%s: slurmscriptd_pid < 0, we don't know the PID of slurmscriptd.",
		      __func__);
		return;
	}

	if (kill(slurmscriptd_pid, SIGTERM) < 0) {
		error("%s: kill failed when trying to SIGTERM slurmscriptd: %m",
		      __func__);
	}
	if (waitpid(slurmscriptd_pid, &status, 0) < 0) {
		if (WIFEXITED(status)) {
			/* Exited normally. */
		} else {
			error("%s: Unable to reap slurmscriptd child process", __func__);
		}
	}
}

extern int slurmscriptd_init(void)
{
	int to_slurmscriptd[2] = {-1, -1};
	int to_slurmctld[2] = {-1, -1};

	if ((pipe(to_slurmscriptd) < 0) || (pipe(to_slurmctld) < 0))
		fatal("%s: pipe failed: %m", __func__);

	slurmctld_readfd = to_slurmctld[0];
	slurmctld_writefd = to_slurmscriptd[1];
	slurmscriptd_readfd = to_slurmscriptd[0];
	slurmscriptd_writefd = to_slurmctld[1];

	slurmscriptd_pid = fork();
	if (slurmscriptd_pid < 0) { /* fork() failed */
		fatal("%s: fork() failed: %m", __func__);
	} else if (slurmscriptd_pid > 0) { /* parent (slurmctld) */
		ssize_t i;
		int rc = SLURM_ERROR, ack;

		/*
		 * Communication between slurmctld and slurmscriptd happens via
		 * the to_slurmscriptd and to_slurmctld pipes.
		 * slurmctld writes data to slurmscriptd with the
		 * to_slurmscriptd pipe and slurmscriptd writes data to
		 * slurmctld with the to_slurmctld pipe.
		 */
		if (close(to_slurmscriptd[0]) < 0) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: Unable to close read to_slurmscriptd in parent: %m",
			      __func__);
		}
		if (close(to_slurmctld[1]) < 0) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: Unable to close write to_slurmctld in parent: %m",
			      __func__);
		}

		/* Test communications with slurmscriptd. */
		i = read(slurmctld_readfd, &rc, sizeof(int));
		if (i < 0) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: Can not read return code from slurmscriptd: %m",
			      __func__);
		} else if (i != sizeof(int)) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: slurmscriptd failed to send return code: %m",
			      __func__);
		}
		if (rc != SLURM_SUCCESS) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: slurmscriptd did not initialize",
			      __func__);
		}
		ack = SLURM_SUCCESS;
		i = write(slurmctld_writefd, &ack, sizeof(int));
		if (i != sizeof(int)) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: failed to send ack to slurmscriptd: %m",
			      __func__);
		}

		debug("slurmctld: slurmscriptd fork()'d and initialized.");
	} else { /* child (slurmscriptd_pid == 0) */
		ssize_t i;
		int rc = SLURM_ERROR, ack;
		/* Close extra fd's. */
		if (close(to_slurmscriptd[1]) < 0) {
			error("%s: slurmscriptd: Unable to close write to_slurmscriptd in child: %m",
			      __func__);
			_exit(1);
		}
		if (close(to_slurmctld[0]) < 0) {
			error("%s: slurmscriptd: Unable to close read to_slurmctld in child: %m",
			      __func__);
			_exit(1);
		}
		/* Test communiations with slurmctld. */
		ack = SLURM_SUCCESS;
		i = write(slurmscriptd_writefd, &ack, sizeof(int));
		if (i != sizeof(int)) {
			error("%s: slurmscriptd: failed to send return code to slurmctld: %m",
			      __func__);
			_exit(1);
		}
		i = read(slurmscriptd_readfd, &rc, sizeof(int));
		if (i < 0) {
			error("%s: slurmscriptd: Can not read ack from slurmctld: %m",
			      __func__);
			_exit(1);
		} else if (i != sizeof(int)) {
			error("%s: slurmscriptd: slurmctld failed to send ack: %m",
			      __func__);
			_exit(1);
		}

		debug("slurmscriptd: Got ack from slurmctld, initialization successful");
		_slurmscriptd_msg_handler();
		/* We never want to return from here, only exit. */
		_exit(0);
	}

	return SLURM_SUCCESS;
}

extern int slurmscriptd_fini(void)
{
	_kill_slurmscriptd();
	(void) close(slurmctld_writefd);
	(void) close(slurmctld_readfd);

	debug("%s complete", __func__);

	return SLURM_SUCCESS;
}
