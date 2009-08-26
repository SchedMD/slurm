/*****************************************************************************\
 *  slurm_accounting_storage.h - Define accounting storage plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _SLURM_ACCOUNTING_STORAGE_H 
#define _SLURM_ACCOUNTING_STORAGE_H

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include <sys/types.h>
#include <pwd.h>

typedef enum {
	ACCT_ADMIN_NOTSET,
	ACCT_ADMIN_NONE,
	ACCT_ADMIN_OPERATOR,
	ACCT_ADMIN_SUPER_USER
} acct_admin_level_t;

typedef enum {
	ACCT_UPDATE_NOTSET,
	ACCT_ADD_USER,
	ACCT_ADD_ASSOC,
	ACCT_ADD_COORD,
	ACCT_MODIFY_USER,
	ACCT_MODIFY_ASSOC,
	ACCT_REMOVE_USER,
	ACCT_REMOVE_ASSOC,
	ACCT_REMOVE_COORD,
	ACCT_ADD_QOS,
	ACCT_REMOVE_QOS,
	ACCT_MODIFY_QOS,
	ACCT_ADD_WCKEY,
	ACCT_REMOVE_WCKEY,
	ACCT_MODIFY_WCKEY,
} acct_update_type_t;

typedef enum {
	ACCT_CLASS_NONE, /* no class given */
	ACCT_CLASS_CAPABILITY, /* capability cluster */
	ACCT_CLASS_CAPACITY, /* capacity cluster */
	ACCT_CLASS_CAPAPACITY, /* a cluster that is both capability
				* and capacity */
} acct_classification_type_t;

typedef enum {
	ACCT_PROBLEM_NOT_SET,
	ACCT_PROBLEM_ACCT_NO_ASSOC,
	ACCT_PROBLEM_ACCT_NO_USERS,
	ACCT_PROBLEM_USER_NO_ASSOC,
	ACCT_PROBLEM_USER_NO_UID,
} acct_problem_type_t;

#define ACCT_CLASSIFIED_FLAG 0x0100
#define ACCT_CLASS_BASE      0x00ff

/* Association conditions used for queries of the database */
typedef struct {
	List acct_list;		/* list of char * */
	List cluster_list;	/* list of char * */

	List fairshare_list;	/* fairshare number */

	List grp_cpu_mins_list; /* list of char * */
	List grp_cpus_list; /* list of char * */
	List grp_jobs_list;	/* list of char * */
	List grp_nodes_list; /* list of char * */
	List grp_submit_jobs_list; /* list of char * */
	List grp_wall_list; /* list of char * */

	List id_list;		/* list of char */

	List max_cpu_mins_pj_list; /* list of char * */
	List max_cpus_pj_list; /* list of char * */
	List max_jobs_list;	/* list of char * */
	List max_nodes_pj_list; /* list of char * */
	List max_submit_jobs_list; /* list of char * */
	List max_wall_pj_list; /* list of char * */

	List partition_list;	/* list of char * */
	List parent_acct_list;	/* name of parent account */

	List qos_list; /* list of char * */	

	time_t usage_end; 
	time_t usage_start; 

	List user_list;		/* list of char * */

	uint16_t with_usage;  /* fill in usage */
	uint16_t with_deleted; /* return deleted associations */
	uint16_t with_raw_qos; /* return a raw qos or delta_qos */
	uint16_t with_sub_accts; /* return sub acct information also */
	uint16_t without_parent_info; /* don't give me parent id/name */
	uint16_t without_parent_limits; /* don't give me limits from
					 * parents */
} acct_association_cond_t;

typedef struct {
	acct_association_cond_t *assoc_cond;/* use acct_list here for
						names */
	List description_list; /* list of char * */
	List organization_list; /* list of char * */
	uint16_t with_assocs; 
	uint16_t with_coords; 
	uint16_t with_deleted; 
} acct_account_cond_t;

typedef struct {
	List assoc_list; /* list of acct_association_rec_t *'s */
	List coordinators; /* list of acct_coord_rec_t *'s */
	char *description;
	char *name;
	char *organization;
} acct_account_rec_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	uint32_t id;	/* association/wckey ID		*/
	time_t period_start; /* when this record was started */
} acct_accounting_rec_t;

