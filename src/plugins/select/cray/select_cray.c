/*****************************************************************************\
 *  select_cray.c - node selection plugin for cray systems.
 *****************************************************************************
 *  Copyright (C) 2013-2014 SchedMD LLC
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
#include <fcntl.h>

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/pack.h"
#include "src/slurmctld/locks.h"
#include "other_select.h"

#ifdef HAVE_NATIVE_CRAY
#include "alpscomm_sn.h"
#endif

/**
 * struct select_jobinfo - data specific to Cray node selection plugin
 * @magic:		magic number, must equal %JOBINFO_MAGIC
 * @other_jobinfo:	hook into attached, "other" node selection plugin.
 */
struct select_jobinfo {
	bitstr_t               *blade_map;
	uint16_t                cleaning;
	uint16_t		magic;
	uint8_t                 npc;
	select_jobinfo_t       *other_jobinfo;
	bitstr_t               *used_blades;
};
#define JOBINFO_MAGIC 0x86ad

/**
 * struct select_nodeinfo - data used for node information
 * @magic:		magic number, must equal %NODEINFO_MAGIC
 * @other_nodeinfo:	hook into attached, "other" node selection plugin.
 */
struct select_nodeinfo {
	uint32_t                blade_id;
	uint16_t		magic;
	uint32_t                nid;
	select_nodeinfo_t	*other_nodeinfo;
};

typedef struct {
	uint64_t apid;
	uint32_t exit_code;
	uint32_t jobid;
	char *nodelist;
	bool step;
	uint32_t user_id;
} nhc_info_t;

typedef struct {
	uint64_t id;
	uint32_t job_cnt;
	bitstr_t *node_bitmap;
} blade_info_t;

typedef enum {
	NPC_NONE, // Don't use network performance counters.
	NPC_SYS, // Use the system-wide network performance counters.
	NPC_BLADE, // NPC on a blade
} npc_type_t;

#define NODEINFO_MAGIC 0x85ad
#define MAX_PTHREAD_RETRIES  1

#define GET_BLADE_X(_X) \
	(int16_t)((_X & 0x0000ffff00000000) >> 32)
#define GET_BLADE_Y(_X) \
	(int16_t)((_X & 0x00000000ffff0000) >> 16)
#define GET_BLADE_Z(_X) \
	(int16_t)(_X & 0x000000000000ffff)

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
slurmdb_cluster_rec_t *working_cluster_rec  __attribute__((weak_import)) = NULL;
struct node_record *node_record_table_ptr __attribute__((weak_import));
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
#else
slurm_ctl_conf_t slurmctld_conf;
int bg_recover = NOT_FROM_CONTROLLER;
slurmdb_cluster_rec_t *working_cluster_rec = NULL;
struct node_record *node_record_table_ptr;
int node_record_count;
time_t last_node_update;
#endif

static blade_info_t *blade_array = NULL;
static bitstr_t *blade_nodes_running_npc = NULL;
static uint32_t blade_cnt = 0;
static pthread_mutex_t blade_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t last_npc_update;

static int active_post_nhc_cnt = 0;
static pthread_mutex_t throttle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t throttle_cond = PTHREAD_COND_INITIALIZER;

#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
static size_t topology_num_nodes = 0;
static alpsc_topology_t *topology = NULL;
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
#define AELD_EVENT_INTERVAL	110	// aeld event sending interval (ms)
#define AELD_LIST_CAPACITY	65536	// maximum list capacity

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
const uint32_t plugin_version	= 120;

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo);

