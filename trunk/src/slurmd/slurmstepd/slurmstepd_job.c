/*****************************************************************************\
 * src/slurmd/slurmstepd/slurmstepd_job.c - slurmd_job_t routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <signal.h>
#include <sys/types.h>
#include <grp.h>

#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/eio.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/fname.h"
#include "src/slurmd/slurmstepd/multi_prog.h"

static char ** _array_copy(int n, char **src);
static void _array_free(char ***array);
static void _srun_info_destructor(void *arg);
static void _job_init_task_info(slurmd_job_t *job, uint32_t *gtid,
				char *ifname, char *ofname, char *efname);
static void _task_info_destroy(slurmd_task_info_t *t, uint16_t multi_prog);

static struct passwd *
_pwd_create(uid_t uid)
{
	struct passwd *pwd = xmalloc(sizeof(*pwd));
	struct passwd *ppwd = getpwuid(uid);

	if (!ppwd) {
		xfree(pwd);
		return NULL;
	}

	pwd->pw_name   = xstrdup(ppwd->pw_name);
	pwd->pw_passwd = xstrdup(ppwd->pw_passwd);
	pwd->pw_gecos  = xstrdup(ppwd->pw_gecos);
	pwd->pw_shell  = xstrdup(ppwd->pw_shell);
	pwd->pw_dir    = xstrdup(ppwd->pw_dir);
	pwd->pw_uid    = ppwd->pw_uid;
	pwd->pw_gid    = ppwd->pw_gid;

	return pwd;
}

static void
_pwd_destroy(struct passwd *pwd)
{
	if (!pwd)
		return;
	xfree(pwd->pw_name);
	xfree(pwd->pw_passwd);
	xfree(pwd->pw_gecos);
	xfree(pwd->pw_shell);
	xfree(pwd->pw_dir);
	xfree(pwd);
}

/* returns 0 if invalid gid, otherwise returns 1 */
static int
_valid_gid(struct passwd *pwd, gid_t *gid)
{
	struct group *grp;
	int i;
	
	if (!pwd)
		return 0;
	if (pwd->pw_gid == *gid)
		return 1;

	grp = getgrgid(*gid);
	if (!grp) {
		error("gid %ld not found on system", (long)(*gid));
		return 0;
	}

	/* Allow user root to use any valid gid */
	if (pwd->pw_uid == 0) {
		pwd->pw_gid = *gid;
		return 1;
	}
	for (i = 0; grp->gr_mem[i]; i++) {
	       	if (strcmp(pwd->pw_name,grp->gr_mem[i]) == 0) {
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
	return 0;
}

/* create a slurmd job structure from a launch tasks message */
slurmd_job_t * 
job_create(launch_tasks_request_msg_t *msg)
{
	struct passwd *pwd = NULL;
	slurmd_job_t  *job = NULL;
	srun_info_t   *srun = NULL;
	slurm_addr     resp_addr;
	slurm_addr     io_addr;
	int            nodeid = NO_VAL;
	
	xassert(msg != NULL);
	xassert(msg->complete_nodelist != NULL);
	debug3("entering job_create");
	if ((pwd = _pwd_create((uid_t)msg->uid)) == NULL) {
		error("uid %ld not found on system", (long) msg->uid);
		slurm_seterrno (ESLURMD_UID_NOT_FOUND);
		return NULL;
	}
	if (!_valid_gid(pwd, &(msg->gid))) {
		slurm_seterrno (ESLURMD_GID_NOT_FOUND);
		_pwd_destroy(pwd);
		return NULL;
	}
	job = xmalloc(sizeof(slurmd_job_t));
#ifndef HAVE_FRONT_END
	nodeid = nodelist_find(msg->complete_nodelist, conf->node_name);
	job->node_name = xstrdup(conf->node_name);
#else
	nodeid = 0;
	job->node_name = xstrdup(msg->complete_nodelist);
#endif
	if(nodeid < 0) {
		error("couldn't find node %s in %s", 
		      job->node_name, msg->complete_nodelist);
		job_destroy(job);
		return NULL;
	}
	
	job->state	= SLURMSTEPD_STEP_STARTING;
	job->pwd	= pwd;
	job->ntasks	= msg->tasks_to_launch[nodeid];
	job->nprocs	= msg->nprocs;
	job->jobid	= msg->job_id;
	job->stepid	= msg->job_step_id;

	job->job_mem	= msg->job_mem;
	if (job->job_mem)
		jobacct_common_set_mem_limit(job->jobid, job->job_mem);
	
	job->uid	= (uid_t) msg->uid;
	job->gid	= (gid_t) msg->gid;
	job->cwd	= xstrdup(msg->cwd);
	job->task_dist	= msg->task_dist;
	job->plane_size	= msg->plane_size;
	
	job->cpu_bind_type = msg->cpu_bind_type;
	job->cpu_bind = xstrdup(msg->cpu_bind);
	job->mem_bind_type = msg->mem_bind_type;
	job->mem_bind = xstrdup(msg->mem_bind);
	job->ckpt_path = xstrdup(msg->ckpt_path);
	job->cpus_per_task = msg->cpus_per_task;

	job->env     = _array_copy(msg->envc, msg->env);
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
	job->envtp->plane_size = 0;

	job->envtp->cpu_bind_type = 0;
	job->envtp->cpu_bind = NULL;
	job->envtp->mem_bind_type = 0;
	job->envtp->mem_bind = NULL;
	job->envtp->ckpt_path = NULL;
	
	memcpy(&resp_addr, &msg->orig_addr, sizeof(slurm_addr));
	slurm_set_addr(&resp_addr,
		       msg->resp_port[nodeid % msg->num_resp_port],
		       NULL);
	job->user_managed_io = msg->user_managed_io;
	if (!msg->user_managed_io) {
		memcpy(&io_addr,   &msg->orig_addr, sizeof(slurm_addr));
		slurm_set_addr(&io_addr,
			       msg->io_port[nodeid % msg->num_io_port],
			       NULL);
	}
	
	srun = srun_info_create(msg->cred, &resp_addr, &io_addr);

	job->buffered_stdio = msg->buffered_stdio;

	job->task_prolog = xstrdup(msg->task_prolog);
	job->task_epilog = xstrdup(msg->task_epilog);

	job->argc    = msg->argc;
	job->argv    = _array_copy(job->argc, msg->argv);

	job->nnodes  = msg->nnodes;
	job->nodeid  = nodeid;
	job->debug   = msg->slurmd_debug;
	job->cpus    = msg->cpus_allocated[nodeid];
	if (msg->acctg_freq != (uint16_t) NO_VAL)
		jobacct_gather_g_change_poll(msg->acctg_freq);
	job->multi_prog  = msg->multi_prog;
	job->timelimit   = (time_t) -1;
	job->task_flags  = msg->task_flags;
	job->switch_job  = msg->switch_job;
	job->pty         = msg->pty;
	job->open_mode   = msg->open_mode;
	job->options     = msg->options;
	
	list_append(job->sruns, (void *) srun);

	_job_init_task_info(job, msg->global_task_ids[nodeid],
			    msg->ifname, msg->ofname, msg->efname);

	return job;
}

/*
 * return the default output filename for a batch job
 */
static char *
_batchfilename(slurmd_job_t *job, const char *name)
{
	if (name == NULL) 
		return fname_create(job, "slurm-%J.out", 0);
	else
		return fname_create(job, name, 0);
}

slurmd_job_t * 
job_batch_job_create(batch_job_launch_msg_t *msg)
{
	struct passwd *pwd;
	slurmd_job_t *job;
	srun_info_t  *srun = NULL;
	char *in_name;

	xassert(msg != NULL);
	
	debug3("entering batch_job_create");

	if ((pwd = _pwd_create((uid_t)msg->uid)) == NULL) {
		error("uid %ld not found on system", (long) msg->uid);
		slurm_seterrno (ESLURMD_UID_NOT_FOUND);
		return NULL;
	}
	if (!_valid_gid(pwd, &(msg->gid))) {
		slurm_seterrno (ESLURMD_GID_NOT_FOUND);
		_pwd_destroy(pwd);
		return NULL;
	}
	
	job = xmalloc(sizeof(slurmd_job_t));
	
	job->state   = SLURMSTEPD_STEP_STARTING;
	job->pwd     = pwd;
	job->cpus    = msg->cpus_per_node[0];
	job->ntasks  = 1; 
	job->nprocs  = msg->nprocs;
	job->jobid   = msg->job_id;
	job->stepid  = msg->step_id;

	job->job_mem = msg->job_mem;
	if (job->job_mem)
		jobacct_common_set_mem_limit(job->jobid, job->job_mem);

	job->batch   = true;
	if (msg->acctg_freq != (uint16_t) NO_VAL)
		jobacct_gather_g_change_poll(msg->acctg_freq);
	job->multi_prog = 0;
	job->open_mode  = msg->open_mode;
	job->overcommit = (bool) msg->overcommit;
	job->node_name  = xstrdup(conf->node_name);

	job->uid     = (uid_t) msg->uid;
	job->gid     = (gid_t) msg->gid;
	job->cwd     = xstrdup(msg->work_dir);

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
	job->envtp->plane_size = 0;

	job->envtp->cpu_bind_type = 0;
	job->envtp->cpu_bind = NULL;
	job->envtp->mem_bind_type = 0;
	job->envtp->mem_bind = NULL;
	job->envtp->ckpt_path = NULL;
	job->cpus_per_task = msg->cpus_per_node[0];

	srun = srun_info_create(NULL, NULL, NULL);

	list_append(job->sruns, (void *) srun);

	if (msg->argc) {
		job->argc    = msg->argc;
		job->argv    = _array_copy(job->argc, msg->argv);
	} else {
		job->argc    = 2;
		/* job script has not yet been written out to disk --
		 * argv will be filled in later
		 */
		job->argv    = (char **) xmalloc(job->argc * sizeof(char *));
	}

	job->task = (slurmd_task_info_t **)
		xmalloc(sizeof(slurmd_task_info_t *));
	if (msg->err == NULL)
		msg->err = xstrdup(msg->out);

	if (msg->in == NULL)
		in_name = xstrdup("/dev/null");
	else 
		in_name = fname_create(job, msg->in, 0);

	job->task[0] = task_info_create(0, 0,
					in_name,
					_batchfilename(job, msg->out),
					_batchfilename(job, msg->err));
	job->task[0]->argc = job->argc;
	job->task[0]->argv = job->argv;

	return job;
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
_expand_stdio_filename(char *filename, int gtaskid, slurmd_job_t *job)
{
	int id;

	if (filename == NULL)
		return NULL;

	id = fname_single_task_io(filename);

	if (id < 0)
		return fname_create(job, filename, gtaskid);
	if (id >= job->nprocs) {
		error("Task ID in filename is invalid");
		return NULL;
	}

	if (id == gtaskid)
		return NULL;
	else
		return xstrdup("/dev/null");
}

static void
_job_init_task_info(slurmd_job_t *job, uint32_t *gtid,
		    char *ifname, char *ofname, char *efname)
{
	int          i;
	char        *in, *out, *err;

	if (job->ntasks == 0) {
		error("User requested launch of zero tasks!");
		job->task = NULL;
		return;
	}

	job->task = (slurmd_task_info_t **) 
		xmalloc(job->ntasks * sizeof(slurmd_task_info_t *));

	for (i = 0; i < job->ntasks; i++){
		in = _expand_stdio_filename(ifname, gtid[i], job);
		out = _expand_stdio_filename(ofname, gtid[i], job);
		err = _expand_stdio_filename(efname, gtid[i], job);

		job->task[i] = task_info_create(i, gtid[i], in, out, err);

		if (job->multi_prog) {
			multi_prog_get_argv(job->argv[1], job->env, gtid[i],
					    &job->task[i]->argc,
					    &job->task[i]->argv);
		} else {
			job->task[i]->argc = job->argc;
			job->task[i]->argv = job->argv;
		}
	}
}

void
job_signal_tasks(slurmd_job_t *job, int signal)
{
	int n = job->ntasks;
	while (--n >= 0) {
		if ((job->task[n]->pid > (pid_t) 0)
		&&  (kill(job->task[n]->pid, signal) < 0)) {
			if (errno != ESRCH) {
				error("job %d.%d: kill task %d: %m", 
				      job->jobid, job->stepid, n);
			}
		}
	}
}

void 
job_destroy(slurmd_job_t *job)
{
	int i;

	_array_free(&job->env);
	_array_free(&job->argv);

	_pwd_destroy(job->pwd);

	for (i = 0; i < job->ntasks; i++)
		_task_info_destroy(job->task[i], job->multi_prog);
	list_destroy(job->sruns);
	xfree(job->envtp);
	xfree(job->node_name);
	xfree(job->task_prolog);
	xfree(job->task_epilog);
	xfree(job);
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


struct srun_info *
srun_info_create(slurm_cred_t cred, slurm_addr *resp_addr, slurm_addr *ioaddr)
{
	char             *data = NULL;
	int               len  = 0;
	struct srun_info *srun = xmalloc(sizeof(struct srun_info));
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

/* destructor for list routines */
static void
_srun_info_destructor(void *arg)
{
	struct srun_info *srun = (struct srun_info *)arg;
	srun_info_destroy(srun);
}

void
srun_info_destroy(struct srun_info *srun)
{
	xfree(srun->key);
	xfree(srun);
}

slurmd_task_info_t *
task_info_create(int taskid, int gtaskid,
		 char *ifname, char *ofname, char *efname)
{
	slurmd_task_info_t *t = xmalloc(sizeof(slurmd_task_info_t));

	xassert(taskid >= 0);
	xassert(gtaskid >= 0);

	slurm_mutex_init(&t->mutex);
	slurm_mutex_lock(&t->mutex);
	t->state       = SLURMD_TASK_INIT;
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
	t->estatus     = -1;
	t->in          = NULL;
	t->out         = NULL;
	t->err         = NULL;
	t->argc	       = 0;
	t->argv	       = NULL;
	slurm_mutex_unlock(&t->mutex);
	return t;
}


static void 
_task_info_destroy(slurmd_task_info_t *t, uint16_t multi_prog)
{
	slurm_mutex_lock(&t->mutex);
	slurm_mutex_unlock(&t->mutex);
	slurm_mutex_destroy(&t->mutex);
	if (multi_prog) {
		xfree(t->argv);
	} /* otherwise, t->argv is a pointer to job->argv */
	xfree(t);
}