typedef struct acct_association_rec {
	List accounting_list; 	/* list of acct_accounting_rec_t *'s */
	char *acct;		/* account/project associated to association */
	List childern_list;     /* list of childern associations
				 * (DON'T PACK) */
	char *cluster;		/* cluster associated to association
				 * */

	uint64_t grp_cpu_mins; /* max number of cpu minutes the
				* underlying group of
				* associations can run for */
	uint32_t grp_cpus; /* max number of cpus the
			    * underlying group of 
			    * associations can allocate at one time */
	uint32_t grp_jobs;	/* max number of jobs the
				 * underlying group of associations can run
				 * at one time */
	uint32_t grp_nodes; /* max number of nodes the
			     * underlying group of
			     * associations can allocate at once */
	uint32_t grp_submit_jobs; /* max number of jobs the
				   * underlying group of
				   * associations can submit at
				   * one time */
	uint32_t grp_wall; /* total time in hours the 
			    * underlying group of
			    * associations can run for */
	
	uint32_t grp_used_cpus; /* count of active jobs in the group
				 * (DON'T PACK) */
	uint32_t grp_used_nodes; /* count of active jobs in the group
				  * (DON'T PACK) */
	double grp_used_wall;   /* group count of time used in
				 * running jobs (DON'T PACK) */
	
	uint32_t id;		/* id identifing a combination of
				 * user-account-cluster(-partition) */
	
	uint32_t level_shares;  /* number of shares on this level of
				 * the tree (DON'T PACK) */
	
	uint32_t lft;		/* lft used for grouping sub
				 * associations and jobs as a left
				 * most container used with rgt */
	
	uint64_t max_cpu_mins_pj; /* max number of cpu seconds this 
				   * association can have per job */
	uint32_t max_cpus_pj; /* max number of cpus this 
			       * association can allocate per job */
	uint32_t max_jobs;	/* max number of jobs this association can run
				 * at one time */
	uint32_t max_nodes_pj; /* max number of nodes this
				* association can allocate per job */
	uint32_t max_submit_jobs; /* max number of jobs that can be
				     submitted by association */
	uint32_t max_wall_pj; /* longest time this
			       * association can run a job */
	
	char *parent_acct;	/* name of parent account */
	struct acct_association_rec *parent_assoc_ptr;	/* ptr to parent acct
							 * set in
							 * slurmctld 
							 * (DON'T PACK) */
	uint32_t parent_id;	/* id of parent account */
	char *partition;	/* optional partition in a cluster 
				 * associated to association */
	
	List qos_list;          /* list of char * */
	
	uint32_t rgt;		/* rgt used for grouping sub
				 * associations and jobs as a right
				 * most container used with lft */

	double shares_norm;     /* normalized shares (DON'T PACK) */
	uint32_t shares_raw;	/* number of shares allocated to association */

	uint32_t uid;		/* user ID */
	
	long double usage_efctv;/* effective, normalized usage (DON'T PACK) */
	long double usage_norm;	/* normalized usage (DON'T PACK) */
	long double usage_raw;	/* measure of resource usage (DON'T PACK) */

	uint32_t used_jobs;	/* count of active jobs (DON'T PACK) */
	uint32_t used_submit_jobs; /* count of jobs pending or running
				    * (DON'T PACK) */
	
	char *user;		/* user associated to association */
	bitstr_t *valid_qos;    /* qos available for this association
				 * derived from the qos_list.
				 * (DON'T PACK) */	
} acct_association_rec_t;

typedef struct {
	uint16_t classification; /* how this machine is classified */
	List cluster_list; /* list of char * */
	time_t usage_end; 
	time_t usage_start; 
	uint16_t with_deleted; 
	uint16_t with_usage; 
} acct_cluster_cond_t;

typedef struct {
	List accounting_list; /* list of cluster_accounting_rec_t *'s */
	uint16_t classification; /* how this machine is classified */
	char *control_host;
	uint32_t control_port;
	uint32_t cpu_count;
	char *name;
	char *nodes;
	acct_association_rec_t *root_assoc; /* root association for cluster */
	uint16_t rpc_version; /* version of rpc this cluter is running */
} acct_cluster_rec_t;

typedef struct {
	char *name;
	uint16_t direct;
} acct_coord_rec_t;

typedef struct {
	List acct_list;		/* list of char * */
	List associd_list;	/* list of char */
	List cluster_list;	/* list of char * */
	uint16_t duplicates;    /* report duplicate job entries */
	List groupid_list;	/* list of char * */
	List partition_list;	/* list of char * */
	List resv_list;		/* list of char * */
	List resvid_list;	/* list of char * */
	List step_list;         /* list of jobacct_selected_step_t */
	List state_list;        /* list of char * */
	time_t usage_end; 
	time_t usage_start; 
	char *used_nodes;       /* a ranged node string where jobs ran */
	List userid_list;		/* list of char * */
	List wckey_list;		/* list of char * */
	uint16_t without_steps; /* don't give me step info */
	uint16_t without_usage_truncation; /* give me the information
					    * without truncating the
					    * time to the usage_start
					    * and usage_end */
} acct_job_cond_t;

