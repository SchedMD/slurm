/*****************************************************************************\
 *  assoc_mgr.h - keep track of local cache of accounting data.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SLURM_ASSOC_MGR_H
#define _SLURM_ASSOC_MGR_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/list.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurmdbd_defs.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#define ASSOC_MGR_CACHE_ASSOC 0x0001
#define ASSOC_MGR_CACHE_QOS   0x0002
#define ASSOC_MGR_CACHE_USER  0x0004
#define ASSOC_MGR_CACHE_WCKEY 0x0008
#define ASSOC_MGR_CACHE_RES   0x0010
#define ASSOC_MGR_CACHE_ALL   0xffff

/* to lock or not */
typedef struct {
	lock_level_t assoc;
	lock_level_t file;
	lock_level_t qos;
	lock_level_t res;
	lock_level_t user;
	lock_level_t wckey;
} assoc_mgr_lock_t;

/* Interval lock structure
 * we actually use the count for each data type, see macros below
 *   (assoc_mgr_lock_datatype_t * 4 + 0) = read_lock        read locks in use
 *   (assoc_mgr_lock_datatype_t * 4 + 1) = write_lock       write locks in use
 *   (assoc_mgr_lock_datatype_t * 4 + 2) = write_wait_lock  write locks pending
 *   (assoc_mgr_lock_datatype_t * 4 + 3) = write_cnt_lock   write lock count
 */
typedef enum {
	ASSOC_LOCK,
	FILE_LOCK,
	QOS_LOCK,
	RES_LOCK,
	USER_LOCK,
	WCKEY_LOCK,
	ASSOC_MGR_ENTITY_COUNT
} assoc_mgr_lock_datatype_t;

typedef struct {
	int entity[ASSOC_MGR_ENTITY_COUNT * 4];
} assoc_mgr_lock_flags_t;

typedef struct {
 	uint16_t cache_level;
	uint16_t enforce;
	void (*add_license_notify) (slurmdb_res_rec_t *rec);
 	void (*remove_assoc_notify) (slurmdb_association_rec_t *rec);
	void (*remove_license_notify) (slurmdb_res_rec_t *rec);
 	void (*remove_qos_notify) (slurmdb_qos_rec_t *rec);
	void (*sync_license_notify) (List clus_res_list);
 	void (*update_assoc_notify) (slurmdb_association_rec_t *rec);
	void (*update_license_notify) (slurmdb_res_rec_t *rec);
 	void (*update_qos_notify) (slurmdb_qos_rec_t *rec);
	void (*update_resvs) ();
} assoc_init_args_t;

struct assoc_mgr_association_usage {
	List children_list;     /* list of children associations
				 * (DON'T PACK) */
	uint32_t grp_used_cpus; /* count of active jobs in the group
				 * (DON'T PACK) */
	uint32_t grp_used_mem; /* count of active memory in the group
				 * (DON'T PACK) */
	uint32_t grp_used_nodes; /* count of active jobs in the group
				  * (DON'T PACK) */
	double grp_used_wall;   /* group count of time used in
				 * running jobs (DON'T PACK) */
	uint64_t grp_used_cpu_run_secs; /* count of running cpu secs
					 * (DON'T PACK) */
	double fs_factor;	/* Fairshare factor. Not used by all algorithms
				 * (DON'T PACK) */
	uint32_t level_shares;  /* number of shares on this level of
				 * the tree (DON'T PACK) */

	slurmdb_association_rec_t *parent_assoc_ptr; /* ptr to direct
						      * parent assoc
						      * set in slurmctld
						      * (DON'T PACK) */

	slurmdb_association_rec_t *fs_assoc_ptr;    /* ptr to fairshare parent
						     * assoc if fairshare
						     * == SLURMDB_FS_USE_PARENT
						     * set in slurmctld
						     * (DON'T PACK) */

	double shares_norm;     /* normalized shares (DON'T PACK) */

	long double usage_efctv;/* effective, normalized usage (DON'T PACK) */
	long double usage_norm;	/* normalized usage (DON'T PACK) */
	long double usage_raw;	/* measure of resource usage (DON'T PACK) */

	uint32_t used_jobs;	/* count of active jobs (DON'T PACK) */
	uint32_t used_submit_jobs; /* count of jobs pending or running
				    * (DON'T PACK) */

	/* Currently FAIR_TREE and TICKET_BASED systems are defining data on
	 * this struct but instead we could keep a void pointer to system
	 * specific data. This would allow subsystems to define whatever data
	 * they need without having to modify this struct; it would also save
	 * space.
	 */
	uint32_t tickets;       /* Number of tickets (for multifactor2
				 * plugin). (DON'T PACK) */
	unsigned active_seqno;  /* Sequence number for identifying
				 * active associations (DON'T PACK) */

