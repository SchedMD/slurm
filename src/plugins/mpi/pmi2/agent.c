/*****************************************************************************\
 **  agent.c - PMI2 handling thread
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
 *  Written by Artem Y. Polyakov <artemp@mellanox.com>.
 *  All rights reserved.
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

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <poll.h>

#include "src/common/slurm_xlator.h"
#include "src/common/eio.h"
#include "src/common/macros.h"
#include "src/common/slurm_mpi.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "client.h"
#include "pmi.h"
#include "setup.h"

static int *initialized = NULL;
static int *finalized = NULL;

static eio_handle_t *pmi2_handle;
static pthread_mutex_t agent_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t agent_running_cond = PTHREAD_COND_INITIALIZER;
static pthread_t _agent_tid = 0;

static bool _tree_listen_readable(eio_obj_t *obj);
static int  _tree_listen_read(eio_obj_t *obj, List objs);
static struct io_operations tree_listen_ops = {
.readable    = &_tree_listen_readable,
.handle_read = &_tree_listen_read,
};

static bool _task_readable(eio_obj_t *obj);
static int  _task_read(eio_obj_t *obj, List objs);
/* static bool _task_writable(eio_obj_t *obj); */
/* static int  _task_write(eio_obj_t *obj, List objs); */
static struct io_operations task_ops = {
.readable    =  &_task_readable,
.handle_read =  &_task_read,
};


static int _handle_pmi1_init(int fd, int lrank);

/*********************************************************************/

static int
_handle_task_request(int fd, int lrank)
{
	int rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: in _handle_task_request, lrank=%d", lrank);

	if (initialized[lrank] == 0) {
		rc = _handle_pmi1_init(fd, lrank);
		initialized[lrank] = 1;
	} else if (is_pmi11()) {
		rc = handle_pmi1_cmd(fd, lrank);
	} else if (is_pmi20()) {
		rc = handle_pmi2_cmd(fd, lrank);
	} else {
		fatal("this is impossible");
	}
	return rc;
}

static int
_handle_tree_request(int fd)
{
	uint32_t temp;
	int rc = SLURM_SUCCESS;

	if (in_stepd()) {	/* skip uid passed from slurmd */
		safe_read(fd, &temp, sizeof(uint32_t));
		temp = ntohl(temp);
		debug3("mpi/pmi2: _handle_tree_request: req from uid %u", temp);
	}
	rc = handle_tree_cmd(fd);
	return rc;
rwfail:
	return SLURM_ERROR;
}

/*********************************************************************/

static bool
_is_fd_ready(int fd)
{
	struct pollfd pfd[1];
	int    rc;

	pfd[0].fd     = fd;
	pfd[0].events = POLLIN;

	rc = poll(pfd, 1, 10);

	return ((rc == 1) && (pfd[0].revents & POLLIN));
}

static bool
_tree_listen_readable(eio_obj_t *obj)
{
	debug2("mpi/pmi2: _tree_listen_readable");
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			close(obj->fd);
			obj->fd = -1;
		}
		debug2("    false, shutdown");
		return false;
	}
	return true;
}

static int
_tree_listen_read(eio_obj_t *obj, List objs)
{
	int sd;
	slurm_addr_t addr;
	socklen_t size = sizeof(addr);

	debug2("mpi/pmi2: _tree_listen_read");

	while (1) {
		/*
		 * Return early if fd is not now ready
		 */
		if (!_is_fd_ready(obj->fd))
			return 0;

		while ((sd = accept4(obj->fd, (struct sockaddr *)&addr,
				     &size, SOCK_CLOEXEC)) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)    /* No more connections */
				return 0;
			if ((errno == ECONNABORTED) ||
			    (errno == EWOULDBLOCK)) {
				return 0;
			}
			error("mpi/pmi2: unable to accept new connection: %m");
			return 0;
		}

		if (! in_stepd()) {
			debug3("mpi/pmi2: accepted tree connection: ip=%pA sd=%d",
			       &addr, sd);
		}

		/* read command from socket and handle it */
		_handle_tree_request(sd);
		close(sd);
	}
	return 0;
}

/*********************************************************************/

static bool
_task_readable(eio_obj_t *obj)
{
	int lrank;

	debug2("mpi/pmi2: _task_readable");

	lrank = (int)(long)(obj->arg);
	if (finalized[lrank] == 1) {
		debug2("    false, finalized");
		return false;
	}

	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			close(obj->fd);
			obj->fd = -1;
		}
		debug2("    false, shutdown");
		return false;
	}
	return true;
}

