/****************************************************************************\
 *  job.c - job data structure creation functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "src/common/bitstring.h"
#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_cred.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/srun/job.h"
#include "src/srun/opt.h"
#include "src/srun/fname.h"
#include "src/srun/signals.h"

#if HAVE_TOTALVIEW
#include "src/srun/attach.h"
#endif


/*
 * allocation information structure used to store general information
 * about node allocation to be passed to _job_create_internal()
 */
typedef struct allocation_info {
	uint32_t                jobid;
	uint32_t                stepid;
	char                   *nodelist;
	int                     nnodes;
	slurm_addr             *addrs;
	int                     num_cpu_groups;
	int                    *cpus_per_node;
	int                    *cpu_count_reps;
} allocation_info_t;



/*
 * Prototypes:
 */
static inline int _estimate_nports(int nclients, int cli_per_port);
static int        _compute_task_count(allocation_info_t *info);
static void       _set_nprocs(allocation_info_t *info);
static job_t *    _job_create_internal(allocation_info_t *info);
static void       _job_fake_cred(job_t *job);
static int        _job_resp_add_nodes(bitstr_t *req_bitmap, 
				bitstr_t *exc_bitmap, int node_cnt);
static int        _job_resp_bitmap(hostlist_t resp_node_hl, char *nodelist, 
				bitstr_t *bitmap_ptr);
static int        _job_resp_count_max_tasks(
				resource_allocation_response_msg_t *resp);
static int        _job_resp_cpus(uint32_t *cpus_per_node, 
				uint32_t *cpu_count_reps, int node);
static void       _job_resp_hack(resource_allocation_response_msg_t *resp, 
				bitstr_t *req_bitmap);
static char *     _task_state_name(task_state_t state_inx);
static char *     _host_state_name(host_state_t state_inx);
static char *     _normalize_hostlist(const char *hostlist);


/*
 * Create an srun job structure from a resource allocation response msg
 */
job_t *
job_create_allocation(resource_allocation_response_msg_t *resp)
{
	job_t *job;
	allocation_info_t *i = xmalloc(sizeof(*i));

	i->nodelist       = _normalize_hostlist(resp->node_list);
	i->nnodes	  = resp->node_cnt;
	i->jobid          = resp->job_id;
	i->stepid         = NO_VAL;
	i->num_cpu_groups = resp->num_cpu_groups;
	i->cpus_per_node  = resp->cpus_per_node;
	i->cpu_count_reps = resp->cpu_count_reps;
	i->addrs          = resp->node_addr;

	job = _job_create_internal(i);

	xfree(i->nodelist);
	xfree(i);

	return (job);
}


/* 
 * Create an srun job structure w/out an allocation response msg.
 * (i.e. use the command line options)
 */
job_t *
job_create_noalloc(void)
{
	job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(*ai));
	int cpn = 1;
	int i   = 0;

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

	/* if (opt.nprocs < ai->nnodes)
		opt.nprocs = hostlist_count(hl);
	*/
	hostlist_destroy(hl);

	cpn = (opt.nprocs + ai->nnodes - 1) / ai->nnodes;
	ai->cpus_per_node  = &cpn;
	ai->cpu_count_reps = &ai->nnodes;
	ai->addrs          = NULL; 

	/* 
	 * Create job, then fill in host addresses
	 */
	job = _job_create_internal(ai);

	for (i = 0; i < job->nhosts; i++) {
		slurm_set_addr ( &job->slurmd_addr[i], 
				  slurm_get_slurmd_port(), 
				  job->host[i] );
	}

	_job_fake_cred(job);

   error:
	xfree(ai);
	return (job);

}


void
update_job_state(job_t *job, job_state_t state)
{
	pthread_mutex_lock(&job->state_mutex);
	if (job->state < state) {
		job->state = state;
		pthread_cond_signal(&job->state_cond);
	}
	pthread_mutex_unlock(&job->state_mutex);
}

job_state_t 
job_state(job_t *job)
{
	job_state_t state;
	slurm_mutex_lock(&job->state_mutex);
	state = job->state;
	slurm_mutex_unlock(&job->state_mutex);
	return state;
}


void 
job_force_termination(job_t *job)
{
	if (mode == MODE_ATTACH) {
		info ("forcing detach");
		update_job_state(job, SRUN_JOB_DETACHED); 	
	} else {
		info ("forcing job termination");
		update_job_state(job, SRUN_JOB_FORCETERM);
	}

	pthread_kill(job->ioid,  SIGHUP);
}


