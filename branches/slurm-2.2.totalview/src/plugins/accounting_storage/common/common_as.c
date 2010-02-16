/*****************************************************************************\
 *  common_as.c - common functions for accounting storage
 *
 *  $Id: common_as.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include <strings.h>
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_auth.h"
#include "src/common/xstring.h"
#include "common_as.h"

extern char *assoc_hour_table;
extern char *assoc_day_table;
extern char *assoc_month_table;

extern char *cluster_hour_table;
extern char *cluster_day_table;
extern char *cluster_month_table;

extern char *wckey_hour_table;
extern char *wckey_day_table;
extern char *wckey_month_table;


/*
 * send_accounting_update - send update to controller of cluster
 * IN update_list: updates to send
 * IN cluster: name of cluster
 * IN host: control host of cluster
 * IN port: control port of cluster
 * IN rpc_version: rpc version of cluster
 * RET:  error code
 */
extern int
send_accounting_update(List update_list, char *cluster, char *host,
		       uint16_t port, uint16_t rpc_version)
{
	accounting_update_msg_t msg;
	slurm_msg_t req;
	slurm_msg_t resp;
	int rc;

	if(rpc_version > SLURMDBD_VERSION) {
		error("%s at %s(%hu) ver %hu > %u, can't update",
		      cluster, host, port, rpc_version,
		      SLURMDBD_VERSION);
		return SLURM_ERROR;
	}
	msg.rpc_version = rpc_version;
	msg.update_list = update_list;

	debug("sending updates to %s at %s(%hu) ver %hu",
	      cluster, host, port, rpc_version);

	slurm_msg_t_init(&req);
	slurm_set_addr_char(&req.address, port, host);
	req.msg_type = ACCOUNTING_UPDATE_MSG;
	req.flags = SLURM_GLOBAL_AUTH_KEY;
	req.data = &msg;
	slurm_msg_t_init(&resp);

	rc = slurm_send_recv_node_msg(&req, &resp, 0);
	if ((rc != 0) || ! resp.auth_cred) {
		error("update cluster: %m to %s at %s(%hu)",
		      cluster, host, port);
		rc = SLURM_ERROR;
	}
	if (resp.auth_cred)
		g_slurm_auth_destroy(resp.auth_cred);

	switch (resp.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *)resp.data)->
			return_code;
		slurm_free_return_code_msg(resp.data);
		break;
	default:
		error("Unknown response message %u", resp.msg_type);
		rc = SLURM_ERROR;
		break;
	}
	//info("got rc of %d", rc);
	return rc;
}

/*
 * update_assoc_mgr - update the association manager
 * IN update_list: updates to perform
 * RET: error code
 * NOTE: the items in update_list are not deleted
 */
