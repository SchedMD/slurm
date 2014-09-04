/*****************************************************************************\
 *  cookies.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Copyright 2014 Cray Inc. All Rights Reserved.
 *  Written by David Gloe
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

#include "switch_cray.h"

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)

#include <stdlib.h>

#include "src/common/xstring.h"
#include "src/common/read_config.h"

// Default lease time 1 week
#define COOKIE_LEASE_TIME 60*60*24*7

// Extend lease every 2 hours
#define COOKIE_LEASE_INTERVAL 60*60*2

// Cookie owner
#ifndef COOKIE_OWNER
#define COOKIE_OWNER "SLURM"
#endif

// Number of cookies to request
#define NUM_COOKIES 2

// List of cookie ids currently in use
static int32_t *cookie_id_list = NULL;

// Size of the cookie id list
static int32_t cookie_id_list_size = 0;

// Capacity of the cookie id list
static size_t cookie_id_list_capacity = 0;

// Mutex for the cookie id list
static pthread_mutex_t cookie_id_mutex = PTHREAD_MUTEX_INITIALIZER;


// If we are running the lease_extender
static bool lease_extender_running = false;

// Static function declarations
static void _add_cookie(int32_t cookie_id);
static void _remove_cookie(int32_t cookie_id);
static void *_lease_extender(void *args);
static bool _in_slurmctld(void);

/*
 * Start the thread to extend cookie leases.
 */
extern int start_lease_extender(void)
{
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries = 0;

	// Start lease extender in the slurmctld
	if (!_in_slurmctld())
		return SLURM_SUCCESS;

	/* spawn an agent */
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent,
					PTHREAD_CREATE_DETACHED)) {
		CRAY_ERR("pthread_attr_setdetachstate error %m");
	}

	retries = 0;
	while (pthread_create(&thread_agent, &attr_agent,
			      &_lease_extender, NULL)) {
		error("pthread_create error %m");
		if (++retries > 1) {
			CRAY_ERR("Can't create pthread");
			slurm_attr_destroy(&attr_agent);
			return SLURM_ERROR;
		}

		usleep(1000);	/* sleep and retry */
	}
	slurm_attr_destroy(&attr_agent);
	return SLURM_SUCCESS;
}

/*
 * cleanup the lease_extender
 */
extern int cleanup_lease_extender(void)
{
	// Cleanup lease extender in the slurmctld
	if (!_in_slurmctld())
		return SLURM_SUCCESS;

	lease_extender_running = false;

	pthread_mutex_lock(&cookie_id_mutex);
	xfree(cookie_id_list);
	cookie_id_list_size = 0;
	cookie_id_list_capacity = 0;
	pthread_mutex_unlock(&cookie_id_mutex);

	return SLURM_SUCCESS;
}

/*
 * Lease cookies for this job, filling in the information in *job.
 * Leased cookies will periodically have their lease extended.
 */
extern int lease_cookies(slurm_cray_jobinfo_t *job, int32_t *nodes,
			 int32_t num_nodes)
{
	int rc;
	uint32_t i;
	char *err_msg = NULL;
	int32_t *cookie_ids = NULL;
	char **cookies = NULL;

	if (!_in_slurmctld())
		return SLURM_SUCCESS;

	/*
	 * Lease some cookies
	 *
	 * TODO: I could ensure that the nodes list was sorted either by doing
	 * some research to see if it comes in sorted or calling a sort
	 * routine.
	 */
	rc = alpsc_lease_cookies(&err_msg, COOKIE_OWNER, job->apid,
				 COOKIE_LEASE_TIME, nodes,
				 num_nodes, NUM_COOKIES,
				 &cookies, &cookie_ids);
	ALPSC_SN_DEBUG("alpsc_lease_cookies");
	if (rc != 0) {
		return SLURM_ERROR;
	}

	/*
	 * xmalloc the space for the cookies and cookie_ids, so it can be freed
	 * with xfree later, which is consistent with SLURM practices and how
	 * the rest of the structure will be freed.
	 * We must free() the ALPS Common library allocated memory using free(),
	 * not xfree().
	 */
	job->num_cookies = NUM_COOKIES;
	job->cookie_ids = (uint32_t *) xmalloc(sizeof(uint32_t) * NUM_COOKIES);
	memcpy(job->cookie_ids, cookie_ids, sizeof(uint32_t) * NUM_COOKIES);
	free(cookie_ids);

	job->cookies = (char **) xmalloc(sizeof(char **) * NUM_COOKIES);
	for (i = 0; i < NUM_COOKIES; i++) {
		job->cookies[i] = xstrdup(cookies[i]);
		free(cookies[i]);
	}
	free(cookies);

	// Add them to the list
	for (i = 0; i < job->num_cookies; i++) {
		_add_cookie(job->cookie_ids[i]);
	}
	return SLURM_SUCCESS;
}