int
job_rc(job_t *job)
{
	int i;
	int rc = 0;

	if (job->rc >= 0) return(job->rc);


	for (i = 0; i < opt.nprocs; i++) {
		if (job->rc < job->tstatus[i])
			job->rc = job->tstatus[i];
	}

	if ((rc = WEXITSTATUS(job->rc)))
		job->rc = rc;
	else if (WIFSIGNALED(job->rc))
		job->rc = 128 + WTERMSIG(job->rc);

	return(job->rc);
}


void job_fatal(job_t *job, const char *msg)
{
	if (msg) error(msg);

	job_destroy(job, errno);

	exit(1);
}


void 
job_destroy(job_t *job, int error)
{
	if (job->removed)
		return;

	if (job->old_job) {
		debug("cancelling job step %u.%u", job->jobid, job->stepid);
		slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);
		slurm_complete_job_step(job->jobid, job->stepid, 0, error);
	} else if (!opt.no_alloc) {
		debug("cancelling job %u", job->jobid);
		slurm_complete_job(job->jobid, 0, error);
	} else {
		debug("no allocation to cancel, killing remote tasks");
		fwd_signal(job, SIGKILL); 
		return;
	}

#ifdef HAVE_TOTALVIEW
	if (error) tv_launch_failure();
#endif

	job->removed = true;
}


void
job_kill(job_t *job)
{
	if (!opt.no_alloc) {
		if (slurm_kill_job_step(job->jobid, job->stepid, SIGKILL) < 0)
			error ("slurm_kill_job_step: %m");
	}
	update_job_state(job, SRUN_JOB_FAILED);
}


int
job_active_tasks_on_host(job_t *job, int hostid)
{
	int i;
	int retval = 0;

	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->ntask[hostid]; i++) {
		uint32_t tid = job->tids[hostid][i];
		if (job->task_state[tid] == SRUN_TASK_RUNNING) 
			retval++;
	}
	slurm_mutex_unlock(&job->task_mutex);
	return retval;
}
	
void 
report_job_status(job_t *job)
{
	int i;

	for (i = 0; i < job->nhosts; i++) {
		info ("host:%s state:%s", job->host[i], 
		      _host_state_name(job->host_state[i]));
	}
}


#define NTASK_STATES 6
void 
report_task_status(job_t *job)
{
	int i;
	char buf[1024];
	hostlist_t hl[NTASK_STATES];

	for (i = 0; i < NTASK_STATES; i++)
		hl[i] = hostlist_create(NULL);

	for (i = 0; i < opt.nprocs; i++) {
		int state = job->task_state[i];
		if ((state == SRUN_TASK_EXITED) 
		    && ((job->err[i] >= 0) || (job->out[i] >= 0)))
			state = 4;
		snprintf(buf, 256, "task%d", i);
		hostlist_push(hl[state], buf); 
	}

	for (i = 0; i< NTASK_STATES; i++) {
		if (hostlist_count(hl[i]) > 0) {
			hostlist_ranged_string(hl[i], 1022, buf);
			info("%s: %s", buf, _task_state_name(i));
		}
		hostlist_destroy(hl[i]);
	}

}


static inline int
_estimate_nports(int nclients, int cli_per_port)
{
	div_t d;
	d = div(nclients, cli_per_port);
	return d.rem > 0 ? d.quot + 1 : d.quot;
}

static int
_compute_task_count(allocation_info_t *info)
{
	int i, cnt = 0;

	if (opt.cpus_set) {
		for (i = 0; i < info->num_cpu_groups; i++)
			cnt += ( info->cpu_count_reps[i] *
				 (info->cpus_per_node[i]/opt.cpus_per_task));
	}

	return (cnt < info->nnodes) ? info->nnodes : cnt;
}

static void
_set_nprocs(allocation_info_t *info)
{
	if (!opt.nprocs_set)
		opt.nprocs = _compute_task_count(info);
}


