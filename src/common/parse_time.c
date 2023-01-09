/*****************************************************************************\
 *  src/common/parse_time.c - time parsing utility functions
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

#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ctype.h>

#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/slurm_time.h"
#include "src/common/strlcpy.h"
#include "src/common/xstring.h"
#include "src/common/parse_time.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
*/
strong_alias(parse_time, slurm_parse_time);
strong_alias(parse_time_make_str_utc, slurm_parse_time_make_str_utc);
//slurm_make_time_str is already exported
strong_alias(time_str2mins, slurm_time_str2mins);
strong_alias(time_str2secs, slurm_time_str2secs);
strong_alias(secs2time_str, slurm_secs2time_str);
strong_alias(mins2time_str, slurm_mins2time_str);
strong_alias(mon_abbr, slurm_mon_abbr);

typedef struct unit_names {
	char *name;
	int name_len;
	int multiplier;
} unit_names_t;
static unit_names_t un[] = {
	{"seconds",	7,	1},
	{"second",	6,	1},
	{"minutes",	7,	60},
	{"minute",	6,	60},
	{"hours",	5,	(60*60)},
	{"hour",	4,	(60*60)},
	{"days",	4,	(24*60*60)},
	{"day",		3,	(24*60*60)},
	{"weeks",	5,	(7*24*60*60)},
	{"week",	4,	(7*24*60*60)},
	{NULL,		0,	0}
};

/* _is_valid_timespec()
 *
 * Validate that time format follows
 * is supported.
 */
static bool
_is_valid_timespec(const char *s)
{
	int digit;
	int dash;
	int colon;
	bool already_digit = false;
	digit = dash = colon = 0;

	while (*s) {
		if (*s >= '0' && *s <= '9') {
			if (!already_digit) {
				++digit;
				already_digit = true;
			}
		} else if (*s == '-') {
			already_digit = false;
			++dash;
			if (colon)
				return false;
		} else if (*s == ':') {
			already_digit = false;
			++colon;
		} else {
			return false;
		}
		++s;
	}

	if (!digit)
		return false;

	if (dash > 1
	    || colon > 2)
		return false;

	if (dash) {
		if (colon == 1
		    && digit < 3)
			return false;
		if (colon == 2
		    && digit < 4)
			return false;
	} else {
		if (colon == 1
		    && digit < 2)
			return false;
		if (colon == 2
		    && digit < 3)
			return false;
	}

	return true;
}

/* convert time differential string into a number of seconds
 * time_str (in): string to parse
 * pos (in/out): position of parse start/end
 * delta (out): delta in seconds
 * RET: -1 on error, 0 otherwise
 */
static int _get_delta(const char *time_str, int *pos, long *delta)
{
	int i, offset;
	long cnt = 0;
	int digit = 0;

	for (offset = (*pos) + 1;
	     ((time_str[offset] != '\0') && (time_str[offset] != '\n'));
	     offset++) {
		if (isspace((int)time_str[offset]))
			continue;
		for (i=0; un[i].name; i++) {
			if (!xstrncasecmp((time_str + offset),
					 un[i].name, un[i].name_len)) {
				offset += un[i].name_len;
				cnt    *= un[i].multiplier;
				break;
			}
		}
		if (un[i].name)
			break;	/* processed unit name */
		if ((time_str[offset] >= '0') && (time_str[offset] <= '9')) {
			cnt = (cnt * 10) + (time_str[offset] - '0');
			digit++;
			continue;
		}
		goto prob;
	}

	if (!digit)	/* No numbers after the '=' */
		return -1;

	*pos = offset - 1;
	*delta = cnt;
	return 0;

 prob:	*pos = offset - 1;
	return -1;
}

/* convert "HH:MM[:SS] [AM|PM]" string to numeric values
 * time_str (in): string to parse
 * pos (in/out): position of parse start/end
 * hour, minute, second (out): numberic values
 * RET: -1 on error, 0 otherwise
 */
