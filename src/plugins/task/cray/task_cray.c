/*****************************************************************************\
 *  task_cray.c - Library for task pre-launch and post_termination functions
 *	on a cray system
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
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

#include "config.h"

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_NUMA
#  include <numa.h>
#endif

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#ifdef HAVE_NATIVE_CRAY
#  include "alpscomm_cn.h"
#endif

static uint64_t debug_flags = 0;

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "task CRAY plugin";
const char plugin_type[]        = "task/cray";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

#ifdef HAVE_NATIVE_CRAY
#ifdef HAVE_NUMA
// TODO: Remove this prototype once the prototype appears in numa.h.
unsigned int numa_bitmask_weight(const struct bitmask *bmp);
#endif

static void _alpsc_debug(const char *file, int line, const char *func,
			 int rc, int expected_rc, const char *alpsc_func,
			 char *err_msg);
static int _make_status_file(stepd_step_rec_t *job);
static int _check_status_file(stepd_step_rec_t *job,
			      stepd_step_task_info_t *task);
static int _get_numa_nodes(char *path, int *cnt, int **numa_array);
static int _get_cpu_masks(int num_numa_nodes, int32_t *numa_array,
			  cpu_set_t **cpuMasks);

static int _update_num_steps(int val);
static int _step_prologue(void);
static int _step_epilogue(void);
static int track_status = 1;

// A directory on the compute node where temporary files will be kept
#define TASK_CRAY_RUN_DIR   "/var/run/task_cray"

// The spool directory used by libalpslli
// If it doesn't exist, skip exit status recording
#define LLI_SPOOL_DIR	    "/var/opt/cray/alps/spool"

// Filename to write status information to
// This file consists of job->node_tasks + 1 bytes. Each byte will
// be either 1 or 0, indicating that that particular event has occured.
// The first byte indicates the starting LLI message, and the next bytes
// indicate the exiting LLI messages for each task
#define LLI_STATUS_FILE	    LLI_SPOOL_DIR"/status%"PRIu64

// Size of buffer which is guaranteed to hold an LLI_STATUS_FILE
#define LLI_STATUS_FILE_BUF_SIZE    128

// Offset within status file to write to, different for each task
#define LLI_STATUS_OFFS_ENV "ALPS_LLI_STATUS_OFFSET"

// Application rank environment variable for PMI
#define ALPS_APP_PE_ENV "ALPS_APP_PE"

// Environment variable telling PMI not to fork
#define PMI_NO_FORK_ENV "PMI_NO_FORK"

// Environment variable providing the apid using a common name
#define ALPS_APP_ID_ENV "ALPS_APP_ID"

// File containing the number of currently running Slurm steps
#define NUM_STEPS_FILE	TASK_CRAY_RUN_DIR"/slurm_num_steps"

#define _ALPSC_DEBUG(f) _alpsc_debug(THIS_FILE, __LINE__, __func__, \
				     rc, 1, f, err_msg);
#define CRAY_ERR(fmt, ...) error("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				    __func__, ##__VA_ARGS__);
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	debug("%s loaded.", plugin_name);

	char *task_plugin = slurm_get_task_plugin();
	char *task_cgroup = strstr(task_plugin, "cgroup");
	char *task_cray = strstr(task_plugin, "cray");

	if (!task_cgroup || !task_cray || (task_cgroup < task_cray)) {
		fatal("task/cgroup must be used with, and listed after, "
		      "task/cray in TaskPlugin");
	}

	xfree(task_plugin);

#ifdef HAVE_NATIVE_CRAY
	int rc;
	struct stat st;

	debug_flags = slurm_get_debug_flags();

	// Create the run directory
	errno = 0;
	rc = mkdir(TASK_CRAY_RUN_DIR, 0755);
	if ((rc == -1) && (errno != EEXIST)) {
		CRAY_ERR("Couldn't create %s: %m", TASK_CRAY_RUN_DIR);
		return SLURM_ERROR;
	}

	// Determine whether to track app status with LLI
	rc = stat(LLI_SPOOL_DIR, &st);
	if (rc == -1) {
		debug("stat %s failed, disabling exit status tracking: %m",
			LLI_SPOOL_DIR);
		track_status = 0;
	} else {
		track_status = 1;
	}
#endif

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini (void)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_batch_request()
 */
