/*****************************************************************************\
 *  Test gres.conf and system GPU normalization and merging logic.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Written by Michael Hinton
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

#include <sys/stat.h>
#include "src/common/gres.h"
#include "src/common/xstring.h"

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc;
	char *node_name = argv[2];
	char *slurm_conf = NULL;
	char *gres_conf = NULL;
	char *fake_gpus_conf = NULL;
	struct stat stat_buf;

	if (argc != 3) {
		printf("FAILURE: Not enough or too many arguments!\n");
		exit(1);
	}

	xstrfmtcat(slurm_conf, "%s/%s", argv[1], "slurm.conf");
	xstrfmtcat(gres_conf, "%s/%s", argv[1], "gres.conf");
	xstrfmtcat(fake_gpus_conf, "%s/%s", argv[1], "fake_gpus.conf");

	if (stat(slurm_conf, &stat_buf) != 0) {
		printf("FAILURE: Could not find slurm_conf file at %s\n",
		       slurm_conf);
		exit(1);
	}
	if (stat(gres_conf, &stat_buf) != 0) {
		printf("FAILURE: Could not find gres_conf file at %s\n",
		       gres_conf);
		exit(1);
	}
	if (stat(fake_gpus_conf, &stat_buf) != 0) {
		printf("FAILURE: Could not find fake_gpus_conf file at %s\n",
		       fake_gpus_conf);
		exit(1);
	}
	printf("slurm_conf: %s\n", slurm_conf);
	printf("gres_conf: %s\n", gres_conf);
	printf("fake_gpus_conf: %s\n", fake_gpus_conf);

	// Only log info to avoid buffer truncation in expect regex
	opts.stderr_level = LOG_LEVEL_INFO;
	log_init(argv[0], opts, SYSLOG_FACILITY_USER, NULL);

	// Override where Slurm looks for conf files
	setenv("SLURM_CONF", slurm_conf, 1);

	rc = gres_plugin_node_config_load(4, node_name, NULL, NULL, NULL);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_node_config_load");
		exit(1);
	}

/*
 * You'll have to reconfigure Slurm with --enable-memory-leak-debug to eliminate
 * all "possibly lost" blocks and to see the full call stack of valgrind memory
 * errors inside plugins. See plugin_unload() in src/common/plugin.c
 */
#ifdef USING_VALGRIND
	// Clean up for valgrind
	slurm_conf_destroy();
	gres_plugin_fini();
	log_fini();
	xfree(slurm_conf);
	xfree(gres_conf);
	xfree(fake_gpus_conf);
#endif

	printf("Test ran to completion\n");
	exit(0);
}
