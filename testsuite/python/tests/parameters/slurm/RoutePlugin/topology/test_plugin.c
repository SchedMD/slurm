/*****************************************************************************\
 *  test_plugin.c - standalone program to test route/topology
 *****************************************************************************
 *  Copyright (C) 2014 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
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

#include <grp.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_route.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAX_LINES_IN_TEST 200
#define MAX_LINE 100

typedef struct {
	char *testcases;
	char *configdir;
	int   measure;
	int   verbose;
} sroutetest_opts_t;


static sroutetest_opts_t params;

static int  _check_params(void);
static void _free_options(void);
static void _help_msg(void);
static void _init_options(void);
static int  _set_options(const int argc, char **argv);

static void _init_opts(void)
{
	memset(&params, 0, sizeof(sroutetest_opts_t));
	params.measure = 0;
	params.verbose = 0;
}

static int _set_options(const int argc, char **argv)
{
	int option_index = 0;
	int cc;
	char *next_str = NULL;

	static struct option long_options[] = {
		{"configdir", required_argument, 0, 'c'},
		{"measure", no_argument, 0, 'm'},
		{"testcases", required_argument, 0, 't'},
		{"usage", no_argument, 0, 'U'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}};

	_init_opts();

	while ((cc = getopt_long(argc, argv, "cmt:Uv",
	                         long_options, &option_index)) != EOF) {
		switch (cc) {
			case 'c':
				params.configdir = xstrdup(optarg);
				break;
			case 'm':
				params.measure++;
				break;
			case 't':
				params.testcases = xstrdup(optarg);
				break;
			case 'U':
				_help_msg();
				return -1;
				break;
			case 'v':
				params.verbose++;
				break;
			case ':':
			case '?': /* getopt() has explained it */
				return -1;
		}
	}

	return 0;
}

/* _check_params()
 */
static int
_check_params(void)
{
	char* conf_path;
	if (params.testcases == NULL) {
		error("testcases must be specified.");
		return -1;
	}
	if (params.configdir) {
		conf_path = xstrdup_printf("%s/slurm.conf",params.configdir);
		setenv("SLURM_CONF",conf_path,1);
		xfree(conf_path);
	}

	return 0;
}


static void _help_msg(void)
{
	info("\
Usage sroutetest [<OPTION>]\n"
"\n"
"Valid <OPTION> values are:\n"
" -t, --testcases      Path to a file containing test cases.\n"
" -m, --measure        Measure each test case\n"
" -v, --verbose        print test cases and results for successful tests \n"
" --usage              Display brief usage message\n");
}


/* _free_options()
 */
static void
_free_options(void)
{
	xfree(params.testcases);
	xfree(params.configdir);
}

int _measure_api(char* measure_case)
{

	int i,j,et;
	int hl_count = 0;
	char* nodes;
	hostlist_t hl;
	DEF_TIMERS;

	hostlist_t* sp_hl;
	nodes = measure_case;
	hl = hostlist_create(nodes);
	START_TIMER;
	if (route_g_split_hostlist(hl, &sp_hl, &hl_count, 0)) {
		hostlist_destroy(hl);
		fatal("unable to split forward hostlist");
	}
	END_TIMER;
	et = DELTA_TIMER;
	for (j = 0; j < hl_count; j++) {
		hostlist_destroy(sp_hl[j]);
	}
	xfree(sp_hl);
	hostlist_destroy(hl);
	info("%d usec to split %s", et, nodes);
	return et;
}

void _print_test(char** testcase, int lines)
{
	int i;
	for (i=1; i<lines; i++) {
		info("   expected sublist[%d]=%s",i,testcase[i]);
	}
}

void _print_results(hostlist_t* hll, int hl_count)
{
	int i;
	char *list;
	info("   results list_count=%d", hl_count);
	for (i=0; i<hl_count; i++) {
		list = hostlist_ranged_string_xmalloc(hll[i]);
		info("   returned sublist[%d]=%s",i,list);
		xfree(list);
	}
}

int _run_test(char** testcase, int lines)
{
	int i, rc;
	hostlist_t* hll = NULL;
	int hl_count = 0;
	int level;
	char *list;
	char *nodes;
	nodes = testcase[0];
	hostlist_t hl = hostlist_create(nodes);
	if (route_g_split_hostlist(hl, &hll, &hl_count, 0)) {
		info("Unable to split forward hostlist");
		_print_test(testcase,lines);
		rc = SLURM_ERROR;
		goto clean;
	}
	if (hl_count != (lines-1)) {
		info("Expected #lines is %d, not #returned %d", lines,
			hl_count);
		_print_test(testcase,lines);
		_print_results(hll, hl_count);
		rc = SLURM_ERROR;
		goto clean;
	}
	for (i = 0; i < hl_count; i++) {
		list = hostlist_ranged_string_xmalloc(hll[i]);
		if (strcmp(list, testcase[i+1]) != 0) {
			info("List[%d]=%s not expected %s", i, list,
				testcase[i+1]);
			_print_test(testcase,lines);
			xfree(list);
			rc = SLURM_ERROR;
			goto clean;
		}
		xfree(list);
	}
	info("Test OK (%s)", testcase[0]);
	if (params.verbose > 0)
		_print_test(testcase,lines);
	rc = SLURM_SUCCESS;

clean:
	for (i = 0; i < hl_count; i++) {
		hostlist_destroy(hll[i]);
	}
	xfree(hll);
	return rc;
}

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	int i, tl, l, rc, et;
	int   ncases = 0;
	int   nfail = 0;
	char** testcase;
	char*  measure_case;
	FILE *fd;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	int cc;

	cc = _set_options(argc, argv);
	if (cc < 0)
		goto ouch;

	cc = _check_params();
	if (cc < 0)
		goto ouch;

	slurm_init(NULL);
	opts.stderr_level = LOG_LEVEL_DEBUG;
	log_init(argv[0], opts, SYSLOG_FACILITY_USER, NULL);

	if ((fd = fopen(params.testcases, "r")) == NULL) {
		info("Failed to open %s: %m",params.testcases);
		exit(1);
	}
	testcase = xmalloc(sizeof (char*) * MAX_LINES_IN_TEST);
	for (i=0; i<MAX_LINES_IN_TEST; i++) {
		testcase[i] = malloc(sizeof(char) * (MAX_LINE+1));
	}
	tl = 0;
	while (fgets(testcase[tl], MAX_LINE, fd) != NULL) {
		l = strlen(testcase[tl]);
		if (l == 0)
			continue;
		if (testcase[tl][0] == '#')
			continue; // Comment
		testcase[tl][l-1] = '\0';
		if (strlen(testcase[tl]) == 0) {
			if (tl > 0) {
				ncases++;
				measure_case = testcase[0];
				if (params.measure > 0) {
					et = _measure_api(measure_case);
				} else {
					rc = _run_test(testcase, tl);
					if (rc == SLURM_ERROR)
						nfail++;
				}
				tl = -1;
			}
		}
		tl++;

	}
	if (tl > 0) {
		ncases++;
		measure_case = testcase[0];
		if (params.measure > 0) {
			et = _measure_api(measure_case);
		} else {
			rc = _run_test(testcase, tl);
			if (rc == SLURM_ERROR)
				nfail++;
		}
	}
	fclose(fd);
	for (i=0; i<MAX_LINES_IN_TEST; i++) {
		free(testcase[i]);
	}
	xfree(testcase);
	if (params.measure == 0) {
		info("\nTotal test cases %d, Failed cases %d\n",
				ncases, nfail);
	}
	_free_options();
	exit(0);

ouch:
	_free_options();
	return 0;

}
