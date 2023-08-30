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
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/interfaces/gres.h"
#include "src/common/group_cache.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/interfaces/select.h"
#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/jobacct_gather.h"
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
static void _job_init_task_info(stepd_step_rec_t *step, uint32_t **gtid,
				char *ifname, char *ofname, char *efname);
static void _srun_info_destructor(void *arg);
static stepd_step_task_info_t *_task_info_create(int taskid, int gtaskid,
						 char *ifname, char *ofname,
						 char *efname);
static void _task_info_destroy(stepd_step_task_info_t *t, uint16_t multi_prog);
static void _task_info_array_destroy(stepd_step_rec_t *step);

/*
 * return the default output filename for a batch job
 */
static char *
_batchfilename(stepd_step_rec_t *step, const char *name)
{
	if (name == NULL) {
		if (step->array_task_id == NO_VAL)
			return fname_create(step, "slurm-%J.out", 0);
		else
			return fname_create(step, "slurm-%A_%a.out", 0);
	} else
		return fname_create(step, name, 0);
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
_expand_stdio_filename(char *filename, int gtaskid, stepd_step_rec_t *step)
{
	int id;

	if (filename == NULL)
		return NULL;

	id = fname_single_task_io(filename);

	if (id < 0)
		return fname_create(step, filename, gtaskid);
	if (id >= step->ntasks) {
		error("Task ID in filename is invalid");
		return NULL;
	}

	if (id == gtaskid)
		return NULL;
	else
		return xstrdup("/dev/null");
}

static void
_job_init_task_info(stepd_step_rec_t *step, uint32_t **gtid,
		    char *ifname, char *ofname, char *efname)
{
	int          i, node_id = step->nodeid;
	char        *in, *out, *err;
	uint32_t     het_job_offset = 0;

	if (step->node_tasks == 0) {
		error("User requested launch of zero tasks!");
		step->task = NULL;
		return;
	}

	if (step->het_job_offset != NO_VAL)
		het_job_offset = step->het_job_offset;

#if defined(HAVE_NATIVE_CRAY)
	for (i = 0; i < step->nnodes; i++) {
		int j;
		for (j = 1; j < step->task_cnts[i]; j++) {
			if (gtid[i][j] != gtid[i][j-1] + 1) {
				step->non_smp = 1;
				break;
			}
		}
	}
#endif

	step->task = (stepd_step_task_info_t **)
		xmalloc(step->node_tasks * sizeof(stepd_step_task_info_t *));

	for (i = 0; i < step->node_tasks; i++) {
		in  = _expand_stdio_filename(ifname,
					     gtid[node_id][i] + het_job_offset,
					     step);
		out = _expand_stdio_filename(ofname,
					     gtid[node_id][i] + het_job_offset,
					     step);
		err = _expand_stdio_filename(efname,
					     gtid[node_id][i] + het_job_offset,
					     step);
		step->task[i] = _task_info_create(i, gtid[node_id][i], in, out,
						 err);
		if ((step->flags & LAUNCH_MULTI_PROG) == 0) {
			step->task[i]->argc = step->argc;
			step->task[i]->argv = step->argv;
		}
	}

	if (step->flags & LAUNCH_MULTI_PROG) {
		if (!xstrcmp(slurm_conf.switch_type, "switch/cray_aries"))
			multi_prog_parse(step, gtid);
		for (i = 0; i < step->node_tasks; i++){
			multi_prog_get_argv(step->argv[1], step->env,
					    gtid[node_id][i],
					    &step->task[i]->argc,
					    &step->task[i]->argv,
					    step->argc, step->argv);
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
	} /* otherwise, t->argv is a pointer to step->argv */
	xfree(t);
}

static void _task_info_array_destroy(stepd_step_rec_t *step)
{
	uint16_t multi_prog = 0;

	if (!step->task)
		return;

	if (step->flags & LAUNCH_MULTI_PROG)
		multi_prog = 1;

	for (int i = 0; i < step->node_tasks; i++)
		_task_info_destroy(step->task[i], multi_prog);

	xfree(step->task);
}

static void _slurm_cred_to_step_rec(slurm_cred_t *cred, stepd_step_rec_t *step)
{
	slurm_cred_arg_t *cred_arg = slurm_cred_get_args(cred);

	/*
	 * This may have been filed in already from batch_job_launch_msg_t
	 * or launch_tasks_request_msg_t.
	 */
	if (!step->user_name)
		step->user_name = xstrdup(cred_arg->pw_name);

	step->pw_gecos = xstrdup(cred_arg->pw_gecos);
	step->pw_dir = xstrdup(cred_arg->pw_dir);
	step->pw_shell = xstrdup(cred_arg->pw_shell);

	step->ngids = cred_arg->ngids;
	step->gids = cred_arg->gids;
	cred_arg->gids = copy_gids(cred_arg->ngids, cred_arg->gids);
	step->gr_names = copy_gr_names(cred_arg->ngids, cred_arg->gr_names);

	step->job_end_time = cred_arg->job_end_time;
	step->job_licenses = xstrdup(cred_arg->job_licenses);
	step->job_start_time = cred_arg->job_start_time;
	step->selinux_context = xstrdup(cred_arg->selinux_context);

	step->alias_list = xstrdup(cred_arg->job_alias_list);

	slurm_cred_unlock_args(cred);
}

/* create a slurmd step structure from a launch tasks message */
extern stepd_step_rec_t *stepd_step_rec_create(launch_tasks_request_msg_t *msg,
					       uint16_t protocol_version)
{
	stepd_step_rec_t *step = NULL;
	srun_info_t   *srun = NULL;
	slurm_addr_t     resp_addr;
	slurm_addr_t     io_addr;
	int            i, nodeid = NO_VAL;

	xassert(msg != NULL);
	xassert(msg->complete_nodelist != NULL);
	debug3("entering stepd_step_rec_create");

	if (acct_gather_check_acct_freq_task(msg->job_mem_lim, msg->acctg_freq))
		return NULL;

	step = xmalloc(sizeof(stepd_step_rec_t));
	step->msg = msg;
#ifndef HAVE_FRONT_END
	nodeid = nodelist_find(msg->complete_nodelist, conf->node_name);
	step->node_name = xstrdup(conf->node_name);
#else
	nodeid = 0;
	step->node_name = xstrdup(msg->complete_nodelist);
#endif
	if (nodeid < 0) {
		error("couldn't find node %s in %s",
		      step->node_name, msg->complete_nodelist);
		stepd_step_rec_destroy(step);
		return NULL;
	}

	step->state = SLURMSTEPD_STEP_STARTING;
	slurm_cond_init(&step->state_cond, NULL);
	slurm_mutex_init(&step->state_mutex);
	step->node_tasks	= msg->tasks_to_launch[nodeid];
	step->task_cnts  = xcalloc(msg->nnodes, sizeof(uint16_t));
	memcpy(step->task_cnts, msg->tasks_to_launch,
	       sizeof(uint16_t) * msg->nnodes);
	step->ntasks	= msg->ntasks;
	memcpy(&step->step_id, &msg->step_id, sizeof(step->step_id));

	step->uid	= (uid_t) msg->uid;
	step->gid	= (gid_t) msg->gid;
	step->user_name	= xstrdup(msg->user_name);
	_slurm_cred_to_step_rec(msg->cred, step);
	/*
	 * Favor the group info in the launch cred if available - fall back
	 * to the launch_tasks_request_msg_t info if send_gids is disabled.
	 */
	if (!step->ngids) {
		if (slurm_cred_send_gids_enabled()) {
			error("No gids given in the cred.");
			stepd_step_rec_destroy(step);
			return NULL;
		}
		step->ngids = (int) msg->ngids;
		step->gids = copy_gids(msg->ngids, msg->gids);
	}

	if (msg->container) {
		step_container_t *c = xmalloc(sizeof(*step->container));
		c->magic = STEP_CONTAINER_MAGIC;
		c->bundle = xstrdup(msg->container);
		step->container = c;
	}

	step->cwd	= xstrdup(msg->cwd);
	step->task_dist	= msg->task_dist;

	step->cpu_bind_type = msg->cpu_bind_type;
	step->cpu_bind = xstrdup(msg->cpu_bind);
	step->mem_bind_type = msg->mem_bind_type;
	step->mem_bind = xstrdup(msg->mem_bind);
	step->tres_bind = xstrdup(msg->tres_bind);
	step->tres_freq = xstrdup(msg->tres_freq);
	step->cpu_freq_min = msg->cpu_freq_min;
	step->cpu_freq_max = msg->cpu_freq_max;
	step->cpu_freq_gov = msg->cpu_freq_gov;
	step->cpus_per_task = msg->cpus_per_task;

	step->env     = _array_copy(msg->envc, msg->env);
	step->array_job_id  = msg->step_id.job_id;
	step->array_task_id = NO_VAL;
	/* Used for env vars */
	step->het_job_node_offset = msg->het_job_node_offset;
	step->het_job_step_cnt = msg->het_job_step_cnt;
	step->het_job_id  = msg->het_job_id;	/* Used for env vars */
	step->het_job_nnodes = msg->het_job_nnodes;	/* Used for env vars */
	if (msg->het_job_nnodes && msg->het_job_ntasks &&
	    msg->het_job_task_cnts) {
		step->het_job_ntasks = msg->het_job_ntasks;/* Used for env vars*/
		step->het_job_task_cnts = xcalloc(msg->het_job_nnodes,
					      sizeof(uint16_t));
		memcpy(step->het_job_task_cnts, msg->het_job_task_cnts,
		       sizeof(uint16_t) * msg->het_job_nnodes);
		step->het_job_tids = xcalloc(msg->het_job_nnodes,
					    sizeof(uint32_t *));
		for (i = 0; i < msg->het_job_nnodes; i++) {
			step->het_job_tids[i] =
				xcalloc(step->het_job_task_cnts[i],
					sizeof(uint32_t));
			memcpy(step->het_job_tids[i],
			       msg->het_job_tids[i],
			       sizeof(uint32_t) *
			       step->het_job_task_cnts[i]);
		}
		if (msg->het_job_tid_offsets) {
			step->het_job_tid_offsets = xcalloc(step->het_job_ntasks,
							   sizeof(uint32_t));
			memcpy(step->het_job_tid_offsets,
			       msg->het_job_tid_offsets,
			       step->het_job_ntasks * sizeof(uint32_t));
		}
	}
	/* Used for env vars & labels */
	step->het_job_offset = msg->het_job_offset;
	/* Used for env vars & labels */
	step->het_job_task_offset = msg->het_job_task_offset;
	step->het_job_node_list = xstrdup(msg->het_job_node_list);
	for (i = 0; i < msg->envc; i++) {
		/*                         1234567890123456789 */
		if (!xstrncmp(msg->env[i], "SLURM_ARRAY_JOB_ID=", 19))
			step->array_job_id = atoi(msg->env[i] + 19);
		/*                         12345678901234567890 */
		if (!xstrncmp(msg->env[i], "SLURM_ARRAY_TASK_ID=", 20))
			step->array_task_id = atoi(msg->env[i] + 20);
	}

	step->eio     = eio_handle_create(0);
	step->sruns   = list_create((ListDelF) _srun_info_destructor);

	/*
	 * Based on my testing the next 3 lists here could use the
	 * eio_obj_destroy, but if you do you can get an invalid read.  Since
	 * these stay until the end of the step it isn't that big of a deal.
	 */
	step->clients = list_create(NULL); /* FIXME! Needs destructor */
	step->stdout_eio_objs = list_create(NULL); /* FIXME! Needs destructor */
	step->stderr_eio_objs = list_create(NULL); /* FIXME! Needs destructor */
	step->free_incoming = list_create(NULL); /* FIXME! Needs destructor */
	step->incoming_count = 0;
	step->free_outgoing = list_create(NULL); /* FIXME! Needs destructor */
	step->outgoing_count = 0;
	step->outgoing_cache = list_create(NULL); /* FIXME! Needs destructor */

	step->envtp   = xmalloc(sizeof(env_t));
	step->envtp->jobid = -1;
	step->envtp->stepid = -1;
	step->envtp->procid = -1;
	step->envtp->localid = -1;
	step->envtp->nodeid = -1;

	step->envtp->distribution = 0;
	step->envtp->cpu_bind_type = 0;
	step->envtp->cpu_bind = NULL;
	step->envtp->mem_bind_type = 0;
	step->envtp->mem_bind = NULL;
	if (!msg->resp_port)
		msg->num_resp_port = 0;
	if (msg->num_resp_port) {
		step->envtp->comm_port =
			msg->resp_port[nodeid % msg->num_resp_port];
		memcpy(&resp_addr, &msg->orig_addr, sizeof(slurm_addr_t));
		slurm_set_port(&resp_addr,
			       msg->resp_port[nodeid % msg->num_resp_port]);
	} else {
		memset(&resp_addr, 0, sizeof(slurm_addr_t));
	}

	if (msg->num_io_port) {
		memcpy(&io_addr, &msg->orig_addr, sizeof(slurm_addr_t));
		slurm_set_port(&io_addr,
			       msg->io_port[nodeid % msg->num_io_port]);
	} else {
		memset(&io_addr, 0, sizeof(slurm_addr_t));
	}

	srun = srun_info_create(msg->cred, &resp_addr, &io_addr, step->uid,
				protocol_version);

	step->profile     = msg->profile;
	step->task_prolog = xstrdup(msg->task_prolog);
	step->task_epilog = xstrdup(msg->task_epilog);

	step->argc    = msg->argc;
	step->argv    = _array_copy(step->argc, msg->argv);

	step->nnodes  = msg->nnodes;
	step->nodeid  = nodeid;
	step->debug   = msg->slurmd_debug;
	step->cpus    = msg->node_cpus;
	step->job_core_spec = msg->job_core_spec;

	/* This needs to happen before acct_gather_profile_startpoll
	   and only really looks at the profile in the step.
	*/
	acct_gather_profile_g_node_step_start(step);

	acct_gather_profile_startpoll(msg->acctg_freq,
				      slurm_conf.job_acct_gather_freq);

	step->timelimit   = (time_t) -1;
	step->flags       = msg->flags;
	step->switch_job  = msg->switch_job;
	step->open_mode   = msg->open_mode;
	step->options     = msg->options;

	format_core_allocs(msg->cred, conf->node_name, conf->cpus,
			   &step->job_alloc_cores, &step->step_alloc_cores,
			   &step->job_mem, &step->step_mem);

	if (step->step_mem && slurm_conf.job_acct_oom_kill) {
		jobacct_gather_set_mem_limit(&step->step_id, step->step_mem);
	} else if (step->job_mem && slurm_conf.job_acct_oom_kill) {
		jobacct_gather_set_mem_limit(&step->step_id, step->job_mem);
	}

	/* only need these values on the extern step, don't copy otherwise */
	if ((msg->step_id.step_id == SLURM_EXTERN_CONT) && msg->x11) {
		step->x11 = msg->x11;
		step->x11_alloc_host = xstrdup(msg->x11_alloc_host);
		step->x11_alloc_port = msg->x11_alloc_port;
		step->x11_magic_cookie = xstrdup(msg->x11_magic_cookie);
		step->x11_target = xstrdup(msg->x11_target);
		step->x11_target_port = msg->x11_target_port;
	}

	get_cred_gres(msg->cred, conf->node_name,
		      &step->job_gres_list, &step->step_gres_list);

	list_append(step->sruns, (void *) srun);

	_job_init_task_info(step, msg->global_task_ids,
			    msg->ifname, msg->ofname, msg->efname);

	return step;
}

extern stepd_step_rec_t *
batch_stepd_step_rec_create(batch_job_launch_msg_t *msg)
{
	stepd_step_rec_t *step;
	srun_info_t  *srun = NULL;
	char *in_name;

	xassert(msg != NULL);

	debug3("entering batch_stepd_step_rec_create");

	if (acct_gather_check_acct_freq_task(msg->job_mem, msg->acctg_freq))
		return NULL;

	step = xmalloc(sizeof(stepd_step_rec_t));

	step->state = SLURMSTEPD_STEP_STARTING;
	slurm_cond_init(&step->state_cond, NULL);
	slurm_mutex_init(&step->state_mutex);
	if (msg->cpus_per_node)
		step->cpus    = msg->cpus_per_node[0];
	step->node_tasks  = 1;
	step->ntasks  = msg->ntasks;
	step->step_id.job_id   = msg->job_id;
	step->step_id.step_id  = SLURM_BATCH_SCRIPT;
	step->step_id.step_het_comp  = NO_VAL;
	step->array_job_id  = msg->array_job_id;
	step->array_task_id = msg->array_task_id;
	step->het_job_step_cnt = NO_VAL;
	step->het_job_id  = NO_VAL;	/* Used to set env vars */
	step->het_job_nnodes = NO_VAL;	/* Used to set env vars */
	step->het_job_ntasks = NO_VAL;	/* Used to set env vars */
	step->het_job_offset = NO_VAL;	/* Used to set labels and env vars */
	step->job_core_spec = msg->job_core_spec;

	step->batch   = true;
	step->node_name  = xstrdup(conf->node_name);

	step->uid	= (uid_t) msg->uid;
	step->gid	= (gid_t) msg->gid;
	step->user_name	= xstrdup(msg->user_name);
	_slurm_cred_to_step_rec(msg->cred, step);
	/*
	 * Favor the group info in the launch cred if available - fall back
	 * to the batch_job_launch_msg_t info if send_gids is disabled.
	 */
	if (!step->ngids) {
		if (slurm_cred_send_gids_enabled()) {
			error("No gids given in the cred.");
			stepd_step_rec_destroy(step);
			return NULL;
		}
		step->ngids = (int) msg->ngids;
		step->gids = copy_gids(msg->ngids, msg->gids);
	}

	step->profile    = msg->profile;

	/* give them all to the 1 task */
	step->cpus_per_task = step->cpus;

	/* This needs to happen before acct_gather_profile_startpoll
	   and only really looks at the profile in the step.
	*/
	acct_gather_profile_g_node_step_start(step);
	/* needed for the jobacct_gather plugin to start */
	acct_gather_profile_startpoll(msg->acctg_freq,
				      slurm_conf.job_acct_gather_freq);

	step->open_mode  = msg->open_mode;
	step->overcommit = (bool) msg->overcommit;

	step->cwd     = xstrdup(msg->work_dir);

	if (msg->container) {
		step_container_t *c = xmalloc(sizeof(*step->container));
		c->magic = STEP_CONTAINER_MAGIC;
		c->bundle = xstrdup(msg->container);
		step->container = c;
	}

	step->env     = _array_copy(msg->envc, msg->environment);
	step->eio     = eio_handle_create(0);
	step->sruns   = list_create((ListDelF) _srun_info_destructor);
	step->envtp   = xmalloc(sizeof(env_t));
	step->envtp->jobid = -1;
	step->envtp->stepid = -1;
	step->envtp->procid = -1;
	step->envtp->localid = -1;
	step->envtp->nodeid = -1;

	step->envtp->distribution = 0;
	step->cpu_bind_type = msg->cpu_bind_type;
	step->cpu_bind = xstrdup(msg->cpu_bind);
	step->envtp->mem_bind_type = 0;
	step->envtp->mem_bind = NULL;
	step->envtp->restart_cnt = msg->restart_cnt;

	if (msg->cpus_per_node)
		step->cpus    = msg->cpus_per_node[0];

	format_core_allocs(msg->cred, conf->node_name, conf->cpus,
			   &step->job_alloc_cores, &step->step_alloc_cores,
			   &step->job_mem, &step->step_mem);
	if (step->step_mem && slurm_conf.job_acct_oom_kill)
		jobacct_gather_set_mem_limit(&step->step_id, step->step_mem);
	else if (step->job_mem && slurm_conf.job_acct_oom_kill)
		jobacct_gather_set_mem_limit(&step->step_id, step->job_mem);

	get_cred_gres(msg->cred, conf->node_name,
		      &step->job_gres_list, &step->step_gres_list);

	srun = srun_info_create(NULL, NULL, NULL, step->uid, NO_VAL16);

	list_append(step->sruns, (void *) srun);

	if (msg->argc) {
		step->argc    = msg->argc;
		step->argv    = _array_copy(step->argc, msg->argv);
	} else {
		step->argc    = 1;
		/* step script has not yet been written out to disk --
		 * argv will be filled in later by _make_batch_script()
		 */
		step->argv    = (char **) xmalloc(2 * sizeof(char *));
	}

	step->task = xmalloc(sizeof(stepd_step_task_info_t *));
	if (msg->std_err == NULL)
		msg->std_err = xstrdup(msg->std_out);

	if (msg->std_in == NULL)
		in_name = xstrdup("/dev/null");
	else
		in_name = fname_create(step, msg->std_in, 0);

	step->task[0] = _task_info_create(0, 0, in_name,
					 _batchfilename(step, msg->std_out),
					 _batchfilename(step, msg->std_err));
	step->task[0]->argc = step->argc;
	step->task[0]->argv = step->argv;

	return step;
}

extern void
stepd_step_rec_destroy(stepd_step_rec_t *step)
{
	int i;

	env_array_free(step->env);
	step->env = NULL;
	env_array_free(step->argv);
	step->argv = NULL;

	_task_info_array_destroy(step);
	if (step->eio) {
		eio_handle_destroy(step->eio);
		step->eio = NULL;
	}
	FREE_NULL_LIST(step->sruns);
	FREE_NULL_LIST(step->clients);
	FREE_NULL_LIST(step->stdout_eio_objs);
	FREE_NULL_LIST(step->stderr_eio_objs);
	FREE_NULL_LIST(step->free_incoming);
	FREE_NULL_LIST(step->free_outgoing);
	FREE_NULL_LIST(step->outgoing_cache);
	FREE_NULL_LIST(step->job_gres_list);
	FREE_NULL_LIST(step->step_gres_list);
	xfree(step->alias_list);

	if (step->container) {
		step_container_t *c = step->container;
		xassert(c->magic == STEP_CONTAINER_MAGIC);
		xfree(c->bundle);
		FREE_NULL_DATA(c->config);
		xfree(c->mount_spool_dir);
		xfree(c->rootfs);
		xfree(c->spool_dir);
		xfree(step->container);
	}

	xfree(step->cpu_bind);
	xfree(step->cwd);
	xfree(step->envtp);
	xfree(step->job_licenses);
	xfree(step->pw_gecos);
	xfree(step->pw_dir);
	xfree(step->pw_shell);
	xfree(step->gids);
	xfree(step->mem_bind);
	if (step->msg_handle) {
		eio_handle_destroy(step->msg_handle);
		step->msg_handle = NULL;
	}
	xfree(step->node_name);
	mpmd_free(step);
	xfree(step->het_job_task_cnts);
	if (step->het_job_nnodes != NO_VAL) {
		for (i = 0; i < step->het_job_nnodes; i++)
			xfree(step->het_job_tids[i]);
		xfree(step->het_job_tids);
	}
	xfree(step->het_job_tid_offsets);
	xfree(step->task_prolog);
	xfree(step->task_epilog);
	xfree(step->job_alloc_cores);
	xfree(step->step_alloc_cores);
	xfree(step->task_cnts);
	xfree(step->tres_bind);
	xfree(step->tres_freq);
	xfree(step->user_name);
	xfree(step->x11_xauthority);
	xfree(step);
}

extern srun_info_t *srun_info_create(slurm_cred_t *cred,
				     slurm_addr_t *resp_addr,
				     slurm_addr_t *ioaddr, uid_t uid,
				     uint16_t protocol_version)
{
	char             *data = NULL;
	uint32_t          len  = 0;
	srun_info_t *srun = xmalloc(sizeof(srun_info_t));
	srun_key_t       *key  = xmalloc(sizeof(srun_key_t));

	srun->key    = key;
	if (!protocol_version || (protocol_version == NO_VAL16))
		protocol_version = SLURM_PROTOCOL_VERSION;
	srun->protocol_version = protocol_version;
	srun->uid = uid;
	/*
	 * If no credential was provided, return the empty
	 * srun info object. (This is used, for example, when
	 * creating a batch job structure)
	 */
	if (!cred) return srun;

	slurm_cred_get_signature(cred, &data, &len);

	if (data != NULL) {
		key->len = len;
		key->data = xmalloc(len);
		memcpy((void *) key->data, data, len);
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
	srun_key_destroy(srun->key);
	xfree(srun);
}

extern void srun_key_destroy(srun_key_t *key)
{
	xfree(key->data);
	xfree(key);
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