extern int
update_assoc_mgr(List update_list)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	acct_update_object_t *object = NULL;

	itr = list_iterator_create(update_list);
	while((object = list_next(itr))) {
		if(!object->objects || !list_count(object->objects)) {
			list_delete_item(itr);
			continue;
		}
		switch(object->type) {
		case ACCT_MODIFY_USER:
		case ACCT_ADD_USER:
		case ACCT_REMOVE_USER:
		case ACCT_ADD_COORD:
		case ACCT_REMOVE_COORD:
			rc = assoc_mgr_update_users(object);
			break;
		case ACCT_ADD_ASSOC:
		case ACCT_MODIFY_ASSOC:
		case ACCT_REMOVE_ASSOC:
			rc = assoc_mgr_update_assocs(object);
			break;
		case ACCT_ADD_QOS:
		case ACCT_MODIFY_QOS:
		case ACCT_REMOVE_QOS:
			rc = assoc_mgr_update_qos(object);
			break;
		case ACCT_ADD_WCKEY:
		case ACCT_MODIFY_WCKEY:
		case ACCT_REMOVE_WCKEY:
			rc = assoc_mgr_update_wckeys(object);
			break;
		case ACCT_UPDATE_NOTSET:
		default:
			error("unknown type set in "
			      "update_object: %d",
			      object->type);
			break;
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

/*
 * addto_update_list - add object updated to list
 * IN/OUT update_list: list of updated objects
 * IN type: update type
 * IN object: object updated
 * RET: error code
 */
extern int
addto_update_list(List update_list, acct_update_type_t type, void *object)
{
	acct_update_object_t *update_object = NULL;
	ListIterator itr = NULL;
	if(!update_list) {
		error("no update list given");
		return SLURM_ERROR;
	}

	itr = list_iterator_create(update_list);
	while((update_object = list_next(itr))) {
		if(update_object->type == type)
			break;
	}
	list_iterator_destroy(itr);

	if(update_object) {
		/* here we prepend primarly for remove association
		   since parents need to be removed last, and they are
		   removed first in the calling code */
		list_prepend(update_object->objects, object);
		return SLURM_SUCCESS;
	}
	update_object = xmalloc(sizeof(acct_update_object_t));

	list_append(update_list, update_object);

	update_object->type = type;

	switch(type) {
	case ACCT_MODIFY_USER:
	case ACCT_ADD_USER:
	case ACCT_REMOVE_USER:
	case ACCT_ADD_COORD:
	case ACCT_REMOVE_COORD:
		update_object->objects = list_create(destroy_acct_user_rec);
		break;
	case ACCT_ADD_ASSOC:
	case ACCT_MODIFY_ASSOC:
	case ACCT_REMOVE_ASSOC:
		update_object->objects = list_create(
			destroy_acct_association_rec);
		break;
	case ACCT_ADD_QOS:
	case ACCT_MODIFY_QOS:
	case ACCT_REMOVE_QOS:
		update_object->objects = list_create(
			destroy_acct_qos_rec);
		break;
	case ACCT_ADD_WCKEY:
	case ACCT_MODIFY_WCKEY:
	case ACCT_REMOVE_WCKEY:
		update_object->objects = list_create(
			destroy_acct_wckey_rec);
		break;
	case ACCT_UPDATE_NOTSET:
	default:
		error("unknown type set in update_object: %d", type);
		return SLURM_ERROR;
	}
	debug3("XXX: update object with type %d added", type);
	list_append(update_object->objects, object);
	return SLURM_SUCCESS;
}


static void
_dump_acct_assoc_records(List assoc_list)
{
	acct_association_rec_t *assoc = NULL;
	ListIterator itr = NULL;

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		debug("\t\tid=%d", assoc->id);
	}
	list_iterator_destroy(itr);
}

/*
 * dump_update_list - dump contents of updates
 * IN update_list: updates to perform
 */
extern void
dump_update_list(List update_list)
{
	ListIterator itr = NULL;
	acct_update_object_t *object = NULL;

	debug3("========== DUMP UPDATE LIST ==========");
	itr = list_iterator_create(update_list);
	while((object = list_next(itr))) {
		if(!object->objects || !list_count(object->objects)) {
			debug3("\tUPDATE OBJECT WITH NO RECORDS, type: %d", object->type);
			continue;
		}
		switch(object->type) {
		case ACCT_MODIFY_USER:
		case ACCT_ADD_USER:
		case ACCT_REMOVE_USER:
		case ACCT_ADD_COORD:
		case ACCT_REMOVE_COORD:
			debug3("\tUSER RECORDS");
			break;
		case ACCT_ADD_ASSOC:
		case ACCT_MODIFY_ASSOC:
		case ACCT_REMOVE_ASSOC:
			debug3("\tASSOC RECORDS");
			_dump_acct_assoc_records(object->objects);
			break;
		case ACCT_ADD_QOS:
		case ACCT_MODIFY_QOS:
		case ACCT_REMOVE_QOS:
			debug3("\tQOS RECORDS");
			break;
		case ACCT_ADD_WCKEY:
		case ACCT_MODIFY_WCKEY:
		case ACCT_REMOVE_WCKEY:
			debug3("\tWCKEY RECORDS");
			break;
		case ACCT_UPDATE_NOTSET:
		default:
			error("unknown type set in "
			      "update_object: %d",
			      object->type);
			break;
		}
	}
	list_iterator_destroy(itr);
}


/*
 * cluster_first_reg - ask for controller to send nodes in a down state
 *    and jobs pending or running on first registration.
 *
 * IN host: controller host
 * IN port: controller port
 * IN rpc_version: controller rpc version
 * RET: error code
 */
extern int
cluster_first_reg(char *host, uint16_t port, uint16_t rpc_version)
{
	slurm_addr ctld_address;
	slurm_fd fd;
	int rc = SLURM_SUCCESS;

	info("First time to register cluster requesting "
	     "running jobs and system information.");

	slurm_set_addr_char(&ctld_address, port, host);
	fd = slurm_open_msg_conn(&ctld_address);
	if (fd < 0) {
		error("can not open socket back to slurmctld "
		      "%s(%u): %m", host, port);
		rc = SLURM_ERROR;
	} else {
		slurm_msg_t out_msg;
		accounting_update_msg_t update;
		/* We have to put this update message here so
		   we can tell the sender to send the correct
		   RPC version.
		*/
		memset(&update, 0, sizeof(accounting_update_msg_t));
		update.rpc_version = rpc_version;
		slurm_msg_t_init(&out_msg);
		out_msg.msg_type = ACCOUNTING_FIRST_REG;
		out_msg.flags = SLURM_GLOBAL_AUTH_KEY;
		out_msg.data = &update;
		slurm_send_node_msg(fd, &out_msg);
		/* We probably need to add matching recv_msg function
		 * for an arbitray fd or should these be fire
		 * and forget?  For this, that we can probably
		 * forget about it */
		slurm_close_stream(fd);
	}
	return rc;
}

/*
 * set_usage_information - set time and table information for getting usage
 *
 * OUT usage_table: which usage table to query
 * IN type: usage type to get
 * IN/OUT usage_start: start time
 * IN/OUT usage_end: end time
 * RET: error code
 */
extern int
set_usage_information(char **usage_table, slurmdbd_msg_type_t type,
		      time_t *usage_start, time_t *usage_end)
{
	time_t start = (*usage_start), end = (*usage_end);
	time_t my_time = time(NULL);
	struct tm start_tm;
	struct tm end_tm;
	char *my_usage_table = (*usage_table);

	/* Default is going to be the last day */
	if(!end) {
		if(!localtime_r(&my_time, &end_tm)) {
			error("Couldn't get localtime from end %d",
			      my_time);
			return SLURM_ERROR;
		}
		end_tm.tm_hour = 0;
	} else {
		if(!localtime_r(&end, &end_tm)) {
			error("Couldn't get localtime from user end %d",
			      end);
			return SLURM_ERROR;
		}
	}
	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_isdst = -1;
	end = mktime(&end_tm);

	if(!start) {
		if(!localtime_r(&my_time, &start_tm)) {
			error("Couldn't get localtime from start %d",
			      my_time);
			return SLURM_ERROR;
		}
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
	} else {
		if(!localtime_r(&start, &start_tm)) {
			error("Couldn't get localtime from user start %d",
			      start);
			return SLURM_ERROR;
		}
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_isdst = -1;
	start = mktime(&start_tm);

	if(end-start < 3600) {
		end = start + 3600;
		if(!localtime_r(&end, &end_tm)) {
			error("2 Couldn't get localtime from user end %d",
			      end);
			return SLURM_ERROR;
		}
	}
	/* check to see if we are off day boundaries or on month
	 * boundaries other wise use the day table.
	 */
	//info("%d %d %d", start_tm.tm_hour, end_tm.tm_hour, end-start);
	if(start_tm.tm_hour || end_tm.tm_hour || (end-start < 86400)
	   || (end > my_time)) {
		switch (type) {
		case DBD_GET_ASSOC_USAGE:
			my_usage_table = assoc_hour_table;
			break;
		case DBD_GET_WCKEY_USAGE:
			my_usage_table = wckey_hour_table;
			break;
		case DBD_GET_CLUSTER_USAGE:
			my_usage_table = cluster_hour_table;
			break;
		default:
			error("Bad type given for hour usage %d %s", type,
			     slurmdbd_msg_type_2_str(type, 1));
			break;
		}
	} else if(start_tm.tm_mday == 0 && end_tm.tm_mday == 0
		  && (end-start > 86400)) {
		switch (type) {
		case DBD_GET_ASSOC_USAGE:
			my_usage_table = assoc_month_table;
			break;
		case DBD_GET_WCKEY_USAGE:
			my_usage_table = wckey_month_table;
			break;
		case DBD_GET_CLUSTER_USAGE:
			my_usage_table = cluster_month_table;
			break;
		default:
			error("Bad type given for month usage %d %s", type,
			     slurmdbd_msg_type_2_str(type, 1));
			break;
		}
	}

	(*usage_start) = start;
	(*usage_end) = end;
	(*usage_table) = my_usage_table;
	return SLURM_SUCCESS;
}


/*
 * merge_delta_qos_list - apply delta_qos_list to qos_list
 *
 * IN/OUT qos_list: list of QOS'es
 * IN delta_qos_list: list of delta QOS'es
 */
extern void
merge_delta_qos_list(List qos_list, List delta_qos_list)
{
	ListIterator curr_itr = list_iterator_create(qos_list);
	ListIterator new_itr = list_iterator_create(delta_qos_list);
	char *new_qos = NULL, *curr_qos = NULL;

	while((new_qos = list_next(new_itr))) {
		if(new_qos[0] == '-') {
			while((curr_qos = list_next(curr_itr))) {
				if(!strcmp(curr_qos, new_qos+1)) {
					list_delete_item(curr_itr);
					break;
				}
			}
			list_iterator_reset(curr_itr);
		} else if(new_qos[0] == '+') {
			while((curr_qos = list_next(curr_itr))) {
				if(!strcmp(curr_qos, new_qos+1)) {
					break;
				}
			}
			if(!curr_qos) {
				list_append(qos_list, xstrdup(new_qos+1));
			}
			list_iterator_reset(curr_itr);
		}
	}
	list_iterator_destroy(new_itr);
	list_iterator_destroy(curr_itr);
}
