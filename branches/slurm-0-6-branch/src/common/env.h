/*****************************************************************************\
 * src/common/env.h - environment vector manipulation
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
#ifndef _ENV_H
#define _ENV_H

#include <slurm/slurm.h>
#include <sys/utsname.h>

enum distribution_t {
	SRUN_DIST_BLOCK 	= 0, 
	SRUN_DIST_CYCLIC 	= 1,
	SRUN_DIST_UNKNOWN 	= 2
};

typedef struct env_options {
	int nprocs;		/* --nprocs=n,      -n n	*/
	char *task_count;
	bool nprocs_set;	/* true if nprocs explicitly set */
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	enum distribution_t
		distribution;	/* --distribution=, -m dist	*/
	bool overcommit;	/* --overcommit,   -O		*/
	int  slurmd_debug;	/* --slurmd-debug, -D           */
	bool labelio;		/* --label-output, -l		*/
	select_jobinfo_t select_jobinfo;
	int nhosts;
	char *nodelist;		/* nodelist in string form */
	char **env;             /* job environment */
	slurm_addr *cli;
	slurm_addr *self;
	int jobid;		/* assigned job id */
	int stepid;	        /* assigned step id */
	int procid;		/* global task id (across nodes) */
	int localid;		/* local task id (within node) */
	int nodeid;
	int cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	int cpus_on_node;
} env_t;


int     envcount (char **env);
int     setenvfs(const char *fmt, ...);
int     setenvf(char ***envp, const char *name, const char *fmt, ...);
void	unsetenvp(char **env, const char *name);
char *	getenvp(char **env, const char *name);
int     setup_env(env_t *env);

#endif
