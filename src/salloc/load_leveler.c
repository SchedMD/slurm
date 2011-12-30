/*****************************************************************************\
 *  load_leveler.c - LoadLeveler lacks the ability to spawn an interactive
 *  job like SLURM. The following functions provide an interface between an
 *  salloc front-end process and a back-end process spawned as a batch job.
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
#  ifdef HAVE_PTY_H
#    include <pty.h>
#  endif
#endif

#include <signal.h>
#include <stdlib.h>
#include <utmp.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/hostlist.h"
#include "src/common/jobacct_common.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifdef USE_LOADLEVELER

/* PTY_MODE indicates if the salloc back-end is to spawn its task using a
 * psuedo-terminate for stdio/out/err. If set, then stdout and stderr are
 * combined into a single data stream, but the output is flushed regularly.
 * If not set, then I/O may not be flushed very regularly. We might want this
 * to be configurable by job. */
#define PTY_MODE true

/* Set this to generate debugging information for salloc front-end/back-end
 *	program communications */
#define _DEBUG_SALLOC 1

/* Timeout for salloc front-end/back-end messages in usec */
#define MSG_TIMEOUT 5000000

#define OP_CODE_EXIT 0x0101
#define OP_CODE_EXEC 0x0102

typedef struct salloc_child_wait_data {
	int dummy_pipe;
	bool *job_fini_ptr;
	pid_t pid;
	slurm_fd_t signal_socket;
	int *status_ptr;
} salloc_child_wait_data_t;

/*****************************************************************************\
 * Local helper functions for salloc front-end/back-end support
 * NOTE: These functions are needed even without llapi.h
\*****************************************************************************/

/*
 * Socket connection authentication logic
 */
static bool _xmit_resp(slurm_fd_t socket_conn, uint32_t resp_auth_key,
		      uint32_t new_auth_key, uint16_t new_port)
{
	int i, buf_len;
	char *buf_head;
	Buf buf;

	if (!(buf = init_buf(64)))
		fatal("init_buf(), malloc failure");

	pack32(resp_auth_key, buf);
	pack32(new_auth_key, buf);
	pack16(new_port, buf);

	buf_len = get_buf_offset(buf);
	buf_head = get_buf_data(buf);
	i = slurm_write_stream_timeout(socket_conn, buf_head, buf_len,
				       MSG_TIMEOUT);
	free_buf(buf);
	if ((i < 0) || (i <  sizeof(buf_len))) {
		error("xmit_resp write: %m");
		return false;
	}

	return true;
}