static int _run_nhc(nhc_info_t *nhc_info)
{
#ifdef HAVE_NATIVE_CRAY
	int argc = 13, status = 1, wait_rc, i = 0;
	char *argv[argc];
	pid_t cpid;
	char *jobid_char = NULL, *apid_char = NULL, *nodelist_nids = NULL,
		*exit_char = NULL, *user_char = NULL;
	DEF_TIMERS;

	START_TIMER;

	apid_char = xstrdup_printf("%"PRIu64"", nhc_info->apid);
	exit_char = xstrdup_printf("%u", nhc_info->exit_code);
	jobid_char = xstrdup_printf("%u", nhc_info->jobid);
	user_char = xstrdup_printf("%u", nhc_info->user_id);
	nodelist_nids = cray_nodelist2nids(NULL, nhc_info->nodelist);

	argv[i++] = "/opt/cray/nodehealth/default/bin/xtcleanup_after";
	argv[i++] = "-a";
	argv[i++] = apid_char;
	argv[i++] = "-e";
	argv[i++] = exit_char;
	argv[i++] = "-r";
	argv[i++] = jobid_char;
	argv[i++] = "-u";
	argv[i++] = user_char;
	argv[i++] = "-m";
	argv[i++] = nhc_info->step ? "application" : "reservation";
	argv[i++] = nodelist_nids;
	argv[i++] = NULL;

	if (debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (nhc_info->step)
			info("Calling NHC for jobid %u and apid %"PRIu64" "
			     "on nodes %s(%s) exit code %u",
			     nhc_info->jobid, nhc_info->apid,
			     nhc_info->nodelist, nodelist_nids,
			     nhc_info->exit_code);
		else
			info("Calling NHC for jobid %u and apid %"PRIu64" "
			     "on nodes %s(%s)",
			     nhc_info->jobid, nhc_info->apid,
			     nhc_info->nodelist, nodelist_nids);
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
	} else if (debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("_run_nhc jobid %u and apid %"PRIu64" completed took: %s",
		     nhc_info->jobid, nhc_info->apid, TIME_STR);
	}
fini:
	xfree(apid_char);
	xfree(exit_char);
	xfree(jobid_char);
	xfree(user_char);
	xfree(nodelist_nids);

	return status;

#elif HAVE_CRAY_NETWORK
	/* NHC not supported, but don't sleep before return. */
	return 0;

#else
	if (debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("simluating calling NHC for jobid %u "
		     "and apid %"PRIu64" on nodes %s",
		     nhc_info->jobid, nhc_info->apid, nhc_info->nodelist);

	/* simulate sleeping */
	sleep(1);
	if (debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("_run_nhc jobid %u and apid %"PRIu64" completed",
		     nhc_info->jobid, nhc_info->apid);
	}

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

	// Fill in nodes and num_nodes if available
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
		} else if (*capacity >= AELD_LIST_CAPACITY) {
			debug("aeld list over capacity");
			return;
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

	// If there are no nodes, set_application_info will fail
	if (app.nodes == NULL || app.num_nodes == 0) {
		debug("Job %"PRIu32".%"PRIu32" has no nodes, skipping",
		      job_ptr->job_id, step_ptr->step_id);
		_free_event(&app);
		return;
	}

	pthread_mutex_lock(&aeld_mutex);

	// Add it to the event list, only if aeld is up
	if (aeld_running) {
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

static void _remove_job_from_blades(select_jobinfo_t *jobinfo)
{
	int i;

	slurm_mutex_lock(&blade_mutex);
	for (i=0; i<blade_cnt; i++) {
		if (!bit_test(jobinfo->blade_map, i))
			continue;
		blade_array[i].job_cnt--;
		if ((int32_t)blade_array[i].job_cnt < 0) {
			error("blade %d job_cnt underflow", i);
			blade_array[i].job_cnt = 0;
		}

		if (jobinfo->npc == NPC_SYS) {
			bit_nclear(blade_nodes_running_npc, 0,
				   node_record_count-1);
		} else if (jobinfo->npc) {
			bit_not(blade_nodes_running_npc);
			bit_or(blade_nodes_running_npc,
			       blade_array[i].node_bitmap);
			bit_not(blade_nodes_running_npc);
		}
	}

	if (jobinfo->npc)
		last_npc_update = time(NULL);

	slurm_mutex_unlock(&blade_mutex);
}

static void _remove_step_from_blades(struct step_record *step_ptr)
{
	select_jobinfo_t *jobinfo = step_ptr->job_ptr->select_jobinfo->data;
	select_jobinfo_t *step_jobinfo = step_ptr->select_jobinfo->data;

	if (jobinfo->used_blades) {
		bit_not(jobinfo->used_blades);
		bit_or(jobinfo->used_blades, step_jobinfo->blade_map);
		bit_not(jobinfo->used_blades);
	}
}

static void _free_blade(blade_info_t *blade_info)
{
	FREE_NULL_BITMAP(blade_info->node_bitmap);
}

static void _pack_blade(blade_info_t *blade_info, Buf buffer,
			uint16_t protocol_version)
{
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack64(blade_info->id, buffer);
		pack32(blade_info->job_cnt, buffer);
		pack_bit_str_hex(blade_info->node_bitmap, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64(blade_info->id, buffer);
		pack32(blade_info->job_cnt, buffer);
		pack_bit_str(blade_info->node_bitmap, buffer);
	}

}

static int _unpack_blade(blade_info_t *blade_info, Buf buffer,
			 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack64(&blade_info->id, buffer);
		safe_unpack32(&blade_info->job_cnt, buffer);
		unpack_bit_str_hex(&blade_info->node_bitmap, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&blade_info->id, buffer);
		safe_unpack32(&blade_info->job_cnt, buffer);
		unpack_bit_str(&blade_info->node_bitmap, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("Problem unpacking blade info");
	return SLURM_ERROR;

}

/* job_write and blade_mutex must be locked before calling */
static void _set_job_running(struct job_record *job_ptr)
{
	int i;
	select_jobinfo_t *jobinfo = job_ptr->select_jobinfo->data;
	select_nodeinfo_t *nodeinfo;

	for (i=0; i<node_record_count; i++) {
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;

		nodeinfo = node_record_table_ptr[i].select_nodeinfo->data;
		if (!bit_test(jobinfo->blade_map, nodeinfo->blade_id)) {
			bit_set(jobinfo->blade_map, nodeinfo->blade_id);

			blade_array[nodeinfo->blade_id].job_cnt++;

			if (jobinfo->npc == NPC_SYS) {
				bit_nset(blade_nodes_running_npc, 0,
					 node_record_count-1);
			} else if (jobinfo->npc)
				bit_or(blade_nodes_running_npc,
				       blade_array[nodeinfo->blade_id].
				       node_bitmap);
		}
	}

	if (jobinfo->npc)
		last_npc_update = time(NULL);
}

/* job_write and blade_mutex must be locked before calling */
static void _set_job_running_restore(select_jobinfo_t *jobinfo)
{
	int i;

	xassert(jobinfo);
	xassert(jobinfo->blade_map);

	for (i=0; i<blade_cnt; i++) {
		if (!bit_test(jobinfo->blade_map, i))
			continue;

		blade_array[i].job_cnt++;

		if (jobinfo->npc == NPC_SYS) {
			bit_nset(blade_nodes_running_npc, 0,
				 node_record_count-1);
		} else if (jobinfo->npc)
			bit_or(blade_nodes_running_npc,
			       blade_array[i].node_bitmap);
	}

	if (jobinfo->npc)
		last_npc_update = time(NULL);
}

/* These functions prevent the fini's of jobs and steps from keeping
 * the slurmctld write locks constantly set after the nhc is ran,
 * which can prevent other RPCs and system functions from being
 * processed. For example, a steady stream of step or job completions
 * can prevent squeue from responding or jobs from being scheduled. */
static void _throttle_start(void)
{
	slurm_mutex_lock(&throttle_mutex);
	while (1) {
		if (active_post_nhc_cnt == 0) {
			active_post_nhc_cnt++;
			break;
		}
		pthread_cond_wait(&throttle_cond, &throttle_mutex);
	}
	slurm_mutex_unlock(&throttle_mutex);
	usleep(100);
}
static void _throttle_fini(void)
{
	slurm_mutex_lock(&throttle_mutex);
	active_post_nhc_cnt--;
	pthread_cond_broadcast(&throttle_cond);
	slurm_mutex_unlock(&throttle_mutex);
}

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
	nhc_info.user_id = job_ptr->user_id;
	unlock_slurmctld(job_read_lock);

	/* run NHC */
	_run_nhc(&nhc_info);
	/***********/
	xfree(nhc_info.nodelist);

	_throttle_start();
	lock_slurmctld(job_write_lock);
	if (job_ptr->magic == JOB_MAGIC) {
		select_jobinfo_t *jobinfo = NULL;

		other_job_fini(job_ptr);

		jobinfo = job_ptr->select_jobinfo->data;

		_remove_job_from_blades(jobinfo);
		jobinfo->cleaning = 0;
	} else
		error("_job_fini: job %u had a bad magic, "
		      "this should never happen", nhc_info.jobid);

	unlock_slurmctld(job_write_lock);
	_throttle_fini();

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
	nhc_info.user_id = step_ptr->job_ptr->user_id;

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

	_throttle_start();
	lock_slurmctld(job_write_lock);
	if (!step_ptr->job_ptr) {
		error("For some reason we don't have a job_ptr for "
		      "APID %"PRIu64".  This should never happen.",
		      nhc_info.apid);
	} else if (!step_ptr->step_node_bitmap) {
		error("For some reason we don't have a step_node_bitmap "
		      "for APID %"PRIu64".  "
		      "If this is at startup and the step's nodes changed "
		      "this is expected.  Otherwise this should never happen.",
		      nhc_info.apid);

		/* This should be the only cleanup needed */
		jobinfo = step_ptr->select_jobinfo->data;

		_remove_step_from_blades(step_ptr);
		jobinfo->cleaning = 0;

		delete_step_record(step_ptr->job_ptr, step_ptr->step_id);
	} else {
		other_step_finish(step_ptr);

		jobinfo = step_ptr->select_jobinfo->data;

		_remove_step_from_blades(step_ptr);
		jobinfo->cleaning = 0;

		/* free resources on the job */
		post_job_step(step_ptr);
	}
	unlock_slurmctld(job_write_lock);
	_throttle_fini();

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
	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		if (!jobinfo) {
			pack_bit_str_hex(NULL, buffer);
			pack16(0, buffer);
			pack8(0, buffer);
			pack_bit_str_hex(NULL, buffer);
		} else {
			pack_bit_str_hex(jobinfo->blade_map, buffer);
			pack16(jobinfo->cleaning, buffer);
			pack8(jobinfo->npc, buffer);
			pack_bit_str_hex(jobinfo->used_blades, buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!jobinfo) {
			pack_bit_str(NULL, buffer);
			pack16(0, buffer);
			pack8(0, buffer);
			pack_bit_str(NULL, buffer);
		} else {
			pack_bit_str(jobinfo->blade_map, buffer);
			pack16(jobinfo->cleaning, buffer);
			pack8(jobinfo->npc, buffer);
			pack_bit_str(jobinfo->used_blades, buffer);
		}
	}
}

static int _select_jobinfo_unpack(select_jobinfo_t **jobinfo_pptr,
				  Buf buffer, uint16_t protocol_version)
{
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));

	*jobinfo_pptr = jobinfo;

	jobinfo->magic = JOBINFO_MAGIC;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		unpack_bit_str_hex(&jobinfo->blade_map, buffer);
		safe_unpack16(&jobinfo->cleaning, buffer);
		safe_unpack8(&jobinfo->npc, buffer);
		unpack_bit_str_hex(&jobinfo->used_blades, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		unpack_bit_str(&jobinfo->blade_map, buffer);
		safe_unpack16(&jobinfo->cleaning, buffer);
		safe_unpack8(&jobinfo->npc, buffer);
		unpack_bit_str(&jobinfo->used_blades, buffer);
	}

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
	int i;

	slurm_mutex_lock(&blade_mutex);

	FREE_NULL_BITMAP(blade_nodes_running_npc);

	for (i=0; i<blade_cnt; i++)
		_free_blade(&blade_array[i]);
	xfree(blade_array);

#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	if (topology)
		free(topology);
#endif

	slurm_mutex_unlock(&blade_mutex);

	return other_select_fini();
}

/*
 * The remainder of this file implements the standard SLURM
 * node selection API.
 */

extern int select_p_state_save(char *dir_name)
{
	int error_code = 0, log_fd, i;
	char *old_file, *new_file, *reg_file;
	Buf buffer = init_buf(BUF_SIZE);

	DEF_TIMERS;

	debug("cray: select_p_state_save");
	START_TIMER;
	/* write header: time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);

	slurm_mutex_lock(&blade_mutex);

	pack32(blade_cnt, buffer);

	/* write blade records to buffer */
	for (i=0; i<blade_cnt; i++)
		_pack_blade(&blade_array[i], buffer, SLURM_PROTOCOL_VERSION);

	slurm_mutex_unlock(&blade_mutex);

	/* write the buffer to file */
	slurm_conf_lock();
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/blade_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/blade_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/blade_state.new");
	slurm_conf_unlock();

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);

		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}

	if (error_code) {
		(void) unlink(new_file);
	} else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);
	END_TIMER2("select_p_state_save");

	return other_state_save(dir_name);
}

