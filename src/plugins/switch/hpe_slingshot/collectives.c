/*****************************************************************************\
 *  collectives.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2023 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurm_xlator.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "switch_hpe_slingshot.h"
#include "rest.h"

#define CLEANUP_THREAD_PERIOD 30

static slingshot_rest_conn_t fm_conn;  /* Connection to fabric manager */

static bool collectives_enabled = false;

pthread_t cleanup_thread_id = 0;
pthread_cond_t cleanup_thread_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cleanup_thread_lock = PTHREAD_MUTEX_INITIALIZER;
bool cleanup_thread_shutdown = false;

static void *_cleanup_thread(void *data)
{
	struct timespec ts = {0, 0};
	json_object *respjson = NULL, *jobsjson = NULL, *jobjson = NULL;
	long status = 0;
	uint32_t job_id, arraylen;
	size_t path_len, cluster_name_len;
	job_record_t *job_ptr;
	slurmctld_lock_t job_read_lock = { .job = READ_LOCK };
	char *url = "/fabric/collectives/jobs/";

	path_len = strlen(url);
	cluster_name_len = strlen(slurm_conf.cluster_name);

	while (!cleanup_thread_shutdown) {
		slurm_mutex_lock(&cleanup_thread_lock);
		if (!cleanup_thread_shutdown) {
			ts.tv_sec = time(NULL) + CLEANUP_THREAD_PERIOD;
			slurm_cond_timedwait(&cleanup_thread_cond,
					     &cleanup_thread_lock, &ts);
		}
		slurm_mutex_unlock(&cleanup_thread_lock);

		json_object_put(respjson);
		if (!(respjson = slingshot_rest_get(&fm_conn, url, &status))) {
			error("GET %s to fabric manager for job failed: %ld",
			      url, status);
			continue; /* Try again next time around */
		} else {
			log_flag(SWITCH, "GET %s resp='%s'", url,
				 json_object_to_json_string(respjson));
		}
		json_object_object_get_ex(respjson, "documentLinks", &jobsjson);
		arraylen = json_object_array_length(jobsjson);

		for (int i = 0; i < arraylen; i++) {
			bool release = false;
			const char *jobstr;
			char *endptr = NULL;
			jobjson = json_object_array_get_idx(jobsjson, i);
			jobstr = json_object_get_string(jobjson) + path_len;

			if (xstrncmp(jobstr, slurm_conf.cluster_name,
				     cluster_name_len)) {
				log_flag(SWITCH, "Skipping fabric manager job '%s' because the cluster name doesn't match %s",
					jobstr, slurm_conf.cluster_name);
				continue;
			}

			/* Add 1 to skip the '-' after the cluster name */
			job_id = strtol(jobstr + cluster_name_len + 1, &endptr,
					10);
			if (endptr && (*endptr != '\0')) {
				log_flag(SWITCH, "Skipping fabric manager job '%s'",
					 jobstr);
				continue;
			}

			lock_slurmctld(job_read_lock);
			job_ptr = find_job_record(job_id);
			if (!job_ptr) {
				error("job %u isn't in slurmctld, removing from fabric manager",
				      job_id);
				release = true;
			} else if (!IS_JOB_RUNNING(job_ptr) &&
				   !IS_JOB_SUSPENDED(job_ptr)) {
				error("job %u isn't currently allocated resources, removing from fabric manager",
				      job_id);
				release = true;
			}
			unlock_slurmctld(job_read_lock);

			if (release)
				slingshot_release_collectives_job(job_id);
		}
	}

	debug("shutting down collectives cleanup thread");

	return NULL;
}

/*
 * Read any authentication files and connect to the fabric manager,
 * which implements a REST interface supporting Slingshot collectives
 */
