/*****************************************************************************\
 *  slurmdb.h - Interface codes and functions for slurm
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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
#ifndef _SLURMDB_H
#define _SLURMDB_H

/* BEGIN_C_DECLS should be used at the beginning of your declarations,
   so that C++ compilers don't mangle their names.  Use _END_C_DECLS at
   the end of C declarations. */
#undef BEGIN_C_DECLS
#undef END_C_DECLS
#ifdef __cplusplus
# define BEGIN_C_DECLS	extern "C" {
# define END_C_DECLS	}
#else
# define BEGIN_C_DECLS	/* empty */
# define END_C_DECLS	/* empty */
#endif

/* PARAMS is a macro used to wrap function prototypes, so that compilers
   that don't understand ANSI C prototypes still work, and ANSI C
   compilers can issue warnings about type mismatches.  */
#undef PARAMS
#if defined (__STDC__) || defined (_AIX)			\
	|| (defined (__mips) && defined (_SYSTYPE_SVR4))	\
	|| defined(WIN32) || defined(__cplusplus)
# define PARAMS(protos)	protos
#else
# define PARAMS(protos)	()
#endif

BEGIN_C_DECLS

#include <slurm/slurm.h>

typedef enum {
	SLURMDB_ADMIN_NOTSET,
	SLURMDB_ADMIN_NONE,
	SLURMDB_ADMIN_OPERATOR,
	SLURMDB_ADMIN_SUPER_USER
} slurmdb_admin_level_t;

typedef enum {
	SLURMDB_UPDATE_NOTSET,
	SLURMDB_ADD_USER,
	SLURMDB_ADD_ASSOC,
	SLURMDB_ADD_COORD,
	SLURMDB_MODIFY_USER,
	SLURMDB_MODIFY_ASSOC,
	SLURMDB_REMOVE_USER,
	SLURMDB_REMOVE_ASSOC,
	SLURMDB_REMOVE_COORD,
	SLURMDB_ADD_QOS,
	SLURMDB_REMOVE_QOS,
	SLURMDB_MODIFY_QOS,
	SLURMDB_ADD_WCKEY,
	SLURMDB_REMOVE_WCKEY,
	SLURMDB_MODIFY_WCKEY,
} slurmdb_update_type_t;

typedef enum {
	SLURMDB_CLASS_NONE, /* no class given */
	SLURMDB_CLASS_CAPABILITY, /* capability cluster */
	SLURMDB_CLASS_CAPACITY, /* capacity cluster */
	SLURMDB_CLASS_CAPAPACITY, /* a cluster that is both capability
				   * and capacity */
} slurmdb_classification_type_t;

typedef enum {
	SLURMDB_PROBLEM_NOT_SET,
	SLURMDB_PROBLEM_ACCT_NO_ASSOC,
	SLURMDB_PROBLEM_ACCT_NO_USERS,
	SLURMDB_PROBLEM_USER_NO_ASSOC,
	SLURMDB_PROBLEM_USER_NO_UID,
} slurmdb_problem_type_t;

typedef enum {
	SLURMDB_EVENT_ALL,
	SLURMDB_EVENT_CLUSTER,
	SLURMDB_EVENT_NODE
} slurmdb_event_type_t;



#define SLURMDB_CLASSIFIED_FLAG 0x0100
#define SLURMDB_CLASS_BASE      0x00ff

/* job info structures */
typedef struct {
	double cpu_ave;
	uint32_t cpu_min;
	uint32_t cpu_min_nodeid; /* contains which node number it was on */
	uint16_t cpu_min_taskid; /* contains which task number it was on */
	double pages_ave;
	uint32_t pages_max;
	uint32_t pages_max_nodeid; /* contains which node number it was on */
	uint16_t pages_max_taskid; /* contains which task number it was on */
	double rss_ave;
	uint32_t rss_max;
	uint32_t rss_max_nodeid; /* contains which node number it was on */
	uint16_t rss_max_taskid; /* contains which task number it was on */
	double vsize_ave;
	uint32_t vsize_max;
	uint32_t vsize_max_nodeid; /* contains which node number it was on */
	uint16_t vsize_max_taskid; /* contains which task number it was on */
} slurmdb_stats_t;

