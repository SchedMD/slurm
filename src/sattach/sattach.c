/*****************************************************************************\
 *  sattach.c - Attach to a running job step.
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  LLNL-CODE-402394.
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
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/hostlist.h"
#include "src/common/slurm_cred.h"
#include "src/common/bitstring.h"
#include "src/common/net.h"
#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/slurm_auth.h"
#include "src/common/forward.h"
#include "src/api/step_io.h"

#include "src/sattach/opt.h"
#include "src/sattach/attach.h"

static void _mpir_init(int num_tasks);
static void _mpir_cleanup(void);
static void _mpir_dump_proctable(void);
static void print_layout_info(slurm_step_layout_t *layout);
static slurm_cred_t _generate_fake_cred(uint32_t jobid, uint32_t stepid,
					uid_t uid, char *nodelist, 
					uint32_t node_cnt);
static uint32_t _nodeid_from_layout(slurm_step_layout_t *layout,
				    uint32_t taskid);
static int _attach_to_tasks(uint32_t jobid,
			    uint32_t stepid,
			    slurm_step_layout_t *layout,
			    slurm_cred_t fake_cred,
			    uint16_t num_resp_ports,
			    uint16_t *resp_ports,
			    int num_io_ports,
			    uint16_t *io_ports,
			    bitstr_t *tasks_started);

/**********************************************************************
 * Message handler declarations
 **********************************************************************/
typedef struct message_thread_state {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bitstr_t *tasks_started; /* or attempted to start, but failed */
	bitstr_t *tasks_exited;  /* or never started correctly */
	eio_handle_t *msg_handle;
	pthread_t msg_thread;
	uint16_t num_resp_port;
	uint16_t *resp_port; /* array of message response ports */
} message_thread_state_t;
static message_thread_state_t *_msg_thr_create(int num_nodes, int num_tasks);
static void _msg_thr_wait(message_thread_state_t *mts);
static void _msg_thr_destroy(message_thread_state_t *mts);
static void _handle_msg(message_thread_state_t *mts, slurm_msg_t *msg);
static bool _message_socket_readable(eio_obj_t *obj);
static int _message_socket_accept(eio_obj_t *obj, List objs);
static struct io_operations message_socket_ops = {
	readable:	&_message_socket_readable,
	handle_read:	&_message_socket_accept
};

/**********************************************************************
 * sattach
 **********************************************************************/
int sattach(int argc, char *argv[])
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_step_layout_t *layout;
	slurm_cred_t fake_cred;
	message_thread_state_t *mts;
	client_io_t *io;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("sattach parameter parsing");
	}
	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	layout = slurm_job_step_layout_get(opt.jobid, opt.stepid);
	if (layout == NULL) {
		error("Could not get job step info: %m");
		return 1;
	}
	if (opt.layout_only) {
		print_layout_info(layout);
		exit(0);
	}

	totalview_jobid = NULL;
	xstrfmtcat(totalview_jobid, "%u", opt.jobid);
	_mpir_init(layout->task_cnt);
	if (opt.input_filter_set) {
		opt.fds.in.nodeid =
			_nodeid_from_layout(layout, opt.fds.in.taskid);
	}

	fake_cred = _generate_fake_cred(opt.jobid, opt.stepid,
					opt.uid, layout->node_list,
					layout->node_cnt);
	
	mts = _msg_thr_create(layout->node_cnt, layout->task_cnt);

	io = client_io_handler_create(opt.fds, layout->task_cnt,
				      layout->node_cnt, fake_cred,
				      opt.labelio);
	client_io_handler_start(io);

	_attach_to_tasks(opt.jobid, opt.stepid, layout, fake_cred,
			 mts->num_resp_port, mts->resp_port,
			 io->num_listen, io->listenport,
			 mts->tasks_started);
	
	MPIR_debug_state = MPIR_DEBUG_SPAWNED;
	MPIR_Breakpoint();
	if (opt.debugger_test)
		_mpir_dump_proctable();

	_msg_thr_wait(mts);
	_msg_thr_destroy(mts);
	slurm_job_step_layout_free(layout);
	client_io_handler_finish(io);
	client_io_handler_destroy(io);
	_mpir_cleanup();

	return 0;
}