extern bool slingshot_init_collectives(void)
{
	/* Enable Hardware Collectives only if fm_url is configured */
	if (!slingshot_config.fm_url)
		return true;

	if (running_in_slurmctld() &&
	    !xstrcasestr(slurm_conf.slurmctld_params, "enable_stepmgr")) {
		error("Hardware collectives enabled by setting SwitchParameters=fm_url but SlurmctldParameters=enable_stepmgr is not set.");
		return false;
	}

	if (!slingshot_rest_connection(&fm_conn,
				       slingshot_config.fm_url,
				       slingshot_config.fm_auth,
				       slingshot_config.fm_authdir,
				       SLINGSHOT_FM_AUTH_BASIC_USER,
				       SLINGSHOT_FM_AUTH_BASIC_PWD_FILE,
				       SLINGSHOT_FM_TIMEOUT,
				       SLINGSHOT_FM_CONNECT_TIMEOUT,
				       "Slingshot Fabric Manager"))
		goto err;

	if (running_in_slurmctld()) {
		slurm_mutex_lock(&cleanup_thread_lock);
		slurm_thread_create(&cleanup_thread_id, _cleanup_thread, NULL);
		slurm_mutex_unlock(&cleanup_thread_lock);
	}

	collectives_enabled = true;
	return true;

err:
	info("Slingshot collectives support disabled due to errors");
	slingshot_rest_destroy_connection(&fm_conn);
	collectives_enabled = false;
	return false;
}

/*
 * Close connection to fabric manager REST interface, free memory
 */
extern void slingshot_fini_collectives(void)
{
	if (running_in_slurmctld() && cleanup_thread_id) {
		cleanup_thread_shutdown = true;
		slurm_mutex_lock(&cleanup_thread_lock);
		slurm_cond_signal(&cleanup_thread_cond);
		slurm_mutex_unlock(&cleanup_thread_lock);

		slurm_thread_join(cleanup_thread_id);
	}

	slingshot_rest_destroy_connection(&fm_conn);
}

/*
 * Save jobID in slingshot_state.job_hwcoll[] array to indicate use of
 * hardware collectives (for cleanup time).  Return if jobID is already there.
 */
static void _save_hwcoll(uint32_t job_id)
{
	int freeslot = -1;

	for (int i = 0; i < slingshot_state.num_job_hwcoll; i++) {
		if (slingshot_state.job_hwcoll[i] == job_id) {
			goto done;
		} else if (slingshot_state.job_hwcoll[i] == 0 && freeslot < 0) {
			freeslot = i;
		}
	}

	/* If no free slot, allocate a new slot in the job_vnis table */
	if (freeslot < 0) {
		freeslot = slingshot_state.num_job_hwcoll;
		slingshot_state.num_job_hwcoll++;
		xrecalloc(slingshot_state.job_hwcoll,
			  slingshot_state.num_job_hwcoll, sizeof(uint32_t));
	}
	slingshot_state.job_hwcoll[freeslot] = job_id;
done:
	log_flag(SWITCH, "job_hwcoll[%d] %u num_job_hwcoll=%d",
		 freeslot, job_id, slingshot_state.num_job_hwcoll);
	return;
}

/*
 * Zero out entry if job_id is found in slingshot_state.job_hwcoll[];
 * return true if job_id is in the table, false otherwise.
 */
static bool _clear_hwcoll(uint32_t job_id)
{
	if (slingshot_state.num_job_hwcoll == 0)
		return false;

	for (int i = 0; i < slingshot_state.num_job_hwcoll; i++) {
		if (slingshot_state.job_hwcoll[i] == job_id) {
			slingshot_state.job_hwcoll[i] = 0;
			return true;
		}
	}
	return false;
}

static json_object *_post_job_to_fabric_manager(uint32_t job_id)
{
	long status = 0;
	json_object *reqjson = NULL;
	json_object *jobid_json = NULL;
	json_object *mcasts_json = NULL;
	json_object *respjson = NULL;
	char *jobid_str = NULL;

	/* Put job ID and number of multicast addresses to reserve in payload */
	jobid_str = xstrdup_printf("%s-%u", slurm_conf.cluster_name, job_id);
	if (!(reqjson = json_object_new_object()) ||
	    !(jobid_json = json_object_new_string(jobid_str)) ||
	    json_object_object_add(reqjson, "jobID", jobid_json) ||
	    !(mcasts_json = json_object_new_int(
			slingshot_config.hwcoll_addrs_per_job)) ||
	    json_object_object_add(reqjson, "mcastLimit", mcasts_json)) {
		error("Couldn't create collectives request json");
		json_object_put(jobid_json);
		json_object_put(mcasts_json);
		goto out;
	}
	log_flag(SWITCH, "reqjson='%s'", json_object_to_json_string(reqjson));

	if (!(respjson = slingshot_rest_post(&fm_conn,
					     "/fabric/collectives/jobs",
					     reqjson, &status))) {
		error("POST to fabric manager for collectives failed: %ld",
		      status);
		goto out;
	}
	log_flag(SWITCH, "respjson='%s'", json_object_to_json_string(respjson));

out:
	xfree(jobid_str);
	json_object_put(reqjson);

	return respjson;
}

