/*****************************************************************************\
 *  test7.17.prog.c - standalone program to test GRES APIs
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Written by Morris Jette
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
#include "src/common/xstring.h"

/* main - slurmctld main function, start various threads and process RPCs */
int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc;
	uint32_t cpu_count, cpu_alloc, job_id = 12345;
	char *node_name, *reason_down = NULL;
	char *orig_config, *new_config = NULL, *tres_per_node = NULL;
	Buf buffer;
	List job_gres_list = NULL, node_gres_list = NULL;
	bitstr_t *cpu_bitmap;
	char config_dir[10000], test[1000];
	char slurm_conf[1000];
	uint32_t num_tasks = 1;
	uint32_t min_nodes = 1;
	uint32_t max_nodes = 1;
	uint16_t ntasks_per_node = NO_VAL16;
	uint16_t ntasks_per_socket = NO_VAL16;
	uint16_t sockets_per_node = NO_VAL16;
	uint16_t cpus_per_task = NO_VAL16;
	int core_count;

	/* Setup slurm.conf and gres.conf test paths */
	strcpy(config_dir, argv[2]);
	strcpy(config_dir,strcat(config_dir, "/test7.17_configs"));
	strcpy(test, strcat(config_dir, argv[3]));
	strcpy(slurm_conf,strcat(test, "/slurm.conf"));

	/* Enable detailed logging for now */
	opts.stderr_level = LOG_LEVEL_DEBUG;
	log_init(argv[0], opts, SYSLOG_FACILITY_USER, NULL);

	/*
	 * Logic normally executed by slurmd daemon
	 */
	setenv("SLURM_CONF", slurm_conf, 1);
	rc = gres_plugin_init();
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_init");
		exit(1);
	}

	setenv("SLURM_CONFIG_DIR",config_dir, 1);

	cpu_count = strtol(argv[4], NULL, 10);
	node_name = "test_node";
	rc = gres_plugin_node_config_load(cpu_count, node_name, NULL);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_node_config_load");
		exit(1);
	}

	buffer = init_buf(1024);
	rc = gres_plugin_node_config_pack(buffer);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_node_config_pack");
		exit(1);
	}

	/*
	 * Logic normally executed by slurmctld daemon
	 */
	orig_config = "gpu:8";
	rc = gres_plugin_init_node_config(node_name, orig_config,
					  &node_gres_list);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_init_node_config");
		exit(1);
	}

	set_buf_offset(buffer, 0);
	rc = gres_plugin_node_config_unpack(buffer, node_name);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_node_config_unpack");
		exit(1);
	}

	core_count = cpu_count;
	rc = gres_plugin_node_config_validate(node_name, orig_config,
					      &new_config, &node_gres_list,
					      cpu_count, core_count,
					      0, &reason_down);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_node_config_validate");
		exit(1);
	}

	if (argc > 2)
		tres_per_node = xstrdup(argv[1]);

	rc = gres_plugin_job_state_validate(NULL,	/* cpus_per_tres */
					    NULL,	/* tres_per_job */
					    tres_per_node,
					    NULL,	/* tres_per_socket */
					    NULL,	/* tres_per_task */
					    NULL,	/* mem_per_tres */
					    &num_tasks,
					    &min_nodes,
					    &max_nodes,
					    &ntasks_per_node,
					    &ntasks_per_socket,
					    &sockets_per_node,
					    &cpus_per_task,
					    &job_gres_list);
	if (rc != SLURM_SUCCESS) {
		slurm_seterrno(rc);
		slurm_perror("failure: gres_plugin_job_state_validate");
		exit(1);
	}

	gres_plugin_node_state_log(node_gres_list, node_name);
	gres_plugin_job_state_log(job_gres_list, job_id);

	cpu_bitmap = bit_alloc(cpu_count);
	bit_nset(cpu_bitmap, 0, cpu_count - 1);
	cpu_alloc = gres_plugin_job_test(job_gres_list, node_gres_list, true,
					 cpu_bitmap, 0, cpu_count - 1,
					 job_id, node_name);
	if (cpu_alloc == NO_VAL)
		printf("cpu_alloc=ALL\n");
	else
		printf("cpu_alloc=%u\n", cpu_alloc);

	rc = gres_plugin_fini();
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_plugin_fini");
		exit(1);
	}

	printf("Test %s ran to completion\n\n", argv[3]);
	exit(0);
}
