/*****************************************************************************\
 * fname.c - IO filename type implementation (slaunch specific)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-226842.
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

#ifndef _SLAUNCH_FNAME_H
#define _SLAUNCH_FNAME_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif 

enum io_t {
	IO_ALL		= 0, /* multiplex output from all/bcast stdin to all */
	IO_ONE 	        = 1, /* output from only one task/stdin to one task  */
	IO_NONE		= 2, /* close output/close stdin                     */
};

typedef struct fname {
	char      *name;
	enum io_t  type;
	int        taskid;  /* taskid for IO if IO_ONE */
} fname_t;

/*
 * Create an filename from a (probably user supplied) filename format.
 * fname_create() will expand the format as much as possible for slaunch,
 * leaving node or task specific format specifiers for the remote 
 * slurmd to handle.
 */
fname_t *fname_create(char *format, int jobid, int stepid);
void fname_destroy(fname_t *fname);

#endif /* !_SLAUNCH_FNAME_H */

