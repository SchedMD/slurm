/****************************************************************************\
 *  srun_job.c - job data structure creation functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/param.h>           /* MAXPATHLEN */
#include <grp.h>


#include "src/common/bitstring.h"
#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/plugstack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/io_hdr.h"
#include "src/common/forward.h"
#include "src/common/fd.h"
#include "src/common/uid.h"
#include "src/common/proc_args.h"

#include "src/api/step_launch.h"

#include "allocate.h"
#include "srun_job.h"
#include "opt.h"
#include "fname.h"
#include "debugger.h"
#include "launch.h"
#include "multi_prog.h"

/*
 * allocation information structure used to store general information
 * about node allocation to be passed to _job_create_structure()
 */
typedef struct allocation_info {
	char                   *alias_list;
	uint16_t               *cpus_per_node;
	uint32_t               *cpu_count_reps;
	uint32_t                jobid;
	uint32_t                nnodes;
	char                   *nodelist;
	uint32_t                num_cpu_groups;
	char                   *partition;
	dynamic_plugin_data_t  *select_jobinfo;
	uint32_t                stepid;
} allocation_info_t;

static int shepard_fd = -1;
static pthread_t signal_thread = (pthread_t) 0;
static int pty_sigarray[] = { SIGWINCH, 0 };

/*
 * Prototypes:
 */
static void       _set_ntasks(allocation_info_t *info);
static srun_job_t *_job_create_structure(allocation_info_t *info);
static char *     _normalize_hostlist(const char *hostlist);
static int _become_user(void);
static void _call_spank_fini(void);
static int  _call_spank_local_user(srun_job_t *job);
static void _default_sigaction(int sig);
static long _diff_tv_str(struct timeval *tv1, struct timeval *tv2);
static void _handle_intr(srun_job_t *job);
static void _handle_pipe(void);
static void _print_job_information(resource_allocation_response_msg_t *resp);
static void _run_srun_epilog (srun_job_t *job);
static void _run_srun_prolog (srun_job_t *job);
static int _run_srun_script (srun_job_t *job, char *script);
static void _set_env_vars(resource_allocation_response_msg_t *resp);
static void  _set_prio_process_env(void);
static int _set_rlimit_env(void);
static void _set_submit_dir_env(void);
static int _set_umask_env(void);
static void _shepard_notify(int shepard_fd);
static int _shepard_spawn(srun_job_t *job, bool got_alloc);
static void *_srun_signal_mgr(void *no_data);
static void _step_opt_exclusive(void);
static int _validate_relative(resource_allocation_response_msg_t *resp);


/*
 * Create an srun job structure w/out an allocation response msg.
 * (i.e. use the command line options)
 */
srun_job_t *
job_create_noalloc(void)
{
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(allocation_info_t));
	uint16_t cpn = 1;
	hostlist_t  hl = hostlist_create(opt.nodelist);

	if (!hl) {
		error("Invalid node list `%s' specified", opt.nodelist);
		goto error;
	}
	srand48(getpid());
	ai->jobid          = MIN_NOALLOC_JOBID +
		((uint32_t) lrand48() %
		 (MAX_NOALLOC_JOBID - MIN_NOALLOC_JOBID + 1));
	ai->stepid         = (uint32_t) (lrand48());
	ai->nodelist       = opt.nodelist;
	ai->nnodes         = hostlist_count(hl);

	hostlist_destroy(hl);

	cpn = (opt.ntasks + ai->nnodes - 1) / ai->nnodes;
	ai->cpus_per_node  = &cpn;
	ai->cpu_count_reps = &ai->nnodes;

	/*
	 * Create job, then fill in host addresses
	 */
	job = _job_create_structure(ai);

	if (job != NULL)
		job_update_io_fnames(job);

error:
	xfree(ai);
	return (job);

}

/*
 * Create an srun job structure for a step w/out an allocation response msg.
 * (i.e. inside an allocation)
 */
