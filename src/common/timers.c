/*****************************************************************************\
 *  timers.c - Timer functions
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
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

#include <stdio.h>
#include <sys/time.h>
#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"

#define TIMER_DEFAULT_LIMIT \
	((timespec_t) { \
		.tv_sec = 3, \
	})
#define TIMER_DEFAULT_DEBUG_LIMIT \
	((timespec_t) { \
		.tv_sec = 1, \
	})
#define HISTOGRAM_FIELD_DELIMITER "|"

typedef struct {
	const char *label;
	timespec_t start;
	timespec_t end;
} latency_range_t;

/* String large enough to hold strftime(%T) aka strftime(%H:%M:%S) */
#define STRFTIME_T_BYTES 12

typedef struct {
	char str[STRFTIME_T_BYTES];
} hourminsec_str_t;

// clang-format off
#define TS(s, ns) ((timespec_t) { .tv_sec = (s), .tv_nsec = (ns) })
#define T(label, start, end) { label, start, end }
static const latency_range_t latency_ranges[LATENCY_RANGE_COUNT] = {
	/* WARNING: LATENCY_RANGE_COUNT must equal ARRAY_SIZE(latency_ranges) */
	T("<1µs", TS(0, 0), TS(0, MSEC_IN_SEC)),
	T("1µs - 2µs", TS(0, (1 * MSEC_IN_SEC)), TS(0, (2 * MSEC_IN_SEC))),
	T("2µs - 4µs", TS(0, (2 * MSEC_IN_SEC)), TS(0, (4 * MSEC_IN_SEC))),
	T("4µs - 8µs", TS(0, (4 * MSEC_IN_SEC)), TS(0, (8 * MSEC_IN_SEC))),
	T("8µs - 16µs", TS(0, (8 * MSEC_IN_SEC)), TS(0, (16 * MSEC_IN_SEC))),
	T("16µs - 64µs", TS(0, (16 * MSEC_IN_SEC)), TS(0, (64 * MSEC_IN_SEC))),
	T("64µs - 128µs", TS(0, (64 * MSEC_IN_SEC)), TS(0, (128 * MSEC_IN_SEC))),
	T("128µs - 256µs", TS(0, (128 * MSEC_IN_SEC)), TS(0, (256 * MSEC_IN_SEC))),
	T("256µs - 512µs", TS(0, (256 * MSEC_IN_SEC)), TS(0, (512 * MSEC_IN_SEC))),
	T("512µs - 1ms", TS(0, (512 * MSEC_IN_SEC)), TS(0, NSEC_IN_MSEC)),
	T("1ms - 2ms", TS(0, NSEC_IN_MSEC), TS(0, (2 * NSEC_IN_MSEC))),
	T("2ms - 8ms", TS(0, (2 * NSEC_IN_MSEC)), TS(0, (8 * NSEC_IN_MSEC))),
	T("8ms - 16ms", TS(0, (8 * NSEC_IN_MSEC)), TS(0, (16 * NSEC_IN_MSEC))),
	T("16ms - 500ms", TS(0, (16 * NSEC_IN_MSEC)), TS(0, (500 * NSEC_IN_MSEC))),
	T("500ms - 1s", TS(0, (500 * NSEC_IN_MSEC)), TS(1, 0)),
	T("1s - 2s", TS(1, 0), TS(2, 0)),
	T("2s - 4s", TS(2, 0), TS(4, 0)),
	T("4s - 8s", TS(4, 0), TS(8, 0)),
	T("8s - 30s", TS(8, 0), TS(30, 0)),
	T("30s - 1m", TS(30, 0), TS(MINUTE_SECONDS, 0)),
	T("1m - 2m", TS(MINUTE_SECONDS, 0), TS((2 * MINUTE_SECONDS), 0)),
	T("2m - 4m", TS((2 * MINUTE_SECONDS), 0), TS((4 * MINUTE_SECONDS), 0)),
	T("4m - 8m", TS((4 * MINUTE_SECONDS), 0), TS((8 * MINUTE_SECONDS), 0)),
	T(">8m", TS((4 * MINUTE_SECONDS), 0), TIMESPEC_INFINITE)
};
#undef T
#undef TS
// clang-format on

static long _calc_tv_delta(const timespec_t tv1, const timespec_t tv2)
{
	const timespec_diff_ns_t tdiff = timespec_diff_ns(tv2, tv1);
	const timespec_t diff = tdiff.diff;

	return ((diff.tv_sec * USEC_IN_SEC) + (diff.tv_nsec / NSEC_IN_USEC));
}

extern timer_str_t timer_duration_str(const timespec_t tv1,
				      const timespec_t tv2)
{
	timer_str_t ret = { { 0 } };

	(void) snprintf(ret.str, sizeof(ret.str), "usec=%ld",
			_calc_tv_delta(tv1, tv2));

	return ret;
}

static hourminsec_str_t _timespec_to_hourminsec(const timespec_t ts)
{
	struct tm tm = { 0 };
	hourminsec_str_t ret = { { 0 } };

	/* Avoid ambiguous setting of errno on failure */
	errno = EINVAL;

	if (!localtime_r(&ts.tv_sec, &tm))
		error("localtime_r(): %m");
	else if (!strftime(ret.str, sizeof(ret.str), "%T", &tm))
		error("strftime() failed to format time");
	else
		return ret;

	return (hourminsec_str_t) {
		.str = "INVALID",
	};
}

