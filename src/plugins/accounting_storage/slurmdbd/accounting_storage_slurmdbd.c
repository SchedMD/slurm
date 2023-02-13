/*****************************************************************************\
 *  accounting_storage_slurmdbd.c - accounting interface to slurmdbd.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/read_config.h"
#include "src/interfaces/select.h"
#include "src/interfaces/accounting_storage.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_persist_conn.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

#include "as_ext_dbd.h"
#include "slurmdbd_agent.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined(__APPLE__)
extern slurm_conf_t slurm_conf __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
extern uint16_t running_cache __attribute__((weak_import));
extern pthread_mutex_t assoc_cache_mutex __attribute__((weak_import));
extern pthread_cond_t assoc_cache_cond __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern List assoc_mgr_tres_list __attribute__((weak_import));
#else
slurm_conf_t slurm_conf;
List job_list = NULL;
uint16_t running_cache = RUNNING_CACHE_STATE_NOTRUNNING;
pthread_mutex_t assoc_cache_mutex;
pthread_cond_t assoc_cache_cond;
int node_record_count;
List assoc_mgr_tres_list;
#endif

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Accounting storage SLURMDBD plugin";
const char plugin_type[] = "accounting_storage/slurmdbd";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static pthread_t db_inx_handler_thread;
static pthread_mutex_t db_inx_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t db_inx_cond = PTHREAD_COND_INITIALIZER;
static bool running_db_inx = 0;
static int first = 1;
static time_t plugin_shutdown = 0;

static char *cluster_nodes = NULL; /* Protected by node write lock */
static char *cluster_tres = NULL; /* Protected by node write lock */

static hostlist_t cluster_hl = NULL;
static pthread_mutex_t cluster_hl_mutex = PTHREAD_MUTEX_INITIALIZER;

extern int jobacct_storage_p_job_start(void *db_conn, job_record_t *job_ptr);
extern int jobacct_storage_p_job_heavy(void *db_conn, job_record_t *job_ptr);
extern void acct_storage_p_send_all(void *db_conn, time_t event_time,
				    slurm_msg_type_t msg_type);

static void _partial_free_dbd_job_start(void *object)
{
	dbd_job_start_msg_t *req = (dbd_job_start_msg_t *)object;
	if (req) {
		xfree(req->account);
		xfree(req->array_task_str);
		xfree(req->constraints);
		xfree(req->container);
		xfree(req->env_hash);
		xfree(req->licenses);
		xfree(req->mcs_label);
		xfree(req->name);
		xfree(req->nodes);
		xfree(req->partition);
		xfree(req->node_inx);
		xfree(req->wckey);
		xfree(req->gres_used);
		xfree(req->script_hash);
		xfree(req->submit_line);
		xfree(req->tres_alloc_str);
		xfree(req->tres_req_str);
		xfree(req->work_dir);
	}
}

static void _partial_destroy_dbd_job_start(void *object)
{
	dbd_job_start_msg_t *req = (dbd_job_start_msg_t *)object;
	if (req) {
		_partial_free_dbd_job_start(req);
		xfree(req);
	}
}

/* Anything allocated here must be freed in _partial_free_dbd_job_start() */
static int _setup_job_start_msg(dbd_job_start_msg_t *req,
				job_record_t *job_ptr)
{
	if (!job_ptr->details || !job_ptr->details->submit_time) {
		error("jobacct_storage_p_job_start: "
		      "Not inputing this job %u, it has no submit time.",
		      job_ptr->job_id);
		return SLURM_ERROR;
	}
	memset(req, 0, sizeof(dbd_job_start_msg_t));

	req->account       = xstrdup(job_ptr->account);

	req->assoc_id      = job_ptr->assoc_id;
	req->alloc_nodes   = job_ptr->total_nodes;

	if (job_ptr->resize_time) {
		req->eligible_time = job_ptr->resize_time;
		req->submit_time   = job_ptr->details->submit_time;
	} else if (job_ptr->details) {
		req->eligible_time = job_ptr->details->begin_time;
		req->submit_time   = job_ptr->details->submit_time;
	}

	/* If the reason is WAIT_ARRAY_TASK_LIMIT we don't want to
	 * give the pending jobs an eligible time since it will add
	 * time to accounting where as these jobs aren't able to run
	 * until later so mark it as such.
	 */
	if (job_ptr->state_reason == WAIT_ARRAY_TASK_LIMIT)
		req->eligible_time = INFINITE;

	req->start_time    = job_ptr->start_time;
	req->gid           = job_ptr->group_id;
	req->job_id        = job_ptr->job_id;
	req->array_job_id  = job_ptr->array_job_id;
	req->array_task_id = job_ptr->array_task_id;
	if (job_ptr->het_job_id) {
		req->het_job_id     = job_ptr->het_job_id;
		req->het_job_offset = job_ptr->het_job_offset;
	} else {
		//req->het_job_id   = 0;
		req->het_job_offset = NO_VAL;
	}

	build_array_str(job_ptr);
	if (job_ptr->array_recs && job_ptr->array_recs->task_id_str) {
		req->array_task_str = xstrdup(job_ptr->array_recs->task_id_str);
		req->array_max_tasks = job_ptr->array_recs->max_run_tasks;
		req->array_task_pending = job_ptr->array_recs->task_cnt;
	}

	req->db_flags      = job_ptr->db_flags;

	req->db_index      = job_ptr->db_index;
	if (!IS_JOB_PENDING(job_ptr))
		req->constraints   = xstrdup(job_ptr->details->features_use);
	else
		req->constraints   = xstrdup(job_ptr->details->features);

	req->container     = xstrdup(job_ptr->container);
	req->licenses = xstrdup(job_ptr->licenses);
	req->job_state     = job_ptr->job_state;
	req->state_reason_prev = job_ptr->state_reason_prev_db;
	req->name          = xstrdup(job_ptr->name);
	req->nodes         = xstrdup(job_ptr->nodes);
	req->work_dir      = xstrdup(job_ptr->details->work_dir);

	/* create req->node_inx outside of locks when packing */

	if (!IS_JOB_PENDING(job_ptr) && job_ptr->part_ptr)
		req->partition = xstrdup(job_ptr->part_ptr->name);
	else
		req->partition = xstrdup(job_ptr->partition);

	if (job_ptr->details) {
		req->req_cpus = job_ptr->details->min_cpus;
		req->req_mem = job_ptr->details->pn_min_memory;
		req->submit_line = xstrdup(job_ptr->details->submit_line);
		/* Only send this once per instance of the job! */
		if (!job_ptr->db_index || (job_ptr->db_index == NO_VAL64)) {
			req->env_hash = xstrdup(job_ptr->details->env_hash);
			req->script_hash =
				xstrdup(job_ptr->details->script_hash);
		}
	}
	req->resv_id       = job_ptr->resv_id;
	req->priority      = job_ptr->priority;
	req->timelimit     = job_ptr->time_limit;
	req->tres_alloc_str= xstrdup(job_ptr->tres_alloc_str);
	req->tres_req_str  = xstrdup(job_ptr->tres_req_str);
	req->mcs_label	   = xstrdup(job_ptr->mcs_label);
	req->wckey         = xstrdup(job_ptr->wckey);
	req->uid           = job_ptr->user_id;
	req->qos_id        = job_ptr->qos_id;
	req->gres_used     = xstrdup(job_ptr->gres_used);

	return SLURM_SUCCESS;
}