srun_job_t *
job_step_create_allocation(resource_allocation_response_msg_t *resp)
{
	uint32_t job_id = resp->job_id;
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(allocation_info_t));
	hostlist_t hl = NULL;
	char *buf = NULL;
	int count = 0;
	uint32_t alloc_count = 0;
	char *step_nodelist = NULL;

	ai->jobid          = job_id;
	ai->stepid         = NO_VAL;
	ai->alias_list     = resp->alias_list;
	ai->nodelist       = opt.alloc_nodelist;
	hl = hostlist_create(ai->nodelist);
	hostlist_uniq(hl);
	alloc_count = hostlist_count(hl);
	ai->nnodes = alloc_count;
	hostlist_destroy(hl);

	if (opt.exc_nodes) {
		hostlist_t exc_hl = hostlist_create(opt.exc_nodes);
		hostlist_t inc_hl = NULL;
		char *node_name = NULL;

		hl = hostlist_create(ai->nodelist);
		if (opt.nodelist) {
			inc_hl = hostlist_create(opt.nodelist);
		}
		hostlist_uniq(hl);
		//info("using %s or %s", opt.nodelist, ai->nodelist);
		while ((node_name = hostlist_shift(exc_hl))) {
			int inx = hostlist_find(hl, node_name);
			if (inx >= 0) {
				debug("excluding node %s", node_name);
				hostlist_delete_nth(hl, inx);
				ai->nnodes--;	/* decrement node count */
			}
			if (inc_hl) {
				inx = hostlist_find(inc_hl, node_name);
				if (inx >= 0) {
					error("Requested node %s is also "
					      "in the excluded list.",
					      node_name);
					error("Job not submitted.");
					hostlist_destroy(exc_hl);
					hostlist_destroy(inc_hl);
					goto error;
				}
			}
			free(node_name);
		}
		hostlist_destroy(exc_hl);

		/* we need to set this here so if there are more nodes
		 * available than we requested we can set it
		 * straight. If there is no exclude list then we set
		 * the vars then.
		 */
		if (!opt.nodes_set) {
			/* we don't want to set the number of nodes =
			 * to the number of requested processes unless we
			 * know it is less than the number of nodes
			 * in the allocation
			 */
			if (opt.ntasks_set && (opt.ntasks < ai->nnodes))
				opt.min_nodes = opt.ntasks;
			else
				opt.min_nodes = ai->nnodes;
			opt.nodes_set = true;
		}
		if (!opt.max_nodes)
			opt.max_nodes = opt.min_nodes;
		if ((opt.max_nodes > 0) && (opt.max_nodes < ai->nnodes))
			ai->nnodes = opt.max_nodes;

		count = hostlist_count(hl);
		if (!count) {
			error("Hostlist is now nothing!  Can't run job.");
			hostlist_destroy(hl);
			goto error;
		}
		if (inc_hl) {
			count = hostlist_count(inc_hl);
			if (count < ai->nnodes) {
				/* add more nodes to get correct number for
				   allocation */
				hostlist_t tmp_hl = hostlist_copy(hl);
				int i=0;
				int diff = ai->nnodes - count;
				buf = hostlist_ranged_string_xmalloc(inc_hl);
				hostlist_delete(tmp_hl, buf);
				xfree(buf);
				while ((node_name = hostlist_shift(tmp_hl)) &&
				       (i < diff)) {
					hostlist_push_host(inc_hl, node_name);
					i++;
				}
				hostlist_destroy(tmp_hl);
			}
			buf = hostlist_ranged_string_xmalloc(inc_hl);
			hostlist_destroy(inc_hl);
			xfree(opt.nodelist);
			opt.nodelist = buf;
		} else {
			if (count > ai->nnodes) {
				/* remove more nodes than needed for
				 * allocation */
				int i;
				for (i = count; i >= ai->nnodes; i--)
					hostlist_delete_nth(hl, i);
			}
			xfree(opt.nodelist);
			opt.nodelist = hostlist_ranged_string_xmalloc(hl);
		}

		hostlist_destroy(hl);
	} else {
		if (!opt.nodes_set) {
			/* we don't want to set the number of nodes =
			 * to the number of requested processes unless we
			 * know it is less than the number of nodes
			 * in the allocation
			 */
			if (opt.ntasks_set && (opt.ntasks < ai->nnodes))
				opt.min_nodes = opt.ntasks;
			else
				opt.min_nodes = ai->nnodes;
			opt.nodes_set = true;
		}
		if (!opt.max_nodes)
			opt.max_nodes = opt.min_nodes;
		if ((opt.max_nodes > 0) && (opt.max_nodes < ai->nnodes))
			ai->nnodes = opt.max_nodes;
		/* Don't reset the ai->nodelist because that is the
		 * nodelist we want to say the allocation is under
		 * opt.nodelist is what is used for the allocation.
		 */
		/* xfree(ai->nodelist); */
		/* ai->nodelist = xstrdup(buf); */
	}

	/* get the correct number of hosts to run tasks on */
	if (opt.nodelist)
		step_nodelist = opt.nodelist;
	else if ((opt.distribution == SLURM_DIST_ARBITRARY) && (count == 0))
		step_nodelist = getenv("SLURM_ARBITRARY_NODELIST");
	if (step_nodelist) {
		hl = hostlist_create(step_nodelist);
		if (opt.distribution != SLURM_DIST_ARBITRARY)
			hostlist_uniq(hl);
		if (!hostlist_count(hl)) {
			error("Hostlist is now nothing!  Can not run job.");
			hostlist_destroy(hl);
			goto error;
		}

		buf = hostlist_ranged_string_xmalloc(hl);
		count = hostlist_count(hl);
		hostlist_destroy(hl);
		/* Don't reset the ai->nodelist because that is the
		 * nodelist we want to say the allocation is under
		 * opt.nodelist is what is used for the allocation.
		 */
		/* xfree(ai->nodelist); */
		/* ai->nodelist = xstrdup(buf); */
		xfree(opt.nodelist);
		opt.nodelist = buf;
	}

	if ((opt.distribution == SLURM_DIST_ARBITRARY) &&
	    (count != opt.ntasks)) {
		error("You asked for %d tasks but hostlist specified %d nodes",
		      opt.ntasks, count);
		goto error;
	}

	if (ai->nnodes == 0) {
		error("No nodes in allocation, can't run job");
		goto error;
	}

	ai->num_cpu_groups = resp->num_cpu_groups;
	ai->cpus_per_node  = resp->cpus_per_node;
	ai->cpu_count_reps = resp->cpu_count_reps;
	ai->partition = resp->partition;

/* 	info("looking for %d nodes out of %s with a must list of %s", */
/* 	     ai->nnodes, ai->nodelist, opt.nodelist); */
	/*
	 * Create job
	 */
	job = _job_create_structure(ai);