/*
 * If Slingshot hardware collectives are configured, and the job has
 * enough nodes, reserve the configured per-job number of multicast addresses
 * by registering the job with the fabric manager
 */
extern bool slingshot_setup_collectives(slingshot_stepinfo_t *job,
					uint32_t node_cnt, uint32_t job_id,
					uint32_t step_id)
{
	long status = 0;
	json_object *respjson = NULL;
	char *jobid_str = NULL, *url;
	const char *token = NULL;
	bool rc = false;

	/*
	 * Only reserve multicast addresses if configured and job has
	 * enough nodes
	 */
	if (!slingshot_config.fm_url || !collectives_enabled ||
	    (slingshot_config.hwcoll_num_nodes == 0) ||
	    (node_cnt < slingshot_config.hwcoll_num_nodes))
		return true;

	/* GET on the job object if it already exists */
	url = xstrdup_printf("/fabric/collectives/jobs/%s-%u",
			     slurm_conf.cluster_name, job_id);
	if (!(respjson = slingshot_rest_get(&fm_conn, url, &status))) {
		error("GET %s to fabric manager for job failed: %ld",
			url, status);
	} else {
		log_flag(SWITCH, "GET %s resp='%s'",
				url, json_object_to_json_string(respjson));
	}
	xfree(url);

	if (status == HTTP_NOT_FOUND) {
		/* If the job object doesn't exist, create it */
		respjson = _post_job_to_fabric_manager(job_id);
	}

	/* Get per-job session token out of response */
	if (!(token = json_object_get_string(
			json_object_object_get(respjson, "sessionToken")))) {
		error("Couldn't extract sessionToken from fabric manager response");
		goto out;
	}

	/* Put info in job struct to send to slurmd */
	job->hwcoll = xmalloc(sizeof(slingshot_hwcoll_t));
	job->hwcoll->job_id = job_id;
	job->hwcoll->step_id = step_id;
	job->hwcoll->mcast_token = xstrdup(token);
	job->hwcoll->fm_url = xstrdup(slingshot_config.fm_url);
	job->hwcoll->addrs_per_job = slingshot_config.hwcoll_addrs_per_job;
	job->hwcoll->num_nodes = slingshot_config.hwcoll_num_nodes;

	/*
	 * Save jobID in slingshot_state.job_hwcoll[] array to indicate
	 * use of hardware collectives (for cleanup time)
	 */
	_save_hwcoll(job_id);

	rc = true;

out:
	xfree(jobid_str);
	json_object_put(respjson);
	return rc;
}

/*
 * Set up collectives-related environment variables for job step:
 * if job->hwcoll is set, add the string-ized value of every
 * field in job->hwcoll to this job step's environment
 */
extern void slingshot_collectives_env(slingshot_stepinfo_t *job, char ***env)
{
	slingshot_hwcoll_t *hwcoll = job->hwcoll;
	char *job_id = NULL, *step_id = NULL;
	char *addrs_per_job = NULL, *num_nodes = NULL;
	char *fm_full_url = NULL;

	if (!hwcoll)
		return;

	xstrfmtcat(job_id, "%s-%u", slurm_conf.cluster_name, hwcoll->job_id);
	xstrfmtcat(step_id, "%u", hwcoll->step_id);
	xstrfmtcat(addrs_per_job, "%u", hwcoll->addrs_per_job);
	xstrfmtcat(num_nodes, "%u", hwcoll->num_nodes);
	xstrfmtcat(fm_full_url, "%s/fabric/collectives/multicasts",
		   hwcoll->fm_url);

	log_flag(SWITCH, "%s=%s %s=%s %s=%s",
		 SLINGSHOT_FI_CXI_COLL_JOB_ID_ENV, job_id,
		 SLINGSHOT_FI_CXI_COLL_JOB_STEP_ID_ENV, step_id,
		 SLINGSHOT_FI_CXI_COLL_MCAST_TOKEN_ENV, hwcoll->mcast_token);
	log_flag(SWITCH, "%s=%s %s=%s %s=%s",
		 SLINGSHOT_FI_CXI_COLL_FABRIC_MGR_URL_ENV, fm_full_url,
		 SLINGSHOT_FI_CXI_HWCOLL_ADDRS_PER_JOB_ENV, addrs_per_job,
		 SLINGSHOT_FI_CXI_HWCOLL_MIN_NODES_ENV, num_nodes);

	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_JOB_ID_ENV, job_id);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_JOB_STEP_ID_ENV,
			    step_id);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_MCAST_TOKEN_ENV,
			    hwcoll->mcast_token);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_FABRIC_MGR_URL_ENV,
			    fm_full_url);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_HWCOLL_ADDRS_PER_JOB_ENV,
			    addrs_per_job);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_HWCOLL_MIN_NODES_ENV,
			    num_nodes);
	xfree(job_id);
	xfree(step_id);
	xfree(addrs_per_job);
	xfree(num_nodes);
	xfree(fm_full_url);
	return;
}

