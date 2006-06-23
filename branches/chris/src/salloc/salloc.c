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

#include <unistd.h>
#include <sys/types.h>

#include <slurm/slurm.h>

#include "src/salloc/opt.h"

int main(int argc, char *argv[])
{
	job_desc_msg_t desc;
	resource_allocation_response_msg_t *alloc;

	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("salloc parameter parsing");
	}

	slurm_init_job_desc_msg(&desc);
	desc.user_id = getuid();
	desc.group_id = getgid();
	desc.min_nodes = opt.min_nodes;
	desc.name = opt.job_name;
	desc.immediate = opt.immediate;

	alloc = slurm_allocate_resources_blocking(&desc, 0);
	if (alloc == NULL) 
		fatal("Failed to allocate resources: %m");

	sleep(5);

	if (slurm_complete_job(alloc->job_id, 0) != 0)
		fatal("Unable to clean up job allocation %d: %m",
		      alloc->job_id);

	slurm_free_resource_allocation_response_msg(alloc);
}