typedef struct {
	uint32_t alloc_cpus;
	uint32_t alloc_nodes;
	char    *account;
	uint32_t associd;
	char	*blockid;
	char    *cluster;
	uint32_t elapsed;
	time_t eligible;
	time_t end;
	int32_t	exitcode;
	void *first_step_ptr; /* this pointer to a jobacct_step_rec_t
				 is set up on the
				 client side so does not need to
				 be packed */
	uint32_t gid;
	uint32_t jobid;
	char	*jobname;
	uint32_t lft;
	char	*partition;
	char	*nodes;
	uint32_t priority;
	uint16_t qos;
	uint32_t req_cpus;
	uint32_t requid;
	uint32_t resvid;
	uint32_t show_full;
	time_t start;
	enum job_states	state;
	slurmdb_stats_t stats;
	List    steps; /* list of jobacct_step_rec_t *'s */
	time_t submit;
	uint32_t suspended;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint32_t timelimit;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	uint16_t track_steps;
	uint32_t uid;
	char    *user;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
	char    *wckey;
	uint32_t wckeyid;
} slurmdb_job_rec_t;

typedef struct {
	uint32_t elapsed;
	time_t end;
	int32_t exitcode;
	slurmdb_job_rec_t *job_ptr; /* this pointer is set up on the
				       client side so does not need to
				       be packed */
	uint32_t ncpus;
	uint32_t nnodes;
	char *nodes;
	uint32_t ntasks;
	uint32_t requid;
	time_t start;
	enum job_states	state;
	slurmdbd_stats_t stats;
	uint32_t stepid;	/* job's step number */
	char *stepname;
	uint32_t suspended;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint16_t task_dist;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
} slurmdb_step_rec_t;

typedef struct {
	uint32_t jobid;
	uint32_t stepid;
} slurmdb_selected_step_t;

/********************************************/



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
} slurmdb_association_cond_t;

typedef struct {
	slurmdb_association_cond_t *assoc_cond;/* use acct_list here for
						  names */
	List description_list; /* list of char * */
	List organization_list; /* list of char * */
	uint16_t with_assocs;
	uint16_t with_coords;
	uint16_t with_deleted;
} slurmdb_account_cond_t;

typedef struct {
	List assoc_list; /* list of slurmdb_association_rec_t *'s */
	List coordinators; /* list of slurmdb_coord_rec_t *'s */
	char *description;
	char *name;
	char *organization;
} slurmdb_account_rec_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	uint32_t id;	/* association/wckey ID		*/
	time_t period_start; /* when this record was started */
} slurmdb_accounting_rec_t;

typedef struct slurmdb_association_rec {
	List accounting_list; 	/* list of slurmdb_accounting_rec_t *'s */
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
	struct slurmdb_association_rec *parent_assoc_ptr; /* ptr to parent acct
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
} slurmdb_association_rec_t;

typedef struct {
	uint16_t classification; /* how this machine is classified */
	List cluster_list; /* list of char * */
	time_t usage_end;
	time_t usage_start;
	uint16_t with_deleted;
	uint16_t with_usage;
} slurmdb_cluster_cond_t;

typedef struct {
	List accounting_list; /* list of cluster_accounting_rec_t *'s */
	uint16_t classification; /* how this machine is classified */
	char *control_host;
	uint32_t control_port;
	uint32_t cpu_count;
	char *name;
	char *nodes;
	slurmdb_association_rec_t *root_assoc; /* root association for cluster */
	uint16_t rpc_version; /* version of rpc this cluter is running */
} slurmdb_cluster_rec_t;

typedef struct {
	char *name;
	uint16_t direct;
} slurmdb_coord_rec_t;

typedef struct {
	List cluster_list;	/* list of char * */
	uint32_t cpus_max;      /* number of cpus high range */
	uint32_t cpus_min;      /* number of cpus low range */
	uint16_t event_type;    /* type of events (slurmdb_event_type_t),
				 * default is all */
	List node_list;	        /* list of char * */
	time_t period_end;      /* period end of events */
	time_t period_start;    /* period start of events */
	List reason_list;       /* list of char * */
	List reason_uid_list;   /* list of char * */
	List state_list;        /* list of char * */
} slurmdb_event_cond_t;

typedef struct {
	char *cluster;          /* Name of associated cluster */
	char *cluster_nodes;    /* node list in cluster during time
				 * period (only set in a cluster event) */
	uint32_t cpu_count;     /* Number of CPUs effected by event */
	char *node_name;        /* Name of node (only set in a node event) */
	time_t period_end;      /* End of period */
	time_t period_start;    /* Start of period */
	char *reason;           /* reason node is in state during time
				   period (only set in a node event) */
	uint32_t reason_uid;    /* uid of that who set the reason */
	uint16_t state;         /* State of node during time
				   period (only set in a node event) */
} slurmdb_event_rec_t;

