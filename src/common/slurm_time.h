/*****************************************************************************\
 *  slurm_time.h - assorted time functions
 *****************************************************************************
 *  Convert `time_t' to `struct tm' in local time zone.
 *  Copyright (C) 1991-2015 Free Software Foundation, Inc.
 *  This file is part of the GNU C Library.
 *
 *  The GNU C Library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  The GNU C Library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with the GNU C Library; if not, see
 *  <http://www.gnu.org/licenses/>.
\*****************************************************************************/
#ifndef _HAVE_SLURM_TIME_H
#define _HAVE_SLURM_TIME_H

#include <time.h>

extern time_t slurm_mktime(struct tm *tp);

/* Slurm variants of ctime and ctime_r without a trailing new-line */
extern char *slurm_ctime2(const time_t *timep);
extern char *slurm_ctime2_r(const time_t *timep, char *time_str);

/* Print the current date + time as formatted by slurm_ctime2_r */
extern void print_date(void);

#endif /* _HAVE_SLURM_TIME_H */
