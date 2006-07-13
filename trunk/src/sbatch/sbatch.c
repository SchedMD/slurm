/*****************************************************************************\
 *  sbatch.c - Submit a SLURM batch script.
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

#include <slurm/slurm.h>

#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/env.h"

#include "src/sbatch/opt.h"

static int fill_job_desc_from_opts(job_desc_msg_t *desc);
static char *xget_script_string(void);

int main(int argc, char *argv[])
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t desc;
	submit_response_msg_t *resp;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("sbatch parameter parsing");
	}
	/* reinit log with new verbosity (if changed by command line) */
	if (_verbose || opt.quiet) {
		logopt.stderr_level += _verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	slurm_init_job_desc_msg(&desc);
	if (fill_job_desc_from_opts(&desc) == -1) {
		exit(1);
	}

	desc.script = xget_script_string();
	if (desc.script == NULL)
		exit(2);

	if (slurm_submit_batch_job(&desc, &resp) == -1) {
		error("Batch job submission failed: %m");
		exit(3);
	}

	info("Submitted batch job %d", resp->job_id);
	xfree(desc.script);
	slurm_free_submit_response_response_msg(resp);

	return 0;
}

/* Returns 0 on success, -1 on failure */
static int fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	extern char **environ;

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

	desc->environment = environ;
	desc->env_size = envcount (environ);
/* 	desc->argv = remote_argv; */
/* 	desc->argc = remote_argc; */
/* 	desc->err  = opt.efname; */
/* 	desc->in   = opt.ifname; */
/* 	desc->out  = opt.ofname; */
	desc->work_dir = opt.cwd;
	desc->no_requeue = opt.no_requeue;

	return 0;
}

static char *script_from_stream(FILE *fs)
{
	char *script = NULL;
	char buf[1024];

	while(fgets(buf, 1024, fs) != NULL) {
		xstrcat(script, buf);
	}

	return script;
}

static char *xget_script_string(void)
{
	FILE* fs;
	char *script;

	if (remote_argv == NULL || remote_argv[0] == NULL) {
		fs = stdin;
	} else {
		fs = fopen(remote_argv[0], "r");
		if (fs == NULL) {
			error("Unable to open file %s", remote_argv[0]);
			return NULL;
		}
	}	
	script = script_from_stream(fs);
	fclose(fs);

	return script;
}