/*
 * If this job step is using Slingshot hardware collectives, release any
 * multicast addresses associated with this job step, by PATCHing the job
 * object.  The job object has a "jobSteps" field:
 * "jobSteps": { "<job step ID>": [ <mcast_address1>, ... ] }
 * To release the multicast addresses associated with the job step,
 * PATCH the "jobSteps" object with a NULL value under the job step ID key.
 */
extern void slingshot_release_collectives_job_step(slingshot_stepinfo_t *job)
{
	slingshot_hwcoll_t *hwcoll = job->hwcoll;
	long status = 0;
	char *stepid_str = NULL;
	json_object *reqjson = NULL;
	json_object *jobsteps_json = NULL;
	json_object *respjson = NULL;
	const char *url = NULL;

	/* Just return if we're not using collectives */
	if (!slingshot_config.fm_url || !collectives_enabled || !hwcoll)
		return;

	/* Payload is '{ "jobSteps": { "<step_id>": null } }' */
	stepid_str = xstrdup_printf("%u", hwcoll->step_id);
	if (!(reqjson = json_object_new_object()) ||
	    !(jobsteps_json = json_object_new_object()) ||
	    json_object_object_add(jobsteps_json, stepid_str, NULL) ||
	    json_object_object_add(reqjson, "jobSteps", jobsteps_json)) {
		error("Slingshot hardware collectives release failed (JSON creation failed)");
		json_object_put(jobsteps_json);
		goto out;
	}
	log_flag(SWITCH, "reqjson='%s'", json_object_to_json_string(reqjson));

	/*
	 * PATCH the "jobSteps" map in this job's object
	 * NOTE: timing-wise, the job complete could happen before this.
	 * Don't fail on error 404 (Not Found)
	 */
	url = xstrdup_printf("/fabric/collectives/jobs/%s-%u",
			     slurm_conf.cluster_name, hwcoll->job_id);
	if (!(respjson = slingshot_rest_patch(&fm_conn, url, reqjson,
					      &status))) {
		if (status != HTTP_NOT_FOUND) {
			error("Slingshot hardware collectives release failed (PATCH %s fabric manager failed: %ld)",
			      url, status);
			goto out;
		}
	}
	log_flag(SWITCH, "respjson='%s'", json_object_to_json_string(respjson));

	/* If in debug mode, do a GET on the PATCHed job object and print it */
	if ((slurm_conf.debug_flags & DEBUG_FLAG_SWITCH) &&
	    (status != HTTP_NOT_FOUND)) {
		json_object_put(respjson);
		if (!(respjson = slingshot_rest_get(&fm_conn, url, &status))) {
			error("GET %s to fabric manager for job failed: %ld",
			      url, status);
		} else {
			log_flag(SWITCH, "GET %s resp='%s'",
				 url, json_object_to_json_string(respjson));
		}
	}

out:
	json_object_put(reqjson);
	json_object_put(respjson);
	xfree(stepid_str);
	xfree(url);
	return;
}

/*
 * If this job is using Slingshot hardware collectives, release any
 * multicast addresses associated with this job, by DELETEing the job
 * object from the fabric manager.
 */
extern void slingshot_release_collectives_job(uint32_t job_id)
{
	long status = 0;
	const char *url = NULL;

	/* Just return if we're not using collectives */
	if (!slingshot_config.fm_url || !collectives_enabled)
		return;

	_clear_hwcoll(job_id);

	/* Do a DELETE on the job object in the fabric manager */
	url = xstrdup_printf("/fabric/collectives/jobs/%s-%u",
			     slurm_conf.cluster_name, job_id);
	if (!slingshot_rest_delete(&fm_conn, url, &status)) {
		error("DELETE %s from fabric manager for collectives failed: %ld",
		      url, status);
	}
	xfree(url);
	return;
}
