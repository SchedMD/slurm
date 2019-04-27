/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd_job.c - stepd_step_rec_t routines
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#include "config.h"

#include <grp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/gres.h"
#include "src/common/group_cache.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/fname.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/multi_prog.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static char **_array_copy(int n, char **src);
static void _array_free(char ***array);
static void _job_init_task_info(stepd_step_rec_t *job, uint32_t **gtid,
				char *ifname, char *ofname, char *efname);
static void _srun_info_destructor(void *arg);
static stepd_step_task_info_t *_task_info_create(int taskid, int gtaskid,
						 char *ifname, char *ofname,
						 char *efname);
static void _task_info_destroy(stepd_step_task_info_t *t, uint16_t multi_prog);

/*
 * return the default output filename for a batch job
 */
static char *
_batchfilename(stepd_step_rec_t *job, const char *name)
{
	if (name == NULL) {
		if (job->array_task_id == NO_VAL)
			return fname_create(job, "slurm-%J.out", 0);
		else
			return fname_create(job, "slurm-%A_%a.out", 0);
	} else
		return fname_create(job, name, 0);
}

/*
 * Expand a stdio file name.
 *
 * If "filename" is NULL it means that an eio object should be created
 * for that stdio file rather than a directly connecting it to a file.
 *
 * If the "filename" is a valid task number in string form and the
 * number matches "taskid", then NULL is returned so that an eio
 * object will be used.  If is a valid number, but it does not match
 * "taskid", then the file descriptor will be connected to /dev/null.
 */
static char *
_expand_stdio_filename(char *filename, int gtaskid, stepd_step_rec_t *job)
{
	int id;

	if (filename == NULL)
		return NULL;

	id = fname_single_task_io(filename);

	if (id < 0)
		return fname_create(job, filename, gtaskid);
	if (id >= job->ntasks) {
		error("Task ID in filename is invalid");
		return NULL;
	}

	if (id == gtaskid)
		return NULL;
	else
		return xstrdup("/dev/null");
}

static void
_job_init_task_info(stepd_step_rec_t *job, uint32_t **gtid,
		    char *ifname, char *ofname, char *efname)
{
	int          i, node_id = job->nodeid;
	char        *in, *out, *err;
	uint32_t     pack_offset = 0;

	if (job->node_tasks == 0) {
		error("User requested launch of zero tasks!");
		job->task = NULL;
		return;
	}

	if (job->pack_offset != NO_VAL)
		pack_offset = job->pack_offset;

#if defined(HAVE_NATIVE_CRAY)
	for (i = 0; i < job->nnodes; i++) {
		int j;
		for (j = 1; j < job->task_cnts[i]; j++) {
			if (gtid[i][j] != gtid[i][j-1] + 1) {
				job->non_smp = 1;
				break;
			}
		}
	}
#endif

	job->task = (stepd_step_task_info_t **)
		xmalloc(job->node_tasks * sizeof(stepd_step_task_info_t *));

	for (i = 0; i < job->node_tasks; i++) {
		in  = _expand_stdio_filename(ifname,
					     gtid[node_id][i] + pack_offset,
					     job);
		out = _expand_stdio_filename(ofname,
					     gtid[node_id][i] + pack_offset,
					     job);
		err = _expand_stdio_filename(efname,
					     gtid[node_id][i] + pack_offset,
					     job);
		job->task[i] = _task_info_create(i, gtid[node_id][i], in, out,
						 err);
		if ((job->flags & LAUNCH_MULTI_PROG) == 0) {
			job->task[i]->argc = job->argc;
			job->task[i]->argv = job->argv;
		}
	}

	if (job->flags & LAUNCH_MULTI_PROG) {
		char *switch_type = slurm_get_switch_type();
		if (!xstrcmp(switch_type, "switch/cray_aries"))
			multi_prog_parse(job, gtid);
		xfree(switch_type);
		for (i = 0; i < job->node_tasks; i++){
			multi_prog_get_argv(job->argv[1], job->env,
					    gtid[node_id][i],
					    &job->task[i]->argc,
					    &job->task[i]->argv,
					    job->argc, job->argv);
		}
	}
}

