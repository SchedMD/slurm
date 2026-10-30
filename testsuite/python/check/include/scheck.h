/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

/*
 * Generic helpers for the libcheck tests, like atf.py for the python tests.
 *
 * Every libcheck test is built from a single source file, so the helpers are
 * inline static here instead of in a library. Keep them generic, and only
 * use API available in all the supported Slurm versions, as the release
 * branches run the testsuite of master.
 *
 * The helpers using ck_assert*() can only be called from a test function.
 */

#ifndef _SCHECK_H
#define _SCHECK_H

#include <check.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Create a new directory in the working directory, which is the pytest
 * tmp_path of test_100_1, so pytest removes it and the files of failed tests
 * can be inspected.
 * IN name - prefix of the directory name, a random suffix is appended
 * RET the xmalloc'ed path of the directory, or NULL on error
 */
inline static char *scheck_mkdtemp(const char *name)
{
	char *cwd = NULL, *dir = NULL;

	if (!(cwd = getcwd(NULL, 0))) {
		error("getcwd() failed: %m");
		return NULL;
	}
	dir = xstrdup_printf("%s/%s-XXXXXX", cwd, name);
	free(cwd);

	if (!mkdtemp(dir)) {
		error("mkdtemp(%s) failed: %m", dir);
		xfree(dir);
	}

	return dir;
}

/* Write content into the file dir/name, failing the test on error */
inline static void scheck_write_file(const char *dir, const char *name,
				     const char *content)
{
	char *path = NULL;
	FILE *fp = NULL;

	xstrfmtcat(path, "%s/%s", dir, name);
	fp = fopen(path, "w");
	if (!fp)
		ck_abort_msg("fopen(%s) failed: %s", path, strerror(errno));
	ck_assert_msg(fputs(content, fp) >= 0, "writing %s failed", path);
	fclose(fp);
	xfree(path);
}

/*
 * Read the whole file at path.
 * RET the xmalloc'ed content (NULL if empty), or a message about the error to
 *     show in the test output if the file cannot be opened
 */
inline static char *scheck_read_file(const char *path)
{
	char buf[4096], *txt = NULL;
	size_t len = 0;
	FILE *fp = fopen(path, "r");

	if (!fp)
		return xstrdup_printf("(cannot open %s: %s)", path,
				      strerror(errno));
	while ((len = fread(buf, 1, sizeof(buf), fp)) > 0)
		xstrncat(txt, buf, len);
	fclose(fp);
	return txt;
}

/*
 * Log into log_path up to file_level, and also into stderr up to stderr_level.
 * Read it with scheck_read_file() to check the messages logged.
 * RET the return code of log_alter()
 */
inline static int scheck_log_to_file(char *log_path, log_level_t file_level,
				     log_level_t stderr_level)
{
	log_options_t opts = LOG_OPTS_INITIALIZER;

	opts.stderr_level = stderr_level;
	opts.syslog_level = LOG_LEVEL_QUIET;
	opts.logfile_level = file_level;

	return log_alter(opts, 0, log_path);
}

/* RET the number of lines of log_txt matching the extended regex pattern */
inline static int scheck_log_count(const char *log_txt, const char *pattern)
{
	regex_t re = { 0 };
	char *copy, *line, *save_ptr = NULL;
	int cnt = 0;

	ck_assert_msg(!regcomp(&re, pattern, (REG_EXTENDED | REG_NOSUB)),
		      "invalid regex: %s", pattern);

	copy = xstrdup(log_txt);
	line = copy ? strtok_r(copy, "\n", &save_ptr) : NULL;
	while (line) {
		if (!regexec(&re, line, 0, NULL, 0))
			cnt++;
		line = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(copy);
	regfree(&re);

	return cnt;
}

/*
 * Assert that exactly cnt lines of log_txt match the extended regex pattern.
 * IN id - identifier of the case, to prefix the failure message
 */
inline static void scheck_assert_log_count(const char *id, const char *log_txt,
					   const char *pattern, int cnt)
{
	int got = scheck_log_count(log_txt, pattern);

	ck_assert_msg(
		got == cnt,
		"%s: there should be %d log lines matching \"%s\", but got %d. Log:\n%s",
		id, cnt, pattern, got, log_txt);
}

#endif
