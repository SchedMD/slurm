/*
 *****************************************************************************
 *  Copyright (C) 1993-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by: Robert Wood, bwood@llnl.gov
 *  UCRL-CODE-155981.
 *
 *  This file is part of the LCRM (Livermore Computing Resource Management)
 *  system. For details see http://www.llnl.gov/lcrm.
 *
 *  LCRM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  LCRM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with LCRM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************
 *
 *	Author:	Gregg Hommes
 *
 *	datetoi() parses a date specification in and returns an integer
 *	corresponding to the seconds since 1/1/70 00:00:00.
 *
 *	The valid string formats for conversion can include
 *	most combinations of one or more of "time_of_day", "date",
 *	"meridian", and "timezone".
 *
 *	The meridian can be specified as a unique element within the string,
 *	or be appended to the "date" or "time_of_day" specifications. valid
 *	meridians are:
 *		am
 *		pm
 *		m
 *
 *	The "timezone" can be specified as a unique element within the string,
 *	or be appended to the "date" or "time_of_day" specifications with a
 *	"-timezone". All North American time zones (standard and daylight
 *	savings times) are acceptable.
 *
 *	Valid "time_of_day" specifications are:
 *		hh
 *		hh:mm
 *		hh:mm:ss
 *		noon
 *		midnight
 *
 *	Valid date specifications are:
 *		yy-mm
 *		yyyy-mm
 *		yy-mm-dd
 *		yyyy-mm-dd
 *		yy-month
 *		yyyy-month
 *		yy-month-dd
 *		yyyy-month-dd
 *
 *	NOTE: Any 2-digit year in the preceeding specifications must
 *	be >= 32 or the specification becomes ambiguous. Additioal
 *	acceptable formats are:
 *
 *		dd-mm-yy
 *		dd-mm-yyyy
 *		dd-month-yy
 *		dd-month-yyyy
 *		dd-mm
 *		dd-month
 *		month/dd/yyyy
 *		month/dd/yy
 *		month/dd
 *		mm/dd/yyyy
 *		mm/dd/yy
 *		mm/dd
 *		yesterday
 *		today
 *		tomorrow
 *		sunday - saturday
 *
 *	Any date specification allowing a month will accept any 3 or more
 *	character abbreviation of the name of a month. If a day of the week
 *	has been specified, it assumes the next such occurence of that day,
 *	unless some combination of year, month or day has also been provided.
 *	In that case, the weekday is ignored.
 *
 *	Other formats not indicated may be successfully converted if the
 *	function can determine unambiguously how to parse the data.
 */
#include "lrm_install.h"
#include "liblrmsup.h"

#define NUM_WEEKDAYS		7
#define NUM_MONTHS		12
#define	NUM_TIMEZONES		20
#define	NUM_MERIDIANS		3

#define	NO_TOKEN		0
#define	GOT_TOKEN		1

#define	TOKEN_OK		1
#define	DATE_FMT_ERR		-1

#define	NO_MERIDIAN		-1
#define	MERIDIAN_AM		0
#define	MERIDIAN_PM		1
#define	MERIDIAN_M		2

static char *days_of_week[NUM_WEEKDAYS] = {
	"sunday", "monday", "tuesday", "wednesday",
	"thursday", "friday", "saturday" };

static char *months[NUM_MONTHS] = {
	"january", "february", "march", "april", "may", "june", "july",
	"august", "september", "october", "november", "december" };

/*
 *	List all supported timezones and their time in minutes relative to GMT.
 */
struct tz_desc {
	char	*tz_name;
	int	offset_from_gmt;
};

