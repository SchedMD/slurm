/*****************************************************************************\
 *  test7.17.prog.c - standalone program to test GRES APIs
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Written by Morris Jette
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <grp.h>
#include <errno.h>
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

#include "src/common/gres.h"
#include "src/common/log.h"
#include "src/common/pack.h"

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc;
	uint32_t cpu_count, cpu_alloc, job_id = 12345;
	char *node_name, *reason_down = NULL;
	char *orig_config, *new_config = NULL, *job_config = NULL;
	Buf buffer;
	List job_gres_list = NULL, node_gres_list = NULL;
	bitstr_t *cpu_bitmap;

	/* Enable detailed logging for now */
	opts.stderr_level = LOG_LEVEL_DEBUG;
	log_init(argv[0], opts, SYSLOG_FACILITY_USER, NULL);

	/*
	 * Logic normally executed by slurmd daemon
	 */
	rc = gres_plugin_init();
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_init");
		exit(1);
	}

	/* FIXME: Read values from slurm.conf? */
	cpu_count = 32;
	node_name = "jette";
	rc = gres_plugin_node_config_load(cpu_count, node_name);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_node_config_load");
		exit(1);
	}

	buffer = init_buf(1024);
	rc = gres_plugin_node_config_pack(buffer);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_node_config_pack");
		exit(1);
	}

	/*
	 * Logic normally executed by slurmctld daemon
	 */
	/* FIXME: Read values from slurm.conf */
	orig_config = "craynetwork:4";
	rc = gres_plugin_init_node_config(node_name, orig_config,
					  &node_gres_list);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_init_node_config");
		exit(1);
	}

	set_buf_offset(buffer, 0);
	rc = gres_plugin_node_config_unpack(buffer, node_name);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_node_config_unpack");
		exit(1);
	}

	rc = gres_plugin_node_config_validate(node_name, orig_config,
					      &new_config, &node_gres_list,
					      0, &reason_down);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_node_config_validate");
		exit(1);
	}

	if (argc == 2)
		job_config = argv[1];
	rc = gres_plugin_job_state_validate(job_config, &job_gres_list);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_job_state_validate");
		exit(1);
	}

	gres_plugin_node_state_log(node_gres_list, node_name);
	gres_plugin_job_state_log(job_gres_list, job_id);

	cpu_bitmap = bit_alloc(cpu_count);
	cpu_alloc = gres_plugin_job_test(job_gres_list, node_gres_list, true,
					 cpu_bitmap, 0, cpu_count - 1,
					 job_id, node_name);
	if (cpu_alloc == NO_VAL)
		printf("cpu_alloc=ALL\n");
	else
		printf("cpu_alloc=%u\n", cpu_alloc);

	rc = gres_plugin_fini();
	if (rc != SLURM_SUCCESS) {
		slurm_perror("FAILURE: gres_plugin_fini");
		exit(1);
	}

	printf("Test ran to completion\n");
	exit(0);
}
