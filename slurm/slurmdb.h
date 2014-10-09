/*****************************************************************************\
 *  slurmdb.h - Interface codes and functions for slurm
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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
#ifndef _SLURMDB_H
#define _SLURMDB_H

/* BEGIN_C_DECLS should be used at the beginning of your declarations,
   so that C++ compilers don't mangle their names.  Use END_C_DECLS at
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

/* Define Server Resource flags */
#define	SLURMDB_RES_FLAG_BASE        0x0fffffff /* apply to get real flags */
#define	SLURMDB_RES_FLAG_NOTSET      0x10000000
#define	SLURMDB_RES_FLAG_ADD         0x20000000
#define	SLURMDB_RES_FLAG_REMOVE      0x40000000

/* flags and types of resources */
/* when we come up with some */

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
#define CLUSTER_FLAG_BGL    0x00000002 /* This is a bluegene/l cluster */
#define CLUSTER_FLAG_BGP    0x00000004 /* This is a bluegene/p cluster */
#define CLUSTER_FLAG_BGQ    0x00000008 /* This is a bluegene/q cluster */
#define CLUSTER_FLAG_SC     0x00000010 /* This is a sun constellation cluster */
#define CLUSTER_FLAG_XCPU   0x00000020 /* This has xcpu */
#define CLUSTER_FLAG_AIX    0x00000040 /* This is an aix cluster */
#define CLUSTER_FLAG_MULTSD 0x00000080 /* This cluster is multiple slurmd */
#define CLUSTER_FLAG_CRAYXT 0x00000100 /* This cluster is a ALPS cray
					* (deprecated) Same as CRAY_A */
#define CLUSTER_FLAG_CRAY_A 0x00000100 /* This cluster is a ALPS cray */
#define CLUSTER_FLAG_FE     0x00000200 /* This cluster is a front end system */
#define CLUSTER_FLAG_CRAY_N 0x00000400 /* This cluster is a Native cray */


/* Cluster Combo flags */
#define CLUSTER_FLAG_CRAY   0x00000500 /* This cluster is a cray.
					  Combo of CRAY_A | CRAY_N */

/* Define assoc_mgr_association_usage_t below to avoid including
 * extraneous slurmdb headers */
#ifndef __assoc_mgr_association_usage_t_defined
#  define  __assoc_mgr_association_usage_t_defined
/* opaque data type */
typedef struct assoc_mgr_association_usage assoc_mgr_association_usage_t;
#endif

/* Define assoc_mgr_qos_usage_t below to avoid including
 * extraneous slurmdb headers */
#ifndef __assoc_mgr_qos_usage_t_defined
#  define  __assoc_mgr_qos_usage_t_defined
/* opaque data type */
typedef struct assoc_mgr_qos_usage assoc_mgr_qos_usage_t;
#endif

/********************************************/

/* Association conditions used for queries of the database */

/* slurmdb_association_cond_t is used in other structures below so
 * this needs to be declared first.
 */
typedef struct {
	List acct_list;		/* list of char * */
	List cluster_list;	/* list of char * */

	List def_qos_id_list;   /* list of char * */

	List fairshare_list;	/* fairshare number */

	List grp_cpu_mins_list; /* list of char * */
	List grp_cpu_run_mins_list; /* list of char * */
	List grp_cpus_list; /* list of char * */
	List grp_jobs_list;	/* list of char * */
	List grp_mem_list;	/* list of char * */
	List grp_nodes_list; /* list of char * */
	List grp_submit_jobs_list; /* list of char * */
	List grp_wall_list; /* list of char * */

	List id_list;		/* list of char */

	List max_cpu_mins_pj_list; /* list of char * */
	List max_cpu_run_mins_list; /* list of char * */
	List max_cpus_pj_list; /* list of char * */
	List max_jobs_list;	/* list of char * */
	List max_nodes_pj_list; /* list of char * */
	List max_submit_jobs_list; /* list of char * */
	List max_wall_pj_list; /* list of char * */

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
} slurmdb_association_cond_t;

/* slurmdb_job_cond_t is used by slurmdb_archive_cond_t so it needs to
 * be defined before hand.
 */
