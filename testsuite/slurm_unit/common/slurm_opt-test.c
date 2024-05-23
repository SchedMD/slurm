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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

Suite *slurm_opt_suite(void)
{
	Suite *s = suite_create("slurm_opt");
	TCase *tc_core = tcase_create("Core");
	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	/* Set up Slurm logging */
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("slurm_opt-test", log_opts, 0, NULL);

	/* Call slurm_init() with a mock slurm.conf*/
	int fd;
	char *slurm_unit_conf_filename = xstrdup("slurm_unit.conf-XXXXXX");
	if ((fd = mkstemp(slurm_unit_conf_filename)) == -1) {
		error("error creating slurm_unit.conf (%s)",
		      slurm_unit_conf_filename);
		return EXIT_FAILURE;
	} else
		debug("fake slurm.conf created: %s", slurm_unit_conf_filename);

	/*
	 * PluginDir=. is needed as loading the slurm.conf will check for the
	 * existence of the dir. As 'make check' doesn't install anything the
	 * normal PluginDir might not exist. As we don't load any plugins for
	 * these test this should be ok.
	 */
	char slurm_unit_conf_content[] = "ClusterName=slurm_unit\n"
		                         "PluginDir=.\n"
					 "SlurmctldHost=slurm_unit\n";
	size_t csize = sizeof(slurm_unit_conf_content);
	ssize_t rc = write(fd, slurm_unit_conf_content, csize);
	if (rc < csize) {
		error("error writing slurm_unit.conf (%s)",
		      slurm_unit_conf_filename);
		return EXIT_FAILURE;
	}

	/* Do not load any plugins, we are only testing slurm_opt */
	if (slurm_conf_init(slurm_unit_conf_filename) != SLURM_SUCCESS) {
		error("slurm_conf_init() failed");
		return EXIT_FAILURE;
	}

	unlink(slurm_unit_conf_filename);
	xfree(slurm_unit_conf_filename);
	close(fd);

	/* Start the actual libcheck code */
	int number_failed;
	SRunner *sr = srunner_create(slurm_opt_suite());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