static char **
_array_copy(int n, char **src)
{
	char **dst = xmalloc((n+1) * sizeof(char *));
	int i;

	for (i = 0; i < n; i++) {
		dst[i] = xstrdup(src[i]);
	}
	dst[n] = NULL;

	return dst;
}

static void
_array_free(char ***array)
{
	int i = 0;
	while ((*array)[i] != NULL)
		xfree((*array)[i++]);
	xfree(*array);
	*array = NULL;
}

/* destructor for list routines */
static void
_srun_info_destructor(void *arg)
{
	srun_info_t *srun = (srun_info_t *)arg;
	srun_info_destroy(srun);
}

static void
_task_info_destroy(stepd_step_task_info_t *t, uint16_t multi_prog)
{
	slurm_mutex_lock(&t->mutex);
	slurm_mutex_unlock(&t->mutex);
	slurm_mutex_destroy(&t->mutex);
	if (multi_prog) {
		xfree(t->argv);
	} /* otherwise, t->argv is a pointer to job->argv */
	xfree(t);
}

static void _slurm_cred_to_step_rec(slurm_cred_t *cred, stepd_step_rec_t *job)
{
	slurm_cred_arg_t cred_arg;
	slurm_cred_get_args(cred, &cred_arg);

	/*
	 * This may have been filed in already from batch_job_launch_msg_t
	 * or launch_tasks_request_msg_t.
	 */
	if (!job->user_name) {
		job->user_name = cred_arg.pw_name;
		cred_arg.pw_name = NULL;
	}

	job->pw_gecos = cred_arg.pw_gecos;
	cred_arg.pw_gecos = NULL;
	job->pw_dir = cred_arg.pw_dir;
	cred_arg.pw_dir = NULL;
	job->pw_shell = cred_arg.pw_shell;
	cred_arg.pw_shell = NULL;

	job->ngids = cred_arg.ngids;
	job->gids = cred_arg.gids;
	cred_arg.gids = NULL;
	job->gr_names = cred_arg.gr_names;
	cred_arg.gr_names = NULL;

	slurm_cred_free_args(&cred_arg);
}

