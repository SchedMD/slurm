/*****************************************************************************\
 *  select.h - resource selection plugin wrapper.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef _INTERFACES_SELECT_H
#define _INTERFACES_SELECT_H

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/list.h"
#include "src/common/plugrack.h"
#include "src/slurmctld/slurmctld.h"

typedef struct avail_res {	/* Per-node resource availability */
	uint16_t avail_cpus;	/* Count of available CPUs for this job
				   limited by options like --ntasks-per-node */
	uint16_t avail_gpus;	/* Count of available GPUs */
	uint16_t avail_res_cnt;	/* Count of available CPUs + GPUs */
	uint16_t *avail_cores_per_sock;	/* Per-socket available core count */
	uint32_t gres_min_cpus; /* Minimum required cpus for gres */
	uint32_t gres_max_tasks; /* Maximum tasks for gres */
	uint16_t max_cpus;	/* Maximum available CPUs on the node */
	uint16_t min_cpus;	/* Minimum allocated CPUs */
	uint16_t sock_cnt;	/* Number of sockets on this node */
	list_t *sock_gres_list;	/* Per-socket GRES availability, sock_gres_t */
	uint16_t spec_threads;	/* Specialized threads to be reserved */
	uint16_t tpc;		/* Threads/cpus per core */
} avail_res_t;

typedef struct will_run_data {
	time_t start;
	time_t end;
} will_run_data_t;

extern bool running_cons_tres(void);

/*******************************************\
 * GLOBAL SELECT STATE MANAGEMENT FUNCTIONS *
\*******************************************/

/*
 * Initialize context for node selection plugin
 */
extern int select_g_init(void);

/*
 * Terminate plugin and free all associated memory
 */
extern int select_g_fini(void);

/*
 * Convert SelectTypeParameter to equivalent string
 * NOTE: Not reentrant
 */
extern char *select_type_param_string(uint16_t select_type_param);

/*********************************\
 * STATE INITIALIZATION FUNCTIONS *
\*********************************/

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(void);

/* Note reconfiguration or change in partition configuration */
extern int select_g_reconfigure(void);

/**************************\
 * NODE SPECIFIC FUNCTIONS *
\**************************/

/* Reset select plugin specific information about a job
 * IN job_ptr - The updated job */
extern int select_g_select_nodeinfo_set(job_record_t *job_ptr);

/* Update select plugin information about every node as needed (if changed since
 * previous query) */
extern int select_g_select_nodeinfo_set_all(void);

/******************************************************\
 * JOB SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCTIONS *
\******************************************************/

#define SELECT_MODE_BASE         0x00ff
#define SELECT_MODE_FLAGS        0xff00

#define SELECT_MODE_RUN_NOW	 0x0000
#define SELECT_MODE_TEST_ONLY	 0x0001
#define SELECT_MODE_WILL_RUN	 0x0002
#define SELECT_MODE_RESV	 0x0004

#define SELECT_MODE_PREEMPT_FLAG 0x0100
#define SELECT_MODE_CHECK_FULL   0x0200
#define SELECT_MODE_IGN_ERR      0x0400

#define SELECT_IS_MODE_RUN_NOW(_X) \
	(((_X & SELECT_MODE_BASE) == SELECT_MODE_RUN_NOW) \
	 && !SELECT_IS_PREEMPT_ON_FULL_TEST(_X))

#define SELECT_IS_MODE_TEST_ONLY(_X) \
	(_X & SELECT_MODE_TEST_ONLY)

#define SELECT_IS_MODE_WILL_RUN(_X) \
	(_X & SELECT_MODE_WILL_RUN || SELECT_IS_MODE_RESV(_X))

#define SELECT_IS_MODE_RESV(_X) \
	(_X & SELECT_MODE_RESV)

#define SELECT_IGN_ERR(_X) \
	(_X & SELECT_MODE_IGN_ERR)

#define SELECT_IS_PREEMPT_SET(_X) \
	(_X & SELECT_MODE_PREEMPT_FLAG)

#define SELECT_IS_CHECK_FULL_SET(_X) \
	(_X & SELECT_MODE_CHECK_FULL)

#define SELECT_IS_TEST(_X) \
	(SELECT_IS_MODE_TEST_ONLY(_X) || SELECT_IS_MODE_WILL_RUN(_X))

#define SELECT_IS_PREEMPT_ON_FULL_TEST(_X) \
	(SELECT_IS_CHECK_FULL_SET(_X) && SELECT_IS_PREEMPT_SET(_X))

#define SELECT_IS_PREEMPTABLE_TEST(_X) \
	((SELECT_IS_MODE_TEST_ONLY(_X) || SELECT_IS_MODE_WILL_RUN(_X))	\
	 && SELECT_IS_PREEMPT_SET(_X))

/*
 * packs the select plugin_id for backwards compatibility
 * Remove when 24.11 is no longer supported.
 */
extern void select_plugin_id_pack(buf_t *buffer);

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can bee preempted
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 *		Existing list is appended to.
 * IN resv_exc_ptr - Various TRES which the job can NOT use.
 * IN will_run_ptr - Pointer to data specific to WILL_RUN mode
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(job_record_t *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     list_t *preemptee_candidates,
			     list_t **preemptee_job_list,
			     resv_exc_t *resv_exc_ptr,
			     will_run_data_t *will_run_ptr);

/*
 * Note initiation of job is about to begin. Called immediately
 * after select_g_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_g_job_begin(job_record_t *job_ptr);

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET -1 on error, 1 if ready to execute, 0 otherwise
 */
extern int select_g_job_ready(job_record_t *job_ptr);

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(job_record_t *job_ptr);

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * IN indf_susp - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(job_record_t *job_ptr, bool indf_susp);

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * IN indf_susp - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(job_record_t *job_ptr, bool indf_susp);

/*
 * Move the resource allocated to one job into that of another job.
 *	All resources are removed from "from_job_ptr" and moved into
 *	"to_job_ptr". Also see other_job_resized().
 * RET: 0 or an error code
 */
extern int select_g_job_expand(job_record_t *from_job_ptr,
			       job_record_t *to_job_ptr);

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_g_job_resized(job_record_t *job_ptr, node_record_t *node_ptr);

#endif