	long double level_fs;	/* (FAIR_TREE) Result of fairshare equation
				 * compared to the association's siblings (DON'T
				 * PACK) */

	bitstr_t *valid_qos;    /* qos available for this association
				 * derived from the qos_list.
				 * (DON'T PACK) */
};

struct assoc_mgr_qos_usage {
	List job_list; /* list of job pointers to submitted/running
			  jobs (DON'T PACK) */
	uint32_t grp_used_cpus; /* count of cpus in use in this qos
				 * (DON'T PACK) */
	uint64_t grp_used_cpu_run_secs; /* count of running cpu secs
					 * (DON'T PACK) */
	uint32_t grp_used_jobs;	/* count of active jobs (DON'T PACK) */
	uint32_t grp_used_mem; /* count of memory in use in this qos
				* (DON'T PACK) */
	uint32_t grp_used_nodes; /* count of nodes in use in this qos
				  * (DON'T PACK) */
	uint32_t grp_used_submit_jobs; /* count of jobs pending or running
					* (DON'T PACK) */
	double grp_used_wall;   /* group count of time (minutes) used in
				 * running jobs (DON'T PACK) */
	double norm_priority;/* normalized priority (DON'T PACK) */
	long double usage_raw;	/* measure of resource usage (DON'T PACK) */

	List user_limit_list; /* slurmdb_used_limits_t's (DON'T PACK) */
};


extern List assoc_mgr_association_list;
extern List assoc_mgr_res_list;
extern List assoc_mgr_qos_list;
extern List assoc_mgr_user_list;
extern List assoc_mgr_wckey_list;

extern slurmdb_association_rec_t *assoc_mgr_root_assoc;

extern uint32_t g_qos_max_priority; /* max priority in all qos's */
extern uint32_t g_qos_count; /* count used for generating qos bitstr's */
extern uint32_t g_user_assoc_count; /* Number of assocations which are users */


extern int assoc_mgr_init(void *db_conn, assoc_init_args_t *args,
			  int db_conn_errno);
extern int assoc_mgr_fini(char *state_save_location);
extern void assoc_mgr_lock(assoc_mgr_lock_t *locks);
extern void assoc_mgr_unlock(assoc_mgr_lock_t *locks);

extern assoc_mgr_association_usage_t *create_assoc_mgr_association_usage();
extern void destroy_assoc_mgr_association_usage(void *object);
extern assoc_mgr_qos_usage_t *create_assoc_mgr_qos_usage();
extern void destroy_assoc_mgr_qos_usage(void *object);

/*
 * get info from the storage
 * IN:  assoc - slurmdb_association_rec_t with at least cluster and
 *		    account set for account association.  To get user
 *		    association set user, and optional partition.
 *		    Sets "id" field with the association ID.
 * IN: enforce - return an error if no such association exists
 * IN/OUT: assoc_list - contains a list of assoc_rec ptrs to
 *                      associations this user has in the list.  This
 *                      list should be created with list_create(NULL)
 *                      since we are putting pointers to memory used elsewhere.
 * RET: SLURM_SUCCESS on success, else SLURM_ERROR
 *
 * NOTE: Since the returned assoc_list is full of pointers from the
 *       assoc_mgr_association_list assoc_mgr_lock_t READ_LOCK on
 *       associations must be set before calling this function and while
 *       handling it after a return.
 */
extern int assoc_mgr_get_user_assocs(void *db_conn,
				     slurmdb_association_rec_t *assoc,
				     int enforce,
				     List assoc_list);

/*
 * get info from the storage
 * IN/OUT:  assoc - slurmdb_association_rec_t with at least cluster and
 *		    account set for account association.  To get user
 *		    association set user, and optional partition.
 *		    Sets "id" field with the association ID.
 * IN: enforce - return an error if no such association exists
 * IN/OUT: assoc_pptr - if non-NULL then return a pointer to the
 *			slurmdb_association record in cache on success
 *                      DO NOT FREE.
 * IN: locked - If you plan on using assoc_pptr after this function
 *              you need to have an assoc_mgr_lock_t READ_LOCK for
 *              associations while you use it before and after the
 *              return.  This is not required if using the assoc for
 *              non-pointer portions.
 * RET: SLURM_SUCCESS on success, else SLURM_ERROR
 */
extern int assoc_mgr_fill_in_assoc(void *db_conn,
				   slurmdb_association_rec_t *assoc,
				   int enforce,
				   slurmdb_association_rec_t **assoc_pptr,
				   bool locked);

