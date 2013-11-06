/*****************************************************************************\
 **  mpi_mpich1_p4.c - Library routines for initiating jobs on with mpich1_p4
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "slurm/slurm_errno.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/mpi.h"
#include "src/common/net.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "mpi MPICH1_P4 plugin";
const char plugin_type[]        = "mpi/mpich1_p4";
const uint32_t plugin_version   = 100;

/* communication for master port info */
pthread_t p4_tid = (pthread_t) -1;
int p4_fd1 = -1, p4_fd2 = -1;

/*
 * These vars are used to break the mpi thread out of a poll call, exit,
 * and allow the main thread to do a timed wait for that exit
 */
static int  shutdown_pipe[2];
static bool shutdown_complete;  /* Set true when mpi thr about to exit */
static int  shutdown_timeout;   /* Num secs for main thread to wait for
				   mpi thread to finish */
static pthread_mutex_t shutdown_lock;
static pthread_cond_t  shutdown_cond;


int p_mpi_hook_slurmstepd_prefork(const stepd_step_rec_t *job, char ***env)
{
	debug("mpi/mpich1_p4: slurmstepd prefork");
	return SLURM_SUCCESS;
}

int p_mpi_hook_slurmstepd_task (const mpi_plugin_task_info_t *job,
				char ***env)
{
	char *nodelist, *task_cnt;

	nodelist = getenvp(*env, "SLURM_NODELIST");
	if (nodelist) {
		char *host_str = NULL, *tmp;
		hostlist_t hl = hostlist_create(nodelist);
		while ((tmp = hostlist_shift(hl))) {
			if (host_str)
				xstrcat(host_str, ",");
			xstrcat(host_str, tmp);
			free(tmp);
		}
		hostlist_destroy(hl);
		env_array_overwrite_fmt(env, "SLURM_MPICH_NODELIST", "%s",
			host_str);
		xfree(host_str);
	}

	task_cnt = getenvp(*env, "SLURM_TASKS_PER_NODE");
	if (task_cnt) {
		char *task_str = NULL, tmp_str[32];
		int i=0, val, reps;
		while (task_cnt[i]) {
			if ((task_cnt[i] >= '0') && (task_cnt[i] <= '9'))
				val = atoi(&task_cnt[i]);
			else
				break;	/* bad parse */
			i++;
			while (task_cnt[i]
			&&     (task_cnt[i] != 'x') && (task_cnt[i] != ','))
				i++;
			if (task_cnt[i] == 'x') {
				i++;
				reps = atoi(&task_cnt[i]);
				while (task_cnt[i] && (task_cnt[i] != ','))
					i++;
			} else
				reps = 1;
			if (task_cnt[i] == ',')
				i++;
			while (reps) {
				if (task_str)
					xstrcat(task_str, ",");
				snprintf(tmp_str, sizeof(tmp_str), "%d", val);
				xstrcat(task_str, tmp_str);
				reps--;
			}
		}
		env_array_overwrite_fmt(env, "SLURM_MPICH_TASKS", "%s",
			task_str);
		xfree(task_str);
	}

	return SLURM_SUCCESS;
}

static void *mpich1_thr(void *arg)
{
	int cc, flags;
	int new_port, new_fd;
	struct pollfd ufds[2];
	struct sockaddr cli_addr;
	socklen_t cli_len;
	char in_buf[128];
	debug("waiting for p4 communication");
	if ((flags = fcntl(p4_fd1, F_GETFL)) < 0) {
		error("mpich_p4: fcntl: %m");
		goto done;
	}
	if (fcntl(p4_fd1, F_SETFL, flags | O_NONBLOCK) < 0) {
		error("mpich_p4: fcntl: %m");
		goto done;
	}
	ufds[0].fd = p4_fd1;
	ufds[0].events = POLLIN;
	ufds[1].fd = shutdown_pipe[0];
	ufds[1].events = POLLIN;

	while (1) {
		if (p4_tid == (pthread_t) -1)
			goto done;
		cc = read(p4_fd1, &new_port, sizeof(new_port));
		if (cc >= 0)
			break;
		if (errno != EAGAIN) {
			error("mpich_p4: read/1: %m");
			goto done;
		}
		cc = poll(ufds, 2, 10000);
		if (cc <= 0) {
			error("mpich_p4: poll/1: %m");
			goto done;
		}
		if (ufds[1].revents & POLLIN) {
			goto done;
		}
	}
	if (cc != sizeof(new_port)) {
		error("mpich_p4: read/1 %d bytes", cc);
		goto done;
	}
	debug("mpich_p4 read/1 port %d", new_port);

	ufds[0].fd = p4_fd2;

	/* send this port number to other tasks on demand */
	while (1) {
		if (p4_tid == (pthread_t) -1)
			goto done;

		cc = poll(ufds, 2, -1);
		if (cc <= 0) {
			error("mpich_p4: poll/2: %m");
			goto done;
		}
		if (ufds[1].revents & POLLIN) {
			goto done;
		}

		new_fd = accept(p4_fd2, &cli_addr, &cli_len);
		if (new_fd < 0)
			continue;
		cc = read(new_fd, in_buf, sizeof(in_buf));
		if (cc > 0)
			debug("mpich_p4 read/2 port: %s", in_buf);
		cc = write(new_fd, &new_port, sizeof(new_port));
		if (cc < sizeof(new_port))
			error("mpich_p4: write2: %m");
		close(new_fd);
	}

done:
	pthread_mutex_lock(&shutdown_lock);
	shutdown_complete = true;
	pthread_cond_signal(&shutdown_cond);
	pthread_mutex_unlock(&shutdown_lock);
	return NULL;
}