static job_t *
_job_create_internal(allocation_info_t *info)
{
	int i;
	int cpu_cnt = 0;
	int cpu_inx = 0;
	int tph     = 0;
	hostlist_t hl;
	job_t *job;

	/* Reset nprocs if necessary 
	 */
	_set_nprocs(info);

	debug2("creating job with %d tasks", opt.nprocs);

	job = xmalloc(sizeof(*job));

	slurm_mutex_init(&job->state_mutex);
	pthread_cond_init(&job->state_cond, NULL);
	job->state = SRUN_JOB_INIT;

	job->signaled = false;
	job->rc       = -1;

	job->nodelist = xstrdup(info->nodelist);
	hl = hostlist_create(job->nodelist);
	job->nhosts = hostlist_count(hl);

	job->jobid   = info->jobid;
	job->stepid  = info->stepid;
	job->old_job = false;
	job->removed = false;

	/* 
	 *  Initialize Launch and Exit timeout values
	 */
	job->ltimeout = 0;
	job->etimeout = 0;

	job->slurmd_addr = xmalloc(job->nhosts * sizeof(slurm_addr));
	if (info->addrs)
		memcpy( job->slurmd_addr, info->addrs, 
			sizeof(slurm_addr)*job->nhosts);

	job->host  = (char **) xmalloc(job->nhosts * sizeof(char *));
	job->cpus  = (int *)   xmalloc(job->nhosts * sizeof(int) );
	job->ntask = (int *)   xmalloc(job->nhosts * sizeof(int) );

	/* Compute number of file descriptors / Ports needed for Job 
	 * control info server
	 */
	job->njfds = _estimate_nports(opt.nprocs, 48);
	job->jfd   = (slurm_fd *)   xmalloc(job->njfds * sizeof(slurm_fd));
	job->jaddr = (slurm_addr *) xmalloc(job->njfds * sizeof(slurm_addr));

	debug3("njfds = %d", job->njfds);

	/* Compute number of IO file descriptors needed and allocate 
	 * memory for them
	 */
	job->niofds = _estimate_nports(opt.nprocs, 64);
	job->iofd   = (int *) xmalloc(job->niofds * sizeof(int));
	job->ioport = (int *) xmalloc(job->niofds * sizeof(int));

	/* ntask stdout and stderr fds */
	job->out    = (int *)  xmalloc(opt.nprocs * sizeof(int));
	job->err    = (int *)  xmalloc(opt.nprocs * sizeof(int));

	/* ntask cbufs for stdout and stderr */
	job->outbuf     = (cbuf_t *) xmalloc(opt.nprocs * sizeof(cbuf_t));
	job->errbuf     = (cbuf_t *) xmalloc(opt.nprocs * sizeof(cbuf_t));
	job->inbuf      = (cbuf_t *) xmalloc(opt.nprocs * sizeof(cbuf_t));
	job->stdin_eof  = (bool *)   xmalloc(opt.nprocs * sizeof(bool));


	/* nhost host states */
	job->host_state =  xmalloc(job->nhosts * sizeof(host_state_t));

	/* ntask task states and statii*/
	job->task_state  =  xmalloc(opt.nprocs * sizeof(task_state_t));
	job->tstatus	 =  xmalloc(opt.nprocs * sizeof(int));

	for (i = 0; i < opt.nprocs; i++) {
		job->task_state[i] = SRUN_TASK_INIT;

		job->outbuf[i]     = cbuf_create(4096, 1048576);
		job->errbuf[i]     = cbuf_create(4096, 1048576);
		job->inbuf[i]      = cbuf_create(4096, 4096);

		cbuf_opt_set(job->outbuf[i], CBUF_OPT_OVERWRITE, CBUF_NO_DROP);
		cbuf_opt_set(job->errbuf[i], CBUF_OPT_OVERWRITE, CBUF_NO_DROP);
		cbuf_opt_set(job->inbuf[i],  CBUF_OPT_OVERWRITE, CBUF_NO_DROP);

		job->stdin_eof[i]  = false;
	}

	slurm_mutex_init(&job->task_mutex);

	/* tasks per host, round up */
	tph = (opt.nprocs+job->nhosts-1) / job->nhosts; 

	for(i = 0; i < job->nhosts; i++) {
		job->host[i]  = hostlist_shift(hl);

		if (opt.overcommit)
			job->cpus[i] = tph;
		else
			job->cpus[i] = info->cpus_per_node[cpu_inx];

		if ((++cpu_cnt) >= info->cpu_count_reps[cpu_inx]) {
			/* move to next record */
			cpu_inx++;
			cpu_cnt = 0;
		}
	}

	job_update_io_fnames(job);

	hostlist_destroy(hl);

	return job;
}

void
job_update_io_fnames(job_t *job)
{
	job->ifname = fname_create(job, opt.ifname);
	job->ofname = fname_create(job, opt.ofname);
	job->efname = opt.efname ? fname_create(job, opt.efname) : job->ofname;
}

