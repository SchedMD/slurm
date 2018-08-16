/*****************************************************************************\
 *  msg.c - Process socket communications for slurmctld/nonstop plugin
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/bitstring.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/node_conf.h"
#include "src/common/parse_time.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "slurm/smd_ns.h"
#include "src/plugins/slurmctld/nonstop/do_work.h"
#include "src/plugins/slurmctld/nonstop/msg.h"
#include "src/plugins/slurmctld/nonstop/read_config.h"

/* This version string is defined at configure time of libsmd. The
 * META of libsmd needs to reflect this version. */
char *version_string = "VERSION:18.08";

/* When a remote socket closes on AIX, we have seen poll() return EAGAIN
 * indefinitely for a pending write request. Rather than locking up
 * socket, abort after _MAX_RETRIES poll() failures. */
#define _MAX_RETRIES	10

static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t msg_thread_id;

static size_t _read_bytes(int fd, char *buf, size_t size)
{
	size_t bytes_remaining, bytes_read;
	char *ptr;
	struct pollfd ufds;
	int rc;

	bytes_remaining = size;
	size = 0;
	ufds.fd = fd;
	ufds.events = POLLIN;
	ptr = buf;
	while (bytes_remaining > 0) {
		rc = poll(&ufds, 1, 10000);	/* 10 sec timeout */
		if (rc == 0)		/* timed out */
			break;
		if ((rc == -1) &&	/* some error */
		    ((errno== EINTR) || (errno == EAGAIN)))
			continue;
		if ((ufds.revents & POLLIN) == 0) /* some poll error */
			break;

		bytes_read = read(fd, ptr, bytes_remaining);
		if (bytes_read <= 0)
			break;
		bytes_remaining -= bytes_read;
		size += bytes_read;
		ptr += bytes_read;
	}

	return size;
}

static size_t _write_bytes(int fd, char *buf, size_t size)
{
	size_t bytes_remaining, bytes_written;
	char *ptr;
	struct pollfd ufds;
	int rc, retry_cnt = 0;

	bytes_remaining = size;
	size = 0;
	ptr = buf;
	ufds.fd = fd;
	ufds.events = POLLOUT;
	while (bytes_remaining > 0) {
		rc = poll(&ufds, 1, 10000);	/* 10 sec timeout */
		if (rc == 0)		/* timed out */
			break;
		if ((rc == -1) &&	/* some error */
		    ((errno== EINTR) || (errno == EAGAIN))) {
			if ((retry_cnt++) >= _MAX_RETRIES) {
				info("slurmctld/nonstop: repeated poll "
				     "errors for write: %m");
				break;
			}
			continue;
		}
		if ((ufds.revents & POLLOUT) == 0) /* some poll error */
			break;

		bytes_written = write(fd, ptr, bytes_remaining);
		if (bytes_written <= 0)
			break;
		bytes_remaining -= bytes_written;
		size += bytes_written;
		ptr += bytes_written;
	}

	return size;
}

static char *_recv_msg(int new_fd)
{
	char header[10];
	unsigned long size;
	char *buf;

	if (_read_bytes((int) new_fd, header, 9) != 9) {
		info("slurmctld/nonstop: failed to read message header %m");
		return NULL;
	}

	if (sscanf(header, "%lu", &size) != 1) {
		info("slurmctld/nonstop: malformed message header (%s)",
		     header);
		return NULL;
	}

	buf = xmalloc(size + 1);	/* need '\0' on end to print */
	if (_read_bytes((int) new_fd, buf, size) != size) {
		info("slurmctld/nonstop: unable to read data message");
		xfree(buf);
		return NULL;
	}

	if (nonstop_debug > 1)
		info("slurmctld/nonstop: msg recv:%s", buf);

	return buf;
}

static void _send_reply(int new_fd, char *msg)
{
	uint32_t data_sent, msg_size = 0;
	char header[10];

	if (msg)
		msg_size = strlen(msg) + 1;
	(void) sprintf(header, "%08u\n", msg_size);
	if (_write_bytes((int) new_fd, header, 9) != 9) {
		info("slurmctld/nonstop: failed to write message header %m");
		return;
	}

	data_sent = _write_bytes((int) new_fd, msg, msg_size);
	if (data_sent != msg_size) {
		info("slurmctld/nonstop: unable to write data message "
		      "(%u of %u) %m", data_sent, msg_size);
	}
}

static char *_decrypt(char *msg, uid_t *uid)
{
	void *buf_out = NULL;
	int buf_out_size = 0, err;
	gid_t gid;

	err = munge_decode(msg, ctx, &buf_out, &buf_out_size, uid, &gid);
	if (err != EMUNGE_SUCCESS) {
		info("slurmctld/nonstop: munge_decode error: %s",
		     munge_strerror(err));
		xfree(buf_out);
	}
	return (char *) buf_out;
}