typedef struct {
	char *description;
	uint32_t id;
	char *job_flags;
	List job_list; /* list of job pointers to submitted/running
			  jobs (DON'T PACK) */

	uint64_t grp_cpu_mins; /* max number of cpu hours the
				     * underlying group of
				     * associations can run for */
	uint32_t grp_cpus; /* max number of cpus this qos
			      can allocate at one time */
	uint32_t grp_jobs;	/* max number of jobs this qos can run
				 * at one time */
	uint32_t grp_nodes; /* max number of nodes this qos 
			       can allocate at once */
	uint32_t grp_submit_jobs; /* max number of jobs this qos can submit at
				   * one time */
	uint32_t grp_wall; /* total time in hours this qos can run for */

	uint32_t grp_used_cpus; /* count of cpus in use in this qos
				 * (DON'T PACK) */
	uint32_t grp_used_jobs;	/* count of active jobs (DON'T PACK) */
	uint32_t grp_used_nodes; /* count of nodes in use in this qos
				  * (DON'T PACK) */
	uint32_t grp_used_submit_jobs; /* count of jobs pending or running
				    * (DON'T PACK) */
	double grp_used_wall;   /* group count of time (minutes) used in
				 * running jobs (DON'T PACK) */

	uint64_t max_cpu_mins_pu; /* max number of cpu mins a user can
				   * use with this qos */
	uint32_t max_cpus_pu; /* max number of cpus a user can
			       * allocate with this qos */
	uint32_t max_jobs_pu;	/* max number of jobs a user can
				 * run with this qos at one time */
	uint32_t max_nodes_pu; /* max number of nodes a user can
				* allocate with this qos at one time */
	uint32_t max_submit_jobs_pu; /* max number of jobs a user can
				     submit with this qos at once */
	uint32_t max_wall_pu; /* longest time this
			       * qos can run a job */

	char *name;
	double norm_priority;/* normalized priority (DON'T PACK) */
	bitstr_t *preempt_bitstr; /* other qos' this qos can preempt */
	List preempt_list; /* list of char *'s only used to add or
			    * change the other qos' this can preempt,
			    * when doing a get use the preempt_bitstr */
	uint32_t priority;  /* ranged int needs to be a unint for
			     * heterogeneous systems */
	double usage_factor; /* factor to apply to usage in this qos */
	long double usage_raw;	/* measure of resource usage (DON'T PACK) */

	List user_limit_list; /* acct_used_limits_t's (DON'T PACK) */
} acct_qos_rec_t;

typedef struct {
	List description_list; /* list of char * */
	List id_list; /* list of char * */
	List name_list; /* list of char * */
	uint16_t with_deleted; 
} acct_qos_cond_t;

typedef struct {
	List cluster_list; /* cluster reservations are on list of
			    * char * */
	uint16_t flags; /* flags for reservation. */
	List id_list;   /* ids of reservations. list of char * */
	List name_list; /* name of reservations. list of char * */
	char *nodes; /* list of nodes in reservation */
	time_t time_end; /* end time of reservation */
	time_t time_start; /* start time of reservation */
	uint16_t with_usage; /* send usage for reservation */
} acct_reservation_cond_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	char *assocs; /* comma seperated list of associations */
	char *cluster; /* cluster reservation is for */
	uint32_t cpus; /* how many cpus are in reservation */
	uint64_t down_secs; /* number of cpu seconds down */
	uint16_t flags; /* flags for reservation. */
	uint32_t id;   /* id of reservation. */
	char *name; /* name of reservation */
	char *nodes; /* list of nodes in reservation */
	char *node_inx; /* node index of nodes in reservation */
	time_t time_end; /* end time of reservation */
	time_t time_start; /* start time of reservation */
	time_t time_start_prev; /* If start time was changed this is
				 * the pervious start time.  Needed
				 * for accounting */	
} acct_reservation_rec_t;

/* Right now this is used in the acct_qos_rec_t structure.  In the
 * user_limit_list. */
typedef struct {
 	uint64_t cpu_mins;	/* count of cpu mins used */
 	uint32_t cpus;	/* count of cpus in use */
	uint32_t jobs;	/* count of active jobs */
	uint32_t nodes;	/* count of nodes in use */
	uint32_t submit_jobs; /* count of jobs pending or running */
	uint32_t wall; /* how much time this user has used */
	uint32_t uid;
} acct_used_limits_t;