struct tz_desc time_zones[NUM_TIMEZONES] = {
	{ "gmt", 0 },			/* Greenwich Mean Time */
	{ "z", 0 },			/* Zulu */
	{ "ut", 0 },			/* Universal Time */
	{ "nst", 210 },
	{ "adt", 180 },			/* Atlantic Daylight Savings Time */
	{ "ast", 240 },			/* Atlantic Standard Time */
	{ "edt", 240 },			/* Eastern Daylight Savings Time */
	{ "est", 300 },			/* Eastern Standard Time */
	{ "cdt", 300 },			/* Central Daylight Savings Time */
	{ "cst", 360 },			/* Central Standard Time */
	{ "mdt", 360 },			/* Mountain Daylight Savings Time */
	{ "mst", 420 },			/* Mountain Standard Time */
	{ "pdt", 420 },			/* Pacific Daylight Savings Time */
	{ "pst", 480 },			/* Pacific Standard Time */
	{ "ydt", 480 },
	{ "yst", 540 },
	{ "hdt", 540 },
	{ "hst", 600 },
	{ "bdt", 600 },
	{ "bst", 660 }
};

/*
 *	List all supported meridian values
 */
static char *meridians[NUM_MERIDIANS] = { "am", "pm", "m" };

/*
 *	Temporary structure for saving values generated while
 *	parsing the components of the provided date string
 */
struct time_data_t {
	int	hour;
	int	minute;
	int	second;
	int	year;
	int	month;
	int	day;
	int	weekday;
	int	timezone;
	int	meridian;
};

/*
 *	get_next_token() finds the next token in the provided string equivalent
 *	to one of the provided delimiters (or space & tab if no delimiters are
 *	provided). The found token is copied into the provided buffer. It
 *	returns GOT_TOKEN on success, NO_TOKEN or -1 on error.
 *
 *	NOTE: The contents of source_str is modified to point to the next
 *	character position at which to begin scanning for the next token, and
 *	the contents of token_len is modified to contain the number of bytes
 *	returned in the buffer.
 */
static int get_next_token(char **source_str, char *delimiter_str, char *token,
	size_t *token_len)
{
	int	i;
	int	num_delimiters = 0;
	char	*delimiters = " \t";
	size_t	new_token_len = 0;

	if ((token == NULL) || (token_len == NULL)) return(-1);

	if (delimiter_str != NULL) delimiters = delimiter_str;
	num_delimiters = strlen(delimiters);
/*
 *	Skip any leading white space or delimiters
 */
	while (1){
		if (**source_str == 0) return(NO_TOKEN);
		for (i = 0; i < num_delimiters; i++)
			if (**source_str != delimiters[i]) break;
		if (i != num_delimiters) break;
		(*source_str)++;
	}

	if (**source_str == 0) return(NO_TOKEN);
	if (*token_len < 1) return(-1);
/*
 *	Copy the next token into the provided storage area...
 */
	memset(token, 0, *token_len);
	for ( ; ; token++, (*source_str)++) {
		if (**source_str == 0) {
			*token_len = new_token_len;
			return(GOT_TOKEN);
		}
		for (i = 0; i < num_delimiters; i++)
			if (**source_str == delimiters[i]) break;
		if (i < num_delimiters) {
			(*source_str)++;
			*token_len = new_token_len;
			return(GOT_TOKEN);
		}
		if (new_token_len >= (*token_len - 1)) {
			*token_len = new_token_len;
			return(GOT_TOKEN);
		}
		*token = **source_str;
		new_token_len += 1;
	}

	return(NO_TOKEN);
}

/*
 *	get_weekday() returns the index into days_of_week of the specified day
 *	if found or -1 on error.
 */
static int get_weekday(char *token)
{
	size_t	cmp_len;
	int	i;

	if (token == NULL) return(-1);

	cmp_len = MAX(3, strlen(token));
	for (i = 0; i < NUM_WEEKDAYS; i++) {
		if (nstreq(token, days_of_week[i], cmp_len)) return(i);
	}

	return(-1);
}

/*
 *	get_month() returns the index into months of the possibly abbreviated
 *	specified month if found or -1 on error.
 */
static int get_month(char *token)
{
	size_t	cmp_len;
	int	i;

	if (token == NULL) return(-1);

	cmp_len = MAX(3, strlen(token));
	for (i = 0; i < NUM_MONTHS; i++) {
		if (nstreq(token, months[i], cmp_len)) return(i);
	}

	return(-1);
}

/*
 *	set_year examines the given value to determines if it is a valid year
 *	specification. If so, it sets the time_data_t year value appropriately.
 */
