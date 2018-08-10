/*****************************************************************************\
 *  slurmdb.h - Interface codes and functions for slurm
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _SLURMDB_H
#define _SLURMDB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <slurm/slurm.h>

typedef enum {
	SLURMDB_ADMIN_NOTSET,
	SLURMDB_ADMIN_NONE,
	SLURMDB_ADMIN_OPERATOR,
	SLURMDB_ADMIN_SUPER_USER
} slurmdb_admin_level_t;

typedef enum {
	SLURMDB_CLASS_NONE, /* no class given */
	SLURMDB_CLASS_CAPABILITY, /* capability cluster */
	SLURMDB_CLASS_CAPACITY, /* capacity cluster */
	SLURMDB_CLASS_CAPAPACITY, /* a cluster that is both capability
				   * and capacity */
} slurmdb_classification_type_t;

typedef enum {
	SLURMDB_EVENT_ALL,
	SLURMDB_EVENT_CLUSTER,
	SLURMDB_EVENT_NODE
} slurmdb_event_type_t;

typedef enum {
	SLURMDB_PROBLEM_NOT_SET,
	SLURMDB_PROBLEM_ACCT_NO_ASSOC,
	SLURMDB_PROBLEM_ACCT_NO_USERS,
	SLURMDB_PROBLEM_USER_NO_ASSOC,
	SLURMDB_PROBLEM_USER_NO_UID,
} slurmdb_problem_type_t;

typedef enum {
	SLURMDB_REPORT_SORT_TIME,
	SLURMDB_REPORT_SORT_NAME
} slurmdb_report_sort_t;

typedef enum {
	SLURMDB_REPORT_TIME_SECS,
	SLURMDB_REPORT_TIME_MINS,
	SLURMDB_REPORT_TIME_HOURS,
	SLURMDB_REPORT_TIME_PERCENT,
	SLURMDB_REPORT_TIME_SECS_PER,
	SLURMDB_REPORT_TIME_MINS_PER,
	SLURMDB_REPORT_TIME_HOURS_PER,
} slurmdb_report_time_format_t;

typedef enum {
	SLURMDB_RESOURCE_NOTSET,
	SLURMDB_RESOURCE_LICENSE
} slurmdb_resource_type_t;

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
	SLURMDB_ADD_CLUSTER,
	SLURMDB_REMOVE_CLUSTER,
	SLURMDB_REMOVE_ASSOC_USAGE,
	SLURMDB_ADD_RES,
	SLURMDB_REMOVE_RES,
	SLURMDB_MODIFY_RES,
	SLURMDB_REMOVE_QOS_USAGE,
	SLURMDB_ADD_TRES,
	SLURMDB_UPDATE_FEDS,
} slurmdb_update_type_t;

/* Define QOS flags */
#define	QOS_FLAG_BASE                0x0fffffff
#define	QOS_FLAG_NOTSET              0x10000000
#define	QOS_FLAG_ADD                 0x20000000
#define	QOS_FLAG_REMOVE              0x40000000

#define	QOS_FLAG_PART_MIN_NODE       0x00000001
#define	QOS_FLAG_PART_MAX_NODE       0x00000002
#define	QOS_FLAG_PART_TIME_LIMIT     0x00000004
#define	QOS_FLAG_ENFORCE_USAGE_THRES 0x00000008
#define	QOS_FLAG_NO_RESERVE          0x00000010
#define	QOS_FLAG_REQ_RESV            0x00000020
#define	QOS_FLAG_DENY_LIMIT          0x00000040
#define	QOS_FLAG_OVER_PART_QOS       0x00000080
#define	QOS_FLAG_NO_DECAY            0x00000100

/* Define Server Resource flags */
#define	SLURMDB_RES_FLAG_BASE        0x0fffffff /* apply to get real flags */
#define	SLURMDB_RES_FLAG_NOTSET      0x10000000
#define	SLURMDB_RES_FLAG_ADD         0x20000000
#define	SLURMDB_RES_FLAG_REMOVE      0x40000000

/* Define Federation flags */
#define	FEDERATION_FLAG_BASE           0x0fffffff
#define	FEDERATION_FLAG_NOTSET         0x10000000
#define	FEDERATION_FLAG_ADD            0x20000000
#define	FEDERATION_FLAG_REMOVE         0x40000000

#define SLURMDB_MODIFY_NO_WAIT       0x00000001

/* SLURM CLUSTER FEDERATION STATES */
enum cluster_fed_states {
	CLUSTER_FED_STATE_NA,
	CLUSTER_FED_STATE_ACTIVE,
	CLUSTER_FED_STATE_INACTIVE
};
#define CLUSTER_FED_STATE_BASE       0x000f
#define CLUSTER_FED_STATE_FLAGS      0xfff0
#define CLUSTER_FED_STATE_DRAIN      0x0010 /* drain cluster by not accepting
					       any new jobs and waiting for all
					       federated jobs to complete.*/
#define CLUSTER_FED_STATE_REMOVE     0x0020 /* remove cluster from federation
					       once cluster is drained of
					       federated jobs */

/* flags and types of resources */
/* when we come up with some */

/* Slurm job condition flags */
#define JOBCOND_FLAG_DUP      0x00000001 /* Report duplicate job entries */
#define JOBCOND_FLAG_NO_STEP  0x00000002 /* Don't report job step info */
#define JOBCOND_FLAG_NO_TRUNC 0x00000004 /* Report info. without truncating
					  * the time to the usage_start and
					  * usage_end */
#define JOBCOND_FLAG_RUNAWAY  0x00000008 /* Report runaway jobs only */
#define JOBCOND_FLAG_WHOLE_HETJOB    0x00000010 /* Report info about all hetjob
						 * components
						 */
#define JOBCOND_FLAG_NO_WHOLE_HETJOB 0x00000020 /* Only report info about
						 * requested hetjob components
						 */

/* Archive / Purge time flags */
#define SLURMDB_PURGE_BASE    0x0000ffff   /* Apply to get the number
					    * of units */
#define SLURMDB_PURGE_FLAGS   0xffff0000   /* apply to get the flags */
#define SLURMDB_PURGE_HOURS   0x00010000   /* Purge units are in hours */
#define SLURMDB_PURGE_DAYS    0x00020000   /* Purge units are in days */
#define SLURMDB_PURGE_MONTHS  0x00040000   /* Purge units are in months,
					    * the default */
#define SLURMDB_PURGE_ARCHIVE 0x00080000   /* Archive before purge */

/* Parent account should be used when calculating FairShare */
#define SLURMDB_FS_USE_PARENT 0x7FFFFFFF

#define SLURMDB_CLASSIFIED_FLAG 0x0100
#define SLURMDB_CLASS_BASE      0x00ff

/* Cluster flags */
#define CLUSTER_FLAG_BG     0x00000001 /* This is a bluegene cluster */
				       /* Removed v18.08 */
#define CLUSTER_FLAG_BGL    0x00000002 /* This is a bluegene/l cluster */
				       /* Removed v17.02 */
#define CLUSTER_FLAG_BGP    0x00000004 /* This is a bluegene/p cluster */
				       /* Removed v17.02 */
#define CLUSTER_FLAG_BGQ    0x00000008 /* This is a bluegene/q cluster */
				       /* Removed v18.08 */
#define CLUSTER_FLAG_SC     0x00000010 /* This is a sun constellation cluster */
				       /* Removed v16.05 */
#define CLUSTER_FLAG_XCPU   0x00000020 /* This has xcpu, removed v15.08 */
#define CLUSTER_FLAG_AIX    0x00000040 /* This is an aix cluster */
				       /* Removed v17.02 */
#define CLUSTER_FLAG_MULTSD 0x00000080 /* This cluster is multiple slurmd */
#define CLUSTER_FLAG_CRAYXT 0x00000100 /* This cluster is a ALPS cray
					* (deprecated) Same as CRAY_A */
#define CLUSTER_FLAG_CRAY_A 0x00000100 /* This cluster is a ALPS cray */
#define CLUSTER_FLAG_FE     0x00000200 /* This cluster is a front end system */
#define CLUSTER_FLAG_CRAY_N 0x00000400 /* This cluster is a Native cray */
#define CLUSTER_FLAG_FED    0x00000800 /* This cluster is in a federation. */


/* Cluster Combo flags */
#define CLUSTER_FLAG_CRAY   0x00000500 /* This cluster is a cray.
					  Combo of CRAY_A | CRAY_N */

