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
#include "src/common/read_config.h"
#include "src/common/select.h"
#include "src/common/xstring.h"

/*
 * test39.18.prog <etc_dir> <nodename> <conf_gres> [<debug_level>]
 *
 * etc_dir	The directory containing slurm.conf, gres.conf, and
 * 		fake_gpus.conf.
 * nodename	The name of the node.
 * conf_gres	A string indicating the GRES ostensibly parsed from a
 * 		slurm.conf for the node. E.g., `gpu:4`.
 * debug_level	(optional) A number representing the log_level_t the program
 * 		should use. If unspecified, defaults to LOG_LEVEL_INFO.
 * 		LOG_LEVEL_INFO is the lowest log level allowed.
 * 		Note that debug, debug2, and debug3 may produce too much output
 * 		and cause expect to fail to parse things properly. This will
 * 		show up as a test failure. Only use debug+ when debugging and
 * 		developing tests, and NOT when running the tests in production.
 *
 * Note that slurm.conf only needs to specify the following fields:
 *	ControlMachine=test_machine
 *	ClusterName=test_cluster
 *	GresTypes=gpu,mps,nic,mic,tmpdisk
 *
 * The actual GRES for the node is specified in conf_gres, not slurm.conf. This
 * makes it so we don't need to re-create the slurm.conf each time we run this
 * test runner program.
 *
 * However, gres.conf and fake_gpus.conf do need to be re-created for each test.
 */
int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc;
	char *etc_dir = NULL;
	char *node_name = NULL;
	char *slurm_conf_gres_str = NULL;
	char *slurm_conf = NULL;
	char *gres_conf = NULL;
	char *fake_gpus_conf = NULL;
	struct stat stat_buf;
	List gres_list = NULL;
	log_level_t debug_level = LOG_LEVEL_INFO;

	if (argc < 4) {
		printf("FAILURE: Not enough arguments!\n");
		exit(1);
	}

	if (argc > 5) {
		printf("FAILURE: Too many arguments!\n");
		exit(1);
	}

	etc_dir = argv[1];
	node_name = argv[2];
	slurm_conf_gres_str = argv[3];
	if (argc == 5)
		debug_level = atoi(argv[4]);

	if (debug_level < LOG_LEVEL_INFO) {
		printf("FAILURE: LOG_LEVEL_INFO is the lowest log level allowed!\n");
		exit(1);
	}

	xstrfmtcat(slurm_conf, "%s/%s", etc_dir, "slurm.conf");
	xstrfmtcat(gres_conf, "%s/%s", etc_dir, "gres.conf");
	xstrfmtcat(fake_gpus_conf, "%s/%s", etc_dir, "fake_gpus.conf");

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

	opts.stderr_level = debug_level;
	log_init(argv[0], opts, SYSLOG_FACILITY_USER, NULL);

	// Override where Slurm looks for conf files
	setenv("SLURM_CONF", slurm_conf, 1);

	slurm_init(NULL);

	// Initialize GRES info (from slurm.conf)
	rc = gres_init_node_config(slurm_conf_gres_str, &gres_list);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_init_node_config");
		exit(1);
	}

	rc = gres_g_node_config_load(4, node_name, gres_list, NULL, NULL);
	FREE_NULL_LIST(gres_list);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_node_config_load");
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
	gres_fini();
	select_g_fini();
	log_fini();
	xfree(slurm_conf);
	xfree(gres_conf);
	xfree(fake_gpus_conf);
#endif

	printf("Test ran to completion\n");
	exit(0);
}
