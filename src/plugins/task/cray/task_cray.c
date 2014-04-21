/*****************************************************************************\
 *  task_cray.c - Library for task pre-launch and post_termination functions
 *	on a cray system
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/param.h>
#include <errno.h>
#include "limits.h"
#include <sched.h>

#ifdef HAVE_NUMA
#include <numa.h>
#endif

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#ifdef HAVE_NATIVE_CRAY
#include "alpscomm_cn.h"
#endif

// Filename to write status information to
// This file consists of job->node_tasks + 1 bytes. Each byte will
// be either 1 or 0, indicating that that particular event has occured.
// The first byte indicates the starting LLI message, and the next bytes
// indicate the exiting LLI messages for each task
#define LLI_STATUS_FILE	    "/var/opt/cray/alps/spool/status%"PRIu64

// Size of buffer which is guaranteed to hold an LLI_STATUS_FILE
#define LLI_STATUS_FILE_BUF_SIZE    128

// Offset within status file to write to, different for each task
#define LLI_STATUS_OFFS_ENV "ALPS_LLI_STATUS_OFFSET"
static uint32_t debug_flags = 0;

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "task CRAY plugin";
const char plugin_type[]        = "task/cray";
const uint32_t plugin_version   = 100;

#ifdef HAVE_NUMA
// TODO: Remove this prototype once the prototype appears in numa.h.
unsigned int numa_bitmask_weight(const struct bitmask *bmp);
#endif

#ifdef HAVE_NATIVE_CRAY
static int _get_numa_nodes(char *path, int *cnt, int **numa_array);
static int _get_cpu_masks(int num_numa_nodes, int32_t *numa_array,
			  cpu_set_t **cpuMasks);

static int terminated = 0;
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	verbose("%s loaded.", plugin_name);
	debug_flags = slurm_get_debug_flags();
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
extern int task_p_slurmd_batch_request (uint32_t job_id,
					batch_job_launch_msg_t *req)
{
	debug("task_p_slurmd_batch_request: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_launch_request()
 */