typedef struct {
	List acct_list;		/* list of char * */
	List associd_list;	/* list of char */
	List cluster_list;	/* list of char * */
	uint32_t cpus_max;      /* number of cpus high range */
	uint32_t cpus_min;      /* number of cpus low range */
	uint16_t duplicates;    /* report duplicate job entries */
	int32_t exitcode;       /* exit code of job */
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
	uint16_t without_steps; /* don't give me step info */
	uint16_t without_usage_truncation; /* give me the information
					    * without truncating the
					    * time to the usage_start
					    * and usage_end */
} slurmdb_job_cond_t;

/* slurmdb_stats_t needs to be defined before slurmdb_job_rec_t and
 * slurmdb_step_rec_t.
 */
typedef struct {
	double act_cpufreq;	/* contains actual average cpu frequency */
	double cpu_ave;
	double consumed_energy; /* contains energy consumption in joules */
	uint32_t cpu_min;
	uint32_t cpu_min_nodeid; /* contains which node number it was on */
	uint32_t cpu_min_taskid; /* contains which task number it was on */
	double disk_read_ave; /* average amount of disk read data, in mb */
	double disk_read_max; /* maximum amount of disk read data, in mb */
	uint32_t disk_read_max_nodeid; /* contains  node number max was on */
	uint32_t disk_read_max_taskid;/* contains task number max was on */
	double disk_write_ave; /* average amount of disk write data, in mb */
	double disk_write_max; /* maximum amount of disk write data, in mb */
	uint32_t disk_write_max_nodeid; /* contains  node number max was on */
	uint32_t disk_write_max_taskid;/* contains task number max was on */
	double pages_ave;
	uint64_t pages_max;
	uint32_t pages_max_nodeid; /* contains which node number it was on */
	uint32_t pages_max_taskid; /* contains which task number it was on */
	double rss_ave;
	uint64_t rss_max;
	uint32_t rss_max_nodeid; /* contains which node number it was on */
	uint32_t rss_max_taskid; /* contains which task number it was on */
	double vsize_ave;
	uint64_t vsize_max;
	uint32_t vsize_max_nodeid; /* contains which node number it was on */
	uint32_t vsize_max_taskid; /* contains which task number it was on */
} slurmdb_stats_t;

/************** alphabetical order of structures **************/

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
	uint64_t consumed_energy; /* energy allocated in Joules */
	uint32_t id;	/* association/wckey ID		*/
	time_t period_start; /* when this record was started */
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
} slurmdb_archive_cond_t;

typedef struct {
	char *archive_file;  /* archive file containing data that was
				once flushed from the database */
	char *insert;     /* an sql statement to be ran containing the
			     insert of jobs since past */
} slurmdb_archive_rec_t;

/* slurmdb_association_cond_t is defined above alphabetical */

typedef struct slurmdb_association_rec {
	List accounting_list; 	   /* list of slurmdb_accounting_rec_t *'s */
	char *acct;		   /* account/project associated to
				    * association */
	struct slurmdb_association_rec *assoc_next; /* next association with
						       * same hash index
						       * based off the
						       * account/user
						       * DOESN'T GET PACKED */
	struct slurmdb_association_rec *assoc_next_id; /* next association with
							* same hash index
							* DOESN'T GET PACKED */
	char *cluster;		   /* cluster associated to association */

	uint32_t def_qos_id;       /* Which QOS id is this
				    * associations default */

	uint64_t grp_cpu_mins;     /* max number of cpu minutes the
				    * underlying group of
				    * associations can run for */
	uint64_t grp_cpu_run_mins; /* max number of cpu minutes the
				    * underlying group of
				    * assoiciations can
				    * having running at one time */
	uint32_t grp_cpus;         /* max number of cpus the
				    * underlying group of
				    * associations can allocate at one time */
	uint32_t grp_jobs;	   /* max number of jobs the
				    * underlying group of associations can run
				    * at one time */
	uint32_t grp_mem;          /* max amount of memory the
				    * underlying group of
				    * associations can allocate at once */
	uint32_t grp_nodes;        /* max number of nodes the
				    * underlying group of
				    * associations can allocate at once */
	uint32_t grp_submit_jobs;  /* max number of jobs the
				    * underlying group of
				    * associations can submit at
				    * one time */
	uint32_t grp_wall;         /* total time in hours the
				    * underlying group of
				    * associations can run for */

	uint32_t id;		   /* id identifing a combination of
				    * user-account-cluster(-partition) */

	uint16_t is_def;           /* Is this the users default assoc/acct */

	uint32_t lft;		   /* lft used for grouping sub
				    * associations and jobs as a left
				    * most container used with rgt */

	uint64_t max_cpu_mins_pj;  /* max number of cpu seconds this
				    * association can have per job */
	uint64_t max_cpu_run_mins; /* max number of cpu minutes this
				    * association can
				    * having running at one time */
	uint32_t max_cpus_pj;      /* max number of cpus this
				    * association can allocate per job */
	uint32_t max_jobs;	   /* max number of jobs this
				    * association can run at one time */
	uint32_t max_nodes_pj;     /* max number of nodes this
				    * association can allocate per job */
	uint32_t max_submit_jobs;  /* max number of jobs that can be
				      submitted by association */
	uint32_t max_wall_pj;      /* longest time this
				    * association can run a job */

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
	assoc_mgr_association_usage_t *usage;
	char *user;		   /* user associated to association */
} slurmdb_association_rec_t;

