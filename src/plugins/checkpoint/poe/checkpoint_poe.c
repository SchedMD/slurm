/*****************************************************************************\
 *  checkpoint_poe.c - IBM POE checkpoint plugin.
 *
 *  This is based upon checkpoint support of poe in the 2005 time frame for
 *  the ASCI Purple computer. It does not work with current versions of POE.
 *  From Gary Mincher (IBM, Sept 6 2012): "Checkpoint/restart on Linux is only
 *  supported for user-space parallel jobs with a maximum of 512 tasks that are
 *  run on Power 775 nodes, but jobs that use a resource manager or scheduler
 *  other than LoadLeveler are not supported."
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2012 SchedMD LLC.
 *  Written by Morris Jette <jette1@llnl.gov> and <jette@schedmd.com>
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

#include "config.h"

#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
struct node_record *node_record_table_ptr = NULL;
int node_record_count = 0;

struct check_job_info {
	uint16_t disabled;	/* counter, checkpointable only if zero */
	uint16_t node_cnt;
	uint16_t reply_cnt;
	uint16_t wait_time;
	time_t   time_stamp;	/* begin or end checkpoint time */
	uint32_t error_code;
	char    *error_msg;
};

static void _send_sig(uint32_t job_id, uint32_t step_id, uint16_t signal,
		      char *node_name, slurm_addr_t node_addr);
static int  _step_sig(struct step_record * step_ptr, uint16_t wait,
		      uint16_t signal, uint16_t sig_timeout);

/* checkpoint request timeout processing */
static bool		ckpt_agent_stop = false;
static pthread_t	ckpt_agent_tid = 0;
static pthread_mutex_t	ckpt_agent_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	ckpt_agent_cond = PTHREAD_COND_INITIALIZER;
static List		ckpt_timeout_list = NULL;
struct ckpt_timeout_info {
	uint32_t   job_id;
	uint32_t   step_id;
	uint16_t   signal;
	time_t     start_time;
	time_t     end_time;
	char      *node_name;
	slurm_addr_t node_addr;
};
static void *_ckpt_agent_thr(void *arg);
static void  _ckpt_enqueue_timeout(uint32_t job_id, uint32_t step_id,
		time_t start_time, uint16_t signal, uint16_t wait_time,
		char *node_name, slurm_addr_t node_addr);
static void  _ckpt_dequeue_timeout(uint32_t job_id, uint32_t step_id,
		time_t start_time);
static void  _ckpt_timeout_free(void *rec);
static void  _ckpt_signal_step(struct ckpt_timeout_info *rec);

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
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "checkpoint" for SLURM checkpoint) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load checkpoint plugins if the plugin_type string has a
 * prefix of "checkpoint/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Checkpoint POE plugin";
const char plugin_type[]       	= "checkpoint/poe";
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	pthread_attr_t attr;

	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate: %m");
	if (pthread_create(&ckpt_agent_tid, &attr, _ckpt_agent_thr, NULL)) {
		error("pthread_create: %m");
		return SLURM_ERROR;
	}
	slurm_attr_destroy(&attr);

	return SLURM_SUCCESS;
}