static uint32_t
_nodeid_from_layout(slurm_step_layout_t *layout, uint32_t taskid)
{
	uint32_t i, nodeid;

	for (nodeid = 0; nodeid < layout->node_cnt; nodeid++) {
		for (i = 0; i < layout->tasks[nodeid]; i++) {
			if (layout->tids[nodeid][i] == taskid) {
				debug3("task %d is on node %d",
				       taskid, nodeid);
				return nodeid;
			}
		}
	}

	return -1; /* node ID not found */
}

static void print_layout_info(slurm_step_layout_t *layout)
{
	hostlist_t nl;
	int i, j;

	printf("Job step layout:\n");
	printf("\t%d tasks, %d nodes (%s)\n\n",
	       layout->task_cnt, layout->node_cnt, layout->node_list);
	nl = hostlist_create(layout->node_list);
	for (i = 0; i < layout->node_cnt; i++) {
		char *name = hostlist_nth(nl, i);
		printf("\tNode %d (%s), %d task(s): ",
		       i, name, layout->tasks[i]);
		for (j = 0; j < layout->tasks[i]; j++) {
			printf("%d ", layout->tids[i][j]);
		}
		printf("\n");
		free(name);
	}
}


/* return a faked job credential */
static slurm_cred_t _generate_fake_cred(uint32_t jobid, uint32_t stepid,
					uid_t uid, char *nodelist,
					uint32_t node_cnt)
{
	slurm_cred_arg_t arg;
	slurm_cred_t cred;

	arg.jobid    = jobid;
	arg.stepid   = stepid;
	arg.uid      = uid;
	arg.hostlist = nodelist;

	arg.core_bitmap   = bit_alloc(node_cnt);
	bit_nset(arg.core_bitmap, 0, node_cnt-1);
	arg.cores_per_socket = xmalloc(sizeof(uint16_t));
	arg.cores_per_socket[0] = 1;
	arg.sockets_per_node = xmalloc(sizeof(uint16_t));
	arg.sockets_per_node[0] = 1;
	arg.sock_core_rep_count = xmalloc(sizeof(uint32_t));
	arg.sock_core_rep_count[0] = node_cnt;
	arg.job_nhosts    = node_cnt;
	arg.job_hostlist  = nodelist;

	cred = slurm_cred_faker(&arg);

	bit_free(arg.core_bitmap);
	xfree(arg.cores_per_socket);
	xfree(arg.sockets_per_node);
	xfree(arg.sock_core_rep_count);
	return cred;
}

void _handle_response_msg(slurm_msg_type_t msg_type, void *msg,
			  bitstr_t *tasks_started)
{
	reattach_tasks_response_msg_t *resp;
	MPIR_PROCDESC *table;
	int i;

	switch(msg_type) {
	case RESPONSE_REATTACH_TASKS:
		resp = (reattach_tasks_response_msg_t *)msg;
		if (resp->return_code != SLURM_SUCCESS) {
			info("Node %s: no tasks running", resp->node_name);
			break;
		}

		debug("Node %s, %d tasks", resp->node_name, resp->ntasks);
		for (i = 0; i < resp->ntasks; i++) {
			bit_set(tasks_started, resp->gtids[i]);
			table = &MPIR_proctable[resp->gtids[i]];
			/* FIXME - node_name is not necessarily
			   a valid hostname */
			table->host_name = xstrdup(resp->node_name);
			table->executable_name =
				xstrdup(resp->executable_names[i]);
			table->pid = (int)resp->local_pids[i];
			debug("\tTask id %u has pid %u, executable name: %s",
			      resp->gtids[i], resp->local_pids[i],
			      resp->executable_names[i]);
		}
		break;
	default:
		error("Unrecognized response to REQUEST_REATTACH_TASKS: %d",
		      msg_type);
		break;
	}
}

