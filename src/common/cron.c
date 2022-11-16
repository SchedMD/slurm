/*****************************************************************************\
 *  cron.c
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <ctype.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/cron.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

extern cron_entry_t *new_cron_entry(void)
{
	cron_entry_t *entry = xmalloc(sizeof(*entry));

	entry->minute = bit_alloc(61);
	entry->hour = bit_alloc(25);
	entry->day_of_month = bit_alloc(32);
	entry->month = bit_alloc(13);
	entry->day_of_week = bit_alloc(8);

	return entry;
}

extern void free_cron_entry(void *in)
{
	cron_entry_t *entry = (cron_entry_t *) in;

	if (!entry)
		return;

	xfree(entry->minute);
	xfree(entry->hour);
	xfree(entry->day_of_month);
	xfree(entry->month);
	xfree(entry->day_of_week);
	xfree(entry->cronspec);
	xfree(entry->command);
	xfree(entry);
}

extern bool valid_cron_entry(cron_entry_t *e)
{
	int first_day_of_month;

	/* basic structure check */
	if ((bit_size(e->minute) != 61) ||
	    (bit_size(e->hour) != 25) ||
	    (bit_size(e->day_of_month) != 32) ||
	    (bit_size(e->month) != 13) ||
	    (bit_size(e->day_of_week) != 8))
		return false;

	/*
	 * Clear top or lower bits (may have been set for wildcard processing).
	 */
	bit_clear(e->minute, 60);
	bit_clear(e->hour, 24);
	bit_clear(e->day_of_month, 0);
	bit_clear(e->month, 0);
	bit_clear(e->day_of_week, 7);

	/*
	 * Missing some e. Need at least one bit set in each field or
	 * the wildcard flag, otherwise calc_next_cron_start() will break.
	 */
	first_day_of_month = bit_ffs(e->day_of_month);
	if ((!(e->flags & CRON_WILD_MINUTE) && (bit_ffs(e->minute) == -1)) ||
	    (!(e->flags & CRON_WILD_HOUR) && (bit_ffs(e->hour) == -1)) ||
	    (!(e->flags & CRON_WILD_DOM) && (first_day_of_month == -1)) ||
	    (!(e->flags & CRON_WILD_MONTH) && (bit_ffs(e->month) == -1)) ||
	    (!(e->flags & CRON_WILD_DOW) && (bit_ffs(e->day_of_week) == -1)))
		return false;

	/*
	 * Make sure the crontab isn't requesting a non-existent
	 * combination of month and day.
	 *
	 * Note: we do allow you to schedule something to only run
	 * on leap days, as crazy as that may seem.
	 */
	if (e->flags & CRON_WILD_DOM) {
		;
	} else if (first_day_of_month == 31) {
		if (!bit_test(e->month, 1) && !bit_test(e->month, 3) &&
		    !bit_test(e->month, 5) && !bit_test(e->month, 7) &&
		    !bit_test(e->month, 8) && !bit_test(e->month, 10) &&
		    !bit_test(e->month, 12))
			return false;
	} else if (first_day_of_month == 30) {
		/* Make sure the only month available isn't February. */
		if ((bit_fls(e->month) == 2) && (bit_ffs(e->month) == 2))
			return false;
	}

	return true;
}

extern char *cronspec_from_cron_entry(cron_entry_t *entry)
{
	char *cronspec = NULL;
	char *fmt;

	if (entry->flags & CRON_WILD_MINUTE) {
		xstrcat(cronspec, "* ");
	} else {
		fmt = bit_fmt_full(entry->minute);
		xstrfmtcat(cronspec, "%s ", fmt);
		xfree(fmt);
	}

	if (entry->flags & CRON_WILD_HOUR) {
		xstrcat(cronspec, "* ");
	} else {
		fmt = bit_fmt_full(entry->hour);
		xstrfmtcat(cronspec, "%s ", fmt);
		xfree(fmt);
	}

	if (entry->flags & CRON_WILD_DOM) {
		xstrcat(cronspec, "* ");
	} else {
		fmt = bit_fmt_full(entry->day_of_month);
		xstrfmtcat(cronspec, "%s ", fmt);
		xfree(fmt);
	}

	if (entry->flags & CRON_WILD_MONTH) {
		xstrcat(cronspec, "* ");
	} else {
		fmt = bit_fmt_full(entry->month);
		xstrfmtcat(cronspec, "%s ", fmt);
		xfree(fmt);
	}

	if (entry->flags & CRON_WILD_DOW) {
		xstrcat(cronspec, "*");
	} else {
		fmt = bit_fmt_full(entry->day_of_week);
		xstrfmtcat(cronspec, "%s", fmt);
		xfree(fmt);
	}

	return cronspec;
}