extern int select_p_state_restore(char *dir_name)
{
	int state_fd, i;
	char *state_file = NULL;
	Buf buffer = NULL;
	char *data = NULL;
	int data_size = 0;
	int data_allocated, data_read = 0;
	uint16_t protocol_version = (uint16_t)NO_VAL;
	uint32_t record_count;

	debug("cray: select_p_state_restore");

	static time_t last_config_update = (time_t) 0;

	/* only run on startup */
	if (last_config_update)
		return SLURM_SUCCESS;

	last_config_update = time(NULL);

	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/blade_state");
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		error("No blade state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);
	safe_unpack16(&protocol_version, buffer);
	debug3("Version in blade_state header is %u", protocol_version);

	if (protocol_version == (uint16_t)NO_VAL) {
		error("***********************************************");
		error("Can not recover blade state, "
		      "data version incompatible");
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}

	slurm_mutex_lock(&blade_mutex);

	safe_unpack32(&record_count, buffer);

	if (record_count != blade_cnt)
		error("For some reason we have a different blade_cnt than we "
		      "did before, this may cause issue.  Got %u expecting %u.",
		      record_count, blade_cnt);

	for (i=0; i<record_count; i++) {
		blade_info_t blade_info;

		memset(&blade_info, 0, sizeof(blade_info_t));

		if (_unpack_blade(&blade_info, buffer, protocol_version))
			goto unpack_error;
		if (blade_info.id == blade_array[i].id) {
			//blade_array[i].job_cnt = blade_info.job_cnt;
			if (!bit_equal(blade_array[i].node_bitmap,
				       blade_info.node_bitmap))
				error("Blade %"PRIu64"(%d %d %d) "
				      "has changed it's nodes!  "
				      "Unexpected results could "
				      "happen if jobs are running!",
				      blade_info.id,
				      GET_BLADE_X(blade_info.id),
				      GET_BLADE_Y(blade_info.id),
				      GET_BLADE_Z(blade_info.id));
		} else {
			int j;
			for (j=0; j<blade_cnt; j++) {
				if (blade_info.id == blade_array[j].id) {
					/* blade_array[j].job_cnt = */
					/* 	blade_info.job_cnt; */
					if (!bit_equal(blade_array[j].
						       node_bitmap,
						       blade_info.node_bitmap))
						error("Blade %"PRIu64"(%d "
						      "%d %d) "
						      "has changed it's "
						      "nodes!  "
						      "Unexpected results "
						      "could "
						      "happen if jobs are "
						      "running!",
						      blade_info.id,
						      GET_BLADE_X(
							      blade_info.id),
						      GET_BLADE_Y(
							      blade_info.id),
						      GET_BLADE_Z(
							      blade_info.id));
					break;
				}
			}
			error("Blade %"PRIu64"(%d %d %d) "
			      "is no longer at location %d, but at %d!  "
			      "Unexpected results could "
			      "happen if jobs are running!",
			      blade_info.id,
			      GET_BLADE_X(blade_info.id),
			      GET_BLADE_Y(blade_info.id),
			      GET_BLADE_Z(blade_info.id),
			      i, j);
		}
		_free_blade(&blade_info);
	}
	slurm_mutex_unlock(&blade_mutex);

	free_buf(buffer);

	return other_state_restore(dir_name);

unpack_error:
	slurm_mutex_unlock(&blade_mutex);

	error("Incomplete blade data checkpoint file, you may get "
	      "unexpected issues if jobs were running.");
	free_buf(buffer);
	/* Since this is more of a sanity check continue without FAILURE. */
	return SLURM_SUCCESS;
}

extern int select_p_job_init(List job_list)
{
	static bool run_already = false;

	/* Execute only on initial startup. */
	if (run_already)
		return other_job_init(job_list);

	run_already = true;

	slurm_mutex_lock(&blade_mutex);
	if (job_list && list_count(job_list)) {
		ListIterator itr = list_iterator_create(job_list);
		struct job_record *job_ptr;
		select_jobinfo_t *jobinfo;

		if (debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("select_p_job_init: syncing jobs");

		while ((job_ptr = list_next(itr))) {

			jobinfo = job_ptr->select_jobinfo->data;
			if (jobinfo->cleaning || IS_JOB_RUNNING(job_ptr))
				_set_job_running_restore(jobinfo);

			/* We need to resize bitmaps if the
			 * blades change in count.  We must increase
			 * the size so loops based off blade_cnt will
			 * work correctly.
			 */
			if (jobinfo->blade_map
			    && (bit_size(jobinfo->blade_map) < blade_cnt))
				jobinfo->blade_map = bit_realloc(
					jobinfo->blade_map, blade_cnt);
			if (jobinfo->used_blades
			    && (bit_size(jobinfo->used_blades) < blade_cnt))
				jobinfo->used_blades = bit_realloc(
					jobinfo->used_blades, blade_cnt);

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

			if (!(slurmctld_conf.select_type_param & CR_NHC_NO)) {
				jobinfo = job_ptr->select_jobinfo->data;
				if (jobinfo && jobinfo->cleaning)
					_spawn_cleanup_thread(
						job_ptr, _job_fini);
			}
		}
		list_iterator_destroy(itr);
	}

	slurm_mutex_unlock(&blade_mutex);

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
	select_nodeinfo_t *nodeinfo = NULL;
	struct node_record *node_rec;
	int i, j;
	uint64_t blade_id = 0;

#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
	int nn, end_nn, last_nn = 0;
	bool found = 0;
	char *err_msg = NULL;

	if (!topology) {
		if (alpsc_get_topology(&err_msg, &topology,
				       &topology_num_nodes)) {
			if (err_msg) {
				error("(%s: %d: %s) Could not get system "
				      "topology info: %s",
				      THIS_FILE, __LINE__,
				      __FUNCTION__, err_msg);
				free(err_msg);
			} else {
				error("(%s: %d: %s) Could not get system "
				      "topology info: No error "
				      "message present.",
				      THIS_FILE, __LINE__, __FUNCTION__);
			}
			return SLURM_ERROR;
		}
	}

#endif

	slurm_mutex_lock(&blade_mutex);

	if (!blade_array)
		blade_array = xmalloc(sizeof(blade_info_t) * node_cnt);

	if (!blade_nodes_running_npc)
		blade_nodes_running_npc = bit_alloc(node_cnt);

	for (i = 0; i < node_cnt; i++) {
		node_rec = &node_ptr[i];
		if (!node_rec->select_nodeinfo)
			node_rec->select_nodeinfo =
				select_g_select_nodeinfo_alloc();
		nodeinfo = node_rec->select_nodeinfo->data;
		if (nodeinfo->nid == NO_VAL) {
			char *nid_char;

			if (!(nid_char = strpbrk(node_rec->name,
						 "0123456789"))) {
				error("(%s: %d: %s) Error: Node was not "
				      "recognizable: %s",
				      THIS_FILE, __LINE__, __FUNCTION__,
				      node_rec->name);
				slurm_mutex_unlock(&blade_mutex);
				return SLURM_ERROR;
			}

			nodeinfo->nid = atoll(nid_char);
		}

#if defined(HAVE_NATIVE_CRAY_GA) && !defined(HAVE_CRAY_NETWORK)
		end_nn = topology_num_nodes;

	start_again:

		for (nn = last_nn; nn < end_nn; nn++) {
			if (topology[nn].nid == nodeinfo->nid) {
				found = 1;
				blade_id = topology[nn].x;
				blade_id <<= 16;
				blade_id += topology[nn].y;
				blade_id <<= 16;
				blade_id += topology[nn].z;
				last_nn = nn;
				break;
			}
		}
		if (end_nn != topology_num_nodes) {
			/* already looped */
			fatal("Node %s(%d) isn't found on the system",
			      node_ptr->name, nodeinfo->nid);
		} else if (!found) {
			end_nn = last_nn;
			last_nn = 0;
			debug2("starting again looking for %s(%u)",
			       node_ptr->name, nodeinfo->nid);
			goto start_again;
		}
#else
		blade_id = nodeinfo->nid % 4; /* simulate 4 blades
					       * round robin style */
#endif
		for (j = 0; j < blade_cnt; j++)
			if (blade_array[j].id == blade_id)
				break;

		nodeinfo->blade_id = j;

		if (j == blade_cnt) {
			blade_cnt++;
			blade_array[j].node_bitmap = bit_alloc(node_cnt);
		}

		bit_set(blade_array[j].node_bitmap, i);
		blade_array[j].id = blade_id;

		debug2("got %s(%u) blade %u %"PRIu64" %"PRIu64" %d %d %d",
		       node_rec->name, nodeinfo->nid, nodeinfo->blade_id,
		       blade_id, blade_array[nodeinfo->blade_id].id,
		       GET_BLADE_X(blade_array[nodeinfo->blade_id].id),
		       GET_BLADE_Y(blade_array[nodeinfo->blade_id].id),
		       GET_BLADE_Z(blade_array[nodeinfo->blade_id].id));
	}
	/* give back the memory */
	xrealloc(blade_array, sizeof(blade_info_t) * blade_cnt);

	slurm_mutex_unlock(&blade_mutex);

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
	select_jobinfo_t *jobinfo = job_ptr->select_jobinfo->data;
	slurm_mutex_lock(&blade_mutex);

	if (jobinfo->npc != NPC_NONE) {
		/* If looking for network performance counters unmark
		   all the nodes that are in use since they cannot be used.
		*/
		if (mode != SELECT_MODE_TEST_ONLY) {
			if (jobinfo->npc == NPC_SYS) {
				/* All the nodes have to be free of
				 * network performance counters to run
				 * NPC_SYS.
				 */
				if (bit_ffs(blade_nodes_running_npc) != -1)
					bit_nclear(bitmap, 0,
						   bit_size(bitmap) - 1);
			} else {
				bit_not(blade_nodes_running_npc);
				bit_and(bitmap, blade_nodes_running_npc);
				bit_not(blade_nodes_running_npc);
			}
		}
	}

	/* char *tmp = bitmap2node_name(bitmap); */
	/* char *tmp3 = bitmap2node_name(blade_nodes_running_npc); */

	/* info("trying %u on %s '%s'", job_ptr->job_id, tmp, tmp3); */
	/* xfree(tmp); */
	/* xfree(tmp3); */
	slurm_mutex_unlock(&blade_mutex);

	return other_job_test(job_ptr, bitmap, min_nodes, max_nodes,
			      req_nodes, mode, preemptee_candidates,
			      preemptee_job_list, exc_core_bitmap);
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	select_jobinfo_t *jobinfo;

	xassert(job_ptr);
	xassert(job_ptr->select_jobinfo);
	xassert(job_ptr->select_jobinfo->data);

	jobinfo = job_ptr->select_jobinfo->data;

	slurm_mutex_lock(&blade_mutex);

	if (!jobinfo->blade_map)
		jobinfo->blade_map = bit_alloc(blade_cnt);

	_set_job_running(job_ptr);

	/* char *tmp3 = bitmap2node_name(blade_nodes_running_npc); */

	/* info("adding %u '%s'", job_ptr->job_id, tmp3); */
	/* xfree(tmp3); */
	slurm_mutex_unlock(&blade_mutex);

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

	if (jobinfo->cleaning == 1)
		error("Cleaning flag already set for job %u, "
		      "this should never happen", job_ptr->job_id);
	else {
		jobinfo->cleaning = 1;
		_spawn_cleanup_thread(job_ptr, _job_fini);
	}

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
					  select_jobinfo_t *step_jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	select_jobinfo_t *jobinfo;

	xassert(avail_nodes);
	xassert(!*avail_nodes);

	jobinfo = job_ptr->select_jobinfo->data;
	xassert(jobinfo);

	if (jobinfo->used_blades) {
		int i;

		*avail_nodes = bit_copy(job_ptr->node_bitmap);
		bit_not(*avail_nodes);

		slurm_mutex_lock(&blade_mutex);
		for (i=0; i<blade_cnt; i++) {
			if (!bit_test(jobinfo->used_blades, i))
				continue;

			bit_or(*avail_nodes, blade_array[i].node_bitmap);
		}
		slurm_mutex_unlock(&blade_mutex);

		bit_not(*avail_nodes);
	}

	return other_step_pick_nodes(job_ptr, jobinfo, node_count, avail_nodes);
}

extern int select_p_step_start(struct step_record *step_ptr)
{
	select_jobinfo_t *jobinfo;

#ifdef HAVE_NATIVE_CRAY
	if (aeld_running) {
		_update_app(step_ptr->job_ptr, step_ptr, ALPSC_EV_START);
	}
#endif

	jobinfo = step_ptr->job_ptr->select_jobinfo->data;
	if (jobinfo->npc) {
		int i;
		select_jobinfo_t *step_jobinfo = step_ptr->select_jobinfo->data;
		select_nodeinfo_t *nodeinfo;

		step_jobinfo->npc = jobinfo->npc;

		if (!jobinfo->used_blades)
			jobinfo->used_blades = bit_alloc(blade_cnt);

		if (!step_jobinfo->blade_map)
			step_jobinfo->blade_map = bit_alloc(blade_cnt);

		for (i=0; i<node_record_count; i++) {
			if (!bit_test(step_ptr->step_node_bitmap, i))
				continue;

			nodeinfo = node_record_table_ptr[i].
				select_nodeinfo->data;
			if (!bit_test(step_jobinfo->blade_map,
				      nodeinfo->blade_id))
				bit_set(step_jobinfo->blade_map,
					nodeinfo->blade_id);
		}
		bit_or(jobinfo->used_blades, step_jobinfo->blade_map);
	}
	return other_step_start(step_ptr);
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

#if 0
	/* The NHC needs to be ran after each step even if the job is about to
	 * run the NHC for the allocation.  The NHC developers feel this is
	 * needed.  If it ever changes just use this below code. */
	else if (IS_JOB_COMPLETING(step_ptr->job_ptr) ||
		 IS_JOB_FINISHED(step_ptr->job_ptr)) {
		debug3("step completion %u.%u was received after job "
		      "allocation is already completing, no extra NHC needed.",
		      step_ptr->job_ptr->job_id, step_ptr->step_id);
		other_step_finish(step_ptr);
		/* free resources on the job */
		post_job_step(step_ptr);
		return SLURM_SUCCESS;
	}
#endif

	if (jobinfo->cleaning == 1) {
		error("Cleaning flag already set for job step %u.%u, "
		      "this should never happen.",
		      step_ptr->step_id, step_ptr->job_ptr->job_id);
	} else {
		jobinfo->cleaning = 1;
		_spawn_cleanup_thread(step_ptr, _step_fini);
	}

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
	nodeinfo->nid   = NO_VAL;
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
	int i;
	static time_t last_set_all = 0;

	/* only set this once when the last_bg_update is newer than
	   the last time we set things up. */
	if (last_set_all && (last_npc_update-1 < last_set_all)) {
		debug3("Node select info for set all hasn't "
		       "changed since %ld",
		       last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_npc_update;

	/* set this here so we know things have changed */
	last_node_update = time(NULL);

	slurm_mutex_lock(&blade_mutex);
	/* clear all marks */
	for (i=0; i<node_record_count; i++) {
		struct node_record *node_ptr = &(node_record_table_ptr[i]);
		if (bit_test(blade_nodes_running_npc, i))
			node_ptr->node_state |= NODE_STATE_NET;
		else
			node_ptr->node_state &= (~NODE_STATE_NET);
	}

	slurm_mutex_unlock(&blade_mutex);

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

	if (blade_cnt)
		jobinfo->blade_map = bit_alloc(blade_cnt);

	jobinfo->other_jobinfo = other_select_jobinfo_alloc();

	return jobinfo;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	char *in_char = (char *) data;

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
	case SELECT_JOBDATA_NETWORK:
		if (!in_char || !strlen(in_char)
		    || !strcmp(in_char, "none"))
			jobinfo->npc = NPC_NONE;
		else if (!strcmp(in_char, "system"))
			jobinfo->npc = NPC_SYS;
		else if (!strcmp(in_char, "blade"))
			jobinfo->npc = NPC_BLADE;
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
	char **in_char = (char **) data;
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
	case SELECT_JOBDATA_NETWORK:
		xassert(in_char);
		switch (jobinfo->npc) {
		case NPC_NONE:
			*in_char = "none";
			break;
		case NPC_SYS:
			*in_char = "system";
			break;
		case NPC_BLADE:
			*in_char = "blade";
			break;
		default:
			*in_char = "unknown";
			break;
		}
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
		FREE_NULL_BITMAP(jobinfo->blade_map);
		FREE_NULL_BITMAP(jobinfo->used_blades);
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

extern int select_p_get_info_from_plugin(enum select_plugindata_info dinfo,
					 struct job_record *job_ptr,
					 void *data)
{
	return other_get_info_from_plugin(dinfo, job_ptr, data);
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

extern bitstr_t * select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt,
				     bitstr_t *avail_bitmap,
				     bitstr_t **core_bitmap)
{
	return other_resv_test(resv_desc_ptr, node_cnt,
			       avail_bitmap, core_bitmap);
}

extern void select_p_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	other_ba_init(node_info_ptr, sanity_check);
}

extern int *select_p_ba_get_dims(void)
{
	return NULL;
}

extern void select_p_ba_fini(void)
{
	other_ba_fini();
}

extern bitstr_t *select_p_ba_cnodelist2bitmap(char *cnodelist)
{
	return other_ba_cnodelist2bitmap(cnodelist);
}
