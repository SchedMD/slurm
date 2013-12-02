/*****************************************************************************\
 *  select_cray.c - node selection plugin for cray systems.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
 *  Written by Danny Auble <da@schedmd.com>
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

#define _GNU_SOURCE 1

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#  if WITH_PTHREADS
#    include <pthread.h>
#  endif
#endif

#include <stdio.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/slurmctld/locks.h"
#include "other_select.h"

#include "alpscomm_sn.h"

/**
 * struct select_jobinfo - data specific to Cray node selection plugin
 * @magic:		magic number, must equal %JOBINFO_MAGIC
 * @other_jobinfo:	hook into attached, "other" node selection plugin.
 */
struct select_jobinfo {
	uint16_t                cleaning;
	uint16_t		magic;
	select_jobinfo_t	*other_jobinfo;
};
#define JOBINFO_MAGIC 0x86ad

/**
 * struct select_nodeinfo - data used for node information
 * @magic:		magic number, must equal %NODEINFO_MAGIC
 * @other_nodeinfo:	hook into attached, "other" node selection plugin.
 */
struct select_nodeinfo {
	uint16_t		magic;
	select_nodeinfo_t	*other_nodeinfo;
};

typedef struct {
	uint64_t apid;
	uint32_t exit_code;
	uint32_t jobid;
	char *nodelist;
	bool step;
} nhc_info_t;

#define NODEINFO_MAGIC 0x85ad
#define MAX_PTHREAD_RETRIES  1

/* Change CRAY_STATE_VERSION value when changing the state save
 * format i.e. state_safe() */
#define CRAY_STATE_VERSION      "VER001"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
slurmdb_cluster_rec_t *working_cluster_rec  __attribute__((weak_import)) = NULL;
#else
slurm_ctl_conf_t slurmctld_conf;
int bg_recover = NOT_FROM_CONTROLLER;
slurmdb_cluster_rec_t *working_cluster_rec = NULL;
#endif

#ifdef HAVE_NATIVE_CRAY
/* Used for aeld communication */
alpsc_ev_app_t *app_list = NULL;	// List of running/suspended apps
int32_t app_list_size = 0;		// Number of running/suspended apps
size_t app_list_capacity = 0;		// Capacity of app list
alpsc_ev_app_t *event_list = NULL;	// List of app state changes
int32_t event_list_size = 0;		// Number of events
size_t event_list_capacity = 0;		// Capacity of event list
volatile sig_atomic_t aeld_running = 0;	// 0 if the aeld thread has exited
					// 1 if the session is temporarily down
					// 2 if the session is running
pthread_mutex_t aeld_mutex = PTHREAD_MUTEX_INITIALIZER;	// Mutex for the above

#define AELD_SESSION_INTERVAL	60	// aeld session retry interval (s)
#define AELD_EVENT_INTERVAL	100	// aeld event sending interval (ms)

/* Static functions used for aeld communication */
static void _handle_aeld_error(const char *funcname, char *errmsg, int rv,
			       alpsc_ev_session_t **session);
static void _clear_event_list(alpsc_ev_app_t *list, int32_t *size);
static void _start_session(alpsc_ev_session_t **session, int *sessionfd);
static void *_aeld_event_loop(void *args);
static void _initialize_event(alpsc_ev_app_t *event,
			      struct job_record *job_ptr,
			      struct step_record *step_ptr,
			      alpsc_ev_app_state_e state);
static void _copy_event(alpsc_ev_app_t *dest, alpsc_ev_app_t *src);
static void _free_event(alpsc_ev_app_t *event);
static void _add_to_app_list(alpsc_ev_app_t **list, int32_t *size,
			     size_t *capacity, alpsc_ev_app_t *app);
static void _update_app(struct job_record *job_ptr,
			struct step_record *step_ptr,
			alpsc_ev_app_state_e state);
#endif

/* All current (2011) XT/XE installations have a maximum dimension of 3,
 * smaller systems deploy a 2D Torus which has no connectivity in
 * X-dimension.  We know the highest system dimensions possible here
 * are 3 so we set it to that.  Do not use SYSTEM_DIMENSIONS since
 * that could easily be wrong if built on a non Cray system. */
static int select_cray_dim_size[3] = {-1};
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
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the node selection API matures.
 */
const char plugin_name[]	= "Cray node selection plugin";
const char plugin_type[]	= "select/cray";
uint32_t plugin_id		= 107;
const uint32_t plugin_version	= 100;

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo);

