/*****************************************************************************\
 *  assoc_mgr.h - keep track of local cache of accounting data.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
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

#ifndef _SLURM_ASSOC_MGR_H 
#define _SLURM_ASSOC_MGR_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/list.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurmdbd_defs.h"
#include "src/slurmctld/slurmctld.h"
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#define ASSOC_MGR_CACHE_ASSOC 0x0001
#define ASSOC_MGR_CACHE_QOS 0x0002
#define ASSOC_MGR_CACHE_USER 0x0004
#define ASSOC_MGR_CACHE_WCKEY 0x0008
#define ASSOC_MGR_CACHE_ALL 0xffff

typedef struct {
	uint16_t cache_level;
	uint16_t enforce;
 	void (*remove_assoc_notify) (acct_association_rec_t *rec);
} assoc_init_args_t;

extern List assoc_mgr_association_list;
extern List assoc_mgr_qos_list;
extern List assoc_mgr_user_list;
extern List assoc_mgr_wckey_list;

extern acct_association_rec_t *assoc_mgr_root_assoc;
extern pthread_mutex_t assoc_mgr_association_lock;
extern pthread_mutex_t assoc_mgr_qos_lock;
extern pthread_mutex_t assoc_mgr_user_lock;
extern pthread_mutex_t assoc_mgr_file_lock;
extern pthread_mutex_t assoc_mgr_wckey_lock;

/* 
 * get info from the storage 
 * IN:  assoc - acct_association_rec_t with at least cluster and
 *		    account set for account association.  To get user
 *		    association set user, and optional partition.
 *		    Sets "id" field with the association ID.
 * IN: enforce - return an error if no such association exists
 * IN/OUT: assoc_list - contains a list of assoc_rec ptrs to
 *                      associations this user has in the list.  This
 *                      list should be created with list_create(NULL)
 *                      since we are putting pointers to memory used elsewhere.
 * RET: SLURM_SUCCESS on success, else SLURM_ERROR
 */
extern int assoc_mgr_get_user_assocs(void *db_conn,
				     acct_association_rec_t *assoc,
				     int enforce, 
				     List assoc_list);

/* 
 * get info from the storage 
 * IN/OUT:  assoc - acct_association_rec_t with at least cluster and
 *		    account set for account association.  To get user
 *		    association set user, and optional partition.
 *		    Sets "id" field with the association ID.
 * IN: enforce - return an error if no such association exists
 * IN/OUT: assoc_pptr - if non-NULL then return a pointer to the 
 *			acct_association record in cache on success
 *                      DO NOT FREE.
 * RET: SLURM_SUCCESS on success, else SLURM_ERROR
 */
extern int assoc_mgr_fill_in_assoc(void *db_conn,
				   acct_association_rec_t *assoc,
				   int enforce,
				   acct_association_rec_t **assoc_pptr);

/* 
 * get info from the storage 
 * IN/OUT:  user - acct_user_rec_t with the name set of the user.
 *                 "default_account" will be filled in on
 *                 successful return DO NOT FREE.
 * IN/OUT: user_pptr - if non-NULL then return a pointer to the 
 *		       acct_user record in cache on success
 *                     DO NOT FREE.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int assoc_mgr_fill_in_user(void *db_conn, acct_user_rec_t *user,
				  int enforce,
				  acct_user_rec_t **user_pptr);

/* 
 * get info from the storage 
 * IN/OUT:  qos - acct_qos_rec_t with the id set of the qos.
 * IN/OUT:  qos_pptr - if non-NULL then return a pointer to the 
 *		       acct_qos record in cache on success
 *                     DO NOT FREE.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int assoc_mgr_fill_in_qos(void *db_conn, acct_qos_rec_t *qos,
				 int enforce,
				 acct_qos_rec_t **qos_pptr);
/* 
 * get info from the storage 
 * IN/OUT:  wckey - acct_wckey_rec_t with the name, cluster and user
 *		    for the wckey association. 
 *		    Sets "id" field with the wckey ID.
 * IN: enforce - return an error if no such wckey exists
 * IN/OUT: wckey_pptr - if non-NULL then return a pointer to the 
 *			acct_wckey record in cache on success
 * RET: SLURM_SUCCESS on success, else SLURM_ERROR
 */
extern int assoc_mgr_fill_in_wckey(void *db_conn,
				   acct_wckey_rec_t *wckey,
				   int enforce,
				   acct_wckey_rec_t **wckey_pptr);

/* 
 * get admin_level of uid 
 * IN: uid - uid of user to check admin_level of.
 * RET: admin level ACCT_ADMIN_NOTSET on error 
 */
extern acct_admin_level_t assoc_mgr_get_admin_level(void *db_conn, 
						    uint32_t uid);

/* 
 * see if user is coordinator of given acct 
 * IN: uid - uid of user to check.
 * IN: acct - name of account
 * RET: 0 for no, 1 for yes
 */
extern int assoc_mgr_is_user_acct_coord(void *db_conn, uint32_t uid,
					char *acct);

extern int assoc_mgr_init(void *db_conn, assoc_init_args_t *args);
extern int assoc_mgr_fini(char *state_save_location);

/*
 * get the share information from the association list in the form of
 * a list containing association_share_object_t's 
 * IN: uid: uid_t of user issuing the request
 * IN: acct_list: char * list of accounts you want (NULL for all)
 * IN: user_list: char * list of user names you want (NULL for all)
 */
extern List assoc_mgr_get_shares(
	void *db_conn, uid_t uid, List acct_list, List user_list);

/* 
 * update associations in cache 
 * IN:  acct_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_assocs(acct_update_object_t *update);

/* 
 * update wckeys in cache 
 * IN:  acct_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_wckeys(acct_update_object_t *update);

/* 
 * update qos in cache 
 * IN:  acct_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_qos(acct_update_object_t *update);

/* 
 * update users in cache 
 * IN:  acct_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_users(acct_update_object_t *update);

/* 
 * validate that an association ID is still valid 
 * IN:  assoc_id - association ID previously returned by 
 *		get_assoc_id(void *db_conn, 
 )
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int assoc_mgr_validate_assoc_id(void *db_conn, 
				       uint32_t assoc_id,
				       int enforce);

/*
 * clear the used_* fields from every assocation, 
 *	used on reconfiguration
 */
extern void assoc_mgr_clear_used_info(void);


/*
 * Dump the state information of the association mgr just incase the
 * database isn't up next time we run.
 */
extern int dump_assoc_mgr_state(char *state_save_location);

/*
 * Read in the usage for association if the database
 * is up when starting.
 */
extern int load_assoc_usage(char *state_save_location);

/*
 * Read in the information of the association mgr if the database
 * isn't up when starting.
 */
extern int load_assoc_mgr_state(char *state_save_location);

/*
 * Refresh the lists if when running_cache is set this will load new
 * information from the database (if any) and update the cached list.
 */
extern int assoc_mgr_refresh_lists(void *db_conn, assoc_init_args_t *args);

#endif /* _SLURM_ASSOC_MGR_H */
