/*****************************************************************************\
 *  slurm_time.c - assorted time functions
 *****************************************************************************
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

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <src/common/log.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_time.h>
#include <src/common/xassert.h>

extern time_t slurm_mktime(struct tm *tp)
{
	/* Force tm_isdt to -1. */
	tp->tm_isdst = -1;
	return mktime(tp);
}

/* Slurm variants of ctime and ctime_r without a trailing new-line */
extern char *slurm_ctime2(const time_t *timep)
{
	struct tm newtime;
	static char time_str[25];
	localtime_r(timep, &newtime);

	strftime(time_str, sizeof(time_str), "%a %b %d %T %Y", &newtime);

	return time_str;
}

extern char *slurm_ctime2_r(const time_t *timep, char *time_str)
{
	struct tm newtime;
	localtime_r(timep, &newtime);

	strftime(time_str, 25, "%a %b %d %T %Y", &newtime);

	return time_str;
}

extern void print_date(void)
{
	time_t now = time(NULL);
	char time_str[25];

	printf("%s\n", slurm_ctime2_r(&now, time_str));
}

extern timespec_t timespec_now(void)
{
	timespec_t time;
	int rc;

	if ((rc = clock_gettime(TIMESPEC_CLOCK_TYPE, &time))) {
		if (rc == -1)
			rc = errno;

		fatal("%s: clock_gettime() failed: %s",
		      __func__, slurm_strerror(rc));
	}

	return time;
}

extern void timespec_ctime(timespec_t ts, bool abs_time, char *buffer,
			   size_t buffer_len)
{
	uint64_t t, days, hours, minutes, seconds;
	uint64_t milliseconds, microseconds, nanoseconds;
	bool negative = false;

	xassert(buffer);
	xassert(buffer_len > 0);
	if (!buffer || (buffer_len <= 0))
		return;

	if (!ts.tv_nsec && !ts.tv_sec) {
		buffer[0] = '\0';
		return;
	}

	ts = timespec_normalize(ts);

	if (abs_time)
		ts = timespec_normalize(timespec_rem(ts, timespec_now()));

	/* Force positive time */
	if (ts.tv_sec < 0) {
		negative = true;

		ts.tv_sec *= -1;
		ts.tv_nsec *= -1;
	}

	t = ts.tv_sec;

	/* Divide out the orders of magnitude */

	days = t / (DAY_HOURS * HOUR_SECONDS);
	t = t % (DAY_HOURS * HOUR_SECONDS);

	hours = t / HOUR_SECONDS;
	t = t % HOUR_SECONDS;

	minutes = t / MINUTE_SECONDS;
	t = t % MINUTE_SECONDS;

	seconds = t;

	t = ts.tv_nsec;

	milliseconds = t / NSEC_IN_MSEC;
	t = t % NSEC_IN_MSEC;

	microseconds = t / NSEC_IN_USEC;
	t = t % NSEC_IN_USEC;

	nanoseconds = t;

	snprintf(buffer, buffer_len,
		 "%s%s%"PRIu64"d:%"PRIu64"h:%"PRIu64"m:%"PRIu64"s:%"PRIu64"ms:%"PRIu64"μs:%"PRIu64"ns%s",
		 (abs_time ? ( negative ? "now" : "now+" ) : ""),
		 (negative ? "-(" : ""), days, hours, minutes, seconds,
		 milliseconds, microseconds, nanoseconds,
		 (negative ? ")" : ""));
}

extern timespec_t timespec_normalize(timespec_t ts)
{
	/* Force direction of time to be uniform */
	if ((ts.tv_nsec < 0) && (ts.tv_sec > 0)) {
		ts.tv_sec++;
		ts.tv_nsec = NSEC_IN_SEC + ts.tv_nsec;
	} else if ((ts.tv_nsec > 0) && (ts.tv_sec < 0)) {
		ts.tv_sec--;
		ts.tv_nsec = NSEC_IN_SEC - ts.tv_nsec;
	}

	return (timespec_t) {
		.tv_sec = ts.tv_sec + (ts.tv_nsec / NSEC_IN_SEC),
		.tv_nsec = (ts.tv_nsec % NSEC_IN_SEC),
	};
}

extern timespec_t timespec_add(const timespec_t x, const timespec_t y)
{
	/* Use 64bit accumulators to avoid overflow */
	return timespec_normalize((timespec_t) {
		.tv_sec = (((uint64_t) x.tv_sec) + ((uint64_t) y.tv_sec)),
		.tv_nsec = (((uint64_t) x.tv_nsec) + ((uint64_t) y.tv_nsec)),
	});
}

extern timespec_t timespec_rem(const timespec_t x, const timespec_t y)
{
	/* Use 64bit accumulators to avoid underflow */
	int64_t s = (((uint64_t) x.tv_sec) - ((uint64_t) y.tv_sec));
	int64_t ns = (((uint64_t) x.tv_nsec) - ((uint64_t) y.tv_nsec));

	/* reject underflow of time */
	if (s <= 0)
		return (timespec_t) {0};

	/* force ns to be positive */
	if (ns < 0) {
		s--;
		ns = NSEC_IN_SEC - ns;
	}

	return timespec_normalize((timespec_t) {
		.tv_sec = s,
		.tv_nsec = ns,
	});
}

extern bool timespec_is_after(const timespec_t x, const timespec_t y)
{
	if (x.tv_sec < y.tv_sec)
		return false;
	if (x.tv_sec > y.tv_sec)
		return true;

	return (x.tv_nsec > y.tv_nsec);
}

extern int64_t timespec_diff(const timespec_t x, const timespec_t y)
{
	/* Use 64bit signed accumulators to catch underflow */
	return (((int64_t) x.tv_sec) - ((int64_t) y.tv_sec));
}

extern timespec_diff_ns_t timespec_diff_ns(const timespec_t x,
					   const timespec_t y)
{
	/* Use 64bit accumulators to catch underflows */
	int64_t s = (((int64_t) x.tv_sec) - ((int64_t) y.tv_sec));
	int64_t ns = (((int64_t) x.tv_nsec) - ((int64_t) y.tv_nsec));

	/* Adjust postive nanoseconds if seconds is negative */
	if ((ns > 0) && (s < 0)) {
		s += 1;
		ns -= NSEC_IN_SEC;
	}

	if (s < 0)
		return (timespec_diff_ns_t) {
			.after = false,
			.diff = {
				.tv_sec = (-1 * s),
				.tv_nsec = (-1 * ns),
			},
		};
	else
		return (timespec_diff_ns_t) {
			.after = true,
			.diff = {
				.tv_sec = s,
				.tv_nsec = ns,
			},
		};
}
