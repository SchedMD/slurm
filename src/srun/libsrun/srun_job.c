/****************************************************************************\
 *  srun_job.c - job data structure creation functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
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

#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>           /* MAXPATHLEN */
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/cbuf.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/io_hdr.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/api/step_launch.h"

#include "src/srun/libsrun/allocate.h"
#include "src/srun/libsrun/debugger.h"
#include "src/srun/libsrun/fname.h"
#include "src/srun/libsrun/launch.h"
#include "src/srun/libsrun/opt.h"
#include "src/srun/libsrun/multi_prog.h"
#include "src/srun/libsrun/srun_job.h"

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
	uint16_t ntasks_per_board;/* number of tasks to invoke on each board */
	uint16_t ntasks_per_core; /* number of tasks to invoke on each core */
	uint16_t ntasks_per_socket;/* number of tasks to invoke on
				    * each socket */
	uint32_t                num_cpu_groups;
	char                   *partition;
	dynamic_plugin_data_t  *select_jobinfo;
	uint32_t                stepid;
} allocation_info_t;

typedef struct pack_resp_struct {
	char **alias_list;
	uint16_t *cpu_cnt;
	hostlist_t host_list;
	uint32_t node_cnt;
} pack_resp_struct_t;


static int shepherd_fd = -1;
static pthread_t signal_thread = (pthread_t) 0;
static int pty_sigarray[] = { SIGWINCH, 0 };

/*
 * Prototypes:
 */

static int  _become_user(void);
static void _call_spank_fini(void);
static int  _call_spank_local_user(srun_job_t *job, slurm_opt_t *opt_local);
static void _default_sigaction(int sig);
static long _diff_tv_str(struct timeval *tv1, struct timeval *tv2);
static void _handle_intr(srun_job_t *job);
static void _handle_pipe(void);
static srun_job_t *_job_create_structure(allocation_info_t *ainfo,
					 slurm_opt_t *opt_local);
static char *_normalize_hostlist(const char *hostlist);
static void _print_job_information(resource_allocation_response_msg_t *resp);
static void _run_srun_epilog (srun_job_t *job);
static void _run_srun_prolog (srun_job_t *job);
static int  _run_srun_script (srun_job_t *job, char *script);
static void _set_env_vars(resource_allocation_response_msg_t *resp,
			  int pack_offset);
static void _set_env_vars2(resource_allocation_response_msg_t *resp,
			   int pack_offset);
static void _set_ntasks(allocation_info_t *ai, slurm_opt_t *opt_local);
static void _set_prio_process_env(void);
static int  _set_rlimit_env(void);
static void _set_submit_dir_env(void);
static int  _set_umask_env(void);
static void _shepherd_notify(int shepherd_fd);
static int  _shepherd_spawn(srun_job_t *job, List srun_job_list,
			     bool got_alloc);
static void *_srun_signal_mgr(void *no_data);
static void _step_opt_exclusive(slurm_opt_t *opt_local);
static int  _validate_relative(resource_allocation_response_msg_t *resp,
			       slurm_opt_t *opt_local);


/*
 * Create an srun job structure w/out an allocation response msg.
 * (i.e. use the command line options)
 */
srun_job_t *
job_create_noalloc(void)
{
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(allocation_info_t));
	uint16_t cpn[1];
	uint32_t cpu_count_reps[1];
	slurm_opt_t *opt_local = &opt;
	hostlist_t  hl = hostlist_create(opt_local->nodelist);

	if (!hl) {
		error("Invalid node list `%s' specified", opt_local->nodelist);
		goto error;
	}
	srand48(getpid());
	ai->jobid          = MIN_NOALLOC_JOBID +
			     ((uint32_t) lrand48() %
			      (MAX_NOALLOC_JOBID - MIN_NOALLOC_JOBID + 1));
	ai->stepid         = (uint32_t) (lrand48());
	ai->nodelist       = opt_local->nodelist;
	ai->nnodes         = hostlist_count(hl);

	hostlist_destroy(hl);

	cpn[0] = (opt_local->ntasks + ai->nnodes - 1) / ai->nnodes;
	ai->cpus_per_node  = cpn;
	cpu_count_reps[0] = ai->nnodes;
	ai->cpu_count_reps = cpu_count_reps;
	ai->num_cpu_groups = 1;

	/*
	 * Create job, then fill in host addresses
	 */
	job = _job_create_structure(ai, opt_local);

	if (job != NULL)
		job_update_io_fnames(job, opt_local);

error:
	xfree(ai);
	return (job);

}

/*
 * Create an srun job structure for a step w/out an allocation response msg.
 * (i.e. inside an allocation)
 */
