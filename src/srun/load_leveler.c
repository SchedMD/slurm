/*****************************************************************************\
 *  load_leveler.c - Provide an srun command line interface over LoadLeveler.
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

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utmp.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/jobacct_common.h"
#include "src/common/log.h"
#include "src/common/mpi.h"
#include "src/common/net.h"
#include "src/common/plugstack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/switch.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/srun/opt.h"

#ifdef USE_LOADLEVELER
/* PTY_MODE indicates if the srun back-end is to spawn its task using a
 * psuedo-terminate for stdio/out/err. If set, then stdout and stderr are
 * combined into a single data stream, but the output is flushed regularly.
 * If not set, then I/O may not be flushed very regularly. We might want this
 * to be configurable by job. */
#define PTY_MODE false

/* Set this to generate debugging information for srun front-end/back-end
 *	program communications */
#define _DEBUG_SRUN 1

/* Timeout for srun front-end/back-end messages in usec */
#define MSG_TIMEOUT 5000000

static slurm_fd_t global_signal_conn = SLURM_SOCKET_ERROR;

typedef struct srun_child_wait_data {
	int dummy_pipe;
	bool *job_fini_ptr;
	pid_t pid;
	slurm_fd_t signal_socket;
	int *status_ptr;
} srun_child_wait_data_t;

/*****************************************************************************\
 * Local helper functions for srun front-end/back-end support
 * NOTE: These functions are needed even without llapi.h
\*****************************************************************************/

/* srun back-end function, read message from signal_socket and signal the
 *	specified process
 * signal_socket IN - socket to read message from
 * pid IN - process to be signalled
 * RETURN true on EOF */
static bool _be_proc_signal(slurm_fd_t signal_socket, pid_t pid)
{
	uint32_t sig_num = 0;
	int read_len;
	bool fini_rc = true;

	/* Read and process message header */
	read_len = slurm_read_stream(signal_socket, (char *)&sig_num,
				     sizeof(sig_num));
	if (read_len == -1) {
		error("signal read error: %m");
		/* Error, treat like EOF */
	} else if (read_len == 0) {
		/* EOF */
	} else if (read_len < sizeof(sig_num)) {
		error("signal read header, bad size (%d < %lu)",
		      read_len, sizeof(sig_num));
		/* Can not recover, treat like EOF */
	} else {
		if (kill(pid, (int) sig_num))
			error("signal(%u): %m", sig_num);
		else {
#if _DEBUG_SRUN
			info("signal(%u) sent", sig_num);
#endif
		}
		fini_rc = false;
	}

	return fini_rc;
}

/* srun back-end function, read message from stdin_socket and write to pipe.
 * stdin_pipe IN   - pipe to spawned process, write message here
 * stdin_socket IN - socket to read message from
 * RETURN true on EOF */
static bool _be_proc_stdin(int stdin_pipe, slurm_fd_t stdin_socket)
{
	char *buf;
	uint32_t buf_len = 0;
	int read_len;
	bool fini_rc = true;

	/* Read and process message header */
	read_len = slurm_read_stream(stdin_socket, (char *)&buf_len,
				     sizeof(buf_len));
	if (read_len == -1) {
		error("stdin read header error: %m");
		return fini_rc;	/* Error, treat like EOF */
	}
	if (read_len == 0)
		return fini_rc;	/* Abnornal EOF */
	if (read_len < sizeof(buf_len)) {
		error("stdin read header, bad size (%d < %lu)",
		      read_len, sizeof(buf_len));
		return fini_rc;	/* Can not recover, treat like EOF */
	}
	if (buf_len == NO_VAL) {
#if _DEBUG_SRUN
		info("stdin EOF");
#endif
		return fini_rc;	/* EOF received */
	}

	/* Read and process message data */
	fini_rc = false;
	buf = xmalloc(buf_len+1);
	read_len = slurm_read_stream(stdin_socket, buf, buf_len);
	if (read_len < 0) {
		error("stdin read buffer: %m");
	} else if (read_len < buf_len) {
		error("stdin read short (%d < %d)", read_len, buf_len);
	} else {
		int offset = 0, write_len;
		buf[read_len] = '\0';
#if _DEBUG_SRUN
		info("stdin:%s:%d", buf, read_len);
#endif
		while (offset < read_len) {
			write_len = write(stdin_pipe, buf+offset,
					  read_len-offset);
			if (write_len < 0) {
				if ((errno == EAGAIN) && (errno == EINTR))
					continue;
				error("stdin write: %m");
				fini_rc = false;
				break;
			} else {
				offset += write_len;
			}
		}
	}
	xfree(buf);

	return fini_rc;
}

