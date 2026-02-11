/*****************************************************************************\
 *  timers.h - timing functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov>
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

#ifndef _HAVE_TIMERS_H
#define _HAVE_TIMERS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>

#include <src/common/atomic.h>
#include <src/common/slurm_time.h>

#define TIMER_START_TS tv1
#define TIMER_END_TS tv2
#define DEF_TIMERS \
	timespec_t TIMER_START_TS = { 0, 0 }, TIMER_END_TS = { 0, 0 };
#define START_TIMER \
	do { \
		TIMER_START_TS = timespec_now(); \
	} while (false)
#define END_TIMER \
	do { \
		TIMER_END_TS = timespec_now(); \
	} while (false)
#define END_TIMER2(from) \
	do { \
		TIMER_END_TS = timespec_now(); \
		timer_compare_limit(TIMER_START_TS, TIMER_END_TS, from, \
				    (timespec_t) { 0, 0 }); \
	} while (false)
#define END_TIMER3(from, limit) \
	do { \
		TIMER_END_TS = timespec_now(); \
		timer_compare_limit(TIMER_START_TS, TIMER_END_TS, from, \
				    TIMESPEC_FROM_USEC(limit)); \
	} while (false)
/*
 * Get duration of time between START_TIMER and END_TIMER as string
 * Note: Must be called after START_TIMER and END_TIMER macros
 * RET: string of duration of time between calls or "INVALID"
 */
#define TIMER_STR() (timer_duration_str(TIMER_START_TS, TIMER_END_TS).str)
/* Get timer duration in microseconds */
#define TIMER_DURATION_USEC() timer_get_duration(&TIMER_START_TS, &TIMER_END_TS)

/*
 * Get timer duration in microseconds
 * IN/OUT start - ptr to start of timer. Will be populated with now if zero.
 * IN/OUT end - ptr to end of timer. Will be populated with now if zero.
 * RET duration of timer in microseconds
 */
extern long timer_get_duration(timespec_t *start, timespec_t *end);

typedef struct {
	char str[TIMESPEC_CTIME_STR_LEN];
} timer_str_t;

/*
 * Compare the time difference between two times and log when over the limit
 * IN tv1 - start of event
 * IN tv2 - end of event
 * IN from - Name to be printed on long diffs
 * IN limit - limit to wait
 */
extern void timer_compare_limit(const timespec_t tv1, const timespec_t tv2,
				const char *from, timespec_t limit);

/*
 * Get string of time difference between tv1 and tv2 into tv_str
 * IN tv1 - time value start
 * IN tv2 - time value end
 * RET string of duration
 */
extern timer_str_t timer_duration_str(const timespec_t tv1,
				      const timespec_t tv2);

/*
 * Number of latency ranges in latency histogram.
 * WARNING: Must be kept in sync with ARRAY_SIZE(latency_ranges).
 */
#define LATENCY_RANGE_COUNT 24

#ifndef __STDC_NO_ATOMICS__

typedef struct {
	atomic_uint64_t buckets[LATENCY_RANGE_COUNT];
} latency_histogram_t;

#define LATENCY_HISTOGRAM_INITIALIZER \
	((latency_histogram_t) { \
		.buckets = { { 0 } }, \
	})

#else /* !__STDC_NO_ATOMICS__ */

/* Only provide a placeholder type to avoid breaking structs */
typedef void *latency_histogram_t;
#define LATENCY_HISTOGRAM_INITIALIZER NULL

#endif /* !__STDC_NO_ATOMICS__ */

/* Struct to hold latency metric state */
typedef struct {
	latency_histogram_t histogram;
	timespec_t total;
	uint64_t count;
	timespec_t last_log;
} latency_metric_t;

typedef struct {
	double avg; /* Average latency in seconds or 0 if not calculated */
	timespec_t delay; /* Delay from START_LATENCY_TIMER() */
} latency_metric_rc_t;

/*
 * Start recording latency metric
 * NOTE: Must have DECL_LATENCY_TIMER() in scope
 * NOTE: call BEGIN_LATENCY_TIMER() instead
 * IN metric - metric state
 * IN start - timestamp to populate
 */
extern void latency_metric_begin(latency_metric_t *metric, timespec_t *start);

/*
 * Stop recording latency metric and perform analysis
 * NOTE: Must have DECL_LATENCY_TIMER() in scope
 * NOTE: call END_LATENCY_TIMER() instead
 * IN metric - metric state
 * IN start - timestamp populated by START_LATENCY_TIMER()
 * IN end - timestamp when event ended or timespec_now()
 * IN interval
 *	Min interval between calculating analysis
 *	TIMESPEC_INFINITE to skip
 * RET struct full of latency metric analysis
 */
extern latency_metric_rc_t latency_metric_end(latency_metric_t *metric,
					      timespec_t *start, timespec_t end,
					      const timespec_t interval);

/* Declare latency timer */
#define DECL_LATENCY_TIMER \
	static latency_metric_t latency_metric = LATENCY_METRIC_INITIALIZER; \
	static __thread timespec_t latency_metric_start = { 0, 0 };

#define LATENCY_METRIC_INITIALIZER \
	((latency_metric_t) { \
		.histogram = LATENCY_HISTOGRAM_INITIALIZER, \
		.total = { 0, 0 }, \
		.count = 0, \
		.last_log = { 0, 0 }, \
	})

/* Start latency timer */
#define START_LATENCY_TIMER() \
	latency_metric_begin(&latency_metric, &latency_metric_start)

/* End latency timer and generate analysis if past interval */
#define END_LATENCY_TIMER(interval) \
	latency_metric_end(&latency_metric, &latency_metric_start, \
			   timespec_now(), interval)

/* Expected buffer size to hold printed latency histogram */
#define LATENCY_METRIC_HISTOGRAM_STR_LEN 1024

/*
 * print histogram buckets labels to buffer
 * IN buffer - pointer to buffer to populate
 * IN buffer_len - number of bytes in buffer. should be at least
 *	LATENCY_METRIC_HISTOGRAM_STR_LEN.
 * IN numbers of bytes written (excluding \0)
 */
extern int latency_histogram_print_labels(char *buffer, size_t buffer_len);

/*
 * print histogram buckets to buffer
 * Note: operation is threadsafe w/rt to the histogram buckets individually but
 *	will potentially print out of date values.
 * IN metric - latency metric to print histogram from
 * IN buffer - pointer to buffer to populate
 * IN buffer_len - number of bytes in buffer. should be at least
 *	LATENCY_METRIC_HISTOGRAM_STR_LEN.
 * IN numbers of bytes written (excluding \0)
 */
extern int latency_histogram_print(latency_histogram_t *histogram, char *buffer,
				   size_t buffer_len);

/*
 * Add latency value to histogram
 * Note: operation is threadsafe
 * IN metric - latency metric to add new result
 * IN value - duration of time spent waiting
 */
extern void latency_metric_add_histogram_value(latency_histogram_t *histogram,
					       timespec_t value);

#endif
