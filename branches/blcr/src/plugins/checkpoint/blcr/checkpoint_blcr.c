/*****************************************************************************\
 *  checkpoint_blcr.c - BLCR slurm checkpoint plugin.
 *  $Id: checkpoint_blcr.c 0001 2008-12-29 16:50:11Z hjcao $
 *****************************************************************************
 *  Derived from checkpoint_aix.c
 *  Copyright (C) 2007-2009 National University of Defense Technology, China.
 *  Written by Hongia Cao.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif
#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#define MAX_PATH_LEN 1024

struct check_job_info {
	uint16_t disabled;	/* counter, checkpointable only if zero */
	time_t   time_stamp;	/* begin or end checkpoint time */
	uint32_t error_code;
	char    *error_msg;
};

struct ckpt_req {
	uint32_t gid;
	uint32_t uid;
	uint32_t job_id;
	uint32_t step_id;
	time_t begin_time;
	uint16_t wait;
	char *image_dir;
	char *nodelist;
	uint16_t sig_done;
};

static void _send_sig(uint32_t job_id, uint32_t step_id, uint16_t signal, 
		      char *nodelist);
static void _send_sig(uint32_t job_id, uint32_t step_id,
		      uint16_t signal, char *nodelist);
static void *_ckpt_agent_thr(void *arg);
static void _ckpt_req_free(void *ptr);
static int _on_ckpt_complete(uint32_t group_id, uint32_t user_id,
			     uint32_t job_id, uint32_t step_id,
			     char *image_dir, uint32_t error_code);


/* path to shell scripts */
static char *scch_path = SLURM_PREFIX "/sbin/scch";
static char *cr_checkpoint_path = SLURM_PREFIX "/bin/cr_checkpoint.sh";
static char *cr_restart_path = SLURM_PREFIX "/bin/cr_restart.sh";

static uint32_t ckpt_agent_jobid = 0;
static uint16_t ckpt_agent_count = 0;
static pthread_mutex_t ckpt_agent_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ckpt_agent_cond = PTHREAD_COND_INITIALIZER;
/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "checkpoint" for SLURM checkpoint) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load checkpoint plugins if the plugin_type string has a 
 * prefix of "checkpoint/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the checkpoint API matures.
 */
const char plugin_name[]       	= "BLCR checkpoint plugin";
const char plugin_type[]       	= "checkpoint/blcr";
const uint32_t plugin_version	= 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	info("checkpoint/blcr init");
	return SLURM_SUCCESS;
}


extern int fini ( void )
{
	info("checkpoint/blcr fini");
	return SLURM_ERROR;
}

/*
 * The remainder of this file implements the standard SLURM checkpoint API.
 */