extern int fini ( void )
{
	slurm_mutex_lock(&ckpt_agent_mutex);
	ckpt_agent_stop = true;
	slurm_cond_signal(&ckpt_agent_cond);
	slurm_mutex_unlock(&ckpt_agent_mutex);

	if (ckpt_agent_tid && pthread_join(ckpt_agent_tid, NULL)) {
		error("Could not kill checkpoint pthread");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM checkpoint API.
 */

extern int slurm_ckpt_op (uint32_t job_id, uint32_t step_id,
			  struct step_record *step_ptr, uint16_t op,
			  uint16_t data, char *image_dir, time_t *event_time,
			  uint32_t *error_code, char **error_msg )
{
	int rc = SLURM_SUCCESS;
	struct check_job_info *check_ptr;

	if (!step_ptr)
		return ESLURM_INVALID_JOB_ID;
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);

	switch (op) {
		case CHECK_ABLE:
			if (check_ptr->disabled)
				rc = ESLURM_DISABLED;
			else {
				if ((check_ptr->reply_cnt < check_ptr->node_cnt)
				    && event_time) {
					/* Return time of last event */
					*event_time = check_ptr->time_stamp;
				}
				rc = SLURM_SUCCESS;
			}
			break;
		case CHECK_DISABLE:
			check_ptr->disabled++;
			break;
		case CHECK_ENABLE:
			check_ptr->disabled--;
			break;
		case CHECK_CREATE:
			check_ptr->time_stamp = time(NULL);
			check_ptr->reply_cnt = 0;
			check_ptr->error_code = 0;
			xfree(check_ptr->error_msg);
#ifdef SIGSOUND
			rc = _step_sig(step_ptr, data, SIGSOUND, SIGWINCH);
#else
			/* No checkpoint, SIGWINCH for testing purposes */
			info("Checkpoint not supported, sending SIGWINCH");
			rc = _step_sig(step_ptr, data, SIGWINCH, SIGWINCH);
#endif
			break;
		case CHECK_VACATE:
			check_ptr->time_stamp = time(NULL);
			check_ptr->reply_cnt = 0;
			check_ptr->error_code = 0;
			xfree(check_ptr->error_msg);
#ifdef SIGMIGRATE
			rc = _step_sig(step_ptr, data, SIGMIGRATE, SIGTERM);
#else
			/* No checkpoint, kill job now, useful for testing */
			info("Checkpoint not supported, sending SIGTERM");
			rc = _step_sig(step_ptr, data, SIGTERM, SIGTERM);
#endif
			break;
		case CHECK_RESTART:
		case CHECK_REQUEUE:
			rc = ESLURM_NOT_SUPPORTED;
			break;
		case CHECK_ERROR:
			xassert(error_code);
			xassert(error_msg);
			*error_code = check_ptr->error_code;
			xfree(*error_msg);
			*error_msg = xstrdup(check_ptr->error_msg);
			break;
		default:
			error("Invalid checkpoint operation: %d", op);
			rc = EINVAL;
	}

	return rc;
}

extern int slurm_ckpt_comp ( struct step_record * step_ptr, time_t event_time,
		uint32_t error_code, char *error_msg )
{
	struct check_job_info *check_ptr;

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);

	if (event_time && (event_time != check_ptr->time_stamp))
		return ESLURM_ALREADY_DONE;

	if (error_code > check_ptr->error_code) {
		info("slurm_ckpt_comp for step %u.%u error %u: %s",
			step_ptr->job_ptr->job_id, step_ptr->step_id,
			error_code, error_msg);
		check_ptr->error_code = error_code;
		xfree(check_ptr->error_msg);
		check_ptr->error_msg = xstrdup(error_msg);
		return SLURM_SUCCESS;
	}

	/* We need an error-free reply from each compute node,
	 * plus POE itself to note completion */
	if (check_ptr->reply_cnt++ == check_ptr->node_cnt) {
		time_t now = time(NULL);
		long delay = (long) difftime(now, check_ptr->time_stamp);
		info("slurm_ckpt_comp for step %u.%u in %ld secs",
			step_ptr->job_ptr->job_id, step_ptr->step_id,
			delay);
		check_ptr->time_stamp = now;
		_ckpt_dequeue_timeout(step_ptr->job_ptr->job_id,
			step_ptr->step_id, event_time);
	}
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_alloc_job(check_jobinfo_t *jobinfo)
{
	*jobinfo = (check_jobinfo_t) xmalloc(sizeof(struct check_job_info));
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_free_job(check_jobinfo_t jobinfo)
{
	xfree(jobinfo);
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_pack_job(check_jobinfo_t jobinfo, Buf buffer,
			       uint16_t protocol_version)
{
	struct check_job_info *check_ptr =
		(struct check_job_info *)jobinfo;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t x;
		uint32_t y;
		uint32_t z;
		uint32_t size;

		size = 0;
		pack16(CHECK_POE, buffer);
		x = get_buf_offset(buffer);
		pack32(size, buffer);

		y = get_buf_offset(buffer);

		pack16(check_ptr->disabled, buffer);
		pack16(check_ptr->node_cnt, buffer);
		pack16(check_ptr->reply_cnt, buffer);
		pack16(check_ptr->wait_time, buffer);
		pack32(check_ptr->error_code, buffer);
		packstr(check_ptr->error_msg, buffer);
		pack_time(check_ptr->time_stamp, buffer);

		z = get_buf_offset(buffer);
		set_buf_offset(buffer, x);
		pack32(z - y, buffer);
		set_buf_offset(buffer, z);
	}

	return SLURM_SUCCESS;
}

extern int slurm_ckpt_unpack_job(check_jobinfo_t jobinfo, Buf buffer,
				 uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	struct check_job_info *check_ptr =
		(struct check_job_info *)jobinfo;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t id;
		uint32_t size;

		safe_unpack16(&id, buffer);
		safe_unpack32(&size, buffer);
		if (id != CHECK_POE) {
			uint32_t x;
			x = get_buf_offset(buffer);
			set_buf_offset(buffer, x + size);
		} else {
			safe_unpack16(&check_ptr->disabled, buffer);
			safe_unpack16(&check_ptr->node_cnt, buffer);
			safe_unpack16(&check_ptr->reply_cnt, buffer);
			safe_unpack16(&check_ptr->wait_time, buffer);
			safe_unpack32(&check_ptr->error_code, buffer);
			safe_unpackstr_xmalloc(&check_ptr->error_msg,
					       &uint32_tmp, buffer);
			safe_unpack_time(&check_ptr->time_stamp, buffer);
		}
	}

	return SLURM_SUCCESS;

    unpack_error:
	xfree(check_ptr->error_msg);
	return SLURM_ERROR;
}

extern check_jobinfo_t slurm_ckpt_copy_job(check_jobinfo_t jobinfo)
{
	struct check_job_info *jobinfo_src, *jobinfo_dest;

	jobinfo_src  = (struct check_job_info *)jobinfo_src;
	jobinfo_dest = xmalloc(sizeof(struct check_job_info));
	memcpy(jobinfo_dest, jobinfo_src, sizeof(struct check_job_info));
	jobinfo_dest->error_msg = xstrdup(jobinfo_src->error_msg);
	return (check_jobinfo_t) jobinfo_dest;
}

/* Send a signal RPC to a specific node */
static void _send_sig(uint32_t job_id, uint32_t step_id, uint16_t signal,
		char *node_name, slurm_addr_t node_addr)
{
	agent_arg_t *agent_args;
	kill_tasks_msg_t *kill_tasks_msg;

	kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
	kill_tasks_msg->job_id		= job_id;
	kill_tasks_msg->job_step_id	= step_id;
	kill_tasks_msg->signal		= signal;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type		= REQUEST_SIGNAL_TASKS;
	agent_args->retry		= 1;
	agent_args->msg_args		= kill_tasks_msg;
	agent_args->hostlist = hostlist_create(node_name);
	agent_args->node_count		= 1;

	if ((node_ptr = find_node_record(node_name)))
		agent_args->protocol_version = node_ptr->protocol_version;

	hostlist_iterator_destroy(hi);

	agent_queue_request(agent_args);
}

/* Send specified signal only to the process launched on node 0.
 * If the request times out, send sig_timeout. */
static int _step_sig(struct step_record * step_ptr, uint16_t wait,
		uint16_t signal, uint16_t sig_timeout)
{
	struct check_job_info *check_ptr;
	struct job_record *job_ptr;
	int i;

	xassert(step_ptr);
	check_ptr = (struct check_job_info *) step_ptr->check_job;
	xassert(check_ptr);
	job_ptr = step_ptr->job_ptr;
	xassert(job_ptr);

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (check_ptr->disabled)
		return ESLURM_DISABLED;

	check_ptr->node_cnt = 0;	/* re-calculate below */
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		if (check_ptr->node_cnt++ > 0)
			continue;
		_send_sig(step_ptr->job_ptr->job_id, step_ptr->step_id,
			signal, node_record_table_ptr[i].name,
			node_record_table_ptr[i].slurm_addr);
		_ckpt_enqueue_timeout(step_ptr->job_ptr->job_id,
			step_ptr->step_id, check_ptr->time_stamp,
			sig_timeout, wait, node_record_table_ptr[i].name,
			node_record_table_ptr[i].slurm_addr);
	}

	if (!check_ptr->node_cnt) {
		error("_step_sig: job %u.%u has no nodes", job_ptr->job_id,
			step_ptr->step_id);
		return ESLURM_INVALID_NODE_NAME;
	}

	check_ptr->time_stamp = time(NULL);
	check_ptr->wait_time  = wait;

	info("checkpoint requested for job %u.%u", job_ptr->job_id,
		step_ptr->step_id);
	return SLURM_SUCCESS;
}

