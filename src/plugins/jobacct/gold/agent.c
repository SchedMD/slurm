/****************************************************************************\
 *  agent.c - Agent to queue and process pending Gold requests
 *  Largely copied from src/common/slurmdbd_defs.c in Slurm v1.3
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "agent.h"
#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/pack.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#define _DEBUG		0
#define GOLD_MAGIC	0xDEAD3219
#define MAX_AGENT_QUEUE	10000
#define MAX_GOLD_MSG_LEN 16384

static pthread_mutex_t agent_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cond = PTHREAD_COND_INITIALIZER;
static List      agent_list     = (List) NULL;
static pthread_t agent_tid      = 0;
static time_t    agent_shutdown = 0;

static void * _agent(void *x);
static void   _agent_queue_del(void *x);
static void   _create_agent(void);
static Buf    _load_gold_rec(int fd);
static void   _load_gold_state(void);
static int    _process_msg(Buf buffer);
static int    _save_gold_rec(int fd, Buf buffer);
static void   _save_gold_state(void);
static void   _sig_handler(int signal);
static void   _shutdown_agent(void);

/****************************************************************************
 * External APIs for use by jobacct_gold.c
 ****************************************************************************/

/* Initiated a Gold message agent. Recover any saved RPCs. */
extern int gold_agent_init(void)
{
	slurm_mutex_lock(&agent_lock);
	if ((agent_tid == 0) || (agent_list == NULL))
		_create_agent();
	slurm_mutex_unlock(&agent_lock);

	return SLURM_SUCCESS;
}

/* Terminate a Gold message agent. Save any pending RPCs. */
extern int gold_agent_fini(void)
{
	/* NOTE: agent_lock not needed for _shutdown_agent() */
	_shutdown_agent();

	return SLURM_SUCCESS;
}

/* Send an RPC to the Gold. Do not wait for the reply. The RPC
 * will be queued and processed later if Gold is not responding.
 * Returns SLURM_SUCCESS or an error code */
extern int gold_agent_xmit(gold_agent_msg_t *req)
{
	Buf buffer;
	int cnt, rc = SLURM_SUCCESS;
	static time_t syslog_time = 0;

	buffer = init_buf(MAX_GOLD_MSG_LEN);
	pack16(req->msg_type, buffer);
	switch (req->msg_type) {
		case GOLD_MSG_CLUSTER_PROCS:
			gold_agent_pack_cluster_procs_msg(
				(gold_cluster_procs_msg_t *) req->data, buffer);
			break;
		case GOLD_MSG_JOB_COMPLETE:
			gold_agent_pack_job_info_msg(
				(gold_job_info_msg_t *) req->data, buffer);
			break;
		case GOLD_MSG_JOB_START:
			gold_agent_pack_job_info_msg(
				(gold_job_info_msg_t *) req->data, buffer);
			break;
		case GOLD_MSG_NODE_DOWN:
			gold_agent_pack_node_down_msg(
				(gold_node_down_msg_t *) req->data, buffer);
			break;
		case GOLD_MSG_NODE_UP:
			gold_agent_pack_node_up_msg(
				(gold_node_up_msg_t *) req->data, buffer);
			break;
		case GOLD_MSG_STEP_START:
			gold_agent_pack_job_info_msg(
				(gold_job_info_msg_t *) req->data, buffer);
			break;
		default:
			error("gold: Invalid message send type %u",
			      req->msg_type);
			free_buf(buffer);
			return SLURM_ERROR;
	}

	slurm_mutex_lock(&agent_lock);
	if ((agent_tid == 0) || (agent_list == NULL)) {
		_create_agent();
		if ((agent_tid == 0) || (agent_list == NULL)) {
			slurm_mutex_unlock(&agent_lock);
			free_buf(buffer);
			return SLURM_ERROR;
		}
	}
	cnt = list_count(agent_list);
#if _DEBUG
        info("gold agent: queuing msg_type %u queue_len %d", 
	     req->msg_type, cnt);
#endif
	if ((cnt >= (MAX_AGENT_QUEUE / 2)) &&
	    (difftime(time(NULL), syslog_time) > 120)) {
		/* Log critical error every 120 seconds */
		syslog_time = time(NULL);
		error("gold: agent queue filling, RESTART GOLD NOW");
		syslog(LOG_CRIT, "*** RESTART GOLD NOW ***");
	}
	if (cnt < MAX_AGENT_QUEUE) {
		if (list_enqueue(agent_list, buffer) == NULL)
			fatal("list_enqueue: memory allocation failure");
	} else {
		error("gold: agent queue is full, discarding request");
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&agent_lock);
	pthread_cond_broadcast(&agent_cond);
	return rc;
}

