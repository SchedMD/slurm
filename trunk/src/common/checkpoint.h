/*****************************************************************************\
 *  checkpoint.h - implementation-independent checkpoint API definitions. 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.com>
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

#ifndef __CHECKPOINT_H__
#define __CHECKPOINT_H__

#include "slurm/slurm.h"
#include "src/slurmctld/slurmctld.h"

#define CHECK_ERROR 100		/* Used like enum checkopts, but not exported to user */

typedef struct slurm_checkpoint_context * slurm_checkpoint_context_t;

extern int g_slurm_checkpoint_init(void);
extern void g_slurm_checkpoint_fini(void);
extern int g_slurm_checkpoint_op(enum check_opts op, struct step_record * step_ptr);
extern int g_slurm_checkpoint_error(struct step_record * step_ptr, 
		uint32_t *ckpt_errno, char **ckpt_strerror);

#endif /*__CHECKPOINT_H__*/

