#include <slurm/spank.h>
#include <string.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifndef PLUGIN_NAME
#define PLUGIN_NAME spank_fail_test
#endif

SPANK_PLUGIN(PLUGIN_NAME, 1);

int slurm_spank_init_failure_mode = ESPANK_NODE_FAILURE;

static char *current_context(void)
{
	switch (spank_context()) {
	case S_CTX_ERROR:
		return "error";
	case S_CTX_LOCAL:
		return "local";
	case S_CTX_REMOTE:
		return "remote";
	case S_CTX_ALLOCATOR:
		return "allocator";
	case S_CTX_SLURMD:
		return "slurmd";
	case S_CTX_JOB_SCRIPT:
		return "job_script";
	default:
		return "unknown";
	}
}

/*
 * This plugin will fail in one of the SPANK plugin entrypoints if configured
 * to do so via flags in the plugstack file.
 * Syntax:
 *     required spank_test.so <func> <context> [<job|node>]
 * Examples:
 *     required spank_test.so slurm_spank_init remote
 *     required spank_test.so slurm_spank_init remote job
 *     required spank_test.so slurm_spank_init remote node
 */
static int fail_if(int ac, char **av, const char *func)
{
	if (ac == 0)
		return ESPANK_SUCCESS;
	if (ac == 2 || ac == 3) {
		if (ac == 3) {
			if (strcasecmp(av[2], "job") == 0)
				slurm_spank_init_failure_mode =
					ESPANK_JOB_FAILURE;
			else if (strcasecmp(av[2], "node"))
				slurm_error(
					"Invalid failure mode '%s'. Use 'job' or 'node'.",
					av[2]);
		}

		/* Match function name and context. */
		if (!strcmp(av[0], func) &&
		    !strcasecmp(av[1], current_context()))
			return -1;
		return ESPANK_SUCCESS;
	}

	slurm_error(
		"The plugin must be configured with both <func> <context> [<job|node>] or neither.");
	return -1;
}

#define ENTRYPOINT(FUNC)                                                      \
	int FUNC(spank_t sp, int ac, char **av)                               \
	{                                                                     \
		slurm_spank_log("%s: %s %s", TOSTRING(PLUGIN_NAME), __func__, \
				current_context());                           \
		return fail_if(ac, av, __func__);                             \
	}

ENTRYPOINT(slurm_spank_init)
ENTRYPOINT(slurm_spank_job_prolog)
ENTRYPOINT(slurm_spank_init_post_opt)
ENTRYPOINT(slurm_spank_local_user_init)
ENTRYPOINT(slurm_spank_user_init)
ENTRYPOINT(slurm_spank_task_init_privileged)
ENTRYPOINT(slurm_spank_task_init)
ENTRYPOINT(slurm_spank_task_post_fork)
ENTRYPOINT(slurm_spank_task_exit)
ENTRYPOINT(slurm_spank_job_epilog)
ENTRYPOINT(slurm_spank_slurmd_exit)
ENTRYPOINT(slurm_spank_exit)
