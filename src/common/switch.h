/*****************************************************************************\
 * src/common/switch.h - Generic switch (interconnect) info for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>.
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

#ifndef _SWITCH_H
#define _SWITCH_H 	1

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"
#include "src/common/pack.h"

/* opaque data structures - no peeking! */
#ifndef __switch_jobinfo_t_defined
#  define __switch_jobinfo_t_defined
   typedef struct switch_jobinfo *switch_jobinfo_t;
#endif
typedef struct slurm_switch_context * slurm_switch_context_t;

/*****************************************\
 * GLOBAL SWITCH STATE MANGEMENT FUNCIONS*
\ *****************************************/

/* initialize the switch plugin */
extern int  switch_init   (void);

/* save any global switch state to a file within the specified directory
 * the actual file name used in plugin specific
 * IN dir_name - directory into which switch state is saved
 * RET         - slurm error code
 */
extern int  switch_save   (char *dir_name);

/* restore any global switch state from a file within the specified directory
 * the actual file name used in plugin specific
 * IN dir_name - directory from hich switch state is restored or NULL for 
 *               switch restart with no state restored
 * RET         - slurm error code
 */
extern int  switch_restore(char *dir_name);

/* report if resource fragmentation is important. if so, delay scheduling a 
 * new job while another is in the process of terminating.
 * RET          - true if fragmentation is important
 */
extern bool switch_no_frag(void);

/******************************************************\
 * JOB-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

/* allocate storage for a switch job credential
 * OUT jobinfo - storage for a switch job credential
 * RET         - slurm error code
 * NOTE: storage must be freed using g_switch_free_jobinfo
 */
extern int  switch_alloc_jobinfo (switch_jobinfo_t *jobinfo);

/* fill a job's switch credential
 * OUT jobinfo  - storage for a switch job credential
 * IN  nodelist - list of nodes to be used by the job
 * IN  nprocs   - count of tasks in the job
 * IN  cyclic_alloc - task distribution pattern, 1=cyclic, 0=block
 * NOTE: storage must be freed using g_switch_free_jobinfo
 */
extern int  switch_build_jobinfo (switch_jobinfo_t jobinfo, 
		char *nodelist, int nprocs, int cyclic_alloc);

/* copy a switch job credential
 * IN jobinfo - the switch job credential to be copied
 * RET        - the copy
 * NOTE: returned value must be freed using g_switch_free_jobinfo
 */
extern switch_jobinfo_t switch_copy_jobinfo(switch_jobinfo_t jobinfo);

/* free storage previously allocated for a switch job credential
 * IN jobinfo  - the switch job credential to be freed
 */
extern void switch_free_jobinfo  (switch_jobinfo_t jobinfo);

/* pack a switch job credential into a buffer in machine independent form
 * IN jobinfo  - the switch job credential to be saved
 * OUT buffer  - buffer with switch credential appended
 * RET         - slurm error code
 */
extern int  switch_pack_jobinfo  (switch_jobinfo_t jobinfo, Buf buffer);

/* unpack a switch job credential from a buffer
 * OUT jobinfo - the switch job credential read
 * IN  buffer  - buffer with switch credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using g_switch_free_jobinfo
 */
extern int  switch_unpack_jobinfo(switch_jobinfo_t jobinfo, Buf buffer);

/* write job credential string representation to a file
 * IN fp      - an open file pointer
 * IN jobinfo - a switch job credential
 */
extern void switch_print_jobinfo(FILE *fp, switch_jobinfo_t jobinfo);

/* write job credential to a string
 * IN jobinfo - a switch job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * RET        - the string, same as buf
 */
extern char *switch_sprint_jobinfo( switch_jobinfo_t jobinfo,
			char *buf, size_t size);
/********************************************************************\
 * JOB LAUNCH AND MANAGEMENT FUNCTIONS RELATED TO SWITCH CREDENTIAL *
\********************************************************************/

/*
 * Setup node for interconnect use.
 *
 * This function is run from the top level slurmd only once per
 * slurmd run. It may be used, for instance, to perform some one-time
 * interconnect setup or spawn an error handling thread.
 *
 */
extern int interconnect_node_init(void);

/*
 * Finalize interconnect on node. 
 *
 * This function is called once as slurmd exits (slurmd will wait for
 * this function to return before continuing the exit process)
 */
extern int interconnect_node_fini(void);


/*
 * Notes on job related interconnect functions:
 *
 * Interconnect functions are run within slurmd in the following way:
 * (Diagram courtesy of Jim Garlick [see qsw.c] )
 *
 *  Process 1 (root)        Process 2 (root, user)  |  Process 3 (user task)
 *                                                  |
 *  interconnect_preinit                            |
 *  fork ------------------ interconnect_init       |
 *  waitpid                 setuid, chdir, etc.     |
 *                          fork N procs -----------+--- interconnect_attach
 *                          wait all                |    exec mpi process
 *                          interconnect_fini*      |
 *  interconnect_postfini                           |    
 *                                                  |
 *
 * [ *Note: interconnect_fini() is run as the uid of the job owner, not root ]
 */

/*
 * Prepare node for job. 
 *
 * pre is run as root in the first slurmd process, the so called job
 * manager. This function can be used to perform any initialization
 * that needs to be performed in the same process as interconnect_fini()
 * 
 */
extern int interconnect_preinit(switch_jobinfo_t jobinfo);

/* 
 * initialize interconnect on node for job. This function is run from the 
 * 2nd slurmd process (some interconnect implementations may require
 * interconnect init functions to be executed from a separate process
 * than the process executing interconnect_fini() [e.g. QsNet])
 *
 */
extern int interconnect_init(switch_jobinfo_t jobinfo, uid_t uid);

/*
 * This function is run from the same process as interconnect_init()
 * after all job tasks have exited. It is *not* run as root, because
 * the process in question has already setuid to the job owner.
 *
 */
extern int interconnect_fini(switch_jobinfo_t jobinfo);

/*
 * Finalize interconnect on node.
 *
 * This function is run from the initial slurmd process (same process
 * as interconnect_preinit()), and is run as root. Any cleanup routines
 * that need to be run with root privileges should be run from this
 * function.
 */
extern int interconnect_postfini(switch_jobinfo_t jobinfo, uid_t pgid, 
				uint32_t job_id, uint32_t step_id );

/* 
 * attach process to interconnect
 * (Called from within the process, so it is appropriate to set 
 * interconnect specific environment variables here)
 */
extern int interconnect_attach(switch_jobinfo_t jobinfo, char ***env,
		int nodeid, int procid, int nnodes, int nprocs,
		gid_t gid);

#endif /* _SWITCH_H */