extern void timer_compare_limit(const timespec_t tv1, const timespec_t tv2,
				const char *from, timespec_t limit)
{
	bool is_after_limit = false;
	timespec_t debug_limit = limit;
	const timespec_t diff = timespec_diff_ns(tv2, tv1).diff;

	xassert(from);

	if (!limit.tv_nsec && !limit.tv_sec) {
		/*
		 * NOTE: The slurmctld scheduler's default run time limit is 4
		 * seconds, but that would not typically be reached. See
		 * "max_sched_time=" logic in src/slurmctld/job_scheduler.c
		 */
		limit = TIMER_DEFAULT_LIMIT;
		debug_limit = TIMER_DEFAULT_DEBUG_LIMIT;
	}

	if (!(is_after_limit = timespec_is_after(diff, limit)) &&
	    !timespec_is_after(diff, debug_limit))
		return;

	if (is_after_limit) {
		verbose("Warning: Note very large processing time from %s: %s began=%s.%3.3d",
			from, timer_duration_str(tv1, tv2).str,
			_timespec_to_hourminsec(tv1).str,
			(int) (tv1.tv_nsec / NSEC_IN_MSEC));
	} else { /* Log anything over 1 second here */
		debug("Note large processing time from %s: %s began=%s.%3.3d",
		      from, timer_duration_str(tv1, tv2).str,
		      _timespec_to_hourminsec(tv1).str,
		      (int) (tv1.tv_nsec / NSEC_IN_MSEC));
	}
}

extern long timer_get_duration(timespec_t *start, timespec_t *end)
{
	if (!start->tv_sec)
		*start = timespec_now();

	if (!end->tv_sec)
		*end = timespec_now();

	return _calc_tv_delta(*start, *end);
}

extern void latency_metric_begin(latency_metric_t *metric, timespec_t *start)
{
	xassert(!start->tv_sec);
	*start = timespec_now();
}

extern latency_metric_rc_t latency_metric_end(latency_metric_t *metric,
					      timespec_t *start, timespec_t end,
					      const timespec_t interval)
{
	latency_metric_rc_t rc = {0};

	xassert(start->tv_sec > 0);

	{
		timespec_diff_ns_t diff = timespec_diff_ns(end, *start);
		xassert(diff.after);
		metric->total = timespec_add(metric->total, diff.diff);
		rc.delay = diff.diff;
		latency_metric_add_histogram_value(&metric->histogram,
						   diff.diff);
	}

	*start = (timespec_t) {0};
	metric->count++;

	/* Check if interval is not to be checked */
	if (timespec_is_infinite(interval))
		return rc;

	if (!metric->last_log.tv_sec) {
		/* Set timestamp on full run and skip analysis */
		metric->last_log = end;
		return rc;
	} else {
		timespec_diff_ns_t diff =
			timespec_diff_ns(end, metric->last_log);

		xassert(diff.after);

		if (!timespec_is_after(diff.diff, interval))
			return rc;
	}

	{
		double avg;

		/* Promote all components to double to avoid truncation */
		avg = (double) metric->total.tv_sec;
		avg += ((double) metric->total.tv_nsec) /
		       ((double) NSEC_IN_SEC);
		avg /= (double) metric->count;

		rc.avg = avg;
	}

	return rc;
}

extern void latency_metric_add_histogram_value(latency_histogram_t *histogram,
					       timespec_t value)
{
	for (int i = 0; (i < ARRAY_SIZE(latency_ranges)); i++) {
		const latency_range_t *range = &latency_ranges[i];

		if (!timespec_is_after(value, range->start))
			continue;

		if (timespec_is_after(value, range->end))
			continue;

		histogram->buckets[i].count++;
		return;
	}
}

extern int latency_histogram_print_labels(char *buffer, size_t buffer_len)
{
	int wrote = 0;

	for (int i = 0;
	     (i < ARRAY_SIZE(latency_ranges)) && (wrote < buffer_len); i++)
		wrote += snprintf((buffer + wrote), (buffer_len - wrote),
				  "%s%-8s",
				  (wrote ? HISTOGRAM_FIELD_DELIMITER : ""),
				  latency_ranges[i].label);
	return wrote;
}

extern int latency_histogram_print(latency_histogram_t *histogram, char *buffer,
				   size_t buffer_len)
{
	int wrote = 0;

	/* sanity check the buckets sizes are still same sizes */
	xassert(ARRAY_SIZE(latency_ranges) == ARRAY_SIZE(histogram->buckets));
	xassert(ARRAY_SIZE(latency_ranges) == LATENCY_RANGE_COUNT);
	xassert(ARRAY_SIZE(histogram->buckets) == LATENCY_RANGE_COUNT);

	for (int i = 0;
	     (i < ARRAY_SIZE(latency_ranges)) && (wrote < buffer_len); i++)
		wrote += snprintf((buffer + wrote), (buffer_len - wrote),
				  "%s%-8" PRId64,
				  (wrote ? HISTOGRAM_FIELD_DELIMITER : ""),
				  histogram->buckets[i].count);

	return wrote;
}