static int _run_nhc(nhc_info_t *nhc_info)
{
#ifdef HAVE_NATIVE_CRAY
	int argc = 11, status = 1, wait_rc, i = 0;
	char *argv[argc];
	pid_t cpid;
	char *jobid_char = NULL, *apid_char = NULL, *nodelist_nids = NULL,
		*exit_char = NULL;
	DEF_TIMERS;

	START_TIMER;

	apid_char = xstrdup_printf("%"PRIu64"", nhc_info->apid);
	exit_char = xstrdup_printf("%u", nhc_info->exit_code);
	jobid_char = xstrdup_printf("%u", nhc_info->jobid);
	nodelist_nids = cray_nodelist2nids(NULL, nhc_info->nodelist);

	argv[i++] = "/opt/cray/nodehealth/default/bin/xtcleanup_after";
	argv[i++] = "-a";
	argv[i++] = apid_char;
	argv[i++] = "-e";
	argv[i++] = exit_char;
	argv[i++] = "-r";
	argv[i++] = jobid_char;
	argv[i++] = "-m";
	argv[i++] = nhc_info->step ? "application" : "reservation";
	argv[i++] = nodelist_nids;
	argv[i++] = NULL;

	if (debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("Calling NHC for jobid %u and apid %"PRIu64" "
		     "on nodes %s(%s) exit code %u",
		     nhc_info->jobid, nhc_info->apid,
		     nhc_info->nodelist, nodelist_nids,
		     nhc_info->exit_code);
	}

	if (!nhc_info->nodelist || !nodelist_nids) {
		/* already done */
		goto fini;
	}

	if ((cpid = fork()) < 0) {
		error("_run_nhc fork error: %m");
		goto fini;
	}
	if (cpid == 0) {
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execvp(argv[0], argv);
		exit(127);
	}

	while (1) {
		wait_rc = waitpid(cpid, &status, 0);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("_run_nhc waitpid error: %m");
			break;
		} else if (wait_rc > 0) {
			killpg(cpid, SIGKILL);	/* kill children too */
			break;
		}
	}
	END_TIMER;
	if (status != 0) {
		error("_run_nhc jobid %u and apid %"PRIu64" exit "
		      "status %u:%u took: %s",
		      nhc_info->jobid, nhc_info->apid, WEXITSTATUS(status),
		      WTERMSIG(status), TIME_STR);
	} else if (debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("_run_nhc jobid %u and apid %"PRIu64" completed took: %s",
		     nhc_info->jobid, nhc_info->apid, TIME_STR);

 fini:
	xfree(apid_char);
	xfree(exit_char);
	xfree(jobid_char);
	xfree(nodelist_nids);

	return status;
#else
	if (debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("simluating calling NHC for jobid %u "
		     "and apid %"PRIu64" on nodes %s",
		     nhc_info->jobid, nhc_info->apid, nhc_info->nodelist);

	/* simulate sleeping */
	sleep(2);
	return 0;
#endif
}

#ifdef HAVE_NATIVE_CRAY
/*
 * Clean up after a fatal error
 */
static void _aeld_cleanup(void)
{
	aeld_running = 0;

	// Free any used memory
	pthread_mutex_lock(&aeld_mutex);
	_clear_event_list(app_list, &app_list_size);
	app_list_capacity = 0;
	xfree(app_list);
	_clear_event_list(event_list, &event_list_size);
	event_list_capacity = 0;
	xfree(event_list);
	pthread_mutex_unlock(&aeld_mutex);
}

/*
 * Deal with an aeld error.
 */
static void _handle_aeld_error(const char *funcname, char *errmsg, int rv,
			       alpsc_ev_session_t **session)
{
	error("%s failed: %s", funcname, errmsg);
	free(errmsg);
	aeld_running = 1;
	alpsc_ev_destroy_session(*session);
	*session = NULL;

	// Unrecoverable errors
	if (rv == 1 || rv == 2) {
		_aeld_cleanup();
		pthread_exit(NULL);
	}
	return;
}

/*
 * Clear all events from the event list. Must already have the aeld_mutex
 * locked.
 */
static void _clear_event_list(alpsc_ev_app_t *list, int32_t *size)
{
	int32_t i;

	for (i = 0; i < *size; i++) {
		_free_event(&list[i]);
	}
	*size = 0;
	return;
}

/*
 * Start an aeld session.
 */
static void _start_session(alpsc_ev_session_t **session, int *sessionfd)
{
	int rv;
	char *errmsg;

	while (1) {
		pthread_mutex_lock(&aeld_mutex);

		// Clear out the event list
		_clear_event_list(event_list, &event_list_size);

		// Create the session
		rv = alpsc_ev_create_session(&errmsg, session, app_list,
					     app_list_size);

		pthread_mutex_unlock(&aeld_mutex);

		if (rv) {
			_handle_aeld_error("alpsc_ev_create_session",
					   errmsg, rv, session);
		} else {
			// Get the session fd
			rv = alpsc_ev_get_session_fd(&errmsg, *session,
						     sessionfd);
			if (rv) {
				_handle_aeld_error("alpsc_ev_get_session_fd",
						   errmsg, rv, session);
			} else {
				aeld_running = 2;
				break;
			}
		}

		// If we get here, start over
		sleep(AELD_SESSION_INTERVAL);
	}

	debug("%s: Created aeld session fd %d", __func__, *sessionfd);
	return;
}

/*
 * Run the aeld communication event loop, sending events as we get
 * them and all apps on sync requests.
 */
static void *_aeld_event_loop(void *args)
{
	int rv, sessionfd;
	alpsc_ev_session_t *session = NULL;
	struct pollfd fds[1];
	char *errmsg;

	aeld_running = 1;

	// Start out by creating a session
	_start_session(&session, &sessionfd);

	// Now poll on the session fd
	fds[0].fd = sessionfd;
	fds[0].events = POLLIN | POLLPRI | POLLRDHUP;
	while ((rv = TEMP_FAILURE_RETRY(poll(fds, 1, AELD_EVENT_INTERVAL)))
		!= -1) {
		// There was activity on the file descriptor, get state
		if (rv > 0) {
			rv = alpsc_ev_get_session_state(&errmsg, session);
			if (rv > 0) {
				_handle_aeld_error("alpsc_ev_get_session_state",
						   errmsg, rv, &session);
				_start_session(&session, &sessionfd);
				fds[0].fd = sessionfd;
			} else if (rv == -1) {
				// Sync event
				debug("aeld sync event");
				aeld_running = 1;
				alpsc_ev_destroy_session(session);
				session = NULL;
				_start_session(&session, &sessionfd);
				fds[0].fd = sessionfd;
			}
			// Do nothing when rv == 0
		}

		// Process the event list
		pthread_mutex_lock(&aeld_mutex);
		if (event_list_size > 0) {
			// Send event list to aeld
			rv = alpsc_ev_set_application_info(&errmsg, session,
							   event_list,
							   event_list_size);

			// Clear the event list
			_clear_event_list(event_list, &event_list_size);
			pthread_mutex_unlock(&aeld_mutex);
			if (rv > 0) {
				_handle_aeld_error(
					"alpsc_ev_set_application_info",
						   errmsg, rv, &session);
				_start_session(&session, &sessionfd);
				fds[0].fd = sessionfd;
			}
		} else {
			pthread_mutex_unlock(&aeld_mutex);
		}
	}

	error("%s: poll failed: %m", __func__);
	_aeld_cleanup();
	return NULL;
}

/*
 * Initialize an alpsc_ev_app_t
 */
static void _initialize_event(alpsc_ev_app_t *event,
			      struct job_record *job_ptr,
			      struct step_record *step_ptr,
			      alpsc_ev_app_state_e state)
{
	hostlist_t hl;
	hostlist_iterator_t hlit;
	char *node;
	int rv;

	event->apid = SLURM_ID_HASH(job_ptr->job_id, step_ptr->step_id);
	event->uid = job_ptr->user_id;
	event->app_name = xstrdup(step_ptr->name);
	event->batch_id = xmalloc(20);	// More than enough to hold max uint32
	snprintf(event->batch_id, 20, "%"PRIu32, job_ptr->job_id);
	event->state = state;
	event->nodes = NULL;
	event->num_nodes = 0;

	// Fill in nodes and num_nodes
	if (step_ptr->step_layout) {
		hl = hostlist_create(step_ptr->step_layout->node_list);
		if (hl == NULL) {
			return;
		}
		hlit = hostlist_iterator_create(hl);
	    if (hlit == NULL) {
			hostlist_destroy(hl);
			return;
		}

		event->nodes = xmalloc(step_ptr->step_layout->node_cnt
				* sizeof(int32_t));

		while ((node = hostlist_next(hlit)) != NULL) {
			rv = sscanf(node, "nid%"SCNd32,
				    &event->nodes[event->num_nodes]);
			if (rv) {
				event->num_nodes++;
			} else {
				debug("%s: couldn't parse node %s, skipping",
				      __func__, node);
			}
			free(node);
		}

		hostlist_iterator_destroy(hlit);
		hostlist_destroy(hl);
	} else {
	    // TODO: do we have to worry about batch scripts?
	}
	return;
}

/*
 * Copy an alpsc_ev_app_t
 */
static void _copy_event(alpsc_ev_app_t *dest, alpsc_ev_app_t *src)
{
        dest->apid = src->apid;
        dest->uid = src->uid;
        dest->app_name = xstrdup(src->app_name);
        dest->batch_id = xstrdup(src->batch_id);
        dest->state = src->state;
        if (src->num_nodes > 0 && src->nodes != NULL) {
                dest->nodes = xmalloc(src->num_nodes * sizeof(int32_t));
                memcpy(dest->nodes, src->nodes,
		       src->num_nodes * sizeof(int32_t));
                dest->num_nodes = src->num_nodes;
        } else {
                dest->nodes = NULL;
                dest->num_nodes = 0;
        }
        return;
}

/*
 * Free an alpsc_ev_app_t
 */
static void _free_event(alpsc_ev_app_t *event)
{
	xfree(event->app_name);
	xfree(event->batch_id);
	xfree(event->nodes);
	return;
}

/*
 * Add to a list. Must have the aeld_mutex locked.
 */
static void _add_to_app_list(alpsc_ev_app_t **list, int32_t *size,
			     size_t *capacity, alpsc_ev_app_t *app)
{
	// Realloc if necessary
	if (*size + 1 > *capacity) {
		if (*capacity == 0) {
			*capacity = 16;
		} else {
			*capacity *= 2;
		}
		*list = xrealloc(*list, *capacity * sizeof(alpsc_ev_app_t));
	}

	// Copy the event to the destination
	_copy_event(*list + *size, app);
	(*size)++;
	return;
}

/*
 * For starting apps, push to the app list. For ending apps, removes from the
 * app list. For suspend/resume apps, edits the app list. Always adds to the
 * event list.
 */
static void _update_app(struct job_record *job_ptr,
			struct step_record *step_ptr,
			alpsc_ev_app_state_e state)
{
	uint64_t apid;
	int32_t i;
	alpsc_ev_app_t app;
	int found;

	// If aeld thread isn't running, do nothing
	if (aeld_running == 0) {
		return;
	}

	// Fill in the new event
	_initialize_event(&app, job_ptr, step_ptr, state);

	pthread_mutex_lock(&aeld_mutex);

	// Add it to the event list, only if aeld is up
	if (aeld_running == 2) {
		_add_to_app_list(&event_list, &event_list_size,
				 &event_list_capacity, &app);
	}

	// Now deal with the app list
	// Maintain app list even if aeld is down, so we have it ready when
	// it comes up.
	switch(state) {
	case ALPSC_EV_START:
		// This is new, add to the app list
		_add_to_app_list(&app_list, &app_list_size,
				 &app_list_capacity, &app);
		break;
	case ALPSC_EV_END:
		// Search for the app matching this apid
		found = 0;
		apid = SLURM_ID_HASH(job_ptr->job_id, step_ptr->step_id);
		for (i = 0; i < app_list_size; i++) {
			if (app_list[i].apid == apid) {
				found = 1;

				// Free allocated info
				_free_event(&app_list[i]);

				// Copy last list entry to this spot
				if (i < app_list_size - 1) {
					memcpy(&app_list[i],
					       &app_list[app_list_size - 1],
					       sizeof(alpsc_ev_app_t));
				}

				app_list_size--;
				break;
			}
		}

		// Not found
		if (!found) {
			debug("Application %"PRIu64" not found in app list",
			      apid);
		}
		break;
	case ALPSC_EV_SUSPEND:
	case ALPSC_EV_RESUME:
		// Search for the app matching this apid
		apid = SLURM_ID_HASH(job_ptr->job_id, step_ptr->step_id);
		for (i = 0; i < app_list_size; i++) {
			if (app_list[i].apid == apid) {
				// Found it, update the state
				app_list[i].state =
				    (state == ALPSC_EV_SUSPEND) ?
					ALPSC_EV_SUSPEND : ALPSC_EV_START;
				break;
			}
		}

		// Not found
		if (i >= app_list_size) {
			debug("Application %"PRIu64" not found in app list",
			      apid);
		}
		break;
	default:
		break;
	}

	pthread_mutex_unlock(&aeld_mutex);

	_free_event(&app);
	return;
}
#endif

static void *_job_fini(void *args)
{
	struct job_record *job_ptr = (struct job_record *)args;
	nhc_info_t nhc_info;

	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK
	};
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };


	if (!job_ptr) {
		error("_job_fini: no job ptr given, this should never happen");
		return NULL;
	}

	memset(&nhc_info, 0, sizeof(nhc_info_t));
	lock_slurmctld(job_read_lock);
	nhc_info.jobid = job_ptr->job_id;
	nhc_info.nodelist = xstrdup(job_ptr->nodes);
	nhc_info.exit_code = 1; /* hard code to 1 to always run */
	unlock_slurmctld(job_read_lock);

	/* run NHC */
	_run_nhc(&nhc_info);
	/***********/
	xfree(nhc_info.nodelist);

	lock_slurmctld(job_write_lock);
	if (job_ptr->magic == JOB_MAGIC) {
		select_jobinfo_t *jobinfo = NULL;
		other_job_fini(job_ptr);

		jobinfo = job_ptr->select_jobinfo->data;
		jobinfo->cleaning = 0;
	} else
		error("_job_fini: job %u had a bad magic, "
		      "this should never happen", nhc_info.jobid);

	unlock_slurmctld(job_write_lock);

	return NULL;
}