void _handle_response_msg_list(List other_nodes_resp, bitstr_t *tasks_started)
{
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	uint32_t msg_rc;

	itr = list_iterator_create(other_nodes_resp);
	while ((ret_data_info = list_next(itr))) {
		msg_rc = slurm_get_return_code(ret_data_info->type,
					       ret_data_info->data);
		debug("Attach returned msg_rc=%d err=%d type=%d",
		      msg_rc, ret_data_info->err, ret_data_info->type);
		if (msg_rc != SLURM_SUCCESS) 
			errno = ret_data_info->err;
		_handle_response_msg(ret_data_info->type,
				     ret_data_info->data,
				     tasks_started);
	}
	list_iterator_destroy(itr);
}

/*
 * All parameters are inputs EXCEPT for tasks_started, which is an OUTPUT.
 * A bit is set in tasks_started for each task for which we receive a
 * reattach response message stating that the task is still running.
 */
static int _attach_to_tasks(uint32_t jobid,
			    uint32_t stepid,
			    slurm_step_layout_t *layout,
			    slurm_cred_t fake_cred,
			    uint16_t num_resp_ports,
			    uint16_t *resp_ports,
			    int num_io_ports,
			    uint16_t *io_ports,
			    bitstr_t *tasks_started)
{
	slurm_msg_t msg;
	List nodes_resp = NULL;
	int timeout;
	reattach_tasks_request_msg_t reattach_msg;

	slurm_msg_t_init(&msg);
	
	timeout = slurm_get_msg_timeout() * 1000; /* sec to msec */

	reattach_msg.job_id = jobid;
	reattach_msg.job_step_id = stepid;
	reattach_msg.num_resp_port = num_resp_ports;
	reattach_msg.resp_port = resp_ports; /* array of response ports */
	reattach_msg.num_io_port = num_io_ports;
	reattach_msg.io_port = io_ports;
	reattach_msg.cred = fake_cred;

	msg.msg_type = REQUEST_REATTACH_TASKS;
	msg.data = &reattach_msg;
	
	nodes_resp = slurm_send_recv_msgs(layout->node_list, &msg, 
					  timeout, false);
	if (nodes_resp == NULL) {
		error("slurm_send_recv_msgs failed: %m");
		return SLURM_ERROR;
	}

	_handle_response_msg_list(nodes_resp, tasks_started);
	list_destroy(nodes_resp);
	
	return SLURM_SUCCESS;
}

/**********************************************************************
 * Message handler functions
 **********************************************************************/
static void *_msg_thr_internal(void *arg)
{
	message_thread_state_t *mts = (message_thread_state_t *)arg;

	eio_handle_mainloop(mts->msg_handle);

	return NULL;
}

static inline int
_estimate_nports(int nclients, int cli_per_port)
{
	div_t d;
	d = div(nclients, cli_per_port);
	return d.rem > 0 ? d.quot + 1 : d.quot;
}

