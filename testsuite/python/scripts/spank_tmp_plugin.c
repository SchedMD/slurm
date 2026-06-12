/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
 \*****************************************************************************/
#include <fcntl.h>
#include <slurm/spank.h>
#include <stdio.h>
#include <stdlib.h>

SPANK_PLUGIN(spank_tmp_plugin, 1);

int slurm_spank_user_init(spank_t sp, int ac, char **av)
{
	FILE *file = fopen("/tmp/spank/slurm_spank_user_init_log", "w");
	if (file == NULL) {
		slurm_error("Failed to open slurm_spank_user_init_log file\n");
		return ESPANK_ERROR;
	}
	fprintf(file, "slurm_spank_user_init_executed\n");
	fclose(file);
	return ESPANK_SUCCESS;
}

int slurm_spank_task_post_fork(spank_t sp, int ac, char **av)
{
	FILE *file = fopen("/tmp/spank/slurm_spank_task_post_fork_log", "w");
	if (file == NULL) {
		slurm_error(
			"Failed to open slurm_spank_task_post_fork_log file\n");
		return ESPANK_ERROR;
	}
	fprintf(file, "slurm_spank_task_post_fork_executed\n");
	fclose(file);
	return ESPANK_SUCCESS;
}

int slurm_spank_task_exit(spank_t sp, int ac, char **av)
{
	FILE *file = fopen("/tmp/spank/slurm_spank_task_exit_log", "w");
	if (file == NULL) {
		slurm_error("Failed to open slurm_spank_task_exit_log file\n");
		return ESPANK_ERROR;
	}
	fprintf(file, "slurm_spank_task_exit_executed\n");
	fclose(file);
	return ESPANK_SUCCESS;
}
