/*****************************************************************************\
 *  src/slurmd/common/stepd_api.h - slurmd_step message API
 *  $Id: $
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#ifndef _STEPD_API_H
#define _STEPD_API_H

#include <inttypes.h>

#include "slurm/slurm.h"
#include "src/common/list.h"

typedef enum {
	REQUEST_SIGNAL_PROCESS_GROUP = 0,
	REQUEST_SIGNAL_TASK_LOCAL,
	REQUEST_SIGNAL_TASK_GLOBAL,
	REQUEST_SIGNAL_CONTAINER,
	REQUEST_STATUS,
	REQUEST_ATTACH,
} step_msg_t;

typedef struct step_location {
	uint32_t jobid;
	uint32_t stepid;
	char *nodename;
	char *directory;
} step_loc_t;

int stepd_status(step_loc_t step);

/*
 * Send a signal to the process group of a job step.
 */
int stepd_signal(step_loc_t step, void *auth_cred, int signal); 

/*
 * Send a signal to a single task in a job step.
 */
int stepd_signal_task_local(step_loc_t step, void *auth_cred,
			    int signal, int ltaskid); 

/*
 * Send a signal to a single task in a job step.
 */
int stepd_signal_task_global(step_loc_t step, void *auth_cred,
			     int signal, int gtaskid);

/*
 * Send a signal to the proctrack container of a job step.
 */
int stepd_signal_container(step_loc_t step, void *auth_cred, int signal);

int stepd_attach(step_loc_t step, slurm_addr *ioaddr, slurm_addr *respaddr,
		 void *auth_cred, slurm_cred_t job_cred);


/*
 * Scan for available running slurm step daemons by checking
 * "directory" for unix domain sockets with names beginning in "nodename".
 *
 * Returns a List of pointers to step_loc_t structures.
 */
List stepd_available(const char *directory, const char *nodename);

#endif /* _STEPD_API_H */