/********************************************/

/* Association conditions used for queries of the database */

/* slurmdb_tres_rec_t is used in other structures below so this needs
 * to be declared before hand.
 */
typedef struct {
	uint64_t alloc_secs; /* total amount of secs allocated if used in an
				accounting_list */
	uint32_t rec_count;  /* number of records alloc_secs is, DON'T PACK */
	uint64_t count; /* Count of tres on a given cluster, 0 if
			   listed generically. */
	uint32_t id;    /* Database ID for the tres */
	char *name;     /* Name of tres if type is generic like GRES
			   or License. */
	char *type;     /* Type of tres (CPU, MEM, etc) */
} slurmdb_tres_rec_t;

/* slurmdb_assoc_cond_t is used in other structures below so
 * this needs to be declared first.
 */
typedef struct {
	List acct_list;		/* list of char * */
	List cluster_list;	/* list of char * */

	List def_qos_id_list;   /* list of char * */

	List format_list; 	/* list of char * */
	List id_list;		/* list of char */

	uint16_t only_defs;  /* only send back defaults */

	List parent_acct_list;	/* name of parent account */
	List partition_list;	/* list of char * */

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
} slurmdb_assoc_cond_t;

/* slurmdb_job_cond_t is used by slurmdb_archive_cond_t so it needs to
 * be defined before hand.
 */
typedef struct {
	List acct_list;		/* list of char * */
	List associd_list;	/* list of char */
	List cluster_list;	/* list of char * */
	uint32_t cpus_max;      /* number of cpus high range */
	uint32_t cpus_min;      /* number of cpus low range */
	int32_t exitcode;       /* exit code of job */
	uint32_t flags;         /* Reporting flags*/
	List format_list; 	/* list of char * */
	List groupid_list;	/* list of char * */
	List jobname_list;	/* list of char * */
	uint32_t nodes_max;     /* number of nodes high range */
	uint32_t nodes_min;     /* number of nodes low range */
	List partition_list;	/* list of char * */
	List qos_list;  	/* list of char * */
	List resv_list;		/* list of char * */
	List resvid_list;	/* list of char * */
	List state_list;        /* list of char * */
	List step_list;         /* list of slurmdb_selected_step_t */
	uint32_t timelimit_max; /* max timelimit */
	uint32_t timelimit_min; /* min timelimit */
	time_t usage_end;
	time_t usage_start;
	char *used_nodes;       /* a ranged node string where jobs ran */
	List userid_list;	/* list of char * */
	List wckey_list;	/* list of char * */
} slurmdb_job_cond_t;

/* slurmdb_stats_t needs to be defined before slurmdb_job_rec_t and
 * slurmdb_step_rec_t.
 */
typedef struct {
	double act_cpufreq;	/* contains actual average cpu frequency */
	uint64_t consumed_energy; /* contains energy consumption in joules */
	char *tres_usage_in_ave; /* average amount of usage in data */
	char *tres_usage_in_max; /* contains max amount of usage in data */
	char *tres_usage_in_max_nodeid; /* contains node number max was on */
	char *tres_usage_in_max_taskid; /* contains task number max was on */
	char *tres_usage_in_min; /* contains min amount of usage in data */
	char *tres_usage_in_min_nodeid; /* contains node number min was on */
	char *tres_usage_in_min_taskid; /* contains task number min was on */
	char *tres_usage_in_tot; /* total amount of usage in data */
	char *tres_usage_out_ave; /* average amount of usage out data */
	char *tres_usage_out_max; /* contains amount of max usage out data */
	char *tres_usage_out_max_nodeid; /* contains node number max was on */
	char *tres_usage_out_max_taskid; /* contains task number max was on */
	char *tres_usage_out_min; /* contains amount of min usage out data */
	char *tres_usage_out_min_nodeid; /* contains node number min was on */
	char *tres_usage_out_min_taskid; /* contains task number min was on */
	char *tres_usage_out_tot; /* total amount of usage out data */
} slurmdb_stats_t;

/************** alphabetical order of structures **************/

typedef struct {
	slurmdb_assoc_cond_t *assoc_cond;/* use acct_list here for
						  names */
	List description_list; /* list of char * */
	List organization_list; /* list of char * */
	uint16_t with_assocs;
	uint16_t with_coords;
	uint16_t with_deleted;
} slurmdb_account_cond_t;

typedef struct {
	List assoc_list; /* list of slurmdb_assoc_rec_t *'s */
	List coordinators; /* list of slurmdb_coord_rec_t *'s */
	char *description;
	char *name;
	char *organization;
} slurmdb_account_rec_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	uint32_t id;	/* association/wckey ID		*/
	time_t period_start; /* when this record was started */
	slurmdb_tres_rec_t tres_rec;
} slurmdb_accounting_rec_t;

typedef struct {
	char *archive_dir;     /* location to place archive file */
	char *archive_script;  /* script to run instead of default
				  actions */
	slurmdb_job_cond_t *job_cond; /* conditions for the jobs to archive */
	uint32_t purge_event; /* purge events older than this in
			       * months by default set the
			       * SLURMDB_PURGE_ARCHIVE bit for
			       * archiving */
	uint32_t purge_job; /* purge jobs older than this in months
			     * by default set the
			     * SLURMDB_PURGE_ARCHIVE bit for
			     * archiving */
	uint32_t purge_resv; /* purge reservations older than this in months
			      * by default set the
			      * SLURMDB_PURGE_ARCHIVE bit for
			      * archiving */
	uint32_t purge_step; /* purge steps older than this in months
			      * by default set the
			      * SLURMDB_PURGE_ARCHIVE bit for
			      * archiving */
	uint32_t purge_suspend; /* purge suspend data older than this
				 * in months by default set the
				 * SLURMDB_PURGE_ARCHIVE bit for
				 * archiving */
	uint32_t purge_txn; /* purge transaction data older than this
			     * in months by default set the
			     * SLURMDB_PURGE_ARCHIVE bit for
			     * archiving */
	uint32_t purge_usage; /* purge usage data older than this
			       * in months by default set the
			       * SLURMDB_PURGE_ARCHIVE bit for
			       * archiving */
} slurmdb_archive_cond_t;

typedef struct {
	char *archive_file;  /* archive file containing data that was
				once flushed from the database */
	char *insert;     /* an sql statement to be ran containing the
			     insert of jobs since past */
} slurmdb_archive_rec_t;

typedef struct {
	uint64_t count;  /* Count of tres on a given cluster, 0 if
			    listed generically. */
	List format_list;/* list of char * */
	List id_list;    /* Database ID */
	List name_list;  /* Name of tres if type is generic like GRES
			    or License. */
	List type_list;  /* Type of tres (CPU, MEM, etc) */
	uint16_t with_deleted;
} slurmdb_tres_cond_t;

/* slurmdb_tres_rec_t is defined above alphabetical */

/* slurmdb_assoc_cond_t is defined above alphabetical */

/* This has slurmdb_assoc_rec_t's in it so we define the struct afterwards. */
typedef struct slurmdb_assoc_usage slurmdb_assoc_usage_t;