typedef struct {
	uint16_t admin_level; /* really acct_admin_level_t but for
				 packing purposes needs to be uint16_t */
	acct_association_cond_t *assoc_cond; /* use user_list here for
						names */
	List def_acct_list; /* list of char * */
	List def_wckey_list; /* list of char * */
	uint16_t with_assocs; 
	uint16_t with_coords; 
	uint16_t with_deleted; 
	uint16_t with_wckeys; 
} acct_user_cond_t;

/* If there is something that can be altered here it will need to
 * added as something to check for when modifying a user since a user
 * can modify there default account, and default wckey but nothing else in
 * src/slurmdbd/proc_req.c.
 */
typedef struct {
	uint16_t admin_level; /* really acct_admin_level_t but for
				 packing purposes needs to be uint16_t */
	List assoc_list; /* list of acct_association_rec_t *'s */
	List coord_accts; /* list of acct_coord_rec_t *'s */
	char *default_acct;
	char *default_wckey;
	char *name;
	uint32_t uid;
	List wckey_list; /* list of acct_wckey_rec_t *'s */
} acct_user_rec_t;

typedef struct {
	List acct_list; /* list of char * */
	List action_list; /* list of char * */
	List actor_list; /* list of char * */
	List cluster_list; /* list of char * */
	List id_list; /* list of char * */
	List info_list; /* list of char * */
	List name_list; /* list of char * */
	time_t time_end; 
	time_t time_start; 
	List user_list; /* list of char * */
	uint16_t with_assoc_info;
} acct_txn_cond_t;

typedef struct {
	char *accts;
	uint16_t action;
	char *actor_name;
	char *clusters;
	uint32_t id;
	char *set_info;
	time_t timestamp;
	char *users;
	char *where_query;
} acct_txn_rec_t;

typedef struct {
	List objects; /* depending on type */ 
	uint16_t type; /* really acct_update_type_t but for
				  * packing purposes needs to be a
				  * uint16_t */
} acct_update_object_t;

typedef struct {
	List cluster_list;	/* list of char * */
	List id_list;		/* list of char * */

	List name_list;        /* list of char * */

	time_t usage_end; 
	time_t usage_start; 

	List user_list;		/* list of char * */

	uint16_t with_usage;  /* fill in usage */
	uint16_t with_deleted; /* return deleted associations */
} acct_wckey_cond_t;

typedef struct {
	List accounting_list; 	/* list of acct_accounting_rec_t *'s */

	char *cluster;		/* cluster associated */

	uint32_t id;		/* id identifing a combination of
				 * user-wckey-cluster */
	char *name;		/* wckey name */
	uint32_t uid;		/* user ID */

	char *user;		/* user associated */
} acct_wckey_rec_t;

typedef struct {
	uint32_t assoc_id;	/* association ID		*/
	uint32_t shares_used;	/* measure of recent usage	*/
} shares_used_object_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	uint32_t cpu_count; /* number of cpus during time period */
	uint64_t down_secs; /* number of cpu seconds down */
	uint64_t idle_secs; /* number of cpu seconds idle */
	uint64_t over_secs; /* number of cpu seconds overcommitted */
	uint64_t pdown_secs; /* number of cpu seconds planned down */
	time_t period_start; /* when this record was started */
	uint64_t resv_secs; /* number of cpu seconds reserved */	
} cluster_accounting_rec_t;

typedef struct {
	char *name;
	char *print_name;
	char *spaces;
	uint16_t user; /* set to 1 if it is a user i.e. if name[0] is
			* '|' */
} acct_print_tree_t;

typedef struct {
	acct_association_rec_t *assoc;
	char *sort_name;
	List childern;
} acct_hierarchical_rec_t;

typedef struct {
	char *archive_dir;     /* location to place archive file */
	uint16_t archive_events; /* whether or not to keep an archive
				    file of events that can be loaded
				    later */
	uint16_t archive_jobs; /* whether or not to keep an archive
				  file of jobs that can be loaded
				  later */
	char *archive_script;  /* script to run instead of default
				  actions */
	uint16_t archive_steps; /* whether or not to keep an archive
				  file of steps that can be loaded
				  later */
	uint16_t archive_suspend; /* whether or not to keep an archive
				     file of suspend data that can be loaded
				     later */
	acct_job_cond_t *job_cond; /* conditions for the jobs to archive */
	uint16_t purge_event; /* purge events older than this in months */
	uint16_t purge_job; /* purge jobs older than this in months */
	uint16_t purge_step; /* purge steps older than this in months */
	uint16_t purge_suspend; /* purge suspend data older than this
				 * in months */
} acct_archive_cond_t;

