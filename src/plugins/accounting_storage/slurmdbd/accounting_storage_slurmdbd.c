/*****************************************************************************\
 *  accounting_storage_slurmdbd.c - accounting interface to slurmdbd.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>

#include <slurm/slurm_errno.h>

#include "src/common/slurm_xlator.h"
#include "src/common/jobacct_common.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
slurm_ctl_conf_t slurmctld_conf;
List job_list = NULL;

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
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Accounting storage SLURMDBD plugin";
const char plugin_type[] = "accounting_storage/slurmdbd";
const uint32_t plugin_version = 100;

static char *slurmdbd_auth_info = NULL;

static pthread_t db_inx_handler_thread;
static pthread_t cleanup_handler_thread;
static pthread_mutex_t db_inx_lock = PTHREAD_MUTEX_INITIALIZER;
static bool running_db_inx = 0;

extern int jobacct_storage_p_job_start(void *db_conn,
				       struct job_record *job_ptr);

static void _partial_destroy_dbd_job_start(void *object)
{
	dbd_job_start_msg_t *req = (dbd_job_start_msg_t *)object;
	if(req) {
		xfree(req->block_id);
		xfree(req);
	}
}

static int _setup_job_start_msg(dbd_job_start_msg_t *req,
				struct job_record *job_ptr)
{
	char temp_bit[BUF_SIZE];

	if (!job_ptr->details || !job_ptr->details->submit_time) {
		error("jobacct_storage_p_job_start: "
		      "Not inputing this job %u, it has no submit time.",
		      job_ptr->job_id);
		return SLURM_ERROR;
	}
	memset(req, 0, sizeof(dbd_job_start_msg_t));

	req->account       = job_ptr->account;
	req->assoc_id      = job_ptr->assoc_id;
#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID,
				    &req->block_id);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NODE_CNT,
				    &req->alloc_nodes);
#else
	req->alloc_nodes   = job_ptr->total_nodes;
#endif

	if (job_ptr->resize_time) {
		req->eligible_time = job_ptr->resize_time;
		req->submit_time   = job_ptr->resize_time;
	} else if (job_ptr->details) {
		req->eligible_time = job_ptr->details->begin_time;
		req->submit_time   = job_ptr->details->submit_time;
	}

	req->start_time    = job_ptr->start_time;
	req->gid           = job_ptr->group_id;
	req->job_id        = job_ptr->job_id;

	req->db_index      = job_ptr->db_index;

	req->job_state     = job_ptr->job_state;
	req->name          = job_ptr->name;
	req->nodes         = job_ptr->nodes;

	if(job_ptr->node_bitmap)
		req->node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					job_ptr->node_bitmap);
	req->alloc_cpus = job_ptr->total_cpus;
	req->partition     = job_ptr->partition;
	if (job_ptr->details)
		req->req_cpus      = job_ptr->details->min_cpus;
	req->resv_id       = job_ptr->resv_id;
	req->priority      = job_ptr->priority;
	req->timelimit     = job_ptr->time_limit;
	req->wckey         = job_ptr->wckey;
	req->uid           = job_ptr->user_id;
	req->qos_id        = job_ptr->qos_id;

	return SLURM_SUCCESS;
}


static void *_set_db_inx_thread(void *no_data)
{
	struct job_record *job_ptr = NULL;
	ListIterator itr;
	/* Read lock on jobs */
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	/* DEF_TIMERS; */

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while(1) {
		List local_job_list = NULL;
		/* START_TIMER; */
		/* info("starting db_thread"); */
		slurm_mutex_lock(&db_inx_lock);
		/* info("in lock db_thread"); */
		running_db_inx = 1;
		if(!job_list) {
			error("No job list, exiting");
			break;
		}
		/* Here we have off loaded starting
		 * jobs in the database out of band
		 * from the job submission.  This
		 * is can make submitting jobs much
		 * faster and not lock up the
		 * controller waiting for the db inx
		 * back from the database. */
		lock_slurmctld(job_read_lock);
		itr = list_iterator_create(job_list);
		while((job_ptr = list_next(itr))) {
			if(!job_ptr->db_index) {
				dbd_job_start_msg_t *req =
					xmalloc(sizeof(dbd_job_start_msg_t));
				if(_setup_job_start_msg(req, job_ptr)
				   != SLURM_SUCCESS) {
					_partial_destroy_dbd_job_start(req);
					continue;
				}
				/* we only want to destory the pointer
				   here not the contents (except
				   block_id) so call special function
				   _partial_destroy_dbd_job_start.
				*/
				if(!local_job_list)
					local_job_list = list_create(
						_partial_destroy_dbd_job_start);
				list_append(local_job_list, req);
			}
		}
		list_iterator_destroy(itr);
		unlock_slurmctld(job_read_lock);

		if(local_job_list) {
			slurmdbd_msg_t req, resp;
			dbd_list_msg_t send_msg, *got_msg;
			int rc = SLURM_SUCCESS;

			memset(&send_msg, 0, sizeof(dbd_list_msg_t));

			send_msg.my_list = local_job_list;

			req.msg_type = DBD_SEND_MULT_JOB_START;
			req.data = &send_msg;
			rc = slurm_send_recv_slurmdbd_msg(
				SLURMDBD_VERSION, &req, &resp);
			list_destroy(local_job_list);
			if (rc != SLURM_SUCCESS)
				error("slurmdbd: DBD_SEND_MULT_JOB_START "
				      "failure: %m");
			else if (resp.msg_type == DBD_RC) {
				dbd_rc_msg_t *msg = resp.data;
				if(msg->return_code == SLURM_SUCCESS) {
					info("%s", msg->comment);
				} else
					error("%s", msg->comment);
				slurmdbd_free_rc_msg(msg);
			} else if (resp.msg_type != DBD_GOT_MULT_JOB_START) {
				error("slurmdbd: response type not "
				      "DBD_GOT_MULT_JOB_START: %u",
				      resp.msg_type);
			} else {
				dbd_id_rc_msg_t *id_ptr = NULL;
				got_msg = (dbd_list_msg_t *) resp.data;

				lock_slurmctld(job_write_lock);
				itr = list_iterator_create(got_msg->my_list);
				while((id_ptr = list_next(itr))) {
					if((job_ptr = find_job_record(
						    id_ptr->job_id)))
						job_ptr->db_index = id_ptr->id;
				}
				list_iterator_destroy(itr);
				unlock_slurmctld(job_write_lock);

				slurmdbd_free_list_msg(got_msg);
			}
		}

		running_db_inx = 0;
		slurm_mutex_unlock(&db_inx_lock);
		/* END_TIMER; */
		/* info("set all db_inx's in %s", TIME_STR); */

		/* Since it doesn't take much time at all to do this
		   check do it every 5 seconds.  This helps the DBD so
		   it doesn't have to find db_indexs of jobs that
		   haven't had the start rpc come through.
		*/
		sleep(5);
	}

	return NULL;
}

