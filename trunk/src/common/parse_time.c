/*****************************************************************************\
 *  src/common/parse_time.c - time parsing utility functions
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <time.h>
#include <strings.h>
#ifndef   __USE_ISOC99
#  define __USE_ISOC99 /* isblank() */
#endif
#include <ctype.h>

#include <slurm/slurm.h>

#define _RUN_STAND_ALONE 0

time_t     time_now;
struct tm *time_now_tm;

/* convert time differential string into a number of seconds
 * time_str (in): string to parse
 * pos (in/out): position of parse start/end
 * delta (out): delta in seconds
 * RET: -1 on error, 0 otherwise
 */
static int _get_delta(char *time_str, int *pos, long *delta)
{
	int offset;
	long cnt = 0;
	int digit = 0;

	offset = (*pos) + 1;
	for ( ; ((time_str[offset]!='\0')&&(time_str[offset]!='\n')); offset++) {
		if (isspace(time_str[offset]))
			continue;
		if (strncasecmp(time_str+offset, "minutes", 7) == 0) {
			cnt *= 60;
			offset +=7;
			break;
		}
		if (strncasecmp(time_str+offset, "hours", 5) == 0) {
			cnt *= (60 * 60);
			offset += 5;
			break;
		}
		if (strncasecmp(time_str+offset, "days", 4) == 0) {
			cnt *= (24 * 60 * 60);
			offset += 4;
			break;
		}
		if (strncasecmp(time_str+offset, "weeks", 5) == 0) {
			cnt *= (7 * 24 * 60 * 60);
			offset += 5;
			break;
		}
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
static int 
_get_time(char *time_str, int *pos, int *hour, int *minute, int * second)
{
	int hr, min, sec;
	int offset = *pos;

	/* get hour */
	if ((time_str[offset] < '0') || (time_str[offset] > '9'))
		goto prob;
	hr = time_str[offset++] - '0';
	if ((time_str[offset] < '0') || (time_str[offset] > '9'))
		goto prob;
	hr = (hr * 10) + time_str[offset++] - '0';
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

	while (isspace(time_str[offset])) {
		offset++;
	}
	if (strncasecmp(time_str+offset, "pm", 2)== 0) {
		hr += 12;
		if (hr > 23) {
			if (hr == 24)
				hr = 12;
			else
				goto prob;
		}
		offset += 2;
	} else if (strncasecmp(time_str+offset, "am", 2) == 0) {
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
 * time_str (in): string to parse
 * pos (in/out): position of parse start/end
 * month, mday, year (out): numberic values
 * RET: -1 on error, 0 otherwise
 */
static int _get_date(char *time_str, int *pos, int *month, int *mday, int *year)
{
	int mon, day, yr;
	int offset = *pos;

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
 *   midnight, noon, teatime (4PM)
 *   HH:MM[:SS] [AM|PM]
 *   MMDD[YY] or MM/DD[/YY] or MM.DD[.YY]
 *   MM/DD[/YY]-HH:MM[:SS]
 *   now + count [minutes | hours | days | weeks]
 * 
 * Invalid input results in message to stderr and return value of zero
 * NOTE: not thread safe
 * NOTE: by default this will look into the future for the next time.
 * if you want to look in the past set the past flag.
 */
extern time_t parse_time(char *time_str, int past)
{
	int    hour = -1, minute = -1, second = 0;
	int    month = -1, mday = -1, year = -1;
	int    pos = 0;
	struct tm res_tm;

	time_now = time(NULL);
	time_now_tm = localtime(&time_now);

	for (pos=0; ((time_str[pos] != '\0')&&(time_str[pos] != '\n')); pos++) {
		if (isblank(time_str[pos]) || (time_str[pos] == '-'))
			continue;
		if (strncasecmp(time_str+pos, "today", 5) == 0) {
			month = time_now_tm->tm_mon;
			mday  = time_now_tm->tm_mday;
			year  = time_now_tm->tm_year;
			pos += 4;
			continue;
		}
		if (strncasecmp(time_str+pos, "tomorrow", 8) == 0) {
			time_t later = time_now + (24 * 60 * 60);
			struct tm *later_tm = localtime(&later);
			month = later_tm->tm_mon;
			mday  = later_tm->tm_mday;
			year  = later_tm->tm_year;
			pos += 7;
			continue;
		}
		if (strncasecmp(time_str+pos, "midnight", 8) == 0) {
			hour   = 0;
			minute = 0;
			second = 0;
			pos += 7;
			continue;
		}
		if (strncasecmp(time_str+pos, "noon", 4) == 0) {
			hour   = 12;
			minute = 0;
			pos += 3;
			continue;
		}
		if (strncasecmp(time_str+pos, "teatime", 7) == 0) {
			hour   = 16;
			minute = 0;
			pos += 6;
			continue;
		}
		if (strncasecmp(time_str+pos, "now", 3) == 0) {
			int i;
			long delta = 0;
			time_t later;
			struct tm *later_tm;
			for (i=(pos+3); ; i++) {
				if (time_str[i] == '+') {
					pos += i; 
					if (_get_delta(time_str, &pos, &delta))
						goto prob;
					break;
				}
				if (isblank(time_str[i]))
					continue;
				if ((time_str[i] == '\0') || (time_str[i] == '\n')) {
					pos += (i-1);
					break;
				}
				pos += i;
				goto prob;
			}
			later    = time_now + delta;
			later_tm = localtime(&later);
			month    = later_tm->tm_mon;
			mday     = later_tm->tm_mday;
			year     = later_tm->tm_year;
			hour     = later_tm->tm_hour;
			minute   = later_tm->tm_min;
			second   = later_tm->tm_sec;
			continue;
		}

		if ((time_str[pos] < '0') || (time_str[pos] > '9'))	/* invalid */
			goto prob;
		/* We have some numeric value to process */
		if (time_str[pos+2] == ':') {	/* time */
			if (_get_time(time_str, &pos, &hour, &minute, &second))
				goto prob;
			continue;
		}
		if (_get_date(time_str, &pos, &month, &mday, &year))
			goto prob;
	}
	/* printf("%d/%d/%d %d:%d\n",month+1,mday,year+1900,hour+1,minute); */


	if ((hour == -1) && (month == -1))		/* nothing specified, time=0 */
		return (time_t) 0;
	else if ((hour == -1) && (month != -1)) {	/* date, no time implies 00:00 */
		hour = 0;
		minute = 0;
	}
	else if ((hour != -1) && (month == -1)) {	
		/* time, no date implies soonest day */
		if (past || (hour >  time_now_tm->tm_hour)
		    ||  ((hour == time_now_tm->tm_hour) 
			 && (minute > time_now_tm->tm_min))) {
			/* today */
			month = time_now_tm->tm_mon;
			mday  = time_now_tm->tm_mday;
			year  = time_now_tm->tm_year;
		} else {/* tomorrow */
			time_t later = time_now + (24 * 60 * 60);
			struct tm *later_tm = localtime(&later);
			month = later_tm->tm_mon;
			mday  = later_tm->tm_mday;
			year  = later_tm->tm_year;
		}
	}
	if (year == -1) {
		if (past || (month  >  time_now_tm->tm_mon)
		    ||  ((month == time_now_tm->tm_mon) 
			 && (mday >  time_now_tm->tm_mday))
		    ||  ((month == time_now_tm->tm_mon) 
			 && (mday == time_now_tm->tm_mday)
			 && (hour >  time_now_tm->tm_hour)) 
		    ||  ((month == time_now_tm->tm_mon) 
			 && (mday == time_now_tm->tm_mday)
			 && (hour == time_now_tm->tm_hour) 
			 && (minute > time_now_tm->tm_min))) {
			/* this year */
			year = time_now_tm->tm_year;
		} else {
			year = time_now_tm->tm_year + 1;
		}
	}

	/* convert the time into time_t format */
	bzero(&res_tm, sizeof(res_tm));
	res_tm.tm_sec   = second;
	res_tm.tm_min   = minute;
	res_tm.tm_hour  = hour;
	res_tm.tm_mday  = mday;
	res_tm.tm_mon   = month;
	res_tm.tm_year  = year;
	res_tm.tm_isdst = -1;
	return mktime(&res_tm);

 prob:	fprintf(stderr, "Invalid time specification (pos=%d): %s\n", pos, time_str);
	return (time_t) 0;
}

#if _RUN_STAND_ALONE
int main(int argc, char *argv[])
{
	char in_line[128];
	time_t when;

	while (1) {
		printf("time> ");
		if ((fgets(in_line, sizeof(in_line), stdin) == NULL)
		||  (in_line[0] == '\n'))
			break;
		when = parse_time(in_line);
		if (when)
			printf("%s", asctime(localtime(&when)));
	}
}
#endif

/*
 * slurm_make_time_str - convert time_t to string with a format of
 *	"month/date hour:min:sec" for use in user command output
 *
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 * IN size - length of string buffer, we recommend a size of 32 bytes to 
 *	easily support different site-specific formats
 */
extern void
slurm_make_time_str (time_t *time, char *string, int size)
{
	struct tm time_tm;

	localtime_r(time, &time_tm);
	if ( *time == (time_t) 0 ) {
		snprintf(string, size, "Unknown");
	} else {
#ifdef USE_ISO_8601
		/* Format YYYY-MM-DDTHH:MM:SS, ISO8601 standard format,
		 * NOTE: This is expected to break Maui, Moab and LSF
		 * schedulers management of SLURM. */
		snprintf(string, size,
			"%4.4u-%2.2u-%2.2uT%2.2u:%2.2u:%2.2u",
			(time_tm.tm_year + 1900), (time_tm.tm_mon+1), 
			time_tm.tm_mday, time_tm.tm_hour, time_tm.tm_min, 
			time_tm.tm_sec);
#else
		/* Format MM/DD-HH:MM:SS */
		snprintf(string, size,
			"%2.2u/%2.2u-%2.2u:%2.2u:%2.2u",
			(time_tm.tm_mon+1), time_tm.tm_mday,
			time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec);

#endif
	}
}

/* Convert a string to an equivalent time value
 * input formats:
 *   min
 *   min:sec
 *   hr:min:sec
 *   days-hr:min:sec
 *   days-hr
 * output:
 *   minutes  (or -2 on error, INFINITE is -1 as defined in slurm.h)
 *   if unlimited is the value of string)
 */
extern int time_str2mins(char *string)
{
	int days = -1, hr = -1, min = -1, sec = -1;
	int i, tmp = 0, res = 0;

	if ((string == NULL) || (string[0] == '\0'))
		return -1;	/* invalid input */
	if ((!strcasecmp(string, "-1")) ||
	    (!strcasecmp(string, "INFINITE")) ||
	    (!strcasecmp(string, "UNLIMITED"))) {
		return INFINITE;
	}

	for (i=0; ; i++) {
		if ((string[i] >= '0') && (string[i] <= '9')) {
			tmp = (tmp * 10) + (string[i] - '0');
		} else if (string[i] == '-') {
			if (days != -1)
				return -2;	/* invalid input */
			days = tmp;
			tmp = 0;
		} else if ((string[i] == ':') || (string[i] == '\0')) {
			if (min == -1) {
				min = tmp;
			} else if (sec == -1) {
				sec = tmp;
			} else if (hr == -1) {
				hr = min;
				min = sec;
				sec = tmp;
			} else
				return -2;	/* invalid input */
			tmp = 0;
		} else
			return -2;		/* invalid input */

		if (string[i] == '\0')
			break;
	}

	if ((days != -1) && (hr == -1) && (min != 0)) {
		/* format was "days-hr" or "days-hr:min" */
		hr = min;
		min = sec;
		sec = 0;
	}

	if (days == -1)
		days = 0;
	if (hr == -1)
		hr = 0;
	if (min == -1)
		min = 0;
	if (sec == -1)
		sec = 0;
	res = (((days * 24) + hr) * 60) + min;
	if (sec > 0)
		res++;	/* round up */
	return res;
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

		if (days)
			snprintf(string, size,
				"%ld-%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		else
			snprintf(string, size,
				"%2.2ld:%2.2ld:%2.2ld", 
				hours, minutes, seconds);
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

		if (days)
			snprintf(string, size,
				"%ld-%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		else
			snprintf(string, size,
				"%2.2ld:%2.2ld:%2.2ld", 
				hours, minutes, seconds);
	}
}