/* create a slurmd job structure from a launch tasks message */
extern stepd_step_rec_t *stepd_step_rec_create(launch_tasks_request_msg_t *msg,
					       uint16_t protocol_version)
{
	stepd_step_rec_t  *job = NULL;
	srun_info_t   *srun = NULL;
	slurm_addr_t     resp_addr;
	slurm_addr_t     io_addr;
	int            i, nodeid = NO_VAL;

	xassert(msg != NULL);
	xassert(msg->complete_nodelist != NULL);
	debug3("entering stepd_step_rec_create");

	if (acct_gather_check_acct_freq_task(msg->job_mem_lim, msg->acctg_freq))
		return NULL;

	job = xmalloc(sizeof(stepd_step_rec_t));
	job->msg = msg;
#ifndef HAVE_FRONT_END
	nodeid = nodelist_find(msg->complete_nodelist, conf->node_name);
	job->node_name = xstrdup(conf->node_name);
#else
	nodeid = 0;
	job->node_name = xstrdup(msg->complete_nodelist);
#endif
	if (nodeid < 0) {
		error("couldn't find node %s in %s",
		      job->node_name, msg->complete_nodelist);
		stepd_step_rec_destroy(job);
		return NULL;
	}

	job->state = SLURMSTEPD_STEP_STARTING;
	slurm_cond_init(&job->state_cond, NULL);
	slurm_mutex_init(&job->state_mutex);
	job->node_tasks	= msg->tasks_to_launch[nodeid];
	job->task_cnts  = xcalloc(msg->nnodes, sizeof(uint16_t));
	memcpy(job->task_cnts, msg->tasks_to_launch,
	       sizeof(uint16_t) * msg->nnodes);
	job->ntasks	= msg->ntasks;
	job->jobid	= msg->job_id;
	job->stepid	= msg->job_step_id;

	job->uid	= (uid_t) msg->uid;
	job->gid	= (gid_t) msg->gid;
	job->user_name	= xstrdup(msg->user_name);
	_slurm_cred_to_step_rec(msg->cred, job);
	/*
	 * Favor the group info in the launch cred if available - for 19.05+
	 * this is where it is managed, not in launch_tasks_request_msg_t.
	 * For older versions, or for when send_gids is disabled, fall back
	 * to the launch_tasks_request_msg_t info if necessary.
	 */
	if (!job->ngids) {
		job->ngids = (int) msg->ngids;
		job->gids = copy_gids(msg->ngids, msg->gids);
	}

	job->cwd	= xstrdup(msg->cwd);
	job->task_dist	= msg->task_dist;

	job->cpu_bind_type = msg->cpu_bind_type;
	job->cpu_bind = xstrdup(msg->cpu_bind);
	job->mem_bind_type = msg->mem_bind_type;
	job->mem_bind = xstrdup(msg->mem_bind);
	job->tres_bind = xstrdup(msg->tres_bind);
	job->tres_freq = xstrdup(msg->tres_freq);
	job->cpu_freq_min = msg->cpu_freq_min;
	job->cpu_freq_max = msg->cpu_freq_max;
	job->cpu_freq_gov = msg->cpu_freq_gov;
	job->ckpt_dir = xstrdup(msg->ckpt_dir);
	job->restart_dir = xstrdup(msg->restart_dir);
	job->cpus_per_task = msg->cpus_per_task;

	job->env     = _array_copy(msg->envc, msg->env);
	job->array_job_id  = msg->job_id;
	job->array_task_id = NO_VAL;
	job->node_offset = msg->node_offset;	/* Used for env vars */
	job->pack_step_cnt = msg->pack_step_cnt;
	job->pack_jobid  = msg->pack_jobid;	/* Used for env vars */
	job->pack_nnodes = msg->pack_nnodes;	/* Used for env vars */
	if (msg->pack_nnodes && msg->pack_ntasks && msg->pack_task_cnts) {
		job->pack_ntasks = msg->pack_ntasks;	/* Used for env vars */
		job->pack_task_cnts = xcalloc(msg->pack_nnodes,
					      sizeof(uint16_t));
		memcpy(job->pack_task_cnts, msg->pack_task_cnts,
		       sizeof(uint16_t) * msg->pack_nnodes);
		if (msg->pack_tids) {
			/* pack_tids == NULL if request from pre-v19.05 srun */
			job->pack_tids = xcalloc(msg->pack_nnodes,
						 sizeof(uint32_t *));
			for (i = 0; i < msg->pack_nnodes; i++) {
				job->pack_tids[i] =
					xcalloc(job->pack_task_cnts[i],
						sizeof(uint32_t));
				memcpy(job->pack_tids[i], msg->pack_tids[i],
				       sizeof(uint32_t) *
				       job->pack_task_cnts[i]);
			}
		}
		if (msg->pack_tid_offsets) {
			job->pack_tid_offsets = xcalloc(job->pack_ntasks,
							sizeof(uint32_t));
			memcpy(job->pack_tid_offsets, msg->pack_tid_offsets,
			       job->pack_ntasks * sizeof(uint32_t));
		}
	}
	job->pack_offset = msg->pack_offset;	/* Used for env vars & labels */
	job->pack_task_offset = msg->pack_task_offset;	/* Used for env vars &
							 * labels */
	job->pack_node_list = xstrdup(msg->pack_node_list);
	for (i = 0; i < msg->envc; i++) {
		/*                         1234567890123456789 */
		if (!xstrncmp(msg->env[i], "SLURM_ARRAY_JOB_ID=", 19))
			job->array_job_id = atoi(msg->env[i] + 19);
		/*                         12345678901234567890 */
		if (!xstrncmp(msg->env[i], "SLURM_ARRAY_TASK_ID=", 20))
			job->array_task_id = atoi(msg->env[i] + 20);
	}

	job->eio     = eio_handle_create(0);
	job->sruns   = list_create((ListDelF) _srun_info_destructor);

	/*
	 * Based on my testing the next 3 lists here could use the
	 * eio_obj_destroy, but if you do you can get an invalid read.  Since
	 * these stay until the end of the job it isn't that big of a deal.
	 */
	job->clients = list_create(NULL); /* FIXME! Needs destructor */
	job->stdout_eio_objs = list_create(NULL); /* FIXME! Needs destructor */
	job->stderr_eio_objs = list_create(NULL); /* FIXME! Needs destructor */
	job->free_incoming = list_create(NULL); /* FIXME! Needs destructor */
	job->incoming_count = 0;
	job->free_outgoing = list_create(NULL); /* FIXME! Needs destructor */
	job->outgoing_count = 0;
	job->outgoing_cache = list_create(NULL); /* FIXME! Needs destructor */

	job->envtp   = xmalloc(sizeof(env_t));
	job->envtp->jobid = -1;
	job->envtp->stepid = -1;
	job->envtp->procid = -1;
	job->envtp->localid = -1;
	job->envtp->nodeid = -1;

	job->envtp->distribution = 0;
	job->envtp->cpu_bind_type = 0;
	job->envtp->cpu_bind = NULL;
	job->envtp->mem_bind_type = 0;
	job->envtp->mem_bind = NULL;
	if (!msg->resp_port)
		msg->num_resp_port = 0;
	if (msg->num_resp_port) {
		job->envtp->comm_port =
			msg->resp_port[nodeid % msg->num_resp_port];
		memcpy(&resp_addr, &msg->orig_addr, sizeof(slurm_addr_t));
		slurm_set_addr(&resp_addr,
			       msg->resp_port[nodeid % msg->num_resp_port],
			       NULL);
	} else {
		memset(&resp_addr, 0, sizeof(slurm_addr_t));
	}
	if (!msg->io_port)
		msg->flags |= LAUNCH_USER_MANAGED_IO;
	if ((msg->flags & LAUNCH_USER_MANAGED_IO) == 0) {
		memcpy(&io_addr,   &msg->orig_addr, sizeof(slurm_addr_t));
		slurm_set_addr(&io_addr,
			       msg->io_port[nodeid % msg->num_io_port],
			       NULL);
	} else {
		memset(&io_addr, 0, sizeof(slurm_addr_t));
	}

	srun = srun_info_create(msg->cred, &resp_addr, &io_addr,
				protocol_version);

	job->profile     = msg->profile;
	job->task_prolog = xstrdup(msg->task_prolog);
	job->task_epilog = xstrdup(msg->task_epilog);

	job->argc    = msg->argc;
	job->argv    = _array_copy(job->argc, msg->argv);

	job->nnodes  = msg->nnodes;
	job->nodeid  = nodeid;
	job->debug   = msg->slurmd_debug;
	job->cpus    = msg->node_cpus;
	job->job_core_spec = msg->job_core_spec;

	/* This needs to happen before acct_gather_profile_startpoll
	   and only really looks at the profile in the job.
	*/
	acct_gather_profile_g_node_step_start(job);

	acct_gather_profile_startpoll(msg->acctg_freq,
				      conf->job_acct_gather_freq);

	job->timelimit   = (time_t) -1;
	job->flags       = msg->flags;
	job->switch_job  = msg->switch_job;
	job->open_mode   = msg->open_mode;
	job->options     = msg->options;
	format_core_allocs(msg->cred, conf->node_name, conf->cpus,
			   &job->job_alloc_cores, &job->step_alloc_cores,
			   &job->job_mem, &job->step_mem);

	if (job->step_mem && conf->job_acct_oom_kill) {
		jobacct_gather_set_mem_limit(job->jobid, job->stepid,
					     job->step_mem);
	} else if (job->job_mem && conf->job_acct_oom_kill) {
		jobacct_gather_set_mem_limit(job->jobid, job->stepid,
					     job->job_mem);
	}

	/* only need these values on the extern step, don't copy otherwise */
	if ((msg->job_step_id == SLURM_EXTERN_CONT) && msg->x11) {
		job->x11 = msg->x11;
		job->x11_alloc_host = xstrdup(msg->x11_alloc_host);
		job->x11_alloc_port = msg->x11_alloc_port;
		job->x11_magic_cookie = xstrdup(msg->x11_magic_cookie);
		job->x11_target = xstrdup(msg->x11_target);
		job->x11_target_port = msg->x11_target_port;
	}

	get_cred_gres(msg->cred, conf->node_name,
		      &job->job_gres_list, &job->step_gres_list);

	list_append(job->sruns, (void *) srun);

	_job_init_task_info(job, msg->global_task_ids,
			    msg->ifname, msg->ofname, msg->efname);

	return job;
}