static message_thread_state_t *_msg_thr_create(int num_nodes, int num_tasks)
{
	int sock = -1;
	short port = -1;
	eio_obj_t *obj;
	int i;
	message_thread_state_t *mts;
	pthread_attr_t attr;

	debug("Entering _msg_thr_create()");
	mts = (message_thread_state_t *)xmalloc(sizeof(message_thread_state_t));
	pthread_mutex_init(&mts->lock, NULL);
	pthread_cond_init(&mts->cond, NULL);
	mts->tasks_started = bit_alloc(num_tasks);
	mts->tasks_exited = bit_alloc(num_tasks);
	mts->msg_handle = eio_handle_create();
	mts->num_resp_port = _estimate_nports(num_nodes, 48);
	mts->resp_port = xmalloc(sizeof(uint16_t) * mts->num_resp_port);
	for (i = 0; i < mts->num_resp_port; i++) {
		if (net_stream_listen(&sock, &port) < 0) {
			error("unable to intialize step launch"
			      " listening socket: %m");
			goto fail;
		}
		mts->resp_port[i] = port;
		obj = eio_obj_create(sock, &message_socket_ops, (void *)mts);
		eio_new_initial_obj(mts->msg_handle, obj);
	}

	slurm_attr_init(&attr);
	if (pthread_create(&mts->msg_thread, &attr,
			   _msg_thr_internal, (void *)mts) != 0) {
		error("pthread_create of message thread: %m");
		slurm_attr_destroy(&attr);
		goto fail;
	}
	slurm_attr_destroy(&attr);

	return mts;
fail:
	eio_handle_destroy(mts->msg_handle);
	xfree(mts->resp_port);
	xfree(mts);
	return NULL;
}

static void _msg_thr_wait(message_thread_state_t *mts)
{
	/* Wait for all known running tasks to complete */
	pthread_mutex_lock(&mts->lock);
	while (bit_set_count(mts->tasks_exited)
	       < bit_set_count(mts->tasks_started)) {
		pthread_cond_wait(&mts->cond, &mts->lock);
	}
	pthread_mutex_unlock(&mts->lock);
}

static void _msg_thr_destroy(message_thread_state_t *mts)
{
	eio_signal_shutdown(mts->msg_handle);
	pthread_join(mts->msg_thread, NULL);
	eio_handle_destroy(mts->msg_handle);
	pthread_mutex_destroy(&mts->lock);
	pthread_cond_destroy(&mts->cond);
	bit_free(mts->tasks_started);
	bit_free(mts->tasks_exited);
}

static bool _message_socket_readable(eio_obj_t *obj)
{
	debug3("Called _message_socket_readable");
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			debug2("  false, shutdown");
			close(obj->fd);
			obj->fd = -1;
			/*_wait_for_connections();*/
		} else {
			debug2("  false");
		}
		return false;
	}
	return true;
}

static int _message_socket_accept(eio_obj_t *obj, List objs)
{
	message_thread_state_t *mts = (message_thread_state_t *)obj->arg;

	int fd;
	unsigned char *uc;
	short        port;
	struct sockaddr_un addr;
	slurm_msg_t *msg = NULL;
	int len = sizeof(addr);
	int          timeout = 0;	/* slurm default value */

	debug3("Called _msg_socket_accept");

	while ((fd = accept(obj->fd, (struct sockaddr *)&addr,
			    (socklen_t *)&len)) < 0) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN
		    || errno == ECONNABORTED
		    || errno == EWOULDBLOCK) {
			return SLURM_SUCCESS;
		}
		error("Error on msg accept socket: %m");
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	fd_set_close_on_exec(fd);
	fd_set_blocking(fd);

	/* Should not call slurm_get_addr() because the IP may not be
	   in /etc/hosts. */
	uc = (unsigned char *)&((struct sockaddr_in *)&addr)->sin_addr.s_addr;
	port = ((struct sockaddr_in *)&addr)->sin_port;
	debug2("got message connection from %u.%u.%u.%u:%hu",
	       uc[0], uc[1], uc[2], uc[3], ntohs(port));
	fflush(stdout);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);

	timeout = slurm_get_msg_timeout() * 1000;
again:
	if(slurm_receive_msg(fd, msg, timeout) != 0) {
		if (errno == EINTR) {
			goto again;
		}
		error("slurm_receive_msg[%u.%u.%u.%u]: %m",
		      uc[0],uc[1],uc[2],uc[3]);
		goto cleanup;
	}

	_handle_msg(mts, msg); /* handle_msg frees msg->data */