typedef struct {
	uint16_t classification; /* how this machine is classified */
	List cluster_list; /* list of char * */
	uint32_t flags;
	List plugin_id_select_list; /* list of char * */
	List rpc_version_list; /* list of char * */
	time_t usage_end;
	time_t usage_start;
	uint16_t with_deleted;
	uint16_t with_usage;
} slurmdb_cluster_cond_t;

typedef struct {
	List accounting_list; /* list of slurmdb_cluster_accounting_rec_t *'s */
	uint16_t classification; /* how this machine is classified */
	slurm_addr_t control_addr; /* For convenience only.
				    * DOESN'T GET PACKED */
	char *control_host;
	uint32_t control_port;
	uint32_t cpu_count;
	uint16_t dimensions; /* number of dimensions this cluster is */
	int *dim_size; /* For convenience only.
			* Size of each dimension For now only on
			* a bluegene cluster.  DOESN'T GET
			* PACKED, is set up in slurmdb_get_info_cluster */
	uint32_t flags;      /* set of CLUSTER_FLAG_* */
	char *name;
	char *nodes;
	uint32_t plugin_id_select; /* id of the select plugin */
	slurmdb_association_rec_t *root_assoc; /* root association for
						* cluster */
	uint16_t rpc_version; /* version of rpc this cluter is running */
} slurmdb_cluster_rec_t;

typedef struct {
	uint64_t alloc_secs; /* number of cpu seconds allocated */
	uint64_t consumed_energy; /* energy allocated in Joules */
	uint32_t cpu_count; /* number of cpus during time period */
	uint64_t down_secs; /* number of cpu seconds down */
	uint64_t idle_secs; /* number of cpu seconds idle */
	uint64_t over_secs; /* number of cpu seconds overcommitted */
	uint64_t pdown_secs; /* number of cpu seconds planned down */
	time_t period_start; /* when this record was started */
	uint64_t resv_secs; /* number of cpu seconds reserved */
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
	uint16_t event_type;    /* type of event (slurmdb_event_type_t) */
	char *node_name;        /* Name of node (only set in a node event) */
	time_t period_end;      /* End of period */
	time_t period_start;    /* Start of period */
	char *reason;           /* reason node is in state during time
				   period (only set in a node event) */
	uint32_t reason_uid;    /* uid of that who set the reason */
	uint16_t state;         /* State of node during time
				   period (only set in a node event) */
} slurmdb_event_rec_t;

/* slurmdb_job_cond_t is defined above alphabetical */


typedef struct {
	char *cluster;
	uint32_t job_id;
} slurmdb_job_modify_cond_t;

typedef struct {
	char    *account;
	uint32_t alloc_cpus;
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
	char	*partition;
	char	*nodes;
	uint32_t priority;
	uint32_t qosid;
	uint32_t req_cpus;
	char	*req_gres;
	uint32_t req_mem;
	uint32_t requid;
	uint32_t resvid;
	char *resv_name;
	uint32_t show_full;
	time_t start;
	uint16_t	state;
	slurmdb_stats_t stats;
	List    steps; /* list of slurmdb_step_rec_t *'s */
	time_t submit;
	uint32_t suspended;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint32_t timelimit;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	uint16_t track_steps;
	uint32_t uid;
	char 	*used_gres;
	char    *user;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
	char    *wckey;
	uint32_t wckeyid;
} slurmdb_job_rec_t;