extern srun_job_t *job_step_create_allocation(
			resource_allocation_response_msg_t *resp,
			slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	uint32_t job_id = resp->job_id;
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(allocation_info_t));
	hostlist_t hl = NULL;
	char *buf = NULL;
	int count = 0;
	uint32_t alloc_count = 0;
	char *step_nodelist = NULL;
	xassert(srun_opt);

	ai->jobid          = job_id;
	ai->stepid         = NO_VAL;
	ai->alias_list     = resp->alias_list;
	if (srun_opt->alloc_nodelist)
		ai->nodelist = xstrdup(srun_opt->alloc_nodelist);
	else
		ai->nodelist = xstrdup(resp->node_list);
	hl = hostlist_create(ai->nodelist);
	hostlist_uniq(hl);
	alloc_count = hostlist_count(hl);
	ai->nnodes = alloc_count;
	hostlist_destroy(hl);

	if (opt_local->exc_nodes) {
		hostlist_t exc_hl = hostlist_create(opt_local->exc_nodes);
		hostlist_t inc_hl = NULL;
		char *node_name = NULL;

		hl = hostlist_create(ai->nodelist);
		if (opt_local->nodelist)
			inc_hl = hostlist_create(opt_local->nodelist);
		hostlist_uniq(hl);
		//info("using %s or %s", opt_local->nodelist, ai->nodelist);
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
		if (!opt_local->nodes_set) {
			/* we don't want to set the number of nodes =
			 * to the number of requested processes unless we
			 * know it is less than the number of nodes
			 * in the allocation
			 */
			if (opt_local->ntasks_set &&
			    (opt_local->ntasks < ai->nnodes))
				opt_local->min_nodes = opt_local->ntasks;
			else
				opt_local->min_nodes = ai->nnodes;
			opt_local->nodes_set = true;
		}
		if (!opt_local->max_nodes)
			opt_local->max_nodes = opt_local->min_nodes;
		if ((opt_local->max_nodes > 0) &&
		    (opt_local->max_nodes < ai->nnodes))
			ai->nnodes = opt_local->max_nodes;

		count = hostlist_count(hl);
		if (!count) {
			error("Hostlist is empty!  Can't run job.");
			hostlist_destroy(hl);
			goto error;
		}
		if (inc_hl) {
			count = hostlist_count(inc_hl);
			if (count < ai->nnodes) {
				/* add more nodes to get correct number for
				   allocation */
				hostlist_t tmp_hl = hostlist_copy(hl);
				int i = 0;
				int diff = ai->nnodes - count;
				buf = hostlist_ranged_string_xmalloc(inc_hl);
				hostlist_delete(tmp_hl, buf);
				xfree(buf);
				while ((i < diff) &&
				       (node_name = hostlist_shift(tmp_hl))) {
					hostlist_push_host(inc_hl, node_name);
					free(node_name);
					i++;
				}
				hostlist_destroy(tmp_hl);
			}
			buf = hostlist_ranged_string_xmalloc(inc_hl);
			hostlist_destroy(inc_hl);
			xfree(opt_local->nodelist);
			opt_local->nodelist = buf;
		} else {
			if (count > ai->nnodes) {
				/* remove more nodes than needed for
				 * allocation */
				int i;
				for (i = count; i >= ai->nnodes; i--)
					hostlist_delete_nth(hl, i);
			}
			xfree(opt_local->nodelist);
			opt_local->nodelist = hostlist_ranged_string_xmalloc(hl);
		}

		hostlist_destroy(hl);
	} else {
		if (!opt_local->nodes_set) {
			/* we don't want to set the number of nodes =
			 * to the number of requested processes unless we
			 * know it is less than the number of nodes
			 * in the allocation
			 */
			if (opt_local->ntasks_set &&
			    (opt_local->ntasks < ai->nnodes))
				opt_local->min_nodes = opt_local->ntasks;
			else
				opt_local->min_nodes = ai->nnodes;
			opt_local->nodes_set = true;
		}
		if (!opt_local->max_nodes)
			opt_local->max_nodes = opt_local->min_nodes;
		if ((opt_local->max_nodes > 0) &&
		    (opt_local->max_nodes < ai->nnodes))
			ai->nnodes = opt_local->max_nodes;
		/* Don't reset the ai->nodelist because that is the
		 * nodelist we want to say the allocation is under
		 * opt_local->nodelist is what is used for the allocation.
		 */
		/* xfree(ai->nodelist); */
		/* ai->nodelist = xstrdup(buf); */
	}

	/* get the correct number of hosts to run tasks on */
	if (opt_local->nodelist)
		step_nodelist = opt_local->nodelist;
	else if (((opt_local->distribution & SLURM_DIST_STATE_BASE) ==
		  SLURM_DIST_ARBITRARY) && (count == 0))
		step_nodelist = getenv("SLURM_ARBITRARY_NODELIST");
	if (step_nodelist) {
		hl = hostlist_create(step_nodelist);
		if ((opt_local->distribution & SLURM_DIST_STATE_BASE) !=
		    SLURM_DIST_ARBITRARY)
			hostlist_uniq(hl);
		if (!hostlist_count(hl)) {
			error("Hostlist is empty!  Can not run job.");
			hostlist_destroy(hl);
			goto error;
		}

		buf = hostlist_ranged_string_xmalloc(hl);
		count = hostlist_count(hl);
		hostlist_destroy(hl);
		/*
		 * Don't reset the ai->nodelist because that is the
		 * nodelist we want to say the allocation is under
		 * opt_local->nodelist is what is used for the allocation.
		 */
		/* xfree(ai->nodelist); */
		/* ai->nodelist = xstrdup(buf); */
		xfree(opt_local->nodelist);
		opt_local->nodelist = buf;
	}

	if (((opt_local->distribution & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY) && (count != opt_local->ntasks)) {
		error("You asked for %d tasks but hostlist specified %d nodes",
		      opt_local->ntasks, count);
		goto error;
	}

	if (ai->nnodes == 0) {
		error("No nodes in allocation, can't run job");
		goto error;
	}

	ai->num_cpu_groups = resp->num_cpu_groups;
	ai->cpus_per_node  = resp->cpus_per_node;
	ai->cpu_count_reps = resp->cpu_count_reps;
	ai->ntasks_per_board = resp->ntasks_per_board;

	/* Here let the srun options override the allocation resp */
	ai->ntasks_per_core = (opt_local->ntasks_per_core != NO_VAL) ?
		opt_local->ntasks_per_core : resp->ntasks_per_core;
	ai->ntasks_per_socket = (opt_local->ntasks_per_socket != NO_VAL) ?
		opt_local->ntasks_per_socket : resp->ntasks_per_socket;

	ai->partition = resp->partition;

/* 	info("looking for %d nodes out of %s with a must list of %s", */
/* 	     ai->nnodes, ai->nodelist, opt_local->nodelist); */
	/*
	 * Create job
	 */
	job = _job_create_structure(ai, opt_local);
error:
   	xfree(ai);
	return (job);

}

/*
 * Create an srun job structure from a resource allocation response msg
 */
extern srun_job_t *job_create_allocation(
			resource_allocation_response_msg_t *resp,
			slurm_opt_t *opt_local)
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
	i->ntasks_per_board = resp->ntasks_per_board;
	i->ntasks_per_core = resp->ntasks_per_core;
	i->ntasks_per_socket = resp->ntasks_per_socket;

	i->select_jobinfo = select_g_select_jobinfo_copy(resp->select_jobinfo);

	job = _job_create_structure(i, opt_local);
	if (job) {
		job->account = xstrdup(resp->account);
		job->qos = xstrdup(resp->qos);
		job->resv_name = xstrdup(resp->resv_name);
	}

	xfree(i->nodelist);
	xfree(i);

	return (job);
}

static void _copy_args(List missing_argc_list, slurm_opt_t *opt_master)
{
	srun_opt_t *srun_master = opt_master->srun_opt;
	ListIterator iter;
	slurm_opt_t *opt_local;
	int i;
	xassert(srun_master);

	iter = list_iterator_create(missing_argc_list);
	while ((opt_local = list_next(iter))) {
		srun_opt_t *srun_opt = opt_local->srun_opt;
		xassert(srun_opt);
		srun_opt->argc = srun_master->argc;
		srun_opt->argv = xmalloc(sizeof(char *) * (srun_opt->argc+1));
		for (i = 0; i < srun_opt->argc; i++)
			srun_opt->argv[i] = xstrdup(srun_master->argv[i]);
		list_remove(iter);
	}
	list_iterator_destroy(iter);
}

/*
 * Build "pack_group" string. If set on execute line, it may need to be
 * rebuilt for multiple option structures ("--pack-group=1,2" becomes two
 * opt structures). Clear "pack_grp_bits".if determined to not be a pack job.
 */
