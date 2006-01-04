/*****************************************************************************\
 *  src/common/parse_time.h - time parsing utility functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _PARSE_TIME_H_
#define _PARSE_TIME_H_

#include <time.h>

/* Convert string to equivalent time value
 * input formats:
 *   today or tomorrow
 *   midnight, noon, teatime (4PM)
 *   HH:MM [AM|PM]
 *   MMDDYY or MM/DD/YY or MM.DD.YY
 *   now + count [minutes | hours | days | weeks]
 *
 * Invalid input results in message to stderr and return value of zero
 */
extern time_t parse_time(char *time_str);

#endif