error:
   	xfree(ai);
	return (job);

}

/*
 * Create an srun job structure from a resource allocation response msg
 */
extern srun_job_t *
job_create_allocation(resource_allocation_response_msg_t *resp)
{
	srun_job_t *job;
	allocation_info_t *i = xmalloc(sizeof(allocation_info_t));

	i->alias_list     = resp->alias_list;
	i->nodelist       = _normalize_hostlist(resp->node_list);
	i->nnodes	  = resp->node_cnt;
	i->partition      = resp->partition;
	i->jobid          = resp->job_id;
	i->stepid         = NO_VAL;
	i->num_cpu_groups = resp->num_cpu_groups;
	i->cpus_per_node  = resp->cpus_per_node;
	i->cpu_count_reps = resp->cpu_count_reps;
	i->select_jobinfo = select_g_select_jobinfo_copy(resp->select_jobinfo);

	job = _job_create_structure(i);

	xfree(i->nodelist);
	xfree(i);

	return (job);
}

extern void init_srun(int ac, char **av,
		      log_options_t *logopt, int debug_level,
		      bool handle_signals)
{
	/* This must happen before we spawn any threads
	 * which are not designed to handle arbitrary signals */
	if (handle_signals) {
		if (xsignal_block(sig_array) < 0)
			error("Unable to block signals");
	}
	xsignal_block(pty_sigarray);

	/* Initialize plugin stack, read options from plugins, etc.
	 */
	init_spank_env();
	if (spank_init(NULL) < 0) {
		error("Plug-in initialization failed");
		exit(error_exit);
	}

	/* Be sure to call spank_fini when srun exits.
	 */
	if (atexit(_call_spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	if (initialize_and_process_args(ac, av) < 0) {
		error ("srun initialization failed");
		exit (1);
	}
	record_ppid();

	if (spank_init_post_opt() < 0) {
		error("Plugin stack post-option processing failed.");
		exit(error_exit);
	}

	/* reinit log with new verbosity (if changed by command line)
	 */
	if (logopt && (_verbose || opt.quiet)) {
		/* If log level is already increased, only increment the
		 *   level to the difference of _verbose an LOG_LEVEL_INFO
		 */
		if ((_verbose -= (logopt->stderr_level - LOG_LEVEL_INFO)) > 0)
			logopt->stderr_level += _verbose;
		logopt->stderr_level -= opt.quiet;
		logopt->prefix_level = 1;
		log_alter(*logopt, 0, NULL);
	} else
		_verbose = debug_level;

	(void) _set_rlimit_env();
	_set_prio_process_env();
	(void) _set_umask_env();
	_set_submit_dir_env();

	/* Set up slurmctld message handler */
	slurmctld_msg_init();
}

extern void create_srun_job(srun_job_t **p_job, bool *got_alloc,
			    bool slurm_started, bool handle_signals)
{
	resource_allocation_response_msg_t *resp;
	srun_job_t *job = NULL;

	/* now global "opt" should be filled in and available,
	 * create a job from opt
	 */
	if (opt.test_only) {
		int rc = allocate_test();
		if (rc) {
			slurm_perror("allocation failure");
			exit (1);
		}
		exit (0);

	} else if (opt.no_alloc) {
		info("do not allocate resources");
		job = job_create_noalloc();
		if (job == NULL) {
			error("Job creation failure.");
			exit(error_exit);
		}
		if (create_job_step(job, false) < 0) {
			exit(error_exit);
		}
	} else if ((resp = existing_allocation())) {
		select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET,
					&resp->node_cnt);
		if (opt.nodes_set_env && !opt.nodes_set_opt &&
		    (opt.min_nodes > resp->node_cnt)) {
			/* This signifies the job used the --no-kill option
			 * and a node went DOWN or it used a node count range
			 * specification, was checkpointed from one size and
			 * restarted at a different size */
			error("SLURM_NNODES environment variable "
			      "conflicts with allocated node count (%u!=%u).",
			      opt.min_nodes, resp->node_cnt);
			/* Modify options to match resource allocation.
			 * NOTE: Some options are not supported */
			opt.min_nodes = resp->node_cnt;
			xfree(opt.alloc_nodelist);
			if (!opt.ntasks_set)
				opt.ntasks = opt.min_nodes;
		}
		if (opt.core_spec_set) {
			/* NOTE: Silently ignore specialized core count set
			 * with SLURM_CORE_SPEC environment variable */
			error("Ignoring --core-spec value for a job step "
			      "within an existing job. Set specialized cores "
			      "at job allocation time.");
		}
#ifdef HAVE_NATIVE_CRAY
		if (opt.network) {
			if (opt.network_set_env)
				debug2("Ignoring SLURM_NETWORK value for a "
				       "job step within an existing job. "
				       "Using what was set at job "
				       "allocation time.  Most likely this "
				       "variable was set by sbatch or salloc.");
			else
				error("Ignoring --network value for a job step "
				      "within an existing job. Set network "
				      "options at job allocation time.");
		}
#endif
		if (opt.alloc_nodelist == NULL)
			opt.alloc_nodelist = xstrdup(resp->node_list);
		if (opt.exclusive)
			_step_opt_exclusive();
		_set_env_vars(resp);
		if (_validate_relative(resp))
			exit(error_exit);
		job = job_step_create_allocation(resp);
		slurm_free_resource_allocation_response_msg(resp);

		if (opt.begin != 0) {
			error("--begin is ignored because nodes"
			      " are already allocated.");
		}
		if (!job || create_job_step(job, false) < 0)
			exit(error_exit);
	} else {
		/* Combined job allocation and job step launch */
#if defined HAVE_FRONT_END && (!defined HAVE_BG || defined HAVE_BG_L_P || !defined HAVE_BG_FILES) && (!defined HAVE_REAL_CRAY)
		uid_t my_uid = getuid();
		if ((my_uid != 0) &&
		    (my_uid != slurm_get_slurm_user_id())) {
			error("srun task launch not supported on this system");
			exit(error_exit);
		}
#endif
		if (opt.relative_set && opt.relative) {
			fatal("--relative option invalid for job allocation "
			      "request");
		}

		if (!opt.job_name_set_env && opt.job_name_set_cmd)
			setenvfs("SLURM_JOB_NAME=%s", opt.job_name);
		else if (!opt.job_name_set_env && opt.argc)
			setenvfs("SLURM_JOB_NAME=%s", opt.argv[0]);

		if ( !(resp = allocate_nodes(handle_signals)) )
			exit(error_exit);
		global_resp = resp;
		*got_alloc = true;
		_print_job_information(resp);
		_set_env_vars(resp);
		if (_validate_relative(resp)) {
			slurm_complete_job(resp->job_id, 1);
			exit(error_exit);
		}
		job = job_create_allocation(resp);

		opt.time_limit = NO_VAL;/* not applicable for step, only job */
		xfree(opt.constraints);	/* not applicable for this step */
		if (!opt.job_name_set_cmd && opt.job_name_set_env) {
			/* use SLURM_JOB_NAME env var */
			opt.job_name_set_cmd = true;
		}
		if ((opt.core_spec_set || opt.exclusive) && opt.cpus_set) {
			/* Step gets specified CPU count, which may only part
			 * of the job allocation. */
			opt.exclusive = true;
		} else {
			/* Step gets all CPUs in the job allocation. */
			opt.exclusive = false;
		}

		/*
		 *  Become --uid user
		 */
		if (_become_user () < 0)
			info("Warning: Unable to assume uid=%u", opt.uid);

		if (!job || create_job_step(job, true) < 0) {
			slurm_complete_job(resp->job_id, 1);
			exit(error_exit);
		}

		global_resp = NULL;
		slurm_free_resource_allocation_response_msg(resp);
	}

	/*
	 *  Become --uid user
	 */
	if (_become_user () < 0)
		info("Warning: Unable to assume uid=%u", opt.uid);

	if (!slurm_started) {
		/*
		 * Spawn process to insure clean-up of job and/or step
		 * on abnormal termination
		 */
		shepard_fd = _shepard_spawn(job, *got_alloc);
	}

	*p_job = job;
}

extern void pre_launch_srun_job(srun_job_t *job, bool slurm_started,
				bool handle_signals)
{
	pthread_attr_t thread_attr;

	if (handle_signals && !signal_thread) {
		slurm_attr_init(&thread_attr);
		while (pthread_create(&signal_thread, &thread_attr,
				      _srun_signal_mgr, job)) {
			error("pthread_create error %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);
	}

	/* if running from poe This already happened in srun. */
	if (slurm_started)
		return;

	_run_srun_prolog(job);
	if (_call_spank_local_user (job) < 0) {
		error("Failure in local plugin stack");
		slurm_step_launch_abort(job->step_ctx);
		exit(error_exit);
	}
}

extern void fini_srun(srun_job_t *job, bool got_alloc, uint32_t *global_rc,
		      bool slurm_started)
{
	/* If running from poe, most of this already happened in srun. */
	if (slurm_started)
		goto cleanup;
	if (got_alloc) {
		cleanup_allocation();

		/* send the controller we were cancelled */
		if (job->state >= SRUN_JOB_CANCELLED)
			slurm_complete_job(job->jobid, NO_VAL);
		else
			slurm_complete_job(job->jobid, *global_rc);
	}
	_shepard_notify(shepard_fd);

cleanup:
	if (signal_thread) {
		srun_shutdown = true;
		pthread_kill(signal_thread, SIGINT);
		pthread_join(signal_thread,  NULL);
	}

	if (!slurm_started)
		_run_srun_epilog(job);

	slurm_step_ctx_destroy(job->step_ctx);

	if (WIFEXITED(*global_rc))
		*global_rc = WEXITSTATUS(*global_rc);
	else if (WIFSIGNALED(*global_rc))
		*global_rc = 128 + WTERMSIG(*global_rc);

	mpir_cleanup();
	log_fini();
}

void
update_job_state(srun_job_t *job, srun_job_state_t state)
{
	pthread_mutex_lock(&job->state_mutex);
	if (job->state < state) {
		job->state = state;
		pthread_cond_signal(&job->state_cond);

	}
	pthread_mutex_unlock(&job->state_mutex);
	return;
}

srun_job_state_t
job_state(srun_job_t *job)
{
	srun_job_state_t state;
	slurm_mutex_lock(&job->state_mutex);
	state = job->state;
	slurm_mutex_unlock(&job->state_mutex);
	return state;
}


void
job_force_termination(srun_job_t *job)
{
	static int kill_sent = 0;
	static time_t last_msg = 0;

	if (kill_sent == 0) {
		info("forcing job termination");
		/* Sends SIGKILL to tasks directly */
		update_job_state(job, SRUN_JOB_FORCETERM);
	} else {
		time_t now = time(NULL);
		if (last_msg != now) {
			info("job abort in progress");
			last_msg = now;
		}
		if (kill_sent == 1) {
			/* Try sending SIGKILL through slurmctld */
			slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);
		}
	}
	kill_sent++;
}

static void
_set_ntasks(allocation_info_t *ai)
{
	int cnt = 0;

	if (opt.ntasks_set)
		return;

	if (opt.ntasks_per_node != NO_VAL) {
		cnt = ai->nnodes * opt.ntasks_per_node;
		opt.ntasks_set = true;	/* implicit */
	} else if (opt.cpus_set) {
		int i;

		for (i = 0; i < ai->num_cpu_groups; i++)
			cnt += (ai->cpu_count_reps[i] *
				(ai->cpus_per_node[i] / opt.cpus_per_task));
		opt.ntasks_set = true;	/* implicit */
	}

	opt.ntasks = (cnt < ai->nnodes) ? ai->nnodes : cnt;
}

/*
 * Create an srun job structure from a resource allocation response msg
 */
static srun_job_t *
_job_create_structure(allocation_info_t *ainfo)
{
	srun_job_t *job = xmalloc(sizeof(srun_job_t));
	int i;

	_set_ntasks(ainfo);
	debug2("creating job with %d tasks", opt.ntasks);

	slurm_mutex_init(&job->state_mutex);
	pthread_cond_init(&job->state_cond, NULL);
	job->state = SRUN_JOB_INIT;

 	job->alias_list = xstrdup(ainfo->alias_list);
 	job->nodelist = xstrdup(ainfo->nodelist);
 	job->partition = xstrdup(ainfo->partition);
	job->stepid  = ainfo->stepid;

#if defined HAVE_BG && !defined HAVE_BG_L_P
//#if defined HAVE_BGQ && defined HAVE_BG_FILES
	/* Since the allocation will have the correct cnode count get
	   it if it is available.  Else grab it from opt.min_nodes
	   (meaning the allocation happened before).
	*/
	if (ainfo->select_jobinfo)
		select_g_select_jobinfo_get(ainfo->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &job->nhosts);
	else
		job->nhosts   = opt.min_nodes;
	/* If we didn't ask for nodes set it up correctly here so the
	   step allocation does the correct thing.
	*/
	if (!opt.nodes_set) {
		opt.min_nodes = opt.max_nodes = job->nhosts;
		opt.nodes_set = true;
		opt.ntasks_per_node = NO_VAL;
		bg_figure_nodes_tasks(&opt.min_nodes, &opt.max_nodes,
				      &opt.ntasks_per_node, &opt.ntasks_set,
				      &opt.ntasks, opt.nodes_set,
				      opt.nodes_set_opt, opt.overcommit, 1);

#if defined HAVE_BG_FILES
		/* Replace the runjob line with correct information. */
		int i, matches = 0;
		for (i = 0; i < opt.argc; i++) {
			if (!strcmp(opt.argv[i], "-p")) {
				i++;
				xfree(opt.argv[i]);
				opt.argv[i]  = xstrdup_printf(
					"%d", opt.ntasks_per_node);
				matches++;
			} else if (!strcmp(opt.argv[i], "--np")) {
				i++;
				xfree(opt.argv[i]);
				opt.argv[i]  = xstrdup_printf(
					"%d", opt.ntasks);
				matches++;
			}
			if (matches == 2)
				break;
		}
		xassert(matches == 2);
#endif
	}

#elif defined HAVE_FRONT_END && !defined HAVE_ALPS_CRAY
	/* Limited job step support */
	opt.overcommit = true;
	job->nhosts = 1;
#else
	job->nhosts   = ainfo->nnodes;
#endif

#if !defined HAVE_FRONT_END || (defined HAVE_BGQ)
//#if !defined HAVE_FRONT_END || (defined HAVE_BGQ && defined HAVE_BG_FILES)
	if (opt.min_nodes > job->nhosts) {
		error("Only allocated %d nodes asked for %d",
		      job->nhosts, opt.min_nodes);
		if (opt.exc_nodes) {
			/* When resources are pre-allocated and some nodes
			 * are explicitly excluded, this error can occur. */
			error("Are required nodes explicitly excluded?");
		}
		xfree(job);
		return NULL;
	}
	if ((ainfo->cpus_per_node == NULL) ||
	    (ainfo->cpu_count_reps == NULL)) {
		error("cpus_per_node array is not set");
		xfree(job);
		return NULL;
	}
#endif
	job->select_jobinfo = ainfo->select_jobinfo;
	job->jobid   = ainfo->jobid;

	job->ntasks  = opt.ntasks;

	/* If cpus_per_task is set then get the exact count of cpus
	   for the requested step (we might very well use less,
	   especially if --exclusive is used).  Else get the total for the
	   allocation given.
	*/
	if (opt.cpus_set)
		job->cpu_count = opt.ntasks * opt.cpus_per_task;
	else {
		for (i=0; i<ainfo->num_cpu_groups; i++) {
			job->cpu_count += ainfo->cpus_per_node[i] *
				ainfo->cpu_count_reps[i];
		}
	}

	job->rc       = -1;

	job_update_io_fnames(job);

	return (job);
}

void
job_update_io_fnames(srun_job_t *job)
{
	job->ifname = fname_create(job, opt.ifname);
	job->ofname = fname_create(job, opt.ofname);
	job->efname = opt.efname ? fname_create(job, opt.efname) : job->ofname;
}

static char *
_normalize_hostlist(const char *hostlist)
{
	char *buf = NULL;
	hostlist_t hl = hostlist_create(hostlist);

	if (hl)	{
		buf = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
	}
	if (!buf)
		return xstrdup(hostlist);

	return buf;
}

static int _become_user (void)
{
	char *user = uid_to_string(opt.uid);
	gid_t gid = gid_from_uid(opt.uid);

	if (strcmp(user, "nobody") == 0) {
		xfree(user);
		return (error ("Invalid user id %u: %m", opt.uid));
	}

	if (opt.uid == getuid ()) {
		xfree(user);
		return (0);
	}

	if ((opt.egid != (gid_t) -1) && (setgid (opt.egid) < 0)) {
		xfree(user);
		return (error ("setgid: %m"));
	}

	initgroups (user, gid); /* Ignore errors */
	xfree(user);

	if (setuid (opt.uid) < 0)
		return (error ("setuid: %m"));

	return (0);
}

static int _call_spank_local_user (srun_job_t *job)
{
	struct spank_launcher_job_info info[1];

	info->uid = opt.uid;
	info->gid = opt.gid;
	info->jobid = job->jobid;
	info->stepid = job->stepid;
	info->step_layout = launch_common_get_slurm_step_layout(job);
	info->argc = opt.argc;
	info->argv = opt.argv;

	return spank_local_user(info);
}

static void _default_sigaction(int sig)
{
	struct sigaction act;
	if (sigaction(sig, NULL, &act)) {
		error("sigaction(%d): %m", sig);
		return;
	}
	if (act.sa_handler != SIG_IGN)
		return;

	act.sa_handler = SIG_DFL;
	if (sigaction(sig, &act, NULL))
		error("sigaction(%d): %m", sig);
}

/* Return the number of microseconds between tv1 and tv2 with a maximum
 * a maximum value of 10,000,000 to prevent overflows */
static long _diff_tv_str(struct timeval *tv1, struct timeval *tv2)
{
	long delta_t;

	delta_t  = MIN((tv2->tv_sec - tv1->tv_sec), 10);
	delta_t *= 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	return delta_t;
}

static void _handle_intr(srun_job_t *job)
{
	static struct timeval last_intr = { 0, 0 };
	static struct timeval last_intr_sent = { 0, 0 };
	struct timeval now;

	gettimeofday(&now, NULL);
	if (!opt.quit_on_intr && (_diff_tv_str(&last_intr, &now) > 1000000)) {
		if  (opt.disable_status) {
			info("sending Ctrl-C to job %u.%u",
			     job->jobid, job->stepid);
			launch_g_fwd_signal(SIGINT);
		} else if (job->state < SRUN_JOB_FORCETERM) {
			info("interrupt (one more within 1 sec to abort)");
			launch_g_print_status();
		} else {
			info("interrupt (abort already in progress)");
			launch_g_print_status();
		}
		last_intr = now;
	} else  { /* second Ctrl-C in half as many seconds */
		update_job_state(job, SRUN_JOB_CANCELLED);
		/* terminate job */
		if (job->state < SRUN_JOB_FORCETERM) {
			if (_diff_tv_str(&last_intr_sent, &now) < 1000000) {
				job_force_termination(job);
				launch_g_fwd_signal(SIGKILL);
				return;
			}

			info("sending Ctrl-C to job %u.%u",
			     job->jobid, job->stepid);
			last_intr_sent = now;
			launch_g_fwd_signal(SIGINT);
		} else
			job_force_termination(job);

		launch_g_fwd_signal(SIGKILL);
	}
}

static void _handle_pipe(void)
{
	static int ending = 0;

	if (ending)
		return;
	ending = 1;
	launch_g_fwd_signal(SIGKILL);
}


static void _print_job_information(resource_allocation_response_msg_t *resp)
{
	int i;
	char *str = NULL;
	char *sep = "";

	if (!_verbose)
		return;

	xstrfmtcat(str, "jobid %u: nodes(%u):`%s', cpu counts: ",
		   resp->job_id, resp->node_cnt, resp->node_list);

	for (i = 0; i < resp->num_cpu_groups; i++) {
		xstrfmtcat(str, "%s%u(x%u)",
			   sep, resp->cpus_per_node[i],
			   resp->cpu_count_reps[i]);
		sep = ",";
	}
	verbose("%s", str);
	xfree(str);
}

static void _run_srun_epilog (srun_job_t *job)
{
	int rc;

	if (opt.epilog && strcasecmp(opt.epilog, "none") != 0) {
		rc = _run_srun_script(job, opt.epilog);
		debug("srun epilog rc = %d", rc);
	}
}

static void _run_srun_prolog (srun_job_t *job)
{
	int rc;

	if (opt.prolog && strcasecmp(opt.prolog, "none") != 0) {
		rc = _run_srun_script(job, opt.prolog);
		debug("srun prolog rc = %d", rc);
	}
}

static int _run_srun_script (srun_job_t *job, char *script)
{
	int status;
	pid_t cpid;
	int i;
	char **args = NULL;

	if (script == NULL || script[0] == '\0')
		return 0;

	if (access(script, R_OK | X_OK) < 0) {
		info("Access denied for %s: %m", script);
		return 0;
	}

	if ((cpid = fork()) < 0) {
		error ("run_srun_script: fork: %m");
		return -1;
	}
	if (cpid == 0) {

		/* set the scripts command line arguments to the arguments
		 * for the application, but shifted one higher
		 */
		args = xmalloc(sizeof(char *) * 1024);
		args[0] = script;
		for (i = 0; i < opt.argc; i++) {
			args[i+1] = opt.argv[i];
		}
		args[i+1] = NULL;
		execv(script, args);
		error("help! %m");
		exit(127);
	}

	do {
		if (waitpid(cpid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid: %m");
			return 0;
		} else
			return status;
	} while(1);

	/* NOTREACHED */
}

static void _set_env_vars(resource_allocation_response_msg_t *resp)
{
	char *tmp;

	if (!getenv("SLURM_JOB_CPUS_PER_NODE")) {
		tmp = uint32_compressed_to_str(resp->num_cpu_groups,
					       resp->cpus_per_node,
					       resp->cpu_count_reps);
		if (setenvf(NULL, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp) < 0) {
			error("unable to set SLURM_JOB_CPUS_PER_NODE in "
			      "environment");
		}
		xfree(tmp);
	}

	if (resp->alias_list) {
		if (setenv("SLURM_NODE_ALIASES", resp->alias_list, 1) < 0) {
			error("unable to set SLURM_NODE_ALIASES in "
			      "environment");
		}
	} else {
		unsetenv("SLURM_NODE_ALIASES");
	}

	return;
}

/*
 * _set_prio_process_env
 *
 * Set the internal SLURM_PRIO_PROCESS environment variable to support
 * the propagation of the users nice value and the "PropagatePrioProcess"
 * config keyword.
 */
static void  _set_prio_process_env(void)
{
	int retval;

	errno = 0; /* needed to detect a real failure since prio can be -1 */

	if ((retval = getpriority (PRIO_PROCESS, 0)) == -1)  {
		if (errno) {
			error ("getpriority(PRIO_PROCESS): %m");
			return;
		}
	}

	if (setenvf (NULL, "SLURM_PRIO_PROCESS", "%d", retval) < 0) {
		error ("unable to set SLURM_PRIO_PROCESS in environment");
		return;
	}

	debug ("propagating SLURM_PRIO_PROCESS=%d", retval);
}

/* Set SLURM_RLIMIT_* environment variables with current resource
 * limit values, reset RLIMIT_NOFILE to maximum possible value */
static int _set_rlimit_env(void)
{
	int                  rc = SLURM_SUCCESS;
	struct rlimit        rlim[1];
	unsigned long        cur;
	char                 name[64], *format;
	slurm_rlimits_info_t *rli;

	/* Modify limits with any command-line options */
	if (opt.propagate && parse_rlimits( opt.propagate, PROPAGATE_RLIMITS)){
		error( "--propagate=%s is not valid.", opt.propagate );
		exit(error_exit);
	}

	for (rli = get_slurm_rlimits_info(); rli->name != NULL; rli++ ) {

		if (rli->propagate_flag != PROPAGATE_RLIMITS)
			continue;

		if (getrlimit (rli->resource, rlim) < 0) {
			error ("getrlimit (RLIMIT_%s): %m", rli->name);
			rc = SLURM_FAILURE;
			continue;
		}

		cur = (unsigned long) rlim->rlim_cur;
		snprintf(name, sizeof(name), "SLURM_RLIMIT_%s", rli->name);
		if (opt.propagate && rli->propagate_flag == PROPAGATE_RLIMITS)
			/*
			 * Prepend 'U' to indicate user requested propagate
			 */
			format = "U%lu";
		else
			format = "%lu";

		if (setenvf (NULL, name, format, cur) < 0) {
			error ("unable to set %s in environment", name);
			rc = SLURM_FAILURE;
			continue;
		}

		debug ("propagating RLIMIT_%s=%lu", rli->name, cur);
	}

	/*
	 *  Now increase NOFILE to the max available for this srun
	 */
	if (getrlimit (RLIMIT_NOFILE, rlim) < 0)
		return (error ("getrlimit (RLIMIT_NOFILE): %m"));

	if (rlim->rlim_cur < rlim->rlim_max) {
		rlim->rlim_cur = rlim->rlim_max;
		if (setrlimit (RLIMIT_NOFILE, rlim) < 0)
			return (error ("Unable to increase max no. files: %m"));
	}

	return rc;
}

/* Set SLURM_CLUSTER_NAME< SLURM_SUBMIT_DIR and SLURM_SUBMIT_HOST environment 
 * variables within current state */
static void _set_submit_dir_env(void)
{
	char buf[MAXPATHLEN + 1], host[256];
	char *cluster_name;

	cluster_name = slurm_get_cluster_name();
	if (cluster_name) {
		if (setenvf(NULL, "SLURM_CLUSTER_NAME", "%s", cluster_name) < 0)
			error("unable to set SLURM_CLUSTER_NAME in environment");
		xfree(cluster_name);
	}

	if ((getcwd(buf, MAXPATHLEN)) == NULL)
		error("getcwd failed: %m");
	else if (setenvf(NULL, "SLURM_SUBMIT_DIR", "%s", buf) < 0)
		error("unable to set SLURM_SUBMIT_DIR in environment");

	if ((gethostname(host, sizeof(host))))
		error("gethostname_short failed: %m");
	else if (setenvf(NULL, "SLURM_SUBMIT_HOST", "%s", host) < 0)
		error("unable to set SLURM_SUBMIT_HOST in environment");
}

/* Set some environment variables with current state */
static int _set_umask_env(void)
{
	if (!getenv("SRUN_DEBUG")) {	/* do not change current value */
		/* NOTE: Default debug level is 3 (info) */
		int log_level = LOG_LEVEL_INFO + _verbose - opt.quiet;

		if (setenvf(NULL, "SRUN_DEBUG", "%d", log_level) < 0)
			error ("unable to set SRUN_DEBUG in environment");
	}

	if (!getenv("SLURM_UMASK")) {	/* do not change current value */
		char mask_char[5];
		mode_t mask;

		mask = (int)umask(0);
		umask(mask);

		sprintf(mask_char, "0%d%d%d",
			((mask>>6)&07), ((mask>>3)&07), mask&07);
		if (setenvf(NULL, "SLURM_UMASK", "%s", mask_char) < 0) {
			error ("unable to set SLURM_UMASK in environment");
			return SLURM_FAILURE;
		}
		debug ("propagating UMASK=%s", mask_char);
	}

	return SLURM_SUCCESS;
}

static void _shepard_notify(int shepard_fd)
{
	int rc;

	while (1) {
		rc = write(shepard_fd, "", 1);
		if (rc == -1) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			error("write(shepard): %m");
		}
		break;
	}
	close(shepard_fd);
}

static int _shepard_spawn(srun_job_t *job, bool got_alloc)
{
	int shepard_pipe[2], rc;
	pid_t shepard_pid;
	char buf[1];

	if (pipe(shepard_pipe)) {
		error("pipe: %m");
		return -1;
	}

	shepard_pid = fork();
	if (shepard_pid == -1) {
		error("fork: %m");
		return -1;
	}
	if (shepard_pid != 0) {
		close(shepard_pipe[0]);
		return shepard_pipe[1];
	}

	/* Wait for parent to notify of completion or I/O error on abort */
	close(shepard_pipe[1]);
	while (1) {
		rc = read(shepard_pipe[0], buf, 1);
		if (rc == 1) {
			exit(0);
		} else if (rc == 0) {
			break;	/* EOF */
		} else if (rc == -1) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			break;
		}
	}

	(void) slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);

	if (got_alloc)
		slurm_complete_job(job->jobid, NO_VAL);
	exit(0);
	return -1;
}