/* srun back-end function, read message from stdout or stderr pipe and write
 *	it to a socket.
 * stdio_pipe IN   - stdout or stderr pipe to read data from
 * stdio_socket IN - stdout or stderr pipe to write data to
 * stdio_name IN   - "stdout" or "stderr" for logging
 * RETURN true on EOF
 */
static bool _be_proc_stdio(int stdio_pipe, slurm_fd_t stdio_socket,
			   char *stdio_name)
{
	char buf[16 * 1024];
	uint32_t buf_len;
	int read_len, write_len;
	bool fini_rc = false;

	read_len = read(stdio_pipe, buf, (sizeof(buf) - 1));
	if (read_len > 0) {
		buf_len =  (uint32_t) read_len;
		write_len = slurm_write_stream(stdio_socket, (char *)&buf_len,
					       sizeof(buf_len));
		if (write_len < 0) {
			error("%s write_stream: %m", stdio_name);
		} else {
			write_len = slurm_write_stream(stdio_socket, buf,
						       buf_len);
		}
#if _DEBUG_SRUN
		buf[buf_len] = '\0';
		info("%s:%s:%u", stdio_name, buf, buf_len);
#endif
	} else if (read_len < 0) {
		if ((errno ==  EAGAIN) || (errno == EINTR)) {
			debug("%s read: %m", stdio_name);
		} else if (errno == EIO) {	/* This from PTY mode */
			debug("%s read: %m", stdio_name);
			fini_rc = true;	/* Can not recover from error */
		} else {
			error("%s read: %m", stdio_name);
			fini_rc = true;	/* Can not recover from error */
		}
	} else {	/* read_len == 0 means EOF */
#if _DEBUG_SRUN
		info("%s EOF", stdio_name);
#endif
		buf_len =  NO_VAL;
		write_len = slurm_write_stream(stdio_socket, (char *)&buf_len,
					       sizeof(buf_len));
		fini_rc = true;
	}

	return fini_rc;
}

/* srun front-end function, read message from local stdin and write it to
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
#if _DEBUG_SRUN
		info("stdin EOF");
#endif
		msg_len = NO_VAL;
	} else {
		msg_len = in_len;
/* FIXME: Test signal forwarding */
		if (buf[0] == 'S') {
			int sig_num = atoi(buf+1);
			srun_send_signal(sig_num);
			return false;
		}
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
#if _DEBUG_SRUN
		buf[msg_len] = '\0';
		info("stdin:%s:%d", buf, msg_len);
#endif
	}
	return false;
}

/* srun front-end exit code recording function
 * signal_socket IN - socket to read exit code from
 * RETURN the exit code from the remote process
 */
static int _fe_proc_exit(slurm_fd_t signal_socket)
{
	int return_code = 0;
	size_t buf_len;
	uint32_t status_32;

	buf_len = slurm_read_stream_timeout(signal_socket, (char *)&status_32,
					    sizeof(status_32), MSG_TIMEOUT);
	if (buf_len < sizeof(status_32)) {
		error("signal ERROR: %m");
		return 1;
	}

#if _DEBUG_SRUN
{
	int status = status_32;
	if (WIFEXITED(status)) {
		return_code = WEXITSTATUS(status);
		info("exit status:%d", return_code);
	} else if (WIFSIGNALED(status))
		info("exit signaled:%d", WTERMSIG(status));
	else
		info("exit code:%d", status);
}
#endif
	return return_code;
}

