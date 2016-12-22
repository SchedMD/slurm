/*****************************************************************************\
 *  fed_mgr.c - functions for federations
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include <pthread.h>
#include <signal.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmdbd/read_config.h"

#define FED_MGR_STATE_FILE       "fed_mgr_state"
#define FED_MGR_CLUSTER_ID_BEGIN 26

#define FED_SIBLING_BIT(x) ((uint64_t)1 << (x - 1))

slurmdb_federation_rec_t     *fed_mgr_fed_rec      = NULL;
static slurmdb_cluster_rec_t *fed_mgr_cluster_rec  = NULL;

static pthread_t ping_thread  = 0;
static bool      stop_pinging = false, inited = false;
static pthread_mutex_t open_send_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t update_mutex = PTHREAD_MUTEX_INITIALIZER;

/* structs to pass to threads */
typedef struct {
	will_run_response_msg_t *resp;
	slurmdb_cluster_rec_t  	*sibling;
	sib_msg_t               *sib_msg;
	uid_t                    uid;
	pthread_t                thread_id;
	int                      thread_rc;
} sib_willrun_t;

typedef struct {
	slurmdb_cluster_rec_t *sibling;
	sib_msg_t             *sib_msg;
	pthread_t              thread_id;
	int                    thread_rc;
} sib_submit_t;

typedef struct {
	job_desc_msg_t        *job_desc;
	slurmdb_cluster_rec_t *sibling;
	pthread_t              thread_id;
	int                    thread_rc;
} sib_update_t;


static int _close_controller_conn(slurmdb_cluster_rec_t *cluster)
{
	int rc = SLURM_SUCCESS;
//	slurm_persist_conn_t *persist_conn = NULL;

	xassert(cluster);
	slurm_mutex_lock(&cluster->lock);
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("closing sibling conn to %s", cluster->name);

	/* The recv free of this is handled directly in the persist_conn code,
	 * don't free it here */
//	slurm_persist_conn_destroy(cluster->fed.recv);
	cluster->fed.recv = NULL;
	slurm_persist_conn_destroy(cluster->fed.send);
	cluster->fed.send = NULL;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("closed sibling conn to %s", cluster->name);
	slurm_mutex_unlock(&cluster->lock);

	return rc;
}

static int _open_controller_conn(slurmdb_cluster_rec_t *cluster, bool locked)
{
	int rc;
	slurm_persist_conn_t *persist_conn = NULL;
	static int timeout = -1;

	if (timeout < 0)
		timeout = slurm_get_msg_timeout() * 1000;

	if (cluster == fed_mgr_cluster_rec) {
		info("%s: hey! how did we get here with ourselves?", __func__);
		return SLURM_ERROR;
	}

	if (!locked)
		slurm_mutex_lock(&cluster->lock);

	if (!cluster->control_host || !cluster->control_host[0] ||
	    !cluster->control_port) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
			info("%s: Sibling cluster %s doesn't appear up yet, skipping",
			     __func__, cluster->name);
		if (!locked)
			slurm_mutex_unlock(&cluster->lock);
		return SLURM_ERROR;
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("opening sibling conn to %s", cluster->name);

	if (!cluster->fed.send) {
		persist_conn = xmalloc(sizeof(slurm_persist_conn_t));

		cluster->fed.send = persist_conn;

		/* Since this connection is coming from us, make it so ;) */
		persist_conn->cluster_name = xstrdup(slurmctld_cluster_name);
		persist_conn->my_port = slurmctld_conf.slurmctld_port;
		persist_conn->rem_host = xstrdup(cluster->control_host);
		persist_conn->rem_port = cluster->control_port;
		persist_conn->shutdown = &slurmctld_config.shutdown_time;
		persist_conn->timeout = timeout; /* don't put this as 0 it
						  * could cause deadlock */
	} else {
		persist_conn = cluster->fed.send;

		/* Perhaps a backup came up, so don't assume it was the same
		 * host or port we had before.
		 */
		xfree(persist_conn->rem_host);
		persist_conn->rem_host = xstrdup(cluster->control_host);
		persist_conn->rem_port = cluster->control_port;
	}

	rc = slurm_persist_conn_open(persist_conn);
	if (rc != SLURM_SUCCESS) {
		error("fed_mgr: Unable to open connection to cluster %s using host %s(%u)",
		      cluster->name,
		      persist_conn->rem_host, persist_conn->rem_port);
	} else if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("opened sibling conn to %s:%d",
		     cluster->name, persist_conn->fd);
	if (!locked)
		slurm_mutex_unlock(&cluster->lock);

	return rc;
}

/* The cluster->lock should be locked before this is called */
static int _check_send(slurmdb_cluster_rec_t *cluster)
{
	slurm_persist_conn_t *send = cluster->fed.send;

	if (!send || send->fd == -1) {
		return _open_controller_conn(cluster, true);
	}

	return SLURM_SUCCESS;
}

/* fed_mgr read lock needs to be set before coming in here,
 * not the write lock */
static void _open_persist_sends()
{
	ListIterator itr;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurm_persist_conn_t *send = NULL;

	if (!fed_mgr_fed_rec || ! fed_mgr_fed_rec->cluster_list)
		return;

	/* This open_send_mutex will make this like a write lock since at the
	 * same time we are sending out these open requests the other slurmctlds
	 * will be replying and needing to get to the structures.  If we just
	 * used the fed_mgr write lock it would cause deadlock.
	 */
	slurm_mutex_lock(&open_send_mutex);
	itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);
	while ((cluster = list_next(itr))) {
		if (cluster == fed_mgr_cluster_rec)
			continue;

		send = cluster->fed.send;
		if (!send || send->fd == -1)
			_open_controller_conn(cluster, false);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&open_send_mutex);
}

static int _send_recv_msg(slurmdb_cluster_rec_t *cluster, slurm_msg_t *req,
			  slurm_msg_t *resp, bool locked)
{
	int rc;

	xassert(cluster);
	xassert(req);
	xassert(resp);

	slurm_msg_t_init(resp);

	if (!locked)
		slurm_mutex_lock(&cluster->lock);

	rc = _check_send(cluster);
	if ((rc == SLURM_SUCCESS) && cluster->fed.send) {
		resp->conn = req->conn = cluster->fed.send;
		rc = slurm_send_recv_msg(req->conn->fd, req, resp, 0);
	}
	if (!locked)
		slurm_mutex_unlock(&cluster->lock);

	return rc;
}

static int _ping_controller(slurmdb_cluster_rec_t *cluster)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_PING;

	slurm_mutex_lock(&cluster->lock);

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("pinging %s(%s:%d)", cluster->name, cluster->control_host,
		     cluster->control_port);

	if ((rc = _send_recv_msg(cluster, &req_msg, &resp_msg, true))) {
		error("failed to ping %s(%s:%d)",
		      cluster->name, cluster->control_host,
		      cluster->control_port);
	} else if ((rc = slurm_get_return_code(resp_msg.msg_type,
					       resp_msg.data)))
		error("ping returned error from %s(%s:%d)",
		      cluster->name, cluster->control_host,
		      cluster->control_port);
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("finished pinging %s(%s:%d)", cluster->name,
		     cluster->control_host, cluster->control_port);
	slurm_mutex_unlock(&cluster->lock);
	slurm_free_msg_members(&req_msg);
	slurm_free_msg_members(&resp_msg);
	return rc;
}

/*
 * close all sibling conns
 * must lock before entering.
 */
static int _close_sibling_conns()
{
	ListIterator itr;
	slurmdb_cluster_rec_t *cluster;

	if (!fed_mgr_fed_rec || !fed_mgr_fed_rec->cluster_list)
		goto fini;

	itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);
	while ((cluster = list_next(itr))) {
		if (cluster == fed_mgr_cluster_rec)
			continue;
		_close_controller_conn(cluster);
	}
	list_iterator_destroy(itr);

fini:
	return SLURM_SUCCESS;
}

static void *_ping_thread(void *arg)
{
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "fed_ping", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "fed_ping");
	}
#endif
	while (!stop_pinging &&
	       !slurmctld_config.shutdown_time) {
		ListIterator itr;
		slurmdb_cluster_rec_t *cluster;

		lock_slurmctld(fed_read_lock);
		if (!fed_mgr_fed_rec || !fed_mgr_fed_rec->cluster_list)
			goto next;

		itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);

		while ((cluster = list_next(itr))) {
			if (cluster == fed_mgr_cluster_rec)
				continue;
			_ping_controller(cluster);
		}
		list_iterator_destroy(itr);