/* _srun_signal_mgr - Process daemon-wide signals */
static void *_srun_signal_mgr(void *job_ptr)
{
	int sig;
	int i, rc;
	sigset_t set;
	srun_job_t *job = (srun_job_t *)job_ptr;

	/* Make sure no required signals are ignored (possibly inherited) */
	for (i = 0; sig_array[i]; i++)
		_default_sigaction(sig_array[i]);
	while (!srun_shutdown) {
		xsignal_sigset_create(sig_array, &set);
		rc = sigwait(&set, &sig);
		if (rc == EINTR)
			continue;
		switch (sig) {
		case SIGINT:
			if (!srun_shutdown)
				_handle_intr(job);
			break;
		case SIGQUIT:
			info("Quit");
			/* continue with slurm_step_launch_abort */
		case SIGTERM:
		case SIGHUP:
			/* No need to call job_force_termination here since we
			 * are ending the job now and we don't need to update
			 * the state. */
			info("forcing job termination");
			launch_g_fwd_signal(SIGKILL);
			break;
		case SIGCONT:
			info("got SIGCONT");
			break;
		case SIGPIPE:
			_handle_pipe();
			break;
		case SIGALRM:
			if (srun_max_timer) {
				info("First task exited %ds ago", opt.max_wait);
				launch_g_print_status();
				launch_g_step_terminate();
			}
			break;
		default:
			launch_g_fwd_signal(sig);
			break;
		}
	}
	return NULL;
}