typedef struct {
	char *archive_file;  /* archive file containing an insert line
				of jobs since past */
	char *insert;     /* an sql statement to be ran containing the
			     insert of jobs since past */
} acct_archive_rec_t;

extern uint32_t g_qos_max_priority; /* max priority in all qos's */
extern uint32_t g_qos_count; /* count used for generating qos bitstr's */

extern void destroy_acct_user_rec(void *object);
extern void destroy_acct_account_rec(void *object);
extern void destroy_acct_coord_rec(void *object);
extern void destroy_cluster_accounting_rec(void *object);
extern void destroy_acct_cluster_rec(void *object);
extern void destroy_acct_accounting_rec(void *object);
extern void destroy_acct_association_rec(void *object);
extern void destroy_acct_qos_rec(void *object);
extern void destroy_acct_reservation_rec(void *object);
extern void destroy_acct_txn_rec(void *object);
extern void destroy_acct_wckey_rec(void *object);
extern void destroy_acct_archive_rec(void *object);

extern void destroy_acct_user_cond(void *object);
extern void destroy_acct_account_cond(void *object);
extern void destroy_acct_cluster_cond(void *object);
extern void destroy_acct_association_cond(void *object);
extern void destroy_acct_job_cond(void *object);
extern void destroy_acct_qos_cond(void *object);
extern void destroy_acct_reservation_cond(void *object);
extern void destroy_acct_txn_cond(void *object);
extern void destroy_acct_wckey_cond(void *object);
extern void destroy_acct_archive_cond(void *object);

extern void destroy_acct_update_object(void *object);
extern void destroy_acct_used_limits(void *object);
extern void destroy_update_shares_rec(void *object);
extern void destroy_acct_print_tree(void *object);
extern void destroy_acct_hierarchical_rec(void *object);

extern void init_acct_association_rec(acct_association_rec_t *assoc);
extern void init_acct_qos_rec(acct_qos_rec_t *qos);

/* pack functions */
extern void pack_acct_user_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_user_rec(void **object, uint16_t rpc_version, 
				Buf buffer);
extern void pack_acct_account_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_account_rec(void **object, uint16_t rpc_version, 
				   Buf buffer);
extern void pack_acct_coord_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_coord_rec(void **object, uint16_t rpc_version,
				 Buf buffer);
extern void pack_cluster_accounting_rec(void *in, uint16_t rpc_version, 
					Buf buffer);
extern int unpack_cluster_accounting_rec(void **object, uint16_t rpc_version,
					 Buf buffer);
extern void pack_acct_cluster_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_cluster_rec(void **object, uint16_t rpc_version,
				   Buf buffer);
extern void pack_acct_accounting_rec(void *in, uint16_t rpc_version,
				     Buf buffer);
extern int unpack_acct_accounting_rec(void **object, uint16_t rpc_version, 
				      Buf buffer);
extern void pack_acct_association_rec(void *in, uint16_t rpc_version, 
				      Buf buffer);
extern int unpack_acct_association_rec(void **object, uint16_t rpc_version,
				       Buf buffer);
extern void pack_acct_qos_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_qos_rec(void **object, uint16_t rpc_version, Buf buffer);
extern void pack_acct_reservation_rec(void *in, uint16_t rpc_version,
				      Buf buffer);
extern int unpack_acct_reservation_rec(void **object, uint16_t rpc_version,
				       Buf buffer);
extern void pack_acct_txn_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_txn_rec(void **object, uint16_t rpc_version, Buf buffer);
extern void pack_acct_wckey_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_wckey_rec(void **object, uint16_t rpc_version,
				 Buf buffer);
