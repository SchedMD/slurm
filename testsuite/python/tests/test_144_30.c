/*****************************************************************************\
 *  test_144_30.c - Test gres/tpu sysfs discovery against mock device trees.
 *****************************************************************************
 *  Copyright (C) 2026 Google LLC.
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

#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/interfaces/gres.h"

static int _mac_to_abs(char *mac, char **abs)
{
	*abs = xstrdup(mac);
	return SLURM_SUCCESS;
}

/*
 * Usage: test_144_30 <etc_dir> <sysfs_dir>
 *
 * Exercises gres/tpu's sysfs discovery via gres_g_node_config_load() with
 * NULL slurm.conf node GRES so every reported GRES[tpu] record comes from
 * sysfs autodetection.
 */
int main(int argc, char **argv)
{
	log_options_t log_opts = LOG_OPTS_STDERR_ONLY;
	char *slurm_conf_path = NULL;
	uint32_t cpu_cnt = 96;
	int rc;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <etc_dir> <sysfs_dir>\n", argv[0]);
		return 1;
	}

	xstrfmtcat(slurm_conf_path, "%s/slurm.conf", argv[1]);
	setenv("SLURM_CONF", slurm_conf_path, 1);
	setenv("SLURM_TPU_SYSFS_PATH", argv[2], 1);

	log_opts.stderr_level = LOG_LEVEL_DEBUG2;
	log_init("test_144_30", log_opts, 0, NULL);

	if (slurm_conf_init(NULL) != SLURM_SUCCESS) {
		fprintf(stderr, "FATAL: cannot read slurm.conf\n");
		xfree(slurm_conf_path);
		return 1;
	}

	/* Emit merged GRES records via print_gres_list(). */
	slurm_conf.debug_flags |= DEBUG_FLAG_GRES;

	if ((rc = gres_init()) != SLURM_SUCCESS) {
		fprintf(stderr, "FATAL: gres_init: %s\n", slurm_strerror(rc));
		xfree(slurm_conf_path);
		return 1;
	}

	rc = gres_g_node_config_load(cpu_cnt, "testnode", NULL, NULL,
				     _mac_to_abs);
	if (rc != SLURM_SUCCESS) {
		fprintf(stderr, "FATAL: gres_g_node_config_load: %s\n",
			slurm_strerror(rc));
		xfree(slurm_conf_path);
		return 1;
	}

	printf("DISCOVERY_OK\n");
	xfree(slurm_conf_path);
	return 0;
}
