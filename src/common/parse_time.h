/*****************************************************************************\
 *  src/common/parse_time.h - time parsing utility functions
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _PARSE_TIME_H_
#define _PARSE_TIME_H_

#include <inttypes.h>
#include <time.h>

/* Convert string to equivalent time value
 * input formats:
 *   today or tomorrow
 *   midnight, noon, teatime (4PM)
 *   HH:MM [AM|PM]
 *   MMDDYY or MM/DD/YY or MM.DD.YY
 *   YYYY-MM-DD[THH[:MM[:SS]]]
 *   now[{+|-}count[seconds(default)|minutes|hours|days|weeks]]
 *
 * Invalid input results in message to stderr and return value of zero
 */
extern time_t parse_time(const char *time_str, int past);

/*
 * Convert time_t to fixed "%FT%T" formatted string expressed in UTC.
 *
 * IN time - a timestamp
 * OUT string - pointer user defined buffer
 * IN size - length of string buffer (recommend 32 bytes)
 */
extern void parse_time_make_str_utc(time_t *time, char *string, int size);

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
 *   minutes for time_str2mins
 *   seconds for time_str2secs
 *   NO_VAL on error
 *   INFINITE for "infinite" or "unlimited"
 */
extern int time_str2mins(const char *string);
extern int time_str2secs(const char *string);

/* Convert a time value into a string that can be converted back by
 * time_str2mins.
 * fill in string with HH:MM:SS or D-HH:MM:SS
 */
extern void secs2time_str(time_t time, char *string, int size);
extern void mins2time_str(uint32_t time, char *string, int size);

/* used to get a 2 char abbriviated month name from int 0-11 */
extern char *mon_abbr(int mon);

#endif