static void
_job_fake_cred(job_t *job)
{
	slurm_cred_arg_t arg;
	arg.jobid    = job->jobid;
	arg.stepid   = job->stepid;
	arg.uid      = opt.uid;
	arg.hostlist = job->nodelist;
	job->cred = slurm_cred_faker(&arg);
}



static char *
_task_state_name(task_state_t state_inx)
{
	switch (state_inx) {
		case SRUN_TASK_INIT:
			return "initializing";
		case SRUN_TASK_RUNNING:
			return "running";
		case SRUN_TASK_FAILED:
			return "failed";
		case SRUN_TASK_EXITED:
			return "exited";
		case SRUN_TASK_IO_WAIT:
			return "waiting for io";
		case SRUN_TASK_ABNORMAL_EXIT:
			return "exited abnormally";
		default:
			return "unknown";
	}
}

static char *
_host_state_name(host_state_t state_inx)
{
	switch (state_inx) {
		case SRUN_HOST_INIT:
			return "initial";
		case SRUN_HOST_CONTACTED:
			return "contacted";
		case SRUN_HOST_UNREACHABLE:
			return "unreachable";
		case SRUN_HOST_REPLIED:
			return "replied";
		default:
			return "unknown";
	}
}


/*
 *  Returns the first integer pushed onto the hostlist hl.
 *   Returns -2 when hostlist is empty, -1 if strtoul fails.
 */
static int 
_hostlist_shift_int(hostlist_t hl)
{
	char *str = hostlist_shift(hl);
	char *p = NULL;
	unsigned long n;

	if (!str) return (-2);

	n = strtoul(str, &p, 10);
	if ((n < 0) || (*p != '\0')) {
		free(str);
		return -1;
	}
		
	free(str);

	return ((int) n);
}

/*
 *  Returns a ranged string representation of hostlist hl
 *   string is allocated with xmalloc() and must be freed with xfree()
 */
static char *
_hostlist_string_create(hostlist_t hl)
{
	int  len = 4096;
	char *buf = xmalloc(len*sizeof(char));

	while (hostlist_ranged_string(hl, len, buf) < 0)
		xrealloc(buf, (len+=4096)*sizeof(char));

	return buf;
}

/*
 *  Applies the setting of opt.relative to the hostlist given
 *
 */
static char *
_relative_hosts(hostlist_t hl)
{
	int n = 0;
	hostlist_t rl, rlist;
	char *relnodes = NULL;

	xassert (opt.relative);

	if (!(rl = hostlist_create(opt.relative))) 
		return NULL;

	rlist = hostlist_create(NULL); 

	if (hostlist_count(rl) == 1) {
		int i;
		int origin  = _hostlist_shift_int(rl);
		int horizon = MIN(opt.min_nodes, hostlist_count(hl));

		for (i = 0; i < horizon; i++) {
			char *host = hostlist_nth(hl, i+origin);
			hostlist_push_host(rlist, host);
			free (host);
		}

		goto done;
	}

	while ((n = _hostlist_shift_int(rl)) > -2) {
		char *host;

		if (n < 0) {
			hostlist_destroy(rlist);
			hostlist_destroy(rl);
			return NULL;
		}

		host = hostlist_nth(hl, n);
	        hostlist_push_host(rlist, host);
		free (host);
	}

    done:
	relnodes = _hostlist_string_create(rlist);

	/*
	 *  Reset min nodes to the minimum of the new count of available
	 *   hosts and the existing value. This means that requesting
	 *   relative nodes is, in effect, deselecting nodes outside
	 *   the relative set.
	 *
	 *  This will allow proper srun options to fall naturally
	 *   out of use of the relative option.
	 */
	n = hostlist_count(rlist);
	if (n < opt.min_nodes) {
		info("Warning: Only %d node%s available in relative set, "
		     "resetting nnodes to %d", n, (n>1 ? "s":""), n);
		opt.min_nodes = n;
	}

	hostlist_destroy(rlist);
	hostlist_destroy(rl);
	return relnodes;
}

/*
 *  Apply the user option -r, --relative to the allocation response.
 *   Exits program on error parsing relative option.
 *
 */
