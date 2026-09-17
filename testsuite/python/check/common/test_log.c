/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test the logging behavior selected by LogTimeFormat (src/common/log.c).
 *
 * Every case drives the real logging path: the log is pointed at a temporary
 * file, a message is emitted through info(), and the line it wrote is read
 * back and compared.
 *
 * Version gating lives in the skip_tests entry in
 * testsuite/python/tests/test_100_1.py, which skips this whole file below the
 * release the options were added in.
 *****************************************************************************/

#include <check.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/threadpool.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define CAPTURE_PATH "test_log.XXXXXX"
#define CONF_PATH "test_log.conf.XXXXXX"

/*
 * Mirrors THREAD_NAME_WIDTH in src/common/log.c. The name is padded out to this
 * column, and that padding is part of what the prefix promises, so assert it
 * rather than skip over it.
 */
#define THREAD_NAME_COLUMN 12

/*
 * Slurm names its pool threads "worker[N]". The bracket is the point: it is
 * exactly what a scan for the end of the prefix must not trip over.
 */
#define TEST_THREAD_NAME "worker[9]"

/* a second, differently named thread, to tell thread identity from process */
#define OTHER_THREAD_NAME "worker[10]"

/*
 * The shape of each date a LogTimeFormat renders, and the strptime() format
 * that reads the same date back. "d" stands for a digit and "a" for a letter.
 */
