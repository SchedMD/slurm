/*****************************************************************************\
 *  fed_mgr.c - functions for federations
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "src/common/list.h"
#include "src/common/macros.h"
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

slurmdb_federation_rec_t     *fed_mgr_fed_rec      = NULL;
static slurmdb_cluster_rec_t *fed_mgr_cluster_rec  = NULL;

static pthread_t ping_thread  = 0;
static bool      stop_pinging = false, inited = false;
static pthread_mutex_t open_send_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t update_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static int _open_controller_conn(slurmdb_cluster_rec_t *cluster)
{
	int rc;
	slurm_persist_conn_t *persist_conn = NULL;

	if (cluster == fed_mgr_cluster_rec) {
		info("%s: hey! how did we get here with ourselves?", __func__);
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&cluster->lock);

	if (!cluster->control_host || !cluster->control_host[0] ||
	    !cluster->control_port) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
			info("%s: Sibling cluster %s doesn't appear up yet, skipping",
			     __func__, cluster->name);
		slurm_mutex_unlock(&cluster->lock);
		return SLURM_SUCCESS;
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
		//persist_conn->timeout = 0; /* we want this to be 0 */
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
	slurm_mutex_unlock(&cluster->lock);

	return rc;
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
			_open_controller_conn(cluster);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&open_send_mutex);
}

static int _send_recv_msg(slurmdb_cluster_rec_t *cluster, slurm_msg_t *req,
			  slurm_msg_t *resp)
{
	int rc;
	slurm_mutex_lock(&cluster->lock);

	resp->conn = req->conn = cluster->fed.send;

	rc = slurm_send_recv_msg(req->conn->fd, req, resp, 0);
	slurm_mutex_unlock(&cluster->lock);

	return rc;
}

static int _ping_controller(slurmdb_cluster_rec_t *cluster)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_PING;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FEDR)
		info("pinging %s(%s:%d)", cluster->name, cluster->control_host,
		     cluster->control_port);

	if ((rc = _send_recv_msg(cluster, &req_msg, &resp_msg))) {
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
			slurm_persist_conn_t *send;
			if (cluster == fed_mgr_cluster_rec)
				continue;
			send = cluster->fed.send;
			if (!send || send->fd == -1)
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

			/* transfer over the connections we already have */
			db_cluster->fed.send = tmp_cluster->fed.send;
			tmp_cluster->fed.send = NULL;
			db_cluster->fed.recv = tmp_cluster->fed.recv;
			tmp_cluster->fed.recv = NULL;
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

static int _find_cluster_recv_in_list(void *x, void *key)
{
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)x;

	if (object->fed.recv == key)
		return 1;

	return 0;
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
				       _find_cluster_recv_in_list,
				       persist_conn))) {
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
			     slurmdb_cluster_rec_t *cluster)
{
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	_fed_mgr_ptr_init(fed, cluster);

	/* We must open the connections after we get out of the
	 * write_lock or we will end up in deadlock.
	 */
	lock_slurmctld(fed_read_lock);
	_open_persist_sends();
	unlock_slurmctld(fed_read_lock);
	_create_ping_thread();
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

		fed_list = acct_storage_g_get_federations(db_conn, getuid(),
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
			_join_federation(fed, cluster);
		} else {
			error("failed to get cluster from federation that we request");
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

	slurm_persist_conn_recv_server_fini();

	_leave_federation();

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
			_join_federation(fed, cluster);
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
	else if (!ret_fed) {
		error("No feds retrieved");
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

extern int _find_sibling_by_ip(void *x, void *key)
{
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)x;
	char *ip = (char *)key;

	if (!xstrcmp(object->control_host, ip))
		return 1;

	return 0;
}

extern char *fed_mgr_find_sibling_name_by_ip(char *ip)
{
	char *name = NULL;
	slurmdb_cluster_rec_t *sibling = NULL;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	lock_slurmctld(fed_read_lock);
	if (fed_mgr_fed_rec && fed_mgr_fed_rec->cluster_list &&
	    (sibling = list_find_first(fed_mgr_fed_rec->cluster_list,
				       _find_sibling_by_ip, ip)))
		name = xstrdup(sibling->name);
	unlock_slurmctld(fed_read_lock);

	return name;
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
	slurm_persist_conn_t *send = NULL;
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

	cluster->control_port = persist_conn->rem_port;
	xfree(cluster->control_host);
	cluster->control_host = xstrdup(persist_conn->rem_host);
	slurm_persist_conn_destroy(cluster->fed.recv);
	cluster->fed.recv = persist_conn;
	send = cluster->fed.send;

	if (!send || send->fd == -1) {
		slurm_mutex_lock(&open_send_mutex);
		rc = _open_controller_conn(cluster);
		slurm_mutex_unlock(&open_send_mutex);
	}

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
