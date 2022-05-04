/*****************************************************************************\
 **  setup.h - MPI/PMI2 plugin setup
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
 *  Written by Artem Y. Polyakov <artemp@mellanox.com>.
 *  All rights reserved.
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

#ifndef _SETUP_H
#define _SETUP_H

#include <inttypes.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/pack.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_mpi.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/srun/libsrun/debugger.h"
#include "src/srun/libsrun/opt.h"

#include "tree.h"

typedef struct pmi2_job_info {
	slurm_step_id_t step_id; /* Current step id struct            */
	uid_t uid; /* user id for job */
	uint32_t nnodes; /* number of nodes in current job step       */
	uint32_t nodeid; /* relative position of this node in job     */
	uint32_t ntasks; /* total number of tasks in current job      */
	uint32_t ltasks; /* number of tasks on *this* (local) node    */
	uint32_t *gtids; /* global task ids of the tasks              */
	uint32_t spawn_seq;	/* seq of spawn. 0 if not spawned */

	int pmi_debugged;    /* whether output verbose PMI messages */
	char *step_nodelist; /* list of nodes in this job step */
	char *proc_mapping;  /* processor mapping */
	char *pmi_jobid;     /* PMI job id */
	char *spawner_jobid; /* spawner pmi job id */
	char **job_env;	     /* environment of job. use in stepd */

	MPIR_PROCDESC *MPIR_proctable;	/* used only in srun */
	slurm_opt_t *srun_opt;		/* used only in srun */
	char *resv_ports; /* MPI reserved ports */
} pmi2_job_info_t;

typedef struct pmi2_tree_info {
	char *this_node;	/* this nodename */
	char *parent_node;	/* parent nodename */
	int   parent_id;	/* parent nodeid */
	int   num_children;	/* number of children stepds */
	int   depth;		/* depth in tree */
	int   max_depth;	/* max depth of the tree */
	uint16_t pmi_port;	 /* PMI2 comm port of this srun */
	slurm_addr_t *srun_addr; /* PMI2 comm address parent srun */
	uint32_t *children_kvs_seq; /* sequence number of children nodes */
} pmi2_tree_info_t;


extern pmi2_job_info_t job_info;
extern pmi2_tree_info_t tree_info;
extern char tree_sock_addr[];
extern int  tree_sock;
extern int *task_socks;
#define STEPD_PMI_SOCK(lrank) task_socks[lrank * 2]
#define TASK_PMI_SOCK(lrank) task_socks[lrank * 2 + 1]

extern bool in_stepd(void);
extern int  pmi2_setup_stepd(const stepd_step_rec_t *job, char ***env);
extern int  pmi2_setup_srun(const mpi_plugin_client_info_t *job, char ***env);
extern void pmi2_cleanup_stepd(void);

#endif	/* _SETUP_H */