/* srun front-end I/O function, read message from stdout/stderr socket and
 *	write it to local stdout/stderr file descriptor
 * stdio_socket IN - socket to read message from
 * stdio_fd IN - file descriptor to write to
 * stdio_name IN   - "stdout" or "stderr" for logging
 * RETURN true on EOF
*/
static bool _fe_proc_stdio(slurm_fd_t stdio_socket, int stdio_fd,
			   char *stdio_name)
{
	char *buf;
	uint32_t msg_len = 0;
	int buf_inx = 0, buf_len;

	buf_len = slurm_read_stream_timeout(stdio_socket, (char *)&msg_len,
					    sizeof(msg_len), MSG_TIMEOUT);
	if ((buf_len < 0) || (buf_len < sizeof(msg_len))) {
		error("%s read header: %m", stdio_name);
		return true;
	}
	if (msg_len == NO_VAL) {
#if _DEBUG_SRUN
		info("%s EOF", stdio_name);
#endif
		return true;
	}

	buf = xmalloc(msg_len + 1);
	buf_len = slurm_read_stream_timeout(stdio_socket, buf+buf_inx, msg_len,
					    MSG_TIMEOUT);
	if ((buf_len < 0) || (buf_len < msg_len)) {
		error("%s read buffer: %m", stdio_name);
		if (buf_len < 0)
			return true;
	} else if (buf_len == 0) {
#if _DEBUG_SRUN
		info("%s EOF", stdio_name);
#endif
		return true;
	}
#if _DEBUG_SRUN
	buf[buf_len] = '\0';
	info("%s:%s:%d", stdio_name, buf, buf_len);
#else
{
	int offset = 0, write_len;
	while (offset < buf_len) {
		write_len = write(stdio_fd, buf+offset, buf_len-offset);
		if (write_len < 0) {
			if ((errno == EAGAIN) && (errno == EINTR))
				continue;
			error("%s write: %m", stdio_name);
			break;
		} else {
			offset += write_len;
		}
	}
}
#endif
	xfree(buf);
	return false;
}

/* write the exit status of spawned back-end process to the srun front-end */
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
	srun_child_wait_data_t *thread_data = (srun_child_wait_data_t *)x;

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
	static srun_child_wait_data_t thread_data;
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

/* Build a POE command line based upon srun options (using global variables) */
extern char *build_poe_command(void)
{
	int i;
	char *cmd_line = NULL, *tmp_str;

	xstrcat(cmd_line, "poe");
	for (i = 0; i < opt.argc; i++)
		xstrfmtcat(cmd_line, " %s", opt.argv[i]);

	if (opt.network) {
		if (strstr(opt.network, "dedicated"))
			xstrfmtcat(cmd_line, " -adapter-use=dedicated");
		else if (strstr(opt.network, "shared"))
			xstrfmtcat(cmd_line, " -adapter-use=shared");
	}
	if (opt.cpu_bind_type) {
		if ((opt.cpu_bind_type & CPU_BIND_TO_THREADS) ||
		    (opt.cpu_bind_type & CPU_BIND_TO_CORES)) {
			xstrfmtcat(cmd_line, " -bindprocs=yes");
		}
	}
	if (opt.shared != (uint16_t) NO_VAL) {
		if (opt.shared)
			xstrfmtcat(cmd_line, " -cpu_use=unique");
		else
			xstrfmtcat(cmd_line, " -cpu_use=multiple");
	}
	if (opt.network) {
		if (strstr(opt.network, "ib"))
			xstrfmtcat(cmd_line, " -devtype=ip");
	}
	if (opt.network) {
		if (strstr(opt.network, "sn_all"))
			xstrfmtcat(cmd_line, " -euidevice=sn_all");
		else if (strstr(opt.network, "sn_single"))
			xstrfmtcat(cmd_line, " -euidevice=sn_single");
		else if ((tmp_str = strstr(opt.network, "eth"))) {
			char buf[5];
			strncpy(buf, tmp_str, 5);
			buf[4] = '\0';
			xstrfmtcat(cmd_line, " -euidevice=%s", buf);
		}
	}
	if (opt.network) {
		if (strstr(opt.network, "ib"))
			xstrfmtcat(cmd_line, " -euilib=ip");
		else if (strstr(opt.network, "us"))
			xstrfmtcat(cmd_line, " -euilib=us");
	}
	if (opt.nodelist) {
/* FIXME: Need to generate hostlist file on compute node,
 * presumably using environment variables to set up */
		char *fname = NULL, *host_name, *host_line;
		pid_t pid = getpid();
		hostlist_t hl;
		int fd, len, offset, wrote;
		hl = hostlist_create(opt.nodelist);
		if (!hl)
			fatal("Invalid nodelist: %s", opt.nodelist);
		xstrfmtcat(fname, "slurm_hostlist.%u", (uint32_t) pid);
		if ((fd = creat(fname, 0600)) < 0)
			fatal("creat(%s): %m", fname);
		while ((host_name = hostlist_shift(hl))) {
			host_line = NULL;
			xstrfmtcat(host_line, "%s\n", host_name);
			free(host_name);
			len = strlen(host_line) + 1;
			offset = 0;
			while (len > offset) {
				wrote = write(fd, host_line + offset,
					      len - offset);
				if (wrote < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					fatal("write(%s): %m", fname);
				}
				offset += wrote;
			}
			xfree(host_line);
		}
		hostlist_destroy(hl);
		info("wrote hostlist file at %s", fname);
		xfree(fname);
		close(fd);
	}
	if (opt.msg_timeout) {
/* FIXME: Set MP_TIMEOUT env var */
	}
	if (opt.immediate)
		xstrfmtcat(cmd_line, " -retry=0");
	if (_verbose) {
		int info_level = MIN((_verbose + 1), 6);
		xstrfmtcat(cmd_line, " -infolevel=%d", info_level);
	}
	if (opt.labelio)
		xstrfmtcat(cmd_line, " -labelio");
	if (opt.min_nodes != NO_VAL)
		xstrfmtcat(cmd_line, " -nodes=%u", opt.min_nodes);
	if (opt.ntasks)
		xstrfmtcat(cmd_line, " -procs=%u", opt.ntasks);
	if (opt.cpu_bind_type) {
		if (opt.cpu_bind_type & CPU_BIND_TO_THREADS)
			xstrfmtcat(cmd_line, " -task_affinity=cpu");
		else if (opt.cpu_bind_type & CPU_BIND_TO_CORES)
			xstrfmtcat(cmd_line, " -task_affinity=core");
		else if (opt.cpus_per_task) {
			xstrfmtcat(cmd_line, " -task_affinity=cpu:%d",
				   opt.cpus_per_task);
		}
	}
	if (opt.ntasks_per_node != NO_VAL) {
		xstrfmtcat(cmd_line, " -tasks_per_node=%u",
			   opt.ntasks_per_node);
	}
	if (opt.unbuffered) {
		xstrfmtcat(cmd_line, " -stderrmode unordered");
		xstrfmtcat(cmd_line, " -stdoutmode unordered");
	}

info("%s", cmd_line);
	return cmd_line;
}