static void _my_sleep(int secs)
{
	struct timespec ts = {0, 0};
	struct timeval now;

	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + secs;
	ts.tv_nsec = now.tv_usec * 1000;
	slurm_mutex_lock(&ckpt_agent_mutex);
	if (!ckpt_agent_stop)
		slurm_cond_timedwait(&ckpt_agent_cond,&ckpt_agent_mutex,&ts);
	slurm_mutex_unlock(&ckpt_agent_mutex);
}

/* Checkpoint processing pthread
 * Never returns, but is cancelled on plugin termiantion */
static void *_ckpt_agent_thr(void *arg)
{
	ListIterator iter;
	struct ckpt_timeout_info *rec;
	time_t now;

	while (1) {
		_my_sleep(1);
		if (ckpt_agent_stop)
			break;
		if (!ckpt_timeout_list)
			continue;

		now = time(NULL);
		iter = list_iterator_create(ckpt_timeout_list);
		slurm_mutex_lock(&ckpt_agent_mutex);
		/* look for and process any timeouts */
		while ((rec = list_next(iter))) {
			if (rec->end_time > now)
				continue;
			info("checkpoint timeout for %u.%u",
				rec->job_id, rec->step_id);
			_ckpt_signal_step(rec);
			list_delete_item(iter);
		}
		slurm_mutex_unlock(&ckpt_agent_mutex);
		list_iterator_destroy(iter);
	}
	return NULL;
}

