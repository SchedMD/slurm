#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <slurm/spank.h>

SPANK_PLUGIN(test_spank_plugin, 1);

int slurm_spank_user_init(spank_t sp, int ac, char **av) {
	FILE *file = fopen("/tmp/test_147_1_private/slurm_spank_user_init_log", "w");
	if (file == NULL) {
		slurm_error("Failed to open slurm_spank_user_init_log file\n");
		return ESPANK_ERROR;
	}
	fprintf(file, "slurm_spank_user_init_executed\n");
	fclose(file);
	return ESPANK_SUCCESS;
}

int slurm_spank_task_post_fork(spank_t sp, int ac, char **av) {
	FILE *file = fopen("/tmp/test_147_1_private/slurm_spank_task_post_fork_log", "w");
	if (file == NULL) {
		slurm_error("Failed to open slurm_spank_task_post_fork_log file\n");
		return ESPANK_ERROR;
	}
	fprintf(file, "slurm_spank_task_post_fork_executed\n");
	fclose(file);
	return ESPANK_SUCCESS;
}

int slurm_spank_task_exit(spank_t sp, int ac, char **av) {
	FILE *file = fopen("/tmp/test_147_1_private/slurm_spank_task_exit_log", "w");
	if (file == NULL) {
		slurm_error("Failed to open slurm_spank_task_exit_log file\n");
		return ESPANK_ERROR;
	}
	fprintf(file, "slurm_spank_task_exit_executed\n");
	fclose(file);
	return ESPANK_SUCCESS;
}
