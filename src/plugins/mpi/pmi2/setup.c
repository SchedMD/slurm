/*****************************************************************************\
 **  setup.c - PMI2 server setup
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
 *  Written by Artem Y. Polyakov <artemp@mellanox.com>.
 *  All rights reserved.
 *  Portions copyright (C) 2017 SchedMD LLC.
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

#if defined(__FreeBSD__)
#include <sys/socket.h>	/* AF_INET */
#endif

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/net.h"
#include "src/common/proc_args.h"
#include "src/common/reverse_tree.h"
#include "src/common/slurm_mpi.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "setup.h"
#include "tree.h"
#include "pmi.h"
#include "spawn.h"
#include "kvs.h"
#include "ring.h"

#define PMI2_SOCK_ADDR_FMT "%s/sock.pmi2.%u.%u"


extern char **environ;

static bool run_in_stepd = 0;

int  tree_sock;
int *task_socks;
char tree_sock_addr[128];
pmi2_job_info_t job_info;
pmi2_tree_info_t tree_info;

static char *fmt_tree_sock_addr = NULL;

extern bool
in_stepd(void)
{
	return run_in_stepd;
}

static void
_remove_tree_sock(void)
{
	if (fmt_tree_sock_addr) {
		unlink(fmt_tree_sock_addr);
		xfree(fmt_tree_sock_addr);
	}
}

static int
_setup_stepd_job_info(const stepd_step_rec_t *job, char ***env)
{
	char *p;
	int i;

	memset(&job_info, 0, sizeof(job_info));

	if (job->het_job_id && (job->het_job_id != NO_VAL))
		job_info.step_id.job_id  = job->het_job_id;
	else
		job_info.step_id.job_id  = job->step_id.job_id;

	job_info.uid = job->uid;

	if (job->het_job_offset != NO_VAL) {
		job_info.step_id.step_id = job->step_id.step_id;
		job_info.step_id.step_het_comp = job->step_id.step_het_comp;
		job_info.nnodes = job->het_job_nnodes;
		job_info.nodeid = job->nodeid + job->het_job_node_offset;
		job_info.ntasks = job->het_job_ntasks;
		job_info.ltasks = job->node_tasks;
		job_info.gtids = xmalloc(job_info.ltasks * sizeof(uint32_t));
		for (i = 0; i < job_info.ltasks; i ++) {
			job_info.gtids[i] = job->task[i]->gtid +
					    job->het_job_task_offset;
		}
	} else {
		job_info.step_id.step_id = job->step_id.step_id;
		job_info.step_id.step_het_comp = job->step_id.step_het_comp;
		job_info.nnodes = job->nnodes;
		job_info.nodeid = job->nodeid;
		job_info.ntasks = job->ntasks;
		job_info.ltasks = job->node_tasks;
		job_info.gtids = xmalloc(job_info.ltasks * sizeof(uint32_t));
		for (i = 0; i < job_info.ltasks; i ++) {
			job_info.gtids[i] = job->task[i]->gtid;
		}
	}

	p = getenvp(*env, PMI2_PMI_DEBUGGED_ENV);
	if (p) {
		job_info.pmi_debugged = atoi(p);
	} else {
		job_info.pmi_debugged = 0;
	}
	p = getenvp(*env, PMI2_SPAWN_SEQ_ENV);
	if (p) { 		/* spawned */
		job_info.spawn_seq = atoi(p);
		unsetenvp(*env, PMI2_SPAWN_SEQ_ENV);
		p = getenvp(*env, PMI2_SPAWNER_JOBID_ENV);
		job_info.spawner_jobid = xstrdup(p);
		unsetenvp(*env, PMI2_SPAWNER_JOBID_ENV);
	} else {
		job_info.spawn_seq = 0;
		job_info.spawner_jobid = NULL;
	}
	p = getenvp(*env, PMI2_PMI_JOBID_ENV);
	if (p) {
		job_info.pmi_jobid = xstrdup(p);
		unsetenvp(*env, PMI2_PMI_JOBID_ENV);
	} else {
		xstrfmtcat(job_info.pmi_jobid, "%u.%u", job_info.step_id.job_id,
			   job_info.step_id.step_id);
	}
	p = getenvp(*env, PMI2_STEP_NODES_ENV);
	if (!p) {
		error("mpi/pmi2: unable to find nodes in job environment");
		return SLURM_ERROR;
	} else {
		job_info.step_nodelist = xstrdup(p);
		unsetenvp(*env, PMI2_STEP_NODES_ENV);
	}
	/*
	 * how to get the mapping info from stepd directly?
	 * there is the task distribution info in the launch_tasks_request_msg_t,
	 * but it is not stored in the stepd_step_rec_t.
	 */
	p = getenvp(*env, PMI2_PROC_MAPPING_ENV);
	if (!p) {
		error("PMI2_PROC_MAPPING_ENV not found");
		return SLURM_ERROR;
	} else {
		job_info.proc_mapping = xstrdup(p);
		unsetenvp(*env, PMI2_PROC_MAPPING_ENV);
	}

	job_info.job_env = env_array_copy((const char **)*env);

	job_info.MPIR_proctable = NULL;
	job_info.srun_opt = NULL;

	/* get the SLURM_STEP_RESV_PORTS
	 */
	p = getenvp(*env, SLURM_STEP_RESV_PORTS);
	if (!p) {
		debug("%s: %s not found in env", __func__, SLURM_STEP_RESV_PORTS);
	} else {
		job_info.resv_ports = xstrdup(p);
		info("%s: SLURM_STEP_RESV_PORTS found %s", __func__, p);
	}
	return SLURM_SUCCESS;
}