extern int slurm_ckpt_op (uint32_t job_id, uint32_t step_id, uint16_t op,
			  uint16_t data, char *image_dir, time_t * event_time, 
			  uint32_t *error_code, char **error_msg )
{
	int rc = SLURM_SUCCESS;
	struct check_job_info *check_ptr;
	uint16_t done_sig = 0;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	struct node_record *node_ptr;
	pthread_attr_t attr;
	pthread_t ckpt_agent_tid = 0;
	char *nodelist;
	struct ckpt_req *req_ptr;

        /* job/step checked already */
        job_ptr = find_job_record(job_id);
	if (!job_ptr)
		return ESLURM_INVALID_JOB_ID;
        if (step_id == SLURM_BATCH_SCRIPT) {
                check_ptr = (struct check_job_info *)job_ptr->check_job;
                node_ptr = find_first_node_record(job_ptr->node_bitmap);
                nodelist = node_ptr->name;
        } else {
                step_ptr = find_step_record(job_ptr, step_id);
		if (!step_ptr)
			return ESLURM_INVALID_JOB_ID;
                check_ptr = (struct check_job_info *)step_ptr->check_job;
                nodelist = step_ptr->step_layout->node_list;
        }
	xassert(check_ptr);

	switch (op) {
	case CHECK_ABLE:
		if (check_ptr->disabled)
			rc = ESLURM_DISABLED;
		else {
			*event_time = check_ptr->time_stamp;
			rc = SLURM_SUCCESS;
		}
		break;
	case CHECK_DISABLE:
		check_ptr->disabled++;
		break;
	case CHECK_ENABLE:
		check_ptr->disabled--;
		break;
	case CHECK_VACATE:
		done_sig = SIGTERM;
		/* no break */
	case CHECK_CREATE:
		if (check_ptr->disabled) {
			rc = ESLURM_DISABLED;
			break;
		}
		if (check_ptr->time_stamp != 0) {
			rc = EALREADY;
			break;
		}
			
		check_ptr->time_stamp = time(NULL);
		check_ptr->error_code = 0;
		xfree(check_ptr->error_msg);

		req_ptr = xmalloc(sizeof(struct ckpt_req));
		if (!req_ptr) {
			rc = ENOMEM;
			break;
		}
		req_ptr->gid = job_ptr->group_id;
		req_ptr->uid = job_ptr->user_id;
		req_ptr->job_id = job_id;
		req_ptr->step_id = step_id;
		req_ptr->begin_time = check_ptr->time_stamp;
		req_ptr->wait = data;
		req_ptr->image_dir = xstrdup(image_dir);
		req_ptr->nodelist = xstrdup(nodelist);
		req_ptr->sig_done = done_sig;

		slurm_attr_init(&attr);
		if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
			error("pthread_attr_setdetachstate: %m");
			rc = errno;
			break;
		}
		
		if (pthread_create(&ckpt_agent_tid, &attr, _ckpt_agent_thr, 
				   req_ptr)) {
			error("pthread_create: %m");
			rc = errno;
			break;
		}
		slurm_attr_destroy(&attr);

		break;
			
	case CHECK_RESTART:
		if (step_id != SLURM_BATCH_SCRIPT) {
			rc = ESLURM_NOT_SUPPORTED;
			break;
		}
		/* create a batch job from saved desc */
		rc = ESLURM_NOT_SUPPORTED;
		/* TODO: save job script */
		break;
			
	case CHECK_ERROR:
		xassert(error_code);
		xassert(error_msg);
		*error_code = check_ptr->error_code;
		xfree(*error_msg);
		*error_msg = xstrdup(check_ptr->error_msg);
		break;
	default:
		error("Invalid checkpoint operation: %d", op);
		rc = EINVAL;
	}

	return rc;
}

extern int slurm_ckpt_comp ( struct step_record * step_ptr, time_t event_time,
		uint32_t error_code, char *error_msg )
{
	error("checkpoint/blcr: slurm_ckpt_comp not implemented");
	return SLURM_FAILURE; 
}

extern int slurm_ckpt_task_comp ( struct step_record * step_ptr, uint32_t task_id,
				  time_t event_time, uint32_t error_code, char *error_msg )
{
	error("checkpoint/blcr: slurm_ckpt_task_comp not implemented");
	return SLURM_FAILURE; 
}

extern int slurm_ckpt_alloc_job(check_jobinfo_t *jobinfo)
{
	struct check_job_info *check_ptr;
	check_ptr = xmalloc(sizeof(struct check_job_info));
	*jobinfo = (check_jobinfo_t) check_ptr;
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_free_job(check_jobinfo_t jobinfo)
{
	struct check_job_info *check_ptr = (struct check_job_info *)jobinfo;
	if (check_ptr) {
		xfree (check_ptr->error_msg);
		xfree(check_ptr);
	}
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_pack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	struct check_job_info *check_ptr = 
		(struct check_job_info *)jobinfo;
 
	pack16(check_ptr->disabled, buffer);
	pack_time(check_ptr->time_stamp, buffer);
	pack32(check_ptr->error_code, buffer);
	packstr(check_ptr->error_msg, buffer);

	return SLURM_SUCCESS;
}

extern int slurm_ckpt_unpack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	uint32_t uint32_tmp;
	struct check_job_info *check_ptr =
		(struct check_job_info *)jobinfo;

	safe_unpack16(&check_ptr->disabled, buffer);
	safe_unpack_time(&check_ptr->time_stamp, buffer);
	safe_unpack32(&check_ptr->error_code, buffer);
	safe_unpackstr_xmalloc(&check_ptr->error_msg, &uint32_tmp, buffer);
	
	return SLURM_SUCCESS; 

    unpack_error:
	xfree(check_ptr->error_msg);
	return SLURM_ERROR;
}