static int set_year(int year, struct time_data_t *td)
{
	if (year < 0) return(0);
	if ((year >= 1900) && (year <= 1969)) return(0);
	else if (year >= 1900) {
		td->year = year - 1900;
		return(1);
	}
	else if ((year < 100) && (year > 69)) td->year = year;
	else if ((year < 100) && (year > 37)) return(0);
	else if (year < 100) td->year = year + 100;
	else return(0);

	return(1);
}

/*
 *	is_number() returns 1 if the first num_characters of token contain only
 *	digits. Otherwise returns 0.
 */
static int is_number(char *token, int num_characters)
{
	int	i;

	if ((token == NULL) || (num_characters < 1)) return(0);

	for (i = 0; i < num_characters; i++) {
		if (!isdigit(token[i])) return(0);
	}

	return(1);
}

#define	DASH_FORMAT	0
#define SLASH_FORMAT	1

#define	IS_YEAR		1
#define	IS_MONTH	2
#define	IS_DAY		3
/*
 *	parse_date() parses a date specification and updates the time_data_t
 *	structure with appropriate values. It returns TOKEN_OK on success or
 *	DATE_FMT_ERR or -1 on error.
 */
static int parse_date(char *token, struct time_data_t *td)
{
	size_t	segment_len;
	int	format = SLASH_FORMAT;
	int	val = -1;
	int	i = 0;
	int	seg_type[3];
	char	segment[16];

/*
 *	Do a couple quick sanity checks before we even begin parsing
 */
	if ((token == NULL) || (td == NULL))
		return(-1);
/*
 *	Start narrowing down the format by seeing if the specification
 *	contains a '/' or a '-'...
 */
	if (strchr(token, '-') != NULL) {
		format = DASH_FORMAT;
	}
/*
 *	Grab the first segment of the token and see what we got
 *	How we interpret the 2nd and 3rd segments of the
 *	token depend on what we find in the first segment.
 */
	segment_len = sizeof(segment);
	if (get_next_token(&token, "-/", segment, &segment_len) == NO_TOKEN)
		return(DATE_FMT_ERR);

	val = atoi(segment);
	if (format == DASH_FORMAT) {
		if ((segment_len == 4) || (val > 31)) {
			seg_type[0] = IS_YEAR;
			seg_type[1] = IS_MONTH;
			seg_type[2] = IS_DAY;
		} else {
			seg_type[0] = IS_DAY;
			seg_type[1] = IS_MONTH;
			seg_type[2] = IS_YEAR;
		}
	} else {
		seg_type[0] = IS_MONTH;
		seg_type[1] = IS_DAY;
		seg_type[2] = IS_YEAR;
	}

	for (i = 0; i < 3; i++) {
		switch(seg_type[i]) {
			case IS_YEAR:
				if (td->year >= 0) return(DATE_FMT_ERR);
				if (!set_year(val, td)) return(DATE_FMT_ERR);
				break;
			case IS_MONTH:
				if (td->month >= 0) return(DATE_FMT_ERR);
				if (is_number(segment, segment_len))
					td->month = val - 1;
				else td->month = get_month(segment);
				if ((td->month < 0) || (td->month > 11))
					return(DATE_FMT_ERR);
				break;
			case IS_DAY:
				if (td->day >= 0) return(DATE_FMT_ERR);
				if (!is_number(segment, segment_len))
					return(DATE_FMT_ERR);
				if ((val < 0) || (val > 31))
					return(DATE_FMT_ERR);
				td->day = val;
				break;
			}

		segment_len = sizeof(segment);
		if (get_next_token(&token, "-/", segment, &segment_len) ==
		    NO_TOKEN)
			break;
		val = atoi(segment);
	}

	if (i > 2) return(DATE_FMT_ERR);

	return(TOKEN_OK);
}

/*
 *	get_timezone() returns the index into timezones of the specified
 *	timezone (or value with timezone appended) if found or -1 on error.
 *
 *	NOTE: If the timezone is found, it will be stripped out of the token.
 */