static void _ckpt_signal_step(struct ckpt_timeout_info *rec)
{
	/* debug("signal %u.%u %u", rec->job_id, rec->step_id, rec->signal); */
	_send_sig(rec->job_id, rec->step_id, rec->signal,
		rec->node_name, rec->node_addr);
}

/* Queue a checkpoint request timeout */
static void _ckpt_enqueue_timeout(uint32_t job_id, uint32_t step_id,
		time_t start_time, uint16_t signal, uint16_t wait_time,
		char *node_name, slurm_addr_t node_addr)
{
	struct ckpt_timeout_info *rec;

	if ((wait_time == 0) || (signal == 0))
		return;

	slurm_mutex_lock(&ckpt_agent_mutex);
	if (!ckpt_timeout_list)
		ckpt_timeout_list = list_create(_ckpt_timeout_free);
	rec = xmalloc(sizeof(struct ckpt_timeout_info));
	rec->job_id	= job_id;
	rec->step_id	= step_id;
	rec->signal     = signal;
	rec->start_time	= start_time;
	rec->end_time	= start_time + wait_time;
	rec->node_name  = xstrdup(node_name);
	rec->node_addr  = node_addr;
	/* debug("enqueue %u.%u %u", job_id, step_id, wait_time); */
	list_enqueue(ckpt_timeout_list, rec);
	slurm_mutex_unlock(&ckpt_agent_mutex);
}

static void _ckpt_timeout_free(void *rec)
{
	struct ckpt_timeout_info *ckpt_rec = (struct ckpt_timeout_info *)rec;

	if (ckpt_rec) {
		xfree(ckpt_rec->node_name);
		xfree(ckpt_rec);
	}
}

/* De-queue a checkpoint timeout request. The operation completed */
static void _ckpt_dequeue_timeout(uint32_t job_id, uint32_t step_id,
		time_t start_time)
{
	ListIterator iter;
	struct ckpt_timeout_info *rec;

	slurm_mutex_lock(&ckpt_agent_mutex);
	if (!ckpt_timeout_list)
		goto fini;
	iter = list_iterator_create(ckpt_timeout_list);
	while ((rec = list_next(iter))) {
		if ((rec->job_id != job_id) || (rec->step_id != step_id) ||
		    (start_time && (rec->start_time != start_time)))
			continue;
		/* debug("dequeue %u.%u", job_id, step_id); */
		list_delete_item(iter);
		break;
	}
	list_iterator_destroy(iter);
    fini:
	slurm_mutex_unlock(&ckpt_agent_mutex);
}

extern int slurm_ckpt_task_comp ( struct step_record * step_ptr,
				  uint32_t task_id, time_t event_time,
				  uint32_t error_code, char *error_msg )
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_stepd_prefork(void *slurmd_job)
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_signal_tasks(void *slurmd_job)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int slurm_ckpt_restart_task(void *slurmd_job, char *image_dir, int gtid)
{
	return ESLURM_NOT_SUPPORTED;
}