static void *_cleanup_thread(void *no_data)
{
	pthread_join(db_inx_handler_thread, NULL);
	return NULL;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	char *cluster_name = NULL;

	if (first) {
		/* since this can be loaded from many different places
		   only tell us once. */
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf",
			      plugin_name);
		xfree(cluster_name);
		slurmdbd_auth_info = slurm_get_accounting_storage_pass();
		verbose("%s loaded with AuthInfo=%s",
			plugin_name, slurmdbd_auth_info);

		if (job_list) {
			/* only do this when job_list is defined
			 * (in the slurmctld) */
			pthread_attr_t thread_attr;
			slurm_attr_init(&thread_attr);
			if (pthread_create(&db_inx_handler_thread, &thread_attr,
					   _set_db_inx_thread, NULL))
				fatal("pthread_create error %m");

			/* This is here to join the db inx thread so
			   we don't core dump if in the sleep, since
			   there is no other place to join we have to
			   create another thread to do it.
			*/
			slurm_attr_init(&thread_attr);
			if (pthread_create(&cleanup_handler_thread,
					   &thread_attr,
					   _cleanup_thread, NULL))
				fatal("pthread_create error %m");
			slurm_attr_destroy(&thread_attr);
		}
		first = 0;
	} else {
		debug4("%s loaded", plugin_name);
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	if (running_db_inx)
		debug("Waiting for db_inx thread to finish.");

	slurm_mutex_lock(&db_inx_lock);

	/* cancel the db_inx thread and then join the cleanup thread */
	if (db_inx_handler_thread)
		pthread_cancel(db_inx_handler_thread);
	if (cleanup_handler_thread)
		pthread_join(cleanup_handler_thread, NULL);

	slurm_mutex_unlock(&db_inx_lock);

	xfree(slurmdbd_auth_info);

	return SLURM_SUCCESS;
}