static void *_step_fini(void *args)
{
	struct step_record *step_ptr = (struct step_record *)args;
	select_jobinfo_t *jobinfo = NULL;
	nhc_info_t nhc_info;

	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK
	};
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };


	if (!step_ptr) {
		error("_step_fini: no step ptr given, "
		      "this should never happen");
		return NULL;
	}

	memset(&nhc_info, 0, sizeof(nhc_info_t));
	nhc_info.step = 1;
	lock_slurmctld(job_read_lock);
	nhc_info.jobid = step_ptr->job_ptr->job_id;
	nhc_info.apid = SLURM_ID_HASH(step_ptr->job_ptr->job_id,
				      step_ptr->step_id);
	nhc_info.exit_code = step_ptr->exit_code;

	if (!step_ptr->step_layout || !step_ptr->step_layout->node_list) {
		if (step_ptr->job_ptr)
			nhc_info.nodelist = xstrdup(step_ptr->job_ptr->nodes);
	} else
		nhc_info.nodelist = xstrdup(step_ptr->step_layout->node_list);
	unlock_slurmctld(job_read_lock);

	/* run NHC */
	_run_nhc(&nhc_info);
	/***********/

	xfree(nhc_info.nodelist);

	lock_slurmctld(job_write_lock);
	if (!step_ptr->job_ptr || !step_ptr->step_node_bitmap) {
		error("For some reason we don't have a step_node_bitmap or "
		      "a job_ptr for %"PRIu64".  This should never happen.",
		      nhc_info.apid);
	} else {
		other_step_finish(step_ptr);

		jobinfo = step_ptr->select_jobinfo->data;
		jobinfo->cleaning = 0;

		/* free resources on the job */
		post_job_step(step_ptr);
	}
	unlock_slurmctld(job_write_lock);

	return NULL;
}