extern void pack_acct_archive_rec(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_archive_rec(void **object, uint16_t rpc_version,
				 Buf buffer);

extern void pack_acct_user_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_user_cond(void **object, uint16_t rpc_version,
				 Buf buffer);
extern void pack_acct_account_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_account_cond(void **object, uint16_t rpc_version,
				    Buf buffer);
extern void pack_acct_cluster_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_cluster_cond(void **object, uint16_t rpc_version, 
				    Buf buffer);
extern void pack_acct_association_cond(void *in, uint16_t rpc_version,
				       Buf buffer);
extern int unpack_acct_association_cond(void **object, uint16_t rpc_version, 
					Buf buffer);
extern void pack_acct_job_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_job_cond(void **object, uint16_t rpc_version,
				Buf buffer);
extern void pack_acct_qos_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_qos_cond(void **object, uint16_t rpc_version,
				Buf buffer);
extern void pack_acct_reservation_cond(void *in, uint16_t rpc_version,
				       Buf buffer);
extern int unpack_acct_reservation_cond(void **object, uint16_t rpc_version,
					Buf buffer);
extern void pack_acct_txn_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_txn_cond(void **object, uint16_t rpc_version,
				Buf buffer);
extern void pack_acct_wckey_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_wckey_cond(void **object, uint16_t rpc_version,
				  Buf buffer);
extern void pack_acct_archive_cond(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_archive_cond(void **object, uint16_t rpc_version,
				  Buf buffer);

extern void pack_acct_update_object(acct_update_object_t *object, 
				    uint16_t rpc_version, Buf buffer);
extern int unpack_acct_update_object(acct_update_object_t **object,
				     uint16_t rpc_version, Buf buffer);

extern void pack_acct_used_limits(void *in, uint16_t rpc_version, Buf buffer);
extern int unpack_acct_used_limits(void **object,
				   uint16_t rpc_version, Buf buffer);

extern void pack_update_shares_used(void *in, uint16_t rpc_version,
				    Buf buffer);
extern int unpack_update_shares_used(void **object, uint16_t rpc_version, 
				     Buf buffer);

extern char *acct_qos_str(List qos_list, uint32_t level);
extern uint32_t str_2_acct_qos(List qos_list, char *level);
extern char *acct_admin_level_str(acct_admin_level_t level);
extern acct_admin_level_t str_2_acct_admin_level(char *level);

/* The next two functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List get_hierarchical_sorted_assoc_list(List assoc_list);
extern List get_acct_hierarchical_rec_list(List assoc_list);

/* IN/OUT: tree_list a list of acct_print_tree_t's */ 
extern char *get_tree_acct_name(char *name, char *parent, List tree_list);

extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list);
extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos);
extern char *get_qos_complete_str(List qos_list, List num_qos_list);

extern char *get_classification_str(uint16_t class);
extern uint16_t str_2_classification(char *class);

extern char *get_acct_problem_str(uint16_t problem);
extern uint16_t str_2_acct_problem(char *problem);

extern void log_assoc_rec(acct_association_rec_t *assoc_ptr, List qos_list);

extern int slurm_acct_storage_init(char *loc); /* load the plugin */
extern int slurm_acct_storage_fini(void); /* unload the plugin */

/*
 * get a new connection to the storage unit
 * IN: make_agent - Make an agent to manage queued requests
 * IN: conn_num - If running more than one connection to the database
 *     this can be used to tell which connection is doing what
 * IN: rollback - maintain journal of changes to permit rollback
 * RET: pointer used to access db 
 */
extern void *acct_storage_g_get_connection(bool make_agent, int conn_num,
					   bool rollback);

/*
 * release connection to the storage unit
 * IN/OUT: void ** pointer returned from
 *         acct_storage_g_get_connection() which will be freed.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else 
 */
extern int acct_storage_g_close_connection(void **db_conn);

/*
 * commit or rollback changes made without closing connection
 * IN: void * pointer returned from acct_storage_g_get_connection()
 * IN: bool - true will commit changes false will rollback
 * RET: SLURM_SUCCESS on success SLURM_ERROR else 
 */
extern int acct_storage_g_commit(void *db_conn, bool commit);