typedef struct slurmdb_assoc_rec {
	List accounting_list; /* list of slurmdb_accounting_rec_t *'s */
	char *acct;		   /* account/project associated to
				    * assoc */
	struct slurmdb_assoc_rec *assoc_next; /* next assoc with
						       * same hash index
						       * based off the
						       * account/user
						       * DOESN'T GET PACKED */
	struct slurmdb_assoc_rec *assoc_next_id; /* next assoc with
							* same hash index
							* DOESN'T GET PACKED */
	char *cluster;		   /* cluster associated to association */

	uint32_t def_qos_id;       /* Which QOS id is this
				    * associations default */

	uint32_t grp_jobs;	   /* max number of jobs the
				    * underlying group of associations can run
				    * at one time */
	uint32_t grp_jobs_accrue;  /* max number of jobs the
				    * underlying group of associations can have
				    * accruing priority at one time */
	uint32_t grp_submit_jobs;  /* max number of jobs the
				    * underlying group of
				    * associations can submit at
				    * one time */
	char *grp_tres;            /* max number of cpus the
				    * underlying group of
				    * associations can allocate at one time */
	uint64_t *grp_tres_ctld;   /* grp_tres broken out in an array
				    * based off the ordering of the total
				    * number of TRES in the system
				    * (DON'T PACK) */
	char *grp_tres_mins;       /* max number of cpu minutes the
				    * underlying group of
				    * associations can run for */
	uint64_t *grp_tres_mins_ctld; /* grp_tres_mins broken out in an array
				       * based off the ordering of the total
				       * number of TRES in the system
				       * (DON'T PACK) */
	char *grp_tres_run_mins;   /* max number of cpu minutes the
				    * underlying group of
				    * assoiciations can
				    * having running at one time */
	uint64_t *grp_tres_run_mins_ctld; /* grp_tres_run_mins
					   * broken out in an array
					   * based off the ordering
					   * of the total number of TRES in
					   * the system
					   * (DON'T PACK) */
	uint32_t grp_wall;         /* total time in hours the
				    * underlying group of
				    * associations can run for */

	uint32_t id;		   /* id identifing a combination of
				    * user-account-cluster(-partition) */

	uint16_t is_def;           /* Is this the users default assoc/acct */

	uint32_t lft;		   /* lft used for grouping sub
				    * associations and jobs as a left
				    * most container used with rgt */

	uint32_t max_jobs;	   /* max number of jobs this
				    * association can run at one time */
	uint32_t max_jobs_accrue;  /* max number of jobs this association can
				    * have accruing priority time.
				    */
	uint32_t max_submit_jobs;  /* max number of jobs that can be
				      submitted by association */
	char *max_tres_mins_pj;    /* max number of cpu seconds this
				    * association can have per job */
	uint64_t *max_tres_mins_ctld; /* max_tres_mins broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */
	char *max_tres_run_mins;   /* max number of cpu minutes this
				    * association can
				    * having running at one time */
	uint64_t *max_tres_run_mins_ctld; /* max_tres_run_mins
					   * broken out in an array
					   * based off the ordering
					   * of the total number of TRES in
					   * the system
					   * (DON'T PACK) */
	char *max_tres_pj;         /* max number of cpus this
				    * association can allocate per job */
	uint64_t *max_tres_ctld;   /* max_tres broken out in an array
				    * based off the ordering of the
				    * total number of TRES in the system
				    * (DON'T PACK) */
	char *max_tres_pn;         /* max number of TRES this
				    * association can allocate per node */
	uint64_t *max_tres_pn_ctld;   /* max_tres_pn broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */
	uint32_t max_wall_pj;      /* longest time this
				    * association can run a job */

	uint32_t min_prio_thresh;  /* Don't reserve resources for pending jobs
				    * unless they have a priority equal to or
				    * higher than this. */
	char *parent_acct;	   /* name of parent account */
	uint32_t parent_id;	   /* id of parent account */
	char *partition;	   /* optional partition in a cluster
				    * associated to association */

	List qos_list;             /* list of char * */

	uint32_t rgt;		   /* rgt used for grouping sub
				    * associations and jobs as a right
				    * most container used with lft */

	uint32_t shares_raw;	   /* number of shares allocated to
				    * association */

	uint32_t uid;		   /* user ID */
	slurmdb_assoc_usage_t *usage;
	char *user;		   /* user associated to assoc */
} slurmdb_assoc_rec_t;

struct slurmdb_assoc_usage {
	uint32_t accrue_cnt;    /* Count of how many jobs I have accuring prio
				 * (DON'T PACK for state file) */
	List children_list;     /* list of children associations
				 * (DON'T PACK) */
	uint64_t *grp_used_tres; /* array of active tres counts
				  * (DON'T PACK for state file) */
	uint64_t *grp_used_tres_run_secs; /* array of running tres secs
					   * (DON'T PACK for state file) */

	double grp_used_wall;   /* group count of time used in running jobs */
	double fs_factor;	/* Fairshare factor. Not used by all algorithms
				 * (DON'T PACK for state file) */
	uint32_t level_shares;  /* number of shares on this level of
				 * the tree (DON'T PACK for state file) */

	slurmdb_assoc_rec_t *parent_assoc_ptr; /* ptr to direct
						* parent assoc
						* set in slurmctld
						* (DON'T PACK) */

	slurmdb_assoc_rec_t *fs_assoc_ptr;    /* ptr to fairshare parent
					       * assoc if fairshare
					       * == SLURMDB_FS_USE_PARENT
					       * set in slurmctld
					       * (DON'T PACK) */

	double shares_norm;     /* normalized shares
				 * (DON'T PACK for state file) */

	uint32_t tres_cnt; /* size of the tres arrays,
			    * (DON'T PACK for state file) */
	long double usage_efctv;/* effective, normalized usage
				 * (DON'T PACK for state file) */
	long double usage_norm;	/* normalized usage
				 * (DON'T PACK for state file) */
	long double usage_raw;	/* measure of TRESBillableUnits usage */

	long double *usage_tres_raw; /* measure of each TRES usage */
	uint32_t used_jobs;	/* count of active jobs
				 * (DON'T PACK for state file) */
	uint32_t used_submit_jobs; /* count of jobs pending or running
				    * (DON'T PACK for state file) */

	/* Currently FAIR_TREE systems are defining data on
	 * this struct but instead we could keep a void pointer to system
	 * specific data. This would allow subsystems to define whatever data
	 * they need without having to modify this struct; it would also save
	 * space.
	 */
	long double level_fs;	/* (FAIR_TREE) Result of fairshare equation
				 * compared to the association's siblings
				 * (DON'T PACK for state file) */

	bitstr_t *valid_qos;    /* qos available for this association
				 * derived from the qos_list.
				 * (DON'T PACK for state file) */
};

typedef struct {
	uint16_t classification; /* how this machine is classified */
	List cluster_list; /* list of char * */
	List federation_list; /* list of char */
	uint32_t flags;
	List format_list; 	/* list of char * */
	List plugin_id_select_list; /* list of char * */
	List rpc_version_list; /* list of char * */
	time_t usage_end;
	time_t usage_start;
	uint16_t with_deleted;
	uint16_t with_usage;
} slurmdb_cluster_cond_t;

typedef struct {
	List feature_list; /* list of cluster features */
	uint32_t id; /* id of cluster in federation */
	char *name; /* Federation name */
	void *recv;  /* slurm_persist_conn_t we recv information about this
		      * sibling on. (We get this information) */
	void *send; /* slurm_persist_conn_t we send information to this
		     * cluster on. (We set this information) */
	uint32_t state; /* state of cluster in federation */
	bool sync_recvd; /* true sync jobs from sib has been processed. */
	bool sync_sent;  /* true after sib sent sync jobs to sibling */
} slurmdb_cluster_fed_t;

struct slurmdb_cluster_rec {
	List accounting_list; /* list of slurmdb_cluster_accounting_rec_t *'s */
	uint16_t classification; /* how this machine is classified */
	time_t comm_fail_time;	/* avoid constant error messages. For
			         * convenience only. DOESN'T GET PACKED */
	slurm_addr_t control_addr; /* For convenience only.
				    * DOESN'T GET PACKED */
	char *control_host;
	uint32_t control_port;
	uint16_t dimensions; /* number of dimensions this cluster is */
	int *dim_size; /* For convenience only.
			* Size of each dimension For now only on
			* a bluegene cluster.  DOESN'T GET
			* PACKED, is set up in slurmdb_get_info_cluster */
	slurmdb_cluster_fed_t fed; /* Federation information */
	uint32_t flags;      /* set of CLUSTER_FLAG_* */
	pthread_mutex_t lock; /* For convenience only. DOESN"T GET PACKED */
	char *name;
	char *nodes;
	uint32_t plugin_id_select; /* id of the select plugin */
	slurmdb_assoc_rec_t *root_assoc; /* root assoc for
						* cluster */
	uint16_t rpc_version; /* version of rpc this cluter is running */
	List send_rpc;        /* For convenience only. DOESN'T GET PACKED */
	char  	*tres_str;    /* comma separated list of TRES */
};

#ifndef __slurmdb_cluster_rec_t_defined
#  define __slurmdb_cluster_rec_t_defined
typedef struct slurmdb_cluster_rec slurmdb_cluster_rec_t;
#endif

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	uint64_t down_secs; /* number of cpu seconds down */
	uint64_t idle_secs; /* number of cpu seconds idle */
	uint64_t over_secs; /* number of cpu seconds overcommitted */
	uint64_t pdown_secs; /* number of cpu seconds planned down */
	time_t period_start; /* when this record was started */
	uint64_t resv_secs; /* number of cpu seconds reserved */
	slurmdb_tres_rec_t tres_rec;
} slurmdb_cluster_accounting_rec_t;