static void _spawn_cleanup_thread(
	void *obj_ptr, void *(*start_routine) (void *))
{
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;

	/* spawn an agent */
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	retries = 0;
	while (pthread_create(&thread_agent, &attr_agent,
			      start_routine, obj_ptr)) {
		error("pthread_create error %m");
		if (++retries > MAX_PTHREAD_RETRIES)
			fatal("Can't create pthread");
		usleep(1000);	/* sleep and retry */
	}
	slurm_attr_destroy(&attr_agent);
}

static void _select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
				 uint16_t protocol_version)
{
	if (!jobinfo) {
		pack16(0, buffer);
	} else {
		pack16(jobinfo->cleaning, buffer);
	}
}

static int _select_jobinfo_unpack(select_jobinfo_t **jobinfo_pptr,
				  Buf buffer, uint16_t protocol_version)
{
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));

	*jobinfo_pptr = jobinfo;

	jobinfo->magic = JOBINFO_MAGIC;

	safe_unpack16(&jobinfo->cleaning, buffer);

	return SLURM_SUCCESS;

unpack_error:
	select_p_select_jobinfo_free(jobinfo);
	*jobinfo_pptr = NULL;

	return SLURM_ERROR;


}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	/* We must call the api here since we call this from other
	 * things other than the slurmctld.
	 */
	uint16_t select_type_param = slurm_get_select_type_param();
	if (select_type_param & CR_OTHER_CONS_RES)
		plugin_id = 108;
	debug_flags = slurm_get_debug_flags();

