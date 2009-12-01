/*****************************************************************************\
 *  pmi_server.c - Global PMI data as maintained within srun
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <pthread.h>
#include <stdlib.h>
#include <slurm/slurm_errno.h>

#include "src/api/slurm_pmi.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#define _DEBUG           0	/* non-zero for extra KVS logging */
#define _DEBUG_TIMING    0	/* non-zero for KVS timing details */

static pthread_mutex_t kvs_mutex = PTHREAD_MUTEX_INITIALIZER;
static int kvs_comm_cnt = 0;
static int kvs_updated = 0;
static struct kvs_comm **kvs_comm_ptr = NULL;

/* Track time to process kvs put requests
 * This can be used to tune PMI_TIME environment variable */
static int min_time_kvs_put = 1000000;
static int max_time_kvs_put = 0;
static int tot_time_kvs_put = 0;

struct barrier_resp {
	uint16_t port;
	char *hostname;
};				/* details for barrier task communcations */
struct barrier_resp *barrier_ptr = NULL;
uint16_t barrier_resp_cnt = 0;	/* tasks having reached barrier */
uint16_t barrier_cnt = 0;	/* tasks needing to reach barrier */

pthread_mutex_t agent_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  agent_cond  = PTHREAD_COND_INITIALIZER;
struct agent_arg {
	struct barrier_resp *barrier_xmit_ptr;
	int barrier_xmit_cnt;
	struct kvs_comm **kvs_xmit_ptr;
	int kvs_xmit_cnt;
};				/* details for message agent manager */
struct msg_arg {
	struct barrier_resp *bar_ptr;
	struct kvs_comm_set *kvs_ptr;
};
int agent_cnt = 0;		/* number of active message agents */
int agent_max_cnt = 32;		/* maximum number of active agents */

static void *_agent(void *x);
static struct kvs_comm *_find_kvs_by_name(char *name);
struct kvs_comm **_kvs_comm_dup(void);
static void _kvs_xmit_tasks(void);
static void _merge_named_kvs(struct kvs_comm *kvs_orig,
		struct kvs_comm *kvs_new);
static void _move_kvs(struct kvs_comm *kvs_new);
static void *_msg_thread(void *x);
static void _print_kvs(void);

/* Transmit the KVS keypairs to all tasks, waiting at a barrier
 * This will take some time, so we work with a copy of the KVS keypairs.
 * We also work with a private copy of the barrier data and clear the
 * global data pointers so any new barrier requests get treated as
 * completely independent of this one. */
static void _kvs_xmit_tasks(void)
{
	struct agent_arg *args;
	pthread_attr_t attr;
	pthread_t agent_id;

#if _DEBUG
	info("All tasks at barrier, transmit KVS keypairs now");
#endif

	/* Target KVS_TIME should be about ave processing time */
	debug("kvs_put processing time min=%d, max=%d ave=%d (usec)",
		min_time_kvs_put, max_time_kvs_put,
		(tot_time_kvs_put / barrier_cnt));
	min_time_kvs_put = 1000000;
	max_time_kvs_put = 0;
	tot_time_kvs_put = 0;

	/* reset barrier info */
	args = xmalloc(sizeof(struct agent_arg));
	args->barrier_xmit_ptr = barrier_ptr;
	args->barrier_xmit_cnt = barrier_cnt;
	barrier_ptr = NULL;
	barrier_resp_cnt = 0;
	barrier_cnt = 0;

	/* copy the new kvs data */
	if (kvs_updated) {
		args->kvs_xmit_ptr = _kvs_comm_dup();
		args->kvs_xmit_cnt = kvs_comm_cnt;
		kvs_updated = 0;
	} else {	/* No new data to transmit */
		args->kvs_xmit_ptr = xmalloc(0);
		args->kvs_xmit_cnt = 0;
	}

	/* Spawn a pthread to transmit it */
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&agent_id, &attr, _agent, (void *) args))
		fatal("pthread_create");
	slurm_attr_destroy(&attr);
}

static void *_msg_thread(void *x)
{
	struct msg_arg *msg_arg_ptr = (struct msg_arg *) x;
	int rc, success = 0, timeout;
	slurm_msg_t msg_send;

	slurm_msg_t_init(&msg_send);

	debug2("KVS_Barrier msg to %s:%u",
		msg_arg_ptr->bar_ptr->hostname,
		msg_arg_ptr->bar_ptr->port);
	msg_send.msg_type = PMI_KVS_GET_RESP;
	msg_send.data = (void *) msg_arg_ptr->kvs_ptr;
	slurm_set_addr(&msg_send.address,
		msg_arg_ptr->bar_ptr->port,
		msg_arg_ptr->bar_ptr->hostname);

	timeout = slurm_get_msg_timeout() * 10000;
	if (slurm_send_recv_rc_msg_only_one(&msg_send, &rc, timeout) < 0) {
		error("slurm_send_recv_rc_msg_only_one: %m");
	} else if (rc != SLURM_SUCCESS) {
		error("KVS_Barrier confirm from %s, rc=%d",
			msg_arg_ptr->bar_ptr->hostname, rc);
	} else {
		/* successfully transmitted KVS keypairs */
		success = 1;
	}

	slurm_mutex_lock(&agent_mutex);
	agent_cnt--;
	pthread_cond_signal(&agent_cond);
	slurm_mutex_unlock(&agent_mutex);
	xfree(x);
	return NULL;
}