typedef struct {
	char *cluster; /* name of cluster */
	uint16_t percent_allowed; /* percentage of total resources
				   * allowed for this cluster */
} slurmdb_clus_res_rec_t;

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
	List format_list; 	/* list of char * */
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
	uint16_t event_type;    /* type of event (slurmdb_event_type_t) */
	char *node_name;        /* Name of node (only set in a node event) */
	time_t period_end;      /* End of period */
	time_t period_start;    /* Start of period */
	char *reason;           /* reason node is in state during time
				   period (only set in a node event) */
	uint32_t reason_uid;    /* uid of that who set the reason */
	uint16_t state;         /* State of node during time
				   period (only set in a node event) */
	char *tres_str;         /* TRES touched by this event */
} slurmdb_event_rec_t;

typedef struct {
	List cluster_list; 	/* list of char * */
	List federation_list; 	/* list of char * */
	List format_list; 	/* list of char * */
	uint16_t with_deleted;
} slurmdb_federation_cond_t;

typedef struct {
	char     *name;		/* Name of federation */
	uint32_t  flags; 	/* flags to control scheduling on controller */
	List      cluster_list;	/* List of slurmdb_cluster_rec_t *'s */
} slurmdb_federation_rec_t;

/* slurmdb_job_cond_t is defined above alphabetical */


typedef struct {
	char *cluster;
	uint32_t flags;
	uint32_t job_id;
	time_t submit_time;
} slurmdb_job_modify_cond_t;

typedef struct {
	char    *account;
	char	*admin_comment;
	char	*alloc_gres;
	uint32_t alloc_nodes;
	uint32_t array_job_id;	/* job_id of a job array or 0 if N/A */
	uint32_t array_max_tasks; /* How many tasks of the array can be
				     running at one time.
				  */
	uint32_t array_task_id;	/* task_id of a job array of NO_VAL
				 * if N/A */
	char    *array_task_str; /* If pending these are the array
				    tasks this record represents.
				 */
	uint32_t associd;
	char	*blockid;
	char    *cluster;
	uint32_t derived_ec;
	char	*derived_es; /* aka "comment" */
	uint32_t elapsed;
	time_t eligible;
	time_t end;
	uint32_t exitcode;
	void *first_step_ptr;
	uint32_t gid;
	uint32_t jobid;
	char	*jobname;
	uint32_t lft;
	char 	*mcs_label;
	char	*nodes;
	char	*partition;
	uint32_t pack_job_id;
	uint32_t pack_job_offset;
	uint32_t priority;
	uint32_t qosid;
	uint32_t req_cpus;
	char	*req_gres;
	uint64_t req_mem;
	uint32_t requid;
	uint32_t resvid;
	char *resv_name;
	uint32_t show_full;
	time_t start;
	uint32_t state;
	slurmdb_stats_t stats;
	List    steps; /* list of slurmdb_step_rec_t *'s */
	time_t submit;
	uint32_t suspended;
	char	*system_comment;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint32_t timelimit;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	uint16_t track_steps;
	char *tres_alloc_str;
	char *tres_req_str;
	uint32_t uid;
	char 	*used_gres;
	char    *user;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
	char    *wckey;
	uint32_t wckeyid;
	char    *work_dir;
} slurmdb_job_rec_t;

typedef struct {
	uint32_t accrue_cnt;    /* Count of how many jobs I have accuring prio
				 * (DON'T PACK for state file) */
	List acct_limit_list; /* slurmdb_used_limits_t's (DON'T PACK
			       * for state file) */
	List job_list; /* list of job pointers to submitted/running
			  jobs (DON'T PACK) */
	uint32_t grp_used_jobs;	/* count of active jobs (DON'T PACK
				 * for state file) */
	uint32_t grp_used_submit_jobs; /* count of jobs pending or running
					* (DON'T PACK for state file) */
	uint64_t *grp_used_tres; /* count of tres in use in this qos
				 * (DON'T PACK for state file) */
	uint64_t *grp_used_tres_run_secs; /* count of running tres secs
					 * (DON'T PACK for state file) */
	double grp_used_wall;   /* group count of time (minutes) used in
				 * running jobs */
	double norm_priority;/* normalized priority (DON'T PACK for
			      * state file) */
	uint32_t tres_cnt; /* size of the tres arrays,
			    * (DON'T PACK for state file) */
	long double usage_raw;	/* measure of resource usage */

	long double *usage_tres_raw; /* measure of each TRES usage */
	List user_limit_list; /* slurmdb_used_limits_t's (DON'T PACK
			       * for state file) */
} slurmdb_qos_usage_t;

typedef struct {
	char *description;
	uint32_t id;
	uint32_t flags; /* flags for various things to enforce or
			   override other limits */
	uint32_t grace_time; /* preemption grace time */
	uint32_t grp_jobs_accrue; /* max number of jobs this qos can
				   * have accruing priority time
				   */
	uint32_t grp_jobs;	/* max number of jobs this qos can run
				 * at one time */
	uint32_t grp_submit_jobs; /* max number of jobs this qos can submit at
				   * one time */
	char *grp_tres;            /* max number of tres ths qos can
				    * allocate at one time */
	uint64_t *grp_tres_ctld;   /* grp_tres broken out in an array
				    * based off the ordering of the total
				    * number of TRES in the system
				    * (DON'T PACK) */
	char *grp_tres_mins;       /* max number of tres minutes this
				    * qos can run for */
	uint64_t *grp_tres_mins_ctld; /* grp_tres_mins broken out in an array
				       * based off the ordering of the total
				       * number of TRES in the system
				       * (DON'T PACK) */
	char *grp_tres_run_mins;   /* max number of tres minutes this
				    * qos can have running at one time */
	uint64_t *grp_tres_run_mins_ctld; /* grp_tres_run_mins
					   * broken out in an array
					   * based off the ordering
					   * of the total number of TRES in
					   * the system
					   * (DON'T PACK) */
	uint32_t grp_wall; /* total time in hours this qos can run for */

	uint32_t max_jobs_pa;	/* max number of jobs an account can
				 * run with this qos at one time */
	uint32_t max_jobs_pu;	/* max number of jobs a user can
				 * run with this qos at one time */
	uint32_t max_jobs_accrue_pa; /* max number of jobs an account can
				      * have accruing priority time
				      */
	uint32_t max_jobs_accrue_pu; /* max number of jobs a user can
				      * have accruing priority time
				      */
	uint32_t max_submit_jobs_pa; /* max number of jobs an account can
					submit with this qos at once */
	uint32_t max_submit_jobs_pu; /* max number of jobs a user can
					submit with this qos at once */
	char *max_tres_mins_pj;    /* max number of tres seconds this
				    * qos can have per job */
	uint64_t *max_tres_mins_pj_ctld; /* max_tres_mins broken out in an array
					  * based off the ordering of the
					  * total number of TRES in the system
					  * (DON'T PACK) */
	char *max_tres_pa;         /* max number of tres this
				    * QOS can allocate per account */
	uint64_t *max_tres_pa_ctld;   /* max_tres_pa broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */
	char *max_tres_pj;         /* max number of tres this
				    * qos can allocate per job */
	uint64_t *max_tres_pj_ctld;   /* max_tres_pj broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */
	char *max_tres_pn;         /* max number of tres this
				    * qos can allocate per job */
	uint64_t *max_tres_pn_ctld;   /* max_tres_pj broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */
	char *max_tres_pu;         /* max number of tres this
				    * QOS can allocate per user */
	uint64_t *max_tres_pu_ctld;   /* max_tres broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */
	char *max_tres_run_mins_pa;   /* max number of tres minutes this
				       * qos can having running at one
				       * time per account, currently
				       * this doesn't do anything.
				       */
	uint64_t *max_tres_run_mins_pa_ctld; /* max_tres_run_mins_pa
					      * broken out in an array
					      * based off the ordering
					      * of the total number of TRES in
					      * the system, currently
					      * this doesn't do anything.
					      * (DON'T PACK) */
	char *max_tres_run_mins_pu;   /* max number of tres minutes this
				       * qos can having running at one
				       * time, currently this doesn't
				       * do anything.
				       */
	uint64_t *max_tres_run_mins_pu_ctld; /* max_tres_run_mins_pu
					      * broken out in an array
					      * based off the ordering
					      * of the total number of TRES in
					      * the system, currently
					      * this doesn't do anything.
					      * (DON'T PACK) */
	uint32_t max_wall_pj; /* longest time this
			       * qos can run a job */
	uint32_t min_prio_thresh;  /* Don't reserve resources for pending jobs
				    * unless they have a priority equal to or
				    * higher than this. */
	char *min_tres_pj; /* min number of tres a job can
			    * allocate with this qos */
	uint64_t *min_tres_pj_ctld;   /* min_tres_pj broken out in an array
				       * based off the ordering of the
				       * total number of TRES in the system
				       * (DON'T PACK) */

	char *name;
	bitstr_t *preempt_bitstr; /* other qos' this qos can preempt */
	List preempt_list; /* list of char *'s only used to add or
			    * change the other qos' this can preempt,
			    * when doing a get use the preempt_bitstr */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint32_t priority;  /* ranged int needs to be a unint for
			     * heterogeneous systems */
	slurmdb_qos_usage_t *usage; /* For internal use only, DON'T PACK */
	double usage_factor; /* factor to apply to usage in this qos */
	double usage_thres; /* percent of effective usage of an
			       association when breached will deny
			       pending and new jobs */
	time_t blocked_until; /* internal use only, DON'T PACK  */
} slurmdb_qos_rec_t;