static void _pack_grp_test(List opt_list)
{
	ListIterator iter;
	slurm_opt_t *opt_local;
	int pack_offset;
	bitstr_t *master_map = NULL;
	List missing_argv_list = NULL;
	bool multi_pack = false, multi_prog = false;

	if (opt_list) {
		missing_argv_list = list_create(NULL);
		iter = list_iterator_create(opt_list);
		while ((opt_local = list_next(iter))) {
			srun_opt_t *srun_opt = opt_local->srun_opt;
			xassert(srun_opt);
			if (srun_opt->argc == 0)
				list_append(missing_argv_list, opt_local);
			else
				_copy_args(missing_argv_list, opt_local);
			xfree(srun_opt->pack_group);
			if (srun_opt->pack_grp_bits &&
			    ((pack_offset =
			      bit_ffs(srun_opt->pack_grp_bits)) >= 0)) {
				xstrfmtcat(srun_opt->pack_group, "%d",
					   pack_offset);
			}
			if (!srun_opt->pack_grp_bits) {
				error("%s: pack_grp_bits is NULL", __func__);
			} else if (!master_map) {
				master_map
					= bit_copy(srun_opt->pack_grp_bits);
			} else {
				if (bit_overlap(master_map,
						srun_opt->pack_grp_bits)) {
					fatal("Duplicate pack groups in single srun not supported");
				}
				bit_or(master_map, srun_opt->pack_grp_bits);
			}
			if (srun_opt->multi_prog)
				multi_prog = true;
		}
		if (master_map && (bit_set_count(master_map) > 1))
			multi_pack = true;
		FREE_NULL_BITMAP(master_map);
		list_iterator_destroy(iter);
		list_destroy(missing_argv_list);
	} else if (!sropt.pack_group && !getenv("SLURM_PACK_SIZE")) {
		FREE_NULL_BITMAP(sropt.pack_grp_bits);
		/* pack_group is already NULL */
	} else if (!sropt.pack_group && sropt.pack_grp_bits) {
		if ((pack_offset = bit_ffs(sropt.pack_grp_bits)) < 0)
			pack_offset = 0;
		else if (bit_set_count(sropt.pack_grp_bits) > 1)
			multi_pack = true;
		if (sropt.multi_prog)
			multi_prog = true;
		xstrfmtcat(sropt.pack_group, "%d", pack_offset);
	}

	if (multi_pack && multi_prog)
		fatal("--multi-prog option not supported with multiple pack groups");
}

/*
 * Copy job name from last component to all pack job components unless
 * explicitly set.
 */
static void _match_job_name(List opt_list)
{
	int cnt;
	ListIterator iter;
	slurm_opt_t *opt_local;

	if (!opt_list)
		return;

	cnt = list_count(opt_list);
	if (cnt < 2)
		return;

	iter = list_iterator_create(opt_list);
	while ((opt_local = list_next(iter))) {
		if (!opt_local->job_name)
			opt_local->job_name = xstrdup(opt.job_name);
		if (opt_local->srun_opt &&
		    (opt_local->srun_opt->open_mode == 0)) {
			opt_local->srun_opt->open_mode = OPEN_MODE_APPEND;
		}
	}
	list_iterator_destroy(iter);
}

static int _sort_by_offset(void *x, void *y)
{
	slurm_opt_t *opt_local1 = *(slurm_opt_t **) x;
	slurm_opt_t *opt_local2 = *(slurm_opt_t **) y;
	int offset1 = -1, offset2 = -1;

	if (opt_local1->srun_opt->pack_grp_bits)
		offset1 = bit_ffs(opt_local1->srun_opt->pack_grp_bits);
	if (opt_local2->srun_opt->pack_grp_bits)
		offset2 = bit_ffs(opt_local2->srun_opt->pack_grp_bits);
	if (offset1 < offset2)
		return -1;
	if (offset1 > offset2)
		return 1;
	return 0;
}

static void _post_opts(List opt_list)
{
	_pack_grp_test(opt_list);
	_match_job_name(opt_list);
	if (opt_list)
		list_sort(opt_list, _sort_by_offset);
}