extern stepd_step_rec_t *
batch_stepd_step_rec_create(batch_job_launch_msg_t *msg)
{
	stepd_step_rec_t *job;
	srun_info_t  *srun = NULL;
	char *in_name;

	xassert(msg != NULL);

	debug3("entering batch_stepd_step_rec_create");

	if (acct_gather_check_acct_freq_task(msg->job_mem, msg->acctg_freq))
		return NULL;

	job = xmalloc(sizeof(stepd_step_rec_t));

	job->state = SLURMSTEPD_STEP_STARTING;
	slurm_cond_init(&job->state_cond, NULL);
	slurm_mutex_init(&job->state_mutex);
	if (msg->cpus_per_node)
		job->cpus    = msg->cpus_per_node[0];
	job->node_tasks  = 1;
	job->ntasks  = msg->ntasks;
	job->jobid   = msg->job_id;
	job->stepid  = msg->step_id;
	job->array_job_id  = msg->array_job_id;
	job->array_task_id = msg->array_task_id;
	job->pack_step_cnt = NO_VAL;
	job->pack_jobid  = NO_VAL;	/* Used to set env vars */
	job->pack_nnodes = NO_VAL;	/* Used to set env vars */
	job->pack_ntasks = NO_VAL;	/* Used to set env vars */
	job->pack_offset = NO_VAL;	/* Used to set labels and env vars */
	job->job_core_spec = msg->job_core_spec;

	job->batch   = true;
	job->node_name  = xstrdup(conf->node_name);

	job->uid	= (uid_t) msg->uid;
	job->gid	= (gid_t) msg->gid;
	job->user_name	= xstrdup(msg->user_name);
	_slurm_cred_to_step_rec(msg->cred, job);
	/*
	 * Favor the group info in the launch cred if available - for 19.05+
	 * this is where it is managed, not in batch_job_launch_msg_t.
	 * For older versions, or for when send_gids is disabled, fall back
	 * to the batch_job_launch_msg_t info if necessary.
	 */
	if (!job->ngids) {
		job->ngids = (int) msg->ngids;
		job->gids = copy_gids(msg->ngids, msg->gids);
	}

	job->profile    = msg->profile;

	/* give them all to the 1 task */
	job->cpus_per_task = job->cpus;

	/* This needs to happen before acct_gather_profile_startpoll
	   and only really looks at the profile in the job.
	*/
	acct_gather_profile_g_node_step_start(job);
	/* needed for the jobacct_gather plugin to start */
	acct_gather_profile_startpoll(msg->acctg_freq,
				      conf->job_acct_gather_freq);

	job->open_mode  = msg->open_mode;
	job->overcommit = (bool) msg->overcommit;

	job->cwd     = xstrdup(msg->work_dir);

	job->ckpt_dir = xstrdup(msg->ckpt_dir);
	job->restart_dir = xstrdup(msg->restart_dir);

	job->env     = _array_copy(msg->envc, msg->environment);
	job->eio     = eio_handle_create(0);
	job->sruns   = list_create((ListDelF) _srun_info_destructor);
	job->envtp   = xmalloc(sizeof(env_t));
	job->envtp->jobid = -1;
	job->envtp->stepid = -1;
	job->envtp->procid = -1;
	job->envtp->localid = -1;
	job->envtp->nodeid = -1;

	job->envtp->distribution = 0;
	job->cpu_bind_type = msg->cpu_bind_type;
	job->cpu_bind = xstrdup(msg->cpu_bind);
	job->envtp->mem_bind_type = 0;
	job->envtp->mem_bind = NULL;
	job->envtp->restart_cnt = msg->restart_cnt;

	if (msg->cpus_per_node)
		job->cpus    = msg->cpus_per_node[0];

	format_core_allocs(msg->cred, conf->node_name, conf->cpus,
			   &job->job_alloc_cores, &job->step_alloc_cores,
			   &job->job_mem, &job->step_mem);
	if (job->step_mem && conf->job_acct_oom_kill)
		jobacct_gather_set_mem_limit(job->jobid, NO_VAL, job->step_mem);
	else if (job->job_mem && conf->job_acct_oom_kill)
		jobacct_gather_set_mem_limit(job->jobid, NO_VAL, job->job_mem);

	get_cred_gres(msg->cred, conf->node_name,
		      &job->job_gres_list, &job->step_gres_list);

	srun = srun_info_create(NULL, NULL, NULL, NO_VAL16);

	list_append(job->sruns, (void *) srun);

	if (msg->argc) {
		job->argc    = msg->argc;
		job->argv    = _array_copy(job->argc, msg->argv);
	} else {
		job->argc    = 1;
		/* job script has not yet been written out to disk --
		 * argv will be filled in later by _make_batch_script()
		 */
		job->argv    = (char **) xmalloc(2 * sizeof(char *));
	}

	job->task = xmalloc(sizeof(stepd_step_task_info_t *));
	if (msg->std_err == NULL)
		msg->std_err = xstrdup(msg->std_out);

	if (msg->std_in == NULL)
		in_name = xstrdup("/dev/null");
	else
		in_name = fname_create(job, msg->std_in, 0);

	job->task[0] = _task_info_create(0, 0, in_name,
					 _batchfilename(job, msg->std_out),
					 _batchfilename(job, msg->std_err));
	job->task[0]->argc = job->argc;
	job->task[0]->argv = job->argv;

	return job;
}

