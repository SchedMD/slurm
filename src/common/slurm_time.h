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

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __linux__
#define TIMESPEC_CLOCK_TYPE CLOCK_TAI
#else
#define TIMESPEC_CLOCK_TYPE CLOCK_REALTIME
#endif

extern time_t slurm_mktime(struct tm *tp);

/* Slurm variants of ctime and ctime_r without a trailing new-line */
extern char *slurm_ctime2(const time_t *timep);
extern char *slurm_ctime2_r(const time_t *timep, char *time_str);

/* Print the current date + time as formatted by slurm_ctime2_r */
extern void print_date(void);

/* Create typedef to follow *_t naming convention */
typedef struct timespec timespec_t;

/* Get timespec for current timestamp from UNIX Epoch */
extern timespec_t timespec_now(void);

/*
 * Convert timespec into human readable string
 *
 * IN ts - timestamp
 * IN abs_time -
 *	true if ts is time since UNIX epoch
 *	false if ts is arbitrary length of time
 * IN buffer - pointer to buffer to populate (always \0 terminates string)
 * IN buffer_len - number of bytes in buffer
 */
extern void timespec_ctime(timespec_t ts, bool abs_time, char *buffer,
			   size_t buffer_len);

/* Add overflow of nanoseconds into seconds */
extern timespec_t timespec_normalize(timespec_t ts);

/* Add timestamp X to timestamp Y */
extern timespec_t timespec_add(const timespec_t x, const timespec_t y);

/* Subtract timestamp Y from timestamp X */
extern timespec_t timespec_rem(const timespec_t x, const timespec_t y);

/* Is timestamp X after timestamp Y */
extern bool timespec_is_after(const timespec_t x, const timespec_t y);

/*
 * Subtract timestamp Y from timestamp X
 * RET diff in seconds (drops nanoseconds)
 */
extern int64_t timespec_diff(const timespec_t x, const timespec_t y);

typedef struct {
	timespec_t diff; /* x - y */
	bool after; /* x is after y */
} timespec_diff_ns_t;

/*
 * Subtract timestamp Y from timestamp X
 */
extern timespec_diff_ns_t timespec_diff_ns(const timespec_t x,
					   const timespec_t y);

#endif /* _HAVE_SLURM_TIME_H */
