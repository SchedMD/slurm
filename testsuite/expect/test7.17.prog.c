/*****************************************************************************\
 *  test7.17.prog.c - standalone program to test GRES APIs
 *****************************************************************************
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

#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"

#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/strlcpy.h"
#include "src/common/xstring.h"

/*
 * main - slurmctld main function, start various threads and process RPCs
 * test7.17.prog <TRES_PER_NODE> <CONFIG_DIR_HEAD> <CONFIG_SUB_DIR> <CPU_COUNT>
 *
 */
int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc;
	uint32_t cpu_count, cpu_alloc, job_id = 12345;
	char *node_name, *reason_down = NULL;
	char *orig_config, *new_config = NULL, *tres_per_node = NULL;
	buf_t *buffer;
	list_t *job_gres_list = NULL;
	char config_dir[1000], test[1000];
	char slurm_conf[1000];
	uint32_t num_tasks = 1;
	uint32_t min_cpus = 1;
	uint32_t min_nodes = 1;
	uint32_t max_nodes = 1;
	uint16_t ntasks_per_node = NO_VAL16;
	uint16_t ntasks_per_socket = NO_VAL16;
	uint16_t sockets_per_node = NO_VAL16;
	uint16_t cpus_per_task = NO_VAL16;
	uint16_t ntasks_per_tres = NO_VAL16;
	int core_count, sock_count;
	gres_job_state_validate_t gres_js_val = {
		.cpus_per_task = &cpus_per_task,
		.max_nodes = &max_nodes,
		.min_cpus = &min_cpus,
		.min_nodes = &min_nodes,
		.ntasks_per_node = &ntasks_per_node,
		.ntasks_per_socket = &ntasks_per_socket,
		.ntasks_per_tres = &ntasks_per_tres,
		.num_tasks = &num_tasks,
		.sockets_per_node = &sockets_per_node,

		.gres_list = &job_gres_list,
	};
	node_record_t *node_ptr;

	/* Initializing node_ptr */
	node_ptr = xmalloc(sizeof(node_record_t));
	node_ptr->config_ptr = xmalloc(sizeof(config_record_t));

	/* Setup slurm.conf and gres.conf test paths */
	strlcpy(config_dir, argv[2], sizeof(config_dir));
	strlcpy(config_dir, strcat(config_dir, "/test7.17_configs"),
		sizeof(config_dir));
	strlcpy(test, strcat(config_dir, argv[3]), sizeof(test));
	strlcpy(slurm_conf, strcat(test, "/slurm.conf"), sizeof(slurm_conf));

	/* Enable detailed logging for now */
	opts.stderr_level = LOG_LEVEL_DEBUG;
	log_init(argv[0], opts, SYSLOG_FACILITY_USER, NULL);

	/*
	 * Logic normally executed by slurmd daemon
	 */
	setenv("SLURM_CONF", slurm_conf, 1);

	slurm_init(NULL);

	if (select_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize node selection plugin");

	/*
	 * Logic normally executed by slurmctld daemon
	 */
	orig_config = "gpu:8";
	node_ptr->config_ptr->gres = orig_config;
	gres_init_node_config(orig_config, &node_ptr->gres_list);
	cpu_count = strtol(argv[4], NULL, 10);
	core_count = strtol(argv[5], NULL, 10);
	sock_count = strtol(argv[6], NULL, 10);
	node_name = "test_node";
	node_ptr->name = node_name;
	rc = gres_g_node_config_load(cpu_count, node_name, node_ptr->gres_list,
				     NULL, NULL);
	if (rc)
		fatal("failure: gres_node_config_load: %s",
		      slurm_strerror(rc));

	buffer = init_buf(1024);
	rc = gres_node_config_pack(buffer);
	if (rc)
		fatal("failure: gres_node_config_pack: %s",
		      slurm_strerror(rc));

	set_buf_offset(buffer, 0);
	rc = gres_node_config_unpack(buffer, node_name);
	if (rc != SLURM_SUCCESS) {
		slurm_perror("failure: gres_node_config_unpack");
		exit(1);
	}

	rc = gres_node_config_validate(node_ptr, cpu_count, core_count,
				       sock_count, 0, &reason_down);
	if (rc)
		fatal("failure: gres_node_config_validate: %s",
		      slurm_strerror(rc));

	if (argc > 2)
		gres_js_val.tres_per_node = xstrdup(argv[1]);

	rc = gres_job_state_validate(&gres_js_val);
	if (rc)
		fatal("failure: gres_job_state_validate: %s",
		      slurm_strerror(rc));

	gres_node_state_log(node_ptr->gres_list, node_name);
	gres_job_state_log(job_gres_list, job_id);

	cpu_alloc = gres_job_test(job_gres_list, node_ptr->gres_list, true, 0,
				  cpu_count - 1, job_id, node_name);
	if (cpu_alloc == NO_VAL)
		printf("cpu_alloc=ALL\n");
	else
		printf("cpu_alloc=%u\n", cpu_alloc);

	rc = gres_fini();
	if (rc != SLURM_SUCCESS)
		fatal("failure: gres_fini: %s", slurm_strerror(rc));

	printf("Test %s ran to completion\n\n", argv[3]);
	return rc;
}
