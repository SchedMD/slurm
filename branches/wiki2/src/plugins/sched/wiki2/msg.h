/*****************************************************************************\
 *  msg.h - Message/communcation manager for Wiki plugin
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

/*
 * Two modes of operation are currently supported for job prioritization:
 *
 * PRIO_HOLD: Wiki is a polling scheduler, so the initial priority is always 
 * zero to keep SLURM from spontaneously starting the job.  The scheduler will 
 * suggest which job's priority should be made non-zero and thus allowed to 
 * proceed.
 *
 * PRIO_DECREMENT: Set the job priority to one less than the last job and let 
 * Wiki change priorities of jobs as desired to re-order the queue
 */
#define PRIO_HOLD      0
#define PRIO_DECREMENT 1
extern int	init_prio_mode;

extern char *	auth_key;

extern int	spawn_msg_thread(void);
extern void	term_msg_thread(void);