next:
		unlock_slurmctld(fed_read_lock);

		sleep(5);
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("Exiting ping thread");

	return NULL;
}

static void _create_ping_thread()
{
	pthread_attr_t attr;
	slurm_attr_init(&attr);

	stop_pinging = false;
	if (!ping_thread &&
	    (pthread_create(&ping_thread, &attr, _ping_thread, NULL) != 0)) {
		error("pthread_create of message thread: %m");
		slurm_attr_destroy(&attr);
		ping_thread = 0;
		return;
	}
	slurm_attr_destroy(&attr);
}

static void _destroy_ping_thread()
{
	stop_pinging = true;
	if (ping_thread) {
		/* can't wait for ping_thread to finish because it might be
		 * holding the read lock and we are already in the write lock.
		 * pthread_join(ping_thread, NULL);
		 */
		pthread_kill(ping_thread, SIGUSR1);
		ping_thread = 0;
	}
}

/*
 * Must have FED unlocked prior to entering
 */
static void _fed_mgr_ptr_init(slurmdb_federation_rec_t *db_fed,
			      slurmdb_cluster_rec_t *cluster)
{
	ListIterator c_itr;
	slurmdb_cluster_rec_t *tmp_cluster, *db_cluster;
	slurmctld_lock_t fed_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

	xassert(cluster);

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("Joining federation %s", db_fed->name);

	lock_slurmctld(fed_write_lock);
	if (fed_mgr_fed_rec) {
		/* we are already part of a federation, preserve existing
		 * conenctions */
		c_itr = list_iterator_create(db_fed->cluster_list);
		while ((db_cluster = list_next(c_itr))) {
			if (!xstrcmp(db_cluster->name,
				     slurmctld_cluster_name)) {
				fed_mgr_cluster_rec = db_cluster;
				continue;
			}
			if (!(tmp_cluster =
			      list_find_first(fed_mgr_fed_rec->cluster_list,
					      slurmdb_find_cluster_in_list,
					      db_cluster->name))) {
				/* don't worry about destroying the connection
				 * here.  It will happen below when we free
				 * fed_mgr_fed_rec (automagically).
				 */
				continue;
			}
			slurm_mutex_lock(&tmp_cluster->lock);
			/* transfer over the connections we already have */
			db_cluster->fed.send = tmp_cluster->fed.send;
			tmp_cluster->fed.send = NULL;
			db_cluster->fed.recv = tmp_cluster->fed.recv;
			tmp_cluster->fed.recv = NULL;
			slurm_mutex_unlock(&tmp_cluster->lock);
		}
		list_iterator_destroy(c_itr);
		slurmdb_destroy_federation_rec(fed_mgr_fed_rec);
	} else
		fed_mgr_cluster_rec = cluster;

	fed_mgr_fed_rec = db_fed;
	unlock_slurmctld(fed_write_lock);
}

/*
 * Must have FED write lock prior to entering
 */
static void _leave_federation()
{
	if (!fed_mgr_fed_rec)
		return;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("Leaving federation %s", fed_mgr_fed_rec->name);

	_close_sibling_conns();
	_destroy_ping_thread();
	slurmdb_destroy_federation_rec(fed_mgr_fed_rec);
	fed_mgr_fed_rec = NULL;
	fed_mgr_cluster_rec = NULL;
}

static void _persist_callback_fini(void *arg)
{
	slurm_persist_conn_t *persist_conn = arg;
	slurmdb_cluster_rec_t *cluster;
	slurmctld_lock_t fed_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

	/* If we are shutting down just return or you will get deadlock since
	 * all these locks are already locked.
	 */
	if (!persist_conn || *persist_conn->shutdown)
		return;
	lock_slurmctld(fed_write_lock);

	/* shuting down */
	if (!fed_mgr_fed_rec) {
		unlock_slurmctld(fed_write_lock);
		return;
	}

	if (!(cluster = list_find_first(fed_mgr_fed_rec->cluster_list,
					slurmdb_find_cluster_in_list,
					persist_conn->cluster_name))) {
		info("Couldn't find cluster %s?",
		     persist_conn->cluster_name);
		unlock_slurmctld(fed_write_lock);
		return;
	}

	slurm_mutex_lock(&cluster->lock);

	/* This will get handled at the end of the thread, don't free it here */
	cluster->fed.recv = NULL;
//	persist_conn = cluster->fed.recv;
//	slurm_persist_conn_close(persist_conn);

	persist_conn = cluster->fed.send;
	if (persist_conn) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
			info("Closing send to sibling cluster %s",
			     cluster->name);
		slurm_persist_conn_close(persist_conn);
	}

	slurm_mutex_unlock(&cluster->lock);
	unlock_slurmctld(fed_write_lock);
}

static void _join_federation(slurmdb_federation_rec_t *fed,
			     slurmdb_cluster_rec_t *cluster,
			     bool update)
{
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	_fed_mgr_ptr_init(fed, cluster);

	/* We must open the connections after we get out of the
	 * write_lock or we will end up in deadlock.
	 */
	if (!update) {
		lock_slurmctld(fed_read_lock);
		_open_persist_sends();
		unlock_slurmctld(fed_read_lock);
	}
	_create_ping_thread();
}

static int _persist_job_will_run(slurmdb_cluster_rec_t *conn,
				 sib_msg_t *sib_msg,
				 will_run_response_msg_t **will_run_resp)
{
	int rc = SLURM_PROTOCOL_SUCCESS;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);

	req_msg.msg_type = REQUEST_SIB_JOB_WILL_RUN;
	req_msg.data     = sib_msg;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			info("persistent will_run failed/resources not avail: %d", rc);
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	case RESPONSE_JOB_WILL_RUN:
		*will_run_resp = (will_run_response_msg_t *) resp_msg.data;
		resp_msg.data = NULL;
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
		break;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