typedef struct {
	List description_list; /* list of char * */
	List id_list; /* list of char * */
	List format_list;/* list of char * */
	List name_list; /* list of char * */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint16_t with_deleted;
} slurmdb_qos_cond_t;

typedef struct {
	List cluster_list; /* cluster reservations are on list of
			    * char * */
	uint32_t flags; /* flags for reservation. */
	List format_list;/* list of char * */
	List id_list;   /* ids of reservations. list of char * */
	List name_list; /* name of reservations. list of char * */
	char *nodes; /* list of nodes in reservation */
	time_t time_end; /* end time of reservation */
	time_t time_start; /* start time of reservation */
	uint16_t with_usage; /* send usage for reservation */
} slurmdb_reservation_cond_t;

typedef struct {
	char *assocs; /* comma separated list of associations */
	char *cluster; /* cluster reservation is for */
	uint32_t flags; /* flags for reservation. */
	uint32_t id;   /* id of reservation. */
	char *name; /* name of reservation */
	char *nodes; /* list of nodes in reservation */
	char *node_inx; /* node index of nodes in reservation */
	time_t time_end; /* end time of reservation */
	time_t time_start; /* start time of reservation */
	time_t time_start_prev; /* If start time was changed this is
				 * the pervious start time.  Needed
				 * for accounting */
	char *tres_str;
	double unused_wall; /* amount of seconds this reservation wasn't used */
	List tres_list; /* list of slurmdb_tres_rec_t, only set when
			 * job usage is requested.
			 */
} slurmdb_reservation_rec_t;

typedef struct {
	uint32_t array_task_id;		/* task_id of a job array or NO_VAL */
	uint32_t jobid;
	uint32_t pack_job_offset;	/* pack_job_offset or NO_VAL */
	uint32_t stepid;
} slurmdb_selected_step_t;

typedef struct {
	uint32_t elapsed;
	time_t end;
	int32_t exitcode;
	slurmdb_job_rec_t *job_ptr;
	uint32_t nnodes;
	char *nodes;
	uint32_t ntasks;
	char *pid_str;
	uint32_t req_cpufreq_min;
	uint32_t req_cpufreq_max;
	uint32_t req_cpufreq_gov;
	uint32_t requid;
	time_t start;
	uint32_t state;
	slurmdb_stats_t stats;
	uint32_t stepid;	/* job's step number */
	char *stepname;
	uint32_t suspended;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint32_t task_dist;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	char *tres_alloc_str;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
} slurmdb_step_rec_t;

/* slurmdb_stats_t defined above alphabetical */

typedef struct {
	List cluster_list; /* list of char * */
	List description_list; /* list of char * */
	uint32_t flags;
	List format_list;/* list of char * */
	List id_list; /* list of char * */
	List manager_list; /* list of char * */
	List name_list; /* list of char * */
	List percent_list; /* list of char * */
	List server_list; /* list of char * */
	List type_list; /* list of char * */
	uint16_t with_deleted;
	uint16_t with_clusters;
} slurmdb_res_cond_t;

typedef struct {
	List clus_res_list; /* list of slurmdb_clus_res_rec_t *'s */
	slurmdb_clus_res_rec_t *clus_res_rec; /* if only one cluster
						 being represented */
	uint32_t count; /* count of resources managed on the server */
	char *description;
	uint32_t flags; /* resource attribute flags */
	uint32_t id;
	char *manager;  /* resource manager name */
	char *name;
	uint16_t percent_used;
	char *server;  /* resource server name */
	uint32_t type; /* resource type */
} slurmdb_res_rec_t;

