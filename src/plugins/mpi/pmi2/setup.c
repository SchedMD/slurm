/*****************************************************************************\
 **  setup.c - PMI2 server setup
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#if defined(__FreeBSD__)
#include <sys/socket.h>	/* AF_INET */
#endif

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "src/common/slurm_xlator.h"
#include "src/common/mpi.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/common/net.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/common/reverse_tree_math.h"
#include "src/common/mapping.h"

#include "setup.h"
#include "tree.h"
#include "pmi.h"
#include "spawn.h"
#include "kvs.h"

#define PMI2_SOCK_ADDR_FMT "/tmp/sock.pmi2.%u.%u"


extern char **environ;

static bool run_in_stepd = 0;

int  tree_sock;
int *task_socks;
char tree_sock_addr[128];
pmi2_job_info_t job_info;
pmi2_tree_info_t tree_info;

extern bool
in_stepd(void)
{
	return run_in_stepd;
}

static void
_remove_tree_sock(void)
{
	unlink(tree_sock_addr);
}

static int
_setup_stepd_job_info(const stepd_step_rec_t *job, char ***env)
{
	char *p;
	int i;

	memset(&job_info, 0, sizeof(job_info));

	job_info.jobid  = job->jobid;
	job_info.stepid = job->stepid;
	job_info.nnodes = job->nnodes;
	job_info.nodeid = job->nodeid;
	job_info.ntasks = job->ntasks;
	job_info.ltasks = job->node_tasks;
	job_info.gtids = xmalloc(job->node_tasks * sizeof(uint32_t));
	for (i = 0; i < job->node_tasks; i ++) {
		job_info.gtids[i] = job->task[i]->gtid;
	}
	job_info.switch_job = (void*)job->switch_job;

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
		xstrfmtcat(job_info.pmi_jobid, "%u.%u", job->jobid,
			   job->stepid);
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
_setup_stepd_tree_info(const stepd_step_rec_t *job, char ***env)
{
	hostlist_t hl;
	char srun_host[64];
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
			tree_width = slurm_get_tree_width();
		}
	} else {
		tree_width = slurm_get_tree_width();
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

	p = getenvp(*env, "SLURM_SRUN_COMM_HOST");
	if (!p) {
		error("mpi/pmi2: unable to find srun comm ifhn in env");
		return SLURM_ERROR;
	} else {
		strncpy(srun_host, p, 64);
	}
	p = getenvp(*env, PMI2_SRUN_PORT_ENV);
	if (!p) {
		error("mpi/pmi2: unable to find srun pmi2 port in env");
		return SLURM_ERROR;
	} else {
		port = atoi(p);
		unsetenvp(*env, PMI2_SRUN_PORT_ENV);
	}
	tree_info.srun_addr = xmalloc(sizeof(slurm_addr_t));
	slurm_set_addr(tree_info.srun_addr, port, srun_host);

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

	debug("mpi/pmi2: setup sockets");

	tree_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (tree_sock < 0) {
		error("mpi/pmi2: failed to create tree socket: %m");
		return SLURM_ERROR;
	}
	sa.sun_family = PF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), PMI2_SOCK_ADDR_FMT,
		 job->jobid, job->stepid);
	unlink(sa.sun_path);    /* remove possible old socket */

	if (bind(tree_sock, (struct sockaddr *)&sa, SUN_LEN(&sa)) < 0) {
		error("mpi/pmi2: failed to bind tree socket: %m");
		unlink(sa.sun_path);
		return SLURM_ERROR;
	}
	if (listen(tree_sock, 64) < 0) {
		error("mpi/pmi2: failed to listen tree socket: %m");
		unlink(sa.sun_path);
		return SLURM_ERROR;
	}

	/* remove the tree socket file on exit */
	strncpy(tree_sock_addr, sa.sun_path, 128);
	atexit(_remove_tree_sock);

	task_socks = xmalloc(2 * job->node_tasks * sizeof(int));
	for (i = 0; i < job->node_tasks; i ++) {
		socketpair(AF_UNIX, SOCK_STREAM, 0, &task_socks[i * 2]);
		/* this must be delayed after the tasks have been forked */
/* 		close(TASK_PMI_SOCK(i)); */
	}
	return SLURM_SUCCESS;
}