static int _get_time(const char *time_str, int *pos, int *hour, int *minute,
		     int *second)
{
	int hr, min, sec;
	int offset = *pos;

	/* get hour */
	if ((time_str[offset] < '0') || (time_str[offset] > '9'))
		goto prob;
	hr = time_str[offset++] - '0';
	if (time_str[offset] != ':') {
		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		hr = (hr * 10) + time_str[offset++] - '0';
	}
	if (hr > 23) {
		offset -= 2;
		goto prob;
	}
	if (time_str[offset] != ':')
		goto prob;
	offset++;

	/* get minute */
	if ((time_str[offset] < '0') || (time_str[offset] > '9'))
                goto prob;
	min = time_str[offset++] - '0';
	if ((time_str[offset] < '0') || (time_str[offset] > '9'))
		goto prob;
	min = (min * 10)  + time_str[offset++] - '0';
	if (min > 59) {
		offset -= 2;
		goto prob;
	}

	/* get optional second */
	if (time_str[offset] == ':') {
		offset++;
		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		sec = time_str[offset++] - '0';
		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		sec = (sec * 10)  + time_str[offset++] - '0';
		if (sec > 59) {
			offset -= 2;
			goto prob;
		}
	} else
		sec = 0;

	while (isspace((int)time_str[offset])) {
		offset++;
	}
	if (xstrncasecmp(time_str+offset, "pm", 2)== 0) {
		hr += 12;
		if (hr > 23) {
			if (hr == 24)
				hr = 12;
			else
				goto prob;
		}
		offset += 2;
	} else if (xstrncasecmp(time_str+offset, "am", 2) == 0) {
		if (hr > 11) {
			if (hr == 12)
				hr = 0;
			else
				goto prob;
		}
		offset += 2;
	}

	*pos = offset - 1;
	*hour   = hr;
	*minute = min;
	*second = sec;
	return 0;

 prob:	*pos = offset;
	return -1;
}

/* convert "MMDDYY" "MM.DD.YY" or "MM/DD/YY" string to numeric values
 * or "YYYY-MM-DD string to numeric values
* time_str (in): string to parse
 * pos (in/out): position of parse start/end
 * month, mday, year (out): numberic values
 * RET: -1 on error, 0 otherwise
 */
static int _get_date(const char *time_str, int *pos, int *month, int *mday,
		     int *year)
{
	int mon, day, yr;
	int offset = *pos;
	int len;

	if (!time_str)
		goto prob;

	len = strlen(time_str);

	if ((len >= (offset+7)) && (time_str[offset+4] == '-')
	    && (time_str[offset+7] == '-')) {
		/* get year */
		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		yr = time_str[offset++] - '0';

		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		yr = (yr * 10) + time_str[offset++] - '0';

		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		yr = (yr * 10) + time_str[offset++] - '0';

		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		yr = (yr * 10) + time_str[offset++] - '0';

		offset++; // for the -

		/* get month */
		mon = time_str[offset++] - '0';
		if ((time_str[offset] >= '0') && (time_str[offset] <= '9'))
			mon = (mon * 10) + time_str[offset++] - '0';
		if ((mon < 1) || (mon > 12)) {
			offset -= 2;
			goto prob;
		}

		offset++; // for the -

		/* get day */
		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		day = time_str[offset++] - '0';
		if ((time_str[offset] >= '0') && (time_str[offset] <= '9'))
			day = (day * 10) + time_str[offset++] - '0';
		if ((day < 1) || (day > 31)) {
			offset -= 2;
			goto prob;
		}

		*pos = offset - 1;
		*month = mon - 1;	/* zero origin */
		*mday  = day;
		*year  = yr - 1900;     /* need to make it slurm_mktime
					   happy 1900 == "00" */
		return 0;
	}

	/* get month */
	mon = time_str[offset++] - '0';
	if ((time_str[offset] >= '0') && (time_str[offset] <= '9'))
		mon = (mon * 10) + time_str[offset++] - '0';
       	if ((mon < 1) || (mon > 12)) {
		offset -= 2;
		goto prob;
	}
	if ((time_str[offset] == '.') || (time_str[offset] == '/'))
		offset++;

	/* get day */
	if ((time_str[offset] < '0') || (time_str[offset] > '9'))
		goto prob;
	day = time_str[offset++] - '0';
	if ((time_str[offset] >= '0') && (time_str[offset] <= '9'))
		day = (day * 10) + time_str[offset++] - '0';
	if ((day < 1) || (day > 31)) {
		offset -= 2;
		goto prob;
	}
	if ((time_str[offset] == '.') || (time_str[offset] == '/'))
		offset++;

	/* get optional year */
	if ((time_str[offset] >= '0') && (time_str[offset] <= '9')) {
		yr = time_str[offset++] - '0';
		if ((time_str[offset] < '0') || (time_str[offset] > '9'))
			goto prob;
		yr = (yr * 10) + time_str[offset++] - '0';
	} else
		yr = 0;

	*pos = offset - 1;
	*month = mon - 1;	/* zero origin */
	*mday  = day;
	if (yr)
		*year  = yr + 100;	/* 1900 == "00" */
	return 0;

 prob:	*pos = offset;
	return -1;
}


