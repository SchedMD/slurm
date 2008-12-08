/*****************************************************************************\
 *  step_ctx.h - step context declarations
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>,
 *  Christopher J. Morrone <morrone2@llnl.gov>
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#ifndef _STEP_CTX_H
#define _STEP_CTX_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdint.h>

#include <slurm/slurm.h>

#include "src/api/step_launch.h"

#define STEP_CTX_MAGIC 0xc7a3

struct slurm_step_ctx_struct {
	uint16_t magic;	/* magic number */

	uint32_t job_id;	/* assigned job id */
	uint32_t user_id;	/* user the job runs as */

	job_step_create_request_msg_t *step_req;
	job_step_create_response_msg_t *step_resp;

	/* Used by slurm_step_launch() */
	struct step_launch_state *launch_state;
	uint16_t verbose_level; /* for extra logging decisions in step
				 * launch api */
};

#endif /* _STEP_CTX_H */