static void _sending_script_env(dbd_id_rc_msg_t *id_ptr, job_record_t *job_ptr)
{
	xassert(id_ptr);
	xassert(job_ptr);
	xassert(job_ptr->details);

	if ((slurm_conf.conf_flags & CTL_CONF_SJS) &&
	    (id_ptr->flags & JOB_SEND_SCRIPT) &&
	    job_ptr->details->script_hash)
		job_ptr->bit_flags |= JOB_SEND_SCRIPT;
	if ((slurm_conf.conf_flags & CTL_CONF_SJE) &&
	    (id_ptr->flags & JOB_SEND_ENV) &&
	    job_ptr->details->env_hash)
		job_ptr->bit_flags |= JOB_SEND_ENV;

	if (jobacct_storage_p_job_heavy(slurmdbd_conn, job_ptr) ==
	    SLURM_SUCCESS) {
		job_ptr->bit_flags &= ~JOB_SEND_SCRIPT;
		job_ptr->bit_flags &= ~JOB_SEND_ENV;
	}
}

static int _set_db_inx_for_each(void *x, void *arg)
{
	dbd_id_rc_msg_t *id_ptr = x;
	job_record_t *job_ptr;

	if ((job_ptr = find_job_record(id_ptr->job_id))) {
		if (job_ptr->db_index) {
			/*
			 * Only set if db_index != 0.
			 * Is set to NO_VAL64 previously
			 * but may have been set to 0
			 * after the fact, which means
			 * the start needs to be sent
			 * again.
			 */
			job_ptr->db_index = id_ptr->db_index;
			job_ptr->job_state &= (~JOB_UPDATE_DB);
		}
		_sending_script_env(id_ptr, job_ptr);
	}

	return 0;
}

static int _reset_db_inx_for_each(void *x, void *arg)
{
	job_record_t *job_ptr = x;

	if (job_ptr->db_index == NO_VAL64)
		job_ptr->db_index = 0;

	return 0;
}

static void *_set_db_inx_thread(void *no_data)
{
	job_record_t *job_ptr = NULL;
	ListIterator itr;
	struct timeval tvnow;
	struct timespec abs;

	/* Read lock on jobs */
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* DEF_TIMERS; */

	/*
	 * We only want to destory the pointer here not the contents so call
	 * special function _partial_destroy_dbd_job_start.
	 */
	List local_job_list = list_create(_partial_destroy_dbd_job_start);

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "dbinx", NULL, NULL, NULL) < 0) {
		error("cannot set my name to dbinx: %m");
	}
#endif
	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (!plugin_shutdown) {
		/* START_TIMER; */
		/* info("starting db_thread"); */
		slurm_mutex_lock(&db_inx_lock);
		/* info("in lock db_thread"); */
		running_db_inx = 1;

		/* Here we have off loaded starting
		 * jobs in the database out of band
		 * from the job submission.  This
		 * is can make submitting jobs much
		 * faster and not lock up the
		 * controller waiting for the db inx
		 * back from the database.
		 * Even though there is potential of modifying the
		 * job db_index here we use a read lock since the
		 * data isn't that sensitive and will only be updated
		 * later in this function. */
		lock_slurmctld(job_read_lock);	/* USE READ LOCK, SEE ABOVE */
		itr = list_iterator_create(job_list);
		while ((job_ptr = list_next(itr))) {
			dbd_job_start_msg_t *req;

			if (!IS_JOB_UPDATE_DB(job_ptr)) {
				if (job_ptr->db_index || job_ptr->resize_time)
					continue;

				/* We set the db_index to NO_VAL64 here
				 * to avoid a potential race condition
				 * where at this moment in time the
				 * job is only eligible to run and
				 * before this call to the DBD returns,
				 * the job starts and needs to send
				 * the start message as well, but
				 * won't if the db_index is 0
				 * resulting in lost information about
				 * the allocation.  Setting
				 * it to NO_VAL will inform the DBD of
				 * this situation and it will handle
				 * it accordingly.
				 */
				job_ptr->db_index = NO_VAL64;
			}

			req = xmalloc(sizeof(dbd_job_start_msg_t));
			if (_setup_job_start_msg(req, job_ptr)
			    != SLURM_SUCCESS) {
				_partial_destroy_dbd_job_start(req);
				if (job_ptr->db_index == NO_VAL64)
					job_ptr->db_index = 0;
				continue;
			}

			list_append(local_job_list, req);
			/* Just so we don't have a crazy
			   amount of messages at once.
			*/
			if (list_count(local_job_list) > 1000)
				break;
		}
		list_iterator_destroy(itr);
		unlock_slurmctld(job_read_lock);

		while (list_count(local_job_list)) {
			persist_msg_t req = {0}, resp = {0};
			dbd_list_msg_t send_msg, *got_msg;
			int rc = SLURM_SUCCESS;
			bool reset = 0;
			memset(&send_msg, 0, sizeof(dbd_list_msg_t));

			send_msg.my_list = local_job_list;

			req.msg_type = DBD_SEND_MULT_JOB_START;
			req.data = &send_msg;

			rc = dbd_conn_send_recv(
				SLURM_PROTOCOL_VERSION, &req, &resp);
			if (rc != SLURM_SUCCESS) {
				error("DBD_SEND_MULT_JOB_START "
				      "failure: %m");
				reset = 1;
			} else if (resp.msg_type == PERSIST_RC) {
				persist_rc_msg_t *msg = resp.data;
				if (msg->rc == SLURM_SUCCESS) {
					info("%s", msg->comment);
				} else
					error("%s", msg->comment);
				slurm_persist_free_rc_msg(msg);
				reset = 1;
			} else if (resp.msg_type != DBD_GOT_MULT_JOB_START) {
				error("response type not "
				      "DBD_GOT_MULT_JOB_START: %u",
				      resp.msg_type);
				reset = 1;
			} else {
				got_msg = (dbd_list_msg_t *) resp.data;

				lock_slurmctld(job_write_lock);
				list_for_each(got_msg->my_list,
					      _set_db_inx_for_each, NULL);
				unlock_slurmctld(job_write_lock);

				/*
				 * Assume the returned number of elements is
				 * equivalent to the number that was sent out,
				 * and thus those have completed processing
				 * and need to be dropped.
				 *
				 * This is due to slurm_pack_list_until()
				 * potentially sending only a part of the List
				 * to avoid creating an overly-large RPC that
				 * cannot be processed the SlurmDBD.
				 */
				list_flush_max(local_job_list,
					       list_count(got_msg->my_list));

				slurmdbd_free_list_msg(got_msg);
			}

			if (reset) {
				list_flush(local_job_list);
				lock_slurmctld(job_read_lock);
				/* USE READ LOCK, SEE ABOVE on first
				 * read lock */
				list_for_each(job_list,
					      _reset_db_inx_for_each, NULL);
				unlock_slurmctld(job_read_lock);
			}
		}
		running_db_inx = 0;

		/* END_TIMER; */
		/* info("set all db_inx's in %s", TIME_STR); */

		/* Since it doesn't take much time at all to do this
		   check do it every 5 seconds.  This helps the DBD so
		   it doesn't have to find db_indexs of jobs that
		   haven't had the start rpc come through.
		*/

		gettimeofday(&tvnow, NULL);
		abs.tv_sec = tvnow.tv_sec + 5;
		abs.tv_nsec = tvnow.tv_usec * 1000;

		slurm_cond_timedwait(&db_inx_cond, &db_inx_lock, &abs);

		slurm_mutex_unlock(&db_inx_lock);
	}

	FREE_NULL_LIST(local_job_list);

	return NULL;
}