#ifdef HAVE_NATIVE_CRAY
	// Spawn the aeld thread, only in slurmctld.
	if (run_in_daemon("slurmctld")) {
	    _spawn_cleanup_thread(NULL, _aeld_event_loop);
	}
#endif

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM
 * node selection API.
 */

extern int select_p_state_save(char *dir_name)
{
	return other_state_save(dir_name);
}

extern int select_p_state_restore(char *dir_name)
{
	return other_state_restore(dir_name);
}

extern int select_p_job_init(List job_list)
{
	static bool run_already = false;

	/* Execute only on initial startup. We don't support bgblock
	 * creation on demand today, so there is no need to re-sync data. */
	if (run_already)
		return other_job_init(job_list);

	run_already = true;

	if (!(slurmctld_conf.select_type_param & CR_NHC_NO)
	    && job_list && list_count(job_list)) {
		ListIterator itr = list_iterator_create(job_list);
		struct job_record *job_ptr;

		if (debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("select_p_job_init: syncing jobs");

		while ((job_ptr = list_next(itr))) {
			select_jobinfo_t *jobinfo =
				job_ptr->select_jobinfo->data;

			if (!(slurmctld_conf.select_type_param & CR_NHC_STEP_NO)
			    && job_ptr->step_list
			    && list_count(job_ptr->step_list)) {
				ListIterator itr_step = list_iterator_create(
					job_ptr->step_list);
				struct step_record *step_ptr;
				while ((step_ptr = list_next(itr_step))) {
					jobinfo =
						step_ptr->select_jobinfo->data;

					if (jobinfo && jobinfo->cleaning)
						_spawn_cleanup_thread(
							step_ptr, _step_fini);
				}
				list_iterator_destroy(itr_step);
			}
			jobinfo = job_ptr->select_jobinfo->data;
			if (jobinfo && jobinfo->cleaning)
				_spawn_cleanup_thread(job_ptr, _job_fini);
		}
		list_iterator_destroy(itr);
	}

	return other_job_init(job_list);
}

/*
 * select_p_node_ranking - generate node ranking for Cray nodes
 */
extern bool select_p_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	return false;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	return other_node_init(node_ptr, node_cnt);
}

