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

#include "src/common/slurm_jobacct.h"
#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"

#include "src/slurmd/common/proctrack.h"

#include <ctype.h>

#define BUFFER_SIZE 4096

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


/* in jobacct_common.c */
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

/* in common_slurmctld.c */
extern int common_init_slurmctld(char *job_acct_log);
extern int common_fini_slurmctld();
extern int common_job_start_slurmctld(struct job_record *job_ptr);
extern int common_job_complete_slurmctld(struct job_record *job_ptr);
extern int common_step_start_slurmctld(struct step_record *step);
extern int common_step_complete_slurmctld(struct step_record *step);
extern int common_suspend_slurmctld(struct job_record *job_ptr);

/* in common_slurmstepd.c */
extern int common_endpoll();
extern int common_set_proctrack_container_id(uint32_t id);
extern int common_add_task(pid_t pid, jobacct_id_t *jobacct_id);
extern struct jobacctinfo *common_stat_task(pid_t pid);
extern struct jobacctinfo *common_remove_task(pid_t pid);
extern void common_suspend_poll();
extern void common_resume_poll();

/* defined in common_slurmstepd.c */
extern bool jobacct_shutdown;
extern bool suspended;
extern List task_list;
extern pthread_mutex_t jobacct_lock;
extern uint32_t cont_id;
extern bool pgid_plugin;

#endif
