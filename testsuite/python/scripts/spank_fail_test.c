/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
 *****************************************************************************
 *  spank_fail_test.c - Generic SPANK plugin for testing failure-mode behavior.
 *
 *  Configuration is via environment variables:
 *
 *    SPANK_FAIL_TEST_FUNC -- SPANK callback name to fail in
 *                            (e.g. slurm_spank_init, slurm_spank_user_init).
 *    SPANK_FAIL_TEST_CTXT -- SPANK context in which to fail (local, remote,
 *                            allocator, slurmd, job_script).
 *    SPANK_FAIL_TEST_MODE -- optional. Affects only slurm_spank_init failures,
 *                            one of:
 *                              "node" -> slurm_spank_init_failure_mode =
 *                                          ESPANK_NODE_FAILURE
 *                              "job"  -> slurm_spank_init_failure_mode =
 *                                          ESPANK_JOB_FAILURE
 *                            Unset (the common case) leaves the failure_mode
 *                            at its default; the targeted callback still
 *                            returns -ESPANK_ERROR.
 *
 *  If SPANK_FAIL_TEST_FUNC and SPANK_FAIL_TEST_CTXT are both unset, the
 *  plugin is inert: every callback returns ESPANK_SUCCESS.
 *
 *  Every callback emits one of these messages:
 *      [Job: <id>] Found (<target_func>,<target_ctx>)
 *      [Job: <id>] Looking for (<target_func>,<target_ctx>) but found
 *                  (<current_func>,<current_ctx>). Continuing...
 *
 *  The matching callback returns -ESPANK_ERROR; non-matching callbacks return
 *  ESPANK_SUCCESS so the SPANK pipeline continues until the targeted
 *  callback is hit.
\*****************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <slurm/spank.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME spank_fail_test
#endif

SPANK_PLUGIN(PLUGIN_NAME, 1);

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
 * Read a SPANK_FAIL_TEST_* env var. In remote context the process env is
 * wiped, so spank_getenv() is the only path that sees what the submitter
 * sent over. In other contexts, fall back to getenv() on the process env.
 *
 * Returns 0 on success (out is populated), -1 if unset/empty.
 */
static int _get_test_var(spank_t sp, const char *name, char *out, size_t outsz)
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

#define ENTRYPOINT(FUNC)                                                      \
	extern int FUNC(spank_t sp, int ac, char **av)                        \
	{                                                                     \
		char target_func[256] = {0};                                  \
		char target_ctxt[256] = {0};                                  \
		char mode[32] = {0};                                          \
		uint32_t jobid = 0;                                           \
									      \
		spank_get_item(sp, S_JOB_ID, &jobid);                         \
									      \
		/* Both must be set to arm the plugin. */                     \
		if (_get_test_var(sp, "SPANK_FAIL_TEST_FUNC", target_func,    \
				  sizeof(target_func)) ||                     \
		    _get_test_var(sp, "SPANK_FAIL_TEST_CTXT", target_ctxt,    \
				  sizeof(target_ctxt)))                       \
			return ESPANK_SUCCESS;                                \
									      \
		if (strcmp(target_func, __func__) ||                          \
		    strcmp(target_ctxt, current_context())) {                 \
			pthread_mutex_lock(&log_mutex);                       \
			slurm_spank_log(                                      \
				"[Job: %u] Looking for (%s,%s) but found "    \
				"(%s,%s). Continuing...",                     \
				jobid, target_func, target_ctxt, __func__,    \
				current_context());                           \
			pthread_mutex_unlock(&log_mutex);                     \
			return ESPANK_SUCCESS;                                \
		}                                                             \
									      \
		/* This is the targeted callback. */                          \
		if (_get_test_var(sp, "SPANK_FAIL_TEST_MODE", mode,           \
				  sizeof(mode)) == 0) {                       \
			if (!strcasecmp(mode, "job"))                         \
				slurm_spank_init_failure_mode =               \
					ESPANK_JOB_FAILURE;                   \
			else if (!strcasecmp(mode, "node"))                   \
				slurm_spank_init_failure_mode =               \
					ESPANK_NODE_FAILURE;                  \
		}                                                             \
									      \
		pthread_mutex_lock(&log_mutex);                               \
		slurm_spank_log("[Job: %u] Found (%s,%s)", jobid, target_func,\
				target_ctxt);                                 \
		pthread_mutex_unlock(&log_mutex);                             \
		return -ESPANK_ERROR;                                         \
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