/* Convert string to equivalent time value
 * input formats:
 *   today or tomorrow
 *   midnight, noon, fika (3 PM), teatime (4 PM)
 *   HH:MM[:SS] [AM|PM]
 *   MMDD[YY] or MM/DD[/YY] or MM.DD[.YY]
 *   MM/DD[/YY]-HH:MM[:SS]
 *   YYYY-MM-DD[THH:MM[:SS]]
 *   now[{+|-}count[seconds(default)|minutes|hours|days|weeks]]
 *
 * Invalid input results in message to stderr and return value of zero
 * NOTE: not thread safe
 * NOTE: by default this will look into the future for the next time.
 * if you want to look in the past set the past flag.
 */
extern time_t parse_time(const char *time_str, int past)
{
	time_t time_now;
	struct tm time_now_tm;
	int    hour = -1, minute = -1, second = 0;
	int    month = -1, mday = -1, year = -1;
	int    pos = 0;
	struct tm res_tm;
	time_t ret_time;

	if (xstrncasecmp(time_str, "uts", 3) == 0) {
		char *last = NULL;
		long uts = strtol(time_str+3, &last, 10);
		if ((uts < 1000000) || (uts == LONG_MAX) ||
		    (last == NULL) || (last[0] != '\0'))
			goto prob;
		return (time_t) uts;
	}

	time_now = time(NULL);
	localtime_r(&time_now, &time_now_tm);

	for (pos=0; ((time_str[pos] != '\0') && (time_str[pos] != '\n'));
	     pos++) {
		if (isblank((int)time_str[pos]) ||
		    (time_str[pos] == '-') || (time_str[pos] == 'T'))
			continue;
		if (xstrncasecmp(time_str+pos, "today", 5) == 0) {
			month = time_now_tm.tm_mon;
			mday = time_now_tm.tm_mday;
			year = time_now_tm.tm_year;
			pos += 4;
			continue;
		}
		if (xstrncasecmp(time_str+pos, "tomorrow", 8) == 0) {
			time_t later = time_now + (24 * 60 * 60);
			struct tm later_tm;
			localtime_r(&later, &later_tm);
			month = later_tm.tm_mon;
			mday = later_tm.tm_mday;
			year = later_tm.tm_year;
			pos += 7;
			continue;
		}
		if (xstrncasecmp(time_str+pos, "midnight", 8) == 0) {
			hour   = 0;
			minute = 0;
			second = 0;
			pos += 7;
			continue;
		}
		if (xstrncasecmp(time_str+pos, "noon", 4) == 0) {
			hour   = 12;
			minute = 0;
			second = 0;
			pos += 3;
			continue;
		}
		if (xstrncasecmp(time_str+pos, "fika", 4) == 0) {
			hour   = 15;
			minute = 0;
			second = 0;
			pos += 3;
			continue;
		}
		if (xstrncasecmp(time_str+pos, "teatime", 7) == 0) {
			hour   = 16;
			minute = 0;
			second = 0;
			pos += 6;
			continue;
		}
		if (xstrncasecmp(time_str+pos, "now", 3) == 0) {
			int i;
			long delta = 0;
			time_t later;
			struct tm later_tm;
			for (i=(pos+3); ; i++) {
				if (time_str[i] == '+') {
					pos += i;
					if (_get_delta(time_str, &pos, &delta))
						goto prob;
					break;
				}
				if (time_str[i] == '-') {
					pos += i;
					if (_get_delta(time_str, &pos, &delta))
						goto prob;
					delta = -delta;
					break;
				}
				if (isblank((int)time_str[i]))
					continue;
				if ((time_str[i] == '\0')
				    || (time_str[i] == '\n')) {
					pos += (i-1);
					break;
				}
				pos += i;
				goto prob;
			}
			later    = time_now + delta;
			localtime_r(&later, &later_tm);
			month = later_tm.tm_mon;
			mday = later_tm.tm_mday;
			year = later_tm.tm_year;
			hour = later_tm.tm_hour;
			minute = later_tm.tm_min;
			second = later_tm.tm_sec;
			continue;
		}

		if ((time_str[pos] < '0') || (time_str[pos] > '9'))
			/* invalid */
			goto prob;
		/* We have some numeric value to process */
		if ((time_str[pos+1] == ':') || (time_str[pos+2] == ':')) {
			/* Parse the time stamp */
			if (_get_time(time_str, &pos, &hour, &minute, &second))
				goto prob;
			continue;
		}

		if (_get_date(time_str, &pos, &month, &mday, &year))
			goto prob;
	}
/* 	printf("%d/%d/%d %d:%d\n",month+1,mday,year,hour+1,minute);  */


	if ((hour == -1) && (month == -1))		/* nothing specified, time=0 */
		return (time_t) 0;
	else if ((hour == -1) && (month != -1)) {	/* date, no time implies 00:00 */
		hour = 0;
		minute = 0;
	}
	else if ((hour != -1) && (month == -1)) {
		/* time, no date implies soonest day */
		if (past || (hour >  time_now_tm.tm_hour)
		    ||  ((hour == time_now_tm.tm_hour)
			 && (minute > time_now_tm.tm_min))) {
			/* today */
			month = time_now_tm.tm_mon;
			mday = time_now_tm.tm_mday;
			year = time_now_tm.tm_year;
		} else {/* tomorrow */
			time_t later = time_now + (24 * 60 * 60);
			struct tm later_tm;
			localtime_r(&later, &later_tm);
			month = later_tm.tm_mon;
			mday = later_tm.tm_mday;
			year = later_tm.tm_year;
		}
	}
	if (year == -1) {
		if (past) {
			if (month > time_now_tm.tm_mon) {
				/* last year */
				year = time_now_tm.tm_year - 1;
			} else  {
				/* this year */
				year = time_now_tm.tm_year;
			}
		} else if ((month  >  time_now_tm.tm_mon)
			   ||  ((month == time_now_tm.tm_mon)
				&& (mday > time_now_tm.tm_mday))
			   ||  ((month == time_now_tm.tm_mon)
				&& (mday == time_now_tm.tm_mday)
				&& (hour >  time_now_tm.tm_hour))
			   ||  ((month == time_now_tm.tm_mon)
				&& (mday == time_now_tm.tm_mday)
				&& (hour == time_now_tm.tm_hour)
				&& (minute > time_now_tm.tm_min))) {
			/* this year */
			year = time_now_tm.tm_year;
		} else {
			/* next year */
			year = time_now_tm.tm_year + 1;
		}
	}

	/* convert the time into time_t format */
	memset(&res_tm, 0, sizeof(res_tm));
	res_tm.tm_sec   = second;
	res_tm.tm_min   = minute;
	res_tm.tm_hour  = hour;
	res_tm.tm_mday  = mday;
	res_tm.tm_mon   = month;
	res_tm.tm_year  = year;

/* 	printf("%d/%d/%d %d:%d\n",month+1,mday,year,hour,minute); */
	if ((ret_time = slurm_mktime(&res_tm)) != -1)
		return ret_time;

 prob:	fprintf(stderr, "Invalid time specification (pos=%d): %s\n", pos, time_str);
	errno = ESLURM_INVALID_TIME_VALUE;
	return (time_t) 0;
}