static int _persist_submit_batch_job(slurmdb_cluster_rec_t *conn,
				     sib_msg_t *sib_msg,
				     submit_response_msg_t **resp)
{
        int rc = SLURM_PROTOCOL_SUCCESS;
        slurm_msg_t req_msg, resp_msg;

	*resp = NULL;

	slurm_msg_t_init(&req_msg);

	req_msg.msg_type = REQUEST_SIB_SUBMIT_BATCH_JOB;
	req_msg.data     = sib_msg;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		*resp = (submit_response_msg_t *) resp_msg.data;
		resp_msg.data = NULL;
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

static int _persist_allocte_resources(slurmdb_cluster_rec_t *conn,
				      sib_msg_t *sib_msg,
				      resource_allocation_response_msg_t **resp)
{
        int rc = SLURM_PROTOCOL_SUCCESS;
        slurm_msg_t req_msg, resp_msg;

	*resp = NULL;

	slurm_msg_t_init(&req_msg);

	req_msg.msg_type = REQUEST_SIB_RESOURCE_ALLOCATION;
	req_msg.data     = sib_msg;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		*resp = (resource_allocation_response_msg_t *) resp_msg.data;
		resp_msg.data = NULL;
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

static int _persist_update_job(slurmdb_cluster_rec_t *conn,
			       job_desc_msg_t *data)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);

	req_msg.msg_type = REQUEST_UPDATE_JOB;
	req_msg.data     = data;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
		break;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

/*
 * Remove a sibling job that won't be scheduled
 *
 * IN conn       - sibling connection
 * IN job_id     - the job's id
 * IN start_time - time the fed job started
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
static int _persist_fed_job_revoke(slurmdb_cluster_rec_t *conn, uint32_t job_id,
				   time_t start_time)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	sib_msg_t   sib_msg;

	slurm_msg_t_init(&req_msg);

	memset(&sib_msg, 0, sizeof(sib_msg));
	sib_msg.job_id     = job_id;
	sib_msg.start_time = start_time;

	req_msg.msg_type = REQUEST_SIB_JOB_REVOKE;
	req_msg.data	 = &sib_msg;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

/*
 * Grab the fed lock on the sibling job.
 *
 * IN conn       - sibling connection
 * IN job_id     - the job's id
 * IN cluster_id - cluster id of the cluster locking
 * IN do_lock    - true == lock, false == unlock
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
static int _persist_fed_job_lock(slurmdb_cluster_rec_t *conn, uint32_t job_id,
				 uint32_t cluster_id, bool do_lock)
{
	int rc;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);

	sib_msg_t sib_msg;
	memset(&sib_msg, 0, sizeof(sib_msg_t));
	sib_msg.job_id     = job_id;
	sib_msg.cluster_id = cluster_id;

	if (do_lock)
		req_msg.msg_type = REQUEST_SIB_JOB_LOCK;
	else
		req_msg.msg_type = REQUEST_SIB_JOB_UNLOCK;

	req_msg.data     = &sib_msg;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
		break;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

/*
 * Tell the origin cluster that the job was started
 *
 * IN conn       - sibling connection
 * IN job_id     - the job's id
 * IN cluster_id - cluster id of the cluster that started the job
 * IN start_time - time the fed job started
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
static int _persist_fed_job_start(slurmdb_cluster_rec_t *conn,
				  uint32_t job_id, uint32_t cluster_id,
				  time_t start_time)
{
	int rc;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);

	sib_msg_t sib_msg;
	memset(&sib_msg, 0, sizeof(sib_msg_t));
	sib_msg.job_id     = job_id;
	sib_msg.cluster_id = cluster_id;
	sib_msg.start_time = start_time;

	req_msg.msg_type = REQUEST_SIB_JOB_START;
	req_msg.data     = &sib_msg;

	if (_send_recv_msg(conn, &req_msg, &resp_msg, false)) {
		rc = SLURM_PROTOCOL_ERROR;
		goto end_it;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if ((rc = slurm_get_return_code(resp_msg.msg_type,
						resp_msg.data))) {
			slurm_seterrno(rc);
			rc = SLURM_PROTOCOL_ERROR;
		}
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		rc = SLURM_PROTOCOL_ERROR;
		break;
	}

end_it:
	slurm_free_msg_members(&resp_msg);

	return rc;
}

static int _find_sibling_by_id(void *x, void *key)
{
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)x;
	int id = (intptr_t)key;

	if (object->fed.id == id)
		return 1;

	return 0;
}

static slurmdb_cluster_rec_t *_get_cluster_by_id(uint32_t id)
{
	return list_find_first(fed_mgr_fed_rec->cluster_list,
			       _find_sibling_by_id, (void *)(intptr_t)id);
}

/*
 * Revoke all sibling jobs except from cluster_id -- which the request came from
 *
 * IN job_ptr    - job to revoke
 * IN cluster_id - cluster id of cluster that initiated call. Don're revoke
 * 	the job on this cluster -- it's the one that started the fed job.
 * IN start_time - time the fed job started
 */
static void _revoke_sibling_jobs(struct job_record *job_ptr,
				 uint32_t cluster_id, time_t start_time)
{
	int id = 1;
	uint64_t tmp_sibs = job_ptr->fed_details->siblings;
	while (tmp_sibs) {
		if ((tmp_sibs & 1) &&
		    (id != fed_mgr_cluster_rec->fed.id) &&
		    (id != cluster_id)) {
			slurmdb_cluster_rec_t *cluster = _get_cluster_by_id(id);
			if (!cluster) {
				error("couldn't find cluster rec by id %d", id);
				goto next_job;
			}

			_persist_fed_job_revoke(cluster, job_ptr->job_id,
						start_time);
		}

next_job:
		tmp_sibs >>= 1;
		id++;
	}
}

extern int fed_mgr_init(void *db_conn)
{
	int rc = SLURM_SUCCESS;
	slurmdb_federation_cond_t fed_cond;
	List fed_list;
	slurmdb_federation_rec_t *fed = NULL;

	slurm_mutex_lock(&init_mutex);

	if (inited) {
		slurm_mutex_unlock(&init_mutex);
		return SLURM_SUCCESS;
	}

	if (!association_based_accounting)
		goto end_it;

	slurm_persist_conn_recv_server_init();

	if (running_cache) {
		debug("Database appears down, reading federations from state file.");
		fed = fed_mgr_state_load(
			slurmctld_conf.state_save_location);
		if (!fed) {
			debug2("No federation state");
			rc = SLURM_SUCCESS;
			goto end_it;
		}
	} else {
		slurmdb_init_federation_cond(&fed_cond, 0);
		fed_cond.cluster_list = list_create(NULL);
		list_append(fed_cond.cluster_list, slurmctld_cluster_name);

		fed_list = acct_storage_g_get_federations(db_conn,
							  slurmctld_conf.slurm_user_id,
							  &fed_cond);
		FREE_NULL_LIST(fed_cond.cluster_list);
		if (!fed_list) {
			error("failed to get a federation list");
			rc = SLURM_ERROR;
			goto end_it;
		}

		if (list_count(fed_list) == 1)
			fed = list_pop(fed_list);
		else if (list_count(fed_list) > 1) {
			error("got more federations than expected");
			rc = SLURM_ERROR;
		}
		FREE_NULL_LIST(fed_list);
	}

	if (fed) {
		slurmdb_cluster_rec_t *cluster = NULL;

		if ((cluster = list_find_first(fed->cluster_list,
					       slurmdb_find_cluster_in_list,
					       slurmctld_cluster_name))) {
			_join_federation(fed, cluster, false);
		} else {
			error("failed to get cluster from federation that we requested");
			rc = SLURM_ERROR;
		}
	}

end_it:
	inited = true;
	slurm_mutex_unlock(&init_mutex);

	return rc;
}

extern int fed_mgr_fini()
{
	slurmctld_lock_t fed_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

	slurm_mutex_lock(&init_mutex);
	inited = false;
	slurm_mutex_unlock(&init_mutex);

	lock_slurmctld(fed_write_lock);

	/* Call _leave_federation() before slurm_persist_conn_recv_server_fini()
	 * as this will NULL out the cluster's recv persistent connection before
	 * _server_fini() actually destroy's it. That way the cluster's recv
	 * connection won't be pointing to bad memory. */
	_leave_federation();

	slurm_persist_conn_recv_server_fini();

	unlock_slurmctld(fed_write_lock);

	return SLURM_SUCCESS;
}

extern int fed_mgr_update_feds(slurmdb_update_object_t *update)
{
	List feds;
	slurmdb_federation_rec_t *fed = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmctld_lock_t fed_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

	if (!update->objects)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&init_mutex);
	if (!inited) {
		slurm_mutex_unlock(&init_mutex);
		return SLURM_SUCCESS; /* we haven't started the fed mgr and we
				       * can't start it from here, don't worry
				       * all will get set up later. */
	}
	slurm_mutex_unlock(&init_mutex);
	/* we only want one update happening at a time. */
	slurm_mutex_lock(&update_mutex);
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("Got a federation update");

	feds = update->objects;

	/* find the federation that this cluster is in.
	 * if it's changed from last time then update stored information.
	 * grab other clusters in federation
	 * establish connections with each cluster in federation */

	/* what if a remote cluster is removed from federation.
	 * have to detect that and close the connection to the remote */
	while ((fed = list_pop(feds))) {
		if (fed->cluster_list &&
		    (cluster = list_find_first(fed->cluster_list,
					       slurmdb_find_cluster_in_list,
					       slurmctld_cluster_name))) {
			_join_federation(fed, cluster, true);
			break;
		}

		slurmdb_destroy_federation_rec(fed);
	}

	if (!fed) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
			info("Not part of any federation");
		lock_slurmctld(fed_write_lock);
		_leave_federation();
		unlock_slurmctld(fed_write_lock);
	}
	slurm_mutex_unlock(&update_mutex);
	return SLURM_SUCCESS;
}

extern int fed_mgr_state_save(char *state_save_location)
{
	int error_code = 0, log_fd;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	Buf buffer = init_buf(0);

	DEF_TIMERS;

	START_TIMER;

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	lock_slurmctld(fed_read_lock);
	slurmdb_pack_federation_rec(fed_mgr_fed_rec, SLURM_PROTOCOL_VERSION,
				    buffer);
	unlock_slurmctld(fed_read_lock);

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/%s", state_save_location,
				  FED_MGR_STATE_FILE);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m", new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);

	END_TIMER2("fed_mgr_state_save");

	return error_code;
}