static void
_apply_relative_option(resource_allocation_response_msg_t *resp,
                       bitstr_t *reqbits)
{
	bitstr_t *relbits = NULL;
	char *relnodes    = NULL;
	hostlist_t hl     = NULL;

	if (!opt.relative)
		return;

	hl = hostlist_create(resp->node_list);

	if (!(relnodes = _relative_hosts(hl))) {
		error ("Bad argument to -r,--relative: `%s'", opt.relative);
		exit (1);
	}

	relbits  = bit_alloc(resp->node_cnt);

	_job_resp_bitmap(hl, relnodes, reqbits);
	_job_resp_hack(resp, reqbits);

	hostlist_destroy (hl);
	xfree (relnodes);
	bit_free (relbits);

	return;
}


/* The below functions are used to support job steps *\
\* with different allocations than the parent job.   */
int    job_resp_hack_for_step(resource_allocation_response_msg_t *resp)
{
	bitstr_t *exc_bitmap = NULL, *req_bitmap = NULL;
	hostlist_t resp_nodes = hostlist_create(resp->node_list);
	int return_code = 0, total;

	req_bitmap = bit_alloc(resp->node_cnt);
	exc_bitmap = bit_alloc(resp->node_cnt);

	/*
	 *  Apply -r, --relative option first
	 */
	_apply_relative_option(resp, req_bitmap);

	if (opt.nodelist && 
	    _job_resp_bitmap(resp_nodes, opt.nodelist, req_bitmap)) {
		error("Required nodes (%s) missing from job's allocation (%s)",
			opt.nodelist, resp->node_list);
		return_code = 1;
		goto cleanup;
	}

	if (opt.exc_nodes) {
		bitstr_t *tmp_bitmap;
		int overlap;
		_job_resp_bitmap(resp_nodes, opt.exc_nodes, exc_bitmap);
		tmp_bitmap = bit_copy(exc_bitmap);
		bit_and(tmp_bitmap, req_bitmap);
		overlap = bit_set_count(tmp_bitmap);
		bit_free(tmp_bitmap);
		if (overlap > 0) {
			error("Duplicates in hostlist (%s) "
			      "and exclude list (%s)",
			      opt.nodelist, opt.exc_nodes);
			return_code = 1;
			goto cleanup;
		}
	}

	/* Add nodes as specified */
	total = _job_resp_add_nodes(req_bitmap, exc_bitmap, resp->node_cnt);
	if (opt.nodes_set) {
		if (total < opt.min_nodes) {
			error("More nodes requested (%d) than available (%d)",
				opt.min_nodes, total);
			return_code = 1;
			goto cleanup;
		} 
	}

	if (total != resp->node_cnt)
		_job_resp_hack(resp, req_bitmap);
	if (!opt.overcommit) {
		int total = _job_resp_count_max_tasks(resp);
		if (total < opt.nprocs) {
			error("More tasks requested (%d) than resources (%d)",
				opt.nprocs, total);
			return_code = 1;
			goto cleanup;
		}
	}

      cleanup:
	if (exc_bitmap)
		bit_free(exc_bitmap);
	if (req_bitmap)
		bit_free(req_bitmap);
	return return_code;
}


static int 
_job_resp_add_nodes(bitstr_t *req_bitmap, bitstr_t *exc_bitmap, int node_cnt)
{
	int inx, offset;
	int total = bit_set_count(req_bitmap);
	int max_nodes;

	if (opt.nodes_set)
		max_nodes = MAX(opt.min_nodes, opt.max_nodes);
	else
		max_nodes = node_cnt;

	/* work up from first allocated node to first excluded node */
	offset = bit_ffs(req_bitmap);
	if (offset == -1)	/* no specific required nodes */
		offset = 0;	/* start at beginning */
	for (inx=offset; inx<node_cnt; inx++) {
		if (total >= max_nodes) 
			break;
		if (bit_test(exc_bitmap, inx))
			break;
		if (bit_test(req_bitmap, inx))
			continue;
		bit_set(req_bitmap, inx);
		total++;
	}

	/* then work down from first allocated node to first excluded node */
	for (inx=offset; inx>=0; inx--) {
		if (total >= max_nodes) 
			break;
		if (bit_test(exc_bitmap, inx))
			break;
		if (bit_test(req_bitmap, inx))
			continue;
		bit_set(req_bitmap, inx);
		total++;
	}
	if (opt.contiguous)
		return total;

	/* then get everything else */
	for (inx=0; inx<node_cnt; inx++) {
		if (total >= max_nodes) 
			break;
		if (bit_test(exc_bitmap, inx))
			continue;
		if (bit_test(req_bitmap, inx))
			continue;
		bit_set(req_bitmap, inx);
		total++;
	}
	return total;
}