typedef struct {
	List acct_list; /* list of char * */
	List action_list; /* list of char * */
	List actor_list; /* list of char * */
	List cluster_list; /* list of char * */
	List format_list;/* list of char * */
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

/* Right now this is used in the slurmdb_qos_rec_t structure.  In the
 * user_limit_list and acct_limit_list. */
typedef struct {
	uint32_t accrue_cnt; /* count of jobs accruing prio */
	char *acct; /* If limits for an account this is the accounts name */
	uint32_t jobs;	/* count of active jobs */
	uint32_t submit_jobs; /* count of jobs pending or running */
	uint64_t *tres; /* array of TRES allocated */
	uint64_t *tres_run_mins; /* array of how many TRES mins are
				  * allocated currently, currently this doesn't
				  * do anything and isn't set up. */
	uint32_t uid; /* If limits for a user this is the users uid */
} slurmdb_used_limits_t;

typedef struct {
	uint16_t admin_level; /* really slurmdb_admin_level_t but for
				 packing purposes needs to be uint16_t */
	slurmdb_assoc_cond_t *assoc_cond; /* use user_list here for
						   names and acct_list for
						   default accounts */
	List def_acct_list; /* list of char * (We can't really use
			     * the assoc_cond->acct_list for this
			     * because then it is impossible for us
			     * to tell which accounts are defaults
			     * and which ones aren't, especially when
			     * dealing with other versions.)*/
	List def_wckey_list; /* list of char * */
	uint16_t with_assocs;
	uint16_t with_coords;
	uint16_t with_deleted;
	uint16_t with_wckeys;
	uint16_t without_defaults;
} slurmdb_user_cond_t;

typedef struct {
	uint16_t admin_level; /* really slurmdb_admin_level_t but for
				 packing purposes needs to be uint16_t */
	List assoc_list; /* list of slurmdb_association_rec_t *'s */
	List coord_accts; /* list of slurmdb_coord_rec_t *'s */
	char *default_acct;
	char *default_wckey;
	char *name;
	char *old_name;
	uint32_t uid;
	List wckey_list; /* list of slurmdb_wckey_rec_t *'s */
} slurmdb_user_rec_t;

typedef struct {
	List objects; /* depending on type */
	uint16_t type; /* really slurmdb_update_type_t but for
			* packing purposes needs to be a
			* uint16_t */
} slurmdb_update_object_t;

typedef struct {
	List cluster_list;	/* list of char * */
	List format_list;	/* list of char * */
	List id_list;		/* list of char * */

	List name_list;         /* list of char * */

	uint16_t only_defs;     /* only give me the defaults */

	time_t usage_end;
	time_t usage_start;

	List user_list;		/* list of char * */

	uint16_t with_usage;    /* fill in usage */
	uint16_t with_deleted;  /* return deleted associations */
} slurmdb_wckey_cond_t;

typedef struct {
	List accounting_list; /* list of slurmdb_accounting_rec_t *'s */
	char *cluster;		/* cluster associated */

	uint32_t id;		/* id identifing a combination of
				 * user-wckey-cluster */
	uint16_t is_def;        /* Is this the users default wckey */

	char *name;		/* wckey name */
	uint32_t uid;		/* user ID */

	char *user;		/* user associated */
} slurmdb_wckey_rec_t;

typedef struct {
	char *name;
	char *print_name;
	char *spaces;
	uint16_t user; /* set to 1 if it is a user i.e. if name[0] is
			* '|' */
} slurmdb_print_tree_t;

typedef struct {
	slurmdb_assoc_rec_t *assoc;
	char *sort_name;
	List children;
} slurmdb_hierarchical_rec_t;

/************** report specific structures **************/

typedef struct {
	char *acct;
	char *cluster;
	char *parent_acct;
	List tres_list; /* list of slurmdb_tres_rec_t *'s */
	char *user;
} slurmdb_report_assoc_rec_t;

typedef struct {
	char *acct;
	List acct_list; /* list of char *'s */
	List assoc_list; /* list of slurmdb_report_assoc_rec_t's */
	char *name;
	List tres_list; /* list of slurmdb_tres_rec_t *'s */
	uid_t uid;
} slurmdb_report_user_rec_t;

typedef struct {
	List accounting_list; /* list of slurmdb_accounting_rec_t *'s */
	List assoc_list; /* list of slurmdb_report_assoc_rec_t *'s */
	char *name;
	List tres_list; /* list of slurmdb_tres_rec_t *'s */
	List user_list; /* list of slurmdb_report_user_rec_t *'s */
} slurmdb_report_cluster_rec_t;

typedef struct {
	uint32_t count; /* count of jobs */
	List jobs; /* This should be a NULL destroy since we are just
		    * putting a pointer to a slurmdb_job_rec_t here
		    * not allocating any new memory */
	uint32_t min_size; /* smallest size of job in cpus here 0 if first */
	uint32_t max_size; /* largest size of job in cpus here INFINITE if
			    * last */
	List tres_list; /* list of slurmdb_tres_rec_t *'s */
} slurmdb_report_job_grouping_t;

typedef struct {
	char *acct;	/* account name */
	uint32_t count; /* total count of jobs taken up by this acct */
	List groups;	/* containing slurmdb_report_job_grouping_t's*/
	uint32_t lft;
	uint32_t rgt;
	List tres_list; /* list of slurmdb_tres_rec_t *'s */
} slurmdb_report_acct_grouping_t;

typedef struct {
	List acct_list;	/* containing slurmdb_report_acct_grouping_t's */
	char *cluster; 	/* cluster name */
	uint32_t count;	/* total count of jobs taken up by this cluster */
	List tres_list;	/* list of slurmdb_tres_rec_t *'s */
} slurmdb_report_cluster_grouping_t;

#define ROLLUP_HOUR	0
#define ROLLUP_DAY	1
#define ROLLUP_MONTH	2
#define ROLLUP_COUNT	3
typedef struct rollup_stats {
	uint32_t rollup_time[ROLLUP_COUNT];
} rollup_stats_t;

typedef struct {
	uint16_t *rollup_count;		/* Length should be ROLLUP_COUNT */
	uint64_t *rollup_time;		/* Length should be ROLLUP_COUNT */
	uint64_t *rollup_max_time;	/* Length should be ROLLUP_COUNT */

	uint32_t type_cnt;		/* Length of rpc_type arrays */
	uint16_t *rpc_type_id;		/* RPC type */
	uint32_t *rpc_type_cnt;		/* count of RPCs processed */
	uint64_t *rpc_type_time;	/* total usecs this type RPC */
	uint32_t user_cnt;		/* Length of rpc_user arrays */
	uint32_t *rpc_user_id;		/* User ID issuing RPC */
	uint32_t *rpc_user_cnt;		/* count of RPCs processed */
	uint64_t *rpc_user_time;	/* total usecs this user's RPCs */
} slurmdb_stats_rec_t;


/* global variable for cross cluster communication */
extern slurmdb_cluster_rec_t *working_cluster_rec;


/************** account functions **************/

/*
 * add accounts to accounting system
 * IN:  account_list List of slurmdb_account_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_accounts_add(void *db_conn, List acct_list);

/*
 * get info from the storage
 * IN:  slurmdb_account_cond_t *
 * IN:  params void *
 * returns List of slurmdb_account_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_accounts_get(void *db_conn,
				 slurmdb_account_cond_t *acct_cond);

/*
 * modify existing accounts in the accounting system
 * IN:  slurmdb_acct_cond_t *acct_cond
 * IN:  slurmdb_account_rec_t *acct
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_accounts_modify(void *db_conn,
				    slurmdb_account_cond_t *acct_cond,
				    slurmdb_account_rec_t *acct);

/*
 * remove accounts from accounting system
 * IN:  slurmdb_account_cond_t *acct_cond
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_accounts_remove(void *db_conn,
				    slurmdb_account_cond_t *acct_cond);


/************** archive functions **************/

/*
 * expire old info from the storage
 */
extern int slurmdb_archive(void *db_conn, slurmdb_archive_cond_t *arch_cond);

/*
 * expire old info from the storage
 */
extern int slurmdb_archive_load(void *db_conn,
				slurmdb_archive_rec_t *arch_rec);


/************** association functions **************/

/*
 * add associations to accounting system
 * IN:  assoc_list List of slurmdb_assoc_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_associations_add(void *db_conn, List assoc_list);

/*
 * get info from the storage
 * IN:  slurmdb_assoc_cond_t *
 * RET: List of slurmdb_assoc_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_associations_get(void *db_conn,
				     slurmdb_assoc_cond_t *assoc_cond);

/*
 * modify existing associations in the accounting system
 * IN:  slurmdb_assoc_cond_t *assoc_cond
 * IN:  slurmdb_assoc_rec_t *assoc
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_associations_modify(void *db_conn,
					slurmdb_assoc_cond_t *assoc_cond,
					slurmdb_assoc_rec_t *assoc);

/*
 * remove associations from accounting system
 * IN:  slurmdb_assoc_cond_t *assoc_cond
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_associations_remove(void *db_conn,
					slurmdb_assoc_cond_t *assoc_cond);

/************** cluster functions **************/

/*
 * add clusters to accounting system
 * IN:  cluster_list List of slurmdb_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_clusters_add(void *db_conn, List cluster_list);

/*
 * get info from the storage
 * IN:  slurmdb_cluster_cond_t *
 * IN:  params void *
 * returns List of slurmdb_cluster_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_clusters_get(void *db_conn,
				 slurmdb_cluster_cond_t *cluster_cond);

/*
 * modify existing clusters in the accounting system
 * IN:  slurmdb_cluster_cond_t *cluster_cond
 * IN:  slurmdb_cluster_rec_t *cluster
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_clusters_modify(void *db_conn,
				    slurmdb_cluster_cond_t *cluster_cond,
				    slurmdb_cluster_rec_t *cluster);

/*
 * remove clusters from accounting system
 * IN:  slurmdb_cluster_cond_t *cluster_cond
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_clusters_remove(void *db_conn,
				    slurmdb_cluster_cond_t *cluster_cond);

/************** cluster report functions **************/

/* report for clusters of account per user
 * IN: slurmdb_assoc_cond_t *assoc_cond
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_cluster_account_by_user(void *db_conn,
						   slurmdb_assoc_cond_t *assoc_cond);

/* report for clusters of users per account
 * IN: slurmdb_assoc_cond_t *assoc_cond
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_cluster_user_by_account(void *db_conn,
						   slurmdb_assoc_cond_t *assoc_cond);

/* report for clusters of wckey per user
 * IN: slurmdb_wckey_cond_t *wckey_cond
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_cluster_wckey_by_user(void *db_conn,
						 slurmdb_wckey_cond_t *wckey_cond);

/* report for clusters of users per wckey
 * IN: slurmdb_wckey_cond_t *wckey_cond
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_cluster_user_by_wckey(void *db_conn,
						 slurmdb_wckey_cond_t *wckey_cond);


extern List slurmdb_report_job_sizes_grouped_by_top_account(void *db_conn,
							    slurmdb_job_cond_t *job_cond,
							    List grouping_list,
							    bool flat_view);

extern List slurmdb_report_job_sizes_grouped_by_wckey(void *db_conn,
						      slurmdb_job_cond_t *job_cond,
						      List grouping_list);

extern List slurmdb_report_job_sizes_grouped_by_top_account_then_wckey(
	void *db_conn,
	slurmdb_job_cond_t *job_cond,
	List grouping_list,
	bool flat_view);


/* report on users with top usage
 * IN: slurmdb_user_cond_t *user_cond
 * IN: group_accounts - Whether or not to group all accounts together
 *                      for each  user. If 0 a separate entry for each
 *                      user and account reference is displayed.
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_user_top_usage(void *db_conn,
					  slurmdb_user_cond_t *user_cond,
					  bool group_accounts);

/************** connection functions **************/

/*
 * get a new connection to the slurmdb
 * RET: pointer used to access db
 */
extern void *slurmdb_connection_get();
/*
 * get a new connection to the slurmdb
 * OUT: persist_conn_flags - Flags returned from connection if any see
 *                           slurm_persist_conn.h.
 * RET: pointer used to access db
 */
extern void *slurmdb_connection_get2(uint16_t *persist_conn_flags);
/*
 * release connection to the storage unit
 * IN/OUT: void ** pointer returned from
 *         slurmdb_connection_get() which will be freed.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_connection_close(void **db_conn);

/*
 * commit or rollback changes made without closing connection
 * IN: void * pointer returned from slurmdb_connection_get()
 * IN: bool - true will commit changes false will rollback
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_connection_commit(void *db_conn, bool commit);

/************** coordinator functions **************/

/*
 * add users as account coordinators
 * IN: acct_list list of char *'s of names of accounts
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_coord_add(void *db_conn,
			     List acct_list,
			     slurmdb_user_cond_t *user_cond);

/*
 * remove users from being a coordinator of an account
 * IN: acct_list list of char *'s of names of accounts
 * IN: slurmdb_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_coord_remove(void *db_conn, List acct_list,
				 slurmdb_user_cond_t *user_cond);

/*************** Federation functions **************/

/*
 * add federations to accounting system
 * IN:  list List of slurmdb_federation_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_federations_add(void *db_conn, List federation_list);

/*
 * modify existing federations in the accounting system
 * IN:  slurmdb_federation_cond_t *fed_cond
 * IN:  slurmdb_federation_rec_t  *fed
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdb_federations_modify(void *db_conn,
				       slurmdb_federation_cond_t *fed_cond,
				       slurmdb_federation_rec_t *fed);

/*
 * remove federations from accounting system
 * IN:  slurmdb_federation_cond_t *fed_cond
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdb_federations_remove(void *db_conn,
				       slurmdb_federation_cond_t *fed_cond);

/*
 * get info from the storage
 * IN:  slurmdb_federation_cond_t *
 * RET: List of slurmdb_federation_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_federations_get(void *db_conn,
				    slurmdb_federation_cond_t *fed_cond);

/*************** Job functions **************/

/*
 * modify existing job in the accounting system
 * IN:  slurmdb_job_modify_cond_t *job_cond
 * IN:  slurmdb_job_rec_t *job
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdb_job_modify(void *db_conn,
			       slurmdb_job_modify_cond_t *job_cond,
			       slurmdb_job_rec_t *job);

/*
 * get info from the storage
 * returns List of slurmdb_job_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_jobs_get(void *db_conn, slurmdb_job_cond_t *job_cond);

/*
 * Fix runaway jobs
 * IN: jobs, a list of all the runaway jobs
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_jobs_fix_runaway(void *db_conn, List jobs);

/* initialization of job completion logging */
extern int slurmdb_jobcomp_init(char *jobcomp_loc);

/* terminate pthreads and free, general clean-up for termination */
extern int slurmdb_jobcomp_fini(void);

/*
 * get info from the storage
 * returns List of jobcomp_job_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_jobcomp_jobs_get(slurmdb_job_cond_t *job_cond);

/************** extra get functions **************/

/*
 * reconfigure the slurmdbd
 */
extern int slurmdb_reconfig(void *db_conn);

/*
 * shutdown the slurmdbd
 */
extern int slurmdb_shutdown(void *db_conn);

/*
 * clear the slurmdbd statistics
 */
extern int slurmdb_clear_stats(void *db_conn);

/*
 * get the slurmdbd statistics
 * Call slurmdb_destroy_stats_rec() to free stats_pptr
 */
extern int slurmdb_get_stats(void *db_conn, slurmdb_stats_rec_t **stats_pptr);

/*
 * get info from the storage
 * RET: List of config_key_pair_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_config_get(void *db_conn);

/*
 * get info from the storage
 * IN:  slurmdb_event_cond_t *
 * RET: List of slurmdb_event_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_events_get(void *db_conn,
			       slurmdb_event_cond_t *event_cond);

/*
 * get info from the storage
 * IN:  slurmdb_assoc_cond_t *
 * RET: List of slurmdb_assoc_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_problems_get(void *db_conn,
				 slurmdb_assoc_cond_t *assoc_cond);

/*
 * get info from the storage
 * IN:  slurmdb_reservation_cond_t *
 * RET: List of slurmdb_reservation_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_reservations_get(void *db_conn,
				     slurmdb_reservation_cond_t *resv_cond);

/*
 * get info from the storage
 * IN:  slurmdb_txn_cond_t *
 * RET: List of slurmdb_txn_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_txn_get(void *db_conn, slurmdb_txn_cond_t *txn_cond);

/*
 * Get information about requested cluster(s). Similar to
 * slurmdb_clusters_get, but should be used when setting up the
 * working_cluster_rec.  It replaces the plugin_id_select with
 * the position of the id in the select plugin array, as well as sets up the
 * control_addr and dim_size parts of the structure.
 *
 * IN: cluster_names - comma separated string of cluster names
 * RET: List of slurmdb_cluster_rec_t *
 * note List needs to bbe freed with slurm_list_destroy() when called
 */
extern List slurmdb_get_info_cluster(char *cluster_names);

/*
 * get the first cluster that will run a job
 * IN: req - description of resource allocation request
 * IN: cluster_names - comma separated string of cluster names
 * OUT: cluster_rec - record of selected cluster or NULL if none found or
 * 		      cluster_names is NULL
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 *
 * Note: Cluster_rec needs to be freed with slurmdb_destroy_cluster_rec() when
 * called
 * Note: The will_runs are not threaded. Currently it relies on the
 * working_cluster_rec to pack the job_desc's jobinfo. See previous commit for
 * an example of how to thread this.
 */
extern int slurmdb_get_first_avail_cluster(job_desc_msg_t *req,
					   char *cluster_names,
					   slurmdb_cluster_rec_t **cluster_rec);

/*
 * get the first cluster that will run a heterogeneous job
 * IN: req - description of resource allocation request
 * IN: cluster_names - comma separated string of cluster names
 * OUT: cluster_rec - record of selected cluster or NULL if none found or
 * 		      cluster_names is NULL
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 *
 * Note: Cluster_rec needs to be freed with slurmdb_destroy_cluster_rec() when
 * called
 * Note: The will_runs are not threaded. Currently it relies on the
 * working_cluster_rec to pack the job_desc's jobinfo. See previous commit for
 * an example of how to thread this.
 */
extern int slurmdb_get_first_pack_cluster(List job_req_list,
	char *cluster_names, slurmdb_cluster_rec_t **cluster_rec);

/************** helper functions **************/
extern void slurmdb_destroy_assoc_usage(void *object);
extern void slurmdb_destroy_qos_usage(void *object);
extern void slurmdb_destroy_user_rec(void *object);
extern void slurmdb_destroy_account_rec(void *object);
extern void slurmdb_destroy_coord_rec(void *object);
extern void slurmdb_destroy_clus_res_rec(void *object);
extern void slurmdb_destroy_cluster_accounting_rec(void *object);
extern void slurmdb_destroy_cluster_rec(void *object);
extern void slurmdb_destroy_federation_rec(void *object);
extern void slurmdb_destroy_accounting_rec(void *object);
extern void slurmdb_free_assoc_mgr_state_msg(void *object);
extern void slurmdb_free_assoc_rec_members(slurmdb_assoc_rec_t *assoc);
extern void slurmdb_destroy_assoc_rec(void *object);
extern void slurmdb_destroy_event_rec(void *object);
extern void slurmdb_destroy_job_rec(void *object);
extern void slurmdb_free_qos_rec_members(slurmdb_qos_rec_t *qos);
extern void slurmdb_destroy_qos_rec(void *object);
extern void slurmdb_destroy_reservation_rec(void *object);
extern void slurmdb_destroy_step_rec(void *object);
extern void slurmdb_destroy_res_rec(void *object);
extern void slurmdb_destroy_txn_rec(void *object);
extern void slurmdb_destroy_wckey_rec(void *object);
extern void slurmdb_destroy_archive_rec(void *object);
extern void slurmdb_destroy_tres_rec_noalloc(void *object);
extern void slurmdb_destroy_tres_rec(void *object);
extern void slurmdb_destroy_report_assoc_rec(void *object);
extern void slurmdb_destroy_report_user_rec(void *object);
extern void slurmdb_destroy_report_cluster_rec(void *object);

extern void slurmdb_destroy_user_cond(void *object);
extern void slurmdb_destroy_account_cond(void *object);
extern void slurmdb_destroy_cluster_cond(void *object);
extern void slurmdb_destroy_federation_cond(void *object);
extern void slurmdb_destroy_tres_cond(void *object);
extern void slurmdb_destroy_assoc_cond(void *object);
extern void slurmdb_destroy_event_cond(void *object);
extern void slurmdb_destroy_job_cond(void *object);
extern void slurmdb_destroy_job_modify_cond(void *object);
extern void slurmdb_destroy_qos_cond(void *object);
extern void slurmdb_destroy_reservation_cond(void *object);
extern void slurmdb_destroy_res_cond(void *object);
extern void slurmdb_destroy_txn_cond(void *object);
extern void slurmdb_destroy_wckey_cond(void *object);
extern void slurmdb_destroy_archive_cond(void *object);

extern void slurmdb_destroy_update_object(void *object);
extern void slurmdb_destroy_used_limits(void *object);
extern void slurmdb_destroy_update_shares_rec(void *object);
extern void slurmdb_destroy_print_tree(void *object);
extern void slurmdb_destroy_hierarchical_rec(void *object);
extern void slurmdb_destroy_selected_step(void *object);

extern void slurmdb_destroy_report_job_grouping(void *object);
extern void slurmdb_destroy_report_acct_grouping(void *object);
extern void slurmdb_destroy_report_cluster_grouping(void *object);
extern void slurmdb_destroy_stats_rec(void *object);

extern void slurmdb_free_slurmdb_stats_members(slurmdb_stats_t *stats);
extern void slurmdb_destroy_slurmdb_stats(slurmdb_stats_t *stats);

extern void slurmdb_init_assoc_rec(slurmdb_assoc_rec_t *assoc,
				   bool free_it);
extern void slurmdb_init_clus_res_rec(slurmdb_clus_res_rec_t *clus_res,
				      bool free_it);
extern void slurmdb_init_cluster_rec(slurmdb_cluster_rec_t *cluster,
				     bool free_it);
extern void slurmdb_init_federation_rec(slurmdb_federation_rec_t *federation,
					bool free_it);
extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos,
				 bool free_it,
				 uint32_t init_val);