mpi_plugin_client_state_t *
p_mpi_hook_client_prelaunch(mpi_plugin_client_info_t *job, char ***env)
{
	struct sockaddr_in sin;
	pthread_attr_t attr;
	socklen_t len = sizeof(sin);
	short port1, port2;

	debug("Using mpi/mpich1_p4");
	if ((p4_fd1 = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		error("socket: %m");
		return NULL;
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = PF_INET;
	if (bind(p4_fd1, (struct sockaddr *) &sin, len) < 0) {
		error("bind: %m");
		return NULL;
	}
	if (getsockname(p4_fd1, (struct sockaddr *) &sin, &len) < 0) {
		error("getsockname: %m");
		return NULL;
	}
	port1 = ntohs(sin.sin_port);

	if ((p4_fd2 = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		error("socket: %m");
		return NULL;
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(p4_fd2, (struct sockaddr *) &sin, len) < 0) {
		error("bind: %m");
		return NULL;
	}
	if (listen(p4_fd2, 64) < 0)
		error("listen: %m");
	if (getsockname(p4_fd2, (struct sockaddr *) &sin, &len) < 0) {
		error("getsockname: %m");
		return NULL;
	}
	port2 = ntohs(sin.sin_port);

	if (pipe(shutdown_pipe) < 0) {
		error ("pipe: %m");
		return (NULL);
	}
	shutdown_complete = false;
	shutdown_timeout = 5;
	slurm_mutex_init(&shutdown_lock);
	pthread_cond_init(&shutdown_cond, NULL);

	/* Process messages in a separate thread */
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&p4_tid, &attr, &mpich1_thr, NULL)) {
		error("pthread_create: %m");
		slurm_attr_destroy(&attr);
		return NULL;
	}
	slurm_attr_destroy(&attr);
	env_array_overwrite_fmt(env, "SLURM_MPICH_PORT1", "%hu", port1);
	env_array_overwrite_fmt(env, "SLURM_MPICH_PORT2", "%hu", port2);
	debug("mpich_p4 plugin listening on fd=%d,%d ports=%d,%d",
		p4_fd1, p4_fd2, port1, port2);

	/* only return NULL on error */
	return (void *)0xdeadbeef;
}

int p_mpi_hook_client_single_task_per_node(void)
{
	return true;
}

int p_mpi_hook_client_fini(mpi_plugin_client_state_t *state)
{
	if (p4_tid != (pthread_t)-1) {
		char tmp = 1;
		int n;

		/*
		 * Write to the pipe to break the mpi thread out of a poll
		 * (or leave the poll immediately after it is called) and exit.
		 * Do a timed wait for the mpi thread to shut down, or just
		 * exit if the mpi thread cannot respond.
		 */
		n = write(shutdown_pipe[1], &tmp, 1);
		if (n == 1) {
			struct timespec ts = {0, 0};

			slurm_mutex_lock(&shutdown_lock);
			ts.tv_sec = time(NULL) + shutdown_timeout;

			while (!shutdown_complete) {
				if (time(NULL) >= ts.tv_sec) {
					break;
				}
				pthread_cond_timedwait(
					&shutdown_cond,
					&shutdown_lock, &ts);
			}
			slurm_mutex_unlock(&shutdown_lock);
		}
		if (shutdown_complete) {
			close(shutdown_pipe[0]);
			close(shutdown_pipe[1]);

			slurm_mutex_destroy(&shutdown_lock);
			pthread_cond_destroy(&shutdown_cond);
		}
		p4_tid = (pthread_t) -1;
	}
	return SLURM_SUCCESS;
}