/*
 * srun_front_end - Open stdin/out/err socket connections to communicate with
 *	a remote node process and spawn a job to claim that connection
 *	and execute the user's command using poe.
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. srun)
 *	     [1]:  Program to be spawned for user
 *	     [2+]: Arguments to spawned program
 * RETURN - remote processes exit code or -1 if some internal error
 */
extern int srun_front_end (int argc, char **argv)
{
	uint16_t port_e, port_o, port_s;
	slurm_addr_t addr_e, addr_o, addr_s;
	slurm_addr_t stderr_addr, stdout_addr, signal_addr;
	slurm_fd_t local_stdin = 0;
	slurm_fd_t stdout_socket = -1;
	slurm_fd_t stderr_socket = -1;
	slurm_fd_t signal_socket = -1;
	slurm_fd_t stderr_conn = SLURM_SOCKET_ERROR;
	slurm_fd_t stdout_conn = SLURM_SOCKET_ERROR;
	slurm_fd_t signal_conn = SLURM_SOCKET_ERROR;
	bool job_fini = false, stderr_fini = false, stdout_fini = false;
	fd_set except_fds, read_fds;
	char *exec_line = NULL, hostname[1024];
	int i, n_fds, status = -1;
	bool pty = PTY_MODE;

	/* Open sockets for back-end program to communicate with */
	/* Socket for stdin/stdout */
	if ((stdout_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		goto fini;
	}
	if (slurm_get_stream_addr(stdout_socket, &addr_o) < 0) {
		error("slurm_get_stream_addr: %m");
		goto fini;
	}
	port_o = ntohs(((struct sockaddr_in) addr_o).sin_port);

	/* Socket for stderr */
	if ((stderr_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		goto fini;
	}
	if (slurm_get_stream_addr(stderr_socket, &addr_e) < 0) {
		error("slurm_get_stream_addr: %m");
		goto fini;
	}
	port_e = ntohs(((struct sockaddr_in) addr_e).sin_port);

	/* Socket for signals and exit code */
	if ((signal_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		goto fini;
	}
	if (slurm_get_stream_addr(signal_socket, &addr_s) < 0) {
		error("slurm_get_stream_addr: %m");
		goto fini;
	}
	port_s = ntohs(((struct sockaddr_in) addr_s).sin_port);

/* FIXME: Are environment variables, directory, limits and search path propagated? */
	/* Generate back-end execute line */
	gethostname_short(hostname, sizeof(hostname));
	xstrfmtcat(exec_line, "%s/bin/srun --srun-be %s %hu %hu %hu",
		   SLURM_PREFIX, hostname, port_o, port_e, port_s);
	for (i = 1; i < argc; i++)
		xstrfmtcat(exec_line, " %s", argv[i]);
	printf("%s\n", exec_line);
	xfree(exec_line);
/* FIXME: Monitor for job abort, if needed, break out of accept or I/O loop */

	/* Accept connections from the back-end */
	while (true) {
		stdout_conn = slurm_accept_stream(stdout_socket, &stdout_addr);
		if (stdout_conn != SLURM_SOCKET_ERROR)
			break;
		if (errno != EINTR) {
			error("slurm_accept_stream: %m");
			goto fini;
		}
	}
	if (pty) {
		stderr_fini = true;
	} else {
		while (true) {
			stderr_conn = slurm_accept_stream(stderr_socket,
							  &stderr_addr);
			if (stderr_conn != SLURM_SOCKET_ERROR)
				break;
			if (errno != EINTR) {
				error("slurm_accept_stream: %m");
				goto fini;
			}
		}
	}
	while (true) {
		signal_conn = slurm_accept_stream(signal_socket, &signal_addr);
		if (signal_conn != SLURM_SOCKET_ERROR)
			break;
		if (errno != EINTR) {
			error("slurm_accept_stream: %m");
			goto fini;
		}
	}
	global_signal_conn = signal_conn;

	n_fds = local_stdin;
	n_fds = MAX(stderr_conn, n_fds);
	n_fds = MAX(stdout_conn, n_fds);
	n_fds = MAX(signal_conn, n_fds);
	while ( !(job_fini && stderr_fini && stdout_fini) ) {
		FD_ZERO(&except_fds);
		FD_ZERO(&read_fds);
		if (local_stdin >= 0) {
			FD_SET(local_stdin, &except_fds);
			FD_SET(local_stdin, &read_fds);
		}
		if (!stdout_fini)
			FD_SET(stdout_conn, &read_fds);
		if (!stderr_fini)
			FD_SET(stderr_conn, &read_fds);
		FD_SET(signal_conn, &read_fds);

		i = select((n_fds + 1), &read_fds, NULL, &except_fds, NULL);
		if (i == -1) {
			if (errno == EINTR)
				continue;
			error("select: %m");
			break;
		}
		if ((local_stdin >= 0) &&
		    (FD_ISSET(local_stdin, &except_fds) ||
		     FD_ISSET(local_stdin, &read_fds))) {
			if (_fe_proc_stdin(local_stdin, stdout_conn)) {
				local_stdin = -1;
			}
		}
		if (FD_ISSET(stdout_conn, &read_fds) &&
		    _fe_proc_stdio(stdout_conn, 1, "stdout")) {
			/* Remote stderr closed */
			stdout_fini = true;
		}
		if (FD_ISSET(stderr_conn, &read_fds) &&
		    _fe_proc_stdio(stderr_conn, 2, "stderr")) {
			/* Remote stderr closed */
			stderr_fini = true;
		}
		if (FD_ISSET(signal_conn, &read_fds)) {
			i = _fe_proc_exit(signal_conn);
			status = MAX(status, i);
			job_fini = true;
		}
	}

fini:	if (stdout_conn != SLURM_SOCKET_ERROR)
		slurm_close_accepted_conn(stdout_conn);
	if (stderr_conn != SLURM_SOCKET_ERROR)
		slurm_close_accepted_conn(stderr_conn);
	if (signal_conn != SLURM_SOCKET_ERROR) {
		global_signal_conn = SLURM_SOCKET_ERROR;
		slurm_close_accepted_conn(signal_conn);
	}
	if (stdout_socket >= 0)
		slurm_shutdown_msg_engine(stdout_socket);
	if (stderr_socket >= 0)
		slurm_shutdown_msg_engine(stderr_socket);
	if (signal_socket >= 0)
		slurm_shutdown_msg_engine(signal_socket);

	return status;
}


/* srun front-end signal processing function, send a signal to back-end
 *	program
 * sig_num IN - signal to send
 * RETURN 0 on success, -1 on error
*/
extern int srun_send_signal(int sig_num)
{
	int buf_len;
	uint32_t sig_msg = (uint32_t) sig_num;

	if (global_signal_conn == SLURM_SOCKET_ERROR) {
		error("signal write: back-end not connected");
		return -1;
	}
	buf_len = slurm_write_stream_timeout(global_signal_conn,
					     (char *)&sig_msg, sizeof(sig_msg),
					     MSG_TIMEOUT);

	/* NOTE: Do not change test below
	 * (-1 < sizeof(sig_msg)) is false since
	 * -1 gets converted to unsigned long first */
	if ((buf_len < 0) || (buf_len < sizeof(sig_msg))) {
		error("signal write: %m");
		return -1;
	}

	info("signal %d sent", sig_num);
	return 0;
}

/*
 * srun_back_end - Open stdin/out/err socket connections to communicate with
 *	the srun command that spawned this one, forward its stdin/out/err
 *	communications back, forward signals, and return the program's exit
 *	code.
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. srun)
 *	     [1]:  "--srun-be" (argument to spawn srun backend)
 *	     [2]:  Hostname or address of front-end
 *	     [3]:  Port number for stdin/out
 *	     [4]:  Port number for stderr
 *	     [5]:  Port number for signals/exit status
 *	     [6]:  Program to be spawned for user
 *	     [7+]: Arguments to spawned program
 * RETURN - remote processes exit code
 */
extern int srun_back_end (int argc, char **argv)
{
	char *host = NULL;
	uint16_t port_e = 0, port_o = 0, port_s = 0;
	slurm_addr_t addr_e, addr_o, addr_s;
	slurm_fd_t stderr_socket = 0, stdin_socket = 0, stdout_socket = -1;
	slurm_fd_t signal_socket = 0;
	bool job_fini = false;
	bool signal_fini = false, stderr_fini = false, stdout_fini = false;
	int dummy_pipe[2] = {-1, -1}, stderr_pipe[2] = {-1, -1};
	int stdin_pipe[2] = {-1, -1}, stdout_pipe[2] = {-1, -1};
	fd_set read_fds;
	pid_t pid;
	int i, n_fds, status = 0;
	bool pty = PTY_MODE;

	if (argc >= 7) {
		host   = argv[2];
		port_o = atoi(argv[3]);
		port_e = atoi(argv[4]);
		port_s = atoi(argv[5]);
	}
	if ((argc < 7) || (port_o == 0) || (port_e == 0) || (port_s == 0)) {
		error("Usage: srun --srun-be <srun_host> <srun_stdin/out_port> "
		      "<srun_stderr_port> <signal/exit_status_port> "
		      "<program> <args ...>\n");
		return 1;
	}

	/* Set up stdin/out on firt port,
	 * Set up stderr on second port,
	 * Signals and exit code use third port */
	slurm_set_addr(&addr_o, port_o, host);
	stdout_socket = slurm_open_stream(&addr_o);
	if (stdout_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, port_o);
		return 1;
	}
	stdin_socket = stdout_socket;

	slurm_set_addr(&addr_e, port_e, host);
	stderr_socket = slurm_open_stream(&addr_e);
	if (stderr_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, port_e);
		return 1;
	}

	slurm_set_addr(&addr_s, port_s, host);
	signal_socket = slurm_open_stream(&addr_s);
	if (signal_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, port_s);
		return 1;
	}

	if (pty) {
		if (openpty(&stdin_pipe[1], &stdin_pipe[0],
			    NULL, NULL, NULL) < 0) {
			error("stdin openpty: %m");
			return 1;
		}
		stdout_pipe[0] = dup(stdin_pipe[1]);
		stdout_pipe[1] = dup(stdin_pipe[0]);
		if ((stdout_pipe[0] == -1) || (stdout_pipe[1] == -1)) {
			error("dup(openpty): %m");
			return 1;
		}
		/* In PTY mode, stderr goes to the same stream as stdout */
		stderr_pipe[0] = stderr_pipe[1] = -1;
		stderr_fini = true;
	} else if ((pipe(stdin_pipe)  == -1) ||
		   (pipe(stdout_pipe) == -1) ||
		   (pipe(stderr_pipe) == -1)) {
		error("pipe: %m");
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		error("fork: %m");
		return 1;
	} else if (pid == 0) {
		if (pty) {
			login_tty(stdin_pipe[0]);
		} else {
			if ((dup2(stdin_pipe[0],  0) == -1) ||
			    (dup2(stdout_pipe[1], 1) == -1) ||
			    (dup2(stderr_pipe[1], 2) == -1)) {
				error("dup2: %m");
				return 1;
			}
			(void) close(stderr_pipe[0]);
			(void) close(stderr_pipe[1]);
		}
		(void) close(stdin_pipe[0]);
		(void) close(stdin_pipe[1]);
		(void) close(stdout_pipe[0]);
		(void) close(stdout_pipe[1]);

		execvp(argv[6], argv+6);
		error("execv(%s) error: %m", argv[5]);
		return 1;
	}

	(void) close(stdin_pipe[0]);
	(void) close(stdout_pipe[1]);
	(void) close(stderr_pipe[1]);

	/* NOTE: dummy_pipe is only used to wake the select() function in the
	 * loop below when the spawned process terminates */
	if (pipe(dummy_pipe) == -1)
		error("pipe: %m");
	_wait_be_func(pid, signal_socket, &status, &job_fini, dummy_pipe[1]);

	n_fds = dummy_pipe[0];
	n_fds = MAX(signal_socket,  n_fds);
	n_fds = MAX(stdin_socket,   n_fds);
	n_fds = MAX(stderr_pipe[0], n_fds);
	n_fds = MAX(stdout_pipe[0], n_fds);

	while ( !(job_fini && stderr_fini && stdout_fini) ) {
		FD_ZERO(&read_fds);
		FD_SET(dummy_pipe[0], &read_fds);
		if (!signal_fini)
			FD_SET(signal_socket,  &read_fds);
		if (stdin_pipe[1] >= 0)
			FD_SET(stdin_socket,   &read_fds);
		if (!stderr_fini)
			FD_SET(stderr_pipe[0], &read_fds);
		if (!stdout_fini)
			FD_SET(stdout_pipe[0], &read_fds);

		i = select((n_fds + 1), &read_fds, NULL, NULL, NULL);
		if (i == -1) {
			if (errno == EINTR)
				continue;
			error("select: %m");
			break;
		}
		if ((signal_socket >= 0) &&
		    FD_ISSET(signal_socket, &read_fds) &&
		    _be_proc_signal(signal_socket, pid)) {
			/* Remote signal_socket closed */
			signal_fini = true;
		}
		if ((stdin_socket >= 0) && FD_ISSET(stdin_socket, &read_fds) &&
		    _be_proc_stdin(stdin_pipe[1], stdin_socket)) {
			/* Remote stdin closed */
			(void) close(stdin_pipe[1]);
			stdin_pipe[1] = -1;
		}
		if (FD_ISSET(stderr_pipe[0], &read_fds) &&
		    _be_proc_stdio(stderr_pipe[0], stderr_socket, "stderr")) {
			/* Remote stderr closed */
			stderr_fini = true;
		}
		if (FD_ISSET(stdout_pipe[0], &read_fds) &&
		    _be_proc_stdio(stdout_pipe[0], stdout_socket, "stdout")) {
			/* Remote stdout closed */
			stdout_fini = true;
		}
	}

	(void) close(dummy_pipe[0]);
	(void) close(dummy_pipe[1]);
	if (stdin_pipe[1] >= 0)
		(void) close(stdin_pipe[1]);
	(void) close(stderr_pipe[0]);
	(void) close(stdout_pipe[0]);
	slurm_close_stream(signal_socket);
	slurm_close_stream(stderr_socket);
	slurm_close_stream(stdout_socket);

	if (WIFEXITED(status))
		exit(WEXITSTATUS(status));
	exit(0);
}

#endif	/* USE_LOADLEVELER */
