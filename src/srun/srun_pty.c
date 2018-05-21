/*****************************************************************************\
 *  src/srun/srun_pty.c - pty handling for srun
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette  <jette1@llnl.gov>
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

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"

#include "opt.h"
#include "srun_job.h"
#include "srun_pty.h"

/*  Processed by pty_thr() */
static int pty_sigarray[] = { SIGWINCH, 0 };
static int winch;

/*
 * Static prototypes
 */
static void   _handle_sigwinch(int sig);
static void * _pty_thread(void *arg);

/* Set pty window size in job structure
 * RET 0 on success, -1 on error */
int set_winsize(srun_job_t *job)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) {
		error("ioctl(TIOCGWINSZ): %m");
		return -1;
	}

	job->ws_row = ws.ws_row;
	job->ws_col = ws.ws_col;
	debug2("winsize %u:%u", job->ws_row, job->ws_col);
	return 0;
}

/* SIGWINCH should already be blocked by srun/libsrun/srun_job.c */
void block_sigwinch(void)
{
	xsignal_block(pty_sigarray);
}

void pty_thread_create(srun_job_t *job)
{
	slurm_addr_t pty_addr;
	uint16_t *ports;

	if ((ports = slurm_get_srun_port_range()))
		job->pty_fd = slurm_init_msg_engine_ports(ports);
	else
		job->pty_fd = slurm_init_msg_engine_port(0);

	if (job->pty_fd < 0) {
		error("init_msg_engine_port: %m");
		return;
	}
	if (slurm_get_stream_addr(job->pty_fd, &pty_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		return;
	}
	job->pty_port = ntohs(((struct sockaddr_in) pty_addr).sin_port);
	debug2("initialized job control port %hu", job->pty_port);

	slurm_thread_create_detached(NULL, _pty_thread, job);
}

static void  _handle_sigwinch(int sig)
{
	winch = 1;
	xsignal(SIGWINCH, _handle_sigwinch);
}

static void _notify_winsize_change(int fd, srun_job_t *job)
{
	pty_winsz_t winsz;
	int len;
	char buf[4];

	if (fd < 0) {
		error("pty: no file to write window size changes to");
		return;
	}

	winsz.cols = htons(job->ws_col);
	winsz.rows = htons(job->ws_row);
	memcpy(buf, &winsz.cols, 2);
	memcpy(buf+2, &winsz.rows, 2);
	len = slurm_write_stream(fd, buf, 4);
	if (len < sizeof(winsz))
		error("pty: window size change notification error: %m");
}

static void *_pty_thread(void *arg)
{
	int fd = -1;
	srun_job_t *job = (srun_job_t *) arg;
	slurm_addr_t client_addr;

	xsignal_unblock(pty_sigarray);
	xsignal(SIGWINCH, _handle_sigwinch);

	if ((fd = slurm_accept_msg_conn(job->pty_fd, &client_addr)) < 0) {
		error("pty: accept failure: %m");
		return NULL;
	}

	while (job->state <= SRUN_JOB_RUNNING) {
		debug2("waiting for SIGWINCH");
		if ((poll(NULL, 0, -1) < 1) && (errno != EINTR)) {
			debug("%s: poll error %m", __func__);
			continue;
		}
		if (winch) {
			set_winsize(job);
			_notify_winsize_change(fd, job);
		}
		winch = 0;
	}
	close(fd);
	return NULL;
}