/*
 * One important note: struct tm has jan == 0, but the crontab
 * format and our bitstring have jan == 1.
 */
static int _next_month(cron_entry_t *entry, struct tm *tm)
{
	int months_to_advance = 0;

	/* month is current valid, nice and easy, no major adjustments needed */
	if (entry->flags & CRON_WILD_MONTH ||
	    bit_test(entry->month, tm->tm_mon + 1))
		return 0;

	for (int i = tm->tm_mon; i < 12; i++) {
		if (bit_test(entry->month, i + 1))
			goto found;
		months_to_advance++;
	}

	for (int i = 0; i < tm->tm_mon; i++) {
		if (bit_test(entry->month, i + 1))
			goto found;
		months_to_advance++;
	}

	fatal("Could not find a valid month, this should be impossible");

found:
	/*
	 * Next usable month is not this month. Reset other timing to midnight
	 * on the first of the next valid month.
	 */
	tm->tm_mon += months_to_advance;
	tm->tm_hour = 0;
	tm->tm_min = 0;
	tm->tm_mday = 1;
	slurm_mktime(tm);

	return 0;
}

static int _next_day_of_week(cron_entry_t *entry, struct tm *tm)
{
	int days_to_advance = 0;

	for (int i = tm->tm_wday; i < 7; i++) {
		if (bit_test(entry->day_of_week, i))
			return days_to_advance;
		days_to_advance++;
	}

	for (int i = 0; i < tm->tm_wday; i++) {
		if (bit_test(entry->day_of_week, i))
			return days_to_advance;
		days_to_advance++;
	}

	return 0;
}

static int _next_day_of_month(cron_entry_t *entry, struct tm *tm)
{
	int days_to_advance = 0;

	for (int i = tm->tm_mday; i < 29; i++) {
		if (bit_test(entry->day_of_month, i))
			return days_to_advance;
		days_to_advance++;
	}

	/* february == 1 */
	if (tm->tm_mon != 1) {
		if (bit_test(entry->day_of_month, 29))
			return days_to_advance;
		days_to_advance++;
		if (bit_test(entry->day_of_month, 30))
			return days_to_advance;
		days_to_advance++;
		if ((tm->tm_mon == 0) || (tm->tm_mon == 2) ||
		    (tm->tm_mon == 4) || (tm->tm_mon == 6) ||
		    (tm->tm_mon == 7) || (tm->tm_mon == 9) ||
		    (tm->tm_mon == 11)) {
			if (bit_test(entry->day_of_month, 31))
				return days_to_advance;
			days_to_advance++;
		}
	} else {
		/* (ab)use mktime() to figure out leap years for februrary */
		struct tm test = { 0 };
		test.tm_year = tm->tm_year;
		test.tm_mon = 1;
		test.tm_mday = 29;
		slurm_mktime(&test);
		if (test.tm_mon == 1) {
			/* leap year! */
			if (bit_test(entry->day_of_month, 29))
				return days_to_advance;
			days_to_advance++;
		}
	}

	for (int i = 1; i < tm->tm_mday; i++) {
		if (bit_test(entry->day_of_month, i))
			return days_to_advance;
		days_to_advance++;
	}

	return days_to_advance;
}