/* 
 * add users to accounting system 
 * IN:  user_list List of acct_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_users(void *db_conn, uint32_t uid, 
				    List user_list);

/* 
 * add users as account coordinators 
 * IN: acct_list list of char *'s of names of accounts
 * IN:  acct_user_cond_t *user_cond
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_coord(void *db_conn, uint32_t uid,
				    List acct_list, 
				    acct_user_cond_t *user_cond);


/* 
 * add accounts to accounting system 
 * IN:  account_list List of acct_account_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_accounts(void *db_conn, uint32_t uid,
				       List acct_list);

/* 
 * add clusters to accounting system 
 * IN:  cluster_list List of acct_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list);

/* 
 * add associations to accounting system 
 * IN:  association_list List of acct_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_associations(void *db_conn, uint32_t uid, 
					   List association_list);

/* 
 * add qos's to accounting system 
 * IN:  qos_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_qos(void *db_conn, uint32_t uid, 
				  List qos_list);

/* 
 * add wckey's to accounting system 
 * IN:  wckey_list List of acct_wckey_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_wckeys(void *db_conn, uint32_t uid, 
				     List wckey_list);

/* 
 * add reservation's in accounting system 
 * IN:  acct_reservation_rec_t *resv reservation to be added.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_add_reservation(void *db_conn, 
					  acct_reservation_rec_t *resv);

/* 
 * modify existing users in the accounting system 
 * IN:  acct_user_cond_t *user_cond
 * IN:  acct_user_rec_t *user
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_users(void *db_conn, uint32_t uid, 
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user);

/* 
 * modify existing accounts in the accounting system 
 * IN:  acct_acct_cond_t *acct_cond
 * IN:  acct_account_rec_t *acct
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_accounts(void *db_conn, uint32_t uid, 
					   acct_account_cond_t *acct_cond,
					   acct_account_rec_t *acct);

/* 
 * modify existing clusters in the accounting system 
 * IN:  acct_cluster_cond_t *cluster_cond
 * IN:  acct_cluster_rec_t *cluster
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_clusters(void *db_conn, uint32_t uid, 
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster);

/* 
 * modify existing associations in the accounting system 
 * IN:  acct_association_cond_t *assoc_cond
 * IN:  acct_association_rec_t *assoc
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_associations(
	void *db_conn, uint32_t uid, 
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc);

/* 
 * modify existing qos in the accounting system 
 * IN:  acct_qos_cond_t *qos_cond
 * IN:  acct_qos_rec_t *qos
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_qos(void *db_conn, uint32_t uid, 
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos);

/* 
 * modify existing wckey in the accounting system 
 * IN:  acct_wckey_cond_t *wckey_cond
 * IN:  acct_wckey_rec_t *wckey
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_modify_wckeys(void *db_conn, uint32_t uid, 
					 acct_wckey_cond_t *wckey_cond,
					 acct_wckey_rec_t *wckey);

/* 
 * modify reservation's in accounting system 
 * IN:  acct_reservation_rec_t *resv 
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_modify_reservation(void *db_conn, 
					     acct_reservation_rec_t *resv);
/* 
 * remove users from accounting system 
 * IN:  acct_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_users(void *db_conn, uint32_t uid, 
					acct_user_cond_t *user_cond);

/* 
 * remove users from being a coordinator of an account
 * IN: acct_list list of char *'s of names of accounts
 * IN: acct_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_coord(void *db_conn, uint32_t uid, 
					List acct_list,
					acct_user_cond_t *user_cond);

/* 
 * remove accounts from accounting system 
 * IN:  acct_account_cond_t *acct_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_accounts(void *db_conn, uint32_t uid, 
					   acct_account_cond_t *acct_cond);

/* 
 * remove clusters from accounting system 
 * IN:  acct_cluster_cond_t *cluster_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_clusters(void *db_conn, uint32_t uid, 
					   acct_cluster_cond_t *cluster_cond);

/* 
 * remove associations from accounting system 
 * IN:  acct_association_cond_t *assoc_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_associations(
	void *db_conn, uint32_t uid, acct_association_cond_t *assoc_cond);

/* 
 * remove qos from accounting system 
 * IN:  acct_qos_cond_t *assoc_qos
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_qos(
	void *db_conn, uint32_t uid, acct_qos_cond_t *qos_cond);

/* 
 * remove wckey from accounting system 
 * IN:  acct_wckey_cond_t *assoc_wckey
 * RET: List containing (char *'s) else NULL on error
 */
extern List acct_storage_g_remove_wckeys(
	void *db_conn, uint32_t uid, acct_wckey_cond_t *wckey_cond);

