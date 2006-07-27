/*****************************************************************************\
 *  sattach.c - Attach to a running job step.
 *
 *  $Id: sattach.c 8447 2006-06-26 22:29:29Z morrone $
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
#include <stdint.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/hostlist.h"

#include "src/sattach/opt.h"

void print_layout_info(slurm_step_layout_t *layout);

int main(int argc, char *argv[])
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_step_layout_t *layout;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("salloc parameter parsing");
	}
	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	layout = slurm_job_step_layout_get(opt.jobid, opt.stepid);
	if (layout == NULL) {
		error("Could not get job step info: %m");
		return 1;
	}

	print_layout_info(layout);

	slurm_job_step_layout_free(layout);

	return 0;
}

void print_layout_info(slurm_step_layout_t *layout)
{
	hostlist_t nl;
	int i, j;

	info("node count = %d", layout->node_cnt);
	info("total task count = %d", layout->task_cnt);
	info("node names = \"%s\"", layout->node_list);
	nl = hostlist_create(layout->node_list);
	for (i = 0; i < layout->node_cnt; i++) {
		char *name = hostlist_nth(nl, i);
		info("%s: node %d, tasks %d", name, i, layout->tasks[i]);
		for (j = 0; j < layout->tasks[i]; j++) {
			info("\ttask %d", layout->tids[i][j]);
		}
		free(name);
	}
}
