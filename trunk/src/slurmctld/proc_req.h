/*****************************************************************************\
 *  proc_msg.h - process incomming message and timing functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov>, et. al.
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

#ifndef _HAVE_PROC_REQ_H
#define _HAVE_PROC_REQ_H

#include <sys/time.h>

#define PRINT_TIMES      1	/* Set to print timing info */

#if PRINT_TIMES
#	define DEF_TIMERS	struct timeval tv1, tv2; char tv_str[20]
#	define START_TIMER	gettimeofday(&tv1, NULL)
#	define END_TIMER	gettimeofday(&tv2, NULL); \
				diff_tv_str(&tv1, &tv2, tv_str, 20)
#	define TIME_STR 	tv_str
#else
#	define DEF_TIMERS	int tv1, tv2, tv_str
#	define START_TIMER	tv1 = 0
#	define END_TIMER	tv2 = tv_str = 0
#	define TIME_STR 	""
#endif

/*
 * diff_tv_str - build a string showing the time difference between two times
 * tv1 IN - start of event
 * tv2 IN - end of event
 * tv_str OUT - place to put delta time in format "usec=%ld"
 * len_tv_str IN - size of tv_str in bytes
 */
extern inline void diff_tv_str(struct timeval *tv1,struct timeval *tv2, 
		char *tv_str, int len_tv_str);

/*
 * slurmctld_req  - Process an individual RPC request
 * IN/OUT - the request message, data associated with the message is freed
 */
void slurmctld_req (slurm_msg_t * msg);

#endif /* !_HAVE_PROC_REQ_H */