static void *_agent(void *x)
{
	struct agent_arg *args = (struct agent_arg *) x;
	struct kvs_comm_set *kvs_set;
	struct msg_arg *msg_args;
	struct kvs_hosts *kvs_host_list;
	int i, j, kvs_set_cnt = 0, host_cnt, pmi_fanout = 32;
	int msg_sent = 0, max_forward = 0;
	char *tmp, *fanout_off_host;
	pthread_t msg_id;
	pthread_attr_t attr;
	DEF_TIMERS;

	tmp = getenv("PMI_FANOUT");
	if (tmp) {
		pmi_fanout = atoi(tmp);
		if (pmi_fanout < 1)
			pmi_fanout = 32;
	}
	fanout_off_host = getenv("PMI_FANOUT_OFF_HOST");

	/* only send one message to each host,
	 * build table of the ports on each host */
	START_TIMER;
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	kvs_set = xmalloc(sizeof(struct kvs_comm_set) * args->barrier_xmit_cnt);
	for (i=0; i<args->barrier_xmit_cnt; i++) {
		if (args->barrier_xmit_ptr[i].port == 0)
			continue;	/* already sent message to host */
		kvs_host_list = xmalloc(sizeof(struct kvs_hosts) * pmi_fanout);
		host_cnt = 0;

		/* This code enables key-pair forwarding between
		 * tasks. First task on the node gets the key-pairs
		 * with host/port information for all other tasks on
		 * that node it should forward the information to. */
		for (j=(i+1); j<args->barrier_xmit_cnt; j++) {
			if (args->barrier_xmit_ptr[j].port == 0)
				continue;	/* already sent message */
			if ((fanout_off_host == NULL) &&
			    strcmp(args->barrier_xmit_ptr[i].hostname,
				   args->barrier_xmit_ptr[j].hostname))
				continue;	/* another host */
			kvs_host_list[host_cnt].task_id = 0; /* not avail */
			kvs_host_list[host_cnt].port =
					args->barrier_xmit_ptr[j].port;
			kvs_host_list[host_cnt].hostname =
					args->barrier_xmit_ptr[j].hostname;
			args->barrier_xmit_ptr[j].port = 0;/* don't reissue */
			host_cnt++;
			if (host_cnt >= pmi_fanout)
				break;
		}

		msg_sent++;
		max_forward = MAX(host_cnt, max_forward);

		slurm_mutex_lock(&agent_mutex);
		while (agent_cnt >= agent_max_cnt)
			pthread_cond_wait(&agent_cond, &agent_mutex);
		agent_cnt++;
		slurm_mutex_unlock(&agent_mutex);

		msg_args = xmalloc(sizeof(struct msg_arg));
		msg_args->bar_ptr = &args->barrier_xmit_ptr[i];
		msg_args->kvs_ptr = &kvs_set[kvs_set_cnt];
		kvs_set[kvs_set_cnt].host_cnt      = host_cnt;
		kvs_set[kvs_set_cnt].kvs_host_ptr  = kvs_host_list;
		kvs_set[kvs_set_cnt].kvs_comm_recs = args->kvs_xmit_cnt;
		kvs_set[kvs_set_cnt].kvs_comm_ptr  = args->kvs_xmit_ptr;
		kvs_set_cnt++;
		if (agent_max_cnt == 1) {
			/* TotalView slows down a great deal for
			 * pthread_create() calls, so just send the
			 * messages inline when TotalView is in use
			 * or for some other reason we only want
			 * one pthread. */
			_msg_thread((void *) msg_args);
		} else if (pthread_create(&msg_id, &attr, _msg_thread,
				(void *) msg_args)) {
			fatal("pthread_create: %m");
		}
	}

	verbose("Sent KVS info to %d nodes, up to %d tasks per node",
		msg_sent, (max_forward+1));

	/* wait for completion of all outgoing message */
	slurm_mutex_lock(&agent_mutex);
	while (agent_cnt > 0)
		pthread_cond_wait(&agent_cond, &agent_mutex);
	slurm_mutex_unlock(&agent_mutex);
	slurm_attr_destroy(&attr);

	/* Release allocated memory */
	for (i=0; i<kvs_set_cnt; i++)
		xfree(kvs_set[i].kvs_host_ptr);
	xfree(kvs_set);
	for (i=0; i<args->barrier_xmit_cnt; i++)
		xfree(args->barrier_xmit_ptr[i].hostname);
	xfree(args->barrier_xmit_ptr);
	for (i=0; i<args->kvs_xmit_cnt; i++) {
		for (j=0; j<args->kvs_xmit_ptr[i]->kvs_cnt; j++) {
			xfree(args->kvs_xmit_ptr[i]->kvs_keys[j]);
			xfree(args->kvs_xmit_ptr[i]->kvs_values[j]);
		}
		xfree(args->kvs_xmit_ptr[i]->kvs_keys);
		xfree(args->kvs_xmit_ptr[i]->kvs_values);
		xfree(args->kvs_xmit_ptr[i]->kvs_name);
		xfree(args->kvs_xmit_ptr[i]);
	}
	xfree(args->kvs_xmit_ptr);
	xfree(args);

	END_TIMER;
	debug("kvs_xmit time %ld usec", DELTA_TIMER);
	return NULL;
}

