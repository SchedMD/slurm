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
#include <sys/time.h>
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

/*
 * Slurm wrapper for the nanosleep() function. This function will call
 * nanosleep() until the elapsed time passes, or until nanosleep() returns
 * an error with errno != EINTR.
 *
 * According to nanosleep(2):
 *
 *     Compared to sleep(3) and usleep(3), nanosleep() has the following
 *     advantages: it provides a higher resolution for specifying the
 *     sleep interval; POSIX.1 explicitly specifies that it does not
 *     interact with signals; and it makes the task of resuming a sleep
 *     that has been interrupted by a signal handler easier.
 *
 * Note: This function is subject to drift. According to nanosleep(2):
 *
 *     The fact that nanosleep() sleeps for a relative interval can be
 *     problematic if the call is repeatedly restarted after being
 *     interrupted by signals, since the time between the interruptions and
 *     restarts of the call will lead to drift in the time when the sleep
 *     finally completes.  This problem can be avoided by using
 *     clock_nanosleep(2) with an absolute time value.
 *
 * Don't use this function if sleeping for an exact time is important.
 *
 * IN sleep_sec - number of seconds to sleep.
 * IN sleep_ns - number of nanoseconds to sleep. If this number is outside of
 *               the range [0, 999999999] then nanosleep() will return EINVAL.
 * Returns SLURM_SUCCESS on success. Returns errno set by nanosleep() on error.
 * This function will never return EINTR.
 */
extern int slurm_nanosleep(time_t sleep_sec, uint32_t sleep_ns);

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

/* Convert timestamp to seconds with decimal for nanoseconds */
extern double timespec_to_secs(const timespec_t x);

/*
 * Return time in milliseconds since "start time"
 * Takes a struct timeval.
 */
extern int timeval_tot_wait(struct timeval *start_time);

#endif /* _HAVE_SLURM_TIME_H */