extern void init_srun(int argc, char **argv,
		      log_options_t *logopt, int debug_level,
		      bool handle_signals)
{
	bool pack_fini = false;
	int i, pack_argc, pack_inx, pack_argc_off;
	char **pack_argv;

	/*
	 * This must happen before we spawn any threads
	 * which are not designed to handle arbitrary signals
	 */
	if (handle_signals) {
		if (xsignal_block(sig_array) < 0)
			error("Unable to block signals");
	}
	xsignal_block(pty_sigarray);

	/*
	 * Initialize plugin stack, read options from plugins, etc.
	 */
	init_spank_env();
	if (spank_init(NULL) < 0) {
		error("Plug-in initialization failed");
		exit(error_exit);
	}

	/*
	 * Be sure to call spank_fini when srun exits.
	 */
	if (atexit(_call_spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");

	pack_argc = argc;
	pack_argv = argv;
	for (pack_inx = 0; !pack_fini; pack_inx++) {
		pack_argc_off = -1;
		if (initialize_and_process_args(pack_argc, pack_argv,
						&pack_argc_off) < 0) {
			error("srun parameter parsing");
			exit(1);
		}
		if ((pack_argc_off >= 0) && (pack_argc_off < pack_argc)) {
			for (i = pack_argc_off; i < pack_argc; i++) {
				if (!xstrcmp(pack_argv[i], ":")) {
					pack_argc_off = i;
					break;
				}
			}
		}
		if ((pack_argc_off >= 0) && (pack_argc_off < pack_argc) &&
		    !xstrcmp(pack_argv[pack_argc_off], ":")) {
			/*
			 * move pack_argv[0] from "srun" to ":"
			 */
			pack_argc -= pack_argc_off;
			pack_argv += pack_argc_off;
		} else {
			pack_fini = true;
		}
	}
	_post_opts(opt_list);
	record_ppid();

	/*
	 * reinit log with new verbosity (if changed by command line)
	 */
	if (logopt && (_verbose || opt.quiet)) {
		/*
		 * If log level is already increased, only increment the
		 * level to the difference of _verbose an LOG_LEVEL_INFO
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

	/*
	 * Set up slurmctld message handler
	 */
	slurmctld_msg_init();

	/*
	 * save process startup time to be used with -I<timeout>
	 */
	srun_begin_time = time(NULL);
}

/*
 * Modify options for a job step (after job allocaiton is complete
 */
static void _set_step_opts(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	opt_local->time_limit = NO_VAL;/* not applicable for step, only job */
	xfree(opt_local->constraints);	/* not applicable for this step */
	if (!srun_opt->job_name_set_cmd && srun_opt->job_name_set_env) {
		/* use SLURM_JOB_NAME env var */
		sropt.job_name_set_cmd = true;
	}
	if ((srun_opt->core_spec_set || srun_opt->exclusive)
	    && opt_local->cpus_set) {
		/* Step gets specified CPU count, which may only part
		 * of the job allocation. */
		srun_opt->exclusive = true;
	} else {
		/* Step gets all CPUs in the job allocation. */
		srun_opt->exclusive = false;
	}
}

/*
 * Create the job step(s). For a heterogeneous job, each step is requested in
 * a separate RPC. create_job_step() references "opt", so we need to match up
 * the job allocation request with its requested options.
 */
static int _create_job_step(srun_job_t *job, bool use_all_cpus,
			    List srun_job_list, uint32_t pack_jobid,
			    char *pack_nodelist)
{
	ListIterator opt_iter = NULL, job_iter;
	slurm_opt_t *opt_local = &opt;
	uint32_t node_offset = 0, pack_nnodes = 0, step_id = NO_VAL;
	uint32_t pack_ntasks = 0, task_offset = 0;

	job_step_create_response_msg_t *step_resp;
	char *resv_ports = NULL;
	int rc = 0;

	if (srun_job_list) {
		if (opt_list)
			opt_iter = list_iterator_create(opt_list);
		job_iter = list_iterator_create(srun_job_list);
		while ((job = (srun_job_t *) list_next(job_iter))) {
			if (pack_jobid)
				job->pack_jobid = pack_jobid;
			job->stepid = NO_VAL;
			pack_nnodes += job->nhosts;
			pack_ntasks += job->ntasks;
		}

		list_iterator_reset(job_iter);
		while ((job = (srun_job_t *) list_next(job_iter))) {
			if (opt_list)
				opt_local = list_next(opt_iter);
			if (!opt_local)
				fatal("%s: opt_list too short", __func__);
			job->node_offset = node_offset;
			job->pack_nnodes = pack_nnodes;
			job->pack_ntasks = pack_ntasks;
			job->pack_task_offset = task_offset;
			if (step_id != NO_VAL)
				job->stepid = step_id;
			rc = create_job_step(job, use_all_cpus, opt_local);
			if (rc < 0)
				break;
			if (step_id == NO_VAL)
				step_id = job->stepid;

			if ((slurm_step_ctx_get(job->step_ctx,
						SLURM_STEP_CTX_RESP,
						&step_resp) == SLURM_SUCCESS) &&
			    step_resp->resv_ports &&
			    strcmp(step_resp->resv_ports, "(null)")) {
				if (resv_ports)
					xstrcat(resv_ports, ",");
				xstrcat(resv_ports, step_resp->resv_ports);
			}
			node_offset += job->nhosts;
			task_offset += job->ntasks;
		}

		if (resv_ports) {
			/*
			 * Merge numeric values into single range
			 * (e.g. "10-12,13-15,16-18" -> "10-18")
			 */
			hostset_t hs;
			char *tmp = NULL, *sep;
			xstrfmtcat(tmp, "[%s]", resv_ports);
			hs = hostset_create(tmp);
			hostset_ranged_string(hs, strlen(tmp) + 1, tmp);
			sep = strchr(tmp, ']');
			if (sep)
				sep[0] = '\0';
			xfree(resv_ports);
			resv_ports = xstrdup(tmp + 1);
			xfree(tmp);
			hostset_destroy(hs);

			list_iterator_reset(job_iter);
			while ((job = (srun_job_t *) list_next(job_iter))) {
				if (slurm_step_ctx_get(job->step_ctx,
						SLURM_STEP_CTX_RESP,
						&step_resp) == SLURM_SUCCESS) {
					xfree(step_resp->resv_ports);
					step_resp->resv_ports =
						xstrdup(resv_ports);
				}
			}
			xfree(resv_ports);
		}
		list_iterator_destroy(job_iter);
		if (opt_iter)
			list_iterator_destroy(opt_iter);
		return rc;
	} else if (job) {
		return create_job_step(job, use_all_cpus, &opt);
	} else {
		return -1;
	}
}

static void _cancel_steps(List srun_job_list)
{
	srun_job_t *job;
	ListIterator job_iter;
	slurm_msg_t req;
	step_complete_msg_t msg;
	int rc = 0;

	if (!srun_job_list)
		return;

	slurm_msg_t_init(&req);
	req.msg_type = REQUEST_STEP_COMPLETE;
	req.data = &msg;
	memset(&msg, 0, sizeof(step_complete_msg_t));
	msg.step_rc = 0;

	job_iter = list_iterator_create(srun_job_list);
	while ((job = (srun_job_t *) list_next(job_iter))) {
		if (job->stepid == NO_VAL)
			continue;
		msg.job_id	= job->jobid;
		msg.job_step_id	= job->stepid;
		msg.range_first	= 0;
		msg.range_last	= job->nhosts - 1;
		(void) slurm_send_recv_controller_rc_msg(&req, &rc,
							 working_cluster_rec);
	}
	list_iterator_destroy(job_iter);
}

static void _pack_struct_del(void *x)
{
	pack_resp_struct_t *pack_resp = (pack_resp_struct_t *) x;
	int i;

	if (pack_resp->alias_list) {
		for (i = 0; i < pack_resp->node_cnt; i++)
			xfree(pack_resp->alias_list[i]);
		xfree(pack_resp->alias_list);
	}
	xfree(pack_resp->cpu_cnt);
	if (pack_resp->host_list)
		hostlist_destroy(pack_resp->host_list);
	xfree(pack_resp);
}

static char *_compress_pack_nodelist(List used_resp_list)
{
	resource_allocation_response_msg_t *resp;
	pack_resp_struct_t *pack_resp;
	List pack_resp_list;
	ListIterator resp_iter;
	char *aliases = NULL, *save_ptr = NULL, *tok, *tmp;
	char *pack_nodelist = NULL, *node_name;
	hostset_t hs;
	int cnt, i, j, k, len = 0;
	uint16_t *cpus;
	uint32_t *reps, cpu_inx;
	bool have_aliases = false;

	if (!used_resp_list)
		return pack_nodelist;

	cnt = list_count(used_resp_list);
	pack_resp_list = list_create(_pack_struct_del);
	hs = hostset_create("");
	resp_iter = list_iterator_create(used_resp_list);
	while ((resp = (resource_allocation_response_msg_t *)
			list_next(resp_iter))) {
		if (!resp->node_list)
			continue;
		len += strlen(resp->node_list);
		hostset_insert(hs, resp->node_list);
		pack_resp = xmalloc(sizeof(pack_resp_struct_t));
		pack_resp->node_cnt = resp->node_cnt;
		/*
		 * alias_list contains <NodeName>:<NodeAddr>:<NodeHostName>
		 * values in comma separated list
		 */
		if (resp->alias_list) {
			have_aliases = true;
			pack_resp->alias_list = xmalloc(sizeof(char *) *
							resp->node_cnt);
			tmp = xstrdup(resp->alias_list);
			i = 0;
			tok = strtok_r(tmp, ",", &save_ptr);
			while (tok) {
				if (i >= resp->node_cnt) {
					fatal("%s: Invalid alias_list",
					      __func__);
				}
				pack_resp->alias_list[i++] = xstrdup(tok);
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp);
		}
		pack_resp->cpu_cnt = xmalloc(sizeof(uint16_t) * resp->node_cnt);
		pack_resp->host_list = hostlist_create(resp->node_list);
		for (i = 0, k = 0;
		     (i < resp->num_cpu_groups) && (k < resp->node_cnt); i++) {
			for (j = 0; j < resp->cpu_count_reps[i]; j++) {
				pack_resp->cpu_cnt[k++] =
					resp->cpus_per_node[i];
				if (k >= resp->node_cnt)
					break;
			}
			if (k >= resp->node_cnt)
				break;
		}
		list_append(pack_resp_list, pack_resp);
	}
	list_iterator_destroy(resp_iter);

	len += (cnt + 16);
	pack_nodelist = xmalloc(len);
	(void) hostset_ranged_string(hs, len, pack_nodelist);

	cpu_inx = 0;
	cnt = hostset_count(hs);
	cpus = xmalloc(sizeof(uint16_t) * (cnt + 1));
	reps = xmalloc(sizeof(uint32_t) * (cnt + 1));
	for (i = 0; i < cnt; i++) {
		node_name = hostset_nth(hs, i);
		resp_iter = list_iterator_create(pack_resp_list);
		while ((pack_resp = (pack_resp_struct_t *)
				    list_next(resp_iter))) {
			j = hostlist_find(pack_resp->host_list, node_name);
			if ((j == -1) || !pack_resp->cpu_cnt)
				continue;	/* node not in this pack job */
			if (have_aliases) {
				if (aliases)
					xstrcat(aliases, ",");
				if (pack_resp->alias_list &&
				    pack_resp->alias_list[j]) {
					xstrcat(aliases,
						pack_resp->alias_list[j]);
				} else {
					xstrfmtcat(aliases, "%s:%s:%s",
						   node_name, node_name,
						   node_name);
				}
			}
			if (cpus[cpu_inx] == pack_resp->cpu_cnt[j]) {
				reps[cpu_inx]++;
			} else {
				if (cpus[cpu_inx] != 0)
					cpu_inx++;
				cpus[cpu_inx] = pack_resp->cpu_cnt[j];
				reps[cpu_inx]++;
			}
			break;
		}
		list_iterator_destroy(resp_iter);
		free(node_name);
	}

	cpu_inx++;
	tmp = uint32_compressed_to_str(cpu_inx, cpus, reps);
	if (setenv("SLURM_JOB_CPUS_PER_NODE", tmp, 1) < 0) {
		error("%s: Unable to set SLURM_JOB_CPUS_PER_NODE in environment",
		      __func__);
	}
	xfree(tmp);

	if (aliases) {
		if (setenv("SLURM_NODE_ALIASES", aliases, 1) < 0) {
			error("%s: Unable to set SLURM_NODE_ALIASES in environment",
			      __func__);
		}
		xfree(aliases);
	}

	xfree(reps);
	xfree(cpus);
	hostset_destroy(hs);
	list_destroy(pack_resp_list);

	return pack_nodelist;
}

extern void create_srun_job(void **p_job, bool *got_alloc,
			    bool slurm_started, bool handle_signals)
{
	resource_allocation_response_msg_t *resp;
	List job_resp_list = NULL, srun_job_list = NULL;
	List used_resp_list = NULL;
	ListIterator opt_iter, resp_iter;
	srun_job_t *job = NULL;
	int i, max_list_offset, max_pack_offset, pack_offset = -1;
	slurm_opt_t *opt_local;
	uint32_t my_job_id = 0, pack_jobid = 0;
	char *pack_nodelist = NULL;
	bool begin_error_logged = false;
	bool core_spec_error_logged = false;
#ifdef HAVE_NATIVE_CRAY
	bool network_error_logged = false;
#endif
	bool node_cnt_error_logged = false;
	bool x11_error_logged = false;

	/*
	 * now global "opt" should be filled in and available,
	 * create a job from opt
	 */
	if (sropt.test_only) {
		int rc = allocate_test();
		if (rc) {
			slurm_perror("allocation failure");
			exit (1);
		}
		exit (0);

	} else if (sropt.no_alloc) {
		if (opt_list ||
		    (sropt.pack_grp_bits && (bit_fls(sropt.pack_grp_bits) > 0)))
			fatal("--no-allocation option not supported for heterogeneous jobs");
		info("do not allocate resources");
		job = job_create_noalloc();
		if (job == NULL) {
			error("Job creation failure.");
			exit(error_exit);
		}
		if (create_job_step(job, false, &opt) < 0)
			exit(error_exit);
	} else if ((job_resp_list = existing_allocation())) {
		max_list_offset = 0;
		max_pack_offset = list_count(job_resp_list) - 1;
		if (opt_list) {
			opt_iter = list_iterator_create(opt_list);
			while ((opt_local = list_next(opt_iter))) {
				srun_opt_t *srun_opt = opt_local->srun_opt;
				xassert(srun_opt);
				if (srun_opt->pack_grp_bits) {
					i = bit_fls(srun_opt->pack_grp_bits);
					max_list_offset = MAX(max_list_offset,
							      i);
				}
			}
			list_iterator_destroy(opt_iter);
			if (max_list_offset > max_pack_offset) {
				error("Attempt to run a job step with pack group value of %d, "
				      "but the job allocation has maximum value of %d",
				      max_list_offset, max_pack_offset);
				exit(1);
			}
		}
		srun_job_list = list_create(NULL);
		used_resp_list = list_create(NULL);
		if (max_pack_offset > 0)
			pack_offset = 0;
		resp_iter = list_iterator_create(job_resp_list);
		while ((resp = (resource_allocation_response_msg_t *)
				list_next(resp_iter))) {
			bool merge_nodelist = true;
			_print_job_information(resp);
			(void) get_next_opt(-2);
			while ((opt_local = get_next_opt(pack_offset))) {
				srun_opt_t *srun_opt = opt_local->srun_opt;
				xassert(srun_opt);
				if (my_job_id == 0) {
					my_job_id = resp->job_id;
					if (resp->working_cluster_rec)
						slurm_setup_remote_working_cluster(resp);
				}
				if (merge_nodelist) {
					merge_nodelist = false;
					list_append(used_resp_list, resp);
				}
				select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET,
							&resp->node_cnt);
				if (srun_opt->nodes_set_env  &&
				    !srun_opt->nodes_set_opt &&
				    (opt_local->min_nodes > resp->node_cnt)) {
					/*
					 * This signifies the job used the
					 * --no-kill option and a node went DOWN
					 * or it used a node count range
					 * specification, was checkpointed from
					 * one size and restarted at a different
					 * size
					 */
					if (!node_cnt_error_logged) {
						error("SLURM_NNODES environment variable "
						      "conflicts with allocated "
						      "node count (%u != %u).",
						      opt_local->min_nodes,
						      resp->node_cnt);
						node_cnt_error_logged = true;
					}
					/*
					 * Modify options to match resource
					 * allocation.
					 * NOTE: Some options are not supported
					 */
					opt_local->min_nodes = resp->node_cnt;
					xfree(srun_opt->alloc_nodelist);
					if (!opt_local->ntasks_set) {
						opt_local->ntasks =
							opt_local->min_nodes;
					}
				}
				if (srun_opt->core_spec_set &&
				    !core_spec_error_logged) {
					/*
					 * NOTE: Silently ignore specialized
					 * core count set with SLURM_CORE_SPEC
					 * environment variable
					 */
					error("Ignoring --core-spec value for a job step "
					      "within an existing job. Set specialized cores "
					      "at job allocation time.");
					core_spec_error_logged = true;
				}
#ifdef HAVE_NATIVE_CRAY
				if (opt_local->network &&
				    !network_error_logged) {
					if (srun_opt->network_set_env) {
						debug2("Ignoring SLURM_NETWORK value for a "
						       "job step within an existing job. "
						       "Using what was set at job "
						       "allocation time.  Most likely this "
						       "variable was set by sbatch or salloc.");
					} else {
						error("Ignoring --network value for a job step "
						      "within an existing job. Set network "
						      "options at job allocation time.");
					}
					network_error_logged = true;
				}
#endif
				if (srun_opt->exclusive)
					_step_opt_exclusive(opt_local);
				_set_env_vars(resp, pack_offset);
				if (_validate_relative(resp, opt_local))
					exit(error_exit);
				if (opt_local->begin && !begin_error_logged) {
					error("--begin is ignored because nodes are already allocated.");
					begin_error_logged = true;
				}
				if (opt_local->x11 && !x11_error_logged) {
					error("Ignoring --x11 option for a job step within an "
					      "existing job. Set x11 options at job allocation time.");
					x11_error_logged = true;
				}
				job = job_step_create_allocation(resp,
								 opt_local);
				if (!job)
					exit(error_exit);
				if (max_pack_offset > 0)
					job->pack_offset = pack_offset;
				list_append(srun_job_list, job);
			}	/* While more option structures */
			pack_offset++;
		}	/* More pack job components */
		list_iterator_destroy(resp_iter);

		max_pack_offset = get_max_pack_group();
		pack_offset = list_count(job_resp_list) - 1;
		if (max_pack_offset > pack_offset) {
			error("Requested pack-group offset exceeds highest pack job index (%d > %d)",
			      max_pack_offset, pack_offset);
			exit(error_exit);
		}
		i = list_count(srun_job_list);
		if (i == 0) {
			error("No directives to start application on any available "
			      "pack job components");
			exit(error_exit);
		}
		if (i == 1)
			FREE_NULL_LIST(srun_job_list);	/* Just use "job" */
		if (srun_job_list && (list_count(srun_job_list) > 1) &&
		    opt_list && (list_count(opt_list) > 1) && my_job_id) {
			pack_jobid = my_job_id;
		}
		if (list_count(job_resp_list) > 1)
			pack_nodelist = _compress_pack_nodelist(used_resp_list);
		list_destroy(used_resp_list);
		if (_create_job_step(job, false, srun_job_list, pack_jobid,
				     pack_nodelist) < 0) {
			if (*got_alloc)
				slurm_complete_job(my_job_id, 1);
			else
				_cancel_steps(srun_job_list);
			exit(error_exit);
		}
		xfree(pack_nodelist);
	} else {
		/* Combined job allocation and job step launch */
#if defined HAVE_FRONT_END && (!defined HAVE_BG || !defined HAVE_BG_FILES) && (!defined HAVE_REAL_CRAY)
		uid_t my_uid = getuid();
		if ((my_uid != 0) &&
		    (my_uid != slurm_get_slurm_user_id())) {
			error("srun task launch not supported on this system");
			exit(error_exit);
		}
#endif
		if (!sropt.job_name_set_env && sropt.job_name_set_cmd)
			setenvfs("SLURM_JOB_NAME=%s", opt.job_name);
		else if (!sropt.job_name_set_env && sropt.argc)
			setenvfs("SLURM_JOB_NAME=%s", sropt.argv[0]);

		if (opt_list) {
			job_resp_list = allocate_pack_nodes(handle_signals);
			if (!job_resp_list)
				exit(error_exit);
			srun_job_list = list_create(NULL);
			opt_iter  = list_iterator_create(opt_list);
			resp_iter = list_iterator_create(job_resp_list);
			while ((resp = (resource_allocation_response_msg_t *)
				       list_next(resp_iter))) {
				if (my_job_id == 0) {
					my_job_id = resp->job_id;
					*got_alloc = true;
				}
				opt_local = list_next(opt_iter);
				if (!opt_local)
					break;
				if (!global_resp)	/* Used by Cray/ALPS */
					global_resp = resp;
				_print_job_information(resp);
				_set_env_vars(resp, ++pack_offset);
				_set_env_vars2(resp, pack_offset);
				if (_validate_relative(resp, opt_local)) {
					slurm_complete_job(my_job_id, 1);
					exit(error_exit);
				}
				job = job_create_allocation(resp, opt_local);
				job->pack_offset = pack_offset;
				list_append(srun_job_list, job);
				_set_step_opts(opt_local);
			}
			list_iterator_destroy(opt_iter);
			list_iterator_destroy(resp_iter);
			setenvfs("SLURM_PACK_SIZE=%d", pack_offset + 1);
		} else {
			if (!(resp = allocate_nodes(handle_signals, &opt)))
				exit(error_exit);
			global_resp = resp;
			*got_alloc = true;
			my_job_id = resp->job_id;
			_print_job_information(resp);
			_set_env_vars(resp, -1);
			if (_validate_relative(resp, &opt)) {
				slurm_complete_job(resp->job_id, 1);
				exit(error_exit);
			}
			job = job_create_allocation(resp, &opt);
			_set_step_opts(&opt);
		}
		if (srun_job_list && (list_count(srun_job_list) > 1) &&
		    opt_list && (list_count(opt_list) > 1) && my_job_id) {
			pack_jobid = my_job_id;
			pack_nodelist = _compress_pack_nodelist(job_resp_list);
		}

		/*
		 *  Become --uid user
		 */
		if (_become_user () < 0)
			info("Warning: Unable to assume uid=%u", opt.uid);
		if (_create_job_step(job, true, srun_job_list, pack_jobid,
				     pack_nodelist) < 0) {
			slurm_complete_job(my_job_id, 1);
			exit(error_exit);
		}
		xfree(pack_nodelist);

		global_resp = NULL;
		if (opt_list) {
			resp_iter = list_iterator_create(job_resp_list);
			while ((resp = (resource_allocation_response_msg_t *)
				       list_next(resp_iter))) {
				slurm_free_resource_allocation_response_msg(
									resp);
			}
			list_iterator_destroy(resp_iter);
		} else {
			slurm_free_resource_allocation_response_msg(resp);
		}
	}

	/*
	 *  Become --uid user
	 */
	if (_become_user () < 0)
		info("Warning: Unable to assume uid=%u", opt.uid);

	if (!slurm_started) {
		/*
		 * Spawn process to ensure clean-up of job and/or step
		 * on abnormal termination
		 */
		shepherd_fd = _shepherd_spawn(job, srun_job_list, *got_alloc);
	}

	if (opt_list)
		*p_job = (void *) srun_job_list;
	else
		*p_job = (void *) job;
}

extern void pre_launch_srun_job(srun_job_t *job, bool slurm_started,
				bool handle_signals, slurm_opt_t *opt_local)
{
	if (handle_signals && !signal_thread) {
		slurm_thread_create(&signal_thread, _srun_signal_mgr, job);
	}

	/* if running from poe This already happened in srun. */
	if (slurm_started)
		return;

	_run_srun_prolog(job);
	if (_call_spank_local_user(job, opt_local) < 0) {
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

		/* Tell slurmctld that we were cancelled */
		if (job->state >= SRUN_JOB_CANCELLED)
			slurm_complete_job(job->jobid, NO_VAL);
		else
			slurm_complete_job(job->jobid, *global_rc);
	}
	_shepherd_notify(shepherd_fd);

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
	slurm_mutex_lock(&job->state_mutex);
	if (job->state < state) {
		job->state = state;
		slurm_cond_signal(&job->state_cond);

	}
	slurm_mutex_unlock(&job->state_mutex);
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

static void _set_ntasks(allocation_info_t *ai, slurm_opt_t *opt_local)
{
	int cnt = 0;

	if (opt_local->ntasks_set)
		return;

	if (opt_local->ntasks_per_node != NO_VAL) {
		cnt = ai->nnodes * opt_local->ntasks_per_node;
		opt_local->ntasks_set = true;	/* implicit */
	} else if (opt_local->cpus_set) {
		int i;

		for (i = 0; i < ai->num_cpu_groups; i++)
			cnt += (ai->cpu_count_reps[i] *
				(ai->cpus_per_node[i] /
				 opt_local->cpus_per_task));
		opt_local->ntasks_set = true;	/* implicit */
	}

	opt_local->ntasks = (cnt < ai->nnodes) ? ai->nnodes : cnt;
}

/*
 * Create an srun job structure from a resource allocation response msg
 */
static srun_job_t *_job_create_structure(allocation_info_t *ainfo,
					 slurm_opt_t *opt_local)
{
	srun_job_t *job = xmalloc(sizeof(srun_job_t));
	int i;
#if defined HAVE_BG
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);
#endif

	_set_ntasks(ainfo, opt_local);
	debug2("creating job with %d tasks", opt_local->ntasks);

	slurm_mutex_init(&job->state_mutex);
	slurm_cond_init(&job->state_cond, NULL);
	job->state = SRUN_JOB_INIT;

 	job->alias_list = xstrdup(ainfo->alias_list);
 	job->nodelist = xstrdup(ainfo->nodelist);
 	job->partition = xstrdup(ainfo->partition);
	job->stepid  = ainfo->stepid;
	job->pack_jobid  = NO_VAL;
	job->pack_nnodes = NO_VAL;
	job->pack_ntasks = NO_VAL;
 	job->pack_offset = NO_VAL;
	job->pack_task_offset = NO_VAL;

#if defined HAVE_BG
//#if defined HAVE_BGQ && defined HAVE_BG_FILES
	/* Since the allocation will have the correct cnode count get
	   it if it is available.  Else grab it from opt_local->min_nodes
	   (meaning the allocation happened before).
	*/
	if (ainfo->select_jobinfo) {
		select_g_select_jobinfo_get(ainfo->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &job->nhosts);
	} else
		job->nhosts   = opt_local->min_nodes;
	/* If we didn't ask for nodes set it up correctly here so the
	   step allocation does the correct thing.
	*/
	if (!opt_local->nodes_set) {
		opt_local->min_nodes = opt_local->max_nodes = job->nhosts;
		opt_local->nodes_set = true;
		opt_local->ntasks_per_node = NO_VAL;
		bg_figure_nodes_tasks(&opt_local->min_nodes,
				      &opt_local->max_nodes,
				      &opt_local->ntasks_per_node,
				      &opt_local->ntasks_set,
				      &opt_local->ntasks, opt_local->nodes_set,
				      srun_opt->nodes_set_opt,
				      opt_local->overcommit, 1);

#if defined HAVE_BG_FILES
		/* Replace the runjob line with correct information. */
		int i, matches = 0;
		for (i = 0; i < srun_opt->argc; i++) {
			if (!xstrcmp(srun_opt->argv[i], "-p")) {
				i++;
				xfree(srun_opt->argv[i]);
				srun_opt->argv[i]  = xstrdup_printf(
					"%d", opt_local->ntasks_per_node);
				matches++;
			} else if (!xstrcmp(srun_opt->argv[i], "--np")) {
				i++;
				xfree(srun_opt->argv[i]);
				srun_opt->argv[i]  = xstrdup_printf(
					"%d", opt_local->ntasks);
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
	opt_local->overcommit = true;
	job->nhosts = 1;
#else
	job->nhosts   = ainfo->nnodes;
#endif

#if !defined HAVE_FRONT_END || (defined HAVE_BGQ)
//#if !defined HAVE_FRONT_END || (defined HAVE_BGQ && defined HAVE_BG_FILES)
	if (opt_local->min_nodes > job->nhosts) {
		error("Only allocated %d nodes asked for %d",
		      job->nhosts, opt_local->min_nodes);
		if (opt_local->exc_nodes) {
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

	job->ntasks  = opt_local->ntasks;
	job->ntasks_per_board = ainfo->ntasks_per_board;
	job->ntasks_per_core = ainfo->ntasks_per_core;
	job->ntasks_per_socket = ainfo->ntasks_per_socket;

	/*
	 * If cpus_per_task is set then get the exact count of cpus for the
	 * requested step (we might very well use less, especially if
	 * --exclusive is used).  Else get the total for the allocation given.
	 */
	if (opt_local->cpus_set)
		job->cpu_count = opt_local->ntasks * opt_local->cpus_per_task;
	else {
		for (i = 0; i < ainfo->num_cpu_groups; i++) {
			job->cpu_count += ainfo->cpus_per_node[i] *
				ainfo->cpu_count_reps[i];
		}
	}

	job->rc       = -1;

	job_update_io_fnames(job, opt_local);

	return (job);
}

extern void job_update_io_fnames(srun_job_t *job, slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);
	job->ifname = fname_create(job, srun_opt->ifname, opt_local->ntasks);
	job->ofname = fname_create(job, srun_opt->ofname, opt_local->ntasks);
	job->efname = srun_opt->efname ?
		      fname_create(job, srun_opt->efname, opt_local->ntasks) :
		      job->ofname;
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

	if (xstrcmp(user, "nobody") == 0) {
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

	(void) initgroups(user, gid); /* Ignore errors */
	xfree(user);

	if (setuid (opt.uid) < 0)
		return (error ("setuid: %m"));

	return (0);
}

static int _call_spank_local_user(srun_job_t *job, slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	struct spank_launcher_job_info info[1];
	xassert(srun_opt);

	info->argc = srun_opt->argc;
	info->argv = srun_opt->argv;
	info->gid	= opt_local->gid;
	info->jobid	= job->jobid;
	info->stepid	= job->stepid;
	info->step_layout = launch_common_get_slurm_step_layout(job);
	info->uid	= opt_local->uid;

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
	if (!sropt.quit_on_intr && (_diff_tv_str(&last_intr, &now) > 1000000)) {
		if (sropt.disable_status) {
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

/* NOTE: Executed once for entire pack job */
static void _run_srun_epilog (srun_job_t *job)
{
	int rc;

	if (sropt.epilog && xstrcasecmp(sropt.epilog, "none") != 0) {
		rc = _run_srun_script(job, sropt.epilog);
		debug("srun epilog rc = %d", rc);
	}
}

static void _run_srun_prolog (srun_job_t *job)
{
	int rc;

	if (sropt.prolog && xstrcasecmp(sropt.prolog, "none") != 0) {
		rc = _run_srun_script(job, sropt.prolog);
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
		/*
		 * set the prolog/epilog scripts command line arguments to the
		 * application arguments (for last pack job component), but
		 * shifted one higher
		 */
		args = xmalloc(sizeof(char *) * 1024);
		args[0] = script;
		for (i = 0; i < sropt.argc; i++) {
			args[i+1] = sropt.argv[i];
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

static char *_build_key(char *base, int pack_offset)
{
	char *key = NULL;

	if (pack_offset == -1)
		key = xstrdup(base);
	else
		xstrfmtcat(key, "%s_PACK_GROUP_%d", base, pack_offset);

	return key;
}

static void _set_env_vars(resource_allocation_response_msg_t *resp,
			  int pack_offset)
{
	char *key, *value, *tmp;
	int i;

	key = _build_key("SLURM_JOB_CPUS_PER_NODE", pack_offset);
	if (!getenv(key)) {
		tmp = uint32_compressed_to_str(resp->num_cpu_groups,
					       resp->cpus_per_node,
					       resp->cpu_count_reps);
		if (setenvf(NULL, key, "%s", tmp) < 0)
			error("unable to set %s in environment", key);
		xfree(tmp);
	}
	xfree(key);

	key = _build_key("SLURM_NODE_ALIASES", pack_offset);
	if (resp->alias_list) {
		if (setenv(key, resp->alias_list, 1) < 0)
			error("unable to set %s in environment", key);
	} else {
		unsetenv(key);
	}
	xfree(key);

	if (resp->env_size) {	/* Used to set Burst Buffer environment */
		for (i = 0; i < resp->env_size; i++) {
			tmp = xstrdup(resp->environment[i]);
			key = tmp;
			value = strchr(tmp, '=');
			if (value) {
				value[0] = '\0';
				value++;
				setenv(key, value, 0);
			}
			xfree(tmp);
		}
	}

	return;
}

/*
 * Set some pack-job environment variables for combined job & step allocation
 */
static void _set_env_vars2(resource_allocation_response_msg_t *resp,
			   int pack_offset)
{
	char *key;

	if (resp->account) {
		key = _build_key("SLURM_JOB_ACCOUNT", pack_offset);
		if (!getenv(key) &&
		    (setenvf(NULL, key, "%u", resp->account) < 0)) {
			error("unable to set %s in environment", key);
		}
		xfree(key);
	}

	key = _build_key("SLURM_JOB_ID", pack_offset);
	if (!getenv(key) &&
	    (setenvf(NULL, key, "%u", resp->job_id) < 0)) {
		error("unable to set %s in environment", key);
	}
	xfree(key);

	key = _build_key("SLURM_JOB_NODELIST", pack_offset);
	if (!getenv(key) &&
	    (setenvf(NULL, key, "%s", resp->node_list) < 0)) {
		error("unable to set %s in environment", key);
	}
	xfree(key);

	key = _build_key("SLURM_JOB_PARTITION", pack_offset);
	if (!getenv(key) &&
	    (setenvf(NULL, key, "%s", resp->partition) < 0)) {
		error("unable to set %s in environment", key);
	}
	xfree(key);

	if (resp->qos) {
		key = _build_key("SLURM_JOB_QOS", pack_offset);
		if (!getenv(key) &&
		    (setenvf(NULL, key, "%u", resp->qos) < 0)) {
			error("unable to set %s in environment", key);
		}
		xfree(key);
	}

	if (resp->resv_name) {
		key = _build_key("SLURM_JOB_RESERVATION", pack_offset);
		if (!getenv(key) &&
		    (setenvf(NULL, key, "%u", resp->resv_name) < 0)) {
			error("unable to set %s in environment", key);
		}
		xfree(key);
	}

	if (resp->alias_list) {
		key = _build_key("SLURM_NODE_ALIASES", pack_offset);
		if (!getenv(key) &&
		    (setenvf(NULL, key, "%u", resp->alias_list) < 0)) {
			error("unable to set %s in environment", key);
		}
		xfree(key);
	}
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
	if (sropt.propagate
	    && parse_rlimits(sropt.propagate, PROPAGATE_RLIMITS)) {
		error( "--propagate=%s is not valid.", sropt.propagate );
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
		if (sropt.propagate && (rli->propagate_flag == PROPAGATE_RLIMITS))
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

static void _shepherd_notify(int shepherd_fd)
{
	int rc;

	while (1) {
		rc = write(shepherd_fd, "", 1);
		if (rc == -1) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			error("write(shepherd): %m");
		}
		break;
	}
	close(shepherd_fd);
}

static int _shepherd_spawn(srun_job_t *job, List srun_job_list, bool got_alloc)
{
	int shepherd_pipe[2], rc;
	pid_t shepherd_pid;
	char buf[1];

	if (pipe(shepherd_pipe)) {
		error("pipe: %m");
		return -1;
	}

	shepherd_pid = fork();
	if (shepherd_pid == -1) {
		error("fork: %m");
		return -1;
	}
	if (shepherd_pid != 0) {
		close(shepherd_pipe[0]);
		return shepherd_pipe[1];
	}

	/* Wait for parent to notify of completion or I/O error on abort */
	close(shepherd_pipe[1]);
	while (1) {
		rc = read(shepherd_pipe[0], buf, 1);
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

	if (srun_job_list) {
		ListIterator job_iter;
		job_iter  = list_iterator_create(srun_job_list);
		while ((job = (srun_job_t *) list_next(job_iter))) {
			(void) slurm_kill_job_step(job->jobid, job->stepid,
						   SIGKILL);
			if (got_alloc)
				slurm_complete_job(job->jobid, NO_VAL);
		}
		list_iterator_destroy(job_iter);
	} else {
		(void) slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);
		if (got_alloc)
			slurm_complete_job(job->jobid, NO_VAL);
	}

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
				info("First task exited %ds ago", sropt.max_wait);
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

/* if srun_opt->exclusive is set, disable user task layout controls */
static void _step_opt_exclusive(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	if (!opt_local->ntasks_set) {
		error("--ntasks must be set with --exclusive");
		exit(error_exit);
	}
	if (srun_opt->relative_set) {
		error("--relative disabled, incompatible with --exclusive");
		exit(error_exit);
	}
	if (opt_local->exc_nodes) {
		error("--exclude is incompatible with --exclusive");
		exit(error_exit);
	}
}

static int _validate_relative(resource_allocation_response_msg_t *resp,
			      slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	if (srun_opt->relative_set &&
	    ((srun_opt->relative + opt_local->min_nodes)
	     > resp->node_cnt)) {
		if (srun_opt->nodes_set_opt) {
			/* -N command line option used */
			error("--relative and --nodes option incompatible "
			      "with count of allocated nodes (%d+%d>%d)",
			      srun_opt->relative,
			      opt_local->min_nodes,
			      resp->node_cnt);
		} else {		/* SLURM_NNODES option used */
			error("--relative and SLURM_NNODES option incompatible "
			      "with count of allocated nodes (%d+%d>%d)",
			      srun_opt->relative,
			      opt_local->min_nodes,
			      resp->node_cnt);
		}
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static void _call_spank_fini(void)
{
	if (-1 != shepherd_fd)
		spank_fini(NULL);
}