/*
 * get info from the storage
 * IN/OUT:  user - slurmdb_user_rec_t with the name set of the user.
 *                 "default_account" will be filled in on
 *                 successful return DO NOT FREE.
 * IN/OUT: user_pptr - if non-NULL then return a pointer to the
 *		       slurmdb_user record in cache on success
 *                     DO NOT FREE.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int assoc_mgr_fill_in_user(void *db_conn, slurmdb_user_rec_t *user,
				  int enforce,
				  slurmdb_user_rec_t **user_pptr);

/*
 * get info from the storage
 * IN/OUT:  qos - slurmdb_qos_rec_t with the id set of the qos.
 * IN/OUT:  qos_pptr - if non-NULL then return a pointer to the
 *		       slurmdb_qos record in cache on success
 *                     DO NOT FREE.
 * IN: locked - If you plan on using qos_pptr, or g_qos_count outside
 *              this function you need to have an assoc_mgr_lock_t
 *              READ_LOCK for QOS while you use it before and after the
 *              return.  This is not required if using the assoc for
 *              non-pointer portions.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int assoc_mgr_fill_in_qos(void *db_conn, slurmdb_qos_rec_t *qos,
				 int enforce,
				 slurmdb_qos_rec_t **qos_pptr, bool locked);
/*
 * get info from the storage
 * IN/OUT:  wckey - slurmdb_wckey_rec_t with the name, cluster and user
 *		    for the wckey association.
 *		    Sets "id" field with the wckey ID.
 * IN: enforce - return an error if no such wckey exists
 * IN/OUT: wckey_pptr - if non-NULL then return a pointer to the
 *			slurmdb_wckey record in cache on success
 * RET: SLURM_SUCCESS on success, else SLURM_ERROR
 */
extern int assoc_mgr_fill_in_wckey(void *db_conn,
				   slurmdb_wckey_rec_t *wckey,
				   int enforce,
				   slurmdb_wckey_rec_t **wckey_pptr);

/*
 * get admin_level of uid
 * IN: uid - uid of user to check admin_level of.
 * RET: admin level SLURMDB_ADMIN_NOTSET on error
 */
extern slurmdb_admin_level_t assoc_mgr_get_admin_level(void *db_conn,
						       uint32_t uid);

/*
 * see if user is coordinator of given acct
 * IN: uid - uid of user to check.
 * IN: acct - name of account
 * RET: true or false
 */
extern bool assoc_mgr_is_user_acct_coord(void *db_conn, uint32_t uid,
					char *acct);

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
 * assoc_mgr_update - update the association manager
 * IN update_list: updates to perform
 * RET: error code
 * NOTE: the items in update_list are not deleted
 */
extern int assoc_mgr_update(List update_list);

/*
 * update associations in cache
 * IN:  slurmdb_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_assocs(slurmdb_update_object_t *update);

/*
 * update wckeys in cache
 * IN:  slurmdb_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_wckeys(slurmdb_update_object_t *update);

/*
 * update qos in cache
 * IN:  slurmdb_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_qos(slurmdb_update_object_t *update);

/*
 * update cluster resources in cache
 * IN:  slurmdb_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_res(slurmdb_update_object_t *update);

/*
 * update users in cache
 * IN:  slurmdb_update_object_t *object
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int assoc_mgr_update_users(slurmdb_update_object_t *update);

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
 * Remove the association's accumulated usage
 * IN:  slurmdb_association_rec_t *assoc
 * RET: SLURM_SUCCESS on success or else SLURM_ERROR
 */
extern void assoc_mgr_remove_assoc_usage(slurmdb_association_rec_t *assoc);

/*
 * Remove the QOS's accumulated usage
 * IN:  slurmdb_qos_rec_t *qos
 * RET: SLURM_SUCCESS on success or else SLURM_ERROR
 */
extern void assoc_mgr_remove_qos_usage(slurmdb_qos_rec_t *qos);

/*
 * Dump the state information of the association mgr just incase the
 * database isn't up next time we run.
 */
extern int dump_assoc_mgr_state(char *state_save_location);

/*
 * Read in the past usage for associations.
 */
extern int load_assoc_usage(char *state_save_location);

/*
 * Read in the past usage for qos.
 */
extern int load_qos_usage(char *state_save_location);

/*
 * Read in the information of the association mgr if the database
 * isn't up when starting.
 */
extern int load_assoc_mgr_state(char *state_save_location);

/*
 * Refresh the lists if when running_cache is set this will load new
 * information from the database (if any) and update the cached list.
 */
extern int assoc_mgr_refresh_lists(void *db_conn);

/*
 * Sets the uids of users added to the system after the start of the
 * calling program.
 */
extern int assoc_mgr_set_missing_uids();

/* Normalize shares for an association. External so a priority plugin
 * can call it if needed.
 */
extern void assoc_mgr_normalize_assoc_shares(slurmdb_association_rec_t *assoc);

#endif /* _SLURM_ASSOC_MGR_H */