extern slurmdb_federation_rec_t *fed_mgr_state_load(char *state_save_location)
{
	Buf buffer = NULL;
	char *data = NULL, *state_file;
	time_t buf_time;
	uint16_t ver = 0;
	uint32_t data_size = 0;
	int state_fd;
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	slurmdb_federation_rec_t *ret_fed = NULL;

	state_file = xstrdup_printf("%s/%s", state_save_location,
				    FED_MGR_STATE_FILE);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		error("No fed_mgr state file (%s) to recover", state_file);
		xfree(state_file);
		return NULL;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);

	debug3("Version in fed_mgr_state header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		error("***********************************************");
		error("Can not recover fed_mgr state, incompatible version, "
		      "got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return NULL;
	}

	safe_unpack_time(&buf_time, buffer);

	error_code = slurmdb_unpack_federation_rec((void **)&ret_fed, ver,
						   buffer);
	if (error_code != SLURM_SUCCESS)
		goto unpack_error;
	else if (!ret_fed || !ret_fed->name ||
		 !list_count(ret_fed->cluster_list)) {
		slurmdb_destroy_federation_rec(ret_fed);
		ret_fed = NULL;
		debug("No feds to retrieve from state");
	} else {
		/* We want to free the connections here since they don't exist
		 * anymore, but they were packed when state was saved. */
		slurmdb_cluster_rec_t *cluster;
		ListIterator itr = list_iterator_create(
			ret_fed->cluster_list);
		while ((cluster = list_next(itr))) {
			slurm_persist_conn_destroy(cluster->fed.recv);
			cluster->fed.recv = NULL;
			slurm_persist_conn_destroy(cluster->fed.send);
			cluster->fed.send = NULL;
		}
		list_iterator_destroy(itr);
	}

	free_buf(buffer);

	return ret_fed;

unpack_error:
	free_buf(buffer);

	return NULL;
}

/*
 * Returns true if the cluster is part of a federation.
 */
extern bool fed_mgr_is_active()
{
	int rc = false;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	lock_slurmctld(fed_read_lock);
	if (fed_mgr_fed_rec)
		rc = true;
	unlock_slurmctld(fed_read_lock);

	return rc;
}

/*
 * Returns federated job id (<local id> + <cluster id>.
 * Bits  0-25: Local job id
 * Bits 26-31: Cluster id
 */
extern uint32_t fed_mgr_get_job_id(uint32_t orig)
{
	if (!fed_mgr_cluster_rec)
		return orig;
	return orig + (fed_mgr_cluster_rec->fed.id << FED_MGR_CLUSTER_ID_BEGIN);
}

/*
 * Returns the local job id from a federated job id.
 */
extern uint32_t fed_mgr_get_local_id(uint32_t id)
{
	return id & MAX_JOB_ID;
}

/*
 * Returns the cluster id from a federated job id.
 */
extern uint32_t fed_mgr_get_cluster_id(uint32_t id)
{
	return id >> FED_MGR_CLUSTER_ID_BEGIN;
}

extern int fed_mgr_add_sibling_conn(slurm_persist_conn_t *persist_conn,
				    char **out_buffer)
{
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	int rc = SLURM_SUCCESS;

	lock_slurmctld(fed_read_lock);

	if (!fed_mgr_fed_rec) {
		unlock_slurmctld(fed_read_lock);
		*out_buffer = xstrdup_printf(
			"no fed_mgr_fed_rec on cluster %s yet.",
			slurmctld_cluster_name);
		/* This really isn't an error.  If the cluster doesn't know it
		 * is in a federation this could happen on the initial
		 * connection from a sibling that found out about the addition
		 * before I did.
		 */
		debug("%s: %s", __func__, *out_buffer);
		/* The other side needs to see this as an error though or the
		 * connection won't be completely established.
		 */
		return SLURM_ERROR;
	}

	if (!fed_mgr_cluster_rec) {
		unlock_slurmctld(fed_read_lock);
		*out_buffer = xstrdup_printf(
			"no fed_mgr_cluster_rec on cluster %s?  "
			"This should never happen",
			slurmctld_cluster_name);
		error("%s: %s", __func__, *out_buffer);
		return SLURM_ERROR;
	}

	if (!(cluster = list_find_first(fed_mgr_fed_rec->cluster_list,
					slurmdb_find_cluster_in_list,
					persist_conn->cluster_name))) {
		unlock_slurmctld(fed_read_lock);
		*out_buffer = xstrdup_printf(
			"%s isn't a known sibling of ours, but tried to connect to cluster %s federation %s",
			persist_conn->cluster_name, slurmctld_cluster_name,
			fed_mgr_fed_rec->name);
		error("%s: %s", __func__, *out_buffer);
		return SLURM_ERROR;
	}

	persist_conn->callback_fini = _persist_callback_fini;
	persist_conn->flags |= PERSIST_FLAG_ALREADY_INITED;

	slurm_mutex_lock(&cluster->lock);
	cluster->control_port = persist_conn->rem_port;
	xfree(cluster->control_host);
	cluster->control_host = xstrdup(persist_conn->rem_host);

	/* If this pointer exists it will be handled by the persist_conn code,
	 * don't free
	 */
	//slurm_persist_conn_destroy(cluster->fed.recv);

	cluster->fed.recv = persist_conn;

	slurm_mutex_unlock(&cluster->lock);

	unlock_slurmctld(fed_read_lock);

	if (rc == SLURM_SUCCESS &&
	    (rc = slurm_persist_conn_recv_thread_init(
		    persist_conn, -1, persist_conn)
	     != SLURM_SUCCESS)) {
		*out_buffer = xstrdup_printf(
			"Couldn't connect back to %s for some reason",
			persist_conn->cluster_name);
		error("%s: %s", __func__, *out_buffer);
	}

	return rc;
}

static void _destroy_sib_willrun(void *object)
{
	sib_willrun_t *resp = (sib_willrun_t *)object;
	if (resp) {
		slurm_free_will_run_response_msg(resp->resp);
		xfree(resp);
	}
}

static void _xfree_f(void* p)
{
	xfree(p);
}