extern int slurm_ckpt_stepd_prefork(slurmd_job_t *job)
{
	char *old_env = NULL, *new_env = NULL, *ptr = NULL, *save_ptr = NULL;
	
        /*
	 * I was thinking that a thread can be created here to
	 * communicate with the tasks via sockets/pipes.
	 * Maybe this is not needed - we can modify MVAPICH2
	 */

	/* set LD_PRELOAD for batch script shell */
	//if (job->batch) {
		old_env = getenvp(job->env, "LD_PRELOAD");
		if (old_env) {
			/* search and replace all libcr_run and libcr_omit */
			/* the old env value is messed up -- it will be replaced */
			while ((ptr = strtok_r(old_env, " :", &save_ptr))) {
				old_env = NULL;
				if (!ptr)
					break;
				if (!strncmp(ptr, "libcr_run.so", 12) ||
				    !strncmp(ptr, "libcr_omit.so", 13))
					continue;
				xstrcat(new_env, ptr);
				xstrcat(new_env, ":");
			}
		}
		ptr = xstrdup("libcr_run.so");
		if (new_env)
			xstrfmtcat(ptr, ":%s", new_env);
		setenvf(&job->env, "LD_PRELOAD", ptr);
		xfree(new_env);
		xfree(ptr);
		//}
        return SLURM_SUCCESS;
}

extern int slurm_ckpt_signal_tasks(slurmd_job_t *job, char *image_dir)
{
        char *argv[4];
        char context_file[MAX_PATH_LEN];
        char pid[16];
        int status;
        pid_t *children = NULL;
        int *fd = NULL;
        int rc = SLURM_SUCCESS;
        int i;
        char c;

	debug3("checkpoint/blcr: slurm_ckpt_signal_tasks: image_dir=%s", image_dir);
        /*
         * the tasks must be checkpointed concurrently.
         */
        children = xmalloc(sizeof(pid_t) * job->ntasks);
        fd = xmalloc(sizeof(int) * 2 * job->ntasks);
        if (!children || !fd) {
                error("slurm_ckpt_signal_tasks: memory exhausted");
                rc = SLURM_FAILURE;
                goto out;
        }
        for (i = 0; i < job->ntasks; i ++) {
                fd[i*2] = -1;
                fd[i*2+1] = -1;
        }

        for (i = 0; i < job->ntasks; i ++) {
                if (job->batch) {
                        sprintf(context_file, "%s/script.ckpt", image_dir);
                } else {
                        sprintf(context_file, "%s/task.%d.ckpt",
                                image_dir, job->task[i]->gtid);
                }
                sprintf(pid, "%u", (unsigned int)job->task[i]->pid);

                if (pipe(&fd[i*2]) < 0) {
                        error("failed to create pipes: %m");
                        rc = SLURM_ERROR;
                        goto out_wait;
                }

                children[i] = fork();
                if (children[i] < 0) {
                        error("error forking cr_checkpoint");
                        rc = SLURM_ERROR;
                        goto out_wait;
                } else if (children[i] == 0) {
                        close(fd[i*2+1]);

                        while(read(fd[i*2], &c, 1) < 0 && errno == EINTR);
                        if (c)
                                exit(-1);

			/* change cred to job owner */
			if (setgid(job->gid) < 0) {
				error ("checkpoint/blcr: slurm_ckpt_signal_tasks: "
				       "failed to setgid: %m");
				exit(errno);
			}
			if (setuid(job->uid) < 0) {
				error ("checkpoint/blcr: slurm_ckpt_signal_tasks: "
				       "failed to setuid: %m");
				exit(errno);
			}
			if (chdir(job->cwd) < 0) {
				error ("checkpoint/blcr: slurm_ckpt_signal_tasks: "
				       "failed to chdir: %m");
				exit(errno);
			}
			
                        argv[0] = cr_checkpoint_path;
                        argv[1] = pid;
                        argv[2] = context_file;
                        argv[3] = NULL;

                        execv(argv[0], argv);
                        exit(errno);
                }
                close(fd[i*2]);
        }

 out_wait:
        c = (rc == SLURM_SUCCESS) ? 0 : 1;
        for (i = 0; i < job->ntasks; i ++) {
                if (fd[i*2+1] >= 0) {
                        while(write(fd[i*2+1], &c, 1) < 0 && errno == EINTR);
                }
        }
        /* wait children in sequence is OK */
        for (i = 0; i < job->ntasks; i ++) {
                if (children[i] == 0)
                        continue;
                while(waitpid(children[i], &status, 0) < 0 && errno == EINTR);
                if (! (WIFEXITED(status) && WEXITSTATUS(status))== 0)
                        rc = SLURM_ERROR;
        }
 out:
        xfree(children);
        xfree(fd);

        return rc;
}