cleanup:
	if ((msg->conn_fd >= 0) && slurm_close_accepted_conn(msg->conn_fd) < 0)
		error ("close(%d): %m", msg->conn_fd);
	slurm_free_msg(msg);

	return SLURM_SUCCESS;
}

static void
_launch_handler(message_thread_state_t *mts, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = resp->data;
	int i;

	pthread_mutex_lock(&mts->lock);

	for (i = 0; i < msg->count_of_pids; i++) {
		bit_set(mts->tasks_started, msg->task_ids[i]);
	}

	pthread_cond_signal(&mts->cond);
	pthread_mutex_unlock(&mts->lock);

}

static void 
_exit_handler(message_thread_state_t *mts, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;
	int i;
	int rc;

	if ((msg->job_id != opt.jobid) || (msg->step_id != opt.stepid)) {
		debug("Received MESSAGE_TASK_EXIT from wrong job: %u.%u",
		      msg->job_id, msg->step_id);
		return;
	}

	pthread_mutex_lock(&mts->lock);

	for (i = 0; i < msg->num_tasks; i++) {
		debug("task %d done", msg->task_id_list[i]);
		bit_set(mts->tasks_exited, msg->task_id_list[i]);
	}

	verbose("%d tasks finished (rc=%u)",
		msg->num_tasks, msg->return_code);
	if (WIFEXITED(msg->return_code)) {
		rc = WEXITSTATUS(msg->return_code);
		if (rc != 0) {
			for (i = 0; i < msg->num_tasks; i++) {
				error("task %u exited with exit code %d",
				      msg->task_id_list[i], rc);
			}
		}
	} else if (WIFSIGNALED(msg->return_code)) {
		for (i = 0; i < msg->num_tasks; i++) {
			verbose("task %u killed by signal %d",
				msg->task_id_list[i],
				WTERMSIG(msg->return_code));
		}
		rc = 1;
	}

	pthread_cond_signal(&mts->cond);
	pthread_mutex_unlock(&mts->lock);
}

static void
_handle_msg(message_thread_state_t *mts, slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred, NULL);
	static uid_t slurm_uid;
	static bool slurm_uid_set = false;
	uid_t uid = getuid();
	
	if (!slurm_uid_set) {
		slurm_uid = slurm_get_slurm_user_id();
		slurm_uid_set = true;
	}

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type) {
	case RESPONSE_LAUNCH_TASKS:
		debug2("received task launch");
		_launch_handler(mts, msg);
		slurm_free_launch_tasks_response_msg(msg->data);
		break;
	case MESSAGE_TASK_EXIT:
		debug2("received task exit");
		_exit_handler(mts, msg);
		slurm_free_task_exit_msg(msg->data);
		break;
	case SRUN_JOB_COMPLETE:
		debug2("received job step complete message");
		/* FIXME - does nothing yet */
		slurm_free_srun_job_complete_msg(msg->data);
		break;
	default:
		error("received spurious message type: %d",
		      msg->msg_type);
		break;
	}
	return;
}

/**********************************************************************
 * Functions for manipulating the MPIR_* global variables which
 * are accessed by parallel debuggers which trace sattach.
 **********************************************************************/
static void
_mpir_init(int num_tasks)
{
	MPIR_proctable_size = num_tasks;
	MPIR_proctable = xmalloc(sizeof(MPIR_PROCDESC) * num_tasks);
	if (MPIR_proctable == NULL)
		fatal("Unable to initialize MPIR_proctable: %m");
}

static void
_mpir_cleanup()
{
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		xfree(MPIR_proctable[i].host_name);
		xfree(MPIR_proctable[i].executable_name);
	}
	xfree(MPIR_proctable);
}

static void
_mpir_dump_proctable()
{
	MPIR_PROCDESC *tv;
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		tv = &MPIR_proctable[i];
		if (!tv)
			break;
		info("task:%d, host:%s, pid:%d, executable:%s",
		     i, tv->host_name, tv->pid, tv->executable_name);
	}
}
	
