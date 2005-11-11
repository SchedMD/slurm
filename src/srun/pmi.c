/*****************************************************************************\
 *  pmi.c - Global PMI data as maintained within srun
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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

#include <pthread.h>
#include <slurm/slurm_errno.h>

#include "src/api/slurm_pmi.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#define _DEBUG 1

/* Global variables */
pthread_mutex_t kvs_mutex = PTHREAD_MUTEX_INITIALIZER;
int kvs_comm_cnt = 0;
struct kvs_comm **kvs_comm_ptr = NULL;

struct barrier_resp {
	uint16_t port;
	char *hostname;
};
struct barrier_resp *barrier_ptr = NULL;
uint16_t barrier_resp_cnt = 0;
uint16_t barrier_cnt = 0;

struct agent_arg {
	struct barrier_resp *barrier_xmit_ptr;
	int barrier_xmit_cnt;
	struct kvs_comm **kvs_xmit_ptr;
	int kvs_xmit_cnt;
};

static void *_agent(void *x);
static struct kvs_comm *_find_kvs_by_name(char *name);
struct kvs_comm **_kvs_comm_dup(void);
static void _kvs_xmit_tasks(void);
static void _merge_named_kvs(struct kvs_comm *kvs_orig,
		struct kvs_comm *kvs_new);
static void _move_kvs(struct kvs_comm *kvs_new);
static void _print_kvs(void);

/* Transmit the KVS keypairs to all tasks, waiting at a barrier
 * This will take some time, so we work with a copy of the KVS keypairs.
 * We also work with a private copy of the barrier data and clear the 
 * global data pointers so any new barrier requests get treated as
 * completely independent of this one. */
static void _kvs_xmit_tasks(void)
{
	struct agent_arg args;
	pthread_attr_t attr;
	pthread_t agent_id;

#if _DEBUG
	info("All tasks at barrier, transmit KVS keypairs now");
#endif
	/* copy the data */
	args.barrier_xmit_ptr = barrier_ptr;
	args.barrier_xmit_cnt = barrier_cnt;
	barrier_ptr = NULL;
	barrier_resp_cnt = 0;
	barrier_cnt = 0;
	args.kvs_xmit_ptr = _kvs_comm_dup();
	args.kvs_xmit_cnt = kvs_comm_cnt;

	/* Spawn a pthread to transmit it */
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#if 0
/* FIXME: signaling problem if pthread */
	if (pthread_create(&agent_id, &attr, _agent, (void *) &args))
		fatal("pthread_create");
#else
_agent((void *) &args);
#endif
}

static void *_agent(void *x)
{
	struct agent_arg *args = (struct agent_arg *) x;
	struct kvs_comm_set kvs_set;
	int i, j, rc, task_fd;
	slurm_msg_t msg_send, msg_rcv;

	/* send the message */
	kvs_set.kvs_comm_recs = args->kvs_xmit_cnt;
	kvs_set.kvs_comm_ptr  = args->kvs_xmit_ptr;
	msg_send.msg_type = PMI_KVS_GET_RESP;
	msg_send.data = (void *) &kvs_set;
	for (i=0; i<args->barrier_xmit_cnt; i++) {
		debug2("KVS_Barrier msg to %s:%u", 
			args->barrier_xmit_ptr[i].hostname, 
			args->barrier_xmit_ptr[i].port);
		slurm_set_addr(&msg_send.address, 
			args->barrier_xmit_ptr[i].port,
			args->barrier_xmit_ptr[i].hostname);
		if ((task_fd = slurm_open_msg_conn(&msg_send.address)) < 0) {
			error("slurm_init_msg_engine_port:  %m");
			break;
		}
		rc = slurm_send_node_msg(task_fd, &msg_send);
		if (rc < 0) {
			error("KVS_Barrier send data fail to %s",
				args->barrier_xmit_ptr[i].hostname);
			(void) slurm_shutdown_msg_conn(task_fd);
			continue;
		}
#if 0
info("get reply @ %ld", (long)time(NULL));
		rc = slurm_receive_msg(task_fd, &msg_rcv, 0);
info("got reply, rc=%d @ %ld", rc, (long)time(NULL));
#endif
		(void) slurm_shutdown_msg_conn(task_fd);
		if (rc < 0) {
			error("KVS_Barrier confirm fail from %s",
				args->barrier_xmit_ptr[i].hostname);
		}
	}

	/* Release allocated memory */
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
	int i;
	struct kvs_comm *kvs_ptr;

	/* Merge new data with old.
	 * NOTE: We just move pointers rather than copy data where 
	 * possible for improved performance */
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_set_ptr->kvs_comm_recs; i++) {
		kvs_ptr = _find_kvs_by_name(kvs_set_ptr->
			kvs_comm_ptr[i]->kvs_name);
		if (kvs_ptr) {
			_merge_named_kvs(kvs_ptr, 
				kvs_set_ptr->kvs_comm_ptr[i]);
		} else {
			_move_kvs(kvs_set_ptr-> kvs_comm_ptr[i]);
			kvs_set_ptr-> kvs_comm_ptr[i] = NULL;
		}
	}
	slurm_free_kvs_comm_set(kvs_set_ptr);
	_print_kvs();
	pthread_mutex_unlock(&kvs_mutex);
	return SLURM_SUCCESS;
}

extern int pmi_kvs_get(kvs_get_msg_t *kvs_get_ptr)
{
	int rc = SLURM_SUCCESS;

#if _DEBUG
	info("pmi_kvs_get: rank:%u size:%u port:%u, host:%s", 
		kvs_get_ptr->task_id, kvs_get_ptr->size, 
		kvs_get_ptr->port, kvs_get_ptr->hostname);
#endif
	if (kvs_get_ptr->size == 0) {
		error("PMK_KVS_Barrier reached with size == 0");
		return SLURM_ERROR;
	}

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
	if (barrier_resp_cnt == barrier_cnt)
		_kvs_xmit_tasks();
fini:	pthread_mutex_unlock(&kvs_mutex); 
	return rc;
}