static int
_setup_stepd_tree_info(char ***env)
{
	hostlist_t hl;
	char *srun_host;
	uint16_t port;
	char *p;
	int tree_width;

	/* job info available */

	memset(&tree_info, 0, sizeof(tree_info));

	hl = hostlist_create(job_info.step_nodelist);
	p = hostlist_nth(hl, job_info.nodeid); /* strdup-ed */
	tree_info.this_node = xstrdup(p);
	free(p);

	/* this only controls the upward communication tree width */
	p = getenvp(*env, PMI2_TREE_WIDTH_ENV);
	if (p) {
		tree_width = atoi(p);
		if (tree_width < 2) {
			info("invalid PMI2 tree width value (%d) detected. "
			     "fallback to default value.", tree_width);
			tree_width = slurm_conf.tree_width;
		}
	} else {
		tree_width = slurm_conf.tree_width;
	}

	/* TODO: cannot launch 0 tasks on node */

	/*
	 * In tree position calculation, root of the tree is srun with id 0.
	 * Stepd's id will be its nodeid plus 1.
	 */
	reverse_tree_info(job_info.nodeid + 1, job_info.nnodes + 1,
			  tree_width, &tree_info.parent_id,
			  &tree_info.num_children, &tree_info.depth,
			  &tree_info.max_depth);
	tree_info.parent_id --;	       /* restore real nodeid */
	if (tree_info.parent_id < 0) {	/* parent is srun */
		tree_info.parent_node = NULL;
	} else {
		p = hostlist_nth(hl, tree_info.parent_id);
		tree_info.parent_node = xstrdup(p);
		free(p);
	}
	hostlist_destroy(hl);

	tree_info.pmi_port = 0;	/* not used */

	srun_host = getenvp(*env, "SLURM_SRUN_COMM_HOST");
	if (!srun_host) {
		error("mpi/pmi2: unable to find srun comm ifhn in env");
		return SLURM_ERROR;
	}
	p = getenvp(*env, PMI2_SRUN_PORT_ENV);
	if (!p) {
		error("mpi/pmi2: unable to find srun pmi2 port in env");
		return SLURM_ERROR;
	}
	port = atoi(p);

	tree_info.srun_addr = xmalloc(sizeof(slurm_addr_t));
	slurm_set_addr(tree_info.srun_addr, port, srun_host);

	unsetenvp(*env, PMI2_SRUN_PORT_ENV);

	/* init kvs seq to 0. TODO: reduce array size */
	tree_info.children_kvs_seq = xmalloc(sizeof(uint32_t) *
					     job_info.nnodes);

	return SLURM_SUCCESS;
}

/*
 * setup sockets for slurmstepd
 */
