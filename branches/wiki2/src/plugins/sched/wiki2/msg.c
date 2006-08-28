/*****************************************************************************\
 *  msg.c - Message/communcation manager for Wiki plugin
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/slurmctld/sched_plugin.h"
#include "./crypto.h"
#include "./msg.h"

static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t msg_thread_id;
static char *err_msg;
static int   err_code;

int init_prio_mode = PRIO_HOLD;
char *auth_key = NULL;

static char *	_get_wiki_conf_path(void);
static void *	_msg_thread(void *no_data);
static int	_parse_msg(char *msg, char **req);
static void	_parse_wiki_config(void);
static void	_proc_msg(slurm_fd new_fd, char *msg);
static size_t	_read_bytes(int fd, char *buf, const size_t size);
static char *	_recv_msg(slurm_fd new_fd);
static size_t	_send_msg(slurm_fd new_fd, char *buf, size_t size);
static void	_send_reply(slurm_fd new_fd, char *response);
static void	_sig_handler(int signal);
static int	_start_job(slurm_fd new_fd, char *cmd_ptr);
static size_t	_write_bytes(int fd, char *buf, const size_t size);

/*****************************************************************************\
 * spawn message hander thread
\*****************************************************************************/
extern int spawn_msg_thread(void)
{
	pthread_attr_t thread_attr_msg;

	pthread_mutex_lock( &thread_flag_mutex );
	if (thread_running) {
		error("Wiki thread already running, not starting another");
		pthread_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	_parse_wiki_config();
	slurm_attr_init(&thread_attr_msg);
	if (pthread_create(&msg_thread_id, &thread_attr_msg, 
			_msg_thread, NULL))
		fatal("pthread_create %m");

	slurm_attr_destroy(&thread_attr_msg);
	thread_running = true;
	pthread_mutex_unlock(&thread_flag_mutex);
	return SLURM_SUCCESS;
}

/*****************************************************************************\
 * terminate message hander thread
\*****************************************************************************/
extern void term_msg_thread(void)
{
	pthread_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		thread_shutdown = true;
		pthread_kill(msg_thread_id, SIGUSR1);
	}
	pthread_mutex_unlock(&thread_flag_mutex);
}

/*****************************************************************************\
 * message hander thread
\*****************************************************************************/
static void *_msg_thread(void *no_data)
{
	slurm_fd sock_fd, new_fd;
	slurm_addr cli_addr;
	int sig_array[] = {SIGUSR1, 0};
	char *msg;

	if ((sock_fd = slurm_init_msg_engine_port(sched_get_port())) 
			== SLURM_SOCKET_ERROR)
		fatal("wiki: slurm_init_msg_engine_port %m");

	/* SIGUSR1 used to interupt accept call */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sig_array);

	/* Process incoming RPCs until told to shutdown */
	while (!thread_shutdown) {
		if ((new_fd = slurm_accept_msg_conn(sock_fd, &cli_addr))
				== SLURM_SOCKET_ERROR) {
			if (errno != EINTR)
				error("wiki: slurm_accept_msg_conn %m");
			continue;
		}
		/* It would be nice to create a pthread for each new 
		 * RPC, but that leaks memory on some systems when 
		 * done from a plugin. 
		 * FIXME: Maintain a pool of and reuse them. */
		err_code = 0;
		err_msg = "";
		msg = _recv_msg(new_fd);
		_proc_msg(new_fd, msg);
		xfree(msg);
		slurm_close_accepted_conn(new_fd);
	}
	(void) slurm_shutdown_msg_engine(sock_fd);
	pthread_exit((void *) 0);
	return NULL;
}