extern void
stepd_step_rec_destroy(stepd_step_rec_t *job)
{
	uint16_t multi_prog = 0;
	int i;

	_array_free(&job->env);
	_array_free(&job->argv);

	if (job->flags & LAUNCH_MULTI_PROG)
		multi_prog = 1;
	for (i = 0; i < job->node_tasks; i++)
		_task_info_destroy(job->task[i], multi_prog);
	xfree(job->task);
	eio_handle_destroy(job->eio);
	FREE_NULL_LIST(job->sruns);
	FREE_NULL_LIST(job->clients);
	FREE_NULL_LIST(job->stdout_eio_objs);
	FREE_NULL_LIST(job->stderr_eio_objs);
	FREE_NULL_LIST(job->free_incoming);
	FREE_NULL_LIST(job->free_outgoing);
	FREE_NULL_LIST(job->outgoing_cache);
	FREE_NULL_LIST(job->job_gres_list);
	FREE_NULL_LIST(job->step_gres_list);
	xfree(job->ckpt_dir);
	xfree(job->cpu_bind);
	xfree(job->cwd);
	xfree(job->envtp);
	xfree(job->pw_gecos);
	xfree(job->pw_dir);
	xfree(job->pw_shell);
	xfree(job->gids);
	xfree(job->mem_bind);
	eio_handle_destroy(job->msg_handle);
	xfree(job->node_name);
	mpmd_free(job);
	xfree(job->pack_task_cnts);
	if ((job->pack_nnodes != NO_VAL) && job->pack_tids) {
		/* pack_tids == NULL if request from pre-v19.05 srun */
		for (i = 0; i < job->pack_nnodes; i++)
			xfree(job->pack_tids[i]);
		xfree(job->pack_tids);
	}
	xfree(job->pack_tid_offsets);
	xfree(job->task_prolog);
	xfree(job->task_epilog);
	xfree(job->job_alloc_cores);
	xfree(job->restart_dir);
	xfree(job->step_alloc_cores);
	xfree(job->task_cnts);
	xfree(job->tres_bind);
	xfree(job->tres_freq);
	xfree(job->user_name);
	xfree(job->x11_xauthority);
	xfree(job);
}