static int
_setup_stepd_sockets(const stepd_step_rec_t *job, char ***env)
{
	struct sockaddr_un sa;
	int i;
	char *spool;

	debug("mpi/pmi2: setup sockets");

	tree_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (tree_sock < 0) {
		error("mpi/pmi2: failed to create tree socket: %m");
		return SLURM_ERROR;
	}
	sa.sun_family = PF_UNIX;

	/*
	 * tree_sock_addr has to remain unformatted since the formatting
	 * happens on the slurmd side
	 */
	spool = xstrdup(slurm_conf.slurmd_spooldir);
	snprintf(tree_sock_addr, sizeof(tree_sock_addr), PMI2_SOCK_ADDR_FMT,
		 spool, job_info.step_id.job_id, job_info.step_id.step_id);
	/*
	 * Make sure we adjust for the spool dir coming in on the address to
	 * point to the right spot.
	 * We need to unlink this later so we need a formatted version of the
	 * string to unlink.
	 */
	xstrsubstitute(spool, "%n", job->node_name);
	xstrsubstitute(spool, "%h", job->node_name);
	xstrfmtcat(fmt_tree_sock_addr, PMI2_SOCK_ADDR_FMT, spool,
		   job_info.step_id.job_id, job_info.step_id.step_id);

	/*
	 * If socket name would be truncated, emit error and exit
	 */
	if (strlen(fmt_tree_sock_addr) >= sizeof(sa.sun_path)) {
		error("%s: Unix socket path '%s' is too long. (%ld > %ld)",
		      __func__, fmt_tree_sock_addr,
		      (long int)(strlen(fmt_tree_sock_addr) + 1),
		      (long int)sizeof(sa.sun_path));
		xfree(spool);
		xfree(fmt_tree_sock_addr);
		return SLURM_ERROR;
	}

	strlcpy(sa.sun_path, fmt_tree_sock_addr, sizeof(sa.sun_path));

	unlink(sa.sun_path);    /* remove possible old socket */
	xfree(spool);

	if (bind(tree_sock, (struct sockaddr *)&sa, SUN_LEN(&sa)) < 0) {
		error("mpi/pmi2: failed to bind tree socket: %m");
		unlink(sa.sun_path);
		return SLURM_ERROR;
	}
	if (chown(sa.sun_path, job->uid, -1) < 0) {
		error("mpi/pmi2: failed to chown tree socket: %m");
		unlink(sa.sun_path);
		return SLURM_ERROR;
	}
	if (listen(tree_sock, 64) < 0) {
		error("mpi/pmi2: failed to listen tree socket: %m");
		unlink(sa.sun_path);
		return SLURM_ERROR;
	}

	task_socks = xmalloc(2 * job->node_tasks * sizeof(int));
	for (i = 0; i < job->node_tasks; i ++) {
		socketpair(AF_UNIX, SOCK_STREAM, 0, &task_socks[i * 2]);
		/* this must be delayed after the tasks have been forked */
/* 		close(TASK_PMI_SOCK(i)); */
	}
	return SLURM_SUCCESS;
}

static int
_setup_stepd_kvs(char ***env)
{
	int rc = SLURM_SUCCESS, i = 0, pp_cnt = 0;
	char *p, env_key[32], *ppkey, *ppval;

	kvs_seq = 1;
	rc = temp_kvs_init();
	if (rc != SLURM_SUCCESS)
		return rc;

	rc = kvs_init();
	if (rc != SLURM_SUCCESS)
		return rc;

	/* preput */
	p = getenvp(*env, PMI2_PREPUT_CNT_ENV);
	if (p) {
		pp_cnt = atoi(p);
	}

	for (i = 0; i < pp_cnt; i ++) {
		snprintf(env_key, 32, PMI2_PPKEY_ENV"%d", i);
		p = getenvp(*env, env_key);
		ppkey = p; /* getenvp will not modify p */
		snprintf(env_key, 32, PMI2_PPVAL_ENV"%d", i);
		p = getenvp(*env, env_key);
		ppval = p;
		kvs_put(ppkey, ppval);
	}

	/*
	 * For PMI11.
	 * A better logic would be to put PMI_process_mapping in KVS only if
	 * the task distribution method is not "arbitrary", because in
	 * "arbitrary" distribution the process mapping variable is not correct.
	 * MPICH2 may deduce the clique info from the hostnames. But that
	 * is rather costly.
	 */
	kvs_put("PMI_process_mapping", job_info.proc_mapping);

	return SLURM_SUCCESS;
}