static int get_timezone(char *token)
{
	size_t	token_len, suffix_len;
	char	*zptr;
	int	i;

	if (token == NULL) return(-1);
/*
 *	First check for a time zone as a tokn on its own
 */
	for (i = 0; i < NUM_TIMEZONES; i++) {
		if (streq(token, time_zones[i].tz_name)) {
			*token = 0;
			return(i);
		}
	}
/*
 *	Now see if maybe the timezone has been appended to another
 *	specifier with '-timezone'
 */
	token_len = strlen(token);
	for (i = 0; i < NUM_TIMEZONES; i++) {
		suffix_len = strlen(time_zones[i].tz_name) + 1;
		if (suffix_len > token_len) continue;
		zptr = token + token_len - suffix_len;
		if (*zptr != '-') continue;
		if (streq((zptr+1), time_zones[i].tz_name)) {
			*zptr = 0;
			return(i);
		}
	}

	return(-1);
}

/*
 *	get_meridian() returns the index into the meridians table of the
 *	specified meridian (or value with meridian appended) if found. If
 *	no meridian is found, it returns NO_MERIDIAN. Otherwise, it returns -1.
 *
 *	NOTE: If the meridian is found, it will be stripped out of the token.
 */
static int get_meridian(char *token)
{
	size_t	token_len, suffix_len;
	char	*mptr;
	int	i;

	if (token == NULL) return(NO_MERIDIAN);
/*
 *	First check for a meridian as a token on its own
 */
	for (i = 0; i < NUM_MERIDIANS; i++) {
		if (streq(token, meridians[i])) {
			*token = 0;
			return(i);
		}
	}
/*
 *	Now see if maybe the meridian is appended to the token
 */
	token_len = strlen(token);
	for (i = 0; i < NUM_MERIDIANS; i++) {
		suffix_len = strlen(meridians[i]);
		if (suffix_len > token_len) continue;
		mptr = token + token_len - suffix_len;
		if (streq(mptr, meridians[i])) {
			*mptr = 0;
			return(i);
		}
	}

	return(-1);
}

/*
 *	get_day_offset() sets the appropriate values in the time_data_t
 *	structure for indirectly specified dates such as 'yesterday', 'today',
 *	etc. If the specified value is such a specification, it returns 1.
 *	If the value is not an indirectly specified date, it returns 0.
 *	On error it returns -1;
 */
static int get_day_offset(char *token, struct time_data_t *td,
			  struct tm *current)
{
	int	day_offset = 0;

/*
 *	If token indicates the date is 'today', "tomorrow", or "yesterday"
 *	set the desired date to the current value plus 0, 1, or -1 days
 *	respectively.
 */
	if (streq(token, "today")) day_offset = 0;
	else if (streq(token, "tomorrow")) day_offset = 1;
	else if (streq(token, "yesterday")) day_offset = -1;
	else return(0);

	if ((td->year >= 0) || (td->month >= 0) ||
	    (td->day >= 0)) return(DATE_FMT_ERR);

	td->year = current->tm_year;
	td->month = current->tm_mon;
	td->day = current->tm_mday + day_offset;

	return(1);
}

/*
 *	parse_time() parses a time specification and updates the time_data_t
 *	structure with appropriate values. It returns a value of TOKEN_OK if
 *	no errors occurred. Any other value indicates an error.
 *
 *	Valid time specifications are: hh:mm or hh:mm:ss
 */
static int parse_time(char *token, struct time_data_t *td)
{
	size_t	segment_len;
	char	segment[16];

/*
 *	Do a couple quick sanity checks before we even begin parsing
 */
	if ((token == NULL) || (td == NULL)) return(-1);

	if (td->hour >= 0) return(DATE_FMT_ERR);
/*
 *	First pull off any hour specification
 */
	segment_len = sizeof(segment);
	if (get_next_token(&token, ":", segment, &segment_len) == NO_TOKEN)
		return(DATE_FMT_ERR);

	if (!is_number(segment, segment_len)) return(DATE_FMT_ERR);
	td->hour = atoi(segment);
	if (td->hour > 24) return(DATE_FMT_ERR);
/*
 *	Now get any minute specification
 */
	segment_len = sizeof(segment);
	if (get_next_token(&token, ":", segment, &segment_len) == NO_TOKEN)
		return(DATE_FMT_ERR);

	if (!is_number(segment, segment_len)) return(DATE_FMT_ERR);
	td->minute = atoi(segment);
	if (td->minute > 59) return(DATE_FMT_ERR);
/*
 *	If there's anything left in the token, it indicates the seconds
 */
	segment_len = sizeof(segment);
	if (get_next_token(&token, ":", segment, &segment_len) == NO_TOKEN)
		return(TOKEN_OK);

	if (!is_number(segment, segment_len)) return(DATE_FMT_ERR);
	td->second = atoi(segment);
	if (td->second > 59) return(DATE_FMT_ERR);

	return(TOKEN_OK);
}