/*
 * Smart date for @epoch, relative to current date.
 * Maximum output length: 12 characters + '\0'
 *      19 Jan 2003	(distant past or future)
 *     Ystday 20:13
 *         12:26:38	(today)
 *     Tomorr 03:22
 *        Sat 02:17	(next Saturday)
 *     18 Jun 13:14	(non-close past or future)
 *     012345678901
 * Uses base-10 YYYYddd numbers to compute date distances.
 */
static char *_relative_date_fmt(const struct tm *when)
{
	static int todays_date;
	int distance = 1000 * (when->tm_year + 1900) + when->tm_yday;

	if (!todays_date) {
		time_t now = time(NULL);
		struct tm tm;

		localtime_r(&now, &tm);
		todays_date = 1000 * (tm.tm_year + 1900) + tm.tm_yday;
	}

	distance -= todays_date;
	if (distance == -1)			/* yesterday */
		return "Ystday %H:%M";
	if (distance == 0)			/* same day */
		return "%H:%M:%S";
	if (distance == 1)			/* tomorrow */
		return "Tomorr %H:%M";
	if (distance < -365 || distance > 365)	/* far distance */
		return "%-d %b %Y";
	if (distance < -1 || distance > 6)	/* medium distance */
		return "%-d %b %H:%M";
	return "%a %H:%M";			/* near distance */
}