static void _proc_msg(int new_fd, char *msg, slurm_addr_t cli_addr)
{
	/* Locks: Read job and node data */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock2 = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Write node data */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	char *cmd_ptr, *resp = NULL, *msg_decrypted = NULL;
	uid_t cmd_uid;
	uint32_t protocol_version = 0;

	if (!msg) {
		info("slurmctld/nonstop: NULL message received");
		resp = xstrdup("Error:\"NULL message received\"");
		goto send_resp;
	}

	msg_decrypted = _decrypt(msg, &cmd_uid);
	if (!msg_decrypted) {
		info("slurmctld/nonstop: Message decrypt failure");
		resp = xstrdup("Error:\"Message decrypt failure\"");
		goto send_resp;
	}
	if (nonstop_debug > 0)
		info("slurmctld/nonstop: msg decrypted:%s", msg_decrypted);
	cmd_ptr = msg_decrypted;

	/* 123456789012345678901234567890 */
	if (xstrncmp(cmd_ptr, version_string, 13) == 0) {
		cmd_ptr = strchr(cmd_ptr + 13, ':');
		if (cmd_ptr) {
			cmd_ptr++;
			protocol_version = SLURM_PROTOCOL_VERSION;
		}
	}

	if (protocol_version == 0) {
		info("slurmctld/nonstop: Message version invalid");
		resp = xstrdup("Error:\"Message version invalid\"");
		goto send_resp;
	}
	if (xstrncmp(cmd_ptr, "CALLBACK:JOBID:", 15) == 0) {
		resp = register_callback(cmd_ptr, cmd_uid, cli_addr,
					 protocol_version);
	} else if (xstrncmp(cmd_ptr, "DRAIN:NODES:", 12) == 0) {
		lock_slurmctld(node_write_lock);
		resp = drain_nodes_user(cmd_ptr, cmd_uid, protocol_version);
		unlock_slurmctld(node_write_lock);
	} else if (xstrncmp(cmd_ptr, "DROP_NODE:JOBID:", 15) == 0) {
		lock_slurmctld(job_write_lock2);
		resp = drop_node(cmd_ptr, cmd_uid, protocol_version);
		unlock_slurmctld(job_write_lock2);
	} else if (xstrncmp(cmd_ptr, "GET_FAIL_NODES:JOBID:", 21) == 0) {
		lock_slurmctld(job_read_lock);
		resp = fail_nodes(cmd_ptr, cmd_uid, protocol_version);
		unlock_slurmctld(job_read_lock);
	} else if (xstrncmp(cmd_ptr, "REPLACE_NODE:JOBID:", 19) == 0) {
		lock_slurmctld(job_write_lock2);
		resp = replace_node(cmd_ptr, cmd_uid, protocol_version);
		unlock_slurmctld(job_write_lock2);
	} else if (xstrncmp(cmd_ptr, "SHOW_CONFIG", 11) == 0) {
		resp = show_config(cmd_ptr, cmd_uid, protocol_version);
	} else if (xstrncmp(cmd_ptr, "SHOW_JOB:JOBID:", 15) == 0) {
		resp = show_job(cmd_ptr, cmd_uid, protocol_version);
	} else if (xstrncmp(cmd_ptr, "TIME_INCR:JOBID:", 16) == 0) {
		lock_slurmctld(job_write_lock);
		resp = time_incr(cmd_ptr, cmd_uid, protocol_version);
		unlock_slurmctld(job_write_lock);
	} else {
		info("slurmctld/nonstop: Invalid command: %s", cmd_ptr);
		xstrfmtcat(resp, "%s ECMD", SLURM_VERSION_STRING);
	}

 send_resp:
	if (nonstop_debug > 0)
		info("slurmctld/nonstop: msg send:%s", resp);
	_send_reply(new_fd, resp);
	xfree(resp);
	if (msg_decrypted)
		free(msg_decrypted);
	return;
}

static void *_msg_thread(void *no_data)
{
	int sock_fd = -1, new_fd;
	slurm_addr_t cli_addr;
	char *msg;
	int i;

	/* If Port is already taken, keep trying to open it 10 secs */
	for (i = 0; (!thread_shutdown); i++) {
		if (i > 0)
			sleep(10);
		sock_fd = slurm_init_msg_engine_port(nonstop_comm_port);
		if (sock_fd != SLURM_SOCKET_ERROR)
			break;
		error("slurmctld/nonstop: can not open port: %hu %m",
		      nonstop_comm_port);
	}

	/* Process incoming RPCs until told to shutdown */
	while (!thread_shutdown) {
		new_fd = slurm_accept_msg_conn(sock_fd, &cli_addr);
		if (new_fd == SLURM_SOCKET_ERROR) {
			if (errno != EINTR) {
				info("slurmctld/nonstop: "
				     "slurm_accept_msg_conn %m");
			}
			continue;
		}
		if (thread_shutdown) {
			close(new_fd);
			break;
		}
		/* It would be nice to create a pthread for each new
		 * RPC, but that leaks memory on some systems when
		 * done from a plugin. Alternately, we could maintain
		 * a pool of pthreads and reuse them. */
		msg = _recv_msg(new_fd);
		if (msg) {
			_proc_msg(new_fd, msg, cli_addr);
			xfree(msg);
		}
		close(new_fd);
	}
	debug("slurmctld/nonstop: message engine shutdown");
	if (sock_fd > 0)
		close(sock_fd);
	pthread_exit((void *) 0);
	return NULL;
}

extern int spawn_msg_thread(void)
{
	slurm_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		error("nonstop thread already running");
		slurm_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	slurm_thread_create(&msg_thread_id, _msg_thread, NULL);
	thread_running = true;
	slurm_mutex_unlock(&thread_flag_mutex);

	return SLURM_SUCCESS;
}

extern void term_msg_thread(void)
{
	slurm_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		int fd;
		slurm_addr_t addr;

		thread_shutdown = true;

		/* Open and close a connection to the plugin listening port.
		 * Allows slurm_accept_msg_conn() to return in _msg_thread()
		 * so that it can check the thread_shutdown flag.
		 */
		slurm_set_addr(&addr, nonstop_comm_port, "localhost");
		fd = slurm_open_stream(&addr, true);
		if (fd != -1) {
			/* we don't care if the open failed */
			close(fd);
		}

		debug2("waiting for slurmctld/nonstop thread to exit");
		pthread_join(msg_thread_id, NULL);
		msg_thread_id = 0;
		thread_shutdown = false;
		thread_running = false;
		debug2("join of slurmctld/nonstop thread was successful");
	}
	slurm_mutex_unlock(&thread_flag_mutex);
}
