/*****************************************************************************\
 *  checkpoint.h - implementation-independent checkpoint API definitions. 
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#ifndef _HAVE_SLURM_CHECKPOINT_H__
#define _HAVE_SLURM_CHECKPOINT_H__

#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"

/* Define checkpoint options */
enum check_opts {
	CHECK_ABLE,		/* able to checkpoint now */
	CHECK_DISABLE,		/* disable checkpointing */
	CHECK_ENABLE,		/* enable checkpointing */
	CHECK_CREATE,		/* create a checkpoint for this job, 
				 * job continues execution afterwards */
	CHECK_VACATE,		/* create a checkpoint for this job,
				 * job terminates afterwards */
	CHECK_RESTART,		/* restart a previously checkpointed job */
	CHECK_ERROR		/* get error info */
};

/* opaque data structures - no peeking! */
#ifndef __check_jobinfo_t_defined
#  define __check_jobinfo_t_defined
   typedef struct check_jobinfo *check_jobinfo_t;
#endif
typedef struct slurm_checkpoint_context * slurm_checkpoint_context_t;

/* initialize checkpoint plugin */
extern int checkpoint_init(char *checkpoint_type);

/* shutdown checkpoint plugin */
extern int checkpoint_fini(void);

/* perform many checkpoint operation on job/step */
extern int checkpoint_op(uint32_t job_id, uint32_t step_id, 
			 void *step_ptr, uint16_t op,
			 uint16_t data, char *image_dir, time_t *event_time,
			 uint32_t *error_code, char **error_msg);

/* note checkpoint completion */
extern int checkpoint_comp(void * step_ptr, time_t event_time, uint32_t error_code,
		char *error_msg);

extern int checkpoint_task_comp(void * step_ptr, uint32_t task_id, 
			time_t event_time, uint32_t error_code, char *error_msg);

/* gather checkpoint error info */
extern int checkpoint_error(void * step_ptr, 
		uint16_t *ckpt_errno, char **ckpt_strerror);

/* allocate and initialize a job step's checkpoint context */
extern int checkpoint_alloc_jobinfo(check_jobinfo_t *jobinfo);

/* free storage for a job step's checkpoint context */
extern int checkpoint_free_jobinfo(check_jobinfo_t jobinfo);

/* un/pack a job step's checkpoint context */
extern int  checkpoint_pack_jobinfo  (check_jobinfo_t jobinfo, Buf buffer);
extern int  checkpoint_unpack_jobinfo  (check_jobinfo_t jobinfo, Buf buffer);

/* create the necessary threads before forking the tasks */
extern int checkpoint_stepd_prefork (void *slurmd_job);

/* send the checkpoint request to the tasks */
extern int checkpoint_signal_tasks (void *slurmd_job, char *image_dir);

/* restart the requested job task */
extern int checkpoint_restart_task(void *slurmd_job, char *image_dir, int gtid);

/* send checkpoint request to specified job/step */
extern int checkpoint_tasks (uint32_t job_id, uint32_t step_id, 
			     time_t begin_time, char *image_dir, 
			     uint16_t wait, char *nodelist);
#endif /*_HAVE_SLURM_CHECKPOINT_H__*/