typedef struct {
	List acct_list;		/* list of char * */
	List associd_list;	/* list of char */
	List cluster_list;	/* list of char * */
	uint32_t cpus_max;      /* number of cpus high range */
	uint32_t cpus_min;      /* number of cpus low range */
	uint16_t duplicates;    /* report duplicate job entries */
	List groupid_list;	/* list of char * */
	uint32_t nodes_max;     /* number of nodes high range */
	uint32_t nodes_min;     /* number of nodes low range */
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
} slurmdb_job_cond_t;

typedef struct {
	char *description;
	uint32_t id;
	List job_list; /* list of job pointers to submitted/running
			  jobs (DON'T PACK) */

	uint64_t grp_cpu_mins; /* max number of cpu minutes all jobs
				* running under this qos can run for */
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

	uint64_t max_cpu_mins_pj; /* max number of cpu mins a user can
				   * use with this qos */
	uint32_t max_cpus_pj; /* max number of cpus a job can
			       * allocate with this qos */
	uint32_t max_jobs_pu;	/* max number of jobs a user can
				 * run with this qos at one time */
	uint32_t max_nodes_pj; /* max number of nodes a job can
				* allocate with this qos at one time */
	uint32_t max_submit_jobs_pu; /* max number of jobs a user can
					submit with this qos at once */
	uint32_t max_wall_pj; /* longest time this
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

	List user_limit_list; /* slurmdb_used_limits_t's (DON'T PACK) */
} slurmdb_qos_rec_t;

typedef struct {
	List description_list; /* list of char * */
	List id_list; /* list of char * */
	List name_list; /* list of char * */
	uint16_t with_deleted;
} slurmdb_qos_cond_t;

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
} slurmdb_reservation_cond_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	char *assocs; /* comma separated list of associations */
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
} slurmdb_reservation_rec_t;

/* Right now this is used in the slurmdb_qos_rec_t structure.  In the
 * user_limit_list. */
typedef struct {
	uint32_t jobs;	/* count of active jobs */
	uint32_t submit_jobs; /* count of jobs pending or running */
	uint32_t uid;
} slurmdb_used_limits_t;

typedef struct {
	uint16_t admin_level; /* really slurmdb_admin_level_t but for
				 packing purposes needs to be uint16_t */
	slurmdb_association_cond_t *assoc_cond; /* use user_list here for
						   names */
	List def_slurmdb_list; /* list of char * */
	List def_wckey_list; /* list of char * */
	uint16_t with_assocs;
	uint16_t with_coords;
	uint16_t with_deleted;
	uint16_t with_wckeys;
} slurmdb_user_cond_t;

/* If there is something that can be altered here it will need to
 * added as something to check for when modifying a user since a user
 * can modify there default account, and default wckey but nothing else in
 * src/slurmdbd/proc_req.c.
 */
typedef struct {
	uint16_t admin_level; /* really slurmdb_admin_level_t but for
				 packing purposes needs to be uint16_t */
	List assoc_list; /* list of slurmdb_association_rec_t *'s */
	List coord_accts; /* list of slurmdb_coord_rec_t *'s */
	char *default_acct;
	char *default_wckey;
	char *name;
	uint32_t uid;
	List wckey_list; /* list of slurmdb_wckey_rec_t *'s */
} slurmdb_user_rec_t;

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
} slurmdb_txn_cond_t;

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
} slurmdb_txn_rec_t;

typedef struct {
	List objects; /* depending on type */
	uint16_t type; /* really slurmdb_update_type_t but for
			* packing purposes needs to be a
			* uint16_t */
} slurmdb_update_object_t;

typedef struct {
	List cluster_list;	/* list of char * */
	List id_list;		/* list of char * */

	List name_list;        /* list of char * */

	time_t usage_end;
	time_t usage_start;

	List user_list;		/* list of char * */

	uint16_t with_usage;  /* fill in usage */
	uint16_t with_deleted; /* return deleted associations */
} slurmdb_wckey_cond_t;

typedef struct {
	List accounting_list; 	/* list of slurmdb_accounting_rec_t *'s */

	char *cluster;		/* cluster associated */

	uint32_t id;		/* id identifing a combination of
				 * user-wckey-cluster */
	char *name;		/* wckey name */
	uint32_t uid;		/* user ID */

	char *user;		/* user associated */
} slurmdb_wckey_rec_t;

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
} slurmdb_print_tree_t;

typedef struct {
	slurmdb_association_rec_t *assoc;
	char *sort_name;
	List childern;
} slurmdb_hierarchical_rec_t;

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
	slurmdb_job_cond_t *job_cond; /* conditions for the jobs to archive */
	uint16_t purge_event; /* purge events older than this in months */
	uint16_t purge_job; /* purge jobs older than this in months */
	uint16_t purge_step; /* purge steps older than this in months */
	uint16_t purge_suspend; /* purge suspend data older than this
				 * in months */
} slurmdb_archive_cond_t;