extern void *acct_storage_p_get_connection(
             const slurm_trigger_callbacks_t *callbacks,
             int conn_num, bool rollback, char *cluster_name)
{
	if (!slurmdbd_auth_info)
		init();

	if (slurm_open_slurmdbd_conn(slurmdbd_auth_info,
				     callbacks, rollback) == SLURM_SUCCESS)
		errno = SLURM_SUCCESS;
	/* send something back to make sure we don't run this again */
	return (void *)1;
}

extern int acct_storage_p_close_connection(void **db_conn)
{
	if(db_conn)
		*db_conn = NULL;
	return slurm_close_slurmdbd_conn();
}

extern int acct_storage_p_commit(void *db_conn, bool commit)
{
	slurmdbd_msg_t req;
	dbd_fini_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_fini_msg_t));

	get_msg.close_conn = 0;
	get_msg.commit = (uint16_t)commit;

	req.msg_type = DBD_FINI;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_users(void *db_conn, uint32_t uid,
				    List user_list)
{
	slurmdbd_msg_t req;
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = user_list;

	req.msg_type = DBD_ADD_USERS;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_coord(void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond)
{
	slurmdbd_msg_t req;
	dbd_acct_coord_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_acct_coord_msg_t));
	get_msg.acct_list = acct_list;
	get_msg.cond = user_cond;

	req.msg_type = DBD_ADD_ACCOUNT_COORDS;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_accts(void *db_conn, uint32_t uid,
				    List acct_list)
{
	slurmdbd_msg_t req;
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = acct_list;

	req.msg_type = DBD_ADD_ACCOUNTS;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list)
{
	slurmdbd_msg_t req;
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = cluster_list;

	req.msg_type = DBD_ADD_CLUSTERS;
	req.data = &get_msg;

	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS) {
		rc = resp_code;
	}
	return rc;
}

extern int acct_storage_p_add_associations(void *db_conn, uint32_t uid,
					   List association_list)
{
	slurmdbd_msg_t req;
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = association_list;

	req.msg_type = DBD_ADD_ASSOCS;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid,
				  List qos_list)
{
	slurmdbd_msg_t req;
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = qos_list;

	req.msg_type = DBD_ADD_QOS;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_wckeys(void *db_conn, uint32_t uid,
				     List wckey_list)
{
	slurmdbd_msg_t req;
	dbd_list_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_list_msg_t));
	get_msg.my_list = wckey_list;

	req.msg_type = DBD_ADD_WCKEYS;
	req.data = &get_msg;
	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern int acct_storage_p_add_reservation(void *db_conn,
					   slurmdb_reservation_rec_t *resv)
{
	slurmdbd_msg_t req;
	dbd_rec_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_rec_msg_t));
	get_msg.rec = resv;

	req.msg_type = DBD_ADD_RESV;
	req.data = &get_msg;

	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern List acct_storage_p_modify_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user)
{
	slurmdbd_msg_t req, resp;
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = user_cond;
	get_msg.rec = user;

	req.msg_type = DBD_MODIFY_USERS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_USERS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req, resp;
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = acct_cond;
	get_msg.rec = acct;

	req.msg_type = DBD_MODIFY_ACCOUNTS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_ACCOUNTS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_modify_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = cluster_cond;
	get_msg.rec = cluster;

	req.msg_type = DBD_MODIFY_CLUSTERS;
	req.data = &get_msg;

	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_CLUSTERS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_associations(
	void *db_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond,
	slurmdb_association_rec_t *assoc)
{
	slurmdbd_msg_t req;
	dbd_modify_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = assoc_cond;
	get_msg.rec = assoc;

	req.msg_type = DBD_MODIFY_ASSOCS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_ASSOCS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
					slurmdb_job_modify_cond_t *job_cond,
					slurmdb_job_rec_t *job)
{
	slurmdbd_msg_t req, resp;
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = job_cond;
	get_msg.rec = job;

	req.msg_type = DBD_MODIFY_JOB;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_JOB failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_modify_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	slurmdbd_msg_t req, resp;
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = qos_cond;
	get_msg.rec = qos;

	req.msg_type = DBD_MODIFY_QOS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_QOS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req, resp;
	dbd_modify_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_modify_msg_t));
	get_msg.cond = wckey_cond;
	get_msg.rec = wckey;

	req.msg_type = DBD_MODIFY_WCKEYS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_MODIFY_WCKEYS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_rec_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_rec_msg_t));
	get_msg.rec = resv;

	req.msg_type = DBD_MODIFY_RESV;
	req.data = &get_msg;

	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern List acct_storage_p_remove_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond)
{
	slurmdbd_msg_t req;
	dbd_cond_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = user_cond;

	req.msg_type = DBD_REMOVE_USERS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_USERS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_acct_coord_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_acct_coord_msg_t));
	get_msg.acct_list = acct_list;
	get_msg.cond = user_cond;

	req.msg_type = DBD_REMOVE_ACCOUNT_COORDS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_ACCOUNT_COORDS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_cond_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = acct_cond;

	req.msg_type = DBD_REMOVE_ACCOUNTS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_ACCTS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_cond_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = cluster_cond;

	req.msg_type = DBD_REMOVE_CLUSTERS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_CLUSTERS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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