static int
_setup_stepd_kvs(const stepd_step_rec_t *job, char ***env)
{
	int rc = SLURM_SUCCESS, i = 0, pp_cnt = 0;
	char *p, env_key[32], *ppkey, *ppval;

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
	 * "arbitrary" distribution the process mapping varible is not correct.
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
	rc = _setup_stepd_tree_info(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* sockets */
	rc = _setup_stepd_sockets(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* kvs */
	rc = _setup_stepd_kvs(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	return SLURM_SUCCESS;
}

/**************************************************************/

static int
_setup_srun_job_info(const mpi_plugin_client_info_t *job)
{
	char *p;
	void *handle = NULL, *sym = NULL;

	memset(&job_info, 0, sizeof(job_info));

	job_info.jobid  = job->jobid;
	job_info.stepid = job->stepid;
	job_info.nnodes = job->step_layout->node_cnt;
	job_info.nodeid = -1;	/* id in tree. not used. */
	job_info.ntasks = job->step_layout->task_cnt;
	job_info.ltasks = 0;	/* not used */
	job_info.gtids = NULL;	/* not used */
	job_info.switch_job = NULL; /* not used */


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

	job_info.proc_mapping = pack_process_mapping(job->step_layout->node_cnt,
				 job->step_layout->task_cnt,
				 job->step_layout->tasks,
				 job->step_layout->tids);
	if (job_info.proc_mapping == NULL) {
		return SLURM_ERROR;
	}
	p = getenv(PMI2_PMI_JOBID_ENV);
	if (p) {		/* spawned */
		job_info.pmi_jobid = xstrdup(p);
	} else {
		xstrfmtcat(job_info.pmi_jobid, "%u.%u", job->jobid,
			   job->stepid);
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
		job_info.srun_opt = (opt_t *)sym;
	}
	dlclose(handle);

	return SLURM_SUCCESS;
}

static int
_setup_srun_tree_info(const mpi_plugin_client_info_t *job)
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

	snprintf(tree_sock_addr, 128, PMI2_SOCK_ADDR_FMT,
		 job->jobid, job->stepid);

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
_setup_srun_kvs(const mpi_plugin_client_info_t *job)
{
	int rc;

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

static int
_setup_srun_task_launch_detection(void)
{
	int retries = 0;
	pthread_t tid;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	while ((errno = pthread_create(&tid, &attr,
				       &_task_launch_detection, NULL))) {
		if (++retries > 5) {
			error ("mpi/pmi2: pthread_create error %m");
			slurm_attr_destroy(&attr);
			return SLURM_ERROR;
		}
		sleep(1);
	}
	slurm_attr_destroy(&attr);
	debug("mpi/pmi2: task launch detection thread (%lu) started",
	      (unsigned long) tid);

	return SLURM_SUCCESS;
}

extern int
pmi2_setup_srun(const mpi_plugin_client_info_t *job, char ***env)
{
	int rc;

	run_in_stepd = false;

	rc = _setup_srun_job_info(job);
	if (rc != SLURM_SUCCESS)
		return rc;

	rc = _setup_srun_tree_info(job);
	if (rc != SLURM_SUCCESS)
		return rc;

	rc = _setup_srun_socket(job);
	if (rc != SLURM_SUCCESS)
		return rc;

	rc = _setup_srun_kvs(job);
	if (rc != SLURM_SUCCESS)
		return rc;

	rc = _setup_srun_environ(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	if (job_info.spawn_seq) {
		rc = _setup_srun_task_launch_detection();
		if (rc != SLURM_SUCCESS)
			return rc;
	}

	return SLURM_SUCCESS;
}
