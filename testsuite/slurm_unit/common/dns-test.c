/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
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

#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/log.h"
#include "src/common/read_config.h"

START_TEST(test_dns) {}

END_TEST

extern Suite *suite_dns(void)
{
	Suite *s = suite_create("dns");
	TCase *tc_core = tcase_create("dns");

	tcase_add_test(tc_core, test_dns);

	suite_add_tcase(s, tc_core);
	return s;
}

extern int main(void)
{
	enum print_output po = CK_ENV;
	enum fork_status fs = CK_FORK_GETENV;
	SRunner *sr = NULL;
	int number_failed = 0;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("dns-test", log_opts, 0, NULL);

	if (log_opts.stderr_level >= LOG_LEVEL_DEBUG) {
		/* automatically be gdb friendly when debug logging */
		po = CK_VERBOSE;
		fs = CK_NOFORK;
	}

	sr = srunner_create(suite_dns());
	srunner_set_fork_status(sr, fs);
	srunner_run_all(sr, po);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