extern int select_p_block_init(List part_list)
{
	return other_block_init(part_list);
}

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 *	"best" is defined as either single set of consecutive nodes satisfying
 *	the request and leaving the minimum number of unused nodes OR
 *	the fewest number of consecutive node sets
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN exc_core_bitmap - bitmap of cores being reserved.
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init):
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_cpus: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of the job's required at the time that
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	return other_job_test(job_ptr, bitmap, min_nodes, max_nodes,
			      req_nodes, mode, preemptee_candidates,
			      preemptee_job_list, exc_core_bitmap);
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	xassert(job_ptr);

	return other_job_begin(job_ptr);
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	xassert(job_ptr);

	return other_job_ready(job_ptr);
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	return other_job_resized(job_ptr, node_ptr);
}

extern bool select_p_job_expand_allow(void)
{
	return other_job_expand_allow();
}

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	return other_job_expand(from_job_ptr, to_job_ptr);
}

extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	xassert(job_ptr);

	return other_job_signal(job_ptr, signal);
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	select_jobinfo_t *jobinfo = job_ptr->select_jobinfo->data;

	if (slurmctld_conf.select_type_param & CR_NHC_NO) {
		debug3("NHC_No set, not running NHC after allocations");
		other_job_fini(job_ptr);
		return SLURM_SUCCESS;
	}

	jobinfo->cleaning = 1;

	_spawn_cleanup_thread(job_ptr, _job_fini);

	return SLURM_SUCCESS;
}

extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
#ifdef HAVE_NATIVE_CRAY
	ListIterator i;
	struct step_record *step_ptr = NULL;

	// Make an event for each job step
	if (aeld_running) {
		i = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *)list_next(i))
			!= NULL) {
			_update_app(job_ptr, step_ptr, ALPSC_EV_SUSPEND);
		}
		list_iterator_destroy(i);
	}
#endif

	return other_job_suspend(job_ptr, indf_susp);
}

extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
#ifdef HAVE_NATIVE_CRAY
	ListIterator i;
	struct step_record *step_ptr = NULL;

	// Make an event for each job step
	if (aeld_running) {
		i = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *)list_next(i))
			!= NULL) {
			_update_app(job_ptr, step_ptr, ALPSC_EV_RESUME);
		}
		list_iterator_destroy(i);
	}
#endif

	return other_job_resume(job_ptr, indf_susp);
}

extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count)
{
	return other_step_pick_nodes(job_ptr, jobinfo, node_count);
}

extern int select_p_step_start(struct step_record *step_ptr)
{
#ifdef HAVE_NATIVE_CRAY
	if (aeld_running) {
		_update_app(step_ptr->job_ptr, step_ptr, ALPSC_EV_START);
	}
#endif

	return other_step_finish(step_ptr);
}


extern int select_p_step_finish(struct step_record *step_ptr)
{
	select_jobinfo_t *jobinfo = step_ptr->select_jobinfo->data;

#ifdef HAVE_NATIVE_CRAY
	if (aeld_running) {
		_update_app(step_ptr->job_ptr, step_ptr, ALPSC_EV_END);
	}
#endif

	if (slurmctld_conf.select_type_param & CR_NHC_STEP_NO) {
		debug3("NHC_No_Steps set not running NHC on steps.");
		other_step_finish(step_ptr);
		/* free resources on the job */
		post_job_step(step_ptr);
		return SLURM_SUCCESS;
	}
	/* The NHC needs to be ran after each step even if the job is
	   about to run the NHC for the allocation.  The NHC
	   developers feel this is needed.  If it ever changes just
	   remove the below commented code.
	*/

	/*  else if (IS_JOB_COMPLETING(step_ptr->job_ptr)) { */
	/* 	debug3("step completion %u.%u was received after job " */
	/* 	      "allocation is already completing, no extra NHC needed.", */
	/* 	      step_ptr->job_ptr->job_id, step_ptr->step_id); */
	/* 	other_step_finish(step_ptr); */
	/* 	/\* free resources on the job *\/ */
	/* 	post_job_step(step_ptr); */
	/* 	return SLURM_SUCCESS; */
	/* } */

	jobinfo->cleaning = 1;
	_spawn_cleanup_thread(step_ptr, _step_fini);

	return SLURM_SUCCESS;
}

extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
	return other_pack_select_info(last_query_time, show_flags, buffer_ptr,
				      protocol_version);
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;
	nodeinfo->other_nodeinfo = other_select_nodeinfo_alloc();

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		other_select_nodeinfo_free(nodeinfo->other_nodeinfo);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer, uint16_t protocol_version)
{
	int rc = SLURM_ERROR;
	rc = other_select_nodeinfo_pack(nodeinfo->other_nodeinfo,
					buffer, protocol_version);

	return rc;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo_pptr,
					   Buf buffer,
					   uint16_t protocol_version)
{
	int rc = SLURM_ERROR;
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	*nodeinfo_pptr = nodeinfo;

	nodeinfo->magic = NODEINFO_MAGIC;
	rc = other_select_nodeinfo_unpack(&nodeinfo->other_nodeinfo,
					  buffer, protocol_version);

	if (rc != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	select_p_select_nodeinfo_free(nodeinfo);
	*nodeinfo_pptr = NULL;

	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	return other_select_nodeinfo_set_all();
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	return other_select_nodeinfo_set(job_ptr);
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	int rc = SLURM_SUCCESS;
	select_nodeinfo_t **select_nodeinfo = (select_nodeinfo_t **) data;

	if (nodeinfo == NULL) {
		error("select/cray nodeinfo_get: nodeinfo not set");
		return SLURM_ERROR;
	}
	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("select/cray nodeinfo_get: nodeinfo magic bad");
		return SLURM_ERROR;
	}


	switch (dinfo) {
	case SELECT_NODEDATA_PTR:
		*select_nodeinfo = nodeinfo->other_nodeinfo;
		break;
	default:
		rc = other_select_nodeinfo_get(nodeinfo->other_nodeinfo,
					       dinfo, state, data);
		break;
	}
	return rc;
}

extern select_jobinfo_t *select_p_select_jobinfo_alloc(void)
{
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));
	jobinfo->magic = JOBINFO_MAGIC;
	jobinfo->other_jobinfo = other_select_jobinfo_alloc();

	return jobinfo;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;

	if (jobinfo == NULL) {
		error("select/cray jobinfo_set: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select/cray jobinfo_set: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_CLEANING:
		jobinfo->cleaning = *uint16;
		break;
	default:
		rc = other_select_jobinfo_set(jobinfo, data_type, data);
		break;
	}

	return rc;
}

extern int select_p_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
//	uint64_t *uint64 = (uint64_t *) data;
	select_jobinfo_t **select_jobinfo = (select_jobinfo_t **) data;

	if (jobinfo == NULL) {
		debug("select/cray jobinfo_get: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select/cray jobinfo_get: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_PTR:
		*select_jobinfo = jobinfo->other_jobinfo;
		break;
	case SELECT_JOBDATA_CLEANING:
		*uint16 = jobinfo->cleaning;
		break;
	default:
		rc = other_select_jobinfo_get(jobinfo, data_type, data);
		break;
	}

	return rc;
}

extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	struct select_jobinfo *rc = NULL;

	if (jobinfo == NULL)
		;
	else if (jobinfo->magic != JOBINFO_MAGIC)
		error("select/cray jobinfo_copy: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(struct select_jobinfo));
		rc->magic = JOBINFO_MAGIC;
	}
	return rc;
}

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (jobinfo) {
		if (jobinfo->magic != JOBINFO_MAGIC) {
			error("select/cray jobinfo_free: jobinfo magic bad");
			return EINVAL;
		}

		jobinfo->magic = 0;
		other_select_jobinfo_free(jobinfo->other_jobinfo);
		xfree(jobinfo);
	}

	return rc;
}

extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					uint16_t protocol_version)
{
	int rc = SLURM_ERROR;

	_select_jobinfo_pack(jobinfo, buffer, protocol_version);
	if (jobinfo)
		rc = other_select_jobinfo_pack(jobinfo->other_jobinfo, buffer,
					       protocol_version);
	else
		rc = other_select_jobinfo_pack(NULL, buffer, protocol_version);
	return rc;
}

