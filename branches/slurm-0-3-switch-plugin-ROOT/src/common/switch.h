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
extern int  g_switch_init   (void);

/* save any global switch state to a file within the specified directory
 * the actual file name used in plugin specific
 * IN dir_name - directory into which switch state is saved
 * RET         - slurm error code
 */
extern int  g_switch_save   (char *dir_name);

/* restore any global switch state from a file within the specified directory
 * the actual file name used in plugin specific
 * IN dir_name - directory from hich switch state is restored or NULL for 
 *               switch restart with no state restored
 * RET         - slurm error code
 */
extern int  g_switch_restore(char *dir_name);

/******************************************************\
 * JOB-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

/* allocate storage for a switch job credential
 * OUT jobinfo - storage for a switch job credential
 * RET         - slurm error code
 * NOTE: storage must be freed using g_switch_free_jobinfo
 */
extern int  g_switch_alloc_jobinfo (switch_jobinfo_t *jobinfo);

/* allocated and fill a job's switch credential
 * OUT jobinfo  - storage for a switch job credential
 * IN  nodelist - list of nodes to be used by the job
 * IN  nprocs   - count of tasks in the job
 * IN  cyclic_alloc - task distribution pattern, 1=cyclic, 0=block
 * NOTE: storage must be freed using g_switch_free_jobinfo
 */
extern int  g_switch_build_jobinfo (switch_jobinfo_t *jobinfo, 
		char *nodelist, int nprocs, int cyclic_alloc);

/* copy a switch job credential
 * IN jobinfo - the switch job credential to be copied
 * RET        - the copy
 * NOTE: returned value must be freed using g_switch_free_jobinfo
 */
extern switch_jobinfo_t g_switch_copy_jobinfo(switch_jobinfo_t jobinfo);

/* free storage previously allocated for a switch job credential
 * IN jobinfo  - the switch job credential to be freed
 * RET         - slurm error code
 */
extern int  g_switch_free_jobinfo  (switch_jobinfo_t jobinfo);

/* pack a switch job credential into a buffer in machine independent form
 * IN jobinfo  - the switch job credential to be saved
 * OUT buffer  - buffer with switch credential appended
 * RET         - slurm error code
 */
extern int  g_switch_pack_jobinfo  (switch_jobinfo_t jobinfo, Buf buffer);

/* unpack a switch job credential from a buffer
 * OUT jobinfo - the switch job credential read
 * IN  buffer  - buffer with switch credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using g_switch_free_jobinfo
 */
extern int  g_switch_unpack_jobinfo(switch_jobinfo_t *jobinfo, Buf buffer);

/* write job credential string representation to a file
 * IN fp      - an open file pointer
 * IN jobinfo - a switch job credential
 */
extern void g_switch_print_jobinfo(FILE *fp, switch_jobinfo_t jobinfo);

/* write job credential to a string
 * IN jobinfo - a switch job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * RET        - the string, same as buf
 */
extern char *g_switch_sprint_jobinfo( switch_jobinfo_t jobinfo,
		                               char *buf, size_t size);

/********************************************************************\
 * JOB LAUNCH AND MANAGEMENT FUNCTIONS RELATED TO SWITCH CREDENTIAL *
\********************************************************************/

/* switch initialization prior to job launch, execute as user root 
 * before forking user tasks
 * IN jobinfo - the switch job credential to be disabled
 * IN uid     - the uid of user to use this credential
 * RET         - slurm error code
 */
extern int  g_switch_prog_init (switch_jobinfo_t jobinfo, uid_t uid);

/* lay claim to the switch resources, execute as the user and for 
 * each task individually
 * IN jobinfo - the switch job credential to be disabled
 * IN procnum - task number, zero origin
 * RET         - slurm error code
 */
extern int  g_switch_setcap (switch_jobinfo_t jobinfo, int procnum);

/* signal jobs having specified switch credential
 * IN jobinfo - the switch credential of interest
 * IN signal  - signal to send to all processes
 * RET        - slurm error code
 */
extern int  g_switch_prog_signal(switch_jobinfo_t jobinfo, int signal);

/* disable the switch job credential, call this after the program has
 * terminated, execute as the user
 * IN jobinfo - the switch job credential to be disabled
 */
extern void g_switch_prog_fini (switch_jobinfo_t jobinfo );

/* disable the switch credential, execute as user root
 * IN jobinfo - the switch credential of interest
 * RET        - slurm error code
 * NOTE: The existence of active programs still utilizing the switch 
 * resources is considered an error condition
 */
extern int  g_switch_prog_destroy(switch_jobinfo_t jobinfo);

#endif /* !_SWITCH_H */
