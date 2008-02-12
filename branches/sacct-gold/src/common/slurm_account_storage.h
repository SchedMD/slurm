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

typedef enum {
	ACCOUNT_ADMIN_NONE,
	ACCOUNT_ADMIN_OPERATOR,
	ACCOUNT_ADMIN_SUPER_USER
} account_admin_level_t;

typedef enum {
	ACCOUNT_EXPEDITE_NOTSET,
	ACCOUNT_EXPEDITE_NORMAL,
	ACCOUNT_EXPEDITE_EXPEDITE,
	ACCOUNT_EXPEDITE_STANDBY,
	ACCOUNT_EXPEDITE_EXEMPT	
} account_expedite_level_t;

typedef struct {
	char *name;
	uint32_t uid;
	uint32_t gid;
	char *default_account;
	account_expedite_level_t expedite;
	account_admin_level_t admin_level;
} account_user_rec_t;

typedef struct {
	char *name;
	char *description;
	char *organization;
	account_expedite_level_t expedite;
	List coodinators;
} account_account_rec_t;

typedef struct {
	char *name;
	List accounting_list; /* list of clusteracct_rec_t *'s from
			       * slurm_clusteracct_storage.h */
} account_cluster_rec_t;

typedef struct {
	time_t period_start; 
	uint32_t alloc_secs; /* number of cpu seconds allocated */
} account_accounting_rec_t;

typedef struct {
	uint32_t id; /* id identifing a combination of
			user-account-cluster(-partition) */
	char *user;  /* user associated to association */
	char *account; /* account/project associated to association */
	char *cluster; /* cluster associated to association */
	char *partition; /* optional partition in a cluster 
			    associated to association */
	uint32_t parent; /* if there is a parent association to this */
	uint32_t lft; /* left most association in this group */
	uint32_t rgt; /* right most association in this group */
	uint32_t fairshare; /* fairshare number */
	uint32_t max_jobs; /* max number of jobs this association can run
			      at a time */
	uint32_t max_nodes_per_job; /* max number of nodes this
				       association can allocate per job */
	uint32_t max_wall_duration_per_job; /* longest time this
					       association can run a job */
	uint32_t max_cpu_seconds_per_job; /* max number of cpu seconds
					     this association can have per job */
	List accounting_list; /* list of account_accounting_rec_t *'s */
} account_association_rec_t;

extern void destroy_account_user_rec(void *object);
extern void destroy_account_account_rec(void *object);
extern void destroy_account_cluster_rec(void *object);
extern void destroy_account_accounting_rec(void *object);
extern void destroy_account_association_rec(void *object);


extern int slurm_account_storage_init(void); /* load the plugin */
extern int slurm_account_storage_fini(void); /* unload the plugin */

/* 
 * add users to accounting system 
 * IN:  user_list List of account_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_users(List user_list);

/* 
 * add users as account coordinators 
 * IN:  account name of account
 * IN:  user_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_coord(char *account, List user_list);


/* 
 * add accounts to accounting system 
 * IN:  account_list List of account_account_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_accounts(List account_list);

/* 
 * add clusters to accounting system 
 * IN:  cluster_list List of account_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_clusters(List cluster_list);

/* 
 * add accts to accounting system 
 * IN:  association_list List of account_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_add_associations(List association_list);

/* 
 * modify existing users in the accounting system 
 * IN:  user_list List of account_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_users(List user_list);

/* 
 * modify existing users admin level in the accounting system 
 * IN:  level account_admin_level_t
 * IN:  user_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_user_admin_level(
	account_admin_level_t level, List user_list);

/* 
 * modify existing accounts in the accounting system 
 * IN:  account_list List of account_account_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_accounts(List account_list);

/* 
 * modify existing clusters in the accounting system 
 * IN:  cluster_list List of account_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_clusters(List cluster_list);

/* 
 * modify existing associations in the accounting system 
 * IN:  association_list List of account_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_associations(List association_list);

/* 
 * remove users from accounting system 
 * IN:  user_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_users(List user_list);

/* 
 * remove users from being a coordinator of an account
 * IN: account name of account
 * IN: user_list List of char * (user names)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_coord(char *account, List user_list);

/* 
 * remove accounts from accounting system 
 * IN:  account_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_accounts(List account_list);

/* 
 * remove clusters from accounting system 
 * IN:  cluster_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_clusters(List cluster_list);

/* 
 * remove associations from accounting system 
 * IN:  association_list List of account_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_associations(List association_list);

/* 
 * get info from the storage 
 * IN:  selected_users List of char *
 * IN:  params void *
 * returns List of account_user_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_users(List selected_users,
					void *params);

/* 
 * get info from the storage 
 * IN:  selected_accounts List of char *
 * IN:  params void *
 * returns List of account_account_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_accounts(List selected_accounts,
					   void *params);

/* 
 * get info from the storage 
 * IN:  selected_clusters List of char *
 * IN:  params void *
 * returns List of account_cluster_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_clusters(List selected_clusters,
					   void *params);

/* 
 * get info from the storage 
 * IN:  selected_users List of char *
 * IN:  selected_accounts List of char *
 * IN:  selected_parts List of char *
 * IN:  cluster name of cluster
 * IN:  params void *
 * returns List of account_association_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_associations(List selected_users,
					  List selected_accounts,
					  List selected_parts,
					  char *cluster,
					  void *params);

/* 
 * get info from the storage 
 * IN/OUT:  acct_assoc account_association_rec_t with the id set
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <
 * IN:  params void *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_get_hourly_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end, void *params);

/* 
 * get info from the storage 
 * IN/OUT:  acct_assoc account_association_rec_t with the id set
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <
 * IN:  params void *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_get_daily_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end, void *params);

/* 
 * get info from the storage 
 * IN/OUT:  acct_assoc account_association_rec_t with the id set
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <
 * IN:  params void *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_get_monthly_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end, void *params);

#endif /*_SLURM_ACCOUNT_STORAGE_H*/
