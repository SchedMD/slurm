/*****************************************************************************\
 *  account_storage_gold.c - account interface to gold.
 *
 *  $Id: account_gold.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2008 The Regents of the University of California.
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

#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <pwd.h>


#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"
#include <src/common/parse_time.h>

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_account_storage.h"

#include "src/database/gold_interface.h"

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
const char plugin_name[] = "Account storage GOLD plugin";
const char plugin_type[] = "account_storage/gold";
const uint32_t plugin_version = 100;

static char *cluster_name = NULL;



/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	char *keyfile = NULL;
	char *host = NULL;
	uint32_t port = 0;
	struct	stat statbuf;

	if(!(cluster_name = slurm_get_cluster_name())) 
		fatal("To run account_storage/gold you have to specify "
		      "ClusterName in your slurm.conf");

	if(!(keyfile = slurm_get_account_storage_pass()) 
	   || strlen(keyfile) < 1) {
		keyfile = xstrdup("/etc/gold/auth_key");
		debug2("No keyfile specified with AccountStoragePass, "
		       "gold using default %s", keyfile);
	}
	

	if(stat(keyfile, &statbuf)) {
		fatal("Can't stat key file %s. "
		      "To run account_storage/gold you have to set "
		      "your gold keyfile as "
		      "AccountStoragePass in your slurm.conf", keyfile);
	}


	if(!(host = slurm_get_account_storage_host())) {
		host = xstrdup("localhost");
		debug2("No host specified with AccountStorageHost, "
		       "gold using default %s", host);
	}

	if(!(port = slurm_get_account_storage_port())) {
		port = 7112;
		debug2("No port specified with AccountStoragePort, "
		       "gold using default %u", port);
	}

	debug2("connecting from %s to gold with keyfile='%s' for %s(%d)",
	       cluster_name, keyfile, host, port);

	init_gold(cluster_name, keyfile, host, port);

//	if(!gold_account_list) 
//		gold_account_list = list_create(_destroy_gold_account);
		
	xfree(keyfile);
	xfree(host);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	xfree(cluster_name);
//	if(gold_account_list) 
//		list_destroy(gold_account_list);
	fini_gold();
	return SLURM_SUCCESS;
}

/* 
 * add users to accounting system 
 * IN:  user_list List of user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_add_users(List user_list)
{
	return SLURM_SUCCESS;
}

/* 
 * add users as project coordinators 
 * IN:  user_list List of user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_add_coord(char *project, List user_list)
{
	return SLURM_SUCCESS;
}

/* 
 * add projects to accounting system 
 * IN:  project_list List of project_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_add_projects(List project_list)
{
	return SLURM_SUCCESS;
}

/* 
 * add clusters to accounting system 
 * IN:  cluster_list List of cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_add_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

/* 
 * add accts to accounting system 
 * IN:  acct_list List of acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_add_accounts(List account_list)
{
	return SLURM_SUCCESS;
}

/* 
 * modify existing users in the accounting system 
 * IN:  user_list List of user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_modify_users(List user_list)
{
	return SLURM_SUCCESS;
}

/* 
 * modify existing users admin level in the accounting system 
 * IN:  level account_admin_level_t
 * IN:  user_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_modify_user_admin_level(
	account_admin_level_t level, List user_list)
{
	return SLURM_SUCCESS;
}

/* 
 * modify existing projects in the accounting system 
 * IN:  project_list List of project_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_modify_projects(List project_list)
{
	return SLURM_SUCCESS;
}

/* 
 * modify existing clusters in the accounting system 
 * IN:  cluster_list List of cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_modify_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

/* 
 * modify existing accounts in the accounting system 
 * IN:  account_list List of acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_modify_accounts(List account_list)
{
	return SLURM_SUCCESS;
}

/* 
 * remove users from accounting system 
 * IN:  user_list List of char * (user names)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_remove_users(List user_list)
{
	return SLURM_SUCCESS;
}

/* 
 * remove users from being a coordinator of an account
 * IN: project name of project
 * IN: user_list List of char * (user names)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_remove_coord(char *project, List user_list)
{
	return SLURM_SUCCESS;
}

/* 
 * remove projects from accounting system 
 * IN:  project_list List of char * (project names) with either id set
 *      or user, project or cluster or a combination for multiple
 *      fitting the same specs
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_remove_projects(List project_list)
{
	return SLURM_SUCCESS;
}

/* 
 * remove clusters from accounting system 
 * IN:  cluster_list List of char * (cluster names)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_remove_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

/* 
 * remove accounts from accounting system 
 * IN:  account_list List of acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_p_remove_accounts(List account_list)
{
	return SLURM_SUCCESS;
}

/* 
 * get info from the storage 
 * returns List of user_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_users(List selected_users,
					void *params)
{
	return NULL;
}

/* 
 * get info from the storage 
 * returns List of project_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_projects(List selected_projects,
					   void *params)
{
	return NULL;
}

/* 
 * get info from the storage 
 * returns List of cluster_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_clusters(List selected_clusters,
					   void *params)
{
	return NULL;
}

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_accounts(List account_list,
					   List selected_accounts,
					   List selected_users,
					   List selected_projects,
					   char *cluster,
					   void *params)
{
	return NULL;
}

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_hourly_usage(List selected_accounts,
					       List selected_users,
					       List selected_projects,
					       char *cluster,
					       time_t start, 
					       time_t end,
					       void *params)
{
	return NULL;
}

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_daily_usage(List selected_accounts,
					      List selected_users,
					      List selected_projects,
					      char *cluster,
					      time_t start, 
					      time_t end,
					      void *params)
{
	return NULL;	
}

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_p_get_monthly_usage(List selected_accounts,
						List selected_users,
						List selected_projects,
						char *cluster,
						time_t start, 
						time_t end,
						void *params)
{
	return NULL;	
}