extern int select_p_select_jobinfo_unpack(select_jobinfo_t **jobinfo_pptr,
					  Buf buffer, uint16_t protocol_version)
{
	int rc;
	select_jobinfo_t *jobinfo = NULL;

	rc = _select_jobinfo_unpack(jobinfo_pptr, buffer, protocol_version);

	if (rc != SLURM_SUCCESS)
		return SLURM_ERROR;

	jobinfo = *jobinfo_pptr;

	rc = other_select_jobinfo_unpack(&jobinfo->other_jobinfo,
					 buffer, protocol_version);

	if (rc != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	select_p_select_jobinfo_free(jobinfo);
	*jobinfo_pptr = NULL;

	return SLURM_ERROR;
}

extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{

	if (buf == NULL) {
		error("select/cray jobinfo_sprint: buf is null");
		return NULL;
	}

	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("select/cray jobinfo_sprint: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select/cray jobinfo_sprint: jobinfo bad");
			return NULL;
		}
		/* FIXME: in the future print out the header here (if needed) */
		/* snprintf(buf, size, "%s", header); */

		return buf;
	}

	switch (mode) {
	default:
		other_select_jobinfo_sprint(jobinfo->other_jobinfo, buf,
					    size, mode);
		break;
	}

	return buf;
}

extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	char *buf = NULL;

	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("select/cray jobinfo_xstrdup: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select/cray jobinfo_xstrdup: jobinfo bad");
			return NULL;
		}
		/* FIXME: in the future copy the header here (if needed) */
		/* xstrcat(buf, header); */

		return buf;
	}

	switch (mode) {
	/* See comment in select_p_select_jobinfo_sprint() regarding format. */
	default:
		xstrcat(buf, other_select_jobinfo_xstrdup(
				jobinfo->other_jobinfo, mode));
		break;
	}

	return buf;
}

extern int select_p_update_block(update_block_msg_t *block_desc_ptr)
{
	return other_update_block(block_desc_ptr);
}

extern int select_p_update_sub_node(update_block_msg_t *block_desc_ptr)
{
	return other_update_sub_node(block_desc_ptr);
}

extern int select_p_fail_cnode(struct step_record *step_ptr)
{
	return other_fail_cnode(step_ptr);
}

extern int select_p_get_info_from_plugin(enum select_jobdata_type info,
					 struct job_record *job_ptr,
					 void *data)
{
	return other_get_info_from_plugin(info, job_ptr, data);
}

extern int select_p_update_node_config(int index)
{
	return other_update_node_config(index);
}

extern int select_p_update_node_state(struct node_record *node_ptr)
{
	return other_update_node_state(node_ptr);
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return other_alter_node_cnt(type, data);
}

extern int select_p_reconfigure(void)
{
	debug_flags = slurm_get_debug_flags();
	return other_reconfigure();
}

extern bitstr_t * select_p_resv_test(bitstr_t *avail_bitmap, uint32_t node_cnt,
				     uint32_t *core_cnt, bitstr_t **core_bitmap)
{
	return other_resv_test(avail_bitmap, node_cnt, core_cnt, core_bitmap);
}

extern void select_p_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	int i, j, offset;
	int dims = slurmdb_setup_cluster_dims();

	if (select_cray_dim_size[0] == -1) {
		node_info_t *node_ptr;

		/* init the rest of the dim sizes. All current (2011)
		 * XT/XE installations have a maximum dimension of 3,
		 * smaller systems deploy a 2D Torus which has no
		 * connectivity in X-dimension.  Just incase they
		 * decide to change it where we only get 2 instead of
		 * 3 we will initialize it later. */
		for (i = 1; i < dims; i++)
			select_cray_dim_size[i] = -1;
		for (i = 0; i < node_info_ptr->record_count; i++) {
			node_ptr = &(node_info_ptr->node_array[i]);
			if (!node_ptr->node_addr ||
			    (strlen(node_ptr->node_addr) != dims))
				continue;
			for (j = 0; j < dims; j++) {
				offset = select_char2coord(
					node_ptr->node_addr[j]);
				select_cray_dim_size[j] =
					MAX((offset+1),
					    select_cray_dim_size[j]);
			}
		}
	}

	/*
	 * Override the generic setup of dim_size made in _setup_cluster_rec()
	 * FIXME: use a better way, e.g. encoding the 3-dim triplet as a
	 *        string which gets stored in a database (event_table?) entry.
	 */
	if (working_cluster_rec) {
		xfree(working_cluster_rec->dim_size);
		working_cluster_rec->dim_size = xmalloc(sizeof(int) * dims);
		for (j = 0; j < dims; j++)
			working_cluster_rec->dim_size[j] =
				select_cray_dim_size[j];
	}

	other_ba_init(node_info_ptr, sanity_check);
}

extern int *select_p_ba_get_dims(void)
{
	if (select_cray_dim_size[0] != -1)
		return select_cray_dim_size;
	return NULL;
}

extern void select_p_ba_fini(void)
{
	other_ba_fini();
}