extern int
pmi2_setup_stepd(const stepd_step_rec_t *job, char ***env)
{
	int rc;

	run_in_stepd = true;

	/* job info */
	rc = _setup_stepd_job_info(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* tree info */
	rc = _setup_stepd_tree_info(env);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* sockets */
	rc = _setup_stepd_sockets(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* kvs */
	rc = _setup_stepd_kvs(env);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* TODO: finalize pmix_ring state somewhere */
	/* initialize pmix_ring state */
	rc = pmix_ring_init(&job_info, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	return SLURM_SUCCESS;
}

extern void
pmi2_cleanup_stepd(void)
{
	close(tree_sock);
	_remove_tree_sock();
}
/**************************************************************/

/* returned string should be xfree-ed by caller */
static char *
_get_proc_mapping(const mpi_plugin_client_info_t *job)
{
	uint32_t node_cnt, task_cnt, task_mapped, node_task_cnt, **tids;
	uint32_t task_dist, block;
	uint16_t *tasks, *rounds;
	int i, start_id, end_id;
	char *mapping = NULL;

	node_cnt = job->step_layout->node_cnt;
	task_cnt = job->step_layout->task_cnt;
	task_dist = job->step_layout->task_dist & SLURM_DIST_STATE_BASE;
	tasks = job->step_layout->tasks;
	tids = job->step_layout->tids;

	/* for now, PMI2 only supports vector processor mapping */

	if ((task_dist & SLURM_DIST_NODEMASK) == SLURM_DIST_NODECYCLIC) {
		mapping = xstrdup("(vector");

		rounds = xmalloc (node_cnt * sizeof(uint16_t));
		task_mapped = 0;
		while (task_mapped < task_cnt) {
			start_id = 0;
			/* find start_id */
			while (start_id < node_cnt) {
				while (start_id < node_cnt &&
				       ( rounds[start_id] >= tasks[start_id] ||
					 (task_mapped !=
					  tids[start_id][rounds[start_id]]) )) {
					start_id ++;
				}
				if (start_id >= node_cnt)
					break;
				/* block is always 1 */
				/* find end_id */
				end_id = start_id;
				while (end_id < node_cnt &&
				       ( rounds[end_id] < tasks[end_id] &&
					 (task_mapped ==
					  tids[end_id][rounds[end_id]]) )) {
					rounds[end_id] ++;
					task_mapped ++;
					end_id ++;
				}
				xstrfmtcat(mapping, ",(%u,%u,1)", start_id,
					   end_id - start_id);
				start_id = end_id;
			}
		}
		xfree(rounds);
		xstrcat(mapping, ")");
	} else if (task_dist == SLURM_DIST_ARBITRARY) {
		/*
		 * MPICH2 will think that each task runs on a seperate node.
		 * The program will run, but no SHM will be used for
		 * communication.
		 */
		mapping = xstrdup("(vector");
		xstrfmtcat(mapping, ",(0,%u,1)", job->step_layout->task_cnt);
		xstrcat(mapping, ")");

	} else if (task_dist == SLURM_DIST_PLANE) {
		mapping = xstrdup("(vector");

		rounds = xmalloc (node_cnt * sizeof(uint16_t));
		task_mapped = 0;
		while (task_mapped < task_cnt) {
			start_id = 0;
			/* find start_id */
			while (start_id < node_cnt) {
				while (start_id < node_cnt &&
				       ( rounds[start_id] >= tasks[start_id] ||
					 (task_mapped !=
					  tids[start_id][rounds[start_id]]) )) {
					start_id ++;
				}
				if (start_id >= node_cnt)
					break;
				/* find start block. block may be less
				 * than plane size */
				block = 0;
				while (rounds[start_id] < tasks[start_id] &&
				       (task_mapped ==
					tids[start_id][rounds[start_id]])) {
					block ++;
					rounds[start_id] ++;
					task_mapped ++;
				}
				/* find end_id */
				end_id = start_id + 1;
				while (end_id < node_cnt &&
				       (rounds[end_id] + block - 1 <
					tasks[end_id])) {
					for (i = 0;
					     i < tasks[end_id] - rounds[end_id];
					     i ++) {
						if (task_mapped + i !=
						    tids[end_id][rounds[end_id]
								 + i]) {
							break;
						}
					}
					if (i != block)
						break;
					rounds[end_id] += block;
					task_mapped += block;
					end_id ++;
				}
				xstrfmtcat(mapping, ",(%u,%u,%u)", start_id,
					   end_id - start_id, block);
				start_id = end_id;
			}
		}
		xfree(rounds);
		xstrcat(mapping, ")");

	} else {		/* BLOCK mode */
		mapping = xstrdup("(vector");
		start_id = 0;
		node_task_cnt = tasks[start_id];
		for (i = start_id + 1; i < node_cnt; i ++) {
			if (node_task_cnt == tasks[i])
				continue;
			xstrfmtcat(mapping, ",(%u,%u,%u)", start_id,
				   i - start_id, node_task_cnt);
			start_id = i;
			node_task_cnt = tasks[i];
		}
		xstrfmtcat(mapping, ",(%u,%u,%u))", start_id, i - start_id,
			   node_task_cnt);
	}

	debug("mpi/pmi2: processor mapping: %s", mapping);
	return mapping;
}

static int
_setup_srun_job_info(const mpi_plugin_client_info_t *job)
{
	char *p;
	void *handle = NULL, *sym = NULL;

	memset(&job_info, 0, sizeof(job_info));

	if (job->het_job_id && (job->het_job_id != NO_VAL))
		job_info.step_id.job_id  = job->het_job_id;
	else
		job_info.step_id.job_id  = job->step_id.job_id;

	job_info.step_id.step_id = job->step_id.step_id;
	job_info.step_id.step_het_comp = job->step_id.step_het_comp;
	job_info.nnodes = job->step_layout->node_cnt;
	job_info.ntasks = job->step_layout->task_cnt;
	job_info.nodeid = -1;	/* id in tree. not used. */
	job_info.ltasks = 0;	/* not used */
	job_info.gtids = NULL;	/* not used */

	p = getenv(PMI2_PMI_DEBUGGED_ENV);
	if (p) {
		job_info.pmi_debugged = atoi(p);
	} else {
		job_info.pmi_debugged = 0;
	}
	p = getenv(PMI2_SPAWN_SEQ_ENV);
	if (p) { 		/* spawned */
		job_info.spawn_seq = atoi(p);
		p = getenv(PMI2_SPAWNER_JOBID_ENV);
		job_info.spawner_jobid = xstrdup(p);
		/* env unset in stepd */
	} else {
		job_info.spawn_seq = 0;
		job_info.spawner_jobid = NULL;
	}

	job_info.step_nodelist = xstrdup(job->step_layout->node_list);
	job_info.proc_mapping = _get_proc_mapping(job);
	if (job_info.proc_mapping == NULL) {
		return SLURM_ERROR;
	}
	p = getenv(PMI2_PMI_JOBID_ENV);
	if (p) {		/* spawned */
		job_info.pmi_jobid = xstrdup(p);
	} else {
		xstrfmtcat(job_info.pmi_jobid, "%u.%u", job_info.step_id.job_id,
			   job_info.step_id.step_id);
	}
	job_info.job_env = env_array_copy((const char **)environ);

	/* hjcao: this is really dirty.
	   But writing a new launcher is not desirable. */
	handle = dlopen(NULL, RTLD_LAZY);
	if (handle == NULL) {
		error("mpi/pmi2: failed to dlopen()");
		return SLURM_ERROR;
	}
	sym = dlsym(handle, "MPIR_proctable");
	if (sym == NULL) {
		/* if called directly in API, there may be no symbol available */
		verbose ("mpi/pmi2: failed to find symbol 'MPIR_proctable'");
		job_info.MPIR_proctable = NULL;
	} else {
		job_info.MPIR_proctable = *(MPIR_PROCDESC **)sym;
	}
	sym = dlsym(handle, "opt");
	if (sym == NULL) {
		verbose("mpi/pmi2: failed to find symbol 'opt'");
		job_info.srun_opt = NULL;
	} else {
		job_info.srun_opt = (slurm_opt_t *)sym;
	}
	dlclose(handle);

	return SLURM_SUCCESS;
}

static int
_setup_srun_tree_info(void)
{
	char *p;
	uint16_t p_port;

	memset(&tree_info, 0, sizeof(tree_info));

	tree_info.this_node = "launcher"; /* not used */
	tree_info.parent_id = -2;   /* not used */
	tree_info.parent_node = NULL; /* not used */
	tree_info.num_children = job_info.nnodes;
	tree_info.depth = 0;	 /* not used */
	tree_info.max_depth = 0; /* not used */
	/* pmi_port set in _setup_srun_sockets */
	p = getenv(PMI2_SPAWNER_PORT_ENV);
	if (p) {		/* spawned */
		p_port = atoi(p);
		tree_info.srun_addr = xmalloc(sizeof(slurm_addr_t));
		/* assume there is always a lo interface */
		slurm_set_addr(tree_info.srun_addr, p_port, "127.0.0.1");
	} else
		tree_info.srun_addr = NULL;

	/*
	 * FIXME: We need to handle %n and %h in the spool dir, but don't have
	 * the node name here
	 */
	snprintf(tree_sock_addr, 128, PMI2_SOCK_ADDR_FMT,
		 slurm_conf.slurmd_spooldir, job_info.step_id.job_id,
		 job_info.step_id.step_id);

	/* init kvs seq to 0. TODO: reduce array size */
	tree_info.children_kvs_seq = xmalloc(sizeof(uint32_t) *
					     job_info.nnodes);

	return SLURM_SUCCESS;
}

static int
_setup_srun_socket(const mpi_plugin_client_info_t *job)
{
	if (net_stream_listen(&tree_sock,
			      &tree_info.pmi_port) < 0) {
		error("mpi/pmi2: Failed to create tree socket");
		return SLURM_ERROR;
	}
	debug("mpi/pmi2: srun pmi port: %hu", tree_info.pmi_port);

	return SLURM_SUCCESS;
}

static int
_setup_srun_kvs(void)
{
	int rc;

	kvs_seq = 1;
	rc = temp_kvs_init();
	return rc;
}

static int
_setup_srun_environ(const mpi_plugin_client_info_t *job, char ***env)
{
	/* ifhn will be set in SLURM_SRUN_COMM_HOST by slurmd */
	env_array_overwrite_fmt(env, PMI2_SRUN_PORT_ENV, "%hu",
				tree_info.pmi_port);
	env_array_overwrite_fmt(env, PMI2_STEP_NODES_ENV, "%s",
				job_info.step_nodelist);
	env_array_overwrite_fmt(env, PMI2_PROC_MAPPING_ENV, "%s",
				job_info.proc_mapping);
	return SLURM_SUCCESS;
}

inline static int
_tasks_launched (void)
{
	int i, all_launched = 1;
	if (job_info.MPIR_proctable == NULL)
		return 1;

	for (i = 0; i < job_info.ntasks; i ++) {
		if (job_info.MPIR_proctable[i].pid == 0) {
			all_launched = 0;
			break;
		}
	}
	return all_launched;
}

static void *
_task_launch_detection(void *unused)
{
	spawn_resp_t *resp;
	time_t start;
	int rc = 0;

	/*
	 * mpir_init() is called in plugins/launch/slurm/launch_slurm.c before
	 * mpi_hook_client_prelaunch() is called in api/step_launch.c
	 */
	start = time(NULL);
	while (_tasks_launched() == 0) {
		usleep(1000*50);
		if (time(NULL) - start > 600) {
			rc = 1;
			break;
		}
	}

	/* send a resp to spawner srun */
	resp = spawn_resp_new();
	resp->seq = job_info.spawn_seq;
	resp->jobid = xstrdup(job_info.pmi_jobid);
	resp->error_cnt = 0;	/* TODO */
	resp->rc = rc;
	resp->pmi_port = tree_info.pmi_port;

	spawn_resp_send_to_srun(resp);
	spawn_resp_free(resp);
	return NULL;
}

extern int
pmi2_setup_srun(const mpi_plugin_client_info_t *job, char ***env)
{
	static pthread_mutex_t setup_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t setup_cond  = PTHREAD_COND_INITIALIZER;
	static int global_rc = NO_VAL16;
	int rc = SLURM_SUCCESS;

	run_in_stepd = false;
	if ((job->het_job_id == NO_VAL) || (job->het_job_task_offset == 0)) {
		rc = _setup_srun_job_info(job);
		if (rc == SLURM_SUCCESS)
			rc = _setup_srun_tree_info();
		if (rc == SLURM_SUCCESS)
			rc = _setup_srun_socket(job);
		if (rc == SLURM_SUCCESS)
			rc = _setup_srun_kvs();
		if (rc == SLURM_SUCCESS)
			rc = _setup_srun_environ(job, env);
		if ((rc == SLURM_SUCCESS) && job_info.spawn_seq) {
			slurm_thread_create_detached(NULL,
						     _task_launch_detection,
						     NULL);
		}
		slurm_mutex_lock(&setup_mutex);
		global_rc = rc;
		slurm_cond_broadcast(&setup_cond);
		slurm_mutex_unlock(&setup_mutex);
	} else {
		slurm_mutex_lock(&setup_mutex);
		while (global_rc == NO_VAL16)
			slurm_cond_wait(&setup_cond, &setup_mutex);
		rc = global_rc;
		slurm_mutex_unlock(&setup_mutex);
		if (rc == SLURM_SUCCESS)
			rc = _setup_srun_environ(job, env);
 	}

	return rc;
}
