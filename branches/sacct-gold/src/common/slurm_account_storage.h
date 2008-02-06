/*****************************************************************************\
 *  slurm_account_storage.h - Define account storage plugin functions.
 *
 * $Id: slurm_account_storage.h 10574 2006-12-15 23:38:29Z jette $
 *****************************************************************************
 *  Copyright (C) 2004-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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

#ifndef _SLURM_ACCOUNT_STORAGE_H 
#define _SLURM_ACCOUNT_STORAGE_H

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

enum {
	ACCOUNT_ADMIN_NONE,
	ACCOUNT_ADMIN_OPERATOR,
	ACCOUNT_ADMIN_SUPER_USER
} account_admin_level_t;

typedef struct {
	char *name;
	uint32_t uid;
	uint32_t gid;
	char *default_project;
	uint32_t expedite;
	account_admin_level_t admin_level;
} account_user_rec_t;

typedef struct {
	char *name;
	char *description;
	char *organization;
	uint32_t expedite;
} account_project_rec_t;

typedef struct {
	char *name;
} account_cluster_rec_t;

typedef struct {
	uint32_t id;
	char *user;
	char *project;
	char *machine;
	uint32_t parent;
	uint32_t fairshare;
	uint32_t max_jobs;
	uint32_t max_nodes_per_job;
	uint32_t max_wall_duration_per_job;
	uint32_t max_cpu_seonds_per_job;
} account_acct_rec_t;

extern void destroy_account_user_rec(void *object);
extern void destroy_account_project_rec(void *object);
extern void destroy_account_cluster_rec(void *object);
extern void destroy_account_acct_rec(void *object);


extern int slurm_account_storage_init(void); /* load the plugin */
extern int slurm_account_storage_fini(void); /* unload the plugin */

/* 
 * add users to accounting system 
 * IN:  user_list List of user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_users(List user_list);

/* 
 * add projects to accounting system 
 * IN:  project_list List of project_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_projects(List project_list);

/* 
 * add clusters to accounting system 
 * IN:  cluster_list List of cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_clusters(List cluster_list);

/* 
 * add accts to accounting system 
 * IN:  acct_list List of acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_accounts(List account_list);

/* 
 * modify existing users in the accounting system 
 * IN:  user_list List of user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_users(List user_list);

/* 
 * modify existing projects in the accounting system 
 * IN:  project_list List of project_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_projects(List project_list);

/* 
 * modify existing clusters in the accounting system 
 * IN:  cluster_list List of cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_clusters(List cluster_list);

/* 
 * modify existing accounts in the accounting system 
 * IN:  account_list List of acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_accounts(List account_list);

/* 
 * remove users from accounting system 
 * IN:  user_list List of user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_users(List user_list);

/* 
 * remove projects from accounting system 
 * IN:  project_list List of project_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_projects(List project_list);

/* 
 * remove clusters from accounting system 
 * IN:  cluster_list List of cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_clusters(List cluster_list);

/* 
 * remove accounts from accounting system 
 * IN:  account_list List of acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_accounts(List account_list);

/* 
 * get info from the storage 
 * returns List of user_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_users(List selected_users,
					void *params);

/* 
 * get info from the storage 
 * returns List of project_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_projects(List selected_projects,
					   void *params);

/* 
 * get info from the storage 
 * returns List of cluster_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_clusters(List selected_clusters,
					   void *params);

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_accounts(List selected_accounts,
					   List selected_users,
					   List selected_projects,
					   char *cluster,
					   void *params);

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_hourly_usage(List selected_accounts,
					       List selected_users,
					       List selected_projects,
					       char *cluster,
					       time_t start,
					       time_t end,
					       void *params);

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_daily_usage(List selected_accounts,
					      List selected_users,
					      List selected_projects,
					      char *cluster,
					      time_t start,
					      time_t end,
					      void *params);

/* 
 * get info from the storage 
 * returns List of acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_monthly_usage(List selected_accounts,
						List selected_users,
						List selected_projects,
						char *cluster,
						time_t start,
						time_t end,
						void *params);

#endif /*_SLURM_ACCOUNT_STORAGE_H*/
