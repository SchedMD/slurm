/*****************************************************************************\
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
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

#ifndef _SLURMSTEPD_DIST_TASKS_H
#define _SLURMSTEPD_DIST_TASKS_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <signal.h>
#include <sys/types.h>
#include <grp.h>
#include <stdlib.h>

#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/eio.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"

#include "src/common/bitstring.h"

#include "src/slurmd/slurmd/slurmd.h"

/* Structures to create an object oriented version of a 4-D 
   infrastructure --> task id mapping [node][cpu][core][taskid] = tid
*/
struct thread_gids {
  int *gids;  /* Taskids for a specific thread */
  int tasks;  /* Number of tasks for a specific thread */
};

struct core_gids {
  struct thread_gids *threads; /* Taskids for a specific thread */
};

struct socket_gids {
  struct core_gids *cores; /* Taskids for a specific core */
};

struct node_gids {
  struct socket_gids *sockets; /* Taskids for a specific CPU */
};

struct slurm_lllp_context {
#ifndef NDEBUG
#  define LLLP_CTX_MAGIC 0x0d0d0d
        int magic;
#endif
#if WITH_PTHREADS  
        pthread_mutex_t mutex;
#endif
        List           job_list;   /* List of job bindings */
};
typedef struct slurm_lllp_context slurm_lllp_ctx_t;

void cr_reserve_lllp(uint32_t job_id,
			launch_tasks_request_msg_t *req, uint32_t node_id);
void cr_release_lllp(uint32_t job_id);
void lllp_distribution(launch_tasks_request_msg_t *req, uint32_t node_id);
void lllp_ctx_destroy(void);
void lllp_ctx_alloc(void);
void get_bitmap_from_cpu_bind(bitstr_t *bitmap_test,
			      cpu_bind_type_t cpu_bind_type, 
			      char *cpu_bind, uint32_t numtasks);

#endif /* !_SLURMSTEPD_DIST_TASKS_H */

