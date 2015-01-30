/*****************************************************************************\
 *  src/slurmd/slurmstepd/slurmstepd_job.c - stepd_step_rec_t routines
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <grp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/gres.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/fname.h"
#include "src/slurmd/slurmstepd/multi_prog.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static char ** _array_copy(int n, char **src);
static void _array_free(char ***array);
static void _srun_info_destructor(void *arg);
static void _job_init_task_info(stepd_step_rec_t *job, uint32_t **gtid,
				char *ifname, char *ofname, char *efname);
static void _task_info_destroy(stepd_step_task_info_t *t, uint16_t multi_prog);

/* returns 0 if invalid gid, otherwise returns 1.  Set gid with
 * correct gid if root launched job.  Also set user_name
 * if not already set. */
static int
_valid_uid_gid(uid_t uid, gid_t *gid, char **user_name)
{
	struct passwd *pwd;
	struct group *grp;
	int i;

#ifdef HAVE_NATIVE_CRAY
	/* already verified */
	if (*user_name)
		return 1;
#endif
znovu:
	errno = 0;
	pwd = getpwuid(uid);
	if (!pwd) {
		if (errno == EINTR)
			goto znovu;
		error("uid %ld not found on system", (long) uid);
		slurm_seterrno(ESLURMD_UID_NOT_FOUND);
		return 0;
	}

	if (!*user_name)
		*user_name = xstrdup(pwd->pw_name);

	if (pwd->pw_gid == *gid)
		return 1;

	grp = getgrgid(*gid);
	if (!grp) {
		error("gid %ld not found on system", (long)(*gid));
		slurm_seterrno(ESLURMD_GID_NOT_FOUND);
		return 0;
	}

	/* Allow user root to use any valid gid */
	if (pwd->pw_uid == 0) {
		pwd->pw_gid = *gid;
		return 1;
	}
	for (i = 0; grp->gr_mem[i]; i++) {
		if (!strcmp(pwd->pw_name, grp->gr_mem[i])) {
			pwd->pw_gid = *gid;
		       	return 1;
	       	}
	}

	/* root user may have launched this job for this user, but
	 * root did not explicitly set the gid. This would set the
	 * gid to 0. In this case we should set the appropriate
	 * default gid for the user (from the passwd struct).
	 */
	if (*gid == 0) {
		*gid = pwd->pw_gid;
		return 1;
	}
	error("uid %ld is not a member of gid %ld",
		(long)pwd->pw_uid, (long)(*gid));
	slurm_seterrno(ESLURMD_GID_NOT_FOUND);

	return 0;
}

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

	if (job->node_tasks == 0) {
		error("User requested launch of zero tasks!");
		job->task = NULL;
		return;
	}

	job->task = (stepd_step_task_info_t **)
		xmalloc(job->node_tasks * sizeof(stepd_step_task_info_t *));

	for (i = 0; i < job->node_tasks; i++){
		in = _expand_stdio_filename(ifname, gtid[node_id][i], job);
		out = _expand_stdio_filename(ofname, gtid[node_id][i], job);
		err = _expand_stdio_filename(efname, gtid[node_id][i], job);
		job->task[i] = task_info_create(i, gtid[node_id][i], in, out,
						err);
		if (!job->multi_prog) {
			job->task[i]->argc = job->argc;
			job->task[i]->argv = job->argv;
		}
	}

	if (job->multi_prog) {
		char *switch_type = slurm_get_switch_type();
		if (!strcmp(switch_type, "switch/cray"))
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

/* create a slurmd job structure from a launch tasks message */
extern stepd_step_rec_t *
stepd_step_rec_create(launch_tasks_request_msg_t *msg)
{
	stepd_step_rec_t  *job = NULL;
	srun_info_t   *srun = NULL;
	slurm_addr_t     resp_addr;
	slurm_addr_t     io_addr;
	int            i, nodeid = NO_VAL;

	xassert(msg != NULL);
	xassert(msg->complete_nodelist != NULL);
	debug3("entering stepd_step_rec_create");

	if (!_valid_uid_gid((uid_t)msg->uid, &(msg->gid), &(msg->user_name)))
		return NULL;

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

	job->state	= SLURMSTEPD_STEP_STARTING;
	job->node_tasks	= msg->tasks_to_launch[nodeid];
	i = sizeof(uint16_t) * msg->nnodes;
	job->task_cnts  = xmalloc(i);
	memcpy(job->task_cnts, msg->tasks_to_launch, i);
	job->ntasks	= msg->ntasks;
	job->jobid	= msg->job_id;
	job->stepid	= msg->job_step_id;

	job->uid	= (uid_t) msg->uid;
	job->user_name  = xstrdup(msg->user_name);
	job->gid	= (gid_t) msg->gid;
	job->cwd	= xstrdup(msg->cwd);
	job->task_dist	= msg->task_dist;

	job->cpu_bind_type = msg->cpu_bind_type;
	job->cpu_bind = xstrdup(msg->cpu_bind);
	job->mem_bind_type = msg->mem_bind_type;
	job->mem_bind = xstrdup(msg->mem_bind);
	job->cpu_freq = msg->cpu_freq;
	job->ckpt_dir = xstrdup(msg->ckpt_dir);
	job->restart_dir = xstrdup(msg->restart_dir);
	job->cpus_per_task = msg->cpus_per_task;

	job->env     = _array_copy(msg->envc, msg->env);
	job->array_job_id  = msg->job_id;
	job->array_task_id = NO_VAL;
	for (i = 0; i < msg->envc; i++) {
		/*                         1234567890123456789 */
		if (!strncmp(msg->env[i], "SLURM_ARRAY_JOB_ID=", 19))
			job->array_job_id = atoi(msg->env[i] + 19);
		/*                         12345678901234567890 */
		if (!strncmp(msg->env[i], "SLURM_ARRAY_TASK_ID=", 20))
			job->array_task_id = atoi(msg->env[i] + 20);
	}

	job->eio     = eio_handle_create();
	job->sruns   = list_create((ListDelF) _srun_info_destructor);
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
	job->envtp->ckpt_dir = NULL;
	job->envtp->comm_port = msg->resp_port[nodeid % msg->num_resp_port];

	memcpy(&resp_addr, &msg->orig_addr, sizeof(slurm_addr_t));
	slurm_set_addr(&resp_addr,
		       msg->resp_port[nodeid % msg->num_resp_port],
		       NULL);
	job->user_managed_io = msg->user_managed_io;
	if (!msg->user_managed_io) {
		memcpy(&io_addr,   &msg->orig_addr, sizeof(slurm_addr_t));
		slurm_set_addr(&io_addr,
			       msg->io_port[nodeid % msg->num_io_port],
			       NULL);
	}

	srun = srun_info_create(msg->cred, &resp_addr, &io_addr);

	job->buffered_stdio = msg->buffered_stdio;
	job->labelio = msg->labelio;

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

	job->multi_prog  = msg->multi_prog;
	job->timelimit   = (time_t) -1;
	job->task_flags  = msg->task_flags;
	job->switch_job  = msg->switch_job;
	job->pty         = msg->pty;
	job->open_mode   = msg->open_mode;
	job->options     = msg->options;
	format_core_allocs(msg->cred, conf->node_name, conf->cpus,
			   &job->job_alloc_cores, &job->step_alloc_cores,
			   &job->job_mem, &job->step_mem);

	/* If users have configured MemLimitEnforce=no
	 * in their slurm.conf keep going.
	 */
	if (job->step_mem
	    && conf->mem_limit_enforce) {
		jobacct_gather_set_mem_limit(job->jobid, job->stepid,
					     job->step_mem);
	} else if (job->job_mem
		   && conf->mem_limit_enforce) {
		jobacct_gather_set_mem_limit(job->jobid, job->stepid,
					     job->job_mem);
	}

#ifdef HAVE_ALPS_CRAY
	/* This is only used for Cray emulation mode where slurmd is used to
	 * launch job steps. On a real Cray system, ALPS is used to launch
	 * the tasks instead of SLURM. SLURM's task launch RPC does NOT
	 * contain the reservation ID, so just use some non-zero value here
	 * for testing purposes. */
	job->resv_id = 1;
	select_g_select_jobinfo_set(msg->select_jobinfo, SELECT_JOBDATA_RESV_ID,
				    &job->resv_id);
#endif

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

	if (!_valid_uid_gid((uid_t)msg->uid, &(msg->gid), &(msg->user_name)))
		return NULL;

	if (acct_gather_check_acct_freq_task(msg->job_mem, msg->acctg_freq))
		return NULL;

	job = xmalloc(sizeof(stepd_step_rec_t));

	job->state   = SLURMSTEPD_STEP_STARTING;
	if (msg->cpus_per_node)
		job->cpus    = msg->cpus_per_node[0];
	job->node_tasks  = 1;
	job->ntasks  = msg->ntasks;
	job->jobid   = msg->job_id;
	job->stepid  = msg->step_id;
	job->array_job_id  = msg->array_job_id;
	job->array_task_id = msg->array_task_id;
	job->job_core_spec = msg->job_core_spec;

	job->batch   = true;
	job->node_name  = xstrdup(conf->node_name);
	/* This needs to happen before acct_gather_profile_startpoll
	   and only really looks at the profile in the job.
	*/
	acct_gather_profile_g_node_step_start(job);
	/* needed for the jobacct_gather plugin to start */
	acct_gather_profile_startpoll(msg->acctg_freq,
				      conf->job_acct_gather_freq);

	job->multi_prog = 0;
	job->open_mode  = msg->open_mode;
	job->overcommit = (bool) msg->overcommit;

	job->uid     = (uid_t) msg->uid;
	job->user_name  = xstrdup(msg->user_name);
	job->gid     = (gid_t) msg->gid;
	job->cwd     = xstrdup(msg->work_dir);

	job->ckpt_dir = xstrdup(msg->ckpt_dir);
	job->restart_dir = xstrdup(msg->restart_dir);

	job->env     = _array_copy(msg->envc, msg->environment);
	job->eio     = eio_handle_create();
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
	job->envtp->ckpt_dir = NULL;
	job->envtp->restart_cnt = msg->restart_cnt;

	if (msg->cpus_per_node)
		job->cpus    = msg->cpus_per_node[0];

	format_core_allocs(msg->cred, conf->node_name, conf->cpus,
			   &job->job_alloc_cores, &job->step_alloc_cores,
			   &job->job_mem, &job->step_mem);
	if (job->step_mem
		&& conf->mem_limit_enforce)
		jobacct_gather_set_mem_limit(job->jobid, NO_VAL, job->step_mem);
	else if (job->job_mem
		&& conf->mem_limit_enforce)
		jobacct_gather_set_mem_limit(job->jobid, NO_VAL, job->job_mem);

	get_cred_gres(msg->cred, conf->node_name,
		      &job->job_gres_list, &job->step_gres_list);

	srun = srun_info_create(NULL, NULL, NULL);

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

	job->task[0] = task_info_create(0, 0,
					in_name,
					_batchfilename(job, msg->std_out),
					_batchfilename(job, msg->std_err));
	job->task[0]->argc = job->argc;
	job->task[0]->argv = job->argv;

#ifdef HAVE_ALPS_CRAY
	select_g_select_jobinfo_get(msg->select_jobinfo, SELECT_JOBDATA_RESV_ID,
				    &job->resv_id);
#endif

	return job;
}

extern void
stepd_step_rec_destroy(stepd_step_rec_t *job)
{
	int i;

	_array_free(&job->env);
	_array_free(&job->argv);

	for (i = 0; i < job->node_tasks; i++)
		_task_info_destroy(job->task[i], job->multi_prog);
	list_destroy(job->sruns);
	xfree(job->envtp);
	xfree(job->node_name);
	mpmd_free(job);
	xfree(job->task_prolog);
	xfree(job->task_epilog);
	xfree(job->job_alloc_cores);
	xfree(job->step_alloc_cores);
	xfree(job->task_cnts);
	xfree(job->user_name);
	xfree(job);
}

extern srun_info_t *
srun_info_create(slurm_cred_t *cred, slurm_addr_t *resp_addr, slurm_addr_t *ioaddr)
{
	char             *data = NULL;
	uint32_t          len  = 0;
	srun_info_t *srun = xmalloc(sizeof(srun_info_t));
	srun_key_t       *key  = xmalloc(sizeof(srun_key_t));

	srun->key    = key;

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

extern stepd_step_task_info_t *
task_info_create(int taskid, int gtaskid,
		 char *ifname, char *ofname, char *efname)
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
