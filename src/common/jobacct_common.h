/*****************************************************************************\
 *  jobacct_common.h - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
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

#include "slurm/slurmdb.h"

#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"


#include <ctype.h>

#define BUFFER_SIZE 4096
#define FDUMP_FLAG 0x04

typedef struct {
	uint16_t taskid; /* contains which task number it was on */
	uint32_t nodeid; /* contains which node number it was on */
} jobacct_id_t;

struct jobacctinfo {
	pid_t pid;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
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
   typedef struct jobacctinfo jobacctinfo_t;     /* opaque data type */
#endif

/* These should only be called from the jobacct-gather plugin */
extern int jobacct_common_init_struct(struct jobacctinfo *jobacct,
				      jobacct_id_t *jobacct_id);
extern struct jobacctinfo *jobacct_common_alloc_jobacct(
	jobacct_id_t *jobacct_id);
extern void jobacct_common_free_jobacct(void *object);
extern int jobacct_common_setinfo(struct jobacctinfo *jobacct,
			  enum jobacct_data_type type, void *data);
extern int jobacct_common_getinfo(struct jobacctinfo *jobacct,
			  enum jobacct_data_type type, void *data);
extern void jobacct_common_aggregate(struct jobacctinfo *dest,
			     struct jobacctinfo *from);
extern void jobacct_common_2_stats(slurmdb_stats_t *stats,
				   struct jobacctinfo *jobacct);
extern void jobacct_common_pack(struct jobacctinfo *jobacct,
				uint16_t rpc_version, Buf buffer);
extern int jobacct_common_unpack(struct jobacctinfo **jobacct,
				 uint16_t rpc_version, Buf buffer);

extern int jobacct_common_set_mem_limit(uint32_t job_id, uint32_t step_id,
					uint32_t mem_limit);
extern int jobacct_common_add_task(pid_t pid, jobacct_id_t *jobacct_id,
				   List task_list);
extern struct jobacctinfo *jobacct_common_stat_task(pid_t pid, List task_list);
extern struct jobacctinfo *jobacct_common_remove_task(pid_t pid,
						      List task_list);
/***************************************************************/


/* defined in common_jobacct.c */
extern pthread_mutex_t jobacct_lock;
extern uint32_t jobacct_job_id;
extern uint32_t jobacct_step_id;
extern uint32_t jobacct_mem_limit;	/* step's memory limit in KB */
extern uint32_t jobacct_vmem_limit;	/* step's virutal memory limit in KB */

#endif