/* 
 * remove reservation's in accounting system 
 * IN:  acct_reservation_rec_t *resv 
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_remove_reservation(void *db_conn, 
					     acct_reservation_rec_t *resv);
/* 
 * get info from the storage 
 * IN:  acct_user_cond_t *
 * IN:  params void *
 * returns List of acct_user_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_users(void *db_conn,  uint32_t uid,
				     acct_user_cond_t *user_cond);

/* 
 * get info from the storage 
 * IN:  acct_account_cond_t *
 * IN:  params void *
 * returns List of acct_account_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_accounts(void *db_conn,  uint32_t uid,
					acct_account_cond_t *acct_cond);

/* 
 * get info from the storage 
 * IN:  acct_cluster_cond_t *
 * IN:  params void *
 * returns List of acct_cluster_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_clusters(
	void *db_conn, uint32_t uid, acct_cluster_cond_t *cluster_cond);


/* 
 * get info from the storage 
 * RET: List of config_key_pairs_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_config(void *db_conn);

/* 
 * get info from the storage 
 * IN:  acct_association_cond_t *
 * RET: List of acct_association_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_associations(
	void *db_conn, uint32_t uid, acct_association_cond_t *assoc_cond);

/* 
 * get info from the storage 
 * IN:  acct_association_cond_t *
 * RET: List of acct_association_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_problems(
	void *db_conn, uint32_t uid, acct_association_cond_t *assoc_cond);

/* 
 * get info from the storage 
 * IN:  acct_qos_cond_t *
 * RET: List of acct_qos_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_qos(void *db_conn, uint32_t uid,
				   acct_qos_cond_t *qos_cond);

/* 
 * get info from the storage 
 * IN:  acct_wckey_cond_t *
 * RET: List of acct_wckey_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_wckeys(void *db_conn, uint32_t uid,
				      acct_wckey_cond_t *wckey_cond);

/* 
 * get info from the storage 
 * IN:  acct_reservation_cond_t *
 * RET: List of acct_reservation_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_reservations(void *db_conn, uint32_t uid,
					    acct_reservation_cond_t *resv_cond);

/* 
 * get info from the storage 
 * IN:  acct_txn_cond_t *
 * RET: List of acct_txn_rec_t *
 * note List needs to be freed when called
 */
extern List acct_storage_g_get_txn(void *db_conn,  uint32_t uid,
				   acct_txn_cond_t *txn_cond);

/* 
 * get info from the storage 
 * IN/OUT:  in void * (acct_association_rec_t *) or
 *          (acct_wckey_rec_t *) with the id set
 * IN:  type what type is 'in'
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <=
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_get_usage(
	void *db_conn,  uint32_t uid, void *in, int type,
	time_t start, time_t end);
/* 
 * roll up data in the storage 
 * IN: sent_start (option time to do a re-roll or start from this point)
 * IN: sent_end (option time to do a re-roll or end at this point)
 * IN: archive_data (if 0 old data is not archived in a monthly rollup)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_roll_usage(void *db_conn, 
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data);
/* 
 * record shares used information for backup in case slurmctld restarts 
 * IN:  account_list List of shares_used_object_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_update_shares_used(void *db_conn, List acct_list);

/* 
 * This should be called when a cluster does a cold start to flush out
 * any jobs that were running during the restart so we don't have any
 * jobs in the database "running" forever since no endtime will be
 * placed in there other wise. 
 * IN:  char * = cluster name
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_flush_jobs_on_cluster(
	void *db_conn, char *cluster, time_t event_time);

/*********************** CLUSTER ACCOUNTING STORAGE **************************/

extern int clusteracct_storage_g_node_down(void *db_conn, 
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason);

extern int clusteracct_storage_g_node_up(void *db_conn, 
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time);

extern int clusteracct_storage_g_cluster_procs(void *db_conn, 
					       char *cluster,
					       char *cluster_nodes,
					       uint32_t procs,
					       time_t event_time);

extern int clusteracct_storage_g_register_ctld(
	void *db_conn, char *cluster, uint16_t port);

/* 
 * get info from the storage 
 * IN/OUT:  cluster_rec void * (acct_cluster_rec_t *) with the name set
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <
 * IN:  params void *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int clusteracct_storage_g_get_usage(
	void *db_conn, uint32_t uid, void *cluster_rec, int type,
	time_t start, time_t end);

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_g_job_start (void *db_conn, char *cluster_name,
					struct job_record *job_ptr);

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_g_job_complete (void *db_conn, 
					   struct job_record *job_ptr);

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_g_step_start (void *db_conn, 
					 struct step_record *step_ptr);

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_g_step_complete (void *db_conn, 
					    struct step_record *step_ptr);

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_g_job_suspend (void *db_conn, 
					  struct job_record *job_ptr);

/* 
 * get info from the storage 
 * returns List of jobacct_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_g_get_jobs_cond(void *db_conn, uint32_t uid, 
					    acct_job_cond_t *job_cond);

/* 
 * expire old info from the storage 
 */
extern int jobacct_storage_g_archive(void *db_conn, 
				     acct_archive_cond_t *arch_cond);

/* 
 * expire old info from the storage 
 */
extern int jobacct_storage_g_archive_load(void *db_conn, 
					  acct_archive_rec_t *arch_rec);

#endif /*_SLURM_ACCOUNTING_STORAGE_H*/