/* opt.exclusive is set, disable user task layout controls */
static void _step_opt_exclusive(void)
{
	if (!opt.ntasks_set) {
		error("--ntasks must be set with --exclusive");
		exit(error_exit);
	}
	if (opt.relative_set) {
		error("--relative disabled, incompatible with --exclusive");
		exit(error_exit);
	}
	if (opt.exc_nodes) {
		error("--exclude is incompatible with --exclusive");
		exit(error_exit);
	}
}

static int _validate_relative(resource_allocation_response_msg_t *resp)
{

	if (opt.relative_set &&
	    ((opt.relative + opt.min_nodes) > resp->node_cnt)) {
		if (opt.nodes_set_opt) {  /* -N command line option used */
			error("--relative and --nodes option incompatible "
			      "with count of allocated nodes (%d+%d>%d)",
			      opt.relative, opt.min_nodes, resp->node_cnt);
		} else {		/* SLURM_NNODES option used */
			error("--relative and SLURM_NNODES option incompatible "
			      "with count of allocated nodes (%d+%d>%d)",
			      opt.relative, opt.min_nodes, resp->node_cnt);
		}
		return -1;
	}
	return 0;
}

static void _call_spank_fini(void)
{
	if (-1 != shepard_fd)
		spank_fini(NULL);
}