static bool _validate_connect(slurm_fd_t socket_conn, uint32_t auth_key)
{
	struct timeval tv;
	fd_set read_fds;
	uint32_t read_key;
	bool valid = false;
	int i, n_fds;

	n_fds = socket_conn;
	while (1) {
		FD_ZERO(&read_fds);
		FD_SET(socket_conn, &read_fds);

		tv.tv_sec = 2;
		tv.tv_usec = 0;
		i = select((n_fds + 1), &read_fds, NULL, NULL, &tv);
		if (i == 0)
			break;
		if (i < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		i = slurm_read_stream(socket_conn, (char *)&read_key,
				      sizeof(read_key));
		if ((i == sizeof(read_key)) && (read_key == auth_key))
			valid = true;
		break;
	}

	return valid;
}

/* Process incoming requests
 * resp_socket IN - socket to read from
 * auth_key IN - authentication key we are looking for
 * RETURN true to terminate
 */
static bool _be_proc_comm(slurm_fd_t resp_socket, uint32_t auth_key)
{
	uint16_t op_code, msg_size;
	char *msg;
	int i;

	if (!_validate_connect(resp_socket, auth_key))
		return false;
	i = slurm_read_stream(resp_socket, (char *)&op_code, sizeof(op_code));
	if (i != sizeof(op_code)) {
		error("socket read, bad op_code size: %d", i);
		return false;
	}
	if (op_code == OP_CODE_EXIT)
		return true;
	if (op_code == OP_CODE_EXEC) {
		i = slurm_read_stream(resp_socket, (char *)&msg_size,
				      sizeof(msg_size));
		if (i != sizeof(msg_size)) {
			error("socket read, bad msg_size size: %d", i);
			return false;
		}
		msg = xmalloc(msg_size);
		i = slurm_read_stream(resp_socket, msg, msg_size);
		if (i != sizeof(msg_size)) {
			error("socket read, bad msg size: %d", i);
			xfree(msg);
			return false;
		}
/* FIXME: Do fork/exec here */
info("msg: %s", msg);
		xfree(msg);
		return false;
	}
	error("socket read, bad op_code: %hu", op_code);
	return false;
}

/* salloc front-end function, read message from local stdin and write it to
 *	stdin_socket socket.
 * stdin_fd IN - the local stdin file descriptor to read message from
 * stdin_socket IN - the socket to write the message to
 * RETURN true on EOF
 */
static bool _fe_proc_stdin(slurm_fd_t stdin_fd, slurm_fd_t stdin_socket)
{
	char buf[16 * 1024];
	int buf_len, in_len;
	uint32_t msg_len = 0;

	in_len = read(stdin_fd, buf, (sizeof(buf) - 1));
	if (in_len < 0) {
		error("stdin read: %m");
		return false;
	}
	if (in_len == 0) {
#if _DEBUG_SALLOC
		info("stdin EOF");
#endif
		msg_len = NO_VAL;
	} else {
		msg_len = in_len;
	}

	buf_len = slurm_write_stream_timeout(stdin_socket, (char *)&msg_len,
					     sizeof(msg_len), MSG_TIMEOUT);
	/* NOTE: Do not change test below
	 * (-1 < sizeof(msg_len)) is false since
	 * -1 gets converted to unsigned long first */
	if ((buf_len < 0) || (buf_len < sizeof(msg_len))) {
		error("stdin write: %m");
		return false;
	}
	if (msg_len == NO_VAL)
		return true;

	buf_len = slurm_write_stream_timeout(stdin_socket, buf, msg_len,
					     MSG_TIMEOUT);
	if ((buf_len < 0) || (buf_len < msg_len)) {
		error("stdin write: %m");
	} else {
#if _DEBUG_SALLOC
		buf[msg_len] = '\0';
		info("stdin:%s:%d", buf, msg_len);
#endif
	}
	return false;
}

/* write the exit status of spawned back-end process to the salloc front-end */
static void _be_proc_status(int status, slurm_fd_t signal_socket)
{
	uint32_t status_32;

	status_32 = (uint32_t) status;
	if (slurm_write_stream(signal_socket, (char *)&status_32,
			       sizeof(status_32)) < 0) {
		error("slurm_write_stream(exit_status): %m");
	}
}

/* Thread spawned by _wait_be_func(). See that function for details. */
static void *_wait_be_thread(void *x)
{
	salloc_child_wait_data_t *thread_data = (salloc_child_wait_data_t *)x;

	waitpid(thread_data->pid, thread_data->status_ptr, 0);
	_be_proc_status(*(thread_data->status_ptr), thread_data->signal_socket);
	*(thread_data->job_fini_ptr) = true;
	while (write(thread_data->dummy_pipe, "", 1) == -1) {
		if ((errno == EAGAIN) || (errno == EINTR))
			continue;
		error("write(dummy_pipe): %m");
		break;
	}

	return NULL;
}

/*
 * Wait for back-end process completion and send exit code to front-end
 * pid IN - process ID to wait for
 * signal_socket IN - socket used to transmit exit code
 * status_ptr IN - pointer to place for recording process exit status
 * job_fini_ptr IN - flag to set upon job completion
 * dummy_pipe IN - file just used to wake main process
 * RETURN - ID of spawned thread
 */
static pthread_t _wait_be_func(pid_t pid, slurm_fd_t signal_socket,
			       int *status_ptr, bool *job_fini_ptr,
			       int dummy_pipe)
{
	static salloc_child_wait_data_t thread_data;
	pthread_attr_t thread_attr;
	pthread_t thread_id = 0;

	slurm_attr_init(&thread_attr);
	thread_data.dummy_pipe = dummy_pipe;
	thread_data.job_fini_ptr = job_fini_ptr;
	thread_data.pid = pid;
	thread_data.signal_socket = signal_socket;
	thread_data.status_ptr = status_ptr;
	if (pthread_create(&thread_id, &thread_attr, _wait_be_thread,
                           &thread_data)) {
		error("pthread_create: %m");
	}
	slurm_attr_destroy(&thread_attr);
	return thread_id;
}

/*****************************************************************************\
 * LoadLeveler lacks the ability to spawn an interactive job like SLURM.
 * The following functions provide an interface between an salloc front-end
 * process and a back-end process spawned as a batch job.
\*****************************************************************************/


/*
 * salloc_front_end - Open socket connections to communicate with a remote
 *	node process and build a batch script to submit.
 *
 * RETURN - remote processes exit code or -1 if some internal error
 */
extern char *salloc_front_end (void)
{
	char hostname[256];
	uint16_t comm_port;
	slurm_addr_t comm_addr;
	slurm_fd_t comm_socket = -1;
	char *exec_line = NULL;

	/* Open socket for back-end program to communicate with */
	if ((comm_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		goto fini;
	}
	if (slurm_get_stream_addr(comm_socket, &comm_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		goto fini;
	}
	comm_port = ntohs(((struct sockaddr_in) comm_addr).sin_port);
/* FIXME: Need to spawn a thread to read msg and set SALLOC_BE_HOST/PORT */

	exec_line = xstrdup("#!/bin/bash\n");
	if (gethostname_short(hostname, sizeof(hostname)))
		fatal("gethostname_short(): %m");
	xstrfmtcat(exec_line, "%s/bin/salloc --salloc-be %s %hu\n",
		   SLURM_PREFIX, hostname, comm_port);

fini:
	return exec_line;
}

/*
 * salloc_back_end - Open socket connections with the salloc or srun command
 *	that submitted this program as a LoadLeveler batch job and use that to
 *	spawn other jobs (specificially, spawn poe for srun wrapper)
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. salloc)
 *	     [1]:  "--salloc-be" (argument to spawn salloc backend)
 *	     [2]:  Hostname or address of front-end
 *	     [3]:  Port number for communications
 * RETURN - remote processes exit code
 */
extern int salloc_back_end (int argc, char **argv)
{
	char *host = NULL;
	uint16_t comm_port = 0, resp_port = 0;
	slurm_addr_t comm_addr, resp_addr;
	slurm_fd_t comm_socket = SLURM_SOCKET_ERROR;
	slurm_fd_t resp_socket = SLURM_SOCKET_ERROR;
	fd_set read_fds;
	int i, n_fds;
	uint32_t new_auth_key, resp_auth_key;

	if (argc >= 4) {
		host   = argv[2];
		resp_port = atoi(argv[3]);
	}
	if ((argc < 4) || (resp_port == 0)) {
		error("Usage: salloc --salloc-be <salloc_host> "
		      "<salloc_stdin/out_port>\n");
		return 1;
	}
	resp_auth_key = resp_port + getuid();

	/* Open sockets for back-end program to communicate with */
	/* Socket for stdin/stdout */
	if ((comm_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		goto fini;
	}
	if (slurm_get_stream_addr(comm_socket, &comm_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		goto fini;
	}
	comm_port = ntohs(((struct sockaddr_in) comm_addr).sin_port);
	new_auth_key = comm_port + getuid();

	slurm_set_addr(&resp_addr, resp_port, host);
	resp_socket = slurm_open_stream(&resp_addr);
	if (resp_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, resp_port);
		return 1;
	}
	_xmit_resp(resp_socket, resp_auth_key, new_auth_key, comm_port);

	n_fds = resp_socket;
	while (true) {
		FD_ZERO(&read_fds);
		FD_SET(resp_socket, &read_fds);

		i = select((n_fds + 1), &read_fds, NULL, NULL, NULL);
		if (i == -1) {
			if (errno == EINTR)
				continue;
			error("select: %m");
			break;
		}
		if (FD_ISSET(resp_socket, &read_fds) &&
		    _be_proc_comm(resp_socket, new_auth_key)) {
			/* Remote resp_socket closed */
			break;
		}
	}

fini:	if (comm_socket >= 0)
		slurm_shutdown_msg_engine(comm_socket);
	if (resp_socket >= 0)
		slurm_shutdown_msg_engine(resp_socket);
	exit(0);
}

#endif	/* USE_LOADLEVELER */