extern void slurmdb_init_res_rec(slurmdb_res_rec_t *res,
				 bool free_it);
extern void slurmdb_init_wckey_rec(slurmdb_wckey_rec_t *wckey,
				   bool free_it);
extern void slurmdb_init_tres_cond(slurmdb_tres_cond_t *tres,
				   bool free_it);
extern void slurmdb_init_cluster_cond(slurmdb_cluster_cond_t *cluster,
				      bool free_it);
extern void slurmdb_init_federation_cond(slurmdb_federation_cond_t *federation,
					 bool free_it);
extern void slurmdb_init_res_cond(slurmdb_res_cond_t *cluster,
				  bool free_it);

/* The next two functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List slurmdb_get_hierarchical_sorted_assoc_list(
	List assoc_list, bool use_lft);
extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list);


/* IN/OUT: tree_list a list of slurmdb_print_tree_t's */
extern char *slurmdb_tree_name_get(char *name, char *parent, List tree_list);

/************** job report functions **************/

/************** resource functions **************/
/*
 * add resource's to accounting system
 * IN:  res_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_res_add(void *db_conn, List res_list);

/*
 * get info from the storage
 * IN:  slurmdb_res_cond_t *
 * RET: List of slurmdb_res_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_res_get(void *db_conn, slurmdb_res_cond_t *res_cond);

/*
 * modify existing resource in the accounting system
 * IN:  slurmdb_res_cond_t *res_cond
 * IN:  slurmdb_res_rec_t *res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_res_modify(void *db_conn,
			       slurmdb_res_cond_t *res_cond,
			       slurmdb_res_rec_t *res);

/*
 * remove resource from accounting system
 * IN:  slurmdb_res_cond_t *res
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_res_remove(void *db_conn, slurmdb_res_cond_t *res_cond);

/************** qos functions **************/