static void *_sib_will_run(void *arg)
{
	int rc = SLURM_SUCCESS;
	sib_willrun_t *sib_willrun = (sib_willrun_t *)arg;

	if (sib_willrun->sibling == fed_mgr_cluster_rec) {
		char *err_msg = NULL;
		struct job_record *job_ptr = NULL;
		job_desc_msg_t *job_desc;
		sib_msg_t *sib_msg = sib_willrun->sib_msg;
		/* don't need read_fed_lock -- set in fed_mgr_job_allocate */
		slurmctld_lock_t job_write_lock = {
			NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

		lock_slurmctld(job_write_lock);
		job_desc = sib_msg->data;

		if (job_desc->job_id == NO_VAL) {
			/* Get a job_id now without incrementing the job_id
			 * count. This prevents burning job_ids on will_runs */
			job_desc->job_id = get_next_job_id(true);
		}

		rc = job_allocate(sib_msg->data, false, true,
				  &sib_willrun->resp, true, sib_willrun->uid,
				  &job_ptr, &err_msg, sib_msg->data_version);
		unlock_slurmctld(job_write_lock);

		xfree(err_msg);

		if (rc)
			debug2("%s: %s", __func__, slurm_strerror(rc));
	} else if ((rc = _persist_job_will_run(sib_willrun->sibling,
					       sib_willrun->sib_msg,
					       &sib_willrun->resp))) {
		error("Failed to get will_run response from sibling %s",
		      sib_willrun->sibling->name);
	}

	sib_willrun->thread_rc = rc;

	return NULL;
}

static int _sort_sib_will_runs(void *x, void *y)
{
	int idle_rc = 0;
	sib_willrun_t *run1 = *(sib_willrun_t **)x;
	sib_willrun_t *run2 = *(sib_willrun_t **)y;

	if (!run1->resp)
		return 1;
	if (!run2->resp)
		return -1;

	if (run1->sibling->fed.weight < run2->sibling->fed.weight)
		return -1;
	if (run1->sibling->fed.weight > run2->sibling->fed.weight)
		return 1;

	/* pack jobs onto clusters with most avail resources. */
	if (run1->resp->sys_usage_per < run2->resp->sys_usage_per)
		idle_rc = 1;
	if (run1->resp->sys_usage_per > run2->resp->sys_usage_per)
		idle_rc = -1;

	/* spread jobs across clusters */
	if (fed_mgr_fed_rec->flags & FEDERATION_FLAG_LLC)
		idle_rc *= -1;

	return idle_rc;
}

/*
 * Convert comma separated list of cluster names to bitmap of cluster ids.
 */
static uint64_t _cluster_names_to_ids(char *clusters)
{
	uint64_t cluster_ids = 0;
	List cluster_names = list_create(slurm_destroy_char);

	xassert(clusters);

	if (!xstrcasecmp(clusters, "all"))
	    return INFINITE64;

	if (slurm_addto_char_list(cluster_names, clusters)) {
		ListIterator itr = list_iterator_create(cluster_names);
		char *cluster_name;
		slurmdb_cluster_rec_t *sibling;

		while ((cluster_name = list_next(itr))) {
			if ((sibling =
			     list_find_first(fed_mgr_fed_rec->cluster_list,
					     slurmdb_find_cluster_in_list,
					     cluster_name))) {
				cluster_ids |= FED_SIBLING_BIT(sibling->fed.id);
			}
		}
		list_iterator_destroy(itr);
	}
	FREE_NULL_LIST(cluster_names);

	return cluster_ids;
}

/*
 * Get will_run responses from all clusters in a federation.
 * IN msg - contains the original job_desc buffer to send to the siblings and to
 * 	be able to create a job_desc copy to willrun itself.
 * IN job_desc - original job_desc. It contains the federated job_id to put on
 * 	the unpacked job_desc. This is not used for the actual will_run because
 * 	job_allocate will modify the job_desc.
 * IN uid - uid of user submitting the job
 * RET returns a list of will_run_response_msg_t*'s.
 */
static List _get_sib_will_runs(slurm_msg_t *msg, job_desc_msg_t *job_desc,
			       uid_t uid)
{
	sib_willrun_t *sib_willrun     = NULL;
	slurmdb_cluster_rec_t *sibling = NULL;
	ListIterator sib_itr, resp_itr;
	List sib_willruns = NULL;
	pthread_attr_t attr;
	sib_msg_t sib_msg;
	uint32_t buf_offset;
	uint64_t cluster_list = INFINITE64; /* all clusters available */
	slurm_msg_t tmp_msg;

	xassert(job_desc);
	xassert(msg);

	slurm_attr_init(&attr);
	sib_willruns = list_create(_destroy_sib_willrun);

	/* Create copy of submitted job_desc since job_allocate() can modify the
	 * original job_desc. */
	buf_offset = get_buf_offset(msg->buffer);
	slurm_msg_t_init(&tmp_msg);
	tmp_msg.flags            = msg->flags;
	tmp_msg.msg_type         = msg->msg_type;
	tmp_msg.protocol_version = msg->protocol_version;

	unpack_msg(&tmp_msg, msg->buffer);
	set_buf_offset(msg->buffer, buf_offset);

	((job_desc_msg_t *)tmp_msg.data)->job_id = job_desc->job_id;
	sib_msg.data         = tmp_msg.data;
	sib_msg.data_buffer  = msg->buffer;
	sib_msg.data_version = msg->protocol_version;
	sib_msg.data_type    = msg->msg_type;

	if (job_desc->clusters)
		cluster_list = _cluster_names_to_ids(job_desc->clusters);

	/* willrun the sibling clusters */
	sib_itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);
	while ((sibling = list_next(sib_itr))) {
		if (!(cluster_list & FED_SIBLING_BIT(sibling->fed.id))) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
				info("skipping cluster %s -- not in cluster list to submit job to",
				     sibling->name);

			continue;
		}

		sib_willrun = xmalloc(sizeof(sib_willrun_t));
		sib_willrun->sibling = sibling;
		sib_willrun->uid     = uid;
		sib_willrun->sib_msg = &sib_msg;

		if (pthread_create(&sib_willrun->thread_id, &attr,
				   _sib_will_run, sib_willrun) != 0) {
			error("failed to create sib_will_run thread for sib %s",
			      sibling->name);
			_destroy_sib_willrun(sib_willrun);
			continue;
		}

		list_append(sib_willruns, sib_willrun);
	}
	list_iterator_destroy(sib_itr);

	slurm_attr_destroy(&attr);

	resp_itr = list_iterator_create(sib_willruns);
	while ((sib_willrun = list_next(resp_itr))) {
		pthread_join(sib_willrun->thread_id, NULL);

		if (sib_willrun->resp &&
		    (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)) {
			char buf[64];
			slurm_make_time_str(&sib_willrun->resp->start_time,
					    buf, sizeof(buf));
			info("will_run_resp for %s: "
			     "start:%s sys_usage:%-6.2f weight:%d",
			     sib_willrun->sibling->name, buf,
			     sib_willrun->resp->sys_usage_per,
			     sib_willrun->sibling->fed.weight);
		}
	}

	list_iterator_destroy(resp_itr);

	/* Free unpacked job_desc data */
	slurm_free_msg_members(&tmp_msg);

	return sib_willruns;
}

/*
 * Find a sibling that can start the job now.
 * IN msg - contains the original job_desc buffer to send to the siblings and to
 * 	be able to create a job_desc copy to willrun itself.
 * IN job_desc - original job_desc. It contains the federated job_id to put on
 * 	the unpacked job_desc. This is not used for the actual will_run because
 * 	job_allocate will modify the job_desc.
 * IN uid - uid of user submitting the job
 * OUT avail_sibs - bitmap of cluster ids that returned a will_run_response.
 * RET returns a ptr to a cluster_rec that can or start the job now or NULL if
 * 	no cluster can start the job now.
 */
static slurmdb_cluster_rec_t *_find_start_now_sib(slurm_msg_t *msg,
						  job_desc_msg_t *job_desc,
						  uid_t uid,
						  uint64_t *avail_sibs)
{
	ListIterator itr;
	List sib_willruns;
	sib_willrun_t *sib_willrun     = NULL;
	sib_willrun_t *start_now_sib   = NULL;
	slurmdb_cluster_rec_t *ret_sib = NULL;
	time_t now = 0;

	xassert(avail_sibs);
	xassert(job_desc);
	xassert(msg);

	if (!(sib_willruns = _get_sib_will_runs(msg, job_desc, uid))) {
		error("Failed to get any will_run responses from any sibs");
		return NULL;
	}

	list_sort(sib_willruns, (ListCmpF)_sort_sib_will_runs);

	now = time(NULL);

	itr = list_iterator_create(sib_willruns);
	while ((sib_willrun = list_next(itr))) {
		if (!sib_willrun->resp) /* no response if job couldn't run? */
			continue;

		*avail_sibs |= FED_SIBLING_BIT(sib_willrun->sibling->fed.id);

		/* Pick first sibling that can start the job now. siblings are
		 * sorted by weight and resources. */
		if (sib_willrun->resp->start_time <= now) {
			start_now_sib = sib_willrun;
			break;
		}
	}
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR) {
		if (start_now_sib)
			info("Earliest cluster:%s time:%ld now:%ld",
			     start_now_sib->sibling->name,
			     start_now_sib->resp->start_time, now);
		else
			info("No siblings can start the job now (%ld))", now);
	}
	list_iterator_destroy(itr);

	if (start_now_sib)
		ret_sib = start_now_sib->sibling;

	FREE_NULL_LIST(sib_willruns);

	return ret_sib;
}

static void *_submit_sibling_allocation(void *arg)
{
	int rc = SLURM_SUCCESS;
	resource_allocation_response_msg_t *alloc_resp = NULL;
	sib_submit_t *sub = (sib_submit_t *)arg;
	slurmdb_cluster_rec_t *sibling = sub->sibling;
	sib_msg_t *sib_msg             = sub->sib_msg;

	if ((rc = _persist_allocte_resources(sibling, sib_msg, &alloc_resp))) {
		error("Failed to submit job to sibling %s: %m", sibling->name);
	} else if (!alloc_resp) {
		error("Got a success back without a resp. This shouldn't happen");
		rc = SLURM_ERROR;
	} else if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR) {
		info("Submitted federated allocation %u to %s",
		     alloc_resp->job_id, sibling->name);
	}
	sub->thread_rc = rc;

	slurm_free_resource_allocation_response_msg(alloc_resp);

	return NULL;
}

static void *_submit_sibling_batch_job(void *arg)
{
	int rc = SLURM_SUCCESS;
	submit_response_msg_t *resp = NULL;
	sib_submit_t *sub = (sib_submit_t *)arg;
	slurmdb_cluster_rec_t *sibling = sub->sibling;
	sib_msg_t *sib_msg             = sub->sib_msg;

	if ((rc = _persist_submit_batch_job(sibling, sib_msg, &resp))) {
		error("Failed to submit job to sibling %s: %m", sibling->name);
	} else if (!resp) {
		error("Got a success back without a resp. This shouldn't happen");
		rc = SLURM_ERROR;
        } else if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR) {
		info("Submitted federated job %u to %s",
		     resp->job_id, sibling->name);
	}
	sub->thread_rc = rc;

	slurm_free_submit_response_response_msg(resp);

	return NULL;
}