typedef struct {
	char *description;
	uint32_t id;
	uint32_t flags; /* flags for various things to enforce or
			   override other limits */
	uint32_t grace_time; /* preemption grace time */
	uint64_t grp_cpu_mins; /* max number of cpu minutes all jobs
				* running under this qos can run for */
	uint64_t grp_cpu_run_mins; /* max number of cpu minutes all jobs
				    * running under this qos can
				    * having running at one time */
	uint32_t grp_cpus; /* max number of cpus this qos
			      can allocate at one time */
	uint32_t grp_jobs;	/* max number of jobs this qos can run
				 * at one time */
	uint32_t grp_mem; /* max amount of memory this qos
			     can allocate at one time */
	uint32_t grp_nodes; /* max number of nodes this qos
			       can allocate at once */
	uint32_t grp_submit_jobs; /* max number of jobs this qos can submit at
				   * one time */
	uint32_t grp_wall; /* total time in hours this qos can run for */

	uint64_t max_cpu_mins_pj; /* max number of cpu mins a job can
				   * use with this qos */
	uint64_t max_cpu_run_mins_pu; /* max number of cpu mins a user can
				       * allocate at a given time when
				       * using this qos (Not yet valid option) */
	uint32_t max_cpus_pj; /* max number of cpus a job can
			       * allocate with this qos */
	uint32_t max_cpus_pu; /* max number of cpus a user can
			       * allocate with this qos at one time */
	uint32_t max_jobs_pu;	/* max number of jobs a user can
				 * run with this qos at one time */
	uint32_t max_nodes_pj; /* max number of nodes a job can
				* allocate with this qos at one time */
	uint32_t max_nodes_pu; /* max number of nodes a user can
				* allocate with this qos at one time */
	uint32_t max_submit_jobs_pu; /* max number of jobs a user can
					submit with this qos at once */
	uint32_t max_wall_pj; /* longest time this
			       * qos can run a job */
	uint32_t min_cpus_pj; /* min number of cpus a job can
			       * allocate with this qos */

	char *name;
	bitstr_t *preempt_bitstr; /* other qos' this qos can preempt */
	List preempt_list; /* list of char *'s only used to add or
			    * change the other qos' this can preempt,
			    * when doing a get use the preempt_bitstr */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint32_t priority;  /* ranged int needs to be a unint for
			     * heterogeneous systems */
	assoc_mgr_qos_usage_t *usage; /* For internal use only, DON'T PACK */
	double usage_factor; /* factor to apply to usage in this qos */
	double usage_thres; /* percent of effective usage of an
			       association when breached will deny
			       pending and new jobs */
} slurmdb_qos_rec_t;

typedef struct {
	List description_list; /* list of char * */
	List id_list; /* list of char * */
	List name_list; /* list of char * */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
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
} slurmdb_reservation_rec_t;

typedef struct {
	uint32_t array_task_id;	/* task_id of a job array of NO_VAL
				 * if N/A */
	uint32_t jobid;
	uint32_t stepid;
} slurmdb_selected_step_t;