static void _make_time_str_internal(time_t *time, bool utc, char *string,
				    int size)
{
	struct tm time_tm;

	if (utc)
		gmtime_r(time, &time_tm);
	else
		localtime_r(time, &time_tm);
	if ((*time == (time_t) 0) || (*time == (time_t) INFINITE)) {
		snprintf(string, size, "Unknown");
	} else if (*time == (time_t) NO_VAL) {
		snprintf(string, size, "None");
	} else {
		static char fmt_buf[32];
		static const char *display_fmt = "%FT%T";

		if (!utc) {
			char *fmt = getenv("SLURM_TIME_FORMAT");

			if ((!fmt) || (!*fmt) || (!xstrcmp(fmt, "standard"))) {
				;
			} else if (xstrcmp(fmt, "relative") == 0) {
				display_fmt = _relative_date_fmt(&time_tm);
			} else if ((strchr(fmt, '%')  == NULL) ||
				   (strlen(fmt) >= sizeof(fmt_buf))) {
				error("invalid SLURM_TIME_FORMAT = '%s'", fmt);
			} else {
				strlcpy(fmt_buf, fmt, sizeof(fmt_buf));
				display_fmt = fmt_buf;
			}
		}

		slurm_strftime(string, size, display_fmt, &time_tm);
	}
}

/*
 * slurm_make_time_str - convert time_t to formatted string for user output
 *
 * The format depends on the environment variable SLURM_TIME_FORMAT, which may
 * be set to 'standard' (fallback, same as if not set), 'relative' (format is
 * relative to today's date and optimized for space), or a strftime(3) string.
 *
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 * IN size - length of string buffer, we recommend a size of 32 bytes to
 *	easily support different site-specific formats
 */
