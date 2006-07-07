/*****************************************************************************\
 *  salloc.c - Request a SLURM job allocation and
 *             launch a user-specified command.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  UCRL-CODE-217948.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include <slurm/slurm.h>

#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/salloc/opt.h"

static int fill_job_desc_from_opts(job_desc_msg_t *desc);
static void ring_terminal_bell(void);
static void run_command(void);

int main(int argc, char *argv[])
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t desc;
	resource_allocation_response_msg_t *alloc;
	time_t before, after;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("salloc parameter parsing");
	}
	/* reinit log with new verbosity (if changed by command line) */
	if (_verbose || opt.quiet) {
		logopt.stderr_level += _verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	/*
	 * Request a job allocation
	 */
	slurm_init_job_desc_msg(&desc);
	if (fill_job_desc_from_opts(&desc) == -1) {
		exit(1);
	}

	before = time(NULL);
	alloc = slurm_allocate_resources_blocking(&desc, 0);
	if (alloc == NULL) 
		fatal("Failed to allocate resources: %m");
	after = time(NULL);

	/*
	 * Allocation granted!
	 */
	info("Granted job allocation %d", alloc->job_id);
	if (opt.bell == BELL_ALWAYS
	    || (opt.bell == BELL_AFTER_DELAY
		&& ((after - before) > DEFAULT_BELL_DELAY))) {
		ring_terminal_bell();
	}

	/*
	 * Run the user's command.
	 */
	setenvfs("SLURM_JOBID=%d", alloc->job_id);
	setenvfs("SLURM_NNODES=%d", alloc->node_cnt);
	run_command();

	/*
	 * Relinquish the job allocation.
	 */
	info("Relinquishing job allocation %d", alloc->job_id);
	if (slurm_complete_job(alloc->job_id, 0) != 0)
		fatal("Unable to clean up job allocation %d: %m",
		      alloc->job_id);

	slurm_free_resource_allocation_response_msg(alloc);

	return 0;
}


/* Returns 0 on success, -1 on failure */
static int fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	desc->contiguous = opt.contiguous ? 1 : 0;
	desc->features = opt.constraints;
	desc->immediate = opt.immediate;
	desc->name = opt.job_name;
	desc->req_nodes = opt.nodelist;
	if (desc->req_nodes == NULL) {
		char *nodelist = NULL;
		char *hostfile = getenv("SLURM_HOSTFILE");
		
		if (hostfile != NULL) {
			nodelist = slurm_read_hostfile(hostfile, opt.nprocs);
			if (nodelist == NULL) {
				error("Failure getting NodeNames from "
				      "hostfile");
				return -1;
			} else {
				debug("loading nodes from hostfile %s",
				      hostfile);
				desc->req_nodes = xstrdup(nodelist);
				free(nodelist);
			}
		}
	}
	desc->exc_nodes = opt.exc_nodes;
	desc->partition = opt.partition;
	desc->min_nodes = opt.min_nodes;
	if (opt.max_nodes)
		desc->max_nodes = opt.max_nodes;
	desc->user_id = opt.uid;
	desc->group_id = opt.gid;
	desc->dependency = opt.dependency;
	if (opt.nice)
		desc->nice = NICE_OFFSET + opt.nice;
	desc->exclusive = opt.exclusive;
	desc->mail_type = opt.mail_type;
	if (opt.mail_user)
		desc->mail_user = xstrdup(opt.mail_user);
	if (opt.begin)
		desc->begin_time = opt.begin;
	if (opt.network)
		desc->network = xstrdup(opt.network);
	if (opt.account)
		desc->account = xstrdup(opt.account);

	if (opt.hold)
		desc->priority     = 0;
#if SYSTEM_DIMENSIONS
	if (opt.geometry[0] > 0) {
		int i;
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			desc->geometry[i] = opt.geometry[i];
	}
#endif
	if (opt.conn_type != -1)
		desc->conn_type = opt.conn_type;
	if (opt.no_rotate)
		desc->rotate = 0;
	if (opt.mincpus > -1)
		desc->min_procs = opt.mincpus;
	if (opt.realmem > -1)
		desc->min_memory = opt.realmem;
	if (opt.tmpdisk > -1)
		desc->min_tmp_disk = opt.tmpdisk;
	if (opt.overcommit) { 
		desc->num_procs = opt.min_nodes;
		desc->overcommit = opt.overcommit;
	} else
		desc->num_procs = opt.nprocs * opt.cpus_per_task;
	if (opt.nprocs_set)
		desc->num_tasks = opt.nprocs;
	if (opt.cpus_set)
		desc->cpus_per_task = opt.cpus_per_task;
	if (opt.no_kill)
		desc->kill_on_node_fail = 0;
	if (opt.time_limit > -1)
		desc->time_limit = opt.time_limit;
	if (opt.share)
		desc->shared = 1;

/* We want to support the pinger here */
/* 	desc->other_port = slurmctld_comm_addr.port; */
/*	desc->other_hostname = xstrdup(slurmctld_comm_addr.hostname); */

	return 0;
}

static void ring_terminal_bell(void)
{
	fprintf(stdout, "\a");
	fflush(stdout);
}

static void run_command(void)
{
	pid_t pid;
	int status;
	int rc;

	pid = fork();
	if (pid < 0) {
		error("fork failed: %m");
	} else if (pid > 0) {
		/* parent */
		while ((rc = waitpid(pid, &status, 0)) == -1) {
			if (errno != EINTR) {
				error("waitpid failed: %m");
				break;
			}
		}
	} else {
		/* child */
		execvp(command_argv[0], command_argv);
	}
}