static void *_update_sibling_job(void *arg)
{
	sib_update_t *sub = (sib_update_t *)arg;
	sub->thread_rc = _persist_update_job(sub->sibling, sub->job_desc);

	return NULL;
}

static void _update_sib_job_siblings(job_desc_msg_t *job_desc, uint64_t sibs)
{
	/* failed to submit a job to sibling. Need to update all of the
	 * job's fed_siblings bitmaps */
	ListIterator sib_itr, thread_itr;
	slurmdb_cluster_rec_t *sibling;
	List update_threads = list_create(_xfree_f);
	job_desc_msg_t job_update_msg;
	pthread_attr_t attr;
	sib_update_t *tmp_sub = NULL;

	slurm_attr_init(&attr);

	slurm_init_job_desc_msg(&job_update_msg);
	job_update_msg.job_id       = job_desc->job_id;
	job_update_msg.fed_siblings = job_desc->fed_siblings;

	sib_itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);
	while ((sibling = list_next(sib_itr))) {
		pthread_t thread_id = 0;
		sib_update_t *sub;

		/* Local is handled outside */
		if (sibling == fed_mgr_cluster_rec)
			continue;

		if (!(sibs & FED_SIBLING_BIT(sibling->fed.id)))
			continue;

		sub = xmalloc(sizeof(sib_submit_t));
		sub->job_desc = &job_update_msg;
		sub->sibling  = sibling;
		if (pthread_create(&thread_id, &attr,
				   _update_sibling_job, sub) != 0) {
			error("failed to create submit_sibling_job_thread");
			xfree(sub);
			continue;
		}
		sub->thread_id = thread_id;

		list_append(update_threads, sub);
	}
	list_iterator_destroy(sib_itr);
	slurm_attr_destroy(&attr);

	thread_itr = list_iterator_create(update_threads);
	while ((tmp_sub = list_next(thread_itr))) {
		pthread_join(tmp_sub->thread_id, NULL);
		if (tmp_sub->thread_rc) {
			error("failed to update sibling job with updated sibling bitmap on sibling %s",
			      tmp_sub->sibling->name);
			/* other cluster should get updated when it syncs
			 * up */
		}
	}
	list_iterator_destroy(thread_itr);
	FREE_NULL_LIST(update_threads);
}

/*
 * Submit sibling jobs to designated (job_desc->fed_siblings) siblings.
 *
 * Will update job_desc->fed_siblings if a sibling fails to submit a job.
 *
 * IN job_desc - job_desc containing job_id and fed_siblings of job to be.
 * IN msg - contains the original job_desc buffer to send to the siblings.
 * IN alloc_only - true if just an allocation. false if a batch job.
 * RET returns SLURM_SUCCESS if all siblings recieved the job sucessfully or
 * 	SLURM_ERROR if any siblings failed to receive the job. If a sibling
 * 	fails, then the sucessful siblings will be updated with the correct
 * 	sibling bitmap.
 */
static int _submit_sibling_jobs(job_desc_msg_t *job_desc, slurm_msg_t *msg,
				bool alloc_only)
{
	int rc = SLURM_SUCCESS;
	ListIterator sib_itr, thread_itr;
	List submit_threads = NULL;
	sib_submit_t *tmp_sub = NULL;
	sib_msg_t sib_msg;
	slurmdb_cluster_rec_t *sibling = NULL;
	pthread_attr_t attr;

	xassert(job_desc);
	xassert(msg);

	slurm_attr_init(&attr);
	submit_threads = list_create(_xfree_f);

	sib_msg.data_buffer  = msg->buffer;
	sib_msg.data_type    = msg->msg_type;
	sib_msg.data_version = msg->protocol_version;
	sib_msg.fed_siblings = job_desc->fed_siblings;
	sib_msg.job_id       = job_desc->job_id;

	sib_itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);
	while ((sibling = list_next(sib_itr))) {
		pthread_t thread_id = 0;
		sib_submit_t *sub;

		if (sibling == fed_mgr_cluster_rec)
			continue;

		/* fed_siblings is set prior to siblings that responded */
		if (!(job_desc->fed_siblings &
		      FED_SIBLING_BIT(sibling->fed.id)))
			continue;

		sub = xmalloc(sizeof(sib_submit_t));
		sub->sibling = sibling;
		sub->sib_msg = &sib_msg;
		if (pthread_create(&thread_id, &attr,
				   ((alloc_only) ?
				    _submit_sibling_allocation :
				    _submit_sibling_batch_job), sub) != 0) {
			error("failed to create submit_sibling_job_thread");
			xfree(sub);
			continue;
		}
		sub->thread_id = thread_id;

		list_append(submit_threads, sub);
	}
	list_iterator_destroy(sib_itr);

	thread_itr = list_iterator_create(submit_threads);
	while ((tmp_sub = list_next(thread_itr))) {
		pthread_join(tmp_sub->thread_id, NULL);
		rc |= tmp_sub->thread_rc;

		/* take out the job from the siblings bitmap if there was an
		 * error. The local host should stay in it if it's there. */
		if (tmp_sub->thread_rc)
			job_desc->fed_siblings &=
				(~FED_SIBLING_BIT(tmp_sub->sibling->fed.id));
	}
	list_iterator_destroy(thread_itr);

	if (rc && job_desc->fed_siblings) {
		_update_sib_job_siblings(job_desc, INFINITE64);
	}

	slurm_attr_destroy(&attr);
	FREE_NULL_LIST(submit_threads);

	return rc;
}

static uint64_t _get_all_sibling_bits()
{
	ListIterator itr;
	slurmdb_cluster_rec_t *cluster;
	uint64_t sib_bits = 0;

	if (!fed_mgr_fed_rec || !fed_mgr_fed_rec->cluster_list)
		goto fini;

	itr = list_iterator_create(fed_mgr_fed_rec->cluster_list);
	while ((cluster = list_next(itr))) {
		sib_bits |= FED_SIBLING_BIT(cluster->fed.id);
	}
	list_iterator_destroy(itr);

fini:
	return sib_bits;
}

/* Determine how to submit a federated a job.
 *
 * First tries to find a cluster that can start the job now. If a cluster can
 * start the job now, then a sibling job is submitted to that cluster. If no
 * cluster can start the job now, then siblings jobs are submitted to each
 * sibling.
 *
 * Does its own locking (job and fed). Doesn't have a job write lock when
 * communicating with siblings to prevent blocking on sibling communications.
 *
 * IN msg - msg that contains packed job_desc msg to send to siblings.
 * IN job_desc - original job_desc msg.
 * IN alloc_only - true if requesting just an allocation (srun/salloc).
 * IN uid - uid of user requesting allocation.
 * IN protocol_version - version of the code the caller is using
 * OUT job_id_ptr - job_id of allocated job
 * OUT alloc_code - error_code returned from job_allocate
 * OUT err_msg - error message returned if any
 * RET returns SLURM_SUCCESS if the allocation was successful, SLURM_ERROR
 * 	otherwise.
 */