extern void
slurm_make_time_str (time_t *time, char *string, int size)
{
	_make_time_str_internal(time, false, string, size);
}

/*
 * Convert time_t to fixed "%FT%T" formatted string expressed in UTC.
 *
 * IN time - a timestamp
 * OUT string - pointer user defined buffer
 * IN size - length of string buffer (recommend 32 bytes)
 */
extern void parse_time_make_str_utc(time_t *time, char *string, int size)
{
	_make_time_str_internal(time, true, string, size);
}

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
extern int time_str2secs(const char *string)
{
	int d = 0, h = 0, m = 0, s = 0;

	if ((string == NULL) || (string[0] == '\0'))
		return NO_VAL;	/* invalid input */

	if ((!xstrcasecmp(string, "-1"))
	    || (!xstrcasecmp(string, "INFINITE"))
	    || (!xstrcasecmp(string, "UNLIMITED"))) {
		return INFINITE;
	}

	if (! _is_valid_timespec(string))
		return NO_VAL;

	if (xstrchr(string, '-')) {
		/* days-[hours[:minutes[:seconds]]] */
		sscanf(string, "%d-%d:%d:%d", &d, &h, &m, &s);
		d *= 86400;
		h *= 3600;
		m *= 60;
	} else {
		if (sscanf(string, "%d:%d:%d", &h, &m, &s) == 3) {
			/* hours:minutes:seconds */
			h *= 3600;
			m *= 60;
		} else {
			/*
			 * minutes[:seconds]
			 * h is minutes here and m is seconds due
			 * to sscanf parsing left to right
			 */
			s = m;
			m = h * 60;
			h = 0;
		}
	}

	return (d + h + m + s);
}

extern int time_str2mins(const char *string)
{
	int i = time_str2secs(string);
	if ((i != INFINITE) &&  (i != NO_VAL))
		i = (i + 59) / 60;	/* round up */
	return i;
}

extern void secs2time_str(time_t time, char *string, int size)
{
	if (time == INFINITE) {
		snprintf(string, size, "UNLIMITED");
	} else {
		long days, hours, minutes, seconds;
		seconds = time % 60;
		minutes = (time / 60) % 60;
		hours = (time / 3600) % 24;
		days = time / 86400;

		if ((days < 0) || (hours < 0) || (minutes < 0) ||
		    (seconds < 0)) {
			snprintf(string, size, "INVALID");
		} else if (days) {
			snprintf(string, size,
				"%ld-%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		} else {
			snprintf(string, size,
				"%2.2ld:%2.2ld:%2.2ld",
				hours, minutes, seconds);
		}
	}
}

extern void mins2time_str(uint32_t time, char *string, int size)
{
	if (time == INFINITE) {
		snprintf(string, size, "UNLIMITED");
	} else {
		long days, hours, minutes, seconds;
		seconds = 0;
		minutes = time % 60;
		hours = time / 60 % 24;
		days = time / 1440;

		if ((days < 0) || (hours < 0) || (minutes < 0) ||
		    (seconds < 0)) {
			snprintf(string, size, "INVALID");
		} else if (days) {
			snprintf(string, size,
				"%ld-%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		} else {
			snprintf(string, size,
				"%2.2ld:%2.2ld:%2.2ld",
				hours, minutes, seconds);
		}
	}
}

extern char *mon_abbr(int mon)
{
	switch(mon) {
	case 0:
		return "Ja";
		break;
	case 1:
		return "Fe";
		break;
	case 2:
		return "Ma";
		break;
	case 3:
		return "Ap";
		break;
	case 4:
		return "Ma";
		break;
	case 5:
		return "Ju";
		break;
	case 6:
		return "Jl";
		break;
	case 7:
		return "Au";
		break;
	case 8:
		return "Se";
		break;
	case 9:
		return "Oc";
		break;
	case 10:
		return "No";
		break;
	case 11:
		return "De";
		break;
	default:
		return "Un";
		break;
	}
}