static int _send_cluster_tres(void *db_conn,
			      char *cluster_nodes,
			      char *tres_str_in,
			      time_t event_time,
			      uint16_t rpc_version)
{
	persist_msg_t msg = {0};
	dbd_cluster_tres_msg_t req;
	int rc = SLURM_ERROR;

	if (!tres_str_in)
		return rc;

	debug2("Sending tres '%s' for cluster", tres_str_in);
	memset(&req, 0, sizeof(dbd_cluster_tres_msg_t));
	req.cluster_nodes = cluster_nodes;
	req.event_time    = event_time;
	req.tres_str      = tres_str_in;

	msg.msg_type      = DBD_CLUSTER_TRES;
	msg.conn          = db_conn;
	msg.data          = &req;

	dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION, &msg, &rc);

	return rc;
}

static void _update_cluster_nodes(void)
{
	static int prev_node_record_count = -1;
	static bitstr_t *total_node_bitmap = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	xfree(cluster_nodes);
	if (prev_node_record_count != node_record_count) {
		FREE_NULL_BITMAP(total_node_bitmap);
		total_node_bitmap = bit_alloc(node_record_count);
		/*
		 * Set all bits, bitmap2hostlist() will filter out the non-NULL
		 * node_record_t's in node_record_table_ptr.
		 */
		bit_set_all(total_node_bitmap);
		prev_node_record_count = node_record_count;
	}

	slurm_mutex_lock(&cluster_hl_mutex);

	FREE_NULL_HOSTLIST(cluster_hl);
	cluster_hl = bitmap2hostlist(total_node_bitmap);
	if (cluster_hl == NULL) {
		cluster_nodes = xstrdup("");
	} else {
		/*
		 * Can sort since db job's node_inx is based off of
		 * cluster_nodes instead of node_record_table_ptr.
		 * See acct_storage_p_node_inx().
		 */
		hostlist_sort(cluster_hl);
		cluster_nodes = hostlist_ranged_string_xmalloc(cluster_hl);
	}

	assoc_mgr_lock(&locks);
	xfree(cluster_tres);
	cluster_tres = slurmdb_make_tres_string(
		assoc_mgr_tres_list, TRES_STR_FLAG_SIMPLE);
	assoc_mgr_unlock(&locks);

	slurm_mutex_unlock(&cluster_hl_mutex);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	if (first) {
		/* since this can be loaded from many different places
		   only tell us once. */
		if (!slurm_conf.cluster_name)
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);

		slurmdbd_agent_config_setup();

		verbose("%s loaded", plugin_name);

		if (running_in_slurmctld() &&
		    !(slurm_conf.accounting_storage_enforce &
		      ACCOUNTING_ENFORCE_NO_JOBS)) {
			slurm_thread_create(&db_inx_handler_thread,
					    _set_db_inx_thread, NULL);
		}

		ext_dbd_init();

		first = 0;
	} else {
		debug4("%s loaded", plugin_name);
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	plugin_shutdown = time(NULL);

	if (running_db_inx)
		debug("Waiting for db_inx thread to finish.");

	slurm_mutex_lock(&db_inx_lock);

	/* signal the db_inx thread */
	if (db_inx_handler_thread)
		slurm_cond_signal(&db_inx_cond);

	slurm_mutex_unlock(&db_inx_lock);

	/* Now join outside the lock */
	if (db_inx_handler_thread)
		pthread_join(db_inx_handler_thread, NULL);

	ext_dbd_fini();
	xfree(cluster_nodes);
	xfree(cluster_tres);
	FREE_NULL_HOSTLIST(cluster_hl);

	first = 1;

	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(
	int conn_num, uint16_t *persist_conn_flags,
	bool rollback, char *cluster_name)
{
	slurm_persist_conn_t *pc;

	if (first)
		init();

	pc = dbd_conn_open(persist_conn_flags, cluster_name, NULL, 0);

	slurmdbd_agent_set_conn(pc);

	if (pc && persist_conn_flags)
		*persist_conn_flags = pc->flags;

	return pc;
}

extern int acct_storage_p_close_connection(void **db_conn)
{
	slurmdbd_agent_rem_conn();

	dbd_conn_close((slurm_persist_conn_t **)db_conn);

	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(void *db_conn, bool commit)
{
	persist_msg_t req = {0};
	dbd_fini_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_fini_msg_t));

	get_msg.close_conn = 0;
	get_msg.commit = (uint16_t)commit;

	req.msg_type = DBD_FINI;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_users(void *db_conn, uint32_t uid,
				    List user_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = user_list;

	req.msg_type = DBD_ADD_USERS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_coord(void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond)
{
	persist_msg_t req = {0};
	dbd_acct_coord_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_acct_coord_msg_t));
	get_msg.acct_list = acct_list;
	get_msg.cond = user_cond;

	req.msg_type = DBD_ADD_ACCOUNT_COORDS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_accts(void *db_conn, uint32_t uid,
				    List acct_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = acct_list;

	req.msg_type = DBD_ADD_ACCOUNTS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = cluster_list;

	req.msg_type = DBD_ADD_CLUSTERS;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS) {
		rc = resp_code;
	}
	return rc;
}

extern int acct_storage_p_add_federations(void *db_conn, uint32_t uid,
					  List federation_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = federation_list;

	req.msg_type = DBD_ADD_FEDERATIONS;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS) {
		rc = resp_code;
	}
	return rc;
}