#define ISO8601_SHAPE "dddd-dd-ddTdd:dd:dd"
#define ISO8601_STRPTIME "%Y-%m-%dT%H:%M:%S"
#define ISO8601_TZ_SHAPE ISO8601_SHAPE "zdd:dd"
#define SHORT_SHAPE "aaa dd dd:dd:dd"
#define SHORT_STRPTIME "%b %d %H:%M:%S"

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	log_init("log-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

/*
 * Point the log at a new temporary file and apply a LogTimeFormat
 * IN timefmt - timestamp format to log with
 * IN flags - options accompanying that format
 * IN/OUT path - CAPTURE_PATH buffer, populated with the file created
 *
 * Note: libcheck runs every test case in a forked child, so neither the
 * redirect nor the LogTimeFormat change can reach any of the other cases.
 */
static void log_capture_begin(log_fmt_t timefmt, log_flags_t flags, char *path)
{
	log_options_t opts = LOG_OPTS_INITIALIZER;
	int fd = mkstemp(path);

	ck_assert_msg(fd >= 0, "mkstemp() failed: %s", strerror(errno));
	close(fd);

	opts.stderr_level = LOG_LEVEL_QUIET;
	opts.syslog_level = LOG_LEVEL_QUIET;
	opts.logfile_level = LOG_LEVEL_INFO;

	/*
	 * Name the thread the way a real daemon does, so the name log.c reads
	 * back with PR_GET_NAME is both known and bracket bearing.
	 */
	ck_assert_int_eq(prctl(PR_SET_NAME, TEST_THREAD_NAME, NULL, NULL, NULL),
			 0);

	ck_assert_int_eq(log_alter(opts, 0, path), SLURM_SUCCESS);
	log_set_timefmt(timefmt, flags);
}

/* RET everything logged since log_capture_begin(), removing the file */
static char *log_capture_end(const char *path)
{
	char buf[1024] = { 0 };
	ssize_t bytes;
	int fd = open(path, O_RDONLY);

	ck_assert_msg(fd >= 0, "open() failed: %s", strerror(errno));
	bytes = read(fd, buf, (sizeof(buf) - 1));
	ck_assert_msg(bytes >= 0, "read() failed: %s", strerror(errno));
	close(fd);
	unlink(path);

	/*
	 * Every case logs exactly once. A second line means something else
	 * reached the capture, and every check made on it is then reading a
	 * line nobody meant to test.
	 */
	ck_assert_msg((xstrchr(buf, '\n') == xstrrchr(buf, '\n')),
		      "expected one line, got \"%s\"", buf);

	return xstrdup(buf);
}

/*
 * RET the "[...]" prefix of line, without the brackets, or NULL if it has none
 *
 * Note: the caller must xfree() the result. Bounding every check to the prefix
 * keeps the message itself from being mistaken for part of a timestamp.
 *
 * Note: scan for the closing bracket from the end. A thread name may hold one
 * of its own -- Slurm names its pool threads "worker[N]" -- so the first "]" is
 * not necessarily the one that closes the prefix. Every message logged here is
 * bracket free, which is what makes the last one the right one.
 */
static char *log_prefix(const char *line)
{
	const char *end;

	if (!line || (line[0] != '['))
		return NULL;

	if (!(end = xstrrchr(line, ']')))
		return NULL;

	return xstrndup((line + 1), (end - line - 1));
}

/*
 * RET width of the fractional seconds in prefix, or -1 if it has none
 *
 * Note: the deprecated thread_id format left justifies its fraction in a fixed
 * width field, so the padding counts toward the width. Every other format
 * zero pads, where the width is just the digit count.
 *
 * Note: pass the timestamp half, never the whole prefix. The separator and the
 * padded process id that _set_thread_id() writes are spaces too, so an
 * unbounded prefix makes the pad loop swallow them and over report.
 */
static int fractional_width(const char *prefix)
{
	const char *dot = xstrchr(prefix, '.');
	int digits = 0, pad = 0;

	if (!dot)
		return -1;

	while (isdigit((unsigned char) dot[1 + digits]))
		digits++;

	while (dot[1 + digits + pad] == ' ')
		pad++;

	return (digits + pad);
}

/* RET true if prefix ends in an RFC 5424 style "(+/-)hh:mm" offset */
static bool has_tz_offset(const char *prefix)
{
	size_t len = strlen(prefix);
	const char *tz;

	if (len < 6)
		return false;

	tz = (prefix + len - 6);

	return (((tz[0] == '+') || (tz[0] == '-')) &&
		isdigit((unsigned char) tz[1]) &&
		isdigit((unsigned char) tz[2]) && (tz[3] == ':') &&
		isdigit((unsigned char) tz[4]) &&
		isdigit((unsigned char) tz[5]));
}

/*
 * RET the process and thread tail exactly as _set_thread_id() renders it
 *
 * Note: the caller must xfree() the result. The name is read back with
 * PR_GET_NAME rather than assumed, so the tail stays exact wherever the kernel
 * truncated the name log.c reads with that same call.
 */
static char *thread_id_tail(void)
{
	char name[PRCTL_BUF_BYTES] = { 0 };

	if (prctl(PR_GET_NAME, name, NULL, NULL, NULL) < 0)
		name[0] = '\0';

	return xstrdup_printf("%5d %-*s %p", (int) getpid(),
			      THREAD_NAME_COLUMN, name, (void *) pthread_self());
}

/*
 * RET the timestamp half of prefix, which is everything the thread id tail does
 * not occupy
 * IN prefix - the whole bracketed prefix
 * IN tail - what thread_id_tail() rendered, or NULL when nothing asked for one
 *
 * Note: the caller must xfree() the result. Every timestamp check has to be
 * bounded this way. _set_thread_id() appends to the same buffer, so a check
 * that reads the end of the prefix reads the thread id's hex tail rather than
 * the timestamp it meant to look at, and holds whether or not the timestamp
 * rendered at all.
 */
static char *timestamp_half(const char *prefix, const char *tail)
{
	const char *at;

	if (!tail || !(at = xstrstr(prefix, tail)))
		return xstrdup(prefix);

	/* step back over the separator _set_thread_id() writes after a stamp */
	if ((at > prefix) && (at[-1] == ' '))
		at--;

	return xstrndup(prefix, (at - prefix));
}

/*
 * RET true if s begins with the shape of pattern, where "d" stands for a digit,
 * "a" for a letter and every other character stands for itself
 */
static bool matches_shape(const char *s, const char *pattern)
{
	int i;

	for (i = 0; pattern[i]; i++) {
		if (!s[i])
			return false;

		if (pattern[i] == 'd') {
			if (!isdigit((unsigned char) s[i]))
				return false;
		} else if (pattern[i] == 'a') {
			if (!isalpha((unsigned char) s[i]))
				return false;
		} else if (pattern[i] == 'z') {
			if ((s[i] != '+') && (s[i] != '-'))
				return false;
		} else if (s[i] != pattern[i])
			return false;
	}

	return true;
}

/* RET true if s is one or more digits and nothing else */
static bool digits_only(const char *s)
{
	if (!s[0])
		return false;

	for (int i = 0; s[i]; i++)
		if (!isdigit((unsigned char) s[i]))
			return false;

	return true;
}

/*
 * RET this process's own UTC offset for when, as "(+/-)hh:mm"
 *
 * Note: the caller must xfree() the result.
 */
static char *local_tz_offset(time_t when)
{
	struct tm tm = { 0 };
	long minutes;

	localtime_r(&when, &tm);
	minutes = labs(tm.tm_gmtoff) / 60;

	return xstrdup_printf("%c%02ld:%02ld",
			      ((tm.tm_gmtoff < 0) ? '-' : '+'), (minutes / 60),
			      (minutes % 60));
}

/*
 * Assert the timestamp in half names the moment the line was logged
 * IN name - the LogTimeFormat value under test, for the failure message
 * IN half - the timestamp half of the prefix
 * IN date_fmt - strptime() format matching what that value renders
 * IN before - time() taken before the message was logged
 * IN after - time() taken after the line was read back
 *
 * Note: shape alone cannot see a timestamp that is well formed and wrong, so
 * read the value back too. Swapping localtime_r() for gmtime_r() keeps every
 * shape check green while moving every log line by the UTC offset.
 */
static void assert_timestamp_value(const char *name, const char *half,
				   const char *date_fmt, time_t before,
				   time_t after)
{
	struct tm tm = { 0 };
	time_t got;

	/* seed the fields a short date never renders, the year above all */
	localtime_r(&before, &tm);
	tm.tm_isdst = -1;

	ck_assert_msg(strptime(half, date_fmt, &tm),
		      "%s: \"%s\" is not a \"%s\" date", name, half, date_fmt);

	got = mktime(&tm);

	ck_assert_msg(((got >= (before - 1)) && (got <= (after + 1))),
		      "%s: \"%s\" reads %ld, outside [%ld, %ld]", name, half,
		      (long) got, (long) (before - 1), (long) (after + 1));
}

/* ------------------------------------------------ timestamp formats */

/*
 * Every format the man page lists, with the shape of the date it renders, the
 * strptime() format that reads that date back, the width of its fractional
 * second and whether it carries a timezone offset.
 *
 * Note: rfc5424 and rfc3339 share a row shape deliberately. log.c renders them
 * byte for byte identically, so no oracle can separate them; that is a property
 * of the formats, not a gap in the table.
 */
static const struct {
	log_fmt_t fmt;
	const char *name;
	const char *shape;	/* NULL when the format renders no date */
	const char *date_fmt;	/* NULL when it is not a wall clock */
	int width;
	bool tz;
} formats[] = {
	{ LOG_FMT_ISO8601_MS, "iso8601_ms", ISO8601_SHAPE, ISO8601_STRPTIME, 3,
	  false },
	{ LOG_FMT_ISO8601, "iso8601", ISO8601_SHAPE, ISO8601_STRPTIME, -1,
	  false },
	{ LOG_FMT_RFC5424_MS, "rfc5424_ms", ISO8601_SHAPE, ISO8601_STRPTIME, 3,
	  true },
	{ LOG_FMT_RFC5424_US, "rfc5424_us", ISO8601_SHAPE, ISO8601_STRPTIME, 6,
	  true },
	{ LOG_FMT_RFC5424, "rfc5424", ISO8601_SHAPE, ISO8601_STRPTIME, -1,
	  true },
	{ LOG_FMT_RFC3339, "rfc3339", ISO8601_SHAPE, ISO8601_STRPTIME, -1,
	  true },
	{ LOG_FMT_CLOCK, "clock", NULL, NULL, -1, false },
	{ LOG_FMT_SHORT, "short", SHORT_SHAPE, SHORT_STRPTIME, -1, false },
};

/*
 * Every format, against what the man page promises it prints, both on its own
 * and with the thread_id option beside it. The renderer is shared, so a
 * regression in one arm is a regression in all of them.
 *
 * Note: this is a loop test, so each format is reported as its own case. A
 * second regression is then not hidden behind the first, and one known broken
 * format can be xfailed without xfailing the other seven.
 *
 * Note: the option pass is what covers "<format>,thread_id" for every format
 * rather than the two the file used to spot check, clock among them, whose
 * arm reaches the thread id through a goto of its own.
 */
START_TEST(test_log_formats)
{
	for (int f = 0; f < 2; f++) {
		log_flags_t flags = (f ? LOG_FLAG_THREAD_ID : LOG_FLAGS_NONE);
		char path[] = CAPTURE_PATH;
		char *out = NULL, *prefix = NULL, *tail = NULL, *half = NULL;
		time_t before, after;

		before = time(NULL);
		log_capture_begin(formats[_i].fmt, flags, path);
		info("hello");
		out = log_capture_end(path);
		after = time(NULL);

		prefix = log_prefix(out);

		/*
		 * An empty prefix is not a missing one: xstrndup(s, 0) hands
		 * back "", which is non NULL and scores like a format that
		 * renders no fraction and no offset.
		 */
		ck_assert_msg((prefix && prefix[0]),
			      "%s: expected a prefix, got \"%s\"",
			      formats[_i].name, out);

		tail = thread_id_tail();

		/*
		 * Both directions matter. Asserting only that the tail appears
		 * when asked for leaves the guard that withholds it untested,
		 * so a build that printed the process and thread unconditionally
		 * would read as correct.
		 */
		if (flags)
			ck_assert_msg(xstrstr(prefix, tail),
				      "%s: expected the tail \"%s\" in \"%s\"",
				      formats[_i].name, tail, prefix);
		else
			ck_assert_msg(!xstrstr(prefix, tail),
				      "%s: unasked for tail \"%s\" in \"%s\"",
				      formats[_i].name, tail, prefix);

		/* everything below is about the timestamp, so bound it */
		half = timestamp_half(prefix, (flags ? tail : NULL));

		/*
		 * Pin the shape of the date. Without it iso8601, clock and
		 * short are indistinguishable, since the fraction width and
		 * the offset are all any of them scores on.
		 */
		if (formats[_i].shape)
			ck_assert_msg(matches_shape(half, formats[_i].shape),
				      "%s: \"%s\" is not shaped \"%s\"",
				      formats[_i].name, half,
				      formats[_i].shape);
		else
			ck_assert_msg(digits_only(half),
				      "%s: expected only digits in \"%s\"",
				      formats[_i].name, half);

		if (formats[_i].date_fmt)
			assert_timestamp_value(formats[_i].name, half,
					       formats[_i].date_fmt, before,
					       after);

		ck_assert_msg((fractional_width(half) == formats[_i].width),
			      "%s: expected a %d wide fraction in \"%s\"",
			      formats[_i].name, formats[_i].width, half);
		ck_assert_msg((has_tz_offset(half) == formats[_i].tz),
			      "%s: expected timezone offset %d in \"%s\"",
			      formats[_i].name, (int) formats[_i].tz, half);

		if (formats[_i].tz) {
			char *want = local_tz_offset(before);

			/* the offset has to be this process's own, not any */
			ck_assert_str_eq((half + strlen(half) - 6), want);
			xfree(want);
		}

		ck_assert_msg(xstrstr(out, "hello"),
			      "%s: message lost from \"%s\"",
			      formats[_i].name, out);

		xfree(half);
		xfree(tail);
		xfree(prefix);
		xfree(out);
	}
}

END_TEST

/* ------------------------------------------------ scheduler log */

#define SCHED_PREFIX "sched: "

/*
 * The scheduler log decides whether to write a "[%M]" prefix through the same
 * _have_timefmt() gate the main log uses, on a log_t of its own. Only one of
 * that gate's three call sites is reachable through info(), so drive this one
 * directly.
 *
 * Note: the third row is the reason the gate exists. omit alone suppresses the
 * whole prefix, but omit with the thread id option still has something to
 * print, so the gate has to say yes while the timestamp itself says nothing.
 */
static const struct {
	log_fmt_t fmt;
	log_flags_t flags;
	const char *name;
	bool prefixed;
} sched_cases[] = {
	{ LOG_FMT_RFC3339, LOG_FLAGS_NONE, "rfc3339", true },
	{ LOG_FMT_OMIT, LOG_FLAGS_NONE, "omit", false },
	{ LOG_FMT_OMIT, LOG_FLAG_THREAD_ID, "omit,thread_id", true },
};

START_TEST(test_log_sched)
{
	char path[] = CAPTURE_PATH, sched_path[] = CAPTURE_PATH;
	char *out = NULL, *prefix = NULL, *drop = NULL;
	log_options_t opts = LOG_OPTS_INITIALIZER;
	int fd = mkstemp(sched_path);

	ck_assert_msg(fd >= 0, "mkstemp() failed: %s", strerror(errno));
	close(fd);

	opts.stderr_level = LOG_LEVEL_QUIET;
	opts.syslog_level = LOG_LEVEL_QUIET;
	opts.logfile_level = LOG_LEVEL_INFO;

	/* quiets the main log and sets the format both logs then share */
	log_capture_begin(sched_cases[_i].fmt, sched_cases[_i].flags, path);

	ck_assert_int_eq(sched_log_init("log-test", opts, 0, sched_path),
			 SLURM_SUCCESS);

	sched_info("hello");

	out = log_capture_end(sched_path);
	drop = log_capture_end(path);
	xfree(drop);

	ck_assert_msg(!xstrncmp(out, SCHED_PREFIX, strlen(SCHED_PREFIX)),
		      "%s: expected \"%s\" first in \"%s\"",
		      sched_cases[_i].name, SCHED_PREFIX, out);

	prefix = log_prefix(out + strlen(SCHED_PREFIX));

	if (sched_cases[_i].prefixed)
		ck_assert_msg((prefix && prefix[0]),
			      "%s: expected a prefix in \"%s\"",
			      sched_cases[_i].name, out);
	else
		ck_assert_msg(!prefix, "%s: unasked for prefix in \"%s\"",
			      sched_cases[_i].name, out);

	ck_assert_msg(xstrstr(out, "hello"), "%s: message lost from \"%s\"",
		      sched_cases[_i].name, out);

	xfree(prefix);
	xfree(out);
}

END_TEST

/* ------------------------------------------ log_timestamp() renderer */

/*
 * log_timestamp() is a second renderer over the same log->fmt, used where the
 * line is assembled rather than substituted through "%M": the scheduler log and
 * the JSON record. This MR edits it, and nothing else in this file reaches it.
 *
 * Note: unlike _set_timestamp() it keeps a default: arm, so the four formats it
 * was never taught render as plain iso8601 rather than failing to compile. That
 * divergence is deliberate but easy to regress into, so pin it: omit does not
 * omit here, and clock is not a clock here.
 */
static const struct {
	log_fmt_t fmt;
	const char *name;
	const char *shape;
} timestamps[] = {
	{ LOG_FMT_ISO8601_MS, "iso8601_ms", ISO8601_SHAPE },
	{ LOG_FMT_ISO8601, "iso8601", ISO8601_SHAPE },
	{ LOG_FMT_RFC5424_MS, "rfc5424_ms", ISO8601_TZ_SHAPE },
	{ LOG_FMT_RFC5424_US, "rfc5424_us", ISO8601_TZ_SHAPE },
	{ LOG_FMT_RFC5424, "rfc5424", ISO8601_TZ_SHAPE },
	{ LOG_FMT_RFC3339, "rfc3339", ISO8601_TZ_SHAPE },
	{ LOG_FMT_SHORT, "short", SHORT_SHAPE },
	/* the default: arm, reached by every format below this line */
	{ LOG_FMT_CLOCK, "clock", ISO8601_SHAPE },
	{ LOG_FMT_OMIT, "omit", ISO8601_SHAPE },
	{ LOG_FMT_THREAD_ID, "thread_id", ISO8601_SHAPE },
};

START_TEST(test_log_timestamp)
{
	char buf[64] = { 0 };
	size_t used;

	log_set_timefmt(timestamps[_i].fmt, LOG_FLAGS_NONE);

	used = log_timestamp(buf, sizeof(buf));

	ck_assert_msg(used, "%s: log_timestamp() wrote nothing",
		      timestamps[_i].name);
	ck_assert_msg((used == strlen(buf)),
		      "%s: log_timestamp() returned %zu for \"%s\"",
		      timestamps[_i].name, used, buf);
	ck_assert_msg(matches_shape(buf, timestamps[_i].shape),
		      "%s: \"%s\" is not shaped \"%s\"", timestamps[_i].name,
		      buf, timestamps[_i].shape);
	ck_assert_msg((strlen(buf) == strlen(timestamps[_i].shape)),
		      "%s: \"%s\" is longer than \"%s\"", timestamps[_i].name,
		      buf, timestamps[_i].shape);

	/* the thread id option has no bearing on this renderer */
	log_set_timefmt(timestamps[_i].fmt, LOG_FLAG_THREAD_ID);
	ck_assert_int_eq(log_timestamp(buf, sizeof(buf)), (int) used);
}

END_TEST

/* --------------------------------------------- LogTimeFormat parsing */

/*
 * Write a slurm.conf carrying value as its LogTimeFormat
 * IN value - the LogTimeFormat to configure, or NULL to leave the line out
 * IN/OUT path - CONF_PATH buffer, populated with the file created
 *
 * Note: the three settings beside it are the least slurm_conf_init() accepts.
 * SlurmUser has to name a user that resolves here, so use the one running.
 */
static void write_conf(const char *value, char *path)
{
	const struct passwd *pw = getpwuid(getuid());
	int fd = mkstemp(path);
	FILE *fp = NULL;

	ck_assert_msg(fd >= 0, "mkstemp() failed: %s", strerror(errno));
	ck_assert_msg(pw, "getpwuid() failed: %s", strerror(errno));

	fp = fdopen(fd, "w");
	ck_assert_msg(fp, "fdopen() failed: %s", strerror(errno));

	fprintf(fp, "ClusterName=test\n");
	fprintf(fp, "SlurmctldHost=localhost\n");
	fprintf(fp, "SlurmUser=%s\n", pw->pw_name);

	if (value)
		fprintf(fp, "LogTimeFormat=%s\n", value);

	ck_assert_int_eq(fclose(fp), 0);
}

/*
 * Every LogTimeFormat string the man page accepts, against the pair it must
 * resolve to. This is the half of the contract an administrator actually
 * writes; everything else in this file enters below it, through
 * log_set_timefmt(), on a pair some parser already produced.
 */
static const struct {
	const char *value;
	log_fmt_t fmt;
	log_flags_t flags;
} conf_values[] = {
	/* every timestamp format, on its own */
	{ "iso8601_ms", LOG_FMT_ISO8601_MS, LOG_FLAGS_NONE },
	{ "iso8601", LOG_FMT_ISO8601, LOG_FLAGS_NONE },
	{ "rfc5424_ms", LOG_FMT_RFC5424_MS, LOG_FLAGS_NONE },
	{ "rfc5424_us", LOG_FMT_RFC5424_US, LOG_FLAGS_NONE },
	{ "rfc5424", LOG_FMT_RFC5424, LOG_FLAGS_NONE },
	{ "rfc3339", LOG_FMT_RFC3339, LOG_FLAGS_NONE },
	{ "clock", LOG_FMT_CLOCK, LOG_FLAGS_NONE },
	{ "short", LOG_FMT_SHORT, LOG_FLAGS_NONE },
	{ "omit", LOG_FMT_OMIT, LOG_FLAGS_NONE },
	/*
	 * thread_id on its own is still the deprecated format and not the
	 * option, which is what keeps an existing configuration printing what
	 * it always has.
	 */
	{ "thread_id", LOG_FMT_THREAD_ID, LOG_FLAGS_NONE },
	/* the capability that could not be expressed before */
	{ "rfc3339,thread_id", LOG_FMT_RFC3339, LOG_FLAG_THREAD_ID },
	{ "rfc5424_us,thread_id", LOG_FMT_RFC5424_US, LOG_FLAG_THREAD_ID },
	{ "clock,thread_id", LOG_FMT_CLOCK, LOG_FLAG_THREAD_ID },
	/*
	 * The one resolution this MR changes: omit,thread_id used to name the
	 * deprecated format outright, and now drops the timestamp and keeps
	 * the option beside it.
	 */
	{ "omit,thread_id", LOG_FMT_OMIT, LOG_FLAG_THREAD_ID },
	/* the scan is case insensitive, and the order of the two is free */
	{ "RFC3339,THREAD_ID", LOG_FMT_RFC3339, LOG_FLAG_THREAD_ID },
	{ "thread_id,rfc3339", LOG_FMT_RFC3339, LOG_FLAG_THREAD_ID },
};

START_TEST(test_log_conf_values)
{
	char path[] = CONF_PATH;

	write_conf(conf_values[_i].value, path);
	ck_assert_int_eq(slurm_conf_init(path), SLURM_SUCCESS);

	ck_assert_msg((slurm_conf.log_fmt == conf_values[_i].fmt),
		      "\"%s\": expected format %d, got %d",
		      conf_values[_i].value, (int) conf_values[_i].fmt,
		      (int) slurm_conf.log_fmt);
	ck_assert_msg((slurm_conf.log_flags == conf_values[_i].flags),
		      "\"%s\": expected flags %d, got %d",
		      conf_values[_i].value, (int) conf_values[_i].flags,
		      (int) slurm_conf.log_flags);

	slurm_conf_destroy();
	unlink(path);
}

END_TEST

/* Leaving the option out gets what the man page documents as the default */
START_TEST(test_log_conf_default)
{
	char path[] = CONF_PATH;

	write_conf(NULL, path);
	ck_assert_int_eq(slurm_conf_init(path), SLURM_SUCCESS);

	ck_assert_int_eq(slurm_conf.log_fmt, LOG_FMT_ISO8601_MS);
	ck_assert_int_eq(slurm_conf.log_flags, LOG_FLAGS_NONE);

	slurm_conf_destroy();
	unlink(path);
}

END_TEST

/*
 * Taking thread_id back out has to clear the flag on reconfigure. The parser
 * only ever sets bits, so the reset the daemons rely on is the one that gets
 * missed.
 */
START_TEST(test_log_conf_reinit_clears_flag)
{
	char path[] = CONF_PATH;

	write_conf("rfc3339,thread_id", path);
	ck_assert_int_eq(slurm_conf_init(path), SLURM_SUCCESS);
	ck_assert_int_eq(slurm_conf.log_flags, LOG_FLAG_THREAD_ID);

	unlink(path);
	strcpy(path, CONF_PATH);
	write_conf("rfc3339", path);
	ck_assert_int_eq(slurm_conf_reinit(path), SLURM_SUCCESS);

	ck_assert_int_eq(slurm_conf.log_fmt, LOG_FMT_RFC3339);
	ck_assert_int_eq(slurm_conf.log_flags, LOG_FLAGS_NONE);

	slurm_conf_destroy();
	unlink(path);
}

END_TEST

/*
 * rfc5424_us has to carry microsecond precision, not merely six columns of it.
 * A build that rendered milliseconds and padded three zeroes would satisfy
 * every width check in this file, so look at the digits a millisecond clock
 * cannot fill.
 */
START_TEST(test_log_rfc5424_us_precision)
{
	bool sub_msec = false;

	for (int i = 0; !sub_msec && (i < 20); i++) {
		char path[] = CAPTURE_PATH;
		char *out = NULL, *prefix = NULL;
		const char *dot = NULL;

		log_capture_begin(LOG_FMT_RFC5424_US, LOG_FLAGS_NONE, path);
		info("hello");
		out = log_capture_end(path);

		prefix = log_prefix(out);
		ck_assert_msg(prefix, "expected a prefix, got \"%s\"", out);
		ck_assert_int_eq(fractional_width(prefix), 6);

		dot = xstrchr(prefix, '.');
		ck_assert_msg(dot, "expected a fraction in \"%s\"", prefix);

		sub_msec = ((dot[4] != '0') || (dot[5] != '0') ||
			    (dot[6] != '0'));

		xfree(prefix);
		xfree(out);
	}

	ck_assert_msg(sub_msec,
		      "no sub-millisecond digits in twenty rfc5424_us lines");
}

END_TEST

/* ------------------------------------------------- LogTimeFormat=omit */

/* omit on its own suppresses the whole prefix, as it always has */
START_TEST(test_log_omit_alone)
{
	char path[] = CAPTURE_PATH;
	char *out = NULL;

	log_capture_begin(LOG_FMT_OMIT, LOG_FLAGS_NONE, path);
	info("hello");
	out = log_capture_end(path);

	ck_assert_str_eq(out, "hello\n");
	xfree(out);
}

END_TEST

/*
 * Log one line from a thread of its own
 * IN arg - the CAPTURE_PATH buffer to log into
 *
 * Note: everything else here logs from the initial thread, where the thread
 * name defaults to the executable and the thread id is the process's own. A
 * second thread with a name of its own is what separates "the current thread"
 * from "this process".
 */
static void *log_from_thread(void *arg)
{
	char *tail = NULL;

	ck_assert_int_eq(prctl(PR_SET_NAME, OTHER_THREAD_NAME, NULL, NULL, NULL),
			 0);

	info("hello");

	tail = thread_id_tail();

	return tail;
}

/* The thread half of the prefix names the logging thread, not the process */
START_TEST(test_log_thread_id_is_per_thread)
{
	char path[] = CAPTURE_PATH;
	char *out = NULL, *prefix = NULL, *mine = NULL, *theirs = NULL;
	pthread_t id;

	log_capture_begin(LOG_FMT_OMIT, LOG_FLAG_THREAD_ID, path);

	ck_assert_int_eq(pthread_create(&id, NULL, log_from_thread, NULL), 0);
	ck_assert_int_eq(pthread_join(id, (void **) &theirs), 0);

	out = log_capture_end(path);

	prefix = log_prefix(out);
	ck_assert_msg(prefix, "expected a prefix, got \"%s\"", out);

	/* the line carries the other thread's name and id, not this one's */
	mine = thread_id_tail();
	ck_assert_str_eq(prefix, theirs);
	ck_assert_msg(xstrcmp(prefix, mine),
		      "the logging thread was not named in \"%s\"", prefix);
	ck_assert_msg(xstrstr(prefix, OTHER_THREAD_NAME),
		      "expected \"%s\" in \"%s\"", OTHER_THREAD_NAME, prefix);

	xfree(theirs);
	xfree(mine);
	xfree(prefix);
	xfree(out);
}

END_TEST

/* -------------------------------------------- LogTimeFormat=thread_id */

/* The deprecated format prints the timestamp and the thread id together */
START_TEST(test_log_thread_id_as_format)
{
	char path[] = CAPTURE_PATH;
	char *out = NULL, *prefix = NULL, *tail = NULL, *half = NULL;
	time_t before, after;

	before = time(NULL);
	log_capture_begin(LOG_FMT_THREAD_ID, LOG_FLAGS_NONE, path);
	info("hello");
	out = log_capture_end(path);
	after = time(NULL);

	prefix = log_prefix(out);
	ck_assert_msg(prefix, "expected a prefix, got \"%s\"", out);

	/* the process id, thread name and thread id, exactly as promised */
	tail = thread_id_tail();
	ck_assert_msg(xstrstr(prefix, tail),
		      "expected the tail \"%s\" in \"%s\"", tail, prefix);

	/* a short date and time, and then the process id, as it always was */
	half = timestamp_half(prefix, tail);
	ck_assert_msg(half[0], "expected a timestamp in \"%s\"", prefix);
	ck_assert_msg(matches_shape(half, SHORT_SHAPE),
		      "\"%s\" is not shaped \"%s\"", half, SHORT_SHAPE);
	assert_timestamp_value("thread_id", half, SHORT_STRPTIME, before, after);
	ck_assert_msg(!has_tz_offset(half),
		      "no timezone offset belongs in \"%s\"", half);

	/*
	 * The fraction is left justified in a six character field rather than
	 * zero padded. It is malformed, and it is what this deprecated format
	 * has always printed, so it is the promise being kept.
	 */
	ck_assert_msg((fractional_width(half) == 6),
		      "expected a six wide fraction in \"%s\"", half);
	ck_assert_msg(xstrstr(out, "hello"), "message lost from \"%s\"", out);

	xfree(half);
	xfree(tail);
	xfree(prefix);
	xfree(out);
}

END_TEST

/* The combination that could not be asked for while thread_id was only a format */
START_TEST(test_log_thread_id_with_format)
{
	char path[] = CAPTURE_PATH;
	char *out = NULL, *prefix = NULL, *tail = NULL, *half = NULL;

	log_capture_begin(LOG_FMT_RFC3339, LOG_FLAG_THREAD_ID, path);
	info("hello");
	out = log_capture_end(path);

	prefix = log_prefix(out);
	ck_assert_msg(prefix, "expected a prefix, got \"%s\"", out);

	/* the option appended the process id, thread name and thread id */
	tail = thread_id_tail();
	ck_assert_msg(xstrstr(prefix, tail),
		      "expected the tail \"%s\" in \"%s\"", tail, prefix);

	/*
	 * The format survived: the timestamp is still there, and first. Bound
	 * this to the timestamp half. Asked of the whole prefix the offset
	 * check reads the thread id's hex tail and holds either way, which is
	 * how the very regression this file exists to catch stayed green.
	 */
	half = timestamp_half(prefix, tail);
	ck_assert_msg(has_tz_offset(half),
		      "expected a timezone offset in \"%s\"", half);
	ck_assert_msg(!xstrncmp(half, "20", 2),
		      "expected a date first in \"%s\"", half);
	ck_assert_msg(xstrstr(out, "hello"), "message lost from \"%s\"", out);

	xfree(half);
	xfree(tail);
	xfree(prefix);
	xfree(out);
}

END_TEST

/* omit drops the timestamp on its own, leaving the thread id behind */
START_TEST(test_log_thread_id_with_omit)
{
	char path[] = CAPTURE_PATH;
	char *out = NULL, *prefix = NULL, *tail = NULL, *half = NULL;

	log_capture_begin(LOG_FMT_OMIT, LOG_FLAG_THREAD_ID, path);
	info("hello");
	out = log_capture_end(path);

	prefix = log_prefix(out);
	ck_assert_msg(prefix, "expected a prefix, got \"%s\"", out);

	/* "the process and thread alone": the tail is the whole prefix */
	tail = thread_id_tail();
	ck_assert_str_eq(prefix, tail);

	half = timestamp_half(prefix, tail);
	ck_assert_msg(!half[0], "omit left a timestamp in \"%s\"", prefix);
	ck_assert_msg(xstrstr(out, "hello"), "message lost from \"%s\"", out);

	xfree(half);
	xfree(tail);
	xfree(prefix);
	xfree(out);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	TCase *tcase = tcase_create("log");
	Suite *suite = suite_create("log");
	SRunner *sr = NULL;

	tcase_add_unchecked_fixture(tcase, setup, teardown);

	/* the default is 4s, and this file forks upwards of forty cases */
	tcase_set_timeout(tcase, 60);

	tcase_add_loop_test(tcase, test_log_formats, 0,
			    ARRAY_SIZE(formats));
	tcase_add_loop_test(tcase, test_log_sched, 0,
			    ARRAY_SIZE(sched_cases));
	tcase_add_loop_test(tcase, test_log_timestamp, 0,
			    ARRAY_SIZE(timestamps));
	tcase_add_loop_test(tcase, test_log_conf_values, 0,
			    ARRAY_SIZE(conf_values));
	tcase_add_test(tcase, test_log_conf_default);
	tcase_add_test(tcase, test_log_conf_reinit_clears_flag);
	tcase_add_test(tcase, test_log_rfc5424_us_precision);
	tcase_add_test(tcase, test_log_omit_alone);
	tcase_add_test(tcase, test_log_thread_id_as_format);
	tcase_add_test(tcase, test_log_thread_id_with_format);
	tcase_add_test(tcase, test_log_thread_id_with_omit);
	tcase_add_test(tcase, test_log_thread_id_is_per_thread);

	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