extern int slurm_ckpt_restart_task(slurmd_job_t *job, char *image_dir, int gtid)
{
        char *argv[3];
        char context_file[MAX_PATH_LEN];

        /* jobid and stepid must NOT be spelled here, since it is a new job/step */
        if (job->batch) {
                sprintf(context_file, "%s/script.ckpt", image_dir);
        } else {
                sprintf(context_file, "%s/task.%d.ckpt", image_dir, gtid);
        }

        argv[0] = cr_restart_path;
        argv[1] = context_file;
        argv[2] = NULL;

        execv(argv[0], argv);

	/* Should only reach here if execv() fails */
	return SLURM_ERROR;
}


/* Send a signal RPC to a list of nodes */
static void _send_sig(uint32_t job_id, uint32_t step_id, uint16_t signal, 
		      char *nodelist)
{
	agent_arg_t *agent_args;
	kill_tasks_msg_t *kill_tasks_msg;

	kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
	kill_tasks_msg->job_id		= job_id;
	kill_tasks_msg->job_step_id	= step_id;
	kill_tasks_msg->signal		= signal;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type		= REQUEST_SIGNAL_TASKS;
	agent_args->retry		= 1;
	agent_args->msg_args		= kill_tasks_msg;
	agent_args->hostlist            = hostlist_create(nodelist);
	agent_args->node_count		= hostlist_count(agent_args->hostlist);

	agent_queue_request(agent_args);
}


/* Checkpoint processing pthread
 * Never returns, but is cancelled on plugin termiantion */