/****************************************************************************
 * Functions for agent to manage queue of pending message for Gold
 ****************************************************************************/
static void _create_agent(void)
{
	if (agent_list == NULL) {
		agent_list = list_create(_agent_queue_del);
		if (agent_list == NULL)
			fatal("list_create: malloc failure");
		_load_gold_state();
	}

	if (agent_tid == 0) {
		pthread_attr_t agent_attr;
		slurm_attr_init(&agent_attr);
		pthread_attr_setdetachstate(&agent_attr, 
					    PTHREAD_CREATE_DETACHED);
		if (pthread_create(&agent_tid, &agent_attr, _agent, NULL) ||
		    (agent_tid == 0))
			fatal("pthread_create: %m");
	}
}

static void _agent_queue_del(void *x)
{
	Buf buffer = (Buf) x;
	free_buf(buffer);
}

static void _shutdown_agent(void)
{
	int i;

	if (agent_tid) {
		agent_shutdown = time(NULL);
		pthread_cond_broadcast(&agent_cond);
		for (i=0; ((i<10) && agent_tid); i++) {
			sleep(1);
			pthread_cond_broadcast(&agent_cond);
			if (pthread_kill(agent_tid, SIGUSR1))
				agent_tid = 0;
		}
		if (agent_tid) {
			error("gold: agent failed to shutdown gracefully");
		} else
			agent_shutdown = 0;
	}
}

static void *_agent(void *x)
{
	int cnt, rc;
	Buf buffer;
	struct timespec abs_time;
	static time_t fail_time = 0;
	int sigarray[] = {SIGUSR1, 0};

	/* Prepare to catch SIGUSR1 to interrupt pending
	 * I/O and terminate in a timely fashion. */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	while (agent_shutdown == 0) {
		slurm_mutex_lock(&agent_lock);
		if (agent_list)
			cnt = list_count(agent_list);
		else
			cnt = 0;
		if ((cnt == 0) ||
		    (fail_time && (difftime(time(NULL), fail_time) < 10))) {
			abs_time.tv_sec  = time(NULL) + 10;
			abs_time.tv_nsec = 0;
			rc = pthread_cond_timedwait(&agent_cond, &agent_lock,
						    &abs_time);
			slurm_mutex_unlock(&agent_lock);
			continue;
		} else if ((cnt > 0) && ((cnt % 50) == 0))
			info("gold: agent queue size %u", cnt);
		/* Leave item on the queue until processing complete */
		if (agent_list)
			buffer = (Buf) list_peek(agent_list);
		else
			buffer = NULL;
		slurm_mutex_unlock(&agent_lock);
		if (buffer == NULL)
			continue;

		/* NOTE: agent_lock is clear here, so we can add more
		 * requests to the queue while waiting for this RPC to 
		 * complete. */
		rc = _process_msg(buffer);
		if (rc != SLURM_SUCCESS) {
			if (agent_shutdown)
				break;
			error("gold: Failure sending message");
		}

		slurm_mutex_lock(&agent_lock);
		if (agent_list && (rc != EAGAIN)) {
			buffer = (Buf) list_dequeue(agent_list);
			free_buf(buffer);
			fail_time = 0;
		} else {
			fail_time = time(NULL);
		}
		slurm_mutex_unlock(&agent_lock);
	}

	slurm_mutex_lock(&agent_lock);
	_save_gold_state();
	if (agent_list) {
		list_destroy(agent_list);
		agent_list = NULL;
	}
	slurm_mutex_unlock(&agent_lock);
	return NULL;
}

