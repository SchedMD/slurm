/*****************************************************************************\
 *  src/common/env.h - environment vector manipulation
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _ENV_H
#define _ENV_H

#include <sys/types.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <sys/utsname.h>

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"

typedef struct env_options {
	int nprocs;		/* --nprocs=n,      -n n	*/
	char *task_count;
	bool nprocs_set;	/* true if nprocs explicitly set */
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	task_dist_states_t distribution; /* --distribution=, -m dist	*/
	int plane_size;         /* plane_size for SLURM_DIST_PLANE */
	cpu_bind_type_t
		cpu_bind_type;	/* --cpu_bind=			*/
	char *cpu_bind;		/* binding map for map/mask_cpu	*/
	mem_bind_type_t
		mem_bind_type;	/* --mem_bind=			*/
	char *mem_bind;		/* binding map for tasks to memory	*/
	bool overcommit;	/* --overcommit,   -O		*/
	int  slurmd_debug;	/* --slurmd-debug, -D           */
	bool labelio;		/* --label-output, -l		*/
	select_jobinfo_t select_jobinfo;
	int nhosts;
	char *nodelist;		/* nodelist in string form */
	char **env;             /* job environment */
	uint16_t comm_port;	/* srun's communication port */
	char *comm_hostname;	/* srun's hostname */
	slurm_addr *cli;	/* launch node address */
	slurm_addr *self;
	int jobid;		/* assigned job id */
	int stepid;	        /* assigned step id */
	int procid;		/* global task id (across nodes) */
	int localid;		/* local task id (within node) */
	int nodeid;
	int cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	int ntasks_per_node;	/* --ntasks-per-node=n		*/
	int ntasks_per_socket;	/* --ntasks-per-socket=n	*/
	int ntasks_per_core;	/* --ntasks-per-core=n		*/
	int cpus_on_node;
	pid_t task_pid;
	char *sgtids;		/* global ranks array of integers */	
	uint16_t pty_port;	/* used to communicate window size changes */
	uint8_t ws_col;		/* window size, columns */
	uint8_t ws_row;		/* window size, row count */
	char *ckpt_path;	/* --ckpt-path=                 */
} env_t;


int     envcount (char **env);
int     setenvfs(const char *fmt, ...);
int     setenvf(char ***envp, const char *name, const char *fmt, ...);
void	unsetenvp(char **env, const char *name);
char *	getenvp(char **env, const char *name);
int     setup_env(env_t *env);

/**********************************************************************
 * Newer environment variable handling scheme
 **********************************************************************/
/*
 * Set in "dest" the environment variables relevant to a SLURM job
 * allocation, overwriting any environment variables of the same name.
 * If the address pointed to by "dest" is NULL, memory will automatically be
 * xmalloc'ed.  The array is terminated by a NULL pointer, and thus is
 * suitable for use by execle() and other env_array_* functions.
 *
 * Sets the variables:
 *	SLURM_JOB_ID
 *	SLURM_JOB_NUM_NODES
 *	SLURM_JOB_NODELIST
 *	SLURM_JOB_CPUS_PER_NODE
 *	LOADLBATCH (AIX only)
 *
 * Sets OBSOLETE variables:
 *	? probably only needed for users...
 */
void env_array_for_job(char ***dest,
		       const resource_allocation_response_msg_t *alloc,
		       const job_desc_msg_t *desc);

/*
 * Set in "dest" the environment variables relevant to a SLURM batch
 * job allocation, overwriting any environment variables of the same name.
 * If the address pointed to by "dest" is NULL, memory will automatically be
 * xmalloc'ed.  The array is terminated by a NULL pointer, and thus is
 * suitable for use by execle() and other env_array_* functions.
 *
 * Sets the variables:
 *	SLURM_JOB_ID
 *	SLURM_JOB_NUM_NODES
 *	SLURM_JOB_NODELIST
 *	SLURM_JOB_CPUS_PER_NODE
 *	ENVIRONMENT=BATCH
 *	HOSTNAME
 *	LOADLBATCH (AIX only)
 *
 * Sets OBSOLETE variables:
 *	SLURM_JOBID
 *	SLURM_NNODES
 *	SLURM_NODELIST
 *	SLURM_TASKS_PER_NODE <- poorly named, really CPUs per node
 *	? probably only needed for users...
 */
extern void env_array_for_batch_job(char ***dest, 
				    const batch_job_launch_msg_t *batch,
				    const char* node_name);