extern int fed_mgr_job_allocate(slurm_msg_t *msg, job_desc_msg_t *job_desc,
				bool alloc_only, uid_t uid,
				uint16_t protocol_version,
				uint32_t *job_id_ptr, int *alloc_code,
				char **err_msg)
{
	int rc = SLURM_SUCCESS;
	slurmdb_cluster_rec_t *start_now_sib = NULL;
	uint64_t avail_sibs = 0;
	struct job_record *job_ptr = NULL;
	time_t now = time(NULL);
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	xassert(msg);
	xassert(job_desc);
	xassert(job_id_ptr);
	xassert(alloc_code);
	xassert(err_msg);

	lock_slurmctld(fed_read_lock);

	lock_slurmctld(job_write_lock);
	/* get job_id now. Can't submit job to get job_id as job_allocate will
	 * change the job_desc. */
	job_desc->job_id = get_next_job_id(false);
	unlock_slurmctld(job_write_lock);

	if ((job_desc->priority != 0) && (job_desc->begin_time <= now)) {
		/* Don't job/node write lock on _find_start_now_sib. It locks
		 * inside _sib_will_run */
		start_now_sib = _find_start_now_sib(msg, job_desc, uid,
						    &avail_sibs);

		if (!avail_sibs) {
			debug("No cluster responded to sibling will_runs, submitting to self");
			avail_sibs =
				FED_SIBLING_BIT(fed_mgr_cluster_rec->fed.id);
		}
	} else {
		avail_sibs = _get_all_sibling_bits();
	}

	if (job_desc->priority == 0) {
		/* don't submit siblings if the job held, siblings will be
		 * submitted when the job is released. */
		job_desc->fed_siblings = 0;
	} else if (start_now_sib == NULL) {
		job_desc->fed_siblings = avail_sibs;
	} else if (start_now_sib == fed_mgr_cluster_rec) {
		job_desc->fed_siblings |=
			FED_SIBLING_BIT(fed_mgr_cluster_rec->fed.id);
	} else {
		job_desc->fed_siblings |=
			FED_SIBLING_BIT(start_now_sib->fed.id);
	}

	/* Submit local job first. Then submit to all siblings. If the local job
	 * fails, then don't worry about sending to the siblings. */
	lock_slurmctld(job_write_lock);
	*alloc_code = job_allocate(job_desc, job_desc->immediate, false, NULL,
				   alloc_only, uid, &job_ptr, err_msg,
				   protocol_version);

	if (!job_ptr || (*alloc_code && job_ptr->job_state == JOB_FAILED)) {
		unlock_slurmctld(job_write_lock);
		rc = SLURM_ERROR;
		/* There may be an rc but the job won't be failed. Will sit in
		 * qeueue */
		info("failed to submit federated job to local cluster");
		goto end_it;
	}

	*job_id_ptr = job_ptr->job_id;

	if (job_desc->priority == 0) {
		job_ptr->fed_details = xmalloc(sizeof(job_fed_details_t));
		info("Submitted held federated job %u to %s(self)",
		     job_ptr->job_id, fed_mgr_cluster_rec->name);
	} else {
		info("Submitted %sfederated job %u to %s(self)",
		     (!(job_ptr->fed_details->siblings &
			FED_SIBLING_BIT(fed_mgr_cluster_rec->fed.id)) ?
		      "tracking " : ""),
		     job_ptr->job_id, fed_mgr_cluster_rec->name);
	}

	unlock_slurmctld(job_write_lock);

	if (_submit_sibling_jobs(job_desc, msg, alloc_only)) {
		/* failed to submit a sibling job to a sibling. Need to update
		 * the local job's sibling bitmap */

		lock_slurmctld(job_write_lock);
		if ((job_ptr->magic  == JOB_MAGIC) &&
		    (job_ptr->job_id == *job_id_ptr)) {

			if (!job_desc->fed_siblings) {
				/* we know that we already have a job_ptr so
				 * just make it a scheduleable job. */
				error("Failed to submit fed job to siblings, submitting to local cluster");
				job_desc->fed_siblings |=
					FED_SIBLING_BIT(
						fed_mgr_cluster_rec->fed.id);
			}
			set_job_fed_details(job_ptr, job_desc->fed_siblings);
		} else {
			error("%s: job got messed up. this should never happen",
			      __func__);
		}

		unlock_slurmctld(job_write_lock);
	}

end_it:
	unlock_slurmctld(fed_read_lock);

	return rc;
}

/* Tests if the job is a tracker only federated job.
 * Tracker only job: a job that shouldn't run on the local cluster but should be
 * kept around to facilitate communications for it's sibling jobs on other
 * clusters.
 */
extern bool fed_mgr_is_tracker_only_job(struct job_record *job_ptr)
{
	bool rc = false;

	xassert(job_ptr);

	if (!fed_mgr_cluster_rec)
		return rc;

	if (job_ptr->fed_details &&
	    (fed_mgr_get_cluster_id(job_ptr->job_id) ==
	     fed_mgr_cluster_rec->fed.id) &&
	    (!(job_ptr->fed_details->siblings &
	      FED_SIBLING_BIT(fed_mgr_cluster_rec->fed.id))))
		rc = true;

	if (job_ptr->fed_details &&
	    job_ptr->fed_details->cluster_lock &&
	    job_ptr->fed_details->cluster_lock != fed_mgr_cluster_rec->fed.id)
		rc = true;

	return rc;
}

/* Return the cluster name for the given cluster id.
 * Must xfree returned string
 */
extern char *fed_mgr_get_cluster_name(uint32_t id)
{
	slurmdb_cluster_rec_t *sibling;
	char *name = NULL;

	if ((sibling = _get_cluster_by_id(id))) {
		name = xstrdup(sibling->name);
	}

	return name;
}

static int _is_fed_job(struct job_record *job_ptr, uint32_t *origin_id)
{
	xassert(job_ptr);
	xassert(origin_id);

	if (!fed_mgr_cluster_rec)
		return false;

	if ((!job_ptr->fed_details) ||
	    (!(*origin_id = fed_mgr_get_cluster_id(job_ptr->job_id)))) {
		info("job %d not a federated job", job_ptr->job_id);
		return false;
	}

	return true;
}

/*
 * Attempt to grab the job's federation cluster lock so that the requesting
 * cluster can attempt to start to the job.
 *
 * IN job - job to lock
 * IN cluster_id - cluster id of cluster wanting to lock the job. If INFINITE,
 * 	the cluster cluster's fed id will be used.
 * RET returns SLURM_SUCCESS if the lock was granted, SLURM_ERROR otherwise
 */
extern int fed_mgr_job_lock(struct job_record *job_ptr, uint32_t cluster_id)
{
	int rc = SLURM_SUCCESS;
	uint32_t origin_id;

	xassert(job_ptr);

	if (!_is_fed_job(job_ptr, &origin_id))
		return SLURM_SUCCESS;

	if (cluster_id == INFINITE)
		cluster_id = fed_mgr_cluster_rec->fed.id;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("attempting fed job lock on %d by cluster_id %d",
		     job_ptr->job_id, cluster_id);

	/* if this cluster is the only sibling, then just assume the lock */
	if ((job_ptr->fed_details->siblings & FED_SIBLING_BIT(cluster_id)) &&
	    (!(job_ptr->fed_details->siblings & ~FED_SIBLING_BIT(cluster_id))))
		return SLURM_SUCCESS;

	if (origin_id != fed_mgr_cluster_rec->fed.id) {
		slurmdb_cluster_rec_t *origin_cluster;
		if (!(origin_cluster = _get_cluster_by_id(origin_id))) {
			error("Unable to find origin cluster for job %d from origin id %d",
			      job_ptr->job_id, origin_id);
			return SLURM_ERROR;
		}
		return _persist_fed_job_lock(origin_cluster, job_ptr->job_id,
					     cluster_id, true);
	}

	if (job_ptr->fed_details->cluster_lock &&
	    (job_ptr->fed_details->cluster_lock != cluster_id)) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
			info("fed job %d already locked by cluster %d",
			     job_ptr->job_id,
			     job_ptr->fed_details->cluster_lock);
		rc = SLURM_ERROR;
	} else {
		job_ptr->fed_details->cluster_lock = cluster_id;
		rc = SLURM_SUCCESS;
	}

	return rc;
}

/*
 * Release the job's federation cluster lock so that other cluster's can try to
 * start the job.
 *
 * IN job        - job to unlock
 * IN cluster_id - cluster id of cluster wanting to unlock the job. If INFINITE,
 * 	the cluster cluster's fed id will be used.
 * RET returns SLURM_SUCCESS if the lock was released, SLURM_ERROR otherwise
 */
extern int fed_mgr_job_unlock(struct job_record *job_ptr, uint32_t cluster_id)
{
	int rc = SLURM_SUCCESS;
	uint32_t origin_id;

	if (!_is_fed_job(job_ptr, &origin_id))
		return SLURM_SUCCESS;

	if (cluster_id == INFINITE)
		cluster_id = fed_mgr_cluster_rec->fed.id;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("releasing fed job lock on %d by cluster_id %d",
		     job_ptr->job_id, cluster_id);

	/* if this cluster is the only sibling, then dont worry */
	if ((job_ptr->fed_details->siblings & FED_SIBLING_BIT(cluster_id)) &&
	    (!(job_ptr->fed_details->siblings & ~FED_SIBLING_BIT(cluster_id))))
		return SLURM_SUCCESS;

	if (origin_id != fed_mgr_cluster_rec->fed.id) {
		slurmdb_cluster_rec_t *origin_cluster;
		if (!(origin_cluster = _get_cluster_by_id(origin_id))) {
			error("Unable to find origin cluster for job %d from origin id %d",
			      job_ptr->job_id, origin_id);
			return SLURM_ERROR;
		}
		return _persist_fed_job_lock(origin_cluster, job_ptr->job_id,
					     cluster_id, false);
	}

	if (job_ptr->fed_details->cluster_lock &&
	    (job_ptr->fed_details->cluster_lock != cluster_id)) {
		error("attempt to unlock sib job %d by cluster %d which doesn't have job lock",
		      job_ptr->job_id, cluster_id);
		rc = SLURM_ERROR;
	} else {
		job_ptr->fed_details->cluster_lock = 0;
	}

	return rc;
}