extern srun_info_t *
srun_info_create(slurm_cred_t *cred, slurm_addr_t *resp_addr,
		 slurm_addr_t *ioaddr, uint16_t protocol_version)
{
	char             *data = NULL;
	uint32_t          len  = 0;
	srun_info_t *srun = xmalloc(sizeof(srun_info_t));
	srun_key_t       *key  = xmalloc(sizeof(srun_key_t));

	srun->key    = key;
	if (!protocol_version || (protocol_version == NO_VAL16))
		protocol_version = SLURM_PROTOCOL_VERSION;
	srun->protocol_version = protocol_version;
	/*
	 * If no credential was provided, return the empty
	 * srun info object. (This is used, for example, when
	 * creating a batch job structure)
	 */
	if (!cred) return srun;

	slurm_cred_get_signature(cred, &data, &len);

	len = len > SLURM_IO_KEY_SIZE ? SLURM_IO_KEY_SIZE : len;

	if (data != NULL) {
		memcpy((void *) key->data, data, len);

		if (len < SLURM_IO_KEY_SIZE)
			memset( (void *) (key->data + len), 0,
				SLURM_IO_KEY_SIZE - len);
	}

	if (ioaddr != NULL)
		srun->ioaddr    = *ioaddr;
	if (resp_addr != NULL)
		srun->resp_addr = *resp_addr;
	return srun;
}