/*
 *	check_token() examines the value of token, determines what type of
 *	data the token represents and updates the time_data_t structure as
 *	needed. It returns a value of TOKEN_OK if the token was recognized and
 *	properly handled. Any other return value is an error.
 */
static int check_token(char *token, struct time_data_t *td, struct tm *current)
{
	int	val = 0;

	if (token == NULL) return(-1);
/*
 *	If the token represents a timezone or has a "-timezone" suffix
 *	appended to it, deal with it. Any timezone will be stripped from
 *	the token by this function.
 */
	val = get_timezone(token);
	if (val >= 0) {
		if (td->timezone >= 0) return(DATE_FMT_ERR);
		td->timezone = val;
/*
 *	Stripping off the timezone may have left nothing behind
 */
		if (*token == 0) return(TOKEN_OK);
	}
/*
 *	Check if the token is (or contains) the meridian. If it does, the
 *	contents of token will have the meridan value stripped out leaving
 *	the rest of the token untouched.
 */
	val = get_meridian(token);
	if (val >= 0) {
		if (td->meridian >= 0) return(DATE_FMT_ERR);
		td->meridian = val;
/*
 *	Stripping off the meridian may have left nothing behind
 */
		if (*token == 0) return(TOKEN_OK);
	}
/*
 *	Check for a relative date specification such as "yesterday",
 *	"today", or "tomorrow". If found, the proper year/month/day
 *	will be set within the function.
 */
	val = get_day_offset(token, td, current);
	if (val < 0) return(DATE_FMT_ERR);
	else if (val > 0) return(TOKEN_OK);
/*
 *	If the token represents the name of a day of the week,
 *	deal with it.
 */
	val = get_weekday(token);
	if (val >= 0) {
		if (td->weekday >= 0) return(DATE_FMT_ERR);
		td->weekday = val;
		return(TOKEN_OK);
	}
/*
 *	If the token represents the name of a month,
 *	deal with it.
 */
	val = get_month(token);
	if (val >= 0) {
		if (td->month >= 0) return(DATE_FMT_ERR);
		td->month = val;
		return(TOKEN_OK);
	}
/*
 *	If token is 'now', set date and time values in the time_data_t
 *	structure appropriately. NOTE: 'now' by default will yield the
 *	current time and date. However, if the caller provides a year/
 *	month or day, 'now' will be assumed to mean the current time, but
 *	on the given date.
 */
	if (streq(token, "now")) {
		if ((td->hour >= 0) ||
		    (td->minute >= 0) ||
		    (td->second >= 0))
			return(DATE_FMT_ERR);
		td->hour = current->tm_hour;
		td->minute = current->tm_min;
		td->second = current->tm_sec;
		return(TOKEN_OK);
	}
/*
 *	If the token is 'noon' or 'midnight' set the
 *	hour/minute/second values appropriately.
 */
	if ((streq(token, "noon")) || (streq(token, "midnight"))) {
		if (td->hour >= 0) return(DATE_FMT_ERR);
		td->second = 0;
		td->minute = 0;
		if (*token == 'm') td->hour = 24;
		else td->hour = 12;
		return(TOKEN_OK);
	}
/*
 *	If the token contains a '-' or '/', it is specifying some combination
 *	of year, month, and date
 */
	if (strcspn(token, "-/") != strlen(token)) {
		return(parse_date(token, td));
	}
/*
 *	If the token contains a ':' it is specifying some combination of
 *	hour, minute, and second.
 */
	if (strchr(token, ':') != NULL) return(parse_time(token, td));
/*
 *	If we still haven't figured out what the token represents, the
 *	only possibilities left are year, day or hour, and year in this case
 *	must be either a 4 digit value or a value > 31
 */
	if (!is_number(token, strlen(token))) return(DATE_FMT_ERR);

	val = atoi(token);
	if ((strlen(token) == 4) || (val > 31)) {
		if (td->year >= 0) {
/*
 *	One quick kludge here... sometimes a token can be ambiguous when
 *	first scanned and lead to an incorrect assumption as to whether
 *	it was a day or year, but we can correct one of these situations
 *	here. (ie. if we thought we found a 2-digit year before, but
 *	now find a 4-digit value, the new one is the year, and we
 *	correct our previous assumtion and make the 2-digit value the
 *	day.)
 */
			if ((td->day < 0) && (td->year < 32)) {
				td->day = td->year;
				if (!set_year(val, td)) return(DATE_FMT_ERR);
				return(TOKEN_OK);
			}
			return(DATE_FMT_ERR);
		}
		if (!set_year(val, td)) return(DATE_FMT_ERR);
		return(TOKEN_OK);
	}

	if (val < 0) return(DATE_FMT_ERR);
/*
 *	Okay, the only options left are day and hour. If it can't be
 *	the hour, but the day has already been specified, there's a
 *	problem.
 */
	if (val > 24) {
		if (td->day >= 0) return(DATE_FMT_ERR);
		td->day = val;
		return(TOKEN_OK);
	}
/*
 *	If the month has been specified but the day has not, assume
 *	the token represents the day of the month.
 */
	if ((td->month >= 0) && (td->day < 0)) {
		td->day = val;
		return(TOKEN_OK);
	}
/*
 *	At this point assume that if the hour has not yet been
 *	specified the token represents the hour. Otherwise assume
 *	it represents the day of the month.
 */
	if (td->hour < 0) {
		td->hour = val;
		return(TOKEN_OK);
	}

	if (td->day >= 0) return(DATE_FMT_ERR);
	td->day = val;

	return(TOKEN_OK);
}

