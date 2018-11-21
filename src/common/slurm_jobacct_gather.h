/*****************************************************************************\
 *  slurm_jobacct_gather.h - implementation-independent job completion logging
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.com> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *       This file is derived from the file slurm_JOBACCT.c, written by
 *       Morris Jette, et al.
\*****************************************************************************/

#ifndef __SLURM_JOBACCT_GATHER_H__
#define __SLURM_JOBACCT_GATHER_H__

#include <inttypes.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_acct_gather.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#define PROTOCOL_TYPE_SLURM 0
#define PROTOCOL_TYPE_DBD 1

#define CPU_TIME_ADJ 1000

typedef struct {
	uint32_t taskid; /* contains which task number it was on */
	uint32_t nodeid; /* contains which node number it was on */
	stepd_step_rec_t *job; /* contains stepd job pointer */
} jobacct_id_t;

struct jobacctinfo {
	pid_t pid;
	uint32_t sys_cpu_sec;
	uint32_t sys_cpu_usec;
	uint32_t user_cpu_sec;
	uint32_t user_cpu_usec;
	uint32_t act_cpufreq; /* actual cpu frequency */
	acct_gather_energy_t energy;
	double last_total_cputime;
	double this_sampled_cputime;
	uint32_t current_weighted_freq;
	uint32_t current_weighted_power;
	uint32_t tres_count; /* count of tres in the usage array's */
	uint32_t *tres_ids; /* array of tres_count of the tres id's */
	List tres_list; /* list of tres we are dealing with */
	uint64_t *tres_usage_in_max; /* tres max usage in data */
	uint64_t *tres_usage_in_max_nodeid; /* tres max usage in data node id */
	uint64_t *tres_usage_in_max_taskid; /* tres max usage in data task id */
	uint64_t *tres_usage_in_min; /* tres min usage in data */
	uint64_t *tres_usage_in_min_nodeid; /* tres min usage in data node id */
	uint64_t *tres_usage_in_min_taskid; /* tres min usage in data task id */
	uint64_t *tres_usage_in_tot; /* total usage in, in megabytes */
	uint64_t *tres_usage_out_max; /* tres max usage out data */
	uint64_t *tres_usage_out_max_nodeid; /* tres max usage data node id */
	uint64_t *tres_usage_out_max_taskid; /* tres max usage data task id */
	uint64_t *tres_usage_out_min; /* tres min usage out data */
	uint64_t *tres_usage_out_min_nodeid; /* tres min usage data node id */
	uint64_t *tres_usage_out_min_taskid; /* tres min usage data task id */
	uint64_t *tres_usage_out_tot; /* total usage out, in megabytes */

	jobacct_id_t id;
	int dataset_id; /* dataset associated to this task when profiling */

	/* FIXME: these need to be arrays like above */
	double last_tres_usage_in_tot;
	double last_tres_usage_out_tot;
	time_t cur_time;
	time_t last_time;
};

/* Define jobacctinfo_t below to avoid including extraneous slurm headers */
#ifndef __jobacctinfo_t_defined
#  define  __jobacctinfo_t_defined
   typedef struct jobacctinfo jobacctinfo_t;     /* opaque data type */
#endif

/*
 * FIXME: hopefully this will not exist and we can use
 * slurmdb_make_tres_string_from_arrays instead.  Not sure why this is here in
 * the first place as tres_usage_types_t is defined in slurmdb_defs.h.
 */
extern char *make_tres_usage_str_from_array(uint64_t *tres_cnt);

extern int jobacct_gather_init(void); /* load the plugin */
extern int jobacct_gather_fini(void); /* unload the plugin */

extern int  jobacct_gather_startpoll(uint16_t frequency);
extern int  jobacct_gather_endpoll(void);
extern void jobacct_gather_suspend_poll(void);
extern void jobacct_gather_resume_poll(void);

extern int jobacct_gather_add_task(pid_t pid, jobacct_id_t *jobacct_id,
				   int poll);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_gather_stat_task(pid_t pid);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_gather_remove_task(pid_t pid);

extern int jobacct_gather_set_proctrack_container_id(uint64_t id);
extern int jobacct_gather_set_mem_limit(uint32_t job_id,
					uint32_t step_id,
					uint64_t mem_limit);
extern void jobacct_gather_handle_mem_limit(uint64_t total_job_mem,
					    uint64_t total_job_vsize);

extern jobacctinfo_t *jobacctinfo_create(jobacct_id_t *jobacct_id);
extern void jobacctinfo_destroy(void *object);
extern int jobacctinfo_setinfo(jobacctinfo_t *jobacct,
			       enum jobacct_data_type type, void *data,
			       uint16_t protocol_version);
extern int jobacctinfo_getinfo(jobacctinfo_t *jobacct,
			       enum jobacct_data_type type, void *data,
			       uint16_t protocol_version);
extern void jobacctinfo_pack(jobacctinfo_t *jobacct,
			     uint16_t rpc_version,
			     uint16_t protocol_type, Buf buffer);
extern int jobacctinfo_unpack(jobacctinfo_t **jobacct,
			      uint16_t rpc_version,
			      uint16_t protocol_type, Buf buffer, bool alloc);

extern void jobacctinfo_aggregate(jobacctinfo_t *dest, jobacctinfo_t *from);

extern void jobacctinfo_2_stats(slurmdb_stats_t *stats, jobacctinfo_t *jobacct);

extern void jobacct_common_free_jobacct(void *object);

#endif /*__SLURM_JOBACCT_GATHER_H__*/