static void *_ckpt_agent_thr(void *arg)
{
	struct ckpt_req *req = (struct ckpt_req *)arg;
	int rc;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	struct check_job_info *check_ptr;

	/* only perform ckpt operation of ONE JOB */
	slurm_mutex_lock(&ckpt_agent_mutex);
	while (ckpt_agent_jobid && ckpt_agent_jobid != req->job_id) {
		pthread_cond_wait(&ckpt_agent_cond, &ckpt_agent_mutex);
	}
	ckpt_agent_jobid = req->job_id;
	ckpt_agent_count ++;
	slurm_mutex_unlock(&ckpt_agent_mutex);

	debug3("checkpoint/blcr: sending checkpoint tasks request to %u.%hu",
	       req->job_id, req->step_id);
	
	rc = checkpoint_tasks(req->job_id, req->step_id, req->begin_time,
			      req->image_dir, req->wait, req->nodelist);

	lock_slurmctld(job_write_lock);
	
	job_ptr = find_job_record(req->job_id);
	if (!job_ptr) {
		error("_ckpt_agent_thr: job finished");
		goto out;
	}
	if (req->step_id == SLURM_BATCH_SCRIPT) {	/* batch job */
		check_ptr = (struct check_job_info *)job_ptr->check_job;
	} else {
		step_ptr = find_step_record(job_ptr, req->step_id);
		if (! step_ptr) {
			error("_ckpt_agent_thr: step finished");
			goto out;
		}
		check_ptr = (struct check_job_info *)step_ptr->check_job;
	}
	check_ptr->time_stamp = 0;
	check_ptr->error_code = rc;
	if (check_ptr->error_code != SLURM_SUCCESS)
		check_ptr->error_msg = xstrdup(slurm_strerror(rc));

 out:
	unlock_slurmctld(job_write_lock);
		
	if (req->sig_done) {
		_send_sig(req->job_id, req->step_id, req->sig_done, 
			  req->nodelist);
	}

	_on_ckpt_complete(req->gid, req->uid, req->job_id, req->step_id, 
			  req->image_dir, rc);

	slurm_mutex_lock(&ckpt_agent_mutex);
	ckpt_agent_count --;
	if (ckpt_agent_count == 0) {
		ckpt_agent_jobid = 0;
		pthread_cond_broadcast(&ckpt_agent_cond);
	}
	slurm_mutex_unlock(&ckpt_agent_mutex);
	_ckpt_req_free(req);
	return NULL;
}


static void _ckpt_req_free(void *ptr)
{
	struct ckpt_req *req = (struct ckpt_req *)ptr;
	
	if (req) {
		xfree(req->image_dir);
		xfree(req->nodelist);
		xfree(req);
	}
}


/* a checkpoint completed, process the images files */
static int _on_ckpt_complete(uint32_t group_id, uint32_t user_id,
			     uint32_t job_id, uint32_t step_id,
			     char *image_dir, uint32_t error_code)
{
	int status;
	pid_t cpid;

	if (access(scch_path, R_OK | X_OK) < 0) {
		info("Access denied for %s: %m", scch_path);
		return SLURM_ERROR;
	}

	if ((cpid = fork()) < 0) {
		error ("_on_ckpt_complete: fork: %m");
		return SLURM_ERROR;
	}
	
	if (cpid == 0) {
		/*
		 * We don't fork and wait the child process because the job 
		 * read lock is held. It could take minutes to delete/move 
		 * the checkpoint image files. So there is a race condition
		 * of the user requesting another checkpoint before SCCH
		 * finishes.
		 */
		/* fork twice to avoid zombies */
		if ((cpid = fork()) < 0) {
			error ("_on_ckpt_complete: second fork: %m");
			exit(127);
		}
		/* grand child execs */
		if (cpid == 0) {
			char *args[6];
			char str_job[11];
			char str_step[11];
			char str_err[11];
		
			/*
			 * XXX: if slurmctld is running as root, we must setuid here.
			 * But what if slurmctld is running as SlurmUser?
			 * How about we make scch setuid and pass the user/group to it?
			 */
			if (geteuid() == 0) { /* root */
				if (setgid(group_id) < 0) {
					error ("_on_ckpt_complete: failed to "
						"setgid: %m");
					exit(127);
				}
				if (setuid(user_id) < 0) {
					error ("_on_ckpt_complete: failed to "
						"setuid: %m");
					exit(127);
				}
			}
			snprintf(str_job,  sizeof(str_job),  "%u", job_id);
			snprintf(str_step, sizeof(str_step), "%u", step_id);
			snprintf(str_err,  sizeof(str_err),  "%u", error_code);

			args[0] = scch_path;
			args[1] = str_job;
			args[2] = str_step;
			args[3] = str_err;
			args[4] = image_dir;
			args[5] = NULL;

			execv(scch_path, args);
			error("help! %m");
			exit(127);
		}
		/* child just exits */
		exit(0);
	}

	while(1) {
		if (waitpid(cpid, &status, 0) < 0 && errno == EINTR)
			continue;
		break;
	}

	return SLURM_SUCCESS;
}