extern List acct_storage_p_remove_associations(
	void *db_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond)
{
	slurmdbd_msg_t req;
	dbd_cond_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = assoc_cond;

	req.msg_type = DBD_REMOVE_ASSOCS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_ASSOCS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_cond_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = qos_cond;

	req.msg_type = DBD_REMOVE_QOS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_QOS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_cond_msg_t get_msg;
	int rc;
	slurmdbd_msg_t resp;
	dbd_list_msg_t *got_msg;
	List ret_list = NULL;


	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = wckey_cond;

	req.msg_type = DBD_REMOVE_WCKEYS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_REMOVE_WCKEYS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_LIST) {
		error("slurmdbd: response type not DBD_GOT_LIST: %u",
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
	slurmdbd_msg_t req;
	dbd_rec_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_rec_msg_t));
	get_msg.rec = resv;

	req.msg_type = DBD_REMOVE_RESV;
	req.data = &get_msg;

	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;

	return rc;
}

extern List acct_storage_p_get_users(void *db_conn, uid_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = user_cond;

	req.msg_type = DBD_GET_USERS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_USERS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_USERS) {
		error("slurmdbd: response type not DBD_GOT_USERS: %u",
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = acct_cond;

	req.msg_type = DBD_GET_ACCOUNTS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_ACCOUNTS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_ACCOUNTS) {
		error("slurmdbd: response type not DBD_GOT_ACCOUNTS: %u",
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
					slurmdb_account_cond_t *cluster_cond)
{
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = cluster_cond;

	req.msg_type = DBD_GET_CLUSTERS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_CLUSTERS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_CLUSTERS) {
		error("slurmdbd: response type not DBD_GOT_CLUSTERS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}


	return ret_list;
}

extern List acct_storage_p_get_config(void *db_conn)
{
	slurmdbd_msg_t req, resp;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	req.msg_type = DBD_GET_CONFIG;
	req.data = NULL;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_CONFIG failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_CONFIG) {
		error("slurmdbd: response type not DBD_GOT_CONFIG: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_associations(void *db_conn, uid_t uid,
					    slurmdb_association_cond_t *assoc_cond)
{
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = assoc_cond;

	req.msg_type = DBD_GET_ASSOCS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_ASSOCS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_ASSOCS) {
		error("slurmdbd: response type not DBD_GOT_ASSOCS: %u",
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = event_cond;

	req.msg_type = DBD_GET_EVENTS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_EVENTS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_EVENTS) {
		error("slurmdbd: response type not DBD_GOT_EVENTS: %u",
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
					slurmdb_association_cond_t *assoc_cond)
{
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = assoc_cond;

	req.msg_type = DBD_GET_PROBS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_PROBS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_PROBS) {
		error("slurmdbd: response type not DBD_GOT_PROBS: %u",
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = qos_cond;

	req.msg_type = DBD_GET_QOS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_QOS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_QOS) {
		error("slurmdbd: response type not DBD_GOT_QOS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if(!got_msg->my_list)
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = wckey_cond;

	req.msg_type = DBD_GET_WCKEYS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_WCKEYS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_WCKEYS) {
		error("slurmdbd: response type not DBD_GOT_WCKEYS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if(!got_msg->my_list)
		        ret_list = list_create(NULL);
		else
			ret_list = got_msg->my_list;
		got_msg->my_list = NULL;
		slurmdbd_free_list_msg(got_msg);
	}

	return ret_list;
}

extern List acct_storage_p_get_reservations(
	void *mysql_conn, uid_t uid,
	slurmdb_reservation_cond_t *resv_cond)
{
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = resv_cond;

	req.msg_type = DBD_GET_RESVS;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_RESVS failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_RESVS) {
		error("slurmdbd: response type not DBD_GOT_RESVS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		/* do this just for this type since it could be called
		 * multiple times, and if we send back and empty list
		 * instead of no list we will only call this once.
		 */
		if(!got_msg->my_list)
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List ret_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));
	get_msg.cond = txn_cond;

	req.msg_type = DBD_GET_TXN;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_TXN failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			ret_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_TXN) {
		error("slurmdbd: response type not DBD_GOT_TXN: %u",
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
	slurmdbd_msg_t req, resp;
	dbd_usage_msg_t get_msg;
	dbd_usage_msg_t *got_msg;
	slurmdb_association_rec_t *got_assoc = (slurmdb_association_rec_t *)in;
	slurmdb_wckey_rec_t *got_wckey = (slurmdb_wckey_rec_t *)in;
	slurmdb_cluster_rec_t *got_cluster = (slurmdb_cluster_rec_t *)in;
	List *my_list = NULL;
	int rc;

	memset(&get_msg, 0, sizeof(dbd_usage_msg_t));
	get_msg.rec = in;
	get_msg.start = start;
	get_msg.end = end;
	req.msg_type = type;

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
		error("slurmdbd: Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: %s failure: %m",
		      slurmdbd_msg_type_2_str(type, 1));
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			(*my_list) = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_ASSOC_USAGE
		   && resp.msg_type != DBD_GOT_WCKEY_USAGE
		   && resp.msg_type != DBD_GOT_CLUSTER_USAGE) {
		error("slurmdbd: response type not DBD_GOT_*_USAGE: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_usage_msg_t *) resp.data;
		switch (type) {
		case DBD_GET_ASSOC_USAGE:
			got_assoc = (slurmdb_association_rec_t *)got_msg->rec;
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
			error("slurmdbd: Unknown usage type %d", type);
			rc = SLURM_ERROR;
			break;
		}

		slurmdbd_free_usage_msg(got_msg, resp.msg_type);
	}

	return rc;
}

extern int acct_storage_p_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	slurmdbd_msg_t req;
	dbd_roll_usage_msg_t get_msg;
	int rc, resp_code;

	memset(&get_msg, 0, sizeof(dbd_roll_usage_msg_t));
	get_msg.end = sent_end;
	get_msg.start = sent_start;
	get_msg.archive_data = archive_data;

	req.msg_type = DBD_ROLL_USAGE;

	req.data = &get_msg;

	rc = slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION,
					     &req, &resp_code);

	if(resp_code != SLURM_SUCCESS)
		rc = resp_code;
	else
		info("SUCCESS");
	return rc;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	slurmdbd_msg_t msg;
	dbd_node_state_msg_t req;
	uint16_t cpus;
	char *my_reason;

	if (slurmctld_conf.fast_schedule)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

	memset(&req, 0, sizeof(dbd_node_state_msg_t));
	req.cpu_count = cpus;
	req.hostlist   = node_ptr->name;
	req.new_state  = DBD_NODE_STATE_DOWN;
	req.event_time = event_time;
	req.reason     = my_reason;
	req.reason_uid = reason_uid;
	req.state      = node_ptr->node_state;
	msg.msg_type   = DBD_NODE_STATE;
	msg.data       = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_node_up(void *db_conn,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	slurmdbd_msg_t msg;
	dbd_node_state_msg_t req;

	memset(&req, 0, sizeof(dbd_node_state_msg_t));
	req.hostlist   = node_ptr->name;
	req.new_state  = DBD_NODE_STATE_UP;
	req.event_time = event_time;
	req.reason     = NULL;
	msg.msg_type   = DBD_NODE_STATE;
	msg.data       = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_cpus(void *db_conn,
					      char *cluster_nodes,
					      uint32_t cpus,
					      time_t event_time)
{
	slurmdbd_msg_t msg;
	dbd_cluster_cpus_msg_t req;
	int rc = SLURM_ERROR;

	debug2("Sending cpu count of %d for cluster", cpus);
	memset(&req, 0, sizeof(dbd_cluster_cpus_msg_t));
	req.cluster_nodes = cluster_nodes;
	req.cpu_count   = cpus;
	req.event_time   = event_time;
	msg.msg_type     = DBD_CLUSTER_CPUS;
	msg.data         = &req;

	slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION, &msg, &rc);

	return rc;
}

extern int clusteracct_storage_p_register_ctld(void *db_conn, uint16_t port)
{
	slurmdbd_msg_t msg;
	dbd_register_ctld_msg_t req;
	int rc = SLURM_SUCCESS;

	info("Registering slurmctld at port %u with slurmdbd.", port);
	memset(&req, 0, sizeof(dbd_register_ctld_msg_t));

	req.port         = port;
	req.dimensions   = SYSTEM_DIMENSIONS;
	req.flags        = slurmdb_setup_cluster_flags();
	req.plugin_id_select = select_get_plugin_id();

	msg.msg_type     = DBD_REGISTER_CTLD;
	msg.data         = &req;

	slurm_send_slurmdbd_recv_rc_msg(SLURMDBD_VERSION, &msg, &rc);

	return rc;
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(void *db_conn,
				       struct job_record *job_ptr)
{
	slurmdbd_msg_t msg, msg_rc;
	dbd_job_start_msg_t req;
	dbd_id_rc_msg_t *resp;
	int rc = SLURM_SUCCESS;

	if((rc = _setup_job_start_msg(&req, job_ptr)) != SLURM_SUCCESS)
		return rc;

	msg.msg_type      = DBD_JOB_START;
	msg.data          = &req;

	/* if we already have the db_index don't wait around for it
	 * again just send the message.  This also applies when the
	 * slurmdbd is down and we are about to remove the job from
	 * the system.
	 */
	if((req.db_index && !IS_JOB_RESIZING(job_ptr))
	   || (!req.db_index && IS_JOB_FINISHED(job_ptr))) {
		/* This is to ensure we don't do this multiple times for the
		   same job.  This can happen when an account is being
		   deleted and hense the associations dealing with it.
		*/
		if(!req.db_index)
			job_ptr->db_index = NO_VAL;

		if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0) {
			xfree(req.block_id);
			return SLURM_ERROR;
		}
		xfree(req.block_id);
		return SLURM_SUCCESS;
	}
	/* If we don't have the db_index we need to wait for it to be
	 * used in the other submissions for this job.
	 */
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &msg, &msg_rc);
	if (rc != SLURM_SUCCESS) {
		if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0) {
			xfree(req.block_id);
			return SLURM_ERROR;
		}
	} else if (msg_rc.msg_type != DBD_ID_RC) {
		error("slurmdbd: response type not DBD_ID_RC: %u",
		      msg_rc.msg_type);
	} else {
		resp = (dbd_id_rc_msg_t *) msg_rc.data;
		job_ptr->db_index = resp->id;
		rc = resp->return_code;
		//info("here got %d for return code", resp->return_code);
		slurmdbd_free_id_rc_msg(resp);
	}
	xfree(req.block_id);

	return rc;
}

/*
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(void *db_conn,
					  struct job_record *job_ptr)
{
	slurmdbd_msg_t msg;
	dbd_job_comp_msg_t req;

	if (!job_ptr->db_index
	    && (!job_ptr->details || !job_ptr->details->submit_time)) {
		error("jobacct_storage_p_job_complete: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}

	memset(&req, 0, sizeof(dbd_job_comp_msg_t));

	req.assoc_id    = job_ptr->assoc_id;
	req.db_index    = job_ptr->db_index;
	req.derived_ec  = job_ptr->derived_ec;
	req.exit_code   = job_ptr->exit_code;
	req.job_id      = job_ptr->job_id;
	if (IS_JOB_RESIZING(job_ptr)) {
		req.end_time    = job_ptr->resize_time;
		req.job_state   = JOB_RESIZING;
	} else {
		req.end_time    = job_ptr->end_time;
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

	msg.msg_type    = DBD_JOB_COMPLETE;
	msg.data        = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(void *db_conn,
					struct step_record *step_ptr)
{
	uint32_t cpus = 0, tasks = 0, nodes = 0, task_dist = 0;
	char node_list[BUFFER_SIZE];
	slurmdbd_msg_t msg;
	dbd_step_start_msg_t req;
	char temp_bit[BUF_SIZE];

#ifdef HAVE_BG
	char *ionodes = NULL;

	if(step_ptr->job_ptr->details)
		cpus = step_ptr->job_ptr->details->min_cpus;
	else
		cpus = step_ptr->job_ptr->cpu_cnt;
	select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
			     SELECT_JOBDATA_IONODES,
			     &ionodes);
	if (ionodes) {
		snprintf(node_list, BUFFER_SIZE,
			 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else {
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
	}
	select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
			     SELECT_JOBDATA_NODE_CNT,
			     &nodes);
#else
	if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = tasks = step_ptr->job_ptr->total_cpus;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
		nodes = step_ptr->job_ptr->total_nodes;
	} else {
		cpus = step_ptr->cpu_count;
		tasks = step_ptr->step_layout->task_cnt;
		nodes = step_ptr->step_layout->node_cnt;
		task_dist = step_ptr->step_layout->task_dist;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->step_layout->node_list);
	}
#endif

	if (!step_ptr->job_ptr->db_index
	    && (!step_ptr->job_ptr->details
		|| !step_ptr->job_ptr->details->submit_time)) {
		error("jobacct_storage_p_step_start: "
		      "Not inputing this job, it has no submit time.");
		return SLURM_ERROR;
	}
	memset(&req, 0, sizeof(dbd_step_start_msg_t));

	req.assoc_id    = step_ptr->job_ptr->assoc_id;
	req.db_index    = step_ptr->job_ptr->db_index;
	req.job_id      = step_ptr->job_ptr->job_id;
	req.name        = step_ptr->name;
	req.nodes       = node_list;
	if(step_ptr->step_node_bitmap) {
		req.node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
				       step_ptr->step_node_bitmap);
	}
	req.node_cnt    = nodes;

	if(step_ptr->start_time > step_ptr->job_ptr->resize_time)
		req.start_time = step_ptr->start_time;
	else
		req.start_time = step_ptr->job_ptr->resize_time;

	if (step_ptr->job_ptr->resize_time)
		req.job_submit_time   = step_ptr->job_ptr->resize_time;
	else if (step_ptr->job_ptr->details)
		req.job_submit_time   = step_ptr->job_ptr->details->submit_time;
	req.step_id     = step_ptr->step_id;
	if (step_ptr->step_layout)
		req.task_dist   = step_ptr->step_layout->task_dist;
	req.task_dist   = task_dist;
	req.total_cpus = cpus;
	req.total_tasks = tasks;

	msg.msg_type    = DBD_STEP_START;
	msg.data        = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(void *db_conn,
					   struct step_record *step_ptr)
{
	uint32_t cpus = 0, tasks = 0;
	char node_list[BUFFER_SIZE];
	slurmdbd_msg_t msg;
	dbd_step_comp_msg_t req;

#ifdef HAVE_BG
	char *ionodes = NULL;

	if(step_ptr->job_ptr->details)
		cpus = step_ptr->job_ptr->details->min_cpus;
	else
		cpus = step_ptr->job_ptr->cpu_cnt;
	select_g_select_jobinfo_get(step_ptr->job_ptr->select_jobinfo,
			     SELECT_JOBDATA_IONODES,
			     &ionodes);
	if (ionodes) {
		snprintf(node_list, BUFFER_SIZE,
			 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else {
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
	}

#else
	if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = tasks = step_ptr->job_ptr->total_cpus;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
	} else {
		cpus = step_ptr->cpu_count;
		tasks = step_ptr->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->step_layout->node_list);
	}
#endif

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
	req.jobacct     = step_ptr->jobacct;
	req.job_id      = step_ptr->job_ptr->job_id;
	req.req_uid     = step_ptr->requid;
	if(step_ptr->start_time > step_ptr->job_ptr->resize_time)
		req.start_time = step_ptr->start_time;
	else
		req.start_time = step_ptr->job_ptr->resize_time;

	if (step_ptr->job_ptr->resize_time)
		req.job_submit_time   = step_ptr->job_ptr->resize_time;
	else if (step_ptr->job_ptr->details)
		req.job_submit_time   = step_ptr->job_ptr->details->submit_time;
	req.step_id     = step_ptr->step_id;
	req.total_cpus = cpus;
	req.total_tasks = tasks;

	msg.msg_type    = DBD_STEP_COMPLETE;
	msg.data        = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(void *db_conn,
				     struct job_record *job_ptr)
{
	slurmdbd_msg_t msg;
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
	msg.data         = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t get_msg;
	dbd_list_msg_t *got_msg;
	int rc;
	List my_job_list = NULL;

	memset(&get_msg, 0, sizeof(dbd_cond_msg_t));

	get_msg.cond = job_cond;

	req.msg_type = DBD_GET_JOBS_COND;
	req.data = &get_msg;
	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_GET_JOBS_COND failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		if(msg->return_code == SLURM_SUCCESS) {
			info("%s", msg->comment);
			my_job_list = list_create(NULL);
		} else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else if (resp.msg_type != DBD_GOT_JOBS) {
		error("slurmdbd: response type not DBD_GOT_JOBS: %u",
		      resp.msg_type);
	} else {
		got_msg = (dbd_list_msg_t *) resp.data;
		my_job_list = got_msg->my_list;
		got_msg->my_list = NULL;
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
	slurmdbd_msg_t req, resp;
	dbd_cond_msg_t msg;
	int rc = SLURM_SUCCESS;

	memset(&msg, 0, sizeof(dbd_cond_msg_t));

	msg.cond     = arch_cond;

	req.msg_type = DBD_ARCHIVE_DUMP;
	req.data     = &msg;

	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_ARCHIVE_DUMP failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		rc = msg->return_code;

		if(msg->return_code == SLURM_SUCCESS)
			info("%s", msg->comment);
		else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
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
	slurmdbd_msg_t req, resp;
	int rc = SLURM_SUCCESS;

	req.msg_type = DBD_ARCHIVE_LOAD;
	req.data     = arch_rec;

	rc = slurm_send_recv_slurmdbd_msg(SLURMDBD_VERSION, &req, &resp);

	if (rc != SLURM_SUCCESS)
		error("slurmdbd: DBD_ARCHIVE_LOAD failure: %m");
	else if (resp.msg_type == DBD_RC) {
		dbd_rc_msg_t *msg = resp.data;
		rc = msg->return_code;

		if(msg->return_code == SLURM_SUCCESS)
			info("%s", msg->comment);
		else {
			slurm_seterrno(msg->return_code);
			error("%s", msg->comment);
		}
		slurmdbd_free_rc_msg(msg);
	} else {
		error("unknown return for archive_load");
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
	slurmdbd_msg_t msg;
	dbd_cluster_cpus_msg_t req;

	info("Ending any jobs in accounting that were running when controller "
	     "went down on");

	memset(&req, 0, sizeof(dbd_cluster_cpus_msg_t));

	req.cpu_count   = 0;
	req.event_time   = event_time;

	msg.msg_type     = DBD_FLUSH_JOBS;
	msg.data         = &req;

	if (slurm_send_slurmdbd_msg(SLURMDBD_VERSION, &msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}
