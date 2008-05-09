/*****************************************************************************\
 *  src/common/parse_time.h - time parsing utility functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _PARSE_TIME_H_
#define _PARSE_TIME_H_

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif                        /* HAVE_INTTYPES_H */
#else                           /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif

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

/*
 * slurm_make_time_str - convert time_t to string with a format of
 *	"month/date hour:min:sec"
 *
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 * IN size - length of string buffer, we recommend a size of 32 bytes to
 *	easily support different site-specific formats
 */
extern void slurm_make_time_str (time_t *time, char *string, int size); 

/* Convert a string to an equivalent time value
 * input formats:
 *   min
 *   min:sec
 *   hr:min:sec
 *   days-hr:min:sec
 *   days-hr
 * output:
 *   minutes
 */
extern int time_str2mins(char *string);

/* Convert a time value into a string that can be converted back by 
 * time_str2mins. 
 * fill in string with HH:MM:SS or D-HH:MM:SS
 */
extern void secs2time_str(time_t time, char *string, int size);
extern void mins2time_str(uint32_t time, char *string, int size);

#endif