/*
 * Track cookies which have already been leased. These cookies will also
 * have their lease extended periodically. Useful for when slurmctld is
 * restarted, to track cookies leased before it was shut down.
 */
extern int track_cookies(slurm_cray_jobinfo_t *job)
{
	uint32_t i;

	if (!_in_slurmctld())
		return SLURM_SUCCESS;

	// Add cookies to the list
	for (i = 0; i < job->num_cookies; i++) {
		_add_cookie(job->cookie_ids[i]);
	}
	return SLURM_SUCCESS;
}

/*
 * Release cookies which have been leased.
 */
extern int release_cookies(slurm_cray_jobinfo_t *job)
{
	uint32_t i;
	int rc;
	char *err_msg = NULL;

	if (!_in_slurmctld())
		return SLURM_SUCCESS;

	// Remove cookies from the list
	for (i = 0; i < job->num_cookies; i++) {
		_remove_cookie(job->cookie_ids[i]);
	}

	// Release them
	rc = alpsc_release_cookies(&err_msg, (int32_t *) job->cookie_ids,
				   (int32_t) job->num_cookies);
	ALPSC_SN_DEBUG("alpsc_release_cookies");
	if (rc != 0) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Add a cookie to the tracked cookie list
 */
static void _add_cookie(int32_t cookie_id)
{
	int32_t i;

	// Lock the mutex
	pthread_mutex_lock(&cookie_id_mutex);

	// If the cookie is already in the list, skip
	for (i = 0; i < cookie_id_list_size; i++) {
		if (cookie_id_list[i] == cookie_id) {
			pthread_mutex_unlock(&cookie_id_mutex);
			CRAY_INFO("Duplicate cookie %"PRId32" found in tracked"
				  " cookie list", cookie_id);
			return;
		}
	}

	// Extend id list if necessary
	if (cookie_id_list_size + 1 > cookie_id_list_capacity) {
		if (cookie_id_list_capacity == 0) {
			cookie_id_list_capacity = 2048;
		} else {
			cookie_id_list_capacity *= 2;
		}
		cookie_id_list = xrealloc(cookie_id_list,
					  (cookie_id_list_capacity
					   * sizeof(int32_t)));
	}

	// Set value
	cookie_id_list[cookie_id_list_size] = cookie_id;
	cookie_id_list_size++;

	// Unlock the mutex
	pthread_mutex_unlock(&cookie_id_mutex);
}

/*
 * Remove a cookie from the tracked cookie list
 */
static void _remove_cookie(int32_t cookie_id)
{
	int32_t i;
	int found = 0;

	// Lock the mutex
	pthread_mutex_lock(&cookie_id_mutex);

	// Find a match in the list
	for (i = 0; i < cookie_id_list_size; i++) {
		if (cookie_id_list[i] == cookie_id) {
			// Copy the last id to this spot
			if (i < cookie_id_list_size - 1) {
				cookie_id_list[i] =
					cookie_id_list[cookie_id_list_size - 1];
			}

			found = 1;
			cookie_id_list_size--;
			break;
		}
	}
	if (!found) {
		CRAY_INFO("Cookie %"PRId32" not found in tracked cookie list",
			  cookie_id);
	}

	// Unlock the mutex
	pthread_mutex_unlock(&cookie_id_mutex);
}

static void *_lease_extender(void *args)
{
	int rc;
	char *err_msg = NULL;

	CRAY_INFO("Leasing cookies for %ds, renewing every %ds",
		  COOKIE_LEASE_TIME, COOKIE_LEASE_INTERVAL);

	lease_extender_running = true;

	while (lease_extender_running) {
		// Lock the mutex
		pthread_mutex_lock(&cookie_id_mutex);

		// If there are cookies, extend their leases
		if (cookie_id_list_size > 0) {
			// Extend the cookie leases
			CRAY_INFO("Extending leases for %"PRId32" cookies",
				  cookie_id_list_size);

			rc = alpsc_set_cookie_lease(&err_msg, cookie_id_list,
						    cookie_id_list_size,
						    COOKIE_LEASE_TIME);
			ALPSC_SN_DEBUG("alpsc_set_cookie_lease");

			// Just ignore errors, not much we can do about them
		}

		// Unlock the mutex
		pthread_mutex_unlock(&cookie_id_mutex);

		// Wait until we want to extend leases again
		sleep(COOKIE_LEASE_INTERVAL);
	}
	return NULL;
}

static bool _in_slurmctld(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmctld");
	}

	return run;
}

#endif
