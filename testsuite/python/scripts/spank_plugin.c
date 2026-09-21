/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
 *****************************************************************************
 *  spank_plugin.c - Generic SPANK plugin for testing.
 *
 *  Configuration is via environment variables:
 *
 *  FAILURE INJECTION
 *    SPANK_FAIL_TEST_FUNC -- SPANK callback name to fail in
 *                            (e.g. slurm_spank_init, slurm_spank_user_init).
 *    SPANK_FAIL_TEST_CTXT -- SPANK context in which to fail (local, remote,
 *                            allocator, slurmd, job_script).
 *    SPANK_FAIL_TEST_MODE -- optional. Affects only slurm_spank_init failures:
 *                              "node" -> ESPANK_NODE_FAILURE (default)
 *                              "job"  -> ESPANK_JOB_FAILURE
 *    If FUNC and CTXT are both unset the plugin is inert on this path.
 *    Every callback logs one of:
 *        [Job: <id>] Found (<target_func>,<target_ctx>)
 *        [Job: <id>] Looking for (<func>,<ctx>) but found (<func>,<ctx>). Continuing...
 *    The targeted callback returns -ESPANK_ERROR; others return ESPANK_SUCCESS.
 *
 *  HOOK MARKER FILES
 *    SPANK_HOOK_CREATE_FILE -- boolean (any non-empty value enables). Each hook
 *                          that runs creates a file in SPANK_TMP_DIR named
 *                          {hookname}_log to signal it executed. SPANK_TMP_DIR
 *                          is baked in at compile time. Useful for verifying
 *                          hook isolation inside job containers (contain_spank).
 *
 *  Both capabilities are independent: both, one, or neither may be active at
 *  the same time. In remote context, env vars are read via spank_getenv()
 *  because the process environment is wiped; in other contexts, getenv() is
 *  used on the client process environment.
\*****************************************************************************/
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <slurm/spank.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME spank_plugin
#endif

#ifndef SPANK_TMP_DIR
#error "SPANK_TMP_DIR must be defined at compile time (passed via -DSPANK_TMP_DIR=...)"
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

SPANK_PLUGIN(PLUGIN_NAME, 1);

#define MAX_PATH 4096

int slurm_spank_init_failure_mode = ESPANK_NODE_FAILURE;

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

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
 * Read env var by name. In remote context the process env is wiped, so
 * spank_getenv() is the only path that sees what the submitter sent over.
 * In other contexts fall back to getenv() on the client process env.
 * Returns 0 on success (out is NUL-terminated and non-empty), -1 otherwise.
 */
static int _get_env(spank_t sp, const char *name, char *out, size_t outsz)
{
	if (spank_context() == S_CTX_REMOTE) {
		if (spank_getenv(sp, name, out, outsz - 1) == ESPANK_SUCCESS &&
		    out[0] != '\0')
			return 0;
		return -1;
	}

	const char *v = getenv(name);
	if (v && v[0]) {
		strncpy(out, v, outsz - 1);
		out[outsz - 1] = '\0';
		return 0;
	}
	return -1;
}

/*
 * Create SPANK_TMP_DIR/{hookname}_log as a marker that this hook executed.
 * The file content is irrelevant; existence is the signal.
 * SPANK_TMP_DIR is baked in at compile time by spank_plugin via spank_tmp.
 */
static void _write_hook_marker(const char *func)
{
	char path[MAX_PATH + 64];
	int fd;

	snprintf(path, sizeof(path), SPANK_TMP_DIR "/%s_log", func);
	if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
		slurm_error("%s: unable to create %s: %m",
			    TOSTRING(PLUGIN_NAME), path);
	else
		close(fd);
}

/*
 * Failure injection: check whether this callback is the targeted one.
 * Returns -ESPANK_ERROR when targeted, ESPANK_SUCCESS otherwise.
 */
static int _fail_if_targeted(spank_t sp, const char *func)
{
	char target_func[256] = {0};
	char target_ctxt[256] = {0};
	char mode[32] = {0};
	uint32_t jobid = 0;

	spank_get_item(sp, S_JOB_ID, &jobid);

	/* Both must be set to arm the failure injection path. */
	if (_get_env(sp, "SPANK_FAIL_TEST_FUNC", target_func,
		     sizeof(target_func)) ||
	    _get_env(sp, "SPANK_FAIL_TEST_CTXT", target_ctxt,
		     sizeof(target_ctxt)))
		return ESPANK_SUCCESS;

	if (strcmp(target_func, func) || strcmp(target_ctxt, current_context())) {
		pthread_mutex_lock(&log_mutex);
		slurm_spank_log(
			"[Job: %u] Looking for (%s,%s) but found "
			"(%s,%s). Continuing...",
			jobid, target_func, target_ctxt, func,
			current_context());
		pthread_mutex_unlock(&log_mutex);
		return ESPANK_SUCCESS;
	}

	/* This is the targeted callback. */
	if (_get_env(sp, "SPANK_FAIL_TEST_MODE", mode, sizeof(mode)) == 0) {
		if (!strcasecmp(mode, "job"))
			slurm_spank_init_failure_mode = ESPANK_JOB_FAILURE;
		else if (!strcasecmp(mode, "node"))
			slurm_spank_init_failure_mode = ESPANK_NODE_FAILURE;
	}

	pthread_mutex_lock(&log_mutex);
	slurm_spank_log("[Job: %u] Found (%s,%s)", jobid, target_func,
			target_ctxt);
	pthread_mutex_unlock(&log_mutex);
	return -ESPANK_ERROR;
}

#define ENTRYPOINT(FUNC)                                                      \
	extern int FUNC(spank_t sp, int ac, char **av)                        \
	{                                                                     \
		/* Create the log file if configured */                       \
		char _enabled[4] = {0};                                       \
		if (_get_env(sp, "SPANK_HOOK_CREATE_FILE",                    \
			     _enabled, sizeof(_enabled)) == 0)                \
			_write_hook_marker(__func__);                         \
		                                                              \
		/* Return failure if configured */                            \
		return _fail_if_targeted(sp, __func__);                       \
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