/* duplicate the current KVS comm structure */
struct kvs_comm **_kvs_comm_dup(void)
{
	int i, j;
	struct kvs_comm **rc_kvs;

	rc_kvs = xmalloc(sizeof(struct kvs_comm *) * kvs_comm_cnt);
	for (i=0; i<kvs_comm_cnt; i++) {
		rc_kvs[i] = xmalloc(sizeof(struct kvs_comm));
		rc_kvs[i]->kvs_name = xstrdup(kvs_comm_ptr[i]->kvs_name);
		rc_kvs[i]->kvs_cnt = kvs_comm_ptr[i]->kvs_cnt;
		rc_kvs[i]->kvs_keys =
				xmalloc(sizeof(char *) * rc_kvs[i]->kvs_cnt);
		rc_kvs[i]->kvs_values =
				xmalloc(sizeof(char *) * rc_kvs[i]->kvs_cnt);
		for (j=0; j<rc_kvs[i]->kvs_cnt; j++) {
			rc_kvs[i]->kvs_keys[j] =
					xstrdup(kvs_comm_ptr[i]->kvs_keys[j]);
			rc_kvs[i]->kvs_values[j] =
					xstrdup(kvs_comm_ptr[i]->kvs_values[j]);
		}
	}
	return rc_kvs;
}

/* return pointer to named kvs element or NULL if not found */
static struct kvs_comm *_find_kvs_by_name(char *name)
{
	int i;

	for (i=0; i<kvs_comm_cnt; i++) {
		if (strcmp(kvs_comm_ptr[i]->kvs_name, name))
			continue;
		return kvs_comm_ptr[i];
	}
	return NULL;
}

static void _merge_named_kvs(struct kvs_comm *kvs_orig,
		struct kvs_comm *kvs_new)
{
	int i, j;

	for (i=0; i<kvs_new->kvs_cnt; i++) {
		for (j=0; j<kvs_orig->kvs_cnt; j++) {
			if (strcmp(kvs_new->kvs_keys[i], kvs_orig->kvs_keys[j]))
				continue;
			xfree(kvs_orig->kvs_values[j]);
			kvs_orig->kvs_values[j] = kvs_new->kvs_values[i];
			kvs_new->kvs_values[i] = NULL;
			break;
		}
		if (j < kvs_orig->kvs_cnt)
			continue;	/* already recorded, update */
		/* append it */
		kvs_orig->kvs_cnt++;
		xrealloc(kvs_orig->kvs_keys,
				(sizeof(char *) * kvs_orig->kvs_cnt));
		xrealloc(kvs_orig->kvs_values,
				(sizeof(char *) * kvs_orig->kvs_cnt));
		kvs_orig->kvs_keys[kvs_orig->kvs_cnt-1] = kvs_new->kvs_keys[i];
		kvs_orig->kvs_values[kvs_orig->kvs_cnt-1] =
				kvs_new->kvs_values[i];
		kvs_new->kvs_keys[i] = NULL;
		kvs_new->kvs_values[i] = NULL;
	}
}

static void _move_kvs(struct kvs_comm *kvs_new)
{
	kvs_comm_ptr = xrealloc(kvs_comm_ptr, (sizeof(struct kvs_comm *) *
			(kvs_comm_cnt + 1)));
	kvs_comm_ptr[kvs_comm_cnt] = kvs_new;
	kvs_comm_cnt++;
}

