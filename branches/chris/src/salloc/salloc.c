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

#include "src/salloc/opt.h"

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
	desc.user_id = getuid();
	desc.group_id = getgid();
	desc.min_nodes = opt.min_nodes;
	desc.name = opt.job_name;
	desc.immediate = opt.immediate;

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
