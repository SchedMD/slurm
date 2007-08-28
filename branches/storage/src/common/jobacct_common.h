/*****************************************************************************\
 *  jobacct_common.h - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifndef _HAVE_JOBACCT_COMMON_H
#define _HAVE_JOBACCT_COMMON_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <dirent.h>
#include <sys/stat.h>

#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"


#include <ctype.h>

#define BUFFER_SIZE 4096

typedef struct {
	uint16_t taskid; /* contains which task number it was on */
	uint32_t nodeid; /* contains which node number it was on */	
} jobacct_id_t;

typedef struct {
	uint32_t max_vsize; 
	jobacct_id_t max_vsize_id;
	float ave_vsize;
	uint32_t max_rss;
	jobacct_id_t max_rss_id;
	float ave_rss;
	uint32_t max_pages;
	jobacct_id_t max_pages_id;
	float ave_pages;
	float min_cpu;
	jobacct_id_t min_cpu_id;
	float ave_cpu;	
} sacct_t;

typedef struct {
	int opt_completion;	/* --completion */
	int opt_dump;		/* --dump */
	int opt_dup;		/* --duplicates; +1 = explicitly set */
	int opt_fdump;		/* --formattted_dump */
	int opt_stat;		/* --stat */
	int opt_gid;		/* --gid (-1=wildcard, 0=root) */
	int opt_header;		/* can only be cleared */
	int opt_help;		/* --help */
	int opt_long;		/* --long */
	int opt_lowmem;		/* --low_memory */
	int opt_purge;		/* --purge */
	int opt_total;		/* --total */
	int opt_uid;		/* --uid (-1=wildcard, 0=root) */
	int opt_verbose;	/* --verbose */
	long opt_expire;		/* --expire= */ 
	char *opt_expire_timespec; /* --expire= */
	char *opt_field_list;	/* --fields= */
	char *opt_filein;	/* --file */
	char *opt_job_list;	/* --jobs */
	char *opt_partition_list;/* --partitions */
	char *opt_state_list;	/* --states */
} sacct_parameters_t;

typedef struct header {
	uint32_t jobnum;
	char	*partition;
	char	*blockid;
	time_t 	job_submit;
	time_t	timestamp;
	uint32_t uid;
	uint32_t gid;
	uint16_t rec_type;
} jobacct_header_t;

typedef struct {
	uint32_t job_start_seen,		/* useful flags */
		job_step_seen,
		job_terminated_seen,
		jobnum_superseded;	/* older jobnum was reused */
	jobacct_header_t header;
	uint16_t show_full;
	char	*nodes;
	char	*jobname;
	uint16_t track_steps;
	int32_t priority;
	uint32_t ncpus;
	uint32_t ntasks;
	enum job_states	status;
	int32_t	exitcode;
	uint32_t elapsed;
	time_t end;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	struct rusage rusage;
	sacct_t sacct;
	List    steps;
	char    *account;
	uint32_t requid;
} jobacct_job_rec_t;

typedef struct {
	jobacct_header_t   header;
	uint32_t	stepnum;	/* job's step number */
	char	        *nodes;
	char	        *stepname;
	enum job_states	status;
	int32_t	        exitcode;
	uint32_t	ntasks; 
	uint32_t        ncpus;
	uint32_t	elapsed;
	time_t          end;
	uint32_t	tot_cpu_sec;
	uint32_t        tot_cpu_usec;
	struct rusage   rusage;
	sacct_t         sacct;
	char            *account;
	uint32_t requid;
} jobacct_step_rec_t;

typedef struct selected_step_t {
	char *job;
	char *step;
	uint32_t jobid;
	uint32_t stepid;
} jobacct_selected_step_t;

struct jobacctinfo {
	pid_t pid;
	struct rusage rusage; /* returned by wait3 */
	uint32_t max_vsize; /* max size of virtual memory */
	jobacct_id_t max_vsize_id; /* contains which task number it was on */
	uint32_t tot_vsize; /* total virtual memory 
			       (used to figure out ave later) */
	uint32_t max_rss; /* max Resident Set Size */
	jobacct_id_t max_rss_id; /* contains which task it was on */
	uint32_t tot_rss; /* total rss 
			     (used to figure out ave later) */
	uint32_t max_pages; /* max pages */
	jobacct_id_t max_pages_id; /* contains which task it was on */
	uint32_t tot_pages; /* total pages
			     (used to figure out ave later) */ 
	uint32_t min_cpu; /* min cpu time */
	jobacct_id_t min_cpu_id; /* contains which task it was on */
	uint32_t tot_cpu; /* total cpu time 
				 (used to figure out ave later) */
};

/* Define jobacctinfo_t below to avoid including extraneous slurm headers */
#ifndef __jobacctinfo_t_defined
#  define  __jobacctinfo_t_defined
   typedef struct jobacctinfo *jobacctinfo_t;     /* opaque data type */
#endif

extern jobacct_step_rec_t *create_jobacct_step_rec(jobacct_header_t header);
extern jobacct_job_rec_t *create_jobacct_job_rec(jobacct_header_t header);
extern void free_jobacct_header(void *object);
extern void destroy_jobacct_job_rec(void *object);
extern void destroy_jobacct_step_rec(void *object);


/* These should only be called from the jobacct-gather plugin */
extern int common_init_struct(struct jobacctinfo *jobacct, 
			      jobacct_id_t *jobacct_id);
extern struct jobacctinfo *common_alloc_jobacct(jobacct_id_t *jobacct_id);
extern void common_free_jobacct(void *object);
extern int common_setinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data);
extern int common_getinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data);
extern void common_aggregate(struct jobacctinfo *dest, 
			     struct jobacctinfo *from);
extern void common_2_sacct(sacct_t *sacct, struct jobacctinfo *jobacct);
extern void common_pack(struct jobacctinfo *jobacct, Buf buffer);
extern int common_unpack(struct jobacctinfo **jobacct, Buf buffer);

extern int common_endpoll();
extern int common_set_proctrack_container_id(uint32_t id);
extern int common_add_task(pid_t pid, jobacct_id_t *jobacct_id);
extern struct jobacctinfo *common_stat_task(pid_t pid);
extern struct jobacctinfo *common_remove_task(pid_t pid);
extern void common_suspend_poll();
extern void common_resume_poll();
/***************************************************************/


/* defined in common_jobacct.c */
extern bool jobacct_shutdown;
extern bool jobacct_suspended;
extern List task_list;
extern pthread_mutex_t jobacct_lock;
extern uint32_t cont_id;
extern bool pgid_plugin;

#endif