typedef struct {
	char *archive_file;  /* archive file containing data that was
				once flushed from the database */
	char *insert;     /* an sql statement to be ran containing the
			     insert of jobs since past */
} slurmdb_archive_rec_t;

/************** account functions **************/

/*
 * add accounts to accounting system
 * IN:  account_list List of slurmdb_account_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_accounts_add(void *db_conn, List acct_list);

/*
 * get info from the storage
 * IN:  slurmdb_account_cond_t *
 * IN:  params void *
 * returns List of slurmdb_account_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_accounts_get(void *db_conn,
				  slurmdb_account_cond_t *acct_cond);

/*
 * modify existing accounts in the accounting system
 * IN:  slurmdb_acct_cond_t *acct_cond
 * IN:  slurmdb_account_rec_t *acct
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_accounts_modify(void *db_conn,
				     slurmdb_account_cond_t *acct_cond,
				     slurmdb_account_rec_t *acct);

/*
 * remove accounts from accounting system
 * IN:  slurmdb_account_cond_t *acct_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_accounts_remove(void *db_conn,
				     slurmdb_account_cond_t *acct_cond);


/************** archive functions **************/

/*
 * expire old info from the storage
 */
extern int slurmdb_archive(void *db_conn, slurmdb_archive_cond_t *arch_cond);

/*
 * expire old info from the storage
 */
extern int slurmdbd_archive_load(void *db_conn,
				 slurmdb_archive_rec_t *arch_rec);


/************** association functions **************/

/*
 * add associations to accounting system
 * IN:  association_list List of slurmdb_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_associations_add(void *db_conn, List association_list);

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_associations_get(void *db_conn,
				      slurmdb_association_cond_t *assoc_cond);

/*
 * modify existing associations in the accounting system
 * IN:  slurmdb_association_cond_t *assoc_cond
 * IN:  slurmdb_association_rec_t *assoc
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_associations_modify(void *db_conn,
					 slurmdb_association_cond_t *assoc_cond,
					 slurmdb_association_rec_t *assoc);

/*
 * remove associations from accounting system
 * IN:  slurmdb_association_cond_t *assoc_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_associations_remove(
	void *db_conn, slurmdb_association_cond_t *assoc_cond);

/************** cluster functions **************/

/*
 * add clusters to accounting system
 * IN:  cluster_list List of slurmdb_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_clusters_add(void *db_conn, List cluster_list);

/*
 * get info from the storage
 * IN:  slurmdb_cluster_cond_t *
 * IN:  params void *
 * returns List of slurmdb_cluster_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_clusters_get(void *db_conn,
				  slurmdb_cluster_cond_t *cluster_cond);

/*
 * modify existing clusters in the accounting system
 * IN:  slurmdb_cluster_cond_t *cluster_cond
 * IN:  slurmdb_cluster_rec_t *cluster
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_clusters_modify(void *db_conn,
				     slurmdb_cluster_cond_t *cluster_cond,
				     slurmdb_cluster_rec_t *cluster);

/*
 * remove clusters from accounting system
 * IN:  slurmdb_cluster_cond_t *cluster_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_clusters_remove(void *db_conn,
				     slurmdb_cluster_cond_t *cluster_cond);

/************** cluster report functions **************/


/************** connection functions **************/

/*
 * get a new connection to the slurmdb
 * RET: pointer used to access db
 */
extern void *slurmdbd_connection_get();
/*
 * release connection to the storage unit
 * IN/OUT: void ** pointer returned from
 *         slurmdbd_connection_get() which will be freed.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_connection_close(void **db_conn);


/************** coordinator functions **************/

/*
 * add users as account coordinators
 * IN: acct_list list of char *'s of names of accounts
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_coord_add(void *db_conn, List acct_list,
			      slurmdb_user_cond_t *user_cond);

/*
 * remove users from being a coordinator of an account
 * IN: acct_list list of char *'s of names of accounts
 * IN: slurmdb_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_coord_remove(void *db_conn, List acct_list,
				  slurmdb_user_cond_t *user_cond);

/************** extra get functions **************/