typedef struct {
	uint32_t elapsed;
	time_t end;
	int32_t exitcode;
	slurmdb_job_rec_t *job_ptr;
	uint32_t ncpus;
	uint32_t nnodes;
	char *nodes;
	uint32_t ntasks;
	char *pid_str;
	uint32_t req_cpufreq;
	uint32_t requid;
	time_t start;
	enum job_states	state;
	slurmdb_stats_t stats;
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

/* slurmdb_stats_t defined above alphabetical */

typedef struct {
	List cluster_list; /* list of char * */
	List description_list; /* list of char * */
	uint32_t flags;
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
 * user_limit_list. */
typedef struct {
	uint64_t cpu_run_mins; /* how many cpu mins are allocated
				* currently */
	uint32_t cpus; /* count of CPUs allocated */
	uint32_t jobs;	/* count of active jobs */
	uint32_t nodes;	/* count of nodes allocated */
	uint32_t submit_jobs; /* count of jobs pending or running */
	uint32_t uid;
} slurmdb_used_limits_t;

typedef struct {
	uint16_t admin_level; /* really slurmdb_admin_level_t but for
				 packing purposes needs to be uint16_t */
	slurmdb_association_cond_t *assoc_cond; /* use user_list here for
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
	List accounting_list; 	/* list of slurmdb_accounting_rec_t *'s */

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
	slurmdb_association_rec_t *assoc;
	char *sort_name;
	List children;
} slurmdb_hierarchical_rec_t;

/************** report specific structures **************/

typedef struct {
	char *acct;
	char *cluster;
	uint64_t consumed_energy;
	uint64_t cpu_secs;
	char *parent_acct;
	char *user;
} slurmdb_report_assoc_rec_t;

typedef struct {
	char *acct;
	List acct_list; /* list of char *'s */
	List assoc_list; /* list of slurmdb_report_assoc_rec_t's */
	uint64_t consumed_energy;
	uint64_t cpu_secs;
	char *name;
	uid_t uid;
} slurmdb_report_user_rec_t;

typedef struct {
	List assoc_list; /* list of slurmdb_report_assoc_rec_t *'s */
	uint64_t consumed_energy;
	uint32_t cpu_count;
	uint64_t cpu_secs;
	char *name;
	List user_list; /* list of slurmdb_report_user_rec_t *'s */
} slurmdb_report_cluster_rec_t;

typedef struct {
	List jobs; /* This should be a NULL destroy since we are just
		    * putting a pointer to a slurmdb_job_rec_t here
		    * not allocating any new memory */
	uint32_t min_size; /* smallest size of job in cpus here 0 if first */
	uint32_t max_size; /* largest size of job in cpus here INFINITE if
			    * last */
	uint32_t count; /* count of jobs */
	uint64_t cpu_secs; /* how many cpus secs taken up by this
			    * grouping */
} slurmdb_report_job_grouping_t;

typedef struct {
	char *acct; /*account name */
	uint32_t count; /* total count of jobs taken up by this acct */
	uint64_t cpu_secs; /* how many cpus secs taken up by this
			    * acct */
	List groups; /* containing slurmdb_report_job_grouping_t's*/
	uint32_t lft;
	uint32_t rgt;
} slurmdb_report_acct_grouping_t;

typedef struct {
	char *cluster; /*cluster name */
	uint32_t count; /* total count of jobs taken up by this cluster */
	uint64_t cpu_secs; /* how many cpus secs taken up by this
			    * cluster */
	List acct_list; /* containing slurmdb_report_acct_grouping_t's */
} slurmdb_report_cluster_grouping_t;

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
 * IN:  association_list List of slurmdb_association_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_associations_add(void *db_conn, List assoc_list);

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_associations_get(void *db_conn,
				     slurmdb_association_cond_t *assoc_cond);

/*
 * modify existing associations in the accounting system
 * IN:  slurmdb_association_cond_t *assoc_cond
 * IN:  slurmdb_association_rec_t *assoc
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_associations_modify(void *db_conn,
					slurmdb_association_cond_t *assoc_cond,
					slurmdb_association_rec_t *assoc);

/*
 * remove associations from accounting system
 * IN:  slurmdb_association_cond_t *assoc_cond
 * RET: List containing (char *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_associations_remove(
	void *db_conn, slurmdb_association_cond_t *assoc_cond);

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
 * IN: slurmdb_association_cond_t *assoc_cond
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_cluster_account_by_user(void *db_conn,
						   slurmdb_association_cond_t *assoc_cond);

/* report for clusters of users per account
 * IN: slurmdb_association_cond_t *assoc_cond
 * RET: List containing (slurmdb_report_cluster_rec_t *'s) else NULL on error
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_report_cluster_user_by_account(void *db_conn,
						   slurmdb_association_cond_t *assoc_cond);

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
							    slurmdb_job_cond_t *job_cond, List grouping_list, bool flat_view);

extern List slurmdb_report_job_sizes_grouped_by_wckey(void *db_conn,
						      slurmdb_job_cond_t *job_cond, List grouping_list);

extern List slurmdb_report_job_sizes_grouped_by_top_account_then_wckey(
	void *db_conn, slurmdb_job_cond_t *job_cond,
	List grouping_list, bool flat_view);


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
 * release connection to the storage unit
 * IN/OUT: void ** pointer returned from
 *         slurmdb_connection_get() which will be freed.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_connection_close(void **db_conn);


/************** coordinator functions **************/

/*
 * add users as account coordinators
 * IN: acct_list list of char *'s of names of accounts
 * IN:  slurmdb_user_cond_t *user_cond
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_coord_add(void *db_conn, List acct_list,
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

/************** extra get functions **************/

/*
 * reconfigure the slurmdbd
 * RET: List of config_key_pairs_t *
 * note List needs to be freed when called
 */
extern int slurmdb_reconfig(void *db_conn);

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
 * returns List of slurmdb_job_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_jobs_get(void *db_conn, slurmdb_job_cond_t *job_cond);

/*
 * get info from the storage
 * IN:  slurmdb_association_cond_t *
 * RET: List of slurmdb_association_rec_t *
 * note List needs to be freed with slurm_list_destroy() when called
 */
extern List slurmdb_problems_get(void *db_conn,
				 slurmdb_association_cond_t *assoc_cond);

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
 * IN: cluster_names - comman separated string of cluster names
 * RET: List of slurmdb_cluster_rec_t *
 * note List needs to bbe freed with slurm_list_destroy() when called
 */
extern List slurmdb_get_info_cluster(char *cluster_names);

/************** helper functions **************/
extern void slurmdb_destroy_user_defs(void *object);
extern void slurmdb_destroy_user_rec(void *object);
extern void slurmdb_destroy_account_rec(void *object);
extern void slurmdb_destroy_coord_rec(void *object);
extern void slurmdb_destroy_clus_res_rec(void *object);
extern void slurmdb_destroy_cluster_accounting_rec(void *object);
extern void slurmdb_destroy_cluster_rec(void *object);
extern void slurmdb_destroy_accounting_rec(void *object);
extern void slurmdb_destroy_association_rec(void *object);
extern void slurmdb_destroy_event_rec(void *object);
extern void slurmdb_destroy_job_rec(void *object);
extern void slurmdb_destroy_qos_rec(void *object);
extern void slurmdb_destroy_reservation_rec(void *object);
extern void slurmdb_destroy_step_rec(void *object);
extern void slurmdb_destroy_res_rec(void *object);
extern void slurmdb_destroy_txn_rec(void *object);
extern void slurmdb_destroy_wckey_rec(void *object);
extern void slurmdb_destroy_archive_rec(void *object);
extern void slurmdb_destroy_report_assoc_rec(void *object);
extern void slurmdb_destroy_report_user_rec(void *object);
extern void slurmdb_destroy_report_cluster_rec(void *object);

extern void slurmdb_destroy_user_cond(void *object);
extern void slurmdb_destroy_account_cond(void *object);
extern void slurmdb_destroy_cluster_cond(void *object);
extern void slurmdb_destroy_association_cond(void *object);
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

extern void slurmdb_init_association_rec(slurmdb_association_rec_t *assoc,
					 bool free_it);
extern void slurmdb_init_clus_res_rec(slurmdb_clus_res_rec_t *clus_res,
				      bool free_it);
extern void slurmdb_init_cluster_rec(slurmdb_cluster_rec_t *cluster,
				     bool free_it);
extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos,
				 bool free_it);
extern void slurmdb_init_res_rec(slurmdb_res_rec_t *res,
				 bool free_it);
extern void slurmdb_init_wckey_rec(slurmdb_wckey_rec_t *wckey,
				   bool free_it);
extern void slurmdb_init_cluster_cond(slurmdb_cluster_cond_t *cluster,
				      bool free_it);
extern void slurmdb_init_res_cond(slurmdb_res_cond_t *cluster,
				  bool free_it);

/* The next two functions have pointers to assoc_list so do not
 * destroy assoc_list before using the list returned from this function.
 */
extern List slurmdb_get_hierarchical_sorted_assoc_list(List assoc_list);
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
extern int slurmdb_res_add(void *db_conn, uint32_t uid, List res_list);

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
extern int slurmdb_qos_add(void *db_conn, uint32_t uid, List qos_list);

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
extern int slurmdb_usage_get(void *db_conn,  void *in, int type,
			     time_t start, time_t end);

/*
 * roll up data in the storage
 * IN: sent_start (option time to do a re-roll or start from this point)
 * IN: sent_end (option time to do a re-roll or end at this point)
 * IN: archive_data (if 0 old data is not archived in a monthly rollup)
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_usage_roll(void *db_conn,
			      time_t sent_start, time_t sent_end,
			      uint16_t archive_data);

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

END_C_DECLS

#endif /* !_SLURMDB_H */
