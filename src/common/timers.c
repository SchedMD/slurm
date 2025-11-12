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

#define HISTOGRAM_FIELD_DELIMITER "|"

typedef struct {
	const char *label;
	timespec_t start;
	timespec_t end;
} latency_range_t;

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

static long _calc_tv_delta(const struct timeval *tv1, const struct timeval *tv2)
{
	long delta = (tv2->tv_sec - tv1->tv_sec) * USEC_IN_SEC;
	delta += tv2->tv_usec;
	delta -= tv1->tv_usec;
	return delta;
}

extern int timer_duration_str(struct timeval *tv1, struct timeval *tv2,
			      char *tv_str, const size_t len_tv_str)
{
	return snprintf(tv_str, len_tv_str, "usec=%ld",
			_calc_tv_delta(tv1, tv2));
}

/*
 * slurm_diff_tv_str - build a string showing the time difference between two
 *		       times
 * IN tv1 - start of event
 * IN tv2 - end of event
 * OUT tv_str - place to put delta time in format "usec=%ld"
 * IN len_tv_str - size of tv_str in bytes
 * IN from - where the function was called form
 */
extern void slurm_diff_tv_str(struct timeval *tv1, struct timeval *tv2,
			      char *tv_str, int len_tv_str, const char *from,
			      long limit, long *delta_t)
{
	char p[64] = "";
	struct tm tm;
	int debug_limit = limit;

	(*delta_t) = _calc_tv_delta(tv1, tv2);

	(void) timer_duration_str(tv1, tv2, tv_str, len_tv_str);
	if (from) {
		if (!limit) {
			/* NOTE: The slurmctld scheduler's default run time
			 * limit is 4 seconds, but that would not typically
			 * be reached. See "max_sched_time=" logic in
			 * src/slurmctld/job_scheduler.c */
			limit = 3000000;
			debug_limit = 1000000;
		}
		if ((*delta_t > debug_limit) || (*delta_t > limit)) {
			if (!localtime_r(&tv1->tv_sec, &tm))
				error("localtime_r(): %m");
			if (strftime(p, sizeof(p), "%T", &tm) == 0)
				error("strftime(): %m");
			if (*delta_t > limit) {
				verbose("Warning: Note very large processing "
					"time from %s: %s began=%s.%3.3d",
					from, tv_str, p,
					(int)(tv1->tv_usec / 1000));
			} else {	/* Log anything over 1 second here */
				debug("Note large processing time from %s: "
				      "%s began=%s.%3.3d",
				      from, tv_str, p,
				      (int)(tv1->tv_usec / 1000));
			}
		}
	}
}

extern long timer_get_duration(struct timeval *start, struct timeval *end)
{
	if (!start->tv_sec)
		(void) gettimeofday(start, NULL);

	if (!end->tv_sec)
		(void) gettimeofday(end, NULL);

	return _calc_tv_delta(start, end);
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
		avg += ((double) metric->total.tv_sec) / ((double) NSEC_IN_SEC);
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