/*
 *	date2time() generates an integer timestamp for the ascii formatted
 *	date provided in asci_date. On success, the non-negative timestamp
 *	is returned. On error, a value of -1 is returned.
 */
static time_t date2time(char *asci_date)
{
	struct time_data_t	td;
	struct tm		current;
	size_t			len = 0;
	size_t			orig_len = 0;
	size_t			token_len;
	time_t			int_time;
	time_t			local_gmt_offset = 0;
	time_t			Now;
	char			*cptr = NULL;
	char			token[32];
	char			tmp_date[128];
	int			rval = 0;

/*
 *	-1 in any field of the time_data_t structure indicates the
 *	corresponding value has not been provided.
 */
	td.hour = -1;
	td.minute = -1;
	td.second = -1;
	td.year = -1;
	td.month = -1;
	td.day = -1;
	td.weekday = -1;
	td.timezone = -1;
	td.meridian = -1;

	time(&Now);
	localtime_r(&Now, &current);
/*
 *	To make life easier, we'll convert upper-case to lower, and drop
 *	some punctuation and such, so we need to make a private copy
 *	of the input string before we do anything else.
 */
	orig_len = strlen(asci_date);
	if (orig_len >= sizeof(tmp_date)) return(-1);
	memcpy(tmp_date, asci_date, orig_len + 1);
/*
 *	Convert all characters in the incoming string to lower-case,
 *	wipe out things like commas, periods, tabs and new-lines.
 */
	len = strlen(tmp_date);
	for (cptr = tmp_date; *cptr != '\0'; cptr++) {
		*cptr = tolower(*cptr);
		if (*cptr == ',') *cptr = ' ';
		if (*cptr == '.') *cptr = ' ';
		if (*cptr == '\n') *cptr = ' ';
		if (*cptr == '\t') *cptr = ' ';
	}
/*
 *	Loop through all the space delimited tokens in the
 *	input string. a call to check_token() will determine
 *	what type of information is contained in the token
 *	and set the appropriate values.
 */
	cptr = tmp_date;
	while (*cptr != '\0') {
		token_len = sizeof(token);
		if (get_next_token(&cptr, " ", token, &token_len) == NO_TOKEN)
			break;
		rval = check_token(token, &td, &current);
		if (rval == NO_TOKEN) break;
		else if (rval == TOKEN_OK) continue;
		else return(rval);
	}
/*
 *	Need to do some sanity checks on values before we try the
 *	date conversion
 */
	if (td.weekday >= 0) {
		if ((td.year < 0) && (td.month < 0) && (td.day < 0 )) {
			td.year = current.tm_year;
			td.month = current.tm_mon;
			td.day = current.tm_mday;
			if (td.weekday < current.tm_wday)
				td.day += current.tm_wday +
					  (7 - (current.tm_wday - td.weekday));
			else td.day += td.weekday - current.tm_wday;
		}
	}
/*
 *	If day has not been specified, use the current day if no month has
 *	been specified, or the first day of the month if a month was given.
 */
	if (td.day < 0) {
		if ((td.month < 0) && (td.year >= 0)) td.day = 1;
		else if (td.month < 0) td.day = current.tm_mday;
		else td.day = 1;
	}
/*
 *	If month has not been specified, use the current month if no year has
 *	been specified, or the first month of the year if a year was given.
 */
	if (td.month < 0) {
		if (td.year < 0) td.month = current.tm_mon;
		else td.month = 0;
	}

	if (td.year < 0) td.year = current.tm_year;

	if (td.hour < 0) td.hour = 0;
	if (td.minute < 0) td.minute = 0;
	if (td.second < 0) td.second = 0;

/*
 *	If February 29 is specified for a non-leap year, reset it to February
 *	28. Yep, it's arbitrary.
 */
	if ((td.month == 1) && (td.day >= 29) &&
	    (((td.year % 4) != 0) ||
	      (((td.year % 100) == 0) && ((td.year % 400) != 0))))
		td.day = 28;

	if ((td.hour == 24) && (td.minute || td.second)) return(DATE_FMT_ERR);
	switch (td.meridian) {
	case NO_MERIDIAN:
		break;
	case MERIDIAN_AM:
		if (td.hour > 12) return(DATE_FMT_ERR);
		if (td.hour == 12) td.hour = 0;
		break;
	case MERIDIAN_PM:
		if (td.hour > 12) return(DATE_FMT_ERR);
		if (td.hour < 12) td.hour += 12;
		break;
	case MERIDIAN_M:
		if (td.hour != 12) return(DATE_FMT_ERR);
		break;
	}

/*
 *	Set appropriate values in the tm structure for mktime()
 */
	current.tm_sec = td.second;
	current.tm_min = td.minute;
	current.tm_hour = td.hour;
	current.tm_mday = td.day;
	current.tm_mon = td.month;
	current.tm_year = td.year;
	current.tm_wday = -1;
	current.tm_yday = -1;
	current.tm_isdst = -1; /* how should this be set????? */

	tzset();
	int_time = mktime(&current);
	if (int_time < 0) return(DATE_FMT_ERR);
/*
 *	Adjust the time for time zones other than the current local zone
 */
	if (td.timezone < 0) return(int_time);

	if (current.tm_isdst) local_gmt_offset = timezone - 3600;
	else local_gmt_offset = timezone;
/*
 *	local_gmt_offset is in seconds but values from times_zones array
 *	are in minutes, so we have to adjust them to seconds...
 */
	int_time = int_time - (local_gmt_offset -
		(time_zones[td.timezone].offset_from_gmt) * 60);

	return(int_time);
}

/*
 *	datetoi() parses the specified date and returns an integer
 *	corresponding to the seconds since 1/1/70 00:00:00. It returns
 *	0 on error.
 *
 *	NOTE: Since Epoch is not useful to LCRM and since time_t values
 *	are unsigned on some machines, 0 is taken to be an error value.
 */
time_t datetoi(char *dateandtimestr, int *lrmstatp)
{
	time_t res_time = 0;

	*lrmstatp = 0;

	if ((res_time = date2time(dateandtimestr)) < 1) {
		*lrmstatp = LRM_EINVAL;
		return(0);
	}

	return(res_time);
}