extern int acct_storage_p_add_tres(void *db_conn,
				   uint32_t uid, List tres_list_in)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	/* This means we are updating views which don't apply in this plugin */
	if (!tres_list_in)
		return SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = tres_list_in;

	req.msg_type = DBD_ADD_TRES;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_assocs(void *db_conn, uint32_t uid,
				     List assoc_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = assoc_list;

	req.msg_type = DBD_ADD_ASSOCS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid,
				  List qos_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = qos_list;

	req.msg_type = DBD_ADD_QOS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_res(void *db_conn, uint32_t uid,
				  List res_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = res_list;

	req.msg_type = DBD_ADD_RES;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_wckeys(void *db_conn, uint32_t uid,
				     List wckey_list)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = wckey_list;

	req.msg_type = DBD_ADD_WCKEYS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_reservation(void *db_conn,
					  slurmdb_reservation_rec_t *resv)
{
	persist_msg_t req = {0};
	dbd_rec_msg_t get_msg;
	int rc;

	if (!resv) {
		error("No reservation was given to add.");
		return SLURM_ERROR;
	}

	if (!resv->id) {
		error("An id is needed to add a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->time_start) {
		error("A start time is needed to add a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->cluster || !resv->cluster[0]) {
		error("A cluster name is needed to add a reservation.");
		return SLURM_ERROR;
	}

	memset(&get_msg, 0, sizeof(dbd_rec_msg_t));
	get_msg.rec = resv;

	req.msg_type = DBD_ADD_RESV;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &req);

	return rc;
}

extern List acct_storage_p_modify_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = user_cond;
	get_msg.rec = user;

	req.msg_type = DBD_MODIFY_USERS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_USERS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_accts(void *db_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond,
					slurmdb_account_rec_t *acct)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = acct_cond;
	get_msg.rec = acct;

	req.msg_type = DBD_MODIFY_ACCOUNTS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_ACCOUNTS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster)
{
	persist_msg_t req = {0};
	dbd_modify_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = cluster_cond;
	get_msg.rec = cluster;

	req.msg_type = DBD_MODIFY_CLUSTERS;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_CLUSTERS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond,
	slurmdb_assoc_rec_t *assoc)
{
	persist_msg_t req = {0};
	dbd_modify_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = assoc_cond;
	get_msg.rec = assoc;

	req.msg_type = DBD_MODIFY_ASSOCS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_ASSOCS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_federations(
	void *db_conn, uint32_t uid,
	slurmdb_federation_cond_t *fed_cond,
	slurmdb_federation_rec_t *fed)
{
	persist_msg_t req = {0};
	dbd_modify_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = fed_cond;
	get_msg.rec = fed;

	req.msg_type = DBD_MODIFY_FEDERATIONS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_FEDERATIONS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_job(void *db_conn, uint32_t uid,
				      slurmdb_job_cond_t *job_cond,
				      slurmdb_job_rec_t *job)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = job_cond;
	get_msg.rec = job;

	req.msg_type = DBD_MODIFY_JOB;
	req.conn = db_conn;
	req.data = &get_msg;

	/*
	 * Just put it on the list and go, usually means we are coming from the
	 * slurmctld.
	 */
	if (job_cond && (job_cond->flags & JOBCOND_FLAG_NO_WAIT)) {
		slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &req);
		goto end_it;
	}

	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_JOB failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}
end_it:
	return ret_list;
}

extern List acct_storage_p_modify_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = qos_cond;
	get_msg.rec = qos;

	req.msg_type = DBD_MODIFY_QOS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_QOS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_res(void *db_conn, uint32_t uid,
				      slurmdb_res_cond_t *res_cond,
				      slurmdb_res_rec_t *res)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = res_cond;
	get_msg.rec = res;

	req.msg_type = DBD_MODIFY_RES;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_RES failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_wckeys(void *db_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = wckey_cond;
	get_msg.rec = wckey;

	req.msg_type = DBD_MODIFY_WCKEYS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_MODIFY_WCKEYS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern int acct_storage_p_modify_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	persist_msg_t req = {0};
	dbd_rec_msg_t get_msg;
	int rc;

	if (!resv) {
		error("No reservation was given to edit");
		return SLURM_ERROR;
	}

	if (!resv->id) {
		error("An id is needed to edit a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->time_start) {
		error("A start time is needed to edit a reservation.");
		return SLURM_ERROR;
	}
	if (!resv->cluster || !resv->cluster[0]) {
		error("A cluster name is needed to edit a reservation.");
		return SLURM_ERROR;
	}

	if (!resv->time_start_prev) {
		error("We need a time to check for last "
		      "start of reservation.");
		return SLURM_ERROR;
	}

	memset(&get_msg, 0, sizeof(dbd_rec_msg_t));
	get_msg.rec = resv;
	req.msg_type = DBD_MODIFY_RESV;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &req);

	return rc;
}

extern List acct_storage_p_remove_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = user_cond;

	req.msg_type = DBD_REMOVE_USERS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_USERS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		rc = got_msg->return_code;
		slurmdbd_free_list_msg(got_msg);
		errno = rc;
	}

	return ret_list;
}

extern List acct_storage_p_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond)
{
	persist_msg_t req = {0};
	dbd_acct_coord_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_acct_coord_msg_t));
	get_msg.acct_list = acct_list;
	get_msg.cond = user_cond;

	req.msg_type = DBD_REMOVE_ACCOUNT_COORDS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_ACCOUNT_COORDS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_remove_accts(void *db_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = acct_cond;

	req.msg_type = DBD_REMOVE_ACCOUNTS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_ACCTS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		rc = got_msg->return_code;
		slurmdbd_free_list_msg(got_msg);
		errno = rc;
	}

	return ret_list;
}

extern List acct_storage_p_remove_clusters(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *cluster_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = cluster_cond;

	req.msg_type = DBD_REMOVE_CLUSTERS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_CLUSTERS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		rc = got_msg->return_code;
		slurmdbd_free_list_msg(got_msg);
		errno = rc;
	}

	return ret_list;
}

extern List acct_storage_p_remove_assocs(
	void *db_conn, uint32_t uid,
	slurmdb_assoc_cond_t *assoc_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = assoc_cond;

	req.msg_type = DBD_REMOVE_ASSOCS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_ASSOCS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		rc = got_msg->return_code;
		slurmdbd_free_list_msg(got_msg);
		errno = rc;
	}

	return ret_list;
}

extern List acct_storage_p_remove_federations(
	void *db_conn, uint32_t uid,
	slurmdb_federation_cond_t *fed_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = fed_cond;

	req.msg_type = DBD_REMOVE_FEDERATIONS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_FEDERATIONS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		rc = got_msg->return_code;
		slurmdbd_free_list_msg(got_msg);
		errno = rc;
	}

	return ret_list;
}

extern List acct_storage_p_remove_qos(
	void *db_conn, uint32_t uid,
	slurmdb_qos_cond_t *qos_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = qos_cond;

	req.msg_type = DBD_REMOVE_QOS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_QOS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_remove_res(
	void *db_conn, uint32_t uid,
	slurmdb_res_cond_t *res_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = res_cond;

	req.msg_type = DBD_REMOVE_RES;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);
	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_RES failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}
	return ret_list;
}

extern List acct_storage_p_remove_wckeys(
	void *db_conn, uint32_t uid,
	slurmdb_wckey_cond_t *wckey_cond)
{
	persist_msg_t req = {0};
	dbd_cond_msg_t get_msg;
	int rc;
	persist_msg_t resp = {0};
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = wckey_cond;

	req.msg_type = DBD_REMOVE_WCKEYS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_REMOVE_WCKEYS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern int acct_storage_p_remove_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	persist_msg_t req = {0};
	dbd_rec_msg_t get_msg;
	int rc;

	if (!resv) {
		error("No reservation was given to remove");
		return SLURM_ERROR;
	}

	if (!resv->id) {
		error("An id is needed to remove a reservation.");
		return SLURM_ERROR;
	}

	if (!resv->time_start) {
		error("A start time is needed to remove a reservation.");
		return SLURM_ERROR;
	}

	if (!resv->cluster || !resv->cluster[0]) {
		error("A cluster name is needed to remove a reservation.");
		return SLURM_ERROR;
	}

	memset(&get_msg, 0, sizeof(dbd_rec_msg_t));
	get_msg.rec = resv;

	req.msg_type = DBD_REMOVE_RESV;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &req);

	return rc;
}