extern void
srun_info_destroy(srun_info_t *srun)
{
	xfree(srun->key);
	xfree(srun);
}

static stepd_step_task_info_t *_task_info_create(int taskid, int gtaskid,
						 char *ifname, char *ofname,
						 char *efname)
{
	stepd_step_task_info_t *t = xmalloc(sizeof(stepd_step_task_info_t));

	xassert(taskid >= 0);
	xassert(gtaskid >= 0);

	slurm_mutex_init(&t->mutex);
	slurm_mutex_lock(&t->mutex);
	t->state       = STEPD_STEP_TASK_INIT;
	t->id          = taskid;
	t->gtid	       = gtaskid;
	t->pid         = (pid_t) -1;
	t->ifname      = ifname;
	t->ofname      = ofname;
	t->efname      = efname;
	t->stdin_fd    = -1;
	t->to_stdin    = -1;
	t->stdout_fd   = -1;
	t->from_stdout = -1;
	t->stderr_fd   = -1;
	t->from_stderr = -1;
	t->in          = NULL;
	t->out         = NULL;
	t->err         = NULL;
	t->killed_by_cmd = false;
	t->aborted     = false;
	t->esent       = false;
	t->exited      = false;
	t->estatus     = -1;
	t->argc	       = 0;
	t->argv	       = NULL;
	slurm_mutex_unlock(&t->mutex);
	return t;
}