/*
 * Notify origin cluster that cluster_id started job.
 *
 * Cancels remaining sibling jobs.
 *
 * IN job_ptr    - job_ptr of job to unlock
 * IN cluster_id - cluster id of cluster wanting to unlock the job. If INFINITE,
 * 	the cluster cluster's fed id will be used.
 * IN start_time - start_time of the job.
 * RET returns SLURM_SUCCESS if the lock was released, SLURM_ERROR otherwise
 */
extern int fed_mgr_job_start(struct job_record *job_ptr, uint32_t cluster_id,
			     time_t start_time)
{
	int rc = SLURM_SUCCESS;
	uint32_t origin_id;

	assert(job_ptr);

	if (!_is_fed_job(job_ptr, &origin_id))
		return SLURM_SUCCESS;

	if (cluster_id == INFINITE)
		cluster_id = fed_mgr_cluster_rec->fed.id;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("start fed job %d by cluster_id %d",
		     job_ptr->job_id, cluster_id);

	if (origin_id != fed_mgr_cluster_rec->fed.id) {
		slurmdb_cluster_rec_t *origin_cluster;
		if (!(origin_cluster = _get_cluster_by_id(origin_id))) {
			error("Unable to find origin cluster for job %d from origin id %d",
			      job_ptr->job_id, origin_id);
			return SLURM_ERROR;
		}

		set_job_fed_details(job_ptr, FED_SIBLING_BIT(cluster_id));

		return _persist_fed_job_start(origin_cluster, job_ptr->job_id,
					      cluster_id, job_ptr->start_time);
	}

	/* Origin Cluster: */
	if ((job_ptr->fed_details->siblings & FED_SIBLING_BIT(cluster_id)) &&
	    (!(job_ptr->fed_details->siblings & ~FED_SIBLING_BIT(cluster_id))))
	{
		/* if this cluster is the only sibling, then just assume the
		 * lock */
		job_ptr->fed_details->cluster_lock = cluster_id;
	} else if (!job_ptr->fed_details->cluster_lock) {
		error("attempt to start sib job %d by cluster %d but it's not locked",
		      job_ptr->job_id, cluster_id);
		rc = SLURM_ERROR;
	} else if (job_ptr->fed_details->cluster_lock &&
		   (job_ptr->fed_details->cluster_lock != cluster_id)) {
		error("attempt to start sib job %d by cluster %d which doesn't have job lock",
		     job_ptr->job_id, cluster_id);
		rc = SLURM_ERROR;
	} else if (job_ptr->fed_details->siblings &
		   ~FED_SIBLING_BIT(cluster_id)) {
		/* cancel all sibling jobs if there are more siblings than just
		 * the cluster that it came from */
		_revoke_sibling_jobs(job_ptr, cluster_id, start_time);
	}

	if (!rc) {
		/* Update where sibling jobs are running */
		set_job_fed_details(job_ptr, FED_SIBLING_BIT(cluster_id));

		if (cluster_id != fed_mgr_cluster_rec->fed.id) {
			/* leave as pending so that it will stay around */
			fed_mgr_job_revoke(job_ptr, false, start_time);
		}
	}

	return rc;
}

/*
 * Complete the federated job. If the job ran on a cluster other than the
 * origin_cluster then it notifies the origin cluster that the job finished.
 *
 * Tells the origin cluster to revoke the tracking job.
 *
 * IN job_ptr     - job_ptr of job to complete.
 * IN return_code - return code of job
 * RET returns SLURM_SUCCESS if fed job was completed, SLURM_ERROR otherwise
 */
extern int fed_mgr_job_complete(struct job_record *job_ptr,
				uint32_t return_code, time_t start_time)
{
	uint32_t origin_id;

	if (!_is_fed_job(job_ptr, &origin_id))
		return SLURM_SUCCESS;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("complete fed job %d by cluster_id %d",
		     job_ptr->job_id, fed_mgr_cluster_rec->fed.id);

	if (origin_id == fed_mgr_cluster_rec->fed.id)
		return SLURM_SUCCESS;

	slurmdb_cluster_rec_t *conn = _get_cluster_by_id(origin_id);
	if (!conn) {
		error("Unable to find origin cluster for job %d from origin id %d",
		      job_ptr->job_id, origin_id);
		return SLURM_ERROR;
	}

	return _persist_fed_job_revoke(conn, job_ptr->job_id, start_time);
}

/*
 * Revokes the federated job.
 *
 * IN job_ptr      - job_ptr of job to revoke.
 * IN job_complete - whether the job is done or not. If completed then sets the
 * 	state to JOB_REVOKED | JOB_CANCELLED. JOB_REVOKED otherwise.
 * IN start_time   - start time of the job that actually ran.
 * RET returns SLURM_SUCCESS if fed job was completed, SLURM_ERROR otherwise
 */
extern int fed_mgr_job_revoke(struct job_record *job_ptr, bool job_complete,
			      time_t start_time)
{
	uint32_t origin_id;
	uint32_t state = JOB_REVOKED;

	if (!_is_fed_job(job_ptr, &origin_id))
		return SLURM_SUCCESS;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("revoking fed job %d", job_ptr->job_id);

	if (job_complete)
		state |= JOB_CANCELLED;

	job_ptr->job_state  = state;
	job_ptr->start_time = 0;
	job_ptr->end_time   = start_time;
	job_completion_logger(job_ptr, false);

	/* Don't remove the origin job */
	if (origin_id == fed_mgr_cluster_rec->fed.id)
		return SLURM_SUCCESS;

	list_delete_all(job_list, &list_find_job_id,
			(void *)&job_ptr->job_id);

	return SLURM_SUCCESS;
}

/* Convert cluster ids to cluster names.
 *
 * RET: return string of comma-separated clsuter names.
 *      Must free returned string.
 */
extern char *fed_mgr_cluster_ids_to_names(uint64_t cluster_ids)
{
	int bit = 1;
	char *names = NULL;

	if (!fed_mgr_fed_rec || !fed_mgr_fed_rec->cluster_list)
		return names;

	while (cluster_ids) {
		if (cluster_ids & 1) {
			slurmdb_cluster_rec_t *sibling;
			if ((sibling = _get_cluster_by_id(bit))) {
				xstrfmtcat(names, "%s%s",
					   (names) ? "," : "", sibling->name);
			} else {
				error("Couldn't find a sibling cluster with id %d",
				      bit);
			}
		}

		cluster_ids >>= 1;
		bit++;
	}

	return names;
}

/* Find the earliest time a job can start by doing willruns to all clusters in
 * the federation and returning the fastest time.
 *
 * IN msg - msg that contains packed job_desc msg to send to siblings.
 * IN job_desc - original job_desc msg.
 * IN uid - uid of user requesting will_run.
 * OUT resp - will_run_response to return
 * RET returns a SLURM_SUCCESS if a will_run_response is found, SLURM_ERROR
 * 	otherwise.
 */
extern int fed_mgr_sib_will_run(slurm_msg_t *msg, job_desc_msg_t *job_desc,
				uid_t uid, will_run_response_msg_t **resp)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	List sib_willruns;
	sib_willrun_t *sib_willrun;
	sib_willrun_t *earliest_willrun = NULL;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	xassert(msg);
	xassert(job_desc);
	xassert(resp);

	*resp = NULL;

	lock_slurmctld(fed_read_lock);

	if (!(sib_willruns = _get_sib_will_runs(msg, job_desc, uid))) {
		error("Failed to get any will_run responses from any sibs");
		return SLURM_ERROR;
	}

	itr = list_iterator_create(sib_willruns);
	while ((sib_willrun = list_next(itr))) {
		if (!sib_willrun->resp) /* no response if job couldn't run? */
			continue;

		if ((earliest_willrun == NULL) ||
		    (sib_willrun->resp->start_time <
		     earliest_willrun->resp->start_time))
			earliest_willrun = sib_willrun;
	}
	list_iterator_destroy(itr);

	if (earliest_willrun) {
		*resp = earliest_willrun->resp;
		earliest_willrun->resp = NULL;
	} else {
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(sib_willruns);
	unlock_slurmctld(fed_read_lock);

	return rc;
}