extern int task_p_slurmd_launch_request (uint32_t job_id,
					 launch_tasks_request_msg_t *req,
					 uint32_t node_id)
{
	debug("task_p_slurmd_launch_request: %u.%u %u",
	      job_id, req->job_step_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_reserve_resources()
 */
extern int task_p_slurmd_reserve_resources (uint32_t job_id,
					    launch_tasks_request_msg_t *req,
					    uint32_t node_id)
{
	debug("task_p_slurmd_reserve_resources: %u %u", job_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_suspend_job()
 */
extern int task_p_slurmd_suspend_job (uint32_t job_id)
{
	debug("task_p_slurmd_suspend_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_resume_job()
 */
extern int task_p_slurmd_resume_job (uint32_t job_id)
{
	debug("task_p_slurmd_resume_job: %u", job_id);
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
	debug("task_p_pre_setuid: %u.%u",
	      job->jobid, job->stepid);

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

	debug("task_p_pre_launch: %u.%u, task %d",
	      job->jobid, job->stepid, job->envtp->procid);
	/*
	 * Send the rank to the application's PMI layer via an environment
	 * variable.
	 */
	rc = env_array_overwrite_fmt(&job->env, "ALPS_APP_PE",
				     "%d", job->envtp->procid);
	if (rc == 0) {
		error("Failed to set env variable ALPS_APP_PE");
		return SLURM_ERROR;
	}

	/*
	 * Set the PMI_NO_FORK environment variable.
	 */
	rc = env_array_overwrite(&job->env,"PMI_NO_FORK", "1");
	if (rc == 0) {
		error("Failed to set env variable PMI_NO_FORK");
		return SLURM_ERROR;
	}

	// Notify the task which offset to use
	rc = env_array_overwrite_fmt(&job->env, LLI_STATUS_OFFS_ENV,
				     "%d", job->envtp->localid + 1);
	if (rc == 0) {
		error("%s: Failed to set %s", __func__, LLI_STATUS_OFFS_ENV);
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch_priv() is called prior to exec of application task.
 * in privileged mode, just after slurm_spank_task_init_privileged
 */
extern int task_p_pre_launch_priv (stepd_step_rec_t *job)
{
#ifdef HAVE_NATIVE_CRAY
	char llifile[LLI_STATUS_FILE_BUF_SIZE];
	int rv, fd;

	debug("task_p_pre_launch_priv: %u.%u",
	      job->jobid, job->stepid);

	// Get the lli file name
	snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE,
		 SLURM_ID_HASH(job->jobid, job->stepid));

	// Make the file
	errno = 0;
	fd = open(llifile, O_CREAT|O_EXCL|O_WRONLY, 0644);
	if (fd == -1) {
		// Another task_p_pre_launch_priv already created it, ignore
		if (errno == EEXIST) {
			return SLURM_SUCCESS;
		}
		error("%s: creat(%s) failed: %m", __func__, llifile);
		return SLURM_ERROR;
	}

	// Resize it to job->node_tasks + 1
	rv = ftruncate(fd, job->node_tasks + 1);
	if (rv == -1) {
		error("%s: ftruncate(%s) failed: %m", __func__, llifile);
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_ERROR;
	}

	// Change owner/group so app can write to it
	rv = fchown(fd, job->uid, job->gid);
	if (rv == -1) {
		error("%s: chown(%s) failed: %m", __func__, llifile);
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_ERROR;
	}
	info("Created file %s", llifile);

	TEMP_FAILURE_RETRY(close(fd));
#endif
	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term (stepd_step_rec_t *job,
			     stepd_step_task_info_t *task)
{
#ifdef HAVE_NATIVE_CRAY
	char llifile[LLI_STATUS_FILE_BUF_SIZE];
	char status;
	int rv, fd;
	char *reason;

	debug("task_p_post_term: %u.%u, task %d",
	      job->jobid, job->stepid, job->envtp->procid);

	// Get the lli file name
	snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE,
		 SLURM_ID_HASH(job->jobid, job->stepid));

	// Open the lli file.
	fd = open(llifile, O_RDONLY);
	if (fd == -1) {
		error("%s: open(%s) failed: %m", __func__, llifile);
		return SLURM_ERROR;
	}

	// Read the first byte (indicates starting)
	rv = read(fd, &status, sizeof(status));
	if (rv == -1) {
		error("%s: read failed: %m", __func__);
		return SLURM_ERROR;
	}

	// If the first byte is 0, we either aren't an MPI app or
	// it didn't make it past pmi_init, in any case, return success
	if (status == 0) {
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_SUCCESS;
	}

	// Seek to the correct offset (job->envtp->localid + 1)
	rv = lseek(fd, job->envtp->localid + 1, SEEK_SET);
	if (rv == -1) {
		error("%s: lseek failed: %m", __func__);
		TEMP_FAILURE_RETRY(close(fd));
		return SLURM_ERROR;
	}

	// Read the exiting byte
	rv = read(fd, &status, sizeof(status));
	TEMP_FAILURE_RETRY(close(fd));
	if (rv == -1) {
		error("%s: read failed: %m", __func__);
		return SLURM_SUCCESS;
	}

	// Check the result
	if (status == 0 && !terminated) {
		if (task->killed_by_cmd) {
			// We've been killed by request. User already knows
			return SLURM_SUCCESS;
		} else if (task->aborted) {
			reason = "aborted";
		} else if (WIFSIGNALED(task->estatus)) {
			reason = "signaled";
		} else {
			reason = "exited";
		}

		// Cancel the job step, since we didn't find the exiting msg
		error("Terminating job step %"PRIu32".%"PRIu32
			"; task %d exit code %d %s without notification",
			job->jobid, job->stepid, task->gtid,
			WEXITSTATUS(task->estatus), reason);
		terminated = 1;
		slurm_terminate_job_step(job->jobid, job->stepid);
	}

#endif
	return SLURM_SUCCESS;
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

	// Get the lli file name
	snprintf(llifile, sizeof(llifile), LLI_STATUS_FILE,
		 SLURM_ID_HASH(job->jobid, job->stepid));

	// Unlink the file
	errno = 0;
	rc = unlink(llifile);
	if (rc == -1 && errno != ENOENT) {
		error("%s: unlink(%s) failed: %m", __func__, llifile);
	} else if (rc == 0) {
		info("Unlinked %s", llifile);
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
	 * NUMA node: mems
	 * CPU Masks: cpus
	 */


	if ((job->stepid == NO_VAL) || (job->stepid == SLURM_BATCH_SCRIPT)) {
		// Batch Job Step
		rc = snprintf(path, sizeof(path),
			      "/dev/cpuset/slurm/uid_%d/job_%"
			      PRIu32 "/step_batch", job->uid, job->jobid);
		if (rc < 0) {
			error("(%s: %d: %s) snprintf failed. Return code: %d",
			      THIS_FILE, __LINE__, __FUNCTION__, rc);
			return SLURM_ERROR;
		}
	} else {
		// Normal Job Step
		rc = snprintf(path, sizeof(path),
			      "/dev/cpuset/slurm/uid_%d/job_%"
			      PRIu32 "/step_%" PRIu32,
			      job->uid, job->jobid, job->stepid);
		if (rc < 0) {
			error("(%s: %d: %s) snprintf failed. Return code: %d",
			      THIS_FILE, __LINE__, __FUNCTION__, rc);
			return SLURM_ERROR;
		}
	}

	rc = _get_numa_nodes(path, &cnt, &numa_nodes);
	if (rc < 0) {
		error("(%s: %d: %s) get_numa_nodes failed. Return code: %d",
		      THIS_FILE, __LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	rc = _get_cpu_masks(cnt, numa_nodes, &cpuMasks);
	if (rc < 0) {
		error("(%s: %d: %s) get_cpu_masks failed. Return code: %d",
		      THIS_FILE, __LINE__, __FUNCTION__, rc);
		return SLURM_ERROR;
	}

	/*
	 * Compact Memory
	 * The last argument which is a path to the cpuset directory has to be
	 * NULL because the CPUSET directory has already been cleaned up.
	 */
	rc = alpsc_compact_mem(&err_msg, cnt, numa_nodes, cpuMasks, NULL);

	xfree(numa_nodes);
	xfree(cpuMasks);

	if (rc != 1) {
		if (err_msg) {
			error("(%s: %d: %s) alpsc_compact_mem failed: %s",
			      THIS_FILE, __LINE__, __FUNCTION__, err_msg);
			free(err_msg);
		} else {
			error("(%s: %d: %s) alpsc_compact_mem failed:"
			      " No error message present.",
			      THIS_FILE, __LINE__, __FUNCTION__);
		}
		return SLURM_ERROR;
	}
	if (err_msg) {
		info("(%s: %d: %s) alpsc_compact_mem: %s", THIS_FILE, __LINE__,
		     __FUNCTION__, err_msg);
		free(err_msg);
	}
#endif
	return SLURM_SUCCESS;
}

#ifdef HAVE_NATIVE_CRAY

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
static int _get_numa_nodes(char *path, int *cnt, int32_t **numa_array) {
	struct bitmask *bm;
	int i, index, rc = 0;
	int lsz;
	size_t sz;
	char buffer[PATH_MAX];
	FILE *f = NULL;
	char *lin = NULL;

	rc = snprintf(buffer, sizeof(buffer), "%s/%s", path, "mems");
	if (rc < 0) {
		error("(%s: %d: %s) snprintf failed. Return code: %d",
		      THIS_FILE, __LINE__, __FUNCTION__, rc);
	}

	f = fopen(buffer, "r");
	if (f == NULL ) {
		error("Failed to open file %s: %m\n", buffer);
		return -1;
	}

	lsz = getline(&lin, &sz, f);
	if (lsz > 0) {
		if (lin[strlen(lin) - 1] == '\n') {
			lin[strlen(lin) - 1] = '\0';
		}
		bm = numa_parse_nodestring(lin);
		if (bm == NULL ) {
			error("(%s: %d: %s) Error numa_parse_nodestring:"
			      " Invalid node string: %s",
			      THIS_FILE, __LINE__, __FUNCTION__, lin);
			free(lin);
			return SLURM_ERROR;
		}
	} else {
		error("(%s: %d: %s) Reading %s failed.", THIS_FILE, __LINE__,
		      __FUNCTION__, buffer);
		return SLURM_ERROR;
	}
	free(lin);

	*cnt = numa_bitmask_weight(bm);
	if (*cnt == 0) {
		error("(%s: %d: %s)Error no NUMA Nodes found.",
		      THIS_FILE, __LINE__, __FUNCTION__);
		return -1;
	}

	if (debug_flags & DEBUG_FLAG_TASK) {
		info("Bitmask size: %lu\nSizeof(*(bm->maskp)):%zd\n"
		     "Bitmask %#lx\nBitmask weight(number of bits set): %u\n",
		     bm->size, sizeof(*(bm->maskp)), *(bm->maskp), *cnt);
	}

	*numa_array = xmalloc(*cnt * sizeof(int32_t));
	if (*numa_array == NULL ) {
		error("(%s: %d: %s)Error out of memory.\n", THIS_FILE, __LINE__,
		      __FUNCTION__);
		return -1;
	}

	index = 0;
	for (i = 0; i < bm->size; i++) {
		if (*(bm->maskp) & ((long unsigned) 1 << i)) {
			if (debug_flags & DEBUG_FLAG_TASK) {
				info("(%s: %d: %s)NUMA Node %d is present.\n",
				     THIS_FILE,	__LINE__, __FUNCTION__, i);
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

	if (numa_available()) {
		error("(%s: %d: %s) Libnuma not available", THIS_FILE,
		      __LINE__, __FUNCTION__);
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
			error("(%s: %d: %s) numa_node_to_cpus. Return code: %d",
			      THIS_FILE, __LINE__, __FUNCTION__, rc);
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
	for (j=0; j < collective->size; j++) {
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
		for (i =0; i < num_numa_nodes; i++) {
			for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
				info("%6lx", numa_node_cpus[i][j]);
			}
			info("|");
		}
		info("\t Bitmask: Allowed CPUs for NUMA Node\n");

		for (i =0; i < num_numa_nodes; i++) {
			for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
				info("%6lx", numa_all_cpus_ptr->maskp[j]);
			}
			info("|");
		}
		info("\t Bitmask: Allowed CPUs for for CPUSET\n");

		for (i =0; i < num_numa_nodes; i++) {
			for (j = 0; j < NUM_INTS_TO_HOLD_ALL_CPUS; j++) {
				info("%6lx",
				     remaining_numa_node_cpus[i]->maskp[j]);
			}
			info("|");
		}
		info("\t Bitmask: Allowed CPUs between CPUSet and NUMA Node\n");
	}


	// Convert bitmasks to cpu_set_t types
	cpusetptr = xmalloc(num_numa_nodes * sizeof(cpu_set_t));

	for (i=0; i < num_numa_nodes; i++) {
		CPU_ZERO(&cpusetptr[i]);
		for (j=0; j < remaining_numa_node_cpus[i]->size; j++) {
			if (numa_bitmask_isbitset(remaining_numa_node_cpus[i],
						  j)) {
				CPU_SET(j, &cpusetptr[i]);
			}
		}
		if (debug_flags & DEBUG_FLAG_TASK) {
			info("CPU_COUNT() of set:    %d\n",
			     CPU_COUNT(&cpusetptr[i]));
		}
	}

	*cpuMasks = cpusetptr;

	// Freeing Everything
	numa_free_cpumask(collective);
	for (i =0; i < num_numa_nodes; i++) {
		xfree(numa_node_cpus[i]);
		numa_free_cpumask(remaining_numa_node_cpus[i]);
	}
	xfree(numa_node_cpus);
	xfree(numa_node_cpus);
	xfree(remaining_numa_node_cpus);

	return 0;
}
#endif