extern int task_p_slurmd_batch_request (batch_job_launch_msg_t *req)
{
	debug("%s: %u", __func__, req->job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_launch_request()
 */
extern int task_p_slurmd_launch_request (launch_tasks_request_msg_t *req,
					 uint32_t node_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_reserve_resources()
 */
extern int task_p_slurmd_reserve_resources (launch_tasks_request_msg_t *req,
					    uint32_t node_id)
{
	debug("%s: %u %u", __func__, req->job_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_suspend_job()
 */
extern int task_p_slurmd_suspend_job (uint32_t job_id)
{
	DEF_TIMERS;
	START_TIMER;
	debug("task_p_slurmd_suspend_job: %u", job_id);

#ifdef HAVE_NATIVE_CRAY
	_step_epilogue();
#endif
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_resume_job()
 */
extern int task_p_slurmd_resume_job (uint32_t job_id)
{
	DEF_TIMERS;
	START_TIMER;
	debug("task_p_slurmd_resume_job: %u", job_id);

#ifdef HAVE_NATIVE_CRAY
	_step_prologue();
#endif
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_release_resources()
 */
extern int task_p_slurmd_release_resources (uint32_t job_id)
{
	debug("task_p_slurmd_release_resources: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_p_pre_setuid (stepd_step_rec_t *job)
{
	DEF_TIMERS;
	START_TIMER;
	debug("task_p_pre_setuid: %u.%u",
	      job->jobid, job->stepid);

#ifdef HAVE_NATIVE_CRAY
	if (!job->batch)
		_step_prologue();
#endif
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_p_pre_launch (stepd_step_rec_t *job)
{
#ifdef HAVE_NATIVE_CRAY
	int rc;
	uint64_t apid;
	DEF_TIMERS;

	START_TIMER;
	apid = SLURM_ID_HASH(job->jobid, job->stepid);
	debug2("task_p_pre_launch: %u.%u, apid %"PRIu64", task %d",
	       job->jobid, job->stepid, apid, job->envtp->procid);

	/*
	 * Send the rank to the application's PMI layer via an environment
	 * variable.
	 */
	rc = env_array_overwrite_fmt(&job->env, ALPS_APP_PE_ENV,
				     "%d", job->envtp->procid);
	if (rc == 0) {
		CRAY_ERR("Failed to set env variable %s", ALPS_APP_PE_ENV);
		return SLURM_ERROR;
	}

	/*
	 * Set the PMI_NO_FORK environment variable.
	 */
	rc = env_array_overwrite(&job->env, PMI_NO_FORK_ENV, "1");
	if (rc == 0) {
		CRAY_ERR("Failed to set env variable %s", PMI_NO_FORK_ENV);
		return SLURM_ERROR;
	}

	/*
	 *  Notify the task which offset to use
	 */
	rc = env_array_overwrite_fmt(&job->env, LLI_STATUS_OFFS_ENV,
				     "%d", job->envtp->localid + 1);
	if (rc == 0) {
		CRAY_ERR("Failed to set env variable %s",
			 LLI_STATUS_OFFS_ENV);
		return SLURM_ERROR;
	}

	/*
	 * Set the ALPS_APP_ID environment variable for use by
	 * Cray tools.
	 */
	rc = env_array_overwrite_fmt(&job->env, ALPS_APP_ID_ENV, "%"PRIu64,
				     apid);
	if (rc == 0) {
		CRAY_ERR("Failed to set env variable %s",
			 ALPS_APP_ID_ENV);
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch_priv() is called prior to exec of application task.
 * in privileged mode, just after slurm_spank_task_init_privileged
 */
extern int task_p_pre_launch_priv(stepd_step_rec_t *job, pid_t pid)
{
	int rc = SLURM_SUCCESS;
	DEF_TIMERS;

	START_TIMER;

#ifdef HAVE_NATIVE_CRAY
	debug("task_p_pre_launch_priv: %u.%u",
	      job->jobid, job->stepid);

	if (track_status) {
		rc = _make_status_file(job);
	}
#endif
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
	return rc;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term (stepd_step_rec_t *job,
			     stepd_step_task_info_t *task)
{
	int rc = SLURM_SUCCESS;
	DEF_TIMERS;

	START_TIMER;

#ifdef HAVE_NATIVE_CRAY
	debug("task_p_post_term: %u.%u, task %d",
	      job->jobid, job->stepid, task->id);

	if (track_status) {
		rc = _check_status_file(job, task);
	}
#endif
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
	return rc;
}

/*
 * task_p_post_step() is called after termination of the step
 * (all the tasks)
 */
extern int task_p_post_step (stepd_step_rec_t *job)
{
#ifdef HAVE_NATIVE_CRAY
	char llifile[LLI_STATUS_FILE_BUF_SIZE];
	int rc, cnt;
	char *err_msg = NULL, path[PATH_MAX];
	int32_t *numa_nodes;
	cpu_set_t *cpuMasks;
	DEF_TIMERS;

	START_TIMER;

	if (track_status) {
		uint64_t apid = SLURM_ID_HASH(job->jobid, job->stepid);
		// Get the lli file name
		snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE, apid);

		// Unlink the file
		errno = 0;
		rc = unlink(llifile);
		if ((rc == -1) && (errno != ENOENT)) {
			CRAY_ERR("unlink(%s) failed: %m", llifile);
		} else if (rc == 0) {
			debug("Unlinked %s", llifile);
		}

		// Unlink the backwards compatibility symlink
		if (apid != SLURM_ID_HASH_LEGACY(apid)) {
			snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE,
				 SLURM_ID_HASH_LEGACY(apid));
			rc = unlink(llifile);
			if ((rc == -1) && (errno != ENOENT)) {
				CRAY_ERR("unlink(%s) failed: %m", llifile);
			} else if (rc == 0) {
				debug("Unlinked %s", llifile);
			}
		}
	}

	/*
	 * Compact Memory
	 *
	 * Determine which NUMA nodes and CPUS an application is using.  It will
	 * be used to compact the memory.
	 *
	 * You'll find the information in the following location.
	 * For a normal job step:
	 * /dev/cpuset/slurm/uid_<uid>/job_<jobID>/step_<stepID>/
	 *
	 * For a batch job step (only on the head node and only for batch jobs):
	 * /dev/cpuset/slurm/uid_<uid>/job_<jobID>/step_batch/
	 *
	 * NUMA node: mems (or cpuset.mems)
	 */
	if (job->stepid == SLURM_BATCH_SCRIPT) {
		// Batch Job Step
		rc = snprintf(path, sizeof(path),
			      "/dev/cpuset/slurm/uid_%d/job_%"
			      PRIu32 "/step_batch", job->uid, job->jobid);
		if (rc < 0) {
			CRAY_ERR("snprintf failed. Return code: %d", rc);
			return SLURM_ERROR;
		}
	} else if (job->stepid == SLURM_EXTERN_CONT) {
		// Container for PAM to use for externally launched processes
		rc = snprintf(path, sizeof(path),
			      "/dev/cpuset/slurm/uid_%d/job_%"
			      PRIu32 "/step_extern", job->uid, job->jobid);
		if (rc < 0) {
			CRAY_ERR("snprintf failed. Return code: %d", rc);
			return SLURM_ERROR;
		}
	} else {
		// Normal Job Step

		/* Only run epilogue on non-batch steps */
		_step_epilogue();

		rc = snprintf(path, sizeof(path),
			      "/dev/cpuset/slurm/uid_%d/job_%"
			      PRIu32 "/step_%" PRIu32,
			      job->uid, job->jobid, job->stepid);
		if (rc < 0) {
			CRAY_ERR("snprintf failed. Return code: %d", rc);
			return SLURM_ERROR;
		}
	}

	rc = _get_numa_nodes(path, &cnt, &numa_nodes);
	if (rc < 0) {
		/* Failure common due to race condition in releasing cgroups */
		debug("%s: _get_numa_nodes failed. Return code: %d",
		      __func__, rc);
		return SLURM_ERROR;
	}

	rc = _get_cpu_masks(cnt, numa_nodes, &cpuMasks);
	if (rc < 0) {
		CRAY_ERR("_get_cpu_masks failed. Return code: %d", rc);
		xfree(numa_nodes);
		return SLURM_ERROR;
	}

	/*
	 * Compact Memory
	 * The last argument which is a path to the cpuset directory has to be
	 * NULL because the CPUSET directory has already been cleaned up.
	 */
	rc = alpsc_compact_mem(&err_msg, cnt, numa_nodes, cpuMasks, NULL);
	_ALPSC_DEBUG("alpsc_compact_mem");

	xfree(numa_nodes);
	xfree(cpuMasks);

	if (rc != 1)
		return SLURM_ERROR;
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);
#endif
	return SLURM_SUCCESS;
}

#ifdef HAVE_NATIVE_CRAY

/*
 * Print the results of an alpscomm call
 */
static void _alpsc_debug(const char *file, int line, const char *func,
			 int rc, int expected_rc, const char *alpsc_func,
			 char *err_msg)
{
	if (rc != expected_rc) {
		error("(%s: %d: %s) %s failed: %s", file, line, func,
		      alpsc_func,
		      err_msg ? err_msg : "No error message present");
	} else if (err_msg) {
		info("%s: %s", alpsc_func, err_msg);
	} else if (debug_flags & DEBUG_FLAG_TASK) {
		debug("Called %s", alpsc_func);
	}
	free(err_msg);
}

/*
 * If it wasn't created already, make the LLI_STATUS_FILE with given owner
 * and group, permissions 644, with given size
 */
static int _make_status_file(stepd_step_rec_t *job)
{
	char llifile[LLI_STATUS_FILE_BUF_SIZE];
	char oldllifile[LLI_STATUS_FILE_BUF_SIZE];
	int rv, fd;
	uint64_t apid = SLURM_ID_HASH(job->jobid, job->stepid);

	// Get the lli file name
	snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE, apid);

	// Make the file
	errno = 0;
	fd = open(llifile, O_CREAT|O_EXCL|O_WRONLY, 0644);
	if (fd == -1) {
		// Another task_p_pre_launch_priv already created it, ignore
		if (errno == EEXIST) {
			return SLURM_SUCCESS;
		}
		CRAY_ERR("creat(%s) failed: %m", llifile);
		return SLURM_ERROR;
	}

	// Resize it
	rv = ftruncate(fd, job->node_tasks + 1);
	if (rv == -1) {
		CRAY_ERR("ftruncate(%s) failed: %m", llifile);
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_ERROR;
	}

	// Change owner/group so app can write to it
	rv = fchown(fd, job->uid, job->gid);
	if (rv == -1) {
		CRAY_ERR("chown(%s) failed: %m", llifile);
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_ERROR;
	}
	debug("Created file %s", llifile);

	TEMP_FAILURE_RETRY(close(fd));

	// Create a backwards compatibility link
	if (apid != SLURM_ID_HASH_LEGACY(apid)) {
		snprintf(oldllifile, sizeof(oldllifile), LLI_STATUS_FILE,
			 SLURM_ID_HASH_LEGACY(apid));
		rv = symlink(llifile, oldllifile);
		if (rv == -1) {
			CRAY_ERR("symlink(%s, %s) failed: %m",
				 llifile, oldllifile);
			return SLURM_ERROR;
		}
	}
	return SLURM_SUCCESS;
}

/*
 * Check the status file for the exit of the given local task id
 * and terminate the job step if an improper exit is found
 */
static int _check_status_file(stepd_step_rec_t *job,
			      stepd_step_task_info_t *task)
{
	char llifile[LLI_STATUS_FILE_BUF_SIZE];
	char status;
	int rv, fd;

	// We only need to special case termination with exit(0)
	// srun already handles abnormal exit conditions fine
	if (!WIFEXITED(task->estatus) || (WEXITSTATUS(task->estatus) != 0))
		return SLURM_SUCCESS;

	// Get the lli file name
	snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE,
		 SLURM_ID_HASH(job->jobid, job->stepid));

	// Open the lli file.
	fd = open(llifile, O_RDONLY);
	if (fd == -1) {
		// There's a timing issue for large jobs; this file could
		// already be cleaned up by the time we get here.
		// However, this is during a normal cleanup so no big deal.
		debug("open(%s) failed: %m", llifile);
		return SLURM_SUCCESS;
	}

	// Read the first byte (indicates starting)
	rv = read(fd, &status, sizeof(status));
	if (rv == -1) {
		CRAY_ERR("read failed: %m");
		return SLURM_ERROR;
	}

	// If the first byte is 0, we either aren't an MPI app or
	// it didn't make it past pmi_init, in any case, return success
	if (status == 0) {
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_SUCCESS;
	}

	// Seek to the correct offset
	rv = lseek(fd, task->id + 1, SEEK_SET);
	if (rv == -1) {
		CRAY_ERR("lseek failed: %m");
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_ERROR;
	}

	// Read the exiting byte
	rv = read(fd, &status, sizeof(status));
	TEMP_FAILURE_RETRY(close(fd));
	if (rv == -1) {
		CRAY_ERR("read failed: %m");
		return SLURM_SUCCESS;
	}

	// Check the result
	if (status == 0) {
		if (task->killed_by_cmd) {
			// We've been killed by request. User already knows
			return SLURM_SUCCESS;
		}

		verbose("step %u.%u task %u exited without calling "
			"PMI_Finalize()",
			job->jobid, job->stepid, task->gtid);
	}
	return SLURM_SUCCESS;
}

/*
 * Function: _get_numa_nodes
 * Description:
 *  Returns a count of the NUMA nodes that the application is running on.
 *
 *  Returns an array of NUMA nodes that the application is running on.
 *
 *
 *  IN char* path -- The path to the directory containing the files containing
 *                   information about NUMA nodes.
 *
 *  OUT *cnt -- The number of NUMA nodes in the array
 *  OUT **numa_array -- An integer array containing the NUMA nodes.
 *                      This array must be xfreed by the caller.
 *
 * RETURN
 *  0 on success and -1 on failure.
 */
static int _get_numa_nodes(char *path, int *cnt, int32_t **numa_array)
{
	bool cpuset_prefix_set = true;
	char *cpuset_prefix = "cpuset.";
	struct bitmask *bm;
	int i, index, rc = 0;
	int lsz;
	size_t sz;
	char buffer[PATH_MAX];
	FILE *f = NULL;
	char *lin = NULL;

again:
	rc = snprintf(buffer, sizeof(buffer), "%s/%s%s", path, cpuset_prefix,
		      "mems");
	if (rc < 0)
		CRAY_ERR("snprintf failed. Return code: %d", rc);

	f = fopen(buffer, "r");
	if (f == NULL) {
		if (cpuset_prefix_set) {
			cpuset_prefix_set = false;
			cpuset_prefix = "";
			goto again;
		}
		/* Failure common due to race condition in releasing cgroups */
		debug("%s: Failed to open file %s: %m", __func__, buffer);
		return SLURM_ERROR;
	}

	lsz = getline(&lin, &sz, f);
	if (lsz > 0) {
		if (lin[strlen(lin) - 1] == '\n') {
			lin[strlen(lin) - 1] = '\0';
		}
		bm = numa_parse_nodestring(lin);
		if (bm == NULL) {
			CRAY_ERR("Error numa_parse_nodestring:"
				 " Invalid node string: %s", lin);
			free(lin);
			return SLURM_ERROR;
		}
	} else {
		debug("%s: Reading %s failed", __func__, buffer);
		return SLURM_ERROR;
	}
	free(lin);

	*cnt = numa_bitmask_weight(bm);
	if (*cnt == 0) {
		CRAY_ERR("No NUMA Nodes found");
		return -1;
	}

	if (debug_flags & DEBUG_FLAG_TASK) {
		info("Bitmask %#lx size: %lu sizeof(*(bm->maskp)): %zu"
		     " weight: %u",
		     *(bm->maskp), bm->size, sizeof(*(bm->maskp)), *cnt);
	}

	*numa_array = xmalloc(*cnt * sizeof(int32_t));

	index = 0;
	for (i = 0; i < bm->size; i++) {
		if (*(bm->maskp) & ((long unsigned) 1 << i)) {
			if (debug_flags & DEBUG_FLAG_TASK) {
				info("(%s: %d: %s) NUMA Node %d is present",
				     THIS_FILE,	__LINE__, __func__, i);
			}
			(*numa_array)[index++] = i;
		}
	}

	numa_free_nodemask(bm);

	return 0;
}

/*
 * Function: _get_cpu_masks
 * Description:
 *
 *  Returns cpuMasks which contains an array of a cpu_set_t cpumask one per
 *  NUMA node id within the numaNodes array; the cpumask identifies
 *  which CPUs are within that NUMA node.
 *
 *  It does the following.
 *  0.  Uses the cpuset.mems file to determine the total number of Numa Nodes
 *      and their individual index numbers.
 *  1.  Uses numa_node_to_cpus to get the bitmask of CPUs for each Numa Node.
 *  2.  Obtains the bitmask of CPUs for the cpuset from the cpuset.cpus file.
 *  3.  Bitwise-ANDs the bitmasks from steps #1 and #2 to obtain the CPUs
 *      allowed per Numa Node bitmask.
 *
 *  IN int num_numa_nodes -- Number of NUMA nodes in numa_array
 *  IN int32_t *numa_array -- Array of NUMA nodes length num_numa_nodes
 *  OUT cpu_set_t **cpuMasks -- An array of cpu_set_t's one per NUMA node
 *                              The caller must free *cpuMasks via xfree().
 * RETURN
 *  0 on success and -1 on failure.
 */
#define NUM_INTS_TO_HOLD_ALL_CPUS				\
	(numa_all_cpus_ptr->size / (sizeof(unsigned long) * 8))
static int _get_cpu_masks(int num_numa_nodes, int32_t *numa_array,
			  cpu_set_t **cpuMasks) {

	struct bitmask **remaining_numa_node_cpus = NULL, *collective;
	unsigned long **numa_node_cpus = NULL;
	int i, j, at_least_one_cpu = 0, rc = 0;
	cpu_set_t *cpusetptr;
	char *bitmask_str = NULL;

	if (numa_available()) {
		CRAY_ERR("Libnuma not available");
		return -1;
	}

	/*
	 * numa_node_cpus: The CPUs available to the NUMA node.
	 * numa_all_cpus_ptr: all CPUs on which the calling task may execute.
	 * remaining_numa_node_cpus: Bitwise-AND of the above two to get all of
	 *                           the CPUs that the task can run on in this
	 *                           NUMA node.
	 * collective: Collects all of the CPUs as a precaution.
	 */
	remaining_numa_node_cpus = xmalloc(num_numa_nodes *
					   sizeof(struct bitmask *));
	collective = numa_allocate_cpumask();
	numa_node_cpus = xmalloc(num_numa_nodes * sizeof(unsigned long*));
	for (i = 0; i < num_numa_nodes; i++) {
		remaining_numa_node_cpus[i] = numa_allocate_cpumask();
		numa_node_cpus[i] = xmalloc(sizeof(unsigned long) *
					    NUM_INTS_TO_HOLD_ALL_CPUS);
		rc = numa_node_to_cpus(numa_array[i], numa_node_cpus[i],
				       NUM_INTS_TO_HOLD_ALL_CPUS);
		if (rc) {
			CRAY_ERR("numa_node_to_cpus failed: Return code %d",
				 rc);
		}
		for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
			(remaining_numa_node_cpus[i]->maskp[j]) =
				(numa_node_cpus[i][j]) &
				(numa_all_cpus_ptr->maskp[j]);
			collective->maskp[j] |=
				(remaining_numa_node_cpus[i]->maskp[j]);
		}
	}

	/*
	 * Ensure that we have not masked off all of the CPUs.
	 * If we have, just re-enable them all.  Better to clear them all than
	 * none of them.
	 */
	for (j = 0; j < collective->size; j++) {
		if (numa_bitmask_isbitset(collective, j)) {
			at_least_one_cpu = 1;
		}
	}

	if (!at_least_one_cpu) {
		for (i = 0; i < num_numa_nodes; i++) {
			for (j = 0; j <
				     (remaining_numa_node_cpus[i]->size /
				      (sizeof(unsigned long) * 8));
			     j++) {
				(remaining_numa_node_cpus[i]->maskp[j]) =
					(numa_all_cpus_ptr->maskp[j]);
			}
		}
	}

	if (debug_flags & DEBUG_FLAG_TASK) {
		bitmask_str = NULL;
		for (i = 0; i < num_numa_nodes; i++) {
			for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
				xstrfmtcat(bitmask_str, "%6lx ",
					   numa_node_cpus[i][j]);
			}
		}
		info("%sBitmask: Allowed CPUs for NUMA Node", bitmask_str);
		xfree(bitmask_str);
		bitmask_str = NULL;

		for (i = 0; i < num_numa_nodes; i++) {
			for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
				xstrfmtcat(bitmask_str, "%6lx ",
					  numa_all_cpus_ptr->maskp[j]);
			}
		}
		info("%sBitmask: Allowed CPUs for cpuset", bitmask_str);
		xfree(bitmask_str);
		bitmask_str = NULL;

		for (i = 0; i < num_numa_nodes; i++) {
			for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
				xstrfmtcat(bitmask_str, "%6lx ",
					   remaining_numa_node_cpus[i]->
					   maskp[j]);
			}
		}
		info("%sBitmask: Allowed CPUs between cpuset and NUMA Node",
		     bitmask_str);
		xfree(bitmask_str);
	}


	// Convert bitmasks to cpu_set_t types
	cpusetptr = xmalloc(num_numa_nodes * sizeof(cpu_set_t));

	for (i = 0; i < num_numa_nodes; i++) {
		CPU_ZERO(&cpusetptr[i]);
		for (j = 0; j < remaining_numa_node_cpus[i]->size; j++) {
			if (numa_bitmask_isbitset(remaining_numa_node_cpus[i],
						  j)) {
				CPU_SET(j, &cpusetptr[i]);
			}
		}
		if (debug_flags & DEBUG_FLAG_TASK) {
			info("CPU_COUNT() of set: %d",
			     CPU_COUNT(&cpusetptr[i]));
		}
	}

	*cpuMasks = cpusetptr;

	// Freeing Everything
	numa_free_cpumask(collective);
	for (i = 0; i < num_numa_nodes; i++) {
		xfree(numa_node_cpus[i]);
		numa_free_cpumask(remaining_numa_node_cpus[i]);
	}
	xfree(numa_node_cpus);
	xfree(numa_node_cpus);
	xfree(remaining_numa_node_cpus);

	return 0;
}

/*
 * Update the number of running steps on the node
 * Set val to 1 to increment and -1 to decrement the value
 * Returns the new value, or -1 on error
 */
static int _update_num_steps(int val)
{
	int rc, fd, num_steps = 0;
	ssize_t size;
	off_t offset;
	struct flock lock;

	// Sanity check the argument
	if (val != 1 && val != -1) {
		CRAY_ERR("invalid val %d", val);
		return -1;
	}

	// Open the file
	fd = open(NUM_STEPS_FILE, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		CRAY_ERR("open failed: %m");
		return -1;
	}

	// Exclusive lock on the first byte of the file
	// Automatically released when the file descriptor is closed
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = sizeof(int);
	rc = fcntl(fd, F_SETLKW, &lock);
	if (rc == -1) {
		CRAY_ERR("fcntl failed: %m");
		TEMP_FAILURE_RETRY(close(fd));
		return -1;
	}

	// Read the value
	size = read(fd, &num_steps, sizeof(int));
	if (size == -1) {
		CRAY_ERR("read failed: %m");
		TEMP_FAILURE_RETRY(close(fd));
		return -1;
	} else if (size == 0) {
		// Value doesn't exist, must be the first step
		num_steps = 0;
	}

	// Increment or decrement and check result
	num_steps += val;
	if (num_steps < 0) {
		CRAY_ERR("Invalid step count (%d) on the node", num_steps);
		TEMP_FAILURE_RETRY(close(fd));
		return 0;
	}

	// Write the new value
	offset = lseek(fd, 0, SEEK_SET);
	if (offset == -1) {
		CRAY_ERR("fseek failed: %m");
		TEMP_FAILURE_RETRY(close(fd));
		return -1;
	}
	size = write(fd, &num_steps, sizeof(int));
	if (size < sizeof(int)) {
		CRAY_ERR("write failed: %m");
		TEMP_FAILURE_RETRY(close(fd));
		return -1;
	}
	if (debug_flags & DEBUG_FLAG_TASK) {
		debug("Wrote %d steps to %s", num_steps, NUM_STEPS_FILE);
	}

	TEMP_FAILURE_RETRY(close(fd));
	return num_steps;
}

/*
 * Runs Cray-specific step prologue commands
 * Returns SLURM_ERROR or SLURM_SUCCESS
 */
static int _step_prologue(void)
{
	int num_steps, rc;
	char *err_msg;

	num_steps = _update_num_steps(1);
	if (num_steps == -1) {
		return SLURM_ERROR;
	}

	rc = alpsc_node_app_prologue(&err_msg);
	_ALPSC_DEBUG("alpsc_node_app_prologue");
	if (rc != 1) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Runs Cray-specific step epilogue commands
 * Returns SLURM_ERROR or SLURM_SUCCESS
 */
static int _step_epilogue(void)
{
	int num_steps, rc;
	char *err_msg;

	// Note the step is done
	num_steps = _update_num_steps(-1);
	if (num_steps == -1) {
		return SLURM_ERROR;
	}

	// If we're the last step, run the app epilogue
	if (num_steps == 0) {
		rc = alpsc_node_app_epilogue(&err_msg);
		_ALPSC_DEBUG("alpsc_node_app_epilogue");
		if (rc != 1) {
			return SLURM_ERROR;
		}
	} else if (debug_flags & DEBUG_FLAG_TASK) {
		debug("Skipping epilogue, %d other steps running", num_steps);
	}
	return SLURM_SUCCESS;
}
#endif

/*
 * Keep track a of a pid.
 */
extern int task_p_add_pid (pid_t pid)
{
	return SLURM_SUCCESS;
}