/*
 * Set bitmap for every entry in nodelist also in the resp_node_hl
 * resp_node_hl IN - nodes in job's allocation
 * nodelist IN     - list of nodes to seek in resp_node_hl
 * bitmap_ptr OUT  - set bit for every entry in nodelist found
 * RET 1 if some nodelist record not found in resp_node_hl, otherwise zero
 */
static int 
_job_resp_bitmap(hostlist_t resp_node_hl, char *nodelist, 
		bitstr_t *bitmap_ptr)
{
	int  rc = 0;
	hostlist_t node_hl = hostlist_create(nodelist);
	char *node_name;

	while ((node_name = hostlist_shift(node_hl))) {
		int inx = hostlist_find(resp_node_hl, node_name);
		if (inx >= 0)
			bit_set(bitmap_ptr, inx);
		else
			rc = 1;
		free(node_name);
	}

	hostlist_destroy(node_hl);
	return rc;
}

static int _job_resp_count_max_tasks(resource_allocation_response_msg_t *resp)
{
	int inx, total = 0;

	for (inx=0; inx<resp->num_cpu_groups; inx++) {
		int tasks_per_node;
		tasks_per_node = resp->cpus_per_node[inx] / opt.cpus_per_task;
		total += (tasks_per_node * resp->cpu_count_reps[inx]);
	}
	return total;
}

/* Build an updated resource_allocation_response_msg 
 * including only nodes for which req_bitmap is set */
static void
_job_resp_hack(resource_allocation_response_msg_t *resp, bitstr_t *req_bitmap)
{
	hostlist_t old_hl = hostlist_create(resp->node_list);
	hostlist_t new_hl = hostlist_create("");
	char *new_node_list;	/* assigned list of nodes */
	slurm_addr *new_node_addr;	/* network addresses */
	uint32_t *new_cpus_per_node;/* cpus per node */
	uint32_t *new_cpu_count_reps;/* how many nodes have same cpu count */
	int new_node_cnt = bit_set_count(req_bitmap);
	int old_inx, new_inx = 0, i;

	/* Build updated response data structures */
	new_node_addr      = xmalloc(sizeof(slurm_addr) * new_node_cnt);
	new_cpus_per_node  = xmalloc(sizeof(uint32_t)   * new_node_cnt);
	new_cpu_count_reps = xmalloc(sizeof(uint32_t)   * new_node_cnt);
	for (old_inx=0; old_inx<resp->node_cnt; old_inx++) {
		char *node = hostlist_shift(old_hl);
		if (!bit_test(req_bitmap, old_inx)) {
			free(node);
			continue;
		}
		hostlist_push_host(new_hl, node);
		free(node);
		
		memcpy(new_node_addr+new_inx, resp->node_addr+old_inx, 
		       sizeof(slurm_addr));

		new_cpus_per_node[new_inx]  = 
			_job_resp_cpus(resp->cpus_per_node, 
			               resp->cpu_count_reps, old_inx);
		new_cpu_count_reps[new_inx] = 1;
		new_inx++;
	}

	/* Update the response */
	resp->node_cnt = new_node_cnt;

	hostlist_sort(new_hl);
	i = 64;
	new_node_list = xmalloc(i);
	while (hostlist_ranged_string(new_hl, i, new_node_list) == -1) {
		i *= 2;
		xrealloc(new_node_list, i);
	}
	xfree(resp->node_list);
	resp->node_list = new_node_list;
	hostlist_destroy(old_hl);
	hostlist_destroy(new_hl);

	xfree(resp->node_addr);
	resp->node_addr = new_node_addr;

	resp->num_cpu_groups = new_node_cnt;
	xfree(resp->cpus_per_node);
	resp->cpus_per_node  = new_cpus_per_node;
	xfree(resp->cpu_count_reps);
	resp->cpu_count_reps = new_cpu_count_reps;
}

static int 
_job_resp_cpus(uint32_t *cpus_per_node, uint32_t *cpu_count_reps, int node)
{
	int inx, total = 0;

	for (inx=0; ; inx++) {
		total += cpu_count_reps[inx];
		if (node < total)
			return cpus_per_node[inx];
	}
}


static char *
_normalize_hostlist(const char *hostlist)
{
	hostlist_t hl = hostlist_create(hostlist);
	char buf[4096];

	if (!hl ||  (hostlist_ranged_string(hl, 4096, buf) < 0))
		return xstrdup(hostlist);

	return xstrdup(buf);
}
