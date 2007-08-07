/*****************************************************************************\
 *  slurm_jobacct.h - implementation-independent job completion logging 
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.com> et. al.
 *  UCRL-CODE-226842.
 *  
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *       This file is derived from the file slurm_JOBACCT.c, written by
 *       Morris Jette, et al.
\*****************************************************************************/


#ifndef __SLURM_JOBACCT_H__
#define __SLURM_JOBACCT_H__

#if HAVE_STDINT_H
#  include <stdint.h>           /* for uint16_t, uint32_t definitions */
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>         /* for uint16_t, uint32_t definitions */
#endif
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmctld/slurmctld.h"

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

extern jobacct_step_rec_t *jobacct_init_step_rec(jobacct_header_t header);
extern jobacct_job_rec_t *jobacct_init_job_rec(jobacct_header_t header);
extern void jobacct_destroy_acct_header(void *object);
extern void jobacct_destroy_job(void *object);
extern void jobacct_destroy_step(void *object);

/* common */
extern int jobacct_init(void); /* load the plugin */
extern int jobacct_g_init_struct(jobacctinfo_t *jobacct, 
				 jobacct_id_t *jobacct_id);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_g_alloc(jobacct_id_t *jobacct_id);
extern void jobacct_g_free(jobacctinfo_t *jobacct);
extern int jobacct_g_setinfo(jobacctinfo_t *jobacct, 
			     enum jobacct_data_type type, void *data);
extern int jobacct_g_getinfo(jobacctinfo_t *jobacct, 
			     enum jobacct_data_type type, void *data);
extern void jobacct_g_aggregate(jobacctinfo_t *dest, jobacctinfo_t *from);
extern void jobacct_g_2_sacct(sacct_t *sacct, jobacctinfo_t *jobacct);
extern void jobacct_g_pack(jobacctinfo_t *jobacct, Buf buffer);
extern int jobacct_g_unpack(jobacctinfo_t **jobacct, Buf buffer);

/*functions used in slurmctld */
extern int jobacct_g_init_slurmctld(char *job_acct_log);
extern int jobacct_g_fini_slurmctld();
extern int jobacct_g_job_start_slurmctld(struct job_record *job_ptr);
extern int jobacct_g_job_complete_slurmctld(struct job_record *job_ptr); 
extern int jobacct_g_step_start_slurmctld(struct step_record *step);
extern int jobacct_g_step_complete_slurmctld(struct step_record *step);
extern int jobacct_g_suspend_slurmctld(struct job_record *job_ptr);

/*functions used in slurmstepd */
extern int jobacct_g_startpoll(int frequency);
extern int jobacct_g_endpoll();
extern int jobacct_g_set_proctrack_container_id(uint32_t id);
extern int jobacct_g_add_task(pid_t pid, jobacct_id_t *jobacct_id);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_g_stat_task(pid_t pid);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_g_remove_task(pid_t pid);
extern void jobacct_g_suspend_poll();
extern void jobacct_g_resume_poll();

#endif /*__SLURM_JOBACCT_H__*/