/*
 * Set in "dest the environment variables relevant to a SLURM job step,
 * overwriting any environment variables of the same name.  If the address
 * pointed to by "dest" is NULL, memory will automatically be xmalloc'ed.
 * The array is terminated by a NULL pointer, and thus is suitable for
 * use by execle() and other env_array_* functions.
 *
 * Sets variables:
 *	SLURM_STEP_ID
 *	SLURM_STEP_NUM_NODES
 *	SLURM_STEP_NUM_TASKS
 *	SLURM_STEP_TASKS_PER_NODE
 *	SLURM_STEP_LAUNCHER_HOSTNAME
 *	SLURM_STEP_LAUNCHER_PORT
 *	SLURM_STEP_LAUNCHER_IPADDR
 *
 * Sets OBSOLETE variables:
 *	SLURM_STEPID
 *      SLURM_NNODES
 *	SLURM_NPROCS
 *	SLURM_NODELIST
 *	SLURM_TASKS_PER_NODE
 *	SLURM_SRUN_COMM_HOST
 *	SLURM_SRUN_COMM_PORT
 *	SLURM_LAUNCH_NODE_IPADDR
 *
 */
void
env_array_for_step(char ***dest,
		   const job_step_create_response_msg_t *step,
		   const char *launcher_hostname,
		   uint16_t launcher_port);

/*
 * Return an empty environment variable array (contains a single
 * pointer to NULL).
 */
char **env_array_create(void);

/*
 * Merge all of the environment variables in src_array into the
 * array dest_array.  Any variables already found in dest_array
 * will be overwritten with the value from src_array.
 */
void env_array_merge(char ***dest_array, const char **src_array);

/* 
 * Copy env_array must be freed by env_array_free 
 */
char **env_array_copy(const char **array);

/*
 * Free the memory used by an environment variable array.
 */
void env_array_free(char **env_array);

/*
 * Append a single environment variable to an environment variable array,
 * if and only if a variable by that name does not already exist in the
 * array.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_append(char ***array_ptr, const char *name,
		     const char *value);

/*
 * Append a single environment variable to an environment variable array,
 * if and only if a variable by that name does not already exist in the
 * array.
 *
 * "value_fmt" supports printf-style formatting.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_append_fmt(char ***array_ptr, const char *name,
			 const char *value_fmt, ...);

/*
 * Append a single environment variable to an environment variable array
 * if a variable by that name does not already exist.  If a variable
 * by the same name is found in the array, it is overwritten with the
 * new value.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_overwrite(char ***array_ptr, const char *name,
			const char *value);

/*
 * Append a single environment variable to an environment variable array
 * if a variable by that name does not already exist.  If a variable
 * by the same name is found in the array, it is overwritten with the
 * new value.  The "value_fmt" string may contain printf-style options.
 *
 * "value_fmt" supports printf-style formatting.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_overwrite_fmt(char ***array_ptr, const char *name,
			    const char *value_fmt, ...);

/*
 * Set in the running process's environment all of the environment
 * variables in a supplied environment variable array.
 */
void env_array_set_environment(char **env_array);

/*
 * Return an array of strings representing the specified user's default
 * environment variables following a two-prongged approach.
 * 1. Execute (more or less): "/bin/su - <username> -c /usr/bin/env"
 *    Depending upon the user's login scripts, this may take a very
 *    long time to complete or possibly never return
 * 2. Load the user environment from a cache file. This is used
 *    in the event that option 1 times out.
 *
 * timeout value is in seconds or zero for default (8 secs)
 * mode is 1 for short ("su <user>"), 2 for long ("su - <user>")
 * On error, returns NULL.
 *
 * NOTE: The calling process must have an effective uid of root for
 * this function to succeed.
 */
char **env_array_user_default(const char *username, int timeout, int mode);

/*
 * The cpus-per-node representation in SLURM (and perhaps tasks-per-node
 * in the future) is stored in a compressed format comprised of two
 * equal-length arrays of uint32_t, and an integer holding the array length.
 * In one array an element represents a count (number of cpus, number of tasks,
 * etc.), and the corresponding element in the other array contains the
 * number of times the count is repeated sequentially in the uncompressed
 * something-per-node array.
 *
 * This function returns the string representation of the compressed
 * array.  Free with xfree().
 */
char *uint32_compressed_to_str(uint32_t array_len,
			       const uint32_t *array,
			       const uint32_t *array_reps);

#endif