/*
 * add qos's to accounting system
 * IN:  qos_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_qos_add(void *db_conn, List qos_list);

/*
 * get info from the storage
 * IN:  slurmdb_qos_cond_t *
 * RET: List of slurmdb_qos_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_qos_get(void *db_conn, slurmdb_qos_cond_t *qos_cond);

/*
 * modify existing qos in the accounting system
 * IN:  slurmdb_qos_cond_t *qos_cond
 * IN:  slurmdb_qos_rec_t *qos
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_qos_modify(void *db_conn,
			       slurmdb_qos_cond_t *qos_cond,
			       slurmdb_qos_rec_t *qos);

/*
 * remove qos from accounting system
 * IN:  slurmdb_qos_cond_t *assoc_qos
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_qos_remove(void *db_conn, slurmdb_qos_cond_t *qos_cond);

/************** tres functions **************/

/*
 * add tres's to accounting system
 * IN:  tres_list List of char *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_tres_add(void *db_conn, List tres_list);

/*
 * get info from the storage
 * IN:  slurmdb_tres_cond_t *
 * RET: List of slurmdb_tres_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_tres_get(void *db_conn, slurmdb_tres_cond_t *tres_cond);


/************** usage functions **************/

/*
 * get info from the storage
 * IN/OUT:  in void * (slurmdb_assoc_rec_t *) or
 *          (slurmdb_wckey_rec_t *) of (slurmdb_cluster_rec_t *) with
 *          the id, and cluster set.
 * IN:  type what type is 'in'
 * IN:  start time stamp for records >=
 * IN:  end time stamp for records <=
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_usage_get(void *db_conn,
			     void *in,
			     int type,
			     time_t start,
			     time_t end);

/*
 * roll up data in the storage
 * IN: sent_start (option time to do a re-roll or start from this point)
 * IN: sent_end (option time to do a re-roll or end at this point)
 * IN: archive_data (if 0 old data is not archived in a monthly rollup)
 * IN/OUT: rollup_stats data structure in which to save rollup statistics
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_usage_roll(void *db_conn,
			      time_t sent_start,
			      time_t sent_end,
			      uint16_t archive_data,
			      rollup_stats_t *rollup_stats);

/************** user functions **************/

/*
 * add users to accounting system
 * IN:  user_list List of slurmdb_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_users_add(void *db_conn, List user_list);

/*
 * get info from the storage
 * IN:  slurmdb_user_cond_t *
 * IN:  params void *
 * returns List of slurmdb_user_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_users_get(void *db_conn, slurmdb_user_cond_t *user_cond);

/*
 * modify existing users in the accounting system
 * IN:  slurmdb_user_cond_t *user_cond
 * IN:  slurmdb_user_rec_t *user
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_users_modify(void *db_conn,
				 slurmdb_user_cond_t *user_cond,
				 slurmdb_user_rec_t *user);

/*
 * remove users from accounting system
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_users_remove(void *db_conn,
				 slurmdb_user_cond_t *user_cond);


/************** user report functions **************/


/************** wckey functions **************/

/*
 * add wckey's to accounting system
 * IN:  wckey_list List of slurmdb_wckey_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_wckeys_add(void *db_conn, List wckey_list);

/*
 * get info from the storage
 * IN:  slurmdb_wckey_cond_t *
 * RET: List of slurmdb_wckey_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_wckeys_get(void *db_conn,
			       slurmdb_wckey_cond_t *wckey_cond);

/*
 * modify existing wckey in the accounting system
 * IN:  slurmdb_wckey_cond_t *wckey_cond
 * IN:  slurmdb_wckey_rec_t *wckey
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_wckeys_modify(void *db_conn,
				  slurmdb_wckey_cond_t *wckey_cond,
				  slurmdb_wckey_rec_t *wckey);

/*
 * remove wckey from accounting system
 * IN:  slurmdb_wckey_cond_t *assoc_wckey
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_wckeys_remove(void *db_conn,
				  slurmdb_wckey_cond_t *wckey_cond);

#ifdef __cplusplus
}
#endif

#endif /* !_SLURMDB_H */