static int _process_msg(Buf buffer)
{
	int rc;
	uint16_t msg_type;
	uint32_t msg_size;

	/* We save the full buffer size in case the RPC fails 
	 * and we need to save state for later recovery. */ 
	msg_size = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	safe_unpack16(&msg_type, buffer);
#if _DEBUG
	info("gold agent: processing msg_type %u", msg_type);
#endif
	switch (msg_type) {
		case GOLD_MSG_CLUSTER_PROCS:
			rc = agent_cluster_procs(buffer);
			break;
		case GOLD_MSG_JOB_COMPLETE:
			rc = agent_job_complete(buffer);
			break;
		case GOLD_MSG_JOB_START:
			rc = agent_job_start(buffer);
			break;
		case GOLD_MSG_NODE_DOWN:
			rc = agent_node_down(buffer);
			break;
		case GOLD_MSG_NODE_UP:
			rc = agent_node_up(buffer);
			break;
		case GOLD_MSG_STEP_START:
			rc = agent_step_start(buffer);
			break;
		default:
			error("gold: Invalid send message type %u", msg_type);
			rc = SLURM_ERROR;	/* discard entry and continue */
	}
	set_buf_offset(buffer, msg_size);	/* restore buffer size */
	return rc;

unpack_error:
	/* If the message format is bad return SLURM_SUCCESS to get
	 * it off of the queue since we can't work with it anyway */
	error("gold agent: message unpack error");
	return SLURM_ERROR;
}

static void _save_gold_state(void)
{
	char *gold_fname;
	Buf buffer;
	int fd, rc, wrote = 0;

	gold_fname = slurm_get_state_save_location();
	xstrcat(gold_fname, "/gold.messages");
	fd = open(gold_fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error("gold: Creating state save file %s", gold_fname);
	} else if (agent_list) {
		while ((buffer = list_dequeue(agent_list))) {
			rc = _save_gold_rec(fd, buffer);
			free_buf(buffer);
			if (rc != SLURM_SUCCESS)
				break;
			wrote++;
		}
	}
	if (fd >= 0) {
		verbose("gold: saved %d pending RPCs", wrote);
		(void) close(fd);
	}
	xfree(gold_fname);
}

static void _load_gold_state(void)
{
	char *gold_fname;
	Buf buffer;
	int fd, recovered = 0;

	gold_fname = slurm_get_state_save_location();
	xstrcat(gold_fname, "/gold.messages");
	fd = open(gold_fname, O_RDONLY);
	if (fd < 0) {
		error("gold: Opening state save file %s", gold_fname);
	} else {
		while (1) {
			buffer = _load_gold_rec(fd);
			if (buffer == NULL)
				break;
			if (list_enqueue(agent_list, buffer) == NULL)
				fatal("gold: list_enqueue, no memory");
			recovered++;
		}
	}
	if (fd >= 0) {
		verbose("gold: recovered %d pending RPCs", recovered);
		(void) close(fd);
		(void) unlink(gold_fname);	/* clear save state */
	}
	xfree(gold_fname);
}

