/*****************************************************************************\
 *  accounting_storage_slurmdbd.c - accounting interface to slurmdbd.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
#include <slurm/slurm_errno.h>

#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

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
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Accounting storage SLURMDBD plugin";
const char plugin_type[] = "accounting_storage/slurmdbd";
const uint32_t plugin_version = 100;

static char *cluster_name       = NULL;
static char *slurmdbd_auth_info = NULL;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;

	if (first) {
		/* since this can be loaded from many different places
		   only tell us once. */
		if (!(cluster_name = slurm_get_cluster_name()))
			fatal("%s requires ClusterName in slurm.conf", plugin_name);
		
		slurmdbd_auth_info = slurm_get_slurmdbd_auth_info();
		verbose("%s loaded SlurmdDbdAuthInfo=%s",
			plugin_name, slurmdbd_auth_info);
		slurm_open_slurmdbd_conn(slurmdbd_auth_info);

		first = 0;
	} else {
		debug4("%s loaded", plugin_name);
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	xfree(cluster_name);
	xfree(slurmdbd_auth_info);
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(List user_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_coord(char *acct, acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(List acct_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_associations(List association_list)
{
	return SLURM_SUCCESS;
}

extern uint32_t acct_storage_p_get_assoc_id(acct_association_rec_t *assoc)
{
	return 0;
}

extern int acct_storage_p_modify_users(acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_user_admin_level(acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_accts(acct_account_cond_t *acct_q,
				       acct_account_rec_t *acct)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_clusters(acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_associations(acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_users(acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_coord(char *acct, acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_accts(acct_account_cond_t *acct_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_clusters(acct_account_cond_t *cluster_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_associations(acct_association_cond_t *assoc_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_get_users(acct_user_cond_t *user_q)
{
	return NULL;
}

extern List acct_storage_p_get_accts(acct_account_cond_t *acct_q)
{
	return NULL;
}

extern List acct_storage_p_get_clusters(acct_account_cond_t *cluster_q)
{
	return NULL;
}

extern List acct_storage_p_get_associations(acct_association_cond_t *assoc_q)
{
	return NULL;
}

extern int acct_storage_p_get_hourly_usage(acct_association_rec_t *acct_assoc,
					   time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_get_daily_usage(acct_association_rec_t *acct_assoc,
					  time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_get_monthly_usage(acct_association_rec_t *acct_assoc,
					    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int clusteracct_storage_p_node_down(char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason)
{
	slurmdbd_msg_t msg;
	dbd_node_state_msg_t req;

	req.cluster_name = cluster;
	req.hostlist   = node_ptr->name;
	req.new_state  = DBD_NODE_STATE_DOWN;
	req.event_time = event_time;
	req.reason     = reason;
	msg.msg_type   = DBD_NODE_STATE;
	msg.data       = &req;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_node_up(char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	slurmdbd_msg_t msg;
	dbd_node_state_msg_t req;

	req.cluster_name = cluster;
	req.hostlist   = node_ptr->name;
	req.new_state  = DBD_NODE_STATE_UP;
	req.event_time = event_time;
	req.reason     = NULL;
	msg.msg_type   = DBD_NODE_STATE;
	msg.data       = &req;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_cluster_procs(char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
	slurmdbd_msg_t msg;
	dbd_cluster_procs_msg_t req;

	req.cluster_name = cluster;
	req.proc_count   = procs;
	req.event_time   = event_time;
	msg.msg_type     = DBD_CLUSTER_PROCS;
	msg.data         = &req;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_hourly_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{

	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_daily_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_monthly_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	
	return SLURM_SUCCESS;
}