extern List acct_storage_p_get_users(void *db_conn, uid_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = user_cond;

	req.msg_type = DBD_GET_USERS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_USERS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_USERS) {
		error("response type not DBD_GOT_USERS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_accts(void *db_conn, uid_t uid,
				     slurmdb_account_cond_t *acct_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = acct_cond;

	req.msg_type = DBD_GET_ACCOUNTS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_ACCOUNTS failure: %s", slurm_strerror(rc));
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_ACCOUNTS) {
		error("response type not DBD_GOT_ACCOUNTS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}


	return ret_list;
}

extern List acct_storage_p_get_clusters(void *db_conn, uid_t uid,
					slurmdb_cluster_cond_t *cluster_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = cluster_cond;

	req.msg_type = DBD_GET_CLUSTERS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_CLUSTERS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_CLUSTERS) {
		error("response type not DBD_GOT_CLUSTERS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}


	return ret_list;
}

extern List acct_storage_p_get_federations(void *db_conn, uid_t uid,
					   slurmdb_federation_cond_t *fed_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = fed_cond;

	req.msg_type = DBD_GET_FEDERATIONS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_FEDERATIONS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_FEDERATIONS) {
		error("response type not DBD_GOT_FEDERATIONS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_config(void *db_conn, char *config_name)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	if (first)
		init();

	req.msg_type = DBD_GET_CONFIG;
	req.conn = db_conn;
	req.data = config_name;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_CONFIG failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_CONFIG) {
		error("response type not DBD_GOT_CONFIG: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_tres(void *db_conn, uid_t uid,
				    slurmdb_tres_cond_t *tres_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = tres_cond;

	req.msg_type = DBD_GET_TRES;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_TRES failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_TRES) {
		error("response type not DBD_GOT_TRES: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_assocs(
	void *db_conn, uid_t uid, slurmdb_assoc_cond_t *assoc_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = assoc_cond;

	req.msg_type = DBD_GET_ASSOCS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_ASSOCS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_ASSOCS) {
		error("response type not DBD_GOT_ASSOCS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_events(void *db_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = event_cond;

	req.msg_type = DBD_GET_EVENTS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_EVENTS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_EVENTS) {
		error("response type not DBD_GOT_EVENTS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_problems(void *db_conn, uid_t uid,
					slurmdb_assoc_cond_t *assoc_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = assoc_cond;

	req.msg_type = DBD_GET_PROBS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_PROBS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_PROBS) {
		error("response type not DBD_GOT_PROBS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_qos(void *db_conn, uid_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = qos_cond;

	req.msg_type = DBD_GET_QOS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_QOS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_QOS) {
		error("response type not DBD_GOT_QOS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if (!got_msg->my_list)
		        ret_list = list_create(NULL);
		else
			ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_res(void *db_conn, uid_t uid,
				   slurmdb_res_cond_t *res_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = res_cond;

	req.msg_type = DBD_GET_RES;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_RES failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_RES) {
		error("response type not DBD_GOT_RES: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if (!got_msg->my_list)
			ret_list = list_create(NULL);
		else
			ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_wckeys(void *db_conn, uid_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = wckey_cond;

	req.msg_type = DBD_GET_WCKEYS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_WCKEYS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_WCKEYS) {
		error("response type not DBD_GOT_WCKEYS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if (!got_msg->my_list)
		        ret_list = list_create(NULL);
		else
			ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_reservations(
	void *db_conn, uid_t uid,
	slurmdb_reservation_cond_t *resv_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = resv_cond;

	req.msg_type = DBD_GET_RESVS;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_RESVS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_RESVS) {
		error("response type not DBD_GOT_RESVS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if (!got_msg->my_list)
		        ret_list = list_create(NULL);
		else
			ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_txn(void *db_conn, uid_t uid,
				   slurmdb_txn_cond_t *txn_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = txn_cond;

	req.msg_type = DBD_GET_TXN;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_TXN failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_TXN) {
		error("response type not DBD_GOT_TXN: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern int acct_storage_p_get_usage(void *db_conn, uid_t uid,
				    void *in, slurmdbd_msg_type_t type,
				    time_t start, time_t end)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_usage_msg_t get_msg;
	dbd_usage_msg_t *got_msg;
	slurmdb_assoc_rec_t *got_assoc = (slurmdb_assoc_rec_t *)in;
	slurmdb_wckey_rec_t *got_wckey = (slurmdb_wckey_rec_t *)in;
	slurmdb_cluster_rec_t *got_cluster = (slurmdb_cluster_rec_t *)in;
	List *my_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_usage_msg_t));
	get_msg.rec = in;
	get_msg.start = start;
	get_msg.end = end;
	req.msg_type = type;
	req.conn = db_conn;

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		my_list = &got_assoc->accounting_list;
		break;
	case DBD_GET_WCKEY_USAGE:
		my_list = &got_wckey->accounting_list;
		break;
	case DBD_GET_CLUSTER_USAGE:
		my_list = &got_cluster->accounting_list;
		break;
	default:
		error("Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("%s failure: %m",
		      slurmdbd_msg_type_2_str(type, 1));
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			(*my_list) = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_ASSOC_USAGE
		   && resp.msg_type != DBD_GOT_WCKEY_USAGE
		   && resp.msg_type != DBD_GOT_CLUSTER_USAGE) {
		error("response type not DBD_GOT_*_USAGE: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_usage_msg_t *) resp.data;
		switch (type) {
		case DBD_GET_ASSOC_USAGE:
			got_assoc = (slurmdb_assoc_rec_t *)got_msg->rec;
			(*my_list) = got_assoc->accounting_list;
			got_assoc->accounting_list = NULL;
			break;
		case DBD_GET_WCKEY_USAGE:
			got_wckey = (slurmdb_wckey_rec_t *)got_msg->rec;
			(*my_list) = got_wckey->accounting_list;
			got_wckey->accounting_list = NULL;
			break;
		case DBD_GET_CLUSTER_USAGE:
			got_cluster = (slurmdb_cluster_rec_t *)got_msg->rec;
			(*my_list) = got_cluster->accounting_list;
			got_cluster->accounting_list = NULL;
			break;
		default:
			error("Unknown usage type %d", type);
			rc = SLURM_ERROR;
			break;
		}

		slurmdbd_free_usage_msg(got_msg, resp.msg_type);
	}

	return rc;
}

extern int acct_storage_p_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data,
				     List *rollup_stats_list_in)
{
	persist_msg_t req = {0};
	dbd_roll_usage_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_roll_usage_msg_t));
	get_msg.end = sent_end;
	get_msg.start = sent_start;
	get_msg.archive_data = archive_data;

	req.msg_type = DBD_ROLL_USAGE;
	req.conn = db_conn;

	req.data = &get_msg;

	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;
	else
		info("SUCCESS");
	return rc;
}

extern int acct_storage_p_fix_runaway_jobs(void *db_conn, uint32_t uid,
					   List jobs)
{
	persist_msg_t req = {0};
	dbd_list_msg_t get_msg;
	int rc, resp_code = SLURM_SUCCESS;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = jobs;

	req.msg_type = DBD_FIX_RUNAWAY_JOB;
	req.conn = db_conn;
	req.data = &get_msg;

	rc = dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION,
				       &req, &resp_code);

	if (resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   node_record_t *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	persist_msg_t msg = {0};
	dbd_node_state_msg_t req;
	char *my_reason;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

	memset(&req, 0, sizeof(dbd_node_state_msg_t));
	req.hostlist   = node_ptr->name;
	req.new_state  = DBD_NODE_STATE_DOWN;
	req.event_time = event_time;
	req.reason     = my_reason;
	req.reason_uid = reason_uid;
	req.state      = node_ptr->node_state;
	req.tres_str   = node_ptr->tres_str;

	msg.msg_type   = DBD_NODE_STATE;
	msg.conn       = db_conn;
	msg.data       = &req;

	//info("sending a down message here");
	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Create bitmap based off of hostlist order instead of node_ptr->index.
 *
 * node_record_table_ptr can have NULL slots and result in bitmaps that don't
 * match the hostlist that the dbd needs.
 * .e.g.
 * node_record_table_ptr
 * [0]=node1
 * [1]=NULL
 * [2]=node2
 * job runs on node1 and node2.
 *
 * The bitmap generated against node_record_table_ptr would be 0,2. But the dbd
 * doesn't know about the NULL slots and expects node[1-2] to be 0,1. A query
 * for jobs running on node2 won't be found because node2 on the controller is
 * index 2 and on the dbd it's index 1. See setup_cluster_list_with_inx() and
 * good_nodes_from_inx().
 */
extern char *acct_storage_p_node_inx(void *db_conn, char *nodes)
{
	char *host, *ret_str;
	hostlist_t node_hl;
	bitstr_t *node_bitmap;
	hostlist_iterator_t h_itr;

	if (!nodes || !cluster_hl)
		return NULL;

	node_hl = hostlist_create(nodes);
	node_bitmap = bit_alloc(node_record_count);
	h_itr = hostlist_iterator_create(node_hl);

	slurm_mutex_lock(&cluster_hl_mutex);
	while ((host = hostlist_next(h_itr))) {
		int loc;
		if ((loc = hostlist_find(cluster_hl, host)) != -1)
			bit_set(node_bitmap, loc);
		free(host);
	}
	slurm_mutex_unlock(&cluster_hl_mutex);

	hostlist_iterator_destroy(h_itr);
	FREE_NULL_HOSTLIST(node_hl);

	ret_str = bit_fmt_full(node_bitmap);
	FREE_NULL_BITMAP(node_bitmap);
	return ret_str;
}

extern int clusteracct_storage_p_node_up(void *db_conn, node_record_t *node_ptr,
					 time_t event_time)
{
	persist_msg_t msg = {0};
	dbd_node_state_msg_t req;

	if (IS_NODE_FUTURE(node_ptr) ||
	    (IS_NODE_CLOUD(node_ptr) && IS_NODE_POWERED_DOWN(node_ptr)))
		return SLURM_SUCCESS;

	memset(&req, 0, sizeof(dbd_node_state_msg_t));
	req.hostlist   = node_ptr->name;
	req.new_state  = DBD_NODE_STATE_UP;
	req.event_time = event_time;
	req.reason     = NULL;
	msg.msg_type   = DBD_NODE_STATE;
	msg.conn       = db_conn;
	msg.data       = &req;

	// info("sending an up message here");
	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_tres(void *db_conn,
					      char *cluster_nodes_in,
					      char *tres_str_in,
					      time_t event_time,
					      uint16_t rpc_version)
{
	char *send_cluster_nodes, *send_cluster_tres;
	int rc = SLURM_ERROR;
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	lock_slurmctld(node_write_lock);

	_update_cluster_nodes();
	/* Make copies while in locks that protect the strings */
	send_cluster_nodes = xstrdup(cluster_nodes);
	send_cluster_tres = xstrdup(cluster_tres);

	unlock_slurmctld(node_write_lock);

	event_time = time(NULL);
	rc = _send_cluster_tres(db_conn, send_cluster_nodes,
				send_cluster_tres, event_time,
				rpc_version);

	xfree(send_cluster_nodes);
	xfree(send_cluster_tres);

	if ((rc == ACCOUNTING_FIRST_REG) ||
	    (rc == ACCOUNTING_NODES_CHANGE_DB) ||
	    (rc == ACCOUNTING_TRES_CHANGE_DB)) {
		acct_storage_p_send_all(db_conn, event_time, rc);
		rc = SLURM_SUCCESS;
	}

	return rc;
}

extern int clusteracct_storage_p_register_ctld(void *db_conn, uint16_t port)
{
	persist_msg_t msg = {0};
	dbd_register_ctld_msg_t req;
	int rc = SLURM_SUCCESS;

	memset(&req, 0, sizeof(dbd_register_ctld_msg_t));

	req.port         = port;
	req.dimensions   = SYSTEM_DIMENSIONS;
	req.flags        = slurmdb_setup_cluster_flags();
	req.plugin_id_select = select_get_plugin_id();

	msg.msg_type     = DBD_REGISTER_CTLD;
	msg.conn         = db_conn;
	msg.data         = &req;

	if (db_conn &&
	    (((slurm_persist_conn_t *)db_conn)->flags & PERSIST_FLAG_EXT_DBD)) {
		req.flags |= CLUSTER_FLAG_EXT;
		info("Registering slurmctld at port %u with slurmdbd %s:%d",
		     port,
		     ((slurm_persist_conn_t *)db_conn)->rem_host,
		     ((slurm_persist_conn_t *)db_conn)->rem_port);
	} else
		info("Registering slurmctld at port %u with slurmdbd", port);

	dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION, &msg, &rc);

	return rc;
}

extern int clusteracct_storage_p_register_disconn_ctld(
	void *db_conn, char *control_host)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_fini_ctld(void *db_conn,
					   char *ip, uint16_t port,
					   char *cluster_nodes)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(void *db_conn, job_record_t *job_ptr)
{
	persist_msg_t msg = {0}, msg_rc;
	dbd_job_start_msg_t req;
	dbd_id_rc_msg_t *resp;
	int rc = SLURM_SUCCESS;

	if ((rc = _setup_job_start_msg(&req, job_ptr)) != SLURM_SUCCESS)
		return rc;

	msg.msg_type      = DBD_JOB_START;
	msg.conn          = db_conn;
	msg.data          = &req;

	/* If we already have the db_index don't wait around for it
	 * again just send the message when not resizing.  This also applies
	 * when the slurmdbd is down and we are about to remove the job from
	 * the system.  We don't want to wait for the db_index in that situation
	 * either.
	 */
	if ((req.db_index && !IS_JOB_RESIZING(job_ptr))
	    || (!req.db_index && IS_JOB_FINISHED(job_ptr))) {
		/*
		 * This is to ensure we don't do this multiple times for the
		 * same job.  This can happen when an account is being
		 * deleted and hense the associations dealing with it.
		 */
		if (!req.db_index)
			job_ptr->db_index = NO_VAL64;

		if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0) {
			_partial_free_dbd_job_start(&req);
			return SLURM_ERROR;
		}
		_partial_free_dbd_job_start(&req);
		return SLURM_SUCCESS;
	}
	/* If we don't have the db_index we need to wait for it to be
	 * used in the other submissions for this job.
	 */
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &msg, &msg_rc);
	if (rc != SLURM_SUCCESS) {
		if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0) {
			_partial_free_dbd_job_start(&req);
			return SLURM_ERROR;
		}
	} else if (msg_rc.msg_type != DBD_ID_RC) {
		error("response type not DBD_ID_RC: %u",
		      msg_rc.msg_type);
	} else {
		resp = (dbd_id_rc_msg_t *) msg_rc.data;
		job_ptr->db_index = resp->db_index;
		_sending_script_env(resp, job_ptr);
		rc = resp->return_code;
		//info("here got %d for return code", resp->rc);
		slurmdbd_free_id_rc_msg(resp);
	}
	_partial_free_dbd_job_start(&req);

	return rc;
}

extern int jobacct_storage_p_job_heavy(void *db_conn, job_record_t *job_ptr)
{
	persist_msg_t msg = {0};
	dbd_job_heavy_msg_t req;
	int rc = SLURM_SUCCESS;

	/* No reason to be here */
	if (!(job_ptr->bit_flags & (JOB_SEND_ENV | JOB_SEND_SCRIPT)))
		return SLURM_SUCCESS;

	if (!job_ptr->db_index
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("%s: Not inputing this job, it has no submit time.",
		      __func__);
		return SLURM_ERROR;
	}

	xassert(job_ptr->details);

	memset(&req, 0, sizeof(req));

	if (job_ptr->bit_flags & JOB_SEND_ENV) {
		uint32_t env_size = 0;
		char **env = get_job_env(job_ptr, &env_size);
		if (env) {
			char *pos = NULL;
			for (int i = 0; i < env_size; i++)
				xstrfmtcatat(req.env, &pos, "%s\n", env[i]);
			xfree(env[0]);
			xfree(env);
		}
		req.env_hash = job_ptr->details->env_hash;
	}

	if (job_ptr->bit_flags & JOB_SEND_SCRIPT) {
		req.script_buf = get_job_script(job_ptr);
		req.script_hash = job_ptr->details->script_hash;
	}

	msg.msg_type    = DBD_JOB_HEAVY;
	msg.conn        = db_conn;
	msg.data        = &req;

	rc = slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg);

	FREE_NULL_BUFFER(req.script_buf);
	xfree(req.env);

	return rc;
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(void *db_conn, job_record_t *job_ptr)
{
	persist_msg_t msg = {0};
	dbd_job_comp_msg_t req;

	if (!job_ptr->db_index
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("jobacct_storage_p_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	memset(&req, 0, sizeof(dbd_job_comp_msg_t));

	req.assoc_id    = job_ptr->assoc_id;

	req.admin_comment = job_ptr->admin_comment;

	if (slurm_conf.conf_flags & CTL_CONF_SJC)
		req.comment = job_ptr->comment;
	if (slurm_conf.conf_flags & CTL_CONF_SJX)
		req.extra = job_ptr->extra;

	req.db_index    = job_ptr->db_index;
	req.derived_ec  = job_ptr->derived_ec;
	req.exit_code   = job_ptr->exit_code;
	req.failed_node = job_ptr->failed_node;
	req.job_id      = job_ptr->job_id;
	if (IS_JOB_RESIZING(job_ptr)) {
		req.end_time    = job_ptr->resize_time;
		req.job_state   = JOB_RESIZING;
	} else {
		req.end_time    = job_ptr->end_time;
		if (IS_JOB_REQUEUED(job_ptr))
			req.job_state   = JOB_REQUEUE;
		else if (IS_JOB_REVOKED(job_ptr))
			req.job_state   = JOB_REVOKED;
		else
			req.job_state   = job_ptr->job_state & JOB_STATE_BASE;
	}
	req.req_uid     = job_ptr->requid;
	req.nodes       = job_ptr->nodes;

	if (job_ptr->resize_time) {
		req.start_time  = job_ptr->resize_time;
		req.submit_time = job_ptr->resize_time;
	} else {
		req.start_time  = job_ptr->start_time;
		if (job_ptr->details)
			req.submit_time = job_ptr->details->submit_time;
	}

	if (!(job_ptr->bit_flags & TRES_STR_CALC))
		req.tres_alloc_str = job_ptr->tres_alloc_str;

	msg.msg_type    = DBD_JOB_COMPLETE;
	msg.conn        = db_conn;
	msg.data        = &req;

	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(void *db_conn, step_record_t *step_ptr)
{
	uint32_t tasks = 0, nodes = 0, task_dist = 0;
	char *node_list = NULL;
	persist_msg_t msg = {0};
	dbd_step_start_msg_t req;

	if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		tasks = step_ptr->job_ptr->total_cpus;
		nodes = step_ptr->job_ptr->total_nodes;
		node_list = step_ptr->job_ptr->nodes;
	} else {
		tasks = step_ptr->step_layout->task_cnt;
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		node_list = step_ptr->step_layout->node_list;
	}

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}
	memset(&req, 0, sizeof(dbd_step_start_msg_t));

	req.assoc_id    = step_ptr->job_ptr->assoc_id;
	req.container   = step_ptr->container;
	req.db_index    = step_ptr->job_ptr->db_index;
	req.name        = step_ptr->name;
	req.nodes       = node_list;
	/* reate req->node_inx outside of locks when packing */
	req.node_cnt    = nodes;
	if (step_ptr->start_time > step_ptr->job_ptr->resize_time)
		req.start_time = step_ptr->start_time;
	else
		req.start_time = step_ptr->job_ptr->resize_time;

	if (step_ptr->job_ptr->resize_time)
		req.job_submit_time   = step_ptr->job_ptr->resize_time;
	else if (step_ptr->job_ptr->details)
		req.job_submit_time   = step_ptr->job_ptr->details->submit_time;

	memcpy(&req.step_id, &step_ptr->step_id, sizeof(req.step_id));

	if (step_ptr->step_layout)
		req.task_dist   = step_ptr->step_layout->task_dist;
	req.task_dist   = task_dist;

	req.total_tasks = tasks;

	req.submit_line = step_ptr->submit_line;
	req.tres_alloc_str = step_ptr->tres_alloc_str;

	req.req_cpufreq_min = step_ptr->cpu_freq_min;
	req.req_cpufreq_max = step_ptr->cpu_freq_max;
	req.req_cpufreq_gov = step_ptr->cpu_freq_gov;

	msg.msg_type    = DBD_STEP_START;
	msg.conn        = db_conn;
	msg.data        = &req;

	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(void *db_conn,
					   step_record_t *step_ptr)
{
	uint32_t tasks = 0;
	persist_msg_t msg = {0};
	dbd_step_comp_msg_t req;

	if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT)
		tasks = 1;
	else {
		if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			tasks = step_ptr->job_ptr->total_cpus;
		else
			tasks = step_ptr->step_layout->task_cnt;
	}

	if (!step_ptr->job_ptr->db_index
	    && ((!step_ptr->job_ptr->details
		 || !step_ptr->job_ptr->details->submit_time)
		&& !step_ptr->job_ptr->resize_time)) {
		error("jobacct_storage_p_step_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	memset(&req, 0, sizeof(dbd_step_comp_msg_t));

	req.assoc_id    = step_ptr->job_ptr->assoc_id;
	req.db_index    = step_ptr->job_ptr->db_index;
	req.end_time    = time(NULL);	/* called at step completion */
	req.exit_code   = step_ptr->exit_code;
#ifndef HAVE_FRONT_END
	/* Only send this info on a non-frontend system since this
	 * information is of no use on systems that run on a front-end
	 * node.  Since something else is running the job.
	 */
	req.jobacct     = step_ptr->jobacct;
#else
	if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT)
		req.jobacct     = step_ptr->jobacct;
#endif

	req.req_uid     = step_ptr->requid;
	if (step_ptr->start_time > step_ptr->job_ptr->resize_time)
		req.start_time = step_ptr->start_time;
	else
		req.start_time = step_ptr->job_ptr->resize_time;

	if (step_ptr->job_ptr->resize_time)
		req.job_submit_time   = step_ptr->job_ptr->resize_time;
	else if (step_ptr->job_ptr->details)
		req.job_submit_time   = step_ptr->job_ptr->details->submit_time;

	if (step_ptr->job_ptr->bit_flags & TRES_STR_CALC)
		req.job_tres_alloc_str = step_ptr->job_ptr->tres_alloc_str;

	req.state       = step_ptr->state;

	memcpy(&req.step_id, &step_ptr->step_id, sizeof(req.step_id));

	req.total_tasks = tasks;

	msg.msg_type    = DBD_STEP_COMPLETE;
	msg.conn        = db_conn;
	msg.data        = &req;

	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * load into the storage a suspension of a job
 */
extern int jobacct_storage_p_suspend(void *db_conn, job_record_t *job_ptr)
{
	persist_msg_t msg = {0};
	dbd_job_suspend_msg_t req;

	memset(&req, 0, sizeof(dbd_job_suspend_msg_t));

	req.assoc_id     = job_ptr->assoc_id;
	req.job_id       = job_ptr->job_id;
	req.db_index     = job_ptr->db_index;
	req.job_state    = job_ptr->job_state & JOB_STATE_BASE;

	if (job_ptr->resize_time)
		req.submit_time   = job_ptr->resize_time;
	else if (job_ptr->details)
		req.submit_time   = job_ptr->details->submit_time;

	req.suspend_time = job_ptr->suspend_time;
	msg.msg_type     = DBD_JOB_SUSPEND;
	msg.conn         = db_conn;
	msg.data         = &req;

	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * get info from the storage
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(void *db_conn, uid_t uid,
					    slurmdb_job_cond_t *job_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List my_job_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));

	get_msg.cond = job_cond;

	req.msg_type = DBD_GET_JOBS_COND;
	req.conn = db_conn;
	req.data = &get_msg;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_JOBS_COND failure: %s", slurm_strerror(rc));
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("%s", msg->comment);
			my_job_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_JOBS) {
		error("response type not DBD_GOT_JOBS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		my_job_list = got_msg->my_list;
		got_msg->my_list = NULL;
		if (!my_job_list) {
			slurm_seterrno(got_msg->return_code);
			error("%s", slurm_strerror(got_msg->return_code));
		}
		slurmdbd_free_list_msg(got_msg);
	}

	return my_job_list;
}

/*
 * Expire old info from the storage
 * Not applicable for any database
 */
extern int jobacct_storage_p_archive(void *db_conn,
				     slurmdb_archive_cond_t *arch_cond)
{
	persist_msg_t req = {0}, resp = {0};
	dbd_cond_msg_t msg;
	int rc = SLURM_SUCCESS;

	memset(&msg, 0, sizeof(dbd_cond_msg_t));

	msg.cond     = arch_cond;

	req.msg_type = DBD_ARCHIVE_DUMP;
	req.conn = db_conn;
	req.data     = &msg;

	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_ARCHIVE_DUMP failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		rc = msg->rc;

		if (msg->rc == SLURM_SUCCESS)
			info("%s", msg->comment);
		else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else {
		error("unknown return for archive_dump");
		rc = SLURM_ERROR;
	}

	return rc;
}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(void *db_conn,
					  slurmdb_archive_rec_t *arch_rec)
{
	persist_msg_t req = {0}, resp = {0};
	int rc = SLURM_SUCCESS;

	req.msg_type = DBD_ARCHIVE_LOAD;
	req.conn = db_conn;
	req.data     = arch_rec;

	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_ARCHIVE_LOAD failure: %s",
		      slurm_strerror(rc));
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		rc = msg->rc;

		if (msg->rc == SLURM_SUCCESS)
			info("%s", msg->comment);
		else {
			slurm_seterrno(msg->rc);
			error("%s", msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
	} else {
		error("unknown return msg_type for archive_load: %s(%u)",
		      rpc_num2string(resp.msg_type), resp.msg_type);
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(void *db_conn,
						time_t event_time)
{
	persist_msg_t msg = {0};
	dbd_cluster_tres_msg_t req;

	info("Ending any jobs in accounting that were running when controller "
	     "went down on");

	memset(&req, 0, sizeof(dbd_cluster_tres_msg_t));

	req.event_time   = event_time;
	req.tres_str     = NULL;

	msg.msg_type     = DBD_FLUSH_JOBS;
	msg.conn         = db_conn;
	msg.data         = &req;

	if (slurmdbd_agent_send(SLURM_PROTOCOL_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int acct_storage_p_reconfig(void *db_conn, bool dbd)
{
	persist_msg_t msg = {0};
	int rc = SLURM_SUCCESS;

	if (!dbd) {
		slurmdbd_agent_config_setup();
		ext_dbd_reconfig();
		return SLURM_SUCCESS;
	}

	msg.msg_type = DBD_RECONFIG;
	msg.conn = db_conn;
	dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION, &msg, &rc);

	return rc;
}

extern int acct_storage_p_reset_lft_rgt(void *db_conn, uid_t uid,
					List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_stats(void *db_conn, slurmdb_stats_rec_t **stats)
{
	persist_msg_t req = {0}, resp = {0};
	int rc;

	xassert(stats);

	req.msg_type = DBD_GET_STATS;
	req.conn = db_conn;
	rc = dbd_conn_send_recv(SLURM_PROTOCOL_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("DBD_GET_STATS failure: %m");
	else if (resp.msg_type == PERSIST_RC) {
		persist_rc_msg_t *msg = resp.data;
		if (msg->rc == SLURM_SUCCESS) {
			info("RC:%d %s", msg->rc, msg->comment);
		} else {
			slurm_seterrno(msg->rc);
			info("RC:%d %s", msg->rc, msg->comment);
		}
		rc = msg->rc;
		slurm_persist_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_STATS) {
		error("response type not DBD_GOT_STATS: %u",
		      resp.msg_type);
		rc = SLURM_ERROR;
	} else {
		*stats = (slurmdb_stats_rec_t *) resp.data;
	}

	return rc;
}

extern int acct_storage_p_clear_stats(void *db_conn)
{
	persist_msg_t msg = {0};
	int rc = SLURM_SUCCESS;

	msg.msg_type = DBD_CLEAR_STATS;
	msg.conn = db_conn;
	dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION, &msg, &rc);

	return rc;
}

extern int acct_storage_p_get_data(void *db_conn, acct_storage_info_t dinfo,
				   void *data)
{
	int *int_data = (int *) data;
	int rc = SLURM_SUCCESS;

	switch (dinfo) {
	case ACCT_STORAGE_INFO_CONN_ACTIVE:
		*int_data = slurmdbd_conn_active();
		break;
	case ACCT_STORAGE_INFO_AGENT_COUNT:
		*int_data = slurmdbd_agent_queue_count();
		break;
	default:
		error("data request %d invalid", dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern void acct_storage_p_send_all(void *db_conn, time_t event_time,
				    slurm_msg_type_t msg_type)
{
	/*
	 * Ignore the rcs here because if there was an error we will
	 * push the requests on the queue and process them when the
	 * database server comes back up.
	 */
	debug2("called %s", rpc_num2string(msg_type));
	switch (msg_type) {
	case ACCOUNTING_FIRST_REG:
	case ACCOUNTING_NODES_CHANGE_DB:
		(void) send_jobs_to_accounting();
		(void) send_resvs_to_accounting(msg_type);
		/* fall through */
	case ACCOUNTING_TRES_CHANGE_DB:
		/* No need to do jobs or resvs when only the TRES change. */
		(void) send_nodes_to_accounting(event_time);
		break;
	default:
		error("%s: unknown message type of %d given",
		      __func__, msg_type);
		xassert(0);
	}
}

extern int acct_storage_p_shutdown(void *db_conn)
{
	persist_msg_t msg = {0};
	int rc = SLURM_SUCCESS;

	msg.msg_type = DBD_SHUTDOWN;
	msg.conn = db_conn;
	dbd_conn_send_recv_rc_msg(SLURM_PROTOCOL_VERSION, &msg, &rc);

	return rc;
}
