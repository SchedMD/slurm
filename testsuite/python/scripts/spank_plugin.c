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
 *  JOB INFO LOGGING
 *    SPANK_HOOK_LOG_JOB_INFO -- boolean (any non-empty value enables). Logs what
 *                          spank_get_item() and slurm_load_job() report for each
 *                          task, appending to SPANK_TMP_DIR/spank_job_info.log.
 *                          All lines share one layout so a single regex parses:
 *                            caller=<func> source=<source> \
 *                            self_job_id=<id> self_step_id=<id> \
 *                            job_id=<id> array_job_id=<id> array_task_id=<id> \
 *                            [rc=<rc>]
 *                          Inert if unset.
 *
 *  All capabilities are independent: any combination may be active at
 *  the same time. In remote context, env vars are read via spank_getenv()
 *  because the process environment is wiped; in other contexts, getenv() is
 *  used on the client process environment.
\*****************************************************************************/
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <slurm/slurm.h>
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
#define MAX_LINE 1024

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
 * Append one line with a single write(2) on an O_APPEND fd: lines are shorter
 * than PIPE_BUF, so concurrent tasks can't interleave them, while stdio buffers
 * would be lost when the task exec's right after slurm_spank_task_init().
 */
static void _log_line(const char *path, const char *fmt, ...)
{
	char line[MAX_LINE];
	int len, fd;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	if (len < 0)
		return;
	if (len >= (int) sizeof(line))
		len = sizeof(line) - 1;

	if ((fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
		slurm_error("%s: unable to open %s: %m", plugin_name, path);
		return;
	}

	if (write(fd, line, len) != len)
		slurm_error("%s: unable to write to %s: %m", plugin_name, path);

	close(fd);
}

static void _log_job_info(spank_t sp, const char *path, const char *caller)
{
	/*
	 * slurm_load_job() can only be called from slurm_spank_task_init,
	 * once slurmstepd dropped its auth setuid lock.
	 */
	if (strcmp(caller, "slurm_spank_task_init") != 0)
		return;

	uint32_t step_id = NO_VAL, job_id = NO_VAL;
	uint32_t array_job_id = NO_VAL, array_task_id = NO_VAL;
	job_info_msg_t *job_info = NULL;
	uint32_t i;
	int rc;

	if (!(rc = spank_get_item(sp, S_JOB_STEPID, &step_id)) &&
	    !(rc = spank_get_item(sp, S_JOB_ID, &job_id)) &&
	    !(rc = spank_get_item(sp, S_JOB_ARRAY_ID, &array_job_id)))
		rc = spank_get_item(sp, S_JOB_ARRAY_TASK_ID, &array_task_id);

	if (rc) {
		_log_line(path,
			  "caller=%s source=spank_get_item_error self_job_id=%u self_step_id=%u job_id=%u array_job_id=%u array_task_id=%u rc=%d\n",
			  caller, job_id, step_id, job_id, array_job_id,
			  array_task_id, rc);
		return;
	}

	_log_line(path,
		  "caller=%s source=spank_get_item self_job_id=%u self_step_id=%u job_id=%u array_job_id=%u array_task_id=%u\n",
		  caller, job_id, step_id, job_id, array_job_id, array_task_id);

	/*
	 * This RPC deadlocks the task unless slurmstepd dropped its auth setuid
	 * lock before the SPANK stack (task.c:spank_user_task).
	 */
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
	slurm_step_id_t load_step_id = SLURM_STEP_ID_INITIALIZER;
	load_step_id.job_id = job_id;
	rc = slurm_load_job(&job_info, load_step_id, SHOW_DETAIL);
#else /* Ticket 13506 (!3137): Change API to accept slurm_step_id_t */
	rc = slurm_load_job(&job_info, job_id, SHOW_DETAIL);
#endif
	if (rc) {
		_log_line(path,
			  "caller=%s source=load_job_error self_job_id=%u self_step_id=%u job_id=%u array_job_id=%u array_task_id=%u rc=%d\n",
			  caller, job_id, step_id, job_id, array_job_id,
			  array_task_id, rc);
		return;
	}

	for (i = 0; i < job_info->record_count; i++) {
		slurm_job_info_t *job = job_info->job_array + i;

		_log_line(path,
			  "caller=%s source=load_job self_job_id=%u self_step_id=%u job_id=%u array_job_id=%u array_task_id=%u\n",
			  caller, job_id, step_id,
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(25, 11, 0)
			  job->step_id.job_id,
#else
			  job->job_id,
#endif
			  job->array_job_id, job->array_task_id);
	}

	slurm_free_job_info_msg(job_info);
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
		/* Log job info in the log file if configured */              \
		if (spank_context() == S_CTX_REMOTE) {                        \
			char _ji[4] = {0};                                    \
			if (_get_env(sp, "SPANK_HOOK_LOG_JOB_INFO", _ji,      \
				     sizeof(_ji)) == 0)                       \
				_log_job_info(sp,                             \
					      SPANK_TMP_DIR                   \
					      "/spank_job_info.log",          \
					      __func__);                      \
		}                                                             \
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
