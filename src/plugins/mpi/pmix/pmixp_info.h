/*****************************************************************************\
 **  pmix_info.h - PMIx various environment information
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#ifndef PMIXP_INFO_H
#define PMIXP_INFO_H

#include "pmixp_common.h"

/*
 *  Slurm job and job-step information
 */

typedef struct {
#ifndef NDEBUG
#define PMIX_INFO_MAGIC 0xCAFEFACE
	int magic;
#endif
	char nspace[PMIX_MAX_NSLEN];
	uint32_t jobid; /* Current SLURM job id                         */
	uint32_t stepid; /* Current step id (or NO_VAL)                  */
	uint32_t nnodes; /* number of nodes in current step              */
	uint32_t nnodes_job; /* number of nodes in current job               */
	uint32_t ntasks; /* total number of tasks in current step        */
	uint32_t ntasks_job; /* total possible number of tasks in job        */
	uint32_t ncpus_job; /* total possible number of cpus in job         */
	uint32_t *task_cnts; /* Number of tasks on each node in this step    */
	int node_id; /* relative position of this node in this step  */
	int node_id_job; /* relative position of this node in SLURM job  */
	hostlist_t job_hl;
	hostlist_t step_hl;
	char *hostname;
	uint32_t node_tasks; /* number of tasks on *this* node               */
	uint32_t *gtids; /* global ids of tasks located on *this* node   */
	char *task_map_packed; /* string represents packed task mapping information */
	int timeout;
	char *cli_tmpdir;
	char *lib_tmpdir;
} pmix_jobinfo_t;

extern pmix_jobinfo_t _pmixp_job_info;

/* slurmd contact information */
void pmixp_info_srv_contacts(char *path, int fd);
const char *pmixp_info_srv_addr(void);
int pmixp_info_srv_fd(void);

static inline int pmixp_info_timeout(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.timeout;
}

/* My hostname */
static inline char *pmixp_info_hostname(void)
{
	return _pmixp_job_info.hostname;
}

/* Cli tempdir */
static inline char *pmixp_info_tmpdir_cli(void)
{
	return _pmixp_job_info.cli_tmpdir;
}

/* Cli tempdir */
static inline char *pmixp_info_tmpdir_lib(void)
{
	return _pmixp_job_info.lib_tmpdir;
}

/* Dealing with I/O */
void pmixp_info_io_set(eio_handle_t *h);
eio_handle_t *pmixp_info_io();

/* Job information */
int pmixp_info_set(const stepd_step_rec_t *job, char ***env);
int pmixp_info_free(void);

static inline uint32_t pmixp_info_jobid(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.jobid;
}

static inline uint32_t pmixp_info_stepid(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.stepid;
}

static inline char *pmixp_info_namespace(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.nspace;
}

static inline uint32_t pmixp_info_nodeid(void)
{
	/* This routine is called from PMIX_DEBUG/ERROR and
	 * this CAN happen before initialization. Relax demand to have
	 * _pmix_job_info.magic == PMIX_INFO_MAGIC
	 * ! xassert(_pmix_job_info.magic == PMIX_INFO_MAGIC);
	 */
	return _pmixp_job_info.node_id;
}

static inline uint32_t pmixp_info_nodeid_job(void)
{
	/* This routine is called from PMIX_DEBUG/ERROR and
	 * this CAN happen before initialization. Relax demand to have
	 * _pmix_job_info.magic == PMIX_INFO_MAGIC
	 * ! xassert(_pmix_job_info.magic == PMIX_INFO_MAGIC);
	 */
	return _pmixp_job_info.node_id_job;
}

static inline uint32_t pmixp_info_nodes(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.nnodes;
}

static inline uint32_t pmixp_info_nodes_uni(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.nnodes_job;
}

static inline uint32_t pmixp_info_tasks(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.ntasks;
}

static inline uint32_t pmixp_info_tasks_node(uint32_t nodeid)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	xassert(nodeid < _pmixp_job_info.nnodes);
	return _pmixp_job_info.task_cnts[nodeid];
}

static inline uint32_t *pmixp_info_tasks_cnts(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.task_cnts;
}

static inline uint32_t pmixp_info_tasks_loc(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.node_tasks;
}

static inline uint32_t pmixp_info_tasks_uni(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.ntasks_job;
}

static inline uint32_t pmixp_info_cpus(void)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	return _pmixp_job_info.ncpus_job;
}

static inline uint32_t pmixp_info_taskid(uint32_t localid)
{
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	xassert(localid < _pmixp_job_info.node_tasks);
	return _pmixp_job_info.gtids[localid];
}

/*
 * Since tasks array in SLURM job structure is uint16_t
 * task local id can't be grater than 2^16. So we can
 * safely return int here. We need (-1) for the not-found case
 */
static inline int pmixp_info_taskid2localid(uint32_t taskid)
{
	int i;
	xassert(_pmixp_job_info.magic == PMIX_INFO_MAGIC);
	xassert(taskid < _pmixp_job_info.ntasks);

	for (i = 0; i < _pmixp_job_info.node_tasks; i++) {
		if (_pmixp_job_info.gtids[i] == taskid)
			return i;
	}
	return -1;
}

static inline char *pmixp_info_task_map(void)
{
	return _pmixp_job_info.task_map_packed;
}

static inline hostlist_t pmixp_info_step_hostlist(void)
{
	return _pmixp_job_info.step_hl;
}

static inline char *pmixp_info_step_host(int nodeid)
{
	xassert(nodeid < _pmixp_job_info.nnodes);
	char *p = hostlist_nth(_pmixp_job_info.step_hl, nodeid);
	char *ret = xstrdup(p);
	free(p);
	return ret;
}

static inline char *pmixp_info_job_host(int nodeid)
{
	xassert(nodeid < _pmixp_job_info.nnodes_job);
	char *p = hostlist_nth(_pmixp_job_info.job_hl, nodeid);
	char *ret = xstrdup(p);
	free(p);
	return ret;
}

/* namespaces list operations */
static inline char *pmixp_info_nspace_usock(const char *nspace)
{
	char *usock = NULL;
	/* TODO: provide the right way to set this:
	 * - slrum.conf
	 * - what else?
	 */
	char *tmpdir = getenv("TMPDIR");
	if (NULL == tmpdir) {
		tmpdir = PMIXP_TMPDIR_DEFAULT;
	}
	xstrfmtcat(usock, "%s/sock.stepd.%s", tmpdir, nspace);
	return usock;
}

#endif /* PMIXP_INFO_H */
