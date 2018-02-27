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
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#if defined(__FreeBSD__)
#include <sys/socket.h>	/* AF_INET */
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
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
static volatile bool agent_started;
static volatile bool agent_running;
static volatile bool agent_stopped;
static pthread_mutex_t agent_mutex = PTHREAD_MUTEX_INITIALIZER;

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
	struct sockaddr addr;
	struct sockaddr_in *sin;
	socklen_t size = sizeof(addr);
	char buf[INET_ADDRSTRLEN];

	debug2("mpi/pmi2: _tree_listen_read");

	while (1) {
		/*
		 * Return early if fd is not now ready
		 */
		if (!_is_fd_ready(obj->fd))
			return 0;

		while ((sd = accept(obj->fd, &addr, &size)) < 0) {
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
			sin = (struct sockaddr_in *) &addr;
			inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
			debug3("mpi/pmi2: accepted tree connection: ip=%s sd=%d",
			       buf, sd);
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
	eio_handle_t *orig_handle;
	int i;

	slurm_mutex_lock(&agent_mutex);
	agent_running = true;
	slurm_mutex_unlock(&agent_mutex);

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

	eio_handle_mainloop(pmi2_handle);

	debug("mpi/pmi2: agent thread exit");

	slurm_mutex_lock(&agent_mutex);
	agent_running = false;
	orig_handle = pmi2_handle;
	pmi2_handle = NULL;
	slurm_mutex_unlock(&agent_mutex);
	eio_handle_destroy(orig_handle);

	return NULL;
}

static bool _agent_running_test(void)
{
	bool rc;
	slurm_mutex_lock(&agent_mutex);
	rc = agent_running;
	slurm_mutex_unlock(&agent_mutex);
	return rc;
}

/*
 * start the PMI2 agent thread
 */
extern int
pmi2_start_agent(void)
{
	bool is_started;

	slurm_mutex_lock(&agent_mutex);
	is_started = agent_started;
	agent_started = true;
	slurm_mutex_unlock(&agent_mutex);

	if (!is_started) {
		slurm_thread_create_detached(NULL, _agent, NULL);
		debug("mpi/pmi2: started agent thread");
	}

	/* wait for the agent to start */
	while (!_agent_running_test()) {
		sched_yield();
	}

	return SLURM_SUCCESS;
}

/*
 * stop the PMI2 agent thread
 */
extern int
pmi2_stop_agent(void)
{
	bool is_stopped;

	slurm_mutex_lock(&agent_mutex);
	is_stopped = agent_stopped;
	agent_stopped = true;
	slurm_mutex_unlock(&agent_mutex);

	if (!is_stopped && pmi2_handle)
		eio_signal_shutdown(pmi2_handle);

	/* wait for the agent to finish */
	while (_agent_running_test()) {
		sched_yield();
	}

	return SLURM_SUCCESS;
}

extern void
task_finalize(int lrank)
{
	finalized[lrank] = 1;
}