/*
 * get info from the storage
 * RET: List of config_key_pairs_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_config_get(void *db_conn);

/*
 * get info from the storage
 * IN:  slurmdb_event_cond_t *
 * RET: List of slurmdb_event_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_events_get(void *db_conn,
				slurmdb_event_cond_t *event_cond);

/*
 * get info from the storage
 * returns List of slurmdb_job_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_jobs_get(void *db_conn, slurmdb_job_cond_t *job_cond);

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_problems_get(void *db_conn,
				  slurmdb_association_cond_t *assoc_cond);

/*
 * get info from the storage
 * IN:  slurmdb_reservation_cond_t *
 * RET: List of slurmdb_reservation_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_reservations_get(void *db_conn,
				      slurmdb_reservation_cond_t *resv_cond);

/*
 * get info from the storage
 * IN:  slurmdb_txn_cond_t *
 * RET: List of slurmdb_txn_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_txn_get(void *db_conn, slurmdb_txn_cond_t *txn_cond);


/************** helper functions **************/

extern void slurmdb_init_association_rec(slurmdb_association_rec_t *assoc);
extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos);

/* The next two functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List slurmdb_get_hierarchical_sorted_assoc_list(List assoc_list);
extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list);


/* IN/OUT: tree_list a list of acct_print_tree_t's */
extern char *slurmdb_get_tree_acct_name(char *name, char *parent,
					List tree_list);

/************** job report functions **************/


/************** qos functions **************/

/*
 * add qos's to accounting system
 * IN:  qos_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_qos_add(void *db_conn, uint32_t uid, List qos_list);

/*
 * get info from the storage
 * IN:  slurmdb_qos_cond_t *
 * RET: List of slurmdb_qos_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_qos_get(void *db_conn, slurmdb_qos_cond_t *qos_cond);

/*
 * modify existing qos in the accounting system
 * IN:  slurmdb_qos_cond_t *qos_cond
 * IN:  slurmdb_qos_rec_t *qos
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_qos_modify(void *db_conn,
				slurmdb_qos_cond_t *qos_cond,
				slurmdb_qos_rec_t *qos);
/*
 * remove qos from accounting system
 * IN:  slurmdb_qos_cond_t *assoc_qos
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_qos_remove(void *db_conn, slurmdb_qos_cond_t *qos_cond);

/************** usage functions **************/

/*
 * get info from the storage
 * IN/OUT:  in void * (slurmdb_association_rec_t *) or
 *          (slurmdb_wckey_rec_t *) of (slurmdb_cluster_rec_t *) with
 *          the id, and cluster set.
 * IN:  type what type is 'in'
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <=
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_usage_get(void *db_conn,  void *in, int type,
			      time_t start, time_t end);

/*
 * roll up data in the storage
 * IN: sent_start (option time to do a re-roll or start from this point)
 * IN: sent_end (option time to do a re-roll or end at this point)
 * IN: archive_data (if 0 old data is not archived in a monthly rollup)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_usage_roll(void *db_conn,
			       time_t sent_start, time_t sent_end,
			       uint16_t archive_data);

/************** user functions **************/

/*
 * add users to accounting system
 * IN:  user_list List of slurmdb_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_users_add(void *db_conn, List user_list);

/*
 * get info from the storage
 * IN:  slurmdb_user_cond_t *
 * IN:  params void *
 * returns List of slurmdb_user_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_users_get(void *db_conn, slurmdb_user_cond_t *user_cond);

/*
 * modify existing users in the accounting system
 * IN:  slurmdb_user_cond_t *user_cond
 * IN:  slurmdb_user_rec_t *user
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_users_modify(void *db_conn,
				  slurmdb_user_cond_t *user_cond,
				  slurmdb_user_rec_t *user);
/*
 * remove users from accounting system
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_users_remove(void *db_conn,
				  slurmdb_user_cond_t *user_cond);


/************** user report functions **************/


/************** wckey functions **************/

/*
 * add wckey's to accounting system
 * IN:  wckey_list List of slurmdb_wckey_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdbd_wckeys_add(void *db_conn, List wckey_list);

/*
 * get info from the storage
 * IN:  slurmdb_wckey_cond_t *
 * RET: List of slurmdb_wckey_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdbd_wckeys_get(void *db_conn,
				slurmdb_wckey_cond_t *wckey_cond);

/*
 * modify existing wckey in the accounting system
 * IN:  slurmdb_wckey_cond_t *wckey_cond
 * IN:  slurmdb_wckey_rec_t *wckey
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_wckeys_modify(void *db_conn,
				   slurmdb_wckey_cond_t *wckey_cond,
				   slurmdb_wckey_rec_t *wckey);

/*
 * remove wckey from accounting system
 * IN:  slurmdb_wckey_cond_t *assoc_wckey
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdbd_wckeys_remove(void *db_conn,
				   slurmdb_wckey_cond_t *wckey_cond);

END_C_DECLS

#endif /* !_SLURMDB_H */
