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

#include "switch_hpe_slingshot.h"
#include "rest.h"

static slingshot_rest_conn_t fm_conn;  /* Connection to fabric manager */

static bool collectives_enabled = false;

/*
 * Read any authentication files and connect to the fabric manager,
 * which implements a REST interface supporting Slingshot collectives
 */
extern bool slingshot_init_collectives(void)
{
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

	if (!slingshot_rest_connect(&fm_conn))
		goto err;

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

/*
 * If Slingshot hardware collectives are configured, and the job has
 * enough nodes, reserve the configured per-job number of multicast addresses
 * by registering the job with the fabric manager
 */
extern bool slingshot_setup_collectives(slingshot_jobinfo_t *job,
					uint32_t node_cnt, uint32_t job_id,
					uint32_t step_id)
{
	long status = 0;
	json_object *reqjson = NULL;
	json_object *jobid_json = NULL;
	json_object *mcasts_json = NULL;
	json_object *respjson = NULL;
	char *jobid_str = NULL;
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

	/* Put job ID and number of multicast addresses to reserve in payload */
	jobid_str = xstrdup_printf("%u", job_id);
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

	/* If in debug mode, do a GET on the new job object and print it */
	if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH) {
		char *url = xstrdup_printf("/fabric/collectives/jobs/%u",
					   job_id);
		json_object_put(respjson);
		if (!(respjson = slingshot_rest_get(&fm_conn, url, &status))) {
			error("GET %s to fabric manager for job failed: %ld",
			      url, status);
		} else {
			log_flag(SWITCH, "GET %s resp='%s'",
				 url, json_object_to_json_string(respjson));
		}
		xfree(url);
	}

	/*
	 * Save jobID in slingshot_state.job_hwcoll[] array to indicate
	 * use of hardware collectives (for cleanup time)
	 */
	_save_hwcoll(job_id);

	rc = true;

out:
	xfree(jobid_str);
	json_object_put(reqjson);
	json_object_put(respjson);
	return rc;
}

/*
 * Set up collectives-related environment variables for job step:
 * if job->hwcoll is set, add the string-ized value of every
 * field in job->hwcoll to this job step's environment
 */
extern void slingshot_collectives_env(slingshot_jobinfo_t *job, char ***env)
{
	slingshot_hwcoll_t *hwcoll = job->hwcoll;
	char *job_id = NULL, *step_id = NULL;
	char *addrs_per_job = NULL, *num_nodes = NULL;

	if (!hwcoll)
		return;

	xstrfmtcat(job_id, "%u", hwcoll->job_id);
	xstrfmtcat(step_id, "%u", hwcoll->step_id);
	xstrfmtcat(addrs_per_job, "%u", hwcoll->addrs_per_job);
	xstrfmtcat(num_nodes, "%u", hwcoll->num_nodes);

	log_flag(SWITCH, "%s=%s %s=%s %s=%s",
		 SLINGSHOT_FI_CXI_COLL_JOB_ID_ENV, job_id,
		 SLINGSHOT_FI_CXI_COLL_JOB_STEP_ID_ENV, step_id,
		 SLINGSHOT_FI_CXI_COLL_MCAST_TOKEN_ENV, hwcoll->mcast_token);
	log_flag(SWITCH, "%s=%s %s=%s %s=%s",
		 SLINGSHOT_FI_CXI_COLL_FABRIC_MGR_URL_ENV, hwcoll->mcast_token,
		 SLINGSHOT_FI_CXI_HWCOLL_ADDRS_PER_JOB_ENV, addrs_per_job,
		 SLINGSHOT_FI_CXI_HWCOLL_MIN_NODES_ENV, num_nodes);

	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_JOB_ID_ENV, job_id);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_JOB_STEP_ID_ENV,
			    step_id);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_MCAST_TOKEN_ENV,
			    hwcoll->mcast_token);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_COLL_FABRIC_MGR_URL_ENV,
			    hwcoll->fm_url);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_HWCOLL_ADDRS_PER_JOB_ENV,
			    addrs_per_job);
	env_array_overwrite(env, SLINGSHOT_FI_CXI_HWCOLL_MIN_NODES_ENV,
			    num_nodes);
	xfree(job_id);
	xfree(step_id);
	xfree(addrs_per_job);
	xfree(num_nodes);
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
extern void slingshot_release_collectives_job_step(slingshot_jobinfo_t *job)
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
	url = xstrdup_printf("/fabric/collectives/jobs/%u", hwcoll->job_id);
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

	/*
	 * Just return if no job_id in slingshot_state.job_hwcoll[];
	 * clear out entry if found
	 */
	if (!_clear_hwcoll(job_id))
		return;

	/* Do a DELETE on the job object in the fabric manager */
	url = xstrdup_printf("/fabric/collectives/jobs/%u", job_id);
	if (!slingshot_rest_delete(&fm_conn, url, &status)) {
		error("DELETE %s from fabric manager for collectives failed: %ld",
		      url, status);
	}
	xfree(url);
	return;
}
