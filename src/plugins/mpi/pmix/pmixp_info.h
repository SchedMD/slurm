/*****************************************************************************\
 **  pmix_info.h - PMIx various environment information
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2020 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>,
 *             Boris Karasev <karasev.b@gmail.com, boriska@mellanox.com>.
 *  Copyright (C) 2020      Siberian State University of Telecommunications
 *                          and Information Sciences (SibSUTIS).
 *                          All rights reserved.
 *  Written by Boris Bochkarev <boris-bochkaryov@yandex.ru>.
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

#ifndef PMIXP_INFO_H
#define PMIXP_INFO_H

#include "pmixp_common.h"

/*
 *  Slurm job and job-step information
 */

typedef struct {
#ifndef NDEBUG
#define PMIXP_INFO_MAGIC 0xCAFE01F0
	int magic;
#endif
	char *task_dist;
	pmix_nspace_t nspace;
	slurm_step_id_t step_id; /* Current step id (or NO_VAL) */
	uint32_t *het_job_offset;
	uint32_t nnodes; /* number of nodes in current step */
	uint32_t nnodes_job; /* number of nodes in current job */
	uint32_t ntasks; /* total number of tasks in current step */
	uint32_t ntasks_job; /* total possible number of tasks in job */
	uint32_t ncpus_job; /* total possible number of cpus in job */
	uint32_t *task_cnts; /* Number of tasks on each node in this step */
	uint32_t app_ldr; /* first global rank of this het component */
	int node_id; /* relative position of this node in this step */
	int node_id_job; /* relative position of this node in Slurm job */
	hostlist_t *job_hl;
	hostlist_t *step_hl;
	char *hostname;
	uint32_t node_tasks; /* number of tasks on *this* node */
	uint32_t *gtids; /* global ids of tasks located on *this* node */
	char *task_map_packed; /* packed task mapping information */
	int timeout;
	char *cli_tmpdir, *cli_tmpdir_base;
	char *lib_tmpdir;
	char *client_lib_tmpdir; /* path to lib_tmpdir on client */
	char *cmd; /* command to execute and its arguments derived from argv */
	char *server_addr_unfmt;
	char *spool_dir;
	uid_t uid;
	gid_t gid;
	char *srun_ip;
	int abort_agent_port;
} pmix_jobinfo_t;

extern pmix_jobinfo_t _pmixp_job_info;

/* slurmd contact information */
extern void pmixp_info_srv_usock_set(char *path, int fd);
extern const char *pmixp_info_srv_usock_path(void);
extern int pmixp_info_srv_usock_fd(void);
extern bool pmixp_info_same_arch(void);
extern bool pmixp_info_srv_direct_conn(void);
extern bool pmixp_info_srv_direct_conn_early(void);
extern bool pmixp_info_srv_direct_conn_ucx(void);
extern int pmixp_info_srv_fence_coll_type(void);
extern bool pmixp_info_srv_fence_coll_barrier(void);

extern int pmixp_info_timeout();
extern char *pmixp_info_hostname();
extern char *pmixp_info_tmpdir_cli();
extern char *pmixp_info_tmpdir_cli_base();
extern char *pmixp_info_tmpdir_lib();
extern char *_pmixp_info_client_tmpdir_lib();

/* Dealing with I/O */
extern void pmixp_info_io_set(eio_handle_t *h);
extern eio_handle_t *pmixp_info_io(void);

/* Job information */
extern int pmixp_info_set(const stepd_step_rec_t *step, char ***env);
extern int pmixp_info_free(void);

extern char *pmixp_info_cmd();
extern uint32_t pmixp_info_jobuid();
extern uint32_t pmixp_info_jobgid();
extern uint32_t pmixp_info_jobid();
extern uint32_t pmixp_info_job_offset(int i);
extern char *pmixp_info_srun_ip();
extern int pmixp_info_abort_agent_port();
extern uint32_t pmixp_info_stepid();
extern char *pmixp_info_namespace();
extern uint32_t pmixp_info_nodeid();
extern uint32_t pmixp_info_nodeid_job();
extern uint32_t pmixp_info_nodes();
extern uint32_t pmixp_info_nodes_uni();
extern uint32_t pmixp_info_tasks();
extern uint32_t pmixp_info_tasks_node(uint32_t nodeid);
extern uint32_t *pmixp_info_tasks_cnts();
extern uint32_t pmixp_info_tasks_loc();
extern uint32_t pmixp_info_tasks_uni();
extern uint32_t pmixp_info_appldr();
extern uint32_t pmixp_info_cpus();
extern uint32_t pmixp_info_taskid(uint32_t localid);
extern int pmixp_info_taskid2localid(uint32_t taskid);
extern char *pmixp_info_task_dist();
extern char *pmixp_info_task_map();
extern hostlist_t *pmixp_info_step_hostlist();
extern char *pmixp_info_step_host(int nodeid);
extern int pmixp_info_step_hostid(char *hostname);
extern char *pmixp_info_job_host(int nodeid);
extern int pmixp_info_job_hostid(char *hostname);
extern char *pmixp_info_nspace_usock(const char *nspace);

#endif /* PMIXP_INFO_H */