extern time_t calc_next_cron_start(cron_entry_t *entry, time_t next)
{
	struct tm tm;
	time_t now = time(NULL);
	int validated_month, days_to_add;

	/*
	 * Avoid running twice in the same minute.
	 */
	if (next && next > now + 60) {
		now = next;
		localtime_r(&now, &tm);
		tm.tm_sec = 0;
	} else {
		localtime_r(&now, &tm);
		tm.tm_sec = 0;
		tm.tm_min++;
	}

month:
	_next_month(entry, &tm);

	validated_month = tm.tm_mon;

	days_to_add = 0;
	if ((entry->flags & CRON_WILD_DOM) && (entry->flags & CRON_WILD_DOW)) {
		/* Wildcard for both is the easy path out. */
		;
	} else if (entry->flags & CRON_WILD_DOM) {
		/*
		 * Only pay attention to the day of week.
		 */
		days_to_add = _next_day_of_week(entry, &tm);
	} else if (entry->flags & CRON_WILD_DOW) {
		/*
		 * Only attention to the day of month.
		 */
		days_to_add = _next_day_of_month(entry, &tm);
	} else {
		/*
		 * When both are specified, the defacto behavior is to
		 * treat them as OR'd rather than AND'd, as trying to
		 * resolve both simultaneously would result in the job
		 * very rarely running.
		 * So find the soonest time between them and use that.
		 */
		int dom_next = _next_day_of_month(entry, &tm);
		int dow_next = _next_day_of_week(entry, &tm);

		days_to_add = MIN(dom_next, dow_next);
	}
	if (days_to_add) {
		tm.tm_mday += days_to_add;
		tm.tm_hour = 0;
		tm.tm_min = 0;
		slurm_mktime(&tm);

		/* month slipped back, need to re-validate */
		if (validated_month != tm.tm_mon)
			goto month;
	}

hour:
	if (!(entry->flags & CRON_WILD_HOUR) &&
	    !bit_test(entry->hour, tm.tm_hour)) {
		/* must be in future, reset minutes */
		tm.tm_min = 0;

		while (tm.tm_hour < 24) {
			if (bit_test(entry->hour, tm.tm_hour))
				break;
			tm.tm_hour++;
		}
		if (tm.tm_hour == 24) {
			/*
			 * tm_hour set to 24 rolls the day and possibly
			 * the month at well. revalidate month + day.
			 */
			slurm_mktime(&tm);
			goto month;
		}
	}

	if (!(entry->flags & CRON_WILD_MINUTE) &&
	    !bit_test(entry->minute, tm.tm_min)) {
		while (tm.tm_min < 60) {
			if (bit_test(entry->minute, tm.tm_min))
				break;
			tm.tm_min++;
		}
		if (tm.tm_min == 60 && tm.tm_hour == 23) {
			/*
			 * this will roll into the next day,
			 * which may also be a new month
			 */
			slurm_mktime(&tm);
			goto month;
		} else if (tm.tm_min == 60) {
			/*
			 * next hour, but fortunately still
			 * in the same day
			 */
			tm.tm_min = 0;
			tm.tm_hour++;
			goto hour;
		}
	}

	return slurm_mktime(&tm);
}

extern void pack_cron_entry(void *in, uint16_t protocol_version,
			    buf_t *buffer)
{
	uint8_t set = (in ? 1 : 0);
	cron_entry_t *entry = (cron_entry_t *) in;

	pack8(set, buffer);

	if (!set)
		return;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(entry->flags, buffer);
		pack_bit_str_hex(entry->minute, buffer);
		pack_bit_str_hex(entry->hour, buffer);
		pack_bit_str_hex(entry->day_of_month, buffer);
		pack_bit_str_hex(entry->month, buffer);
		pack_bit_str_hex(entry->day_of_week, buffer);
		packstr(entry->cronspec, buffer);
		/* command is not packed, only in struct for parsing */
		pack32(entry->line_start, buffer);
		pack32(entry->line_end, buffer);
	}
}

extern int unpack_cron_entry(void **entry_ptr, uint16_t protocol_version,
			     buf_t *buffer)
{
	uint8_t set;
	uint32_t uint32_tmp;
	cron_entry_t *entry = NULL;

	xassert(entry_ptr);

	safe_unpack8(&set, buffer);

	if (!set)
		return SLURM_SUCCESS;

	entry = xmalloc(sizeof(*entry));
	*entry_ptr = entry;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&entry->flags, buffer);
		unpack_bit_str_hex(&entry->minute, buffer);
		unpack_bit_str_hex(&entry->hour, buffer);
		unpack_bit_str_hex(&entry->day_of_month, buffer);
		unpack_bit_str_hex(&entry->month, buffer);
		unpack_bit_str_hex(&entry->day_of_week, buffer);
		safe_unpackstr_xmalloc(&entry->cronspec, &uint32_tmp, buffer);
		/* command is not packed, only in struct for parsing */
		safe_unpack32(&entry->line_start, buffer);
		safe_unpack32(&entry->line_end, buffer);
	} else {
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	*entry_ptr = NULL;
	free_cron_entry(entry);
	return SLURM_ERROR;
}