/*****************************************************************************\
 * _get_wiki_conf_path - return the pathname of the wiki.conf file
 * return value must be xfreed
\*****************************************************************************/
static char * _get_wiki_conf_path(void)
{
	char *val = getenv("SLURM_CONF");
	char *path = NULL;
	int i;

	if (!val)
		val = default_slurm_config_file;

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("wiki.conf") + 1;
	path = xmalloc(i);
	strcpy(path, val);
	val = strrchr(path, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = path;
	strcpy(val, "wiki.conf");

	return path;
}

/*****************************************************************************\
 * _parse_wiki_config - Results go into global variables
 * 
 * wiki_conf options
 * JobPriority=hold|run
 * AuthKey=number
\*****************************************************************************/
static void _parse_wiki_config(void)
{
	s_p_options_t options[] = {
		{"AuthKey", S_P_STRING},
		{"JobPriority", S_P_STRING}, 
		{NULL} };
	s_p_hashtbl_t *tbl;
	char *priority_mode, *wiki_conf;
	struct stat buf;

	wiki_conf = _get_wiki_conf_path();
	if ((wiki_conf == NULL) || (stat(wiki_conf, &buf) == -1)) {
		debug("No wiki.conf file (%s)", wiki_conf);
		xfree(wiki_conf);
		return;
	}

	debug("Reading wiki.conf file (%s)",wiki_conf);
	tbl = s_p_hashtbl_create(options);
	if (s_p_parse_file(tbl, wiki_conf) == SLURM_ERROR)
		fatal("something wrong with opening/reading wiki.conf file");

	if (! s_p_get_string(&auth_key, "AuthKey", tbl)) {
		debug("Warning: No wiki_conf AuthKey specified");
	}

	if (s_p_get_string(&priority_mode, "JobPriority", tbl)) {
		if (strcasecmp(priority_mode, "hold") == 0)
			init_prio_mode = PRIO_HOLD;
		else if (strcasecmp(priority_mode, "run") == 0)
			init_prio_mode = PRIO_DECREMENT;
		else
			error("Invalid value for JobPriority in wiki.conf");	
		xfree(priority_mode);
	}
	s_p_hashtbl_destroy(tbl);
	xfree(wiki_conf);

	return;
}

/*****************************************************************************\
 * _sig_handler: signal handler, interrupt communications thread
\*****************************************************************************/
static void _sig_handler(int signal)
{
}

static size_t	_read_bytes(int fd, char *buf, const size_t size)
{
	size_t bytes_remaining, bytes_read;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while (bytes_remaining > 0) {
		bytes_read = read(fd, ptr, bytes_remaining);
		if (bytes_read <= 0)
			return 0;
		bytes_remaining -= bytes_read;
		ptr += bytes_read;
	}
	
	return size;
}

static size_t	_write_bytes(int fd, char *buf, const size_t size)
{
	size_t bytes_remaining, bytes_written;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while (bytes_remaining > 0) {
		bytes_written = write(fd, ptr, size);
		if (bytes_written < 0)
			return 0;
		bytes_remaining -= bytes_written;
		ptr += bytes_written;
	}
	return size;
}

/*****************************************************************************\
 * Read a message (request) from specified file descriptor 
 *
 * RET - The message which must be xfreed or 
 *       NULL on error
\*****************************************************************************/
static char *	_recv_msg(slurm_fd new_fd)
{
	char header[10];
	uint32_t size;
	char *buf;

	if (_read_bytes((int) new_fd, header, 9) != 9) {
		err_code = 240;
		err_msg = "failed to read message header";
		error("wiki: failed to read message header %m");
		return NULL;
	}

	if (sscanf(header, "%ul", &size) != 1) {
		err_code = 244;
		err_msg = "malformed message header";
		error("wiki: malformed message header (%s)", header);
		return NULL;
	}

	buf = xmalloc(size);
	if (_read_bytes((int) new_fd, buf, size) != size) {
		err_code = 246;
		err_msg = "unable to read all message data";
		error("wiki: unable to read data message");
		xfree(buf);
		return NULL;
	}

	
}

/*****************************************************************************\
 * Send a message (response) to specified file descriptor 
 *
 * RET - Number of data bytes written (excludes header)
\*****************************************************************************/
static size_t	_send_msg(slurm_fd new_fd, char *buf, size_t size)
{
	char header[10];
	size_t data_sent;

	(void) sprintf(header, "%08ul\n", (uint32_t) size);
	if (_write_bytes((int) new_fd, header, 9) != 9) {
		error("wiki: failed to write message header %m");
		return 0;
	}

	data_sent = _write_bytes((int) new_fd, buf, size);
	if (data_sent != size) {
		error("wiki: unable to write data message (%lu of %lu) %m",
			data_sent, size);
	}

	return data_sent;
}

/*****************************************************************************\
 * Parse and checksum a wiki request
 * msg IN - message to parse
 * req OUT - pointer the request portion of the message
 * RET 0 on success, -1 on error
\*****************************************************************************/
static int	_parse_msg(char *msg, char **req)
{
	char sum[20];	/* format is "CK=%08x%08x" */
	char *auth_ptr = strstr(msg, "AUTH=");
	char *dt_ptr = strstr(msg, "DT=");
	char *ts_ptr = strstr(msg, "TS=");

	if (!auth_ptr) {
		err_code = 300;
		err_msg = "request lacks AUTH";
		error("wiki: request lacks AUTH=");
		return -1;
	}
	if (!dt_ptr) {
		err_code = 300;
		err_msg = "request lacks DT";
		error("wiki: request lacks DT=");
		return -1;
	}
	if (!ts_ptr) {
		err_code = 300;
		err_msg = "request lacks TS";
		error("wiki: request lacks TS=");
		return -1;
	}

	if (auth_key) {
		checksum(sum, ts_ptr);
		if (strncmp(sum, msg, 19) != 0) {
			err_code = 422;
			err_msg = "bad checksum";
			error("wiki: message checksum error");
			return -1;
		}
	}

	*req = dt_ptr + 3;
	return 0;
}

/*****************************************************************************\
 * Parse, process and respond to a request
\*****************************************************************************/
static void	_proc_msg(slurm_fd new_fd, char *msg)
{
	char *req, *cmd_ptr;
	char response[128];

	if (new_fd < 0)
		return;

	if (!msg)
		goto err_msg;

	debug3("wiki msg recv:%s", msg);
	if (_parse_msg(msg, &req) != 0)
		goto err_msg;

	cmd_ptr = strstr(req, "CMD=");
	if (cmd_ptr == NULL) {
		err_code = 300;
		err_msg = "request lacks CMD"; 
		error("wiki: request lacks CMD");
		goto err_msg;
	}
	cmd_ptr +=4;
	if        (strncmp(cmd_ptr, "GETJOBS", 7) == 0) {
	} else if (strncmp(cmd_ptr, "GETNODES", 8) == 0) {
	} else if (strncmp(cmd_ptr, "STARTJOB", 8) == 0) {
		_start_job(new_fd, cmd_ptr);
		goto err_msg;	/* always send reply here */
	} else if (strncmp(cmd_ptr, "CANCELJOB", 9) == 0) {
	} else if (strncmp(cmd_ptr, "SUSPENDJOB", 10) == 0) {
	} else if (strncmp(cmd_ptr, "RESUMEJOB", 9) == 0) {
	} else if (strncmp(cmd_ptr, "JOBADDTASK", 10) == 0) {
	} else if (strncmp(cmd_ptr, "JOBRELEASETASK", 14) == 0) {
	} else {
		err_code = 300;
		err_msg = "unsupported request type";
		error("wiki: unrecognized request type: %s", req);
		goto err_msg;
	}
	return;

 err_msg:
	snprintf(response, sizeof(response), 
		"SC=%d;RESPONSE=%s", err_code, err_msg);
	_send_reply(new_fd, response);
	return;
}

static void	_send_reply(slurm_fd new_fd, char *response)
{
/* FIXME */
}

static int	_start_job(slurm_fd new_fd, char *cmd_ptr)
{
	char *arg_ptr, *task_ptr, *node_ptr, *tmp_char;
	int i;
	uint32_t jobid;
	hostlist_t hl;
	char host_string[1024];
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		err_code = 300;
		err_msg = "STARTJOB lacks ARG";
		error("wiki: STARTJOB lacks ARG");
		return -1;
	}
	jobid = strtol(arg_ptr+4, &tmp_char, 10);
	if (!isspace(tmp_char[0])) {
		err_code = 300;
		err_msg = "Invalid ARG value";
		error("wiki: STARTJOB has invalid jobid");
		return -1;
	}

	task_ptr = strstr(cmd_ptr, "TASKLIST=");
	if (task_ptr == NULL) {
		err_code = 300;
		err_msg = "STARTJOB lacks TASKLIST";
		error("wiki: STARTJOB lacks TASKLIST");
		return -1;
	}
	node_ptr = task_ptr + 9;
	for (i=0; node_ptr[i]!='\0'; i++) {
		if (node_ptr[i] == ':')
			node_ptr[i] = ',';
	}
	hl = hostlist_create(node_ptr);
	i = hostlist_ranged_string(hl, sizeof(host_string), host_string);
	hostlist_destroy(hl);
	if (i < 0) {
		err_code = 300;
		err_msg = "STARTJOB has invalid TASKLIST";
		error("wiki: STARTJOB has invalid TASKLIST");
		return -1;
	}
	if (sched_set_nodelist(jobid, host_string) != SLURM_SUCCESS) {
		err_code = 734;
		err_msg = "failed to assign nodes";
		error("wiki: failed to assign nodes to job %u", jobid);
		return -1;
	}

	if (sched_start_job(jobid, (uint32_t) 1) != SLURM_SUCCESS) {
		err_code = 730;
		err_msg = "failed to start job";
		error("wiki: failed to start job %u", jobid);
		return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg), 
		"job %u started successfully", jobid);
	err_msg = reply_msg;
	return 0;
}