static void _print_kvs(void)
{
#if _DEBUG
	int i, j;

	info("KVS dump start");
	for (i=0; i<kvs_comm_cnt; i++) {
		for (j=0; j<kvs_comm_ptr[i]->kvs_cnt; j++) {
			info("KVS: %s:%s:%s", kvs_comm_ptr[i]->kvs_name,
				kvs_comm_ptr[i]->kvs_keys[j],
				kvs_comm_ptr[i]->kvs_values[j]);
		}
	}
#endif
}

extern int pmi_kvs_put(struct kvs_comm_set *kvs_set_ptr)
{
	int i, usec_timer;
	struct kvs_comm *kvs_ptr;
	DEF_TIMERS;

	/* Merge new data with old.
	 * NOTE: We just move pointers rather than copy data where
	 * possible for improved performance */
	START_TIMER;
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_set_ptr->kvs_comm_recs; i++) {
		kvs_ptr = _find_kvs_by_name(kvs_set_ptr->
			kvs_comm_ptr[i]->kvs_name);
		if (kvs_ptr) {
			_merge_named_kvs(kvs_ptr,
				kvs_set_ptr->kvs_comm_ptr[i]);
		} else {
			_move_kvs(kvs_set_ptr->kvs_comm_ptr[i]);
			kvs_set_ptr-> kvs_comm_ptr[i] = NULL;
		}
	}
	slurm_free_kvs_comm_set(kvs_set_ptr);
	_print_kvs();
	kvs_updated = 1;
	pthread_mutex_unlock(&kvs_mutex);
	END_TIMER;
	usec_timer = DELTA_TIMER;
	min_time_kvs_put = MIN(min_time_kvs_put, usec_timer);
	max_time_kvs_put = MAX(max_time_kvs_put, usec_timer);
	tot_time_kvs_put += usec_timer;

	return SLURM_SUCCESS;
}

extern int pmi_kvs_get(kvs_get_msg_t *kvs_get_ptr)
{
	int rc = SLURM_SUCCESS;
#if _DEBUG_TIMING
	static uint32_t tm[10000];
	int cur_time, i;
	struct timeval tv;
#endif

#if _DEBUG
	info("pmi_kvs_get: rank:%u size:%u port:%u, host:%s",
		kvs_get_ptr->task_id, kvs_get_ptr->size,
		kvs_get_ptr->port, kvs_get_ptr->hostname);
#endif
	if (kvs_get_ptr->size == 0) {
		error("PMK_KVS_Barrier reached with size == 0");
		return SLURM_ERROR;
	}
#if _DEBUG_TIMING
	gettimeofday(&tv, NULL);
	cur_time = (tv.tv_sec % 1000) + tv.tv_usec;
	if (kvs_get_ptr->task_id < 10000)
		tm[kvs_get_ptr->task_id] = cur_time;
#endif
	pthread_mutex_lock(&kvs_mutex);
	if (barrier_cnt == 0) {
		barrier_cnt = kvs_get_ptr->size;
		barrier_ptr = xmalloc(sizeof(struct barrier_resp)*barrier_cnt);
	} else if (barrier_cnt != kvs_get_ptr->size) {
		error("PMK_KVS_Barrier task count inconsistent (%u != %u)",
			barrier_cnt, kvs_get_ptr->size);
		rc = SLURM_ERROR;
		goto fini;
	}
	if (kvs_get_ptr->task_id >= barrier_cnt) {
		error("PMK_KVS_Barrier task count(%u) >= size(%u)",
			kvs_get_ptr->task_id, barrier_cnt);
		rc = SLURM_ERROR;
		goto fini;
	}
	if (barrier_ptr[kvs_get_ptr->task_id].port == 0)
		barrier_resp_cnt++;
	else
		error("PMK_KVS_Barrier duplicate request from task %u",
			kvs_get_ptr->task_id);
	barrier_ptr[kvs_get_ptr->task_id].port = kvs_get_ptr->port;
	barrier_ptr[kvs_get_ptr->task_id].hostname = kvs_get_ptr->hostname;
	kvs_get_ptr->hostname = NULL; /* just moved the pointer */
	if (barrier_resp_cnt == barrier_cnt) {
#if _DEBUG_TIMING
		info("task[%d] at %u", 0, tm[0]);
		for (i=1; ((i<barrier_cnt) && (i<10000)); i++) {
			cur_time = (int) tm[i] - (int) tm[i-1];
			info("task[%d] at %u diff %d", i, tm[i], cur_time);
		}
#endif
		_kvs_xmit_tasks();
}
fini:	pthread_mutex_unlock(&kvs_mutex);

	return rc;
}

/*
 * Set the maximum number of threads to be used by the PMI server code.
 * The PMI server code is used interally by the slurm_step_launch() function
 * to support MPI libraries that bootstrap themselves using PMI.
 */
extern void pmi_server_max_threads(int max_threads)
{
	if (max_threads <= 0)
		error("pmi server max threads must be greater than zero");
	else
		agent_max_cnt = max_threads;
}