static int _save_gold_rec(int fd, Buf buffer)
{
	ssize_t size, wrote;
	uint32_t msg_size = get_buf_offset(buffer);
	uint32_t magic = GOLD_MAGIC;
	char *msg = get_buf_data(buffer);

	size = sizeof(msg_size);
	wrote = write(fd, &msg_size, size);
	if (wrote != size) {
		error("gold: state save error: %m");
		return SLURM_ERROR;
	}

	wrote = 0;
	while (wrote < msg_size) {
		wrote = write(fd, msg, msg_size);
		if (wrote > 0) {
			msg += wrote;
			msg_size -= wrote;
		} else if ((wrote == -1) && (errno == EINTR))
			continue;
		else {
			error("gold: state save error: %m");
			return SLURM_ERROR;
		}
	}	

	size = sizeof(magic);
	wrote = write(fd, &magic, size);
	if (wrote != size) {
		error("gold: state save error: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static Buf _load_gold_rec(int fd)
{
	ssize_t size, rd_size;
	uint32_t msg_size, magic;
	char *msg;
	Buf buffer;

	size = sizeof(msg_size);
	rd_size = read(fd, &msg_size, size);
	if (rd_size == 0)
		return (Buf) NULL;
	if (rd_size != size) {
		error("gold: state recover error: %m");
		return (Buf) NULL;
	}
	if (msg_size > MAX_GOLD_MSG_LEN) {
		error("gold: state recover error, msg_size=%u", msg_size);
		return (Buf) NULL;
	}

	buffer = init_buf((int) msg_size);
	if (buffer == NULL)
		fatal("gold: create_buf malloc failure");
	set_buf_offset(buffer, msg_size);
	msg = get_buf_data(buffer);
	size = msg_size;
	while (size) {
		rd_size = read(fd, msg, size);
		if (rd_size > 0) {
			msg += rd_size;
			size -= rd_size;
		} else if ((rd_size == -1) && (errno == EINTR))
			continue;
		else {
			error("gold: state recover error: %m");
			free_buf(buffer);
			return (Buf) NULL;
		}
	}

	size = sizeof(magic);
	rd_size = read(fd, &magic, size);
	if ((rd_size != size) || (magic != GOLD_MAGIC)) {
		error("gold: state recover error");
		free_buf(buffer);
		return (Buf) NULL;
	}

	return buffer;
}

static void _sig_handler(int signal)
{
}

/****************************************************************************\
 * Free data structures
\****************************************************************************/
void inline gold_agent_free_cluster_procs_msg(gold_cluster_procs_msg_t *msg)
{
	xfree(msg);
}

void inline gold_agent_free_job_info_msg(gold_job_info_msg_t *msg)
{
	if (msg) {
		xfree(msg->account);
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg->partition);
		xfree(msg);
	}
}

void inline gold_agent_free_node_down_msg(gold_node_down_msg_t *msg)
{
	if (msg) {
		xfree(msg->hostlist);
		xfree(msg->reason);
		xfree(msg);
	}
}

void inline gold_agent_free_node_up_msg(gold_node_up_msg_t *msg)
{
	if (msg) {
		xfree(msg->hostlist);
		xfree(msg);
	}
}

/****************************************************************************\
 * Pack and unpack data structures
\****************************************************************************/
void inline
gold_agent_pack_cluster_procs_msg(gold_cluster_procs_msg_t *msg, Buf buffer)
{
	pack32(msg->proc_count,    buffer);
	pack_time(msg->event_time, buffer);
}
int inline
gold_agent_unpack_cluster_procs_msg(gold_cluster_procs_msg_t **msg, Buf buffer)
{
	gold_cluster_procs_msg_t *msg_ptr;

	msg_ptr = xmalloc(sizeof(gold_cluster_procs_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->proc_count, buffer);
	safe_unpack_time(&msg_ptr->event_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
gold_agent_pack_job_info_msg(gold_job_info_msg_t *msg, Buf buffer)
{
	packstr(msg->account, buffer);
	pack_time(msg->begin_time, buffer);
	pack_time(msg->end_time, buffer);
	pack32(msg->exit_code, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->job_state, buffer);
	packstr(msg->name, buffer);
	packstr(msg->nodes, buffer);
	packstr(msg->partition, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->submit_time, buffer);
	pack32(msg->total_procs, buffer);
	pack32(msg->user_id, buffer);
}

int inline 
gold_agent_unpack_job_info_msg(gold_job_info_msg_t **msg, Buf buffer)
{
	uint16_t uint16_tmp;
	gold_job_info_msg_t *msg_ptr = xmalloc(sizeof(gold_job_info_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->account, &uint16_tmp, buffer);
	safe_unpack_time(&msg_ptr->begin_time, buffer);
	safe_unpack_time(&msg_ptr->end_time, buffer);
	safe_unpack32(&msg_ptr->exit_code, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack16(&msg_ptr->job_state, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->name, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->partition, &uint16_tmp, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->submit_time, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	safe_unpack32(&msg_ptr->user_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->account);
	xfree(msg_ptr->name);
	xfree(msg_ptr->nodes);
	xfree(msg_ptr->partition);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
gold_agent_pack_node_down_msg(gold_node_down_msg_t *msg, Buf buffer)
{
	pack16(msg->cpus, buffer);
	pack_time(msg->event_time, buffer);
	packstr(msg->hostlist, buffer);
	packstr(msg->reason, buffer);
}

int inline
gold_agent_unpack_node_down_msg(gold_node_down_msg_t **msg, Buf buffer)
{
	gold_node_down_msg_t *msg_ptr;
	uint16_t uint16_tmp;

	msg_ptr = xmalloc(sizeof(gold_node_down_msg_t));
	*msg = msg_ptr;
	safe_unpack16(&msg_ptr->cpus, buffer);
	safe_unpack_time(&msg_ptr->event_time, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->hostlist, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->reason,   &uint16_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->hostlist);
	xfree(msg_ptr->reason);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
gold_agent_pack_node_up_msg(gold_node_up_msg_t *msg, Buf buffer)
{
	pack_time(msg->event_time, buffer);
	packstr(msg->hostlist, buffer);
}

int inline
gold_agent_unpack_node_up_msg(gold_node_up_msg_t **msg, Buf buffer)
{
	gold_node_up_msg_t *msg_ptr;
	uint16_t uint16_tmp;

	msg_ptr = xmalloc(sizeof(gold_node_up_msg_t));
	*msg = msg_ptr;
	safe_unpack_time(&msg_ptr->event_time, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->hostlist, &uint16_tmp, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->hostlist);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}