static int
_task_read(eio_obj_t *obj, List objs)
{
	int rc, lrank;

	lrank = (int)(long)(obj->arg);
	rc = _handle_task_request(obj->fd, lrank);

	return rc;
}

/*********************************************************************/

/* the PMI1 init */
static int
_handle_pmi1_init(int fd, int lrank)
{
	char buf[64];
	int version, subversion;
	int n, rc = 0;

	debug3("mpi/pmi2: in _handle_pmi1_init");

	while ( (n = read(fd, buf, 64)) < 0 && errno == EINTR);
	if ((n < 0) || (n >= 64)) {
		error("mpi/pmi2: failed to read PMI1 init command");
		return SLURM_ERROR;
	}
	buf[n] = '\0';

	n = sscanf(buf, "cmd=init pmi_version=%d pmi_subversion=%d\n",
		   &version, &subversion);
	if (n != 2) {
		error("mpi/pmi2: invalid PMI1 init command: `%s'", buf);
		rc = 1;
		version = 2;
		subversion = 0;
		goto send_response;
	}

	rc = set_pmi_version(version, subversion);
	if (rc != SLURM_SUCCESS) {
		get_pmi_version(&version, &subversion);
	} else
		rc = 0;

send_response:
	snprintf(buf, 64, "cmd=response_to_init rc=%d pmi_version=%d "
		 "pmi_subversion=%d\n", rc, version, subversion);

	while ( (n = write(fd, buf, strlen(buf))) < 0 && errno == EINTR);
	if (n < 0) {
		error ("mpi/pmi2: failed to write PMI1 init response");
		return SLURM_ERROR;
	}

	debug3("mpi/pmi2: out _handle_pmi1_init");
	return SLURM_SUCCESS;
}

/*********************************************************************/


/*
 * main loop of agent thread
 */
static void *
_agent(void * unused)
{
	eio_obj_t *tree_listen_obj, *task_obj;
	int i;

	pmi2_handle = eio_handle_create(0);

	//fd_set_nonblocking(tree_sock);
	tree_listen_obj = eio_obj_create(tree_sock, &tree_listen_ops,
					 (void *)(-1));
	eio_new_initial_obj(pmi2_handle, tree_listen_obj);

	/* for stepd, add the sockets to tasks */
	if (in_stepd()) {
		for (i = 0; i < job_info.ltasks; i ++) {
			task_obj = eio_obj_create(STEPD_PMI_SOCK(i), &task_ops,
						  (void*)(long)(i));
			eio_new_initial_obj(pmi2_handle, task_obj);
		}
		initialized = xmalloc(job_info.ltasks * sizeof(int));
		finalized = xmalloc(job_info.ltasks * sizeof(int));
	}

	slurm_mutex_lock(&agent_mutex);
	slurm_cond_signal(&agent_running_cond);
	slurm_mutex_unlock(&agent_mutex);

	eio_handle_mainloop(pmi2_handle);

	debug("mpi/pmi2: agent thread exit");

	eio_handle_destroy(pmi2_handle);

	return NULL;
}

/*
 * start the PMI2 agent thread
 */
extern int
pmi2_start_agent(void)
{
	static bool first = true;

	slurm_mutex_lock(&agent_mutex);
	if (!first) {
		slurm_mutex_unlock(&agent_mutex);
		return SLURM_SUCCESS;
	}
	first = false;

	slurm_thread_create(&_agent_tid, _agent, NULL);

	slurm_cond_wait(&agent_running_cond, &agent_mutex);

	debug("mpi/pmi2: started agent thread");

	slurm_mutex_unlock(&agent_mutex);

	return SLURM_SUCCESS;
}

/*
 * stop the PMI2 agent thread
 */
extern int
pmi2_stop_agent(void)
{
	slurm_mutex_lock(&agent_mutex);

	if (_agent_tid) {
		eio_signal_shutdown(pmi2_handle);
		/* wait for the agent thread to stop */
		pthread_join(_agent_tid, NULL);
		_agent_tid = 0;
	}

	slurm_mutex_unlock(&agent_mutex);

	return SLURM_SUCCESS;
}

extern void
task_finalize(int lrank)
{
	finalized[lrank] = 1;
}
