/*****************************************************************************\
 *  proc_req.c - functions for processing incoming RPCs.
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/jobacct_common.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmdbd/proc_req.h"
#include "src/slurmctld/slurmctld.h"

/* Local functions */
static int   _add_accounts(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_assocs(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_clusters(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_qos(slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_users(slurmdbd_conn_t *slurmdbd_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _add_reservation(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _archive_dump(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _archive_load(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _cluster_procs(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_accounts(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_assocs(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_clusters(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_config(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_jobs(slurmdbd_conn_t *slurmdbd_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_jobs_cond(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_probs(slurmdbd_conn_t *slurmdbd_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_qos(slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_txn(slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_usage(uint16_t type, slurmdbd_conn_t *slurmdbd_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_users(slurmdbd_conn_t *slurmdbd_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_reservations(slurmdbd_conn_t *slurmdbd_conn,
			       Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _flush_jobs(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _init_conn(slurmdbd_conn_t *slurmdbd_conn, 
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _fini_conn(slurmdbd_conn_t *slurmdbd_conn, Buf in_buffer,
			Buf *out_buffer);
static int   _job_complete(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_start(slurmdbd_conn_t *slurmdbd_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_suspend(slurmdbd_conn_t *slurmdbd_conn,
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_qos(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_users(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _modify_reservation(slurmdbd_conn_t *slurmdbd_conn,
				 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _node_state(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static char *_node_state_string(uint16_t node_state);
static int   _register_ctld(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				    Buf in_buffer, Buf *out_buffer,
				    uint32_t *uid);
static int   _remove_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_qos(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_users(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _remove_reservation(slurmdbd_conn_t *slurmdbd_conn,
				 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _roll_usage(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _step_complete(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _step_start(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);

/* Process an incoming RPC
 * slurmdbd_conn IN/OUT - in will that the newsockfd set before
 *       calling and db_conn and rpc_version will be filled in with the init.
 * msg IN - incoming message
 * msg_size IN - size of msg in bytes
 * first IN - set if first message received on the socket
 * buffer OUT - outgoing response, must be freed by caller
 * uid IN/OUT - user ID who initiated the RPC
 * RET SLURM_SUCCESS or error code */
extern int 
proc_req(slurmdbd_conn_t *slurmdbd_conn, 
	 char *msg, uint32_t msg_size,
	 bool first, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	uint16_t msg_type;
	Buf in_buffer;
	char *comment = NULL;

	in_buffer = create_buf(msg, msg_size); /* puts msg into buffer struct */
	safe_unpack16(&msg_type, in_buffer);

	if (first && (msg_type != DBD_INIT)) {
		comment = "Initial RPC not DBD_INIT";
		error("%s type (%d)", comment, msg_type);
		rc = EINVAL;
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_INIT);
	} else {
		switch (msg_type) {
		case DBD_ADD_ACCOUNTS:
			rc = _add_accounts(slurmdbd_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_ACCOUNT_COORDS:
			rc = _add_account_coords(slurmdbd_conn,
						 in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_ASSOCS:
			rc = _add_assocs(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_CLUSTERS:
			rc = _add_clusters(slurmdbd_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_QOS:
			rc = _add_qos(slurmdbd_conn,
				      in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_USERS:
			rc = _add_users(slurmdbd_conn, 
					in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_WCKEYS:
			rc = _add_wckeys(slurmdbd_conn, 
					 in_buffer, out_buffer, uid);
			break;
		case DBD_ADD_RESV:
			rc = _add_reservation(slurmdbd_conn,
					      in_buffer, out_buffer, uid);
			break;
		case DBD_ARCHIVE_DUMP:
			rc = _archive_dump(slurmdbd_conn, 
					   in_buffer, out_buffer, uid);
			break;
		case DBD_ARCHIVE_LOAD:
			rc = _archive_load(slurmdbd_conn, 
					   in_buffer, out_buffer, uid);
			break;
		case DBD_CLUSTER_PROCS:
			rc = _cluster_procs(slurmdbd_conn,
					    in_buffer, out_buffer, uid);
			break;
		case DBD_GET_ACCOUNTS:
			rc = _get_accounts(slurmdbd_conn, 
					   in_buffer, out_buffer, uid);
			break;
		case DBD_GET_ASSOCS:
			rc = _get_assocs(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_GET_ASSOC_USAGE:
		case DBD_GET_CLUSTER_USAGE:
			rc = _get_usage(msg_type, slurmdbd_conn,
					in_buffer, out_buffer, uid);
			break;
		case DBD_GET_CLUSTERS:
			rc = _get_clusters(slurmdbd_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_GET_CONFIG:
			rc = _get_config(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_GET_JOBS:
			rc = _get_jobs(slurmdbd_conn,
				       in_buffer, out_buffer, uid);
			break;
		case DBD_GET_JOBS_COND:
			rc = _get_jobs_cond(slurmdbd_conn, 
					    in_buffer, out_buffer, uid);
			break;
		case DBD_GET_PROBS:
			rc = _get_probs(slurmdbd_conn,
					in_buffer, out_buffer, uid);
			break;
		case DBD_GET_QOS:
			rc = _get_qos(slurmdbd_conn,
				      in_buffer, out_buffer, uid);
			break;
		case DBD_GET_TXN:
			rc = _get_txn(slurmdbd_conn,
				      in_buffer, out_buffer, uid);
			break;
		case DBD_GET_WCKEYS:
			rc = _get_wckeys(slurmdbd_conn, 
					 in_buffer, out_buffer, uid);
			break;
		case DBD_GET_RESVS:
			rc = _get_reservations(slurmdbd_conn, 
					       in_buffer, out_buffer, uid);
			break;
		case DBD_GET_USERS:
			rc = _get_users(slurmdbd_conn,
					in_buffer, out_buffer, uid);
			break;
		case DBD_FLUSH_JOBS:
			rc = _flush_jobs(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_INIT:
			if (first)
				rc = _init_conn(slurmdbd_conn, 
						in_buffer, out_buffer, uid);
			else {
				comment = "DBD_INIT sent after connection established";
				error("%s", comment);
				rc = EINVAL;
				*out_buffer = make_dbd_rc_msg(
					slurmdbd_conn->rpc_version, rc, comment,
					DBD_INIT);
			}
			break;
		case DBD_FINI:
			rc = _fini_conn(slurmdbd_conn, in_buffer, out_buffer);
			break;
		case DBD_JOB_COMPLETE:
			rc = _job_complete(slurmdbd_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_JOB_START:
			rc = _job_start(slurmdbd_conn,
					in_buffer, out_buffer, uid);
			break;
		case DBD_JOB_SUSPEND:
			rc = _job_suspend(slurmdbd_conn,
					  in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_ACCOUNTS:
			rc = _modify_accounts(slurmdbd_conn,
					      in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_ASSOCS:
			rc = _modify_assocs(slurmdbd_conn,
					    in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_CLUSTERS:
			rc = _modify_clusters(slurmdbd_conn,
					      in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_QOS:
			rc = _modify_qos(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_USERS:
			rc = _modify_users(slurmdbd_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_WCKEYS:
			rc = _modify_wckeys(slurmdbd_conn, 
					    in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_RESV:
			rc = _modify_reservation(slurmdbd_conn,
						 in_buffer, out_buffer, uid);
			break;
		case DBD_NODE_STATE:
			rc = _node_state(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_REGISTER_CTLD:
			rc = _register_ctld(slurmdbd_conn, in_buffer, 
					    out_buffer, uid);
			break;
		case DBD_REMOVE_ACCOUNTS:
			rc = _remove_accounts(slurmdbd_conn,
					      in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_ACCOUNT_COORDS:
			rc = _remove_account_coords(slurmdbd_conn,
						    in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_ASSOCS:
			rc = _remove_assocs(slurmdbd_conn,
					    in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_CLUSTERS:
			rc = _remove_clusters(slurmdbd_conn,
					      in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_QOS:
			rc = _remove_qos(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_USERS:
			rc = _remove_users(slurmdbd_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_WCKEYS:
			rc = _remove_wckeys(slurmdbd_conn, 
					    in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_RESV:
			rc = _remove_reservation(slurmdbd_conn,
						 in_buffer, out_buffer, uid);
			break;
		case DBD_ROLL_USAGE:
			rc = _roll_usage(slurmdbd_conn, 
					 in_buffer, out_buffer, uid);
			break;
		case DBD_STEP_COMPLETE:
			rc = _step_complete(slurmdbd_conn,
					    in_buffer, out_buffer, uid);
			break;
		case DBD_STEP_START:
			rc = _step_start(slurmdbd_conn,
					 in_buffer, out_buffer, uid);
			break;
		default:
			comment = "Invalid RPC";
			error("%s msg_type=%d", comment, msg_type);
			rc = EINVAL;
			*out_buffer = make_dbd_rc_msg(
				slurmdbd_conn->rpc_version, rc, comment, 0);
			break;
		}

		if(rc == ESLURM_ACCESS_DENIED) 
			error("Security violation, %s",
			      slurmdbd_msg_type_2_str(msg_type, 1));
	}

	xfer_buf_data(in_buffer);	/* delete in_buffer struct without 
					 * xfree of msg */
	return rc;

unpack_error:
	free_buf(in_buffer);
	return SLURM_ERROR;
}

/* replace \" with \` return is the same as what is given */
static char * _replace_double_quotes(char *option)
{
	int i=0;

	if(!option)
		return NULL;

	while(option[i]) {
		if(option[i] == '\"')
			option[i] = '`';
		i++;
	}
	return option;
}


static int _add_accounts(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = NULL;
	char *comment = NULL;

	debug2("DBD_ADD_ACCOUNTS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_OPERATOR) {
		acct_user_rec_t user;

		memset(&user, 0, sizeof(acct_user_rec_t));
		user.uid = *uid;
		if(assoc_mgr_fill_in_user(
			   slurmdbd_conn->db_conn, &user, 1, NULL)
		   != SLURM_SUCCESS) {
			comment = "Your user has not been added to the accounting system yet.";
			error("%s", comment);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if(!user.coord_accts || !list_count(user.coord_accts)) {
			comment = "Your user doesn't have privilege to preform this action";
			error("%s", comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;			
		}
		/* If the user is a coord of any acct they can add
		 * accounts they are only able to make associations to
		 * these accounts if they are coordinators of the
		 * parent they are trying to add to
		 */		
	}

	if (slurmdbd_unpack_list_msg(slurmdbd_conn->rpc_version, 
				     DBD_ADD_ACCOUNTS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_ACCOUNTS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	rc = acct_storage_g_add_accounts(slurmdbd_conn->db_conn, *uid,
					 get_msg->my_list);
end_it:
	slurmdbd_free_list_msg(slurmdbd_conn->rpc_version, 
			       get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_ACCOUNTS);
	return rc;
}
static int _add_account_coords(slurmdbd_conn_t *slurmdbd_conn,
			       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_acct_coord_msg_t *get_msg = NULL;
	char *comment = NULL;
	
	if (slurmdbd_unpack_acct_coord_msg(slurmdbd_conn->rpc_version, 
					   &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_ACCOUNT_COORDS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	debug2("DBD_ADD_ACCOUNT_COORDS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid)
	   < ACCT_ADMIN_OPERATOR) {
		ListIterator itr = NULL;
		ListIterator itr2 = NULL;
		acct_user_rec_t user;
		acct_coord_rec_t *coord = NULL;
		char *acct = NULL;
		int bad = 0;

		memset(&user, 0, sizeof(acct_user_rec_t));
		user.uid = *uid;
		if(assoc_mgr_fill_in_user(
			   slurmdbd_conn->db_conn, &user, 1, NULL) 
		   != SLURM_SUCCESS) {
			comment = "Your user has not been added to the accounting system yet.";
			error("%s", comment);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if(!user.coord_accts || !list_count(user.coord_accts)) {
			comment = "Your user doesn't have privilege to preform this action";
			error("%s", comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;
		}
		itr = list_iterator_create(get_msg->acct_list);
		itr2 = list_iterator_create(user.coord_accts);
		while((acct = list_next(itr))) {
			while((coord = list_next(itr2))) {
				if(!strcasecmp(coord->name, acct))
					break;
			}
			if(!coord)  {
				bad = 1;
				break;
			}
			list_iterator_reset(itr2);
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
		
		if(bad)  {
			comment = "Your user doesn't have privilege to preform this action";
			error("%s", comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;
		}
	}

	rc = acct_storage_g_add_coord(slurmdbd_conn->db_conn, *uid, 
				      get_msg->acct_list, get_msg->cond);
end_it:
	slurmdbd_free_acct_coord_msg(slurmdbd_conn->rpc_version, 
				     get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_ACCOUNT_COORDS);
	return rc;
}

static int _add_assocs(slurmdbd_conn_t *slurmdbd_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = NULL;
	char *comment = NULL;

	debug2("DBD_ADD_ASSOCS: called");

	if (slurmdbd_unpack_list_msg(slurmdbd_conn->rpc_version, 
				     DBD_ADD_ASSOCS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_ASSOCS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_OPERATOR) {
		ListIterator itr = NULL;
		ListIterator itr2 = NULL;
		acct_user_rec_t user;
		acct_coord_rec_t *coord = NULL;
		acct_association_rec_t *object = NULL;

		memset(&user, 0, sizeof(acct_user_rec_t));
		user.uid = *uid;
		if(assoc_mgr_fill_in_user(
			   slurmdbd_conn->db_conn, &user, 1, NULL)
		   != SLURM_SUCCESS) {
			comment = "Your user has not been added to the accounting system yet.";
			error("%s", comment);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if(!user.coord_accts || !list_count(user.coord_accts)) {
			comment = "Your user doesn't have privilege to preform this action";
			error("%s", comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;
		}
		itr = list_iterator_create(get_msg->my_list);
		itr2 = list_iterator_create(user.coord_accts);
		while((object = list_next(itr))) {
			char *account = "root";
			if(object->user)
				account = object->acct;
			else if(object->parent_acct)
				account = object->parent_acct;
			list_iterator_reset(itr2);
			while((coord = list_next(itr2))) {
				if(!strcasecmp(coord->name, account))
					break;
			}
			if(!coord) 
				break;
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
		if(!coord)  {
			comment = "Your user doesn't have privilege to preform this action";
			error("%s", comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;
		}
	}

	rc = acct_storage_g_add_associations(slurmdbd_conn->db_conn, *uid,
					     get_msg->my_list);
end_it:
	slurmdbd_free_list_msg(slurmdbd_conn->rpc_version, 
			       get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_ASSOCS);
	return rc;
}

static int _add_clusters(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = NULL;
	char *comment = NULL;

	debug2("DBD_ADD_CLUSTERS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	if (slurmdbd_unpack_list_msg(slurmdbd_conn->rpc_version, 
				     DBD_ADD_CLUSTERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_CLUSTERS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	rc = acct_storage_g_add_clusters(slurmdbd_conn->db_conn, *uid, 
					 get_msg->my_list);
	if(rc != SLURM_SUCCESS) 
		comment = "Failed to add cluster.";

end_it:
	slurmdbd_free_list_msg(slurmdbd_conn->rpc_version, 
			       get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_CLUSTERS);
	return rc;
}

static int _add_qos(slurmdbd_conn_t *slurmdbd_conn,
		    Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = NULL;
	char *comment = NULL;

	debug2("DBD_ADD_QOS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && (assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	       < ACCT_ADMIN_SUPER_USER)) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	if (slurmdbd_unpack_list_msg(slurmdbd_conn->rpc_version, 
				     DBD_ADD_QOS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_QOS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	rc = acct_storage_g_add_qos(slurmdbd_conn->db_conn, *uid,
				    get_msg->my_list);
	if(rc != SLURM_SUCCESS) 
		comment = "Failed to add qos.";

end_it:
	slurmdbd_free_list_msg(slurmdbd_conn->rpc_version, 
			       get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_QOS);
	return rc;
}

static int _add_users(slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = NULL;
	char *comment = NULL;
	debug2("DBD_ADD_USERS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_OPERATOR) {
		acct_user_rec_t user;

		memset(&user, 0, sizeof(acct_user_rec_t));
		user.uid = *uid;
		if(assoc_mgr_fill_in_user(
			   slurmdbd_conn->db_conn, &user, 1, NULL) 
		   != SLURM_SUCCESS) {
			comment = "Your user has not been added to the accounting system yet.";
			error("%s", comment);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if(!user.coord_accts || !list_count(user.coord_accts)) {
			comment = "Your user doesn't have privilege to preform this action";
			error("%s", comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;			
		}
		/* If the user is a coord of any acct they can add
		 * users they are only able to make associations to
		 * these users if they are coordinators of the
		 * account they are trying to add to
		 */		
	}

	if (slurmdbd_unpack_list_msg(slurmdbd_conn->rpc_version, 
				     DBD_ADD_USERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_USERS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	rc = acct_storage_g_add_users(slurmdbd_conn->db_conn, *uid, 
				      get_msg->my_list);

end_it:
	slurmdbd_free_list_msg(slurmdbd_conn->rpc_version, 
			       get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_USERS);
	return rc;
}

static int _add_wckeys(slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = NULL;
	char *comment = NULL;

	debug2("DBD_ADD_WCKEYS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;			
	}

	if (slurmdbd_unpack_list_msg(slurmdbd_conn->rpc_version, 
				     DBD_ADD_WCKEYS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_WCKEYS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	rc = acct_storage_g_add_wckeys(slurmdbd_conn->db_conn, *uid, 
				      get_msg->my_list);

end_it:
	slurmdbd_free_list_msg(slurmdbd_conn->rpc_version, 
			       get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_WCKEYS);
	return rc;
}

static int _add_reservation(slurmdbd_conn_t *slurmdbd_conn,
			     Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = NULL;
	char *comment = NULL;

	if ((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)) {
		comment = "DBD_ADD_RESV message from invalid uid";
		error("DBD_ADD_RESV message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_rec_msg(slurmdbd_conn->rpc_version, DBD_ADD_RESV,
				    &rec_msg, in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ADD_RESV message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	debug2("DBD_ADD_RESV: called");

	rc = acct_storage_g_add_reservation(slurmdbd_conn->db_conn,
					     rec_msg->rec);

end_it:
	slurmdbd_free_rec_msg(slurmdbd_conn->rpc_version,
			      DBD_ADD_RESV, rec_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ADD_RESV);
	return rc;
}

static int _archive_dump(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	char *comment = "SUCCESS";
	acct_archive_cond_t *arch_cond = NULL;

	debug2("DBD_ARCHIVE_DUMP: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;			
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_ARCHIVE_DUMP, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ARCHIVE_DUMP message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	arch_cond = (acct_archive_cond_t *)get_msg->cond;
	/* set up some defaults */
	if(!arch_cond->archive_dir)
		arch_cond->archive_dir = xstrdup(slurmdbd_conf->archive_dir);
	if(arch_cond->archive_events == (uint16_t)NO_VAL)
		arch_cond->archive_events = slurmdbd_conf->archive_events;
	if(arch_cond->archive_jobs == (uint16_t)NO_VAL)
		arch_cond->archive_jobs = slurmdbd_conf->archive_jobs;
	if(!arch_cond->archive_script)
		arch_cond->archive_script = 
			xstrdup(slurmdbd_conf->archive_script);
	if(arch_cond->archive_steps == (uint16_t)NO_VAL)
		arch_cond->archive_steps = slurmdbd_conf->archive_steps;
	if(arch_cond->archive_suspend == (uint16_t)NO_VAL)
		arch_cond->archive_suspend = slurmdbd_conf->archive_suspend;
	if(arch_cond->purge_event == (uint16_t)NO_VAL)
		arch_cond->purge_event = slurmdbd_conf->purge_event;
	if(arch_cond->purge_job == (uint16_t)NO_VAL)
		arch_cond->purge_job = slurmdbd_conf->purge_job;
	if(arch_cond->purge_step == (uint16_t)NO_VAL)
		arch_cond->purge_step = slurmdbd_conf->purge_step;
	if(arch_cond->purge_suspend == (uint16_t)NO_VAL)
		arch_cond->purge_suspend = slurmdbd_conf->purge_suspend;

	rc = jobacct_storage_g_archive(slurmdbd_conn->db_conn, arch_cond);
	if(rc != SLURM_SUCCESS) {
		if(errno == EACCES) 
			comment = "Problem accessing file.";
		else
			comment = "Error with request.";
	}
end_it:
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_ARCHIVE_DUMP, get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ARCHIVE_DUMP);
	return rc;
}

static int _archive_load(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	acct_archive_rec_t *arch_rec = NULL;
	char *comment = "SUCCESS";

	debug2("DBD_ARCHIVE_LOAD: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;			
	}

	if (unpack_acct_archive_rec((void *)&arch_rec, 
				    slurmdbd_conn->rpc_version, 
				    in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ARCHIVE_LOAD message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	rc = jobacct_storage_g_archive_load(slurmdbd_conn->db_conn, arch_rec);

	if(rc == ENOENT) 
		comment = "No archive file given to recover.";
	else if(rc != SLURM_SUCCESS)
		comment = "Error with request.";
	
end_it:
	destroy_acct_archive_rec(arch_rec);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ARCHIVE_LOAD);
	return rc;
}

static int _cluster_procs(slurmdbd_conn_t *slurmdbd_conn,
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cluster_procs_msg_t *cluster_procs_msg = NULL;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if ((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)) {
		comment = "DBD_CLUSTER_PROCS message from invalid uid";
		error("DBD_CLUSTER_PROCS message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_cluster_procs_msg(slurmdbd_conn->rpc_version, 
					      &cluster_procs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_CLUSTER_PROCS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	debug2("DBD_CLUSTER_PROCS: called for %s(%u)",
	       cluster_procs_msg->cluster_name,
	       cluster_procs_msg->proc_count);

	rc = clusteracct_storage_g_cluster_procs(
		slurmdbd_conn->db_conn,
		cluster_procs_msg->cluster_name,
		cluster_procs_msg->cluster_nodes,
		cluster_procs_msg->proc_count,
		cluster_procs_msg->event_time);
end_it:
	slurmdbd_free_cluster_procs_msg(slurmdbd_conn->rpc_version, 
					cluster_procs_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_CLUSTER_PROCS);
	return rc;
}

static int _get_accounts(slurmdbd_conn_t *slurmdbd_conn, 
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_ACCOUNTS: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_ACCOUNTS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_ACCOUNTS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_ACCOUNTS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_accounts(slurmdbd_conn->db_conn,
						       *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_ACCOUNTS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_ACCOUNTS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_ACCOUNTS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int _get_assocs(slurmdbd_conn_t *slurmdbd_conn, 
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_ASSOCS: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_ASSOCS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_ASSOCS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_ASSOCS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_associations(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_ASSOCS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_ASSOCS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_ASSOCS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_clusters(slurmdbd_conn_t *slurmdbd_conn, 
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_CLUSTERS: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_CLUSTERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_CLUSTERS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_CLUSTERS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_clusters(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_CLUSTERS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_CLUSTERS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_CLUSTERS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_config(slurmdbd_conn_t *slurmdbd_conn, 
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };

	debug2("DBD_GET_CONFIG: called");
	/* No message body to unpack */

	list_msg.my_list = dump_config();
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_CONFIG, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_CONFIG, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int _get_jobs(slurmdbd_conn_t *slurmdbd_conn, 
		     Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_get_jobs_msg_t *get_jobs_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;
	acct_job_cond_t job_cond;

	debug2("DBD_GET_JOBS: called");
	if (slurmdbd_unpack_get_jobs_msg(slurmdbd_conn->rpc_version, 
					 &get_jobs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_JOBS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment, 
					      DBD_GET_JOBS);
		return SLURM_ERROR;
	}
	
	memset(&job_cond, 0, sizeof(acct_job_cond_t));

	job_cond.acct_list = get_jobs_msg->selected_steps;
	job_cond.step_list = get_jobs_msg->selected_steps;
	job_cond.partition_list = get_jobs_msg->selected_parts;

	if (get_jobs_msg->user) {
		uid_t pw_uid;
		if (uid_from_string (get_jobs_msg->user, &pw_uid) >= 0) {
			char *temp = xstrdup_printf("%u", pw_uid);
			job_cond.userid_list = list_create(slurm_destroy_char);
			list_append(job_cond.userid_list, temp);
		}	
	}

	if (get_jobs_msg->gid >=0) {
		char *temp = xstrdup_printf("%u", get_jobs_msg->gid);
		job_cond.groupid_list = list_create(slurm_destroy_char);
		list_append(job_cond.groupid_list, temp);
	}	

	if (get_jobs_msg->cluster_name) {
		job_cond.cluster_list = list_create(NULL);
		list_append(job_cond.cluster_list, get_jobs_msg->cluster_name);
	}		
		
	list_msg.my_list = jobacct_storage_g_get_jobs_cond(
		slurmdbd_conn->db_conn, *uid, &job_cond);
	slurmdbd_free_get_jobs_msg(slurmdbd_conn->rpc_version, 
				   get_jobs_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	if(job_cond.cluster_list)
		list_destroy(job_cond.cluster_list);
	if(job_cond.userid_list)
		list_destroy(job_cond.userid_list);
	if(job_cond.groupid_list)
		list_destroy(job_cond.groupid_list);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_JOBS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_JOBS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_jobs_cond(slurmdbd_conn_t *slurmdbd_conn, 
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *cond_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_JOBS_COND: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_JOBS_COND, &cond_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_JOBS_COND message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment, 
					      DBD_GET_JOBS_COND);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = jobacct_storage_g_get_jobs_cond(
		slurmdbd_conn->db_conn, *uid, cond_msg->cond);

	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_JOBS_COND, cond_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_JOBS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_JOBS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_probs(slurmdbd_conn_t *slurmdbd_conn, 
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_PROBS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid)
	   < ACCT_ADMIN_OPERATOR) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_GET_PROBS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_PROBS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_PROBS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_PROBS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_problems(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_PROBS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_PROBS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_PROBS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_qos(slurmdbd_conn_t *slurmdbd_conn, 
		    Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *cond_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_QOS: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_QOS, &cond_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_QOS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment, 
					      DBD_GET_QOS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_qos(slurmdbd_conn->db_conn, *uid,
						  cond_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_QOS, cond_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_QOS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_QOS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_txn(slurmdbd_conn_t *slurmdbd_conn, 
		    Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *cond_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_TXN: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_TXN, &cond_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_TXN message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment, 
					      DBD_GET_TXN);
		return SLURM_ERROR;
	}

	list_msg.my_list = acct_storage_g_get_txn(slurmdbd_conn->db_conn, *uid,
						  cond_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_TXN, cond_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_TXN, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_TXN, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_usage(uint16_t type, slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_usage_msg_t *get_msg = NULL;
	dbd_usage_msg_t got_msg;
	uint16_t ret_type = 0;
	int (*my_function) (void *db_conn, uid_t uid, void *object, int type,
			    time_t start, time_t end);
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	info("DBD_GET_USAGE: called type is %s",
	     slurmdbd_msg_type_2_str(type, 1));

	if (slurmdbd_unpack_usage_msg(slurmdbd_conn->rpc_version, 
				      type, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_USAGE message"; 
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment, type);
		return SLURM_ERROR;
	}
	switch(type) {
	case DBD_GET_ASSOC_USAGE:
		ret_type = DBD_GOT_ASSOC_USAGE;
		my_function = acct_storage_g_get_usage;
		break;
	case DBD_GET_WCKEY_USAGE:
		ret_type = DBD_GOT_WCKEY_USAGE;
		my_function = acct_storage_g_get_usage;
		break;
	case DBD_GET_CLUSTER_USAGE:
		ret_type = DBD_GOT_CLUSTER_USAGE;
		my_function = clusteracct_storage_g_get_usage;
		break;
	default:
		comment = "Unknown type of usage to get";
		error("%s %u", comment, type);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment, type);
		return SLURM_ERROR;
	}		

	rc = (*(my_function))(slurmdbd_conn->db_conn, *uid, get_msg->rec, type,
			      get_msg->start, get_msg->end);
	slurmdbd_free_usage_msg(slurmdbd_conn->rpc_version, 
				type, get_msg);

	if(rc != SLURM_SUCCESS) {
		comment = "Problem getting usage info";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, type);
		return rc;
		
	}
	memset(&got_msg, 0, sizeof(dbd_usage_msg_t));
	got_msg.rec = get_msg->rec;
	get_msg->rec = NULL;
	*out_buffer = init_buf(1024);
	pack16((uint16_t) ret_type, *out_buffer);
	slurmdbd_pack_usage_msg(slurmdbd_conn->rpc_version, 
				ret_type, &got_msg, *out_buffer);
	
	return SLURM_SUCCESS;
}

static int _get_users(slurmdbd_conn_t *slurmdbd_conn, 
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_USERS: called");

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_USERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_USERS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_USERS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_users(slurmdbd_conn->db_conn,
						    *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_USERS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_USERS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_USERS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_wckeys(slurmdbd_conn_t *slurmdbd_conn, 
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_WCKEYS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED, comment,
					      DBD_GET_WCKEYS);
		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_WCKEYS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_WCKEYS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_WCKEYS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_wckeys(slurmdbd_conn->db_conn,
						     *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_WCKEYS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_WCKEYS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_WCKEYS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _get_reservations(slurmdbd_conn_t *slurmdbd_conn, 
			     Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_GET_RESVS: called");

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_GET_RESVS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_GET_RESVS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_GET_RESVS);
		return SLURM_ERROR;
	}
	
	list_msg.my_list = acct_storage_g_get_reservations(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_GET_RESVS, get_msg);

	if(errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_RESVS, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_RESVS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	
	return SLURM_SUCCESS;
}

static int _flush_jobs(slurmdbd_conn_t *slurmdbd_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cluster_procs_msg_t *cluster_procs_msg = NULL;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if ((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)) {
		comment = "DBD_FLUSH_JOBS message from invalid uid";
		error("DBD_FLUSH_JOBS message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_cluster_procs_msg(slurmdbd_conn->rpc_version, 
					      &cluster_procs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_FLUSH_JOBS message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	debug2("DBD_FLUSH_JOBS: called for %s",
	       cluster_procs_msg->cluster_name);

	rc = acct_storage_g_flush_jobs_on_cluster(
		slurmdbd_conn->db_conn,
		cluster_procs_msg->cluster_name,
		cluster_procs_msg->event_time);
end_it:
	slurmdbd_free_cluster_procs_msg(slurmdbd_conn->rpc_version, 
					cluster_procs_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_FLUSH_JOBS);
	return rc;
}

static int _init_conn(slurmdbd_conn_t *slurmdbd_conn,
		      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_init_msg_t *init_msg = NULL;
	char *comment = NULL;
	int rc = SLURM_SUCCESS;

	if ((rc = slurmdbd_unpack_init_msg(slurmdbd_conn->rpc_version, 
					   &init_msg, in_buffer, 
					   slurmdbd_conf->auth_info))
	    != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_INIT message";
		error("%s", comment);
		goto end_it;
	}
	if ((init_msg->version < SLURMDBD_VERSION_MIN) ||
	    (init_msg->version > SLURMDBD_VERSION)) {
		comment = "Incompatable RPC version";
		error("Incompatable RPC version received "
		      "(%u not between %d and %d)",
		      init_msg->version, 
		      SLURMDBD_VERSION_MIN, SLURMDBD_VERSION);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto end_it;
	}
	*uid = init_msg->uid;
	
	debug("DBD_INIT: VERSION:%u UID:%u IP:%s CONN:%u",
	      init_msg->version, init_msg->uid, 
	      slurmdbd_conn->ip, slurmdbd_conn->newsockfd);
	slurmdbd_conn->db_conn = acct_storage_g_get_connection(
		false, slurmdbd_conn->newsockfd, init_msg->rollback);
	slurmdbd_conn->rpc_version = init_msg->version;

end_it:
	slurmdbd_free_init_msg(slurmdbd_conn->rpc_version, 
			       init_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_INIT);

	return rc;
}

static int   _fini_conn(slurmdbd_conn_t *slurmdbd_conn, Buf in_buffer,
			Buf *out_buffer)
{
	dbd_fini_msg_t *fini_msg = NULL;
	char *comment = NULL;
	int rc = SLURM_SUCCESS;

	if (slurmdbd_unpack_fini_msg(slurmdbd_conn->rpc_version, 
				     &fini_msg, in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_FINI message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	
	debug2("DBD_FINI: CLOSE:%u COMMIT:%u",
	       fini_msg->close_conn, fini_msg->commit);
	if(fini_msg->close_conn == 1)
		rc = acct_storage_g_close_connection(&slurmdbd_conn->db_conn);
	else
		rc = acct_storage_g_commit(slurmdbd_conn->db_conn,
					   fini_msg->commit);
end_it:
	slurmdbd_free_fini_msg(slurmdbd_conn->rpc_version, 
			       fini_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_FINI);

	return rc;

}

static int  _job_complete(slurmdbd_conn_t *slurmdbd_conn,
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_comp_msg_t *job_comp_msg = NULL;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_JOB_COMPLETE message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_job_complete_msg(slurmdbd_conn->rpc_version, 
					     &job_comp_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_JOB_COMPLETE message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}

	debug2("DBD_JOB_COMPLETE: ID:%u ", job_comp_msg->job_id);

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = job_comp_msg->assoc_id;
	job.db_index = job_comp_msg->db_index;
	job.end_time = job_comp_msg->end_time;
	job.exit_code = job_comp_msg->exit_code;
	job.job_id = job_comp_msg->job_id;
	job.job_state = job_comp_msg->job_state;
	job.requid = job_comp_msg->req_uid;
	job.nodes = job_comp_msg->nodes;
	job.start_time = job_comp_msg->start_time;
	details.submit_time = job_comp_msg->submit_time;

	job.details = &details;
	rc = jobacct_storage_g_job_complete(slurmdbd_conn->db_conn, &job);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just incase this gets set we need to clear it */
	xfree(job.wckey);
end_it:
	slurmdbd_free_job_complete_msg(slurmdbd_conn->rpc_version, 
				       job_comp_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_JOB_COMPLETE);
	return SLURM_SUCCESS;
}

static int  _job_start(slurmdbd_conn_t *slurmdbd_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_start_msg_t *job_start_msg = NULL;
	dbd_id_rc_msg_t id_rc_msg;
	struct job_record job;
	struct job_details details;
	char *comment = NULL;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_JOB_START message from invalid uid";
		error("%s %u", comment, *uid);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED, comment,
					      DBD_JOB_START);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_job_start_msg(slurmdbd_conn->rpc_version, 
					  &job_start_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_JOB_START message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_JOB_START);
		return SLURM_ERROR;
	}
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));
	memset(&id_rc_msg, 0, sizeof(dbd_id_rc_msg_t));

	job.total_procs = job_start_msg->alloc_cpus;
	job.node_cnt = job_start_msg->alloc_nodes;
	job.account = _replace_double_quotes(job_start_msg->account);
	job.assoc_id = job_start_msg->assoc_id;
	job.comment = job_start_msg->block_id;
	job.db_index = job_start_msg->db_index;
	details.begin_time = job_start_msg->eligible_time;
	job.user_id = job_start_msg->uid;
	job.group_id = job_start_msg->gid;
	job.job_id = job_start_msg->job_id;
	job.job_state = job_start_msg->job_state;
	job.name = _replace_double_quotes(job_start_msg->name);
	job.nodes = job_start_msg->nodes;
	job.network = job_start_msg->node_inx;
	job.partition = job_start_msg->partition;
	job.num_procs = job_start_msg->req_cpus;
	job.resv_id = job_start_msg->resv_id;
	job.priority = job_start_msg->priority;
	job.start_time = job_start_msg->start_time;
	job.time_limit = job_start_msg->timelimit;
	job.wckey = _replace_double_quotes(job_start_msg->wckey);
	details.submit_time = job_start_msg->submit_time;

	job.details = &details;

	if(job.start_time) {
		debug2("DBD_JOB_START: START CALL ID:%u NAME:%s INX:%u", 
		       job_start_msg->job_id, job_start_msg->name, 
		       job.db_index);	
	} else {
		debug2("DBD_JOB_START: ELIGIBLE CALL ID:%u NAME:%s", 
		       job_start_msg->job_id, job_start_msg->name);
	}
	id_rc_msg.return_code = jobacct_storage_g_job_start(
		slurmdbd_conn->db_conn, job_start_msg->cluster, &job);
	id_rc_msg.id = job.db_index;

	/* just incase job.wckey was set because we didn't send one */
	if(!job_start_msg->wckey)
		xfree(job.wckey);

	slurmdbd_free_job_start_msg(slurmdbd_conn->rpc_version, 
				    job_start_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_ID_RC, *out_buffer);
	slurmdbd_pack_id_rc_msg(slurmdbd_conn->rpc_version, 
				       &id_rc_msg, *out_buffer);
	return SLURM_SUCCESS;
}

static int  _job_suspend(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_suspend_msg_t *job_suspend_msg = NULL;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_JOB_SUSPEND message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_job_suspend_msg(slurmdbd_conn->rpc_version, 
					    &job_suspend_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_JOB_SUSPEND message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}

	debug2("DBD_JOB_SUSPEND: ID:%u STATE:%s", 
	       job_suspend_msg->job_id, 
	       job_state_string((enum job_states) job_suspend_msg->job_state));

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = job_suspend_msg->assoc_id;
	job.db_index = job_suspend_msg->db_index;
	job.job_id = job_suspend_msg->job_id;
	job.job_state = job_suspend_msg->job_state;
	details.submit_time = job_suspend_msg->submit_time;
	job.suspend_time = job_suspend_msg->suspend_time;

	job.details = &details;
	rc = jobacct_storage_g_job_suspend(slurmdbd_conn->db_conn, &job);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just incase this gets set we need to clear it */
	xfree(job.wckey);
end_it:
	slurmdbd_free_job_suspend_msg(slurmdbd_conn->rpc_version, 
				      job_suspend_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_JOB_SUSPEND);
	return SLURM_SUCCESS;
}

static int   _modify_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_MODIFY_ACCOUNTS: called");
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid)
	   < ACCT_ADMIN_OPERATOR) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_MODIFY_ACCOUNTS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_modify_msg(slurmdbd_conn->rpc_version, 
				       DBD_MODIFY_ACCOUNTS, &get_msg,
				       in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_ACCOUNTS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_MODIFY_ACCOUNTS);
		return SLURM_ERROR;
	}
	

	if(!(list_msg.my_list = acct_storage_g_modify_accounts(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond,
		     get_msg->rec))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
					 DBD_MODIFY_ACCOUNTS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_MODIFY_ACCOUNTS);
		return rc;		
	}
	slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
				 DBD_MODIFY_ACCOUNTS, get_msg);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	return rc;
}

static int   _modify_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = NULL;
	char *comment = NULL;
	dbd_list_msg_t list_msg;

	debug2("DBD_MODIFY_ASSOCS: called");

	if (slurmdbd_unpack_modify_msg(slurmdbd_conn->rpc_version, 
				       DBD_MODIFY_ASSOCS, &get_msg, 
				       in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_ASSOCS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_MODIFY_ASSOCS);
		return SLURM_ERROR;
	}
	

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if(!(list_msg.my_list = acct_storage_g_modify_associations(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond,
		     get_msg->rec))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
					 DBD_MODIFY_ASSOCS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_MODIFY_ASSOCS);
		return rc;
	}

	slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
				 DBD_MODIFY_ASSOCS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _modify_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg;
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = NULL;
	char *comment = NULL;

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid)
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_MODIFY_CLUSTERS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_modify_msg(slurmdbd_conn->rpc_version, 
				       DBD_MODIFY_CLUSTERS, &get_msg,
				       in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_CLUSTERS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_MODIFY_CLUSTERS);
		return SLURM_ERROR;
	}
	
	debug2("DBD_MODIFY_CLUSTERS: called");

	if(!(list_msg.my_list = acct_storage_g_modify_clusters(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond,
		     get_msg->rec))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
					 DBD_MODIFY_CLUSTERS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_MODIFY_CLUSTERS);
		return rc;
	}

	slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
				 DBD_MODIFY_CLUSTERS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _modify_qos(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg;
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = NULL;
	char *comment = NULL;

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid)
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_MODIFY_QOS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_modify_msg(slurmdbd_conn->rpc_version, 
				       DBD_MODIFY_QOS, &get_msg,
				       in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_QOS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_MODIFY_QOS);
		return SLURM_ERROR;
	}
	
	debug2("DBD_MODIFY_QOS: called");

	if(!(list_msg.my_list = acct_storage_g_modify_qos(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond,
		     get_msg->rec))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
					 DBD_MODIFY_QOS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_MODIFY_QOS);
		return rc;
	}

	slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
				 DBD_MODIFY_QOS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _modify_users(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg;
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = NULL;
	char *comment = NULL;
	int same_user = 0;
	int admin_level = assoc_mgr_get_admin_level(slurmdbd_conn->db_conn,
						    *uid);
	acct_user_cond_t *user_cond = NULL;
	acct_user_rec_t *user_rec = NULL;
		
	debug2("DBD_MODIFY_USERS: called");

	if (slurmdbd_unpack_modify_msg(slurmdbd_conn->rpc_version, 
				       DBD_MODIFY_USERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_USERS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_MODIFY_USERS);
		return SLURM_ERROR;
	}
	
	user_cond = (acct_user_cond_t *)get_msg->cond;
	user_rec = (acct_user_rec_t *)get_msg->rec;
			
	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && admin_level < ACCT_ADMIN_OPERATOR) {
		if(user_cond && user_cond->assoc_cond 
		   && user_cond->assoc_cond->user_list
		   && (list_count(user_cond->assoc_cond->user_list) == 1)) {
			uid_t pw_uid;
			char *name;
			name = list_peek (user_cond->assoc_cond->user_list);
		        if ((uid_from_string (name, &pw_uid) >= 0)
			    && pw_uid == *uid) {
				same_user = 1;
				goto is_same_user;
			}
		}
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_MODIFY_USERS);

		return ESLURM_ACCESS_DENIED;
	}

is_same_user:
	
	/* same_user can only alter the default account, default wckey
	 * nothing else */ 
	if(same_user) {
		/* If we add anything else here for the user we will
		 * need to document it
		 */
		if((user_rec->admin_level != ACCT_ADMIN_NOTSET)) {
			comment = "You can only change your own default account, default wckey nothing else";
			error("%s", comment);
			*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
						      ESLURM_ACCESS_DENIED,
						      comment,
						      DBD_MODIFY_USERS);
			
			return ESLURM_ACCESS_DENIED;	
		}		
	}

	if((user_rec->admin_level != ACCT_ADMIN_NOTSET) 
	   && (*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && (admin_level < user_rec->admin_level)) {
		comment = "You have to be the same or higher admin level to change another persons";
		user_rec->admin_level = ACCT_ADMIN_NOTSET;
	}

	if(!(list_msg.my_list = acct_storage_g_modify_users(
		     slurmdbd_conn->db_conn, *uid, user_cond, user_rec))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
					 DBD_MODIFY_USERS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_MODIFY_USERS);
		return rc;
	}

	slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
				 DBD_MODIFY_USERS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _modify_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg;
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = NULL;
	char *comment = NULL;

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid)
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_MODIFY_WCKEYS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_modify_msg(slurmdbd_conn->rpc_version, 
				       DBD_MODIFY_WCKEYS, &get_msg,
				       in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_WCKEYS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_MODIFY_WCKEYS);
		return SLURM_ERROR;
	}
	
	debug2("DBD_MODIFY_WCKEYS: called");

	if(!(list_msg.my_list = acct_storage_g_modify_wckeys(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond,
		     get_msg->rec))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
					 DBD_MODIFY_WCKEYS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_MODIFY_WCKEYS);
		return rc;
	}

	slurmdbd_free_modify_msg(slurmdbd_conn->rpc_version, 
				 DBD_MODIFY_WCKEYS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int _modify_reservation(slurmdbd_conn_t *slurmdbd_conn,
			     Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = NULL;
	char *comment = NULL;

	if ((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)) {
		comment = "DBD_MODIFY_RESV message from invalid uid";
		error("DBD_MODIFY_RESV message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_rec_msg(slurmdbd_conn->rpc_version, DBD_MODIFY_RESV,
				    &rec_msg, in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_MODIFY_RESV message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	debug2("DBD_MODIFY_RESV: called");

	rc = acct_storage_g_modify_reservation(slurmdbd_conn->db_conn,
					       rec_msg->rec);

end_it:
	slurmdbd_free_rec_msg(slurmdbd_conn->rpc_version,
			      DBD_MODIFY_RESV, rec_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_MODIFY_RESV);
	return rc;
}

static int _node_state(slurmdbd_conn_t *slurmdbd_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_node_state_msg_t *node_state_msg = NULL;
	struct node_record node_ptr;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;


	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_NODE_STATE message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_node_state_msg(slurmdbd_conn->rpc_version, 
					   &node_state_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_NODE_STATE message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}

	memset(&node_ptr, 0, sizeof(struct node_record));
	node_ptr.name = node_state_msg->hostlist;
	node_ptr.cpus = node_state_msg->cpu_count;
	node_ptr.node_state = node_state_msg->state;

	slurmctld_conf.fast_schedule = 0;
	
	if(!node_ptr.cpus)
		node_state_msg->new_state = DBD_NODE_STATE_UP;

	if(node_state_msg->new_state == DBD_NODE_STATE_UP) {
		debug3("DBD_NODE_STATE: NODE:%s STATE:%s REASON:%s TIME:%u", 
		       node_state_msg->hostlist,
		       _node_state_string(node_state_msg->new_state),
		       node_state_msg->reason, 
		       node_state_msg->event_time);
		rc = clusteracct_storage_g_node_up(
			slurmdbd_conn->db_conn,
			node_state_msg->cluster_name,
			&node_ptr,
			node_state_msg->event_time);
	} else {
		debug2("DBD_NODE_STATE: NODE:%s STATE:%s REASON:%s TIME:%u", 
		       node_state_msg->hostlist,
		       _node_state_string(node_state_msg->new_state),
		       node_state_msg->reason, 
		       node_state_msg->event_time);
		rc = clusteracct_storage_g_node_down(
			slurmdbd_conn->db_conn,
			node_state_msg->cluster_name,
			&node_ptr,
			node_state_msg->event_time,
			node_state_msg->reason);
	}

end_it:
	slurmdbd_free_node_state_msg(slurmdbd_conn->rpc_version, 
				     node_state_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_NODE_STATE);
	return SLURM_SUCCESS;
}

static char *_node_state_string(uint16_t node_state)
{
	switch(node_state) {
	case DBD_NODE_STATE_DOWN:
		return "DOWN";
	case DBD_NODE_STATE_UP:
		return "UP";
	}
	return "UNKNOWN";
}

static int   _register_ctld(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_register_ctld_msg_t *register_ctld_msg = NULL;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	acct_cluster_cond_t cluster_q;
	acct_cluster_rec_t cluster;
	dbd_list_msg_t list_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_REGISTER_CTLD message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_register_ctld_msg(slurmdbd_conn->rpc_version, 
					      &register_ctld_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REGISTER_CTLD message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	debug2("DBD_REGISTER_CTLD: called for %s(%u)",
	       register_ctld_msg->cluster_name, register_ctld_msg->port);

	debug2("slurmctld at ip:%s, port:%d", slurmdbd_conn->ip,
	       register_ctld_msg->port);

	memset(&cluster_q, 0, sizeof(acct_cluster_cond_t));
	memset(&cluster, 0, sizeof(acct_cluster_rec_t));
	cluster_q.cluster_list = list_create(NULL);
	list_append(cluster_q.cluster_list, register_ctld_msg->cluster_name);
	cluster.control_host = slurmdbd_conn->ip;
	cluster.control_port = register_ctld_msg->port;
	cluster.rpc_version = slurmdbd_conn->rpc_version;

	list_msg.my_list = acct_storage_g_modify_clusters(
		slurmdbd_conn->db_conn, *uid, &cluster_q, &cluster);
	if(errno == EFAULT) {
		comment = "Request to register was incomplete";
		rc = SLURM_ERROR;		
	} else if(!list_msg.my_list || !list_count(list_msg.my_list)) {
		comment = "This cluster hasn't been added to accounting yet";
		rc = SLURM_ERROR;
	} 
	
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	

	list_destroy(cluster_q.cluster_list);
	/*
	 * Outgoing message header must have flag set:
	 * out_msg.flags = SLURM_GLOBAL_AUTH_KEY;
	 */
#if 0
	{
		/* Code to validate communications back to slurmctld */
		slurm_fd fd;
		slurm_set_addr_char(&ctld_address, register_ctld_msg->port, ip);
		fd =  slurm_open_msg_conn(&ctld_address);
		if (fd < 0) {
			error("can not open socket back to slurmctld");
		} else {
			slurm_msg_t out_msg;
			slurm_msg_t_init(&out_msg);
			out_msg.msg_type = REQUEST_PING;
			out_msg.flags = SLURM_GLOBAL_AUTH_KEY;
			slurm_send_node_msg(slurmdbd_conn->rpc_version, 
					    fd, &out_msg);
			/* We probably need to add matching recv_msg function
			 * for an arbitray fd or should these be fire and forget? */
			slurm_close_stream(fd);
		}
	}
#endif

end_it:
	slurmdbd_free_register_ctld_msg(slurmdbd_conn->rpc_version, 
					register_ctld_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_REGISTER_CTLD);
	return rc;
}

static int   _remove_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_ACCOUNTS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_OPERATOR) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_REMOVE_ACCOUNTS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_REMOVE_ACCOUNTS, &get_msg, 
				     in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_ACCOUNTS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_REMOVE_ACCOUNTS);
		return SLURM_ERROR;
	}
	
	if(!(list_msg.my_list = acct_storage_g_remove_accounts(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
				       DBD_REMOVE_ACCOUNTS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_REMOVE_ACCOUNTS);
		return rc;
	}

	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_REMOVE_ACCOUNTS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _remove_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				    Buf in_buffer, Buf *out_buffer,
				    uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_acct_coord_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_ACCOUNT_COORDS: called");

	if (slurmdbd_unpack_acct_coord_msg(slurmdbd_conn->rpc_version, 
					   &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_ACCOUNT_COORDS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR, comment,
					      DBD_ADD_ACCOUNT_COORDS);
		return SLURM_ERROR;
	}
	
	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if(!(list_msg.my_list = acct_storage_g_remove_coord(
		     slurmdbd_conn->db_conn, *uid, get_msg->acct_list,
		     get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_acct_coord_msg(slurmdbd_conn->rpc_version, 
					     get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, 
					      DBD_REMOVE_ACCOUNT_COORDS);
		return rc;
	}

	slurmdbd_free_acct_coord_msg(slurmdbd_conn->rpc_version, 
				     get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _remove_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_ASSOCS: called");
	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_REMOVE_ASSOCS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_ASSOCS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_REMOVE_ASSOCS);
		return SLURM_ERROR;
	}

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if(!(list_msg.my_list = acct_storage_g_remove_associations(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
				       DBD_REMOVE_ASSOCS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_REMOVE_ASSOCS);
		return rc;
	}
	
	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_REMOVE_ASSOCS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;

}

static int   _remove_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_CLUSTERS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_REMOVE_CLUSTERS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_REMOVE_CLUSTERS, &get_msg, 
				     in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_CLUSTERS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_REMOVE_CLUSTERS);
		return SLURM_ERROR;
	}
	
	if(!(list_msg.my_list = acct_storage_g_remove_clusters(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
				       DBD_REMOVE_CLUSTERS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_REMOVE_CLUSTERS);
		return rc;		
	}

	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_REMOVE_CLUSTERS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _remove_qos(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_QOS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_REMOVE_QOS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_REMOVE_QOS, &get_msg, 
				     in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_QOS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_REMOVE_QOS);
		return SLURM_ERROR;
	}
	
	if(!(list_msg.my_list = acct_storage_g_remove_qos(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
				       DBD_REMOVE_QOS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_REMOVE_QOS);
		return rc;		
	}

	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_REMOVE_QOS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _remove_users(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_USERS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_OPERATOR) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_REMOVE_USERS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_REMOVE_USERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_USERS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_REMOVE_USERS);
		return SLURM_ERROR;
	}
	
	if(!(list_msg.my_list = acct_storage_g_remove_users(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
				       DBD_REMOVE_USERS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_REMOVE_USERS);
		return rc;
	}

	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_REMOVE_USERS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int   _remove_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			      Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = NULL;
	dbd_list_msg_t list_msg;
	char *comment = NULL;

	debug2("DBD_REMOVE_WCKEYS: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_SUPER_USER) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      ESLURM_ACCESS_DENIED,
					      comment, DBD_REMOVE_WCKEYS);

		return ESLURM_ACCESS_DENIED;
	}

	if (slurmdbd_unpack_cond_msg(slurmdbd_conn->rpc_version, 
				     DBD_REMOVE_WCKEYS, &get_msg, 
				     in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_WCKEYS message";
		error("%s", comment);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      SLURM_ERROR,
					      comment, DBD_REMOVE_WCKEYS);
		return SLURM_ERROR;
	}
	
	if(!(list_msg.my_list = acct_storage_g_remove_wckeys(
		     slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if(errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to preform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if(errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if(errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else {
			comment = "Unknown issue";
			rc = SLURM_ERROR;
		}
		error("%s", comment);
		slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
				       DBD_REMOVE_WCKEYS, get_msg);
		*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
					      rc, comment, DBD_REMOVE_WCKEYS);
		return rc;		
	}

	slurmdbd_free_cond_msg(slurmdbd_conn->rpc_version, 
			       DBD_REMOVE_WCKEYS, get_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(slurmdbd_conn->rpc_version, 
			       DBD_GOT_LIST, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);

	return rc;
}

static int _remove_reservation(slurmdbd_conn_t *slurmdbd_conn,
			     Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = NULL;
	char *comment = NULL;

	if ((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)) {
		comment = "DBD_REMOVE_RESV message from invalid uid";
		error("DBD_REMOVE_RESV message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_rec_msg(slurmdbd_conn->rpc_version, DBD_REMOVE_RESV,
				    &rec_msg, in_buffer) != SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_REMOVE_RESV message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}
	debug2("DBD_REMOVE_RESV: called");

	rc = acct_storage_g_remove_reservation(slurmdbd_conn->db_conn,
					     rec_msg->rec);

end_it:
	slurmdbd_free_rec_msg(slurmdbd_conn->rpc_version,
			      DBD_REMOVE_RESV, rec_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_REMOVE_RESV);
	return rc;
}

static int   _roll_usage(slurmdbd_conn_t *slurmdbd_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_roll_usage_msg_t *get_msg = NULL;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	info("DBD_ROLL_USAGE: called");

	if((*uid != slurmdbd_conf->slurm_user_id && *uid != 0)
	   && assoc_mgr_get_admin_level(slurmdbd_conn->db_conn, *uid) 
	   < ACCT_ADMIN_OPERATOR) {
		comment = "Your user doesn't have privilege to preform this action";
		error("%s", comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	if (slurmdbd_unpack_roll_usage_msg(slurmdbd_conn->rpc_version, 
					   &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_ROLL_USAGE message"; 
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}

	rc = acct_storage_g_roll_usage(slurmdbd_conn->db_conn,
				       get_msg->start, get_msg->end,
				       get_msg->archive_data);

end_it:
	slurmdbd_free_roll_usage_msg(slurmdbd_conn->rpc_version, 
				     get_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_ROLL_USAGE);
	return rc;
}

static int  _step_complete(slurmdbd_conn_t *slurmdbd_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_comp_msg_t *step_comp_msg = NULL;
	struct step_record step;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_STEP_COMPLETE message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_step_complete_msg(slurmdbd_conn->rpc_version, 
					      &step_comp_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_STEP_COMPLETE message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}

	debug2("DBD_STEP_COMPLETE: ID:%u.%u SUBMIT:%u", 
	       step_comp_msg->job_id, step_comp_msg->step_id,
	       step_comp_msg->job_submit_time);

	memset(&step, 0, sizeof(struct step_record));
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = step_comp_msg->assoc_id;
	job.db_index = step_comp_msg->db_index;
	job.end_time = step_comp_msg->end_time;
	step.exit_code = step_comp_msg->exit_code;
	step.jobacct = step_comp_msg->jobacct;
	job.job_id = step_comp_msg->job_id;
	step.requid = step_comp_msg->req_uid;
	job.start_time = step_comp_msg->start_time;
	details.submit_time = step_comp_msg->job_submit_time;
	step.step_id = step_comp_msg->step_id;
	step.cpu_count = step_comp_msg->total_procs;
	details.num_tasks = step_comp_msg->total_tasks;

	job.details = &details;
	step.job_ptr = &job;

	rc = jobacct_storage_g_step_complete(slurmdbd_conn->db_conn, &step);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;
	/* just incase this gets set we need to clear it */
	xfree(job.wckey);
end_it:
	slurmdbd_free_step_complete_msg(slurmdbd_conn->rpc_version, 
					step_comp_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_STEP_COMPLETE);
	return rc;
}

static int  _step_start(slurmdbd_conn_t *slurmdbd_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_start_msg_t *step_start_msg = NULL;
	struct step_record step;
	struct job_record job;
	struct job_details details;
	slurm_step_layout_t layout;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		comment = "DBD_STEP_START message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}
	if (slurmdbd_unpack_step_start_msg(slurmdbd_conn->rpc_version, 
					   &step_start_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		comment = "Failed to unpack DBD_STEP_START message";
		error("%s", comment);
		rc = SLURM_ERROR;
		goto end_it;
	}

	debug2("DBD_STEP_START: ID:%u.%u NAME:%s SUBMIT:%d", 
	       step_start_msg->job_id, step_start_msg->step_id,
	       step_start_msg->name, step_start_msg->job_submit_time);

	memset(&step, 0, sizeof(struct step_record));
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));
	memset(&layout, 0, sizeof(slurm_step_layout_t));

	job.assoc_id = step_start_msg->assoc_id;
	job.db_index = step_start_msg->db_index;
	job.job_id = step_start_msg->job_id;
	step.name = step_start_msg->name;
	job.nodes = step_start_msg->nodes;
	step.network = step_start_msg->node_inx;
	step.start_time = step_start_msg->start_time;
	details.submit_time = step_start_msg->job_submit_time;
	step.step_id = step_start_msg->step_id;
	step.cpu_count = step_start_msg->total_procs;
	details.num_tasks = step_start_msg->total_tasks;

	layout.node_cnt = step_start_msg->node_cnt;
	layout.task_dist = step_start_msg->task_dist;

	job.details = &details;
	step.job_ptr = &job;
	step.step_layout = &layout;

	rc = jobacct_storage_g_step_start(slurmdbd_conn->db_conn, &step);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just incase this gets set we need to clear it */
	xfree(job.wckey);
end_it:
	slurmdbd_free_step_start_msg(slurmdbd_conn->rpc_version, 
				     step_start_msg);
	*out_buffer = make_dbd_rc_msg(slurmdbd_conn->rpc_version, 
				      rc, comment, DBD_STEP_START);
	return rc;
}

