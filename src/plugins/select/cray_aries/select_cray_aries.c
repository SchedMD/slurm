/*****************************************************************************\
 *  select_cray_aries.c - node selection plugin for cray systems with Aries.
 *****************************************************************************
 *  Copyright (C) 2013-2014 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
 *  Written by Danny Auble <da@schedmd.com>
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

#define _GNU_SOURCE 1

#include <stdio.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/locks.h"
#include "other_select.h"
#include "ccm.h"

#ifdef HAVE_NATIVE_CRAY
#include "alpscomm_sn.h"
#endif

#define CLEANING_INIT		0x0000
#define CLEANING_STARTED	0x0001
#define CLEANING_COMPLETE	0x0002

#define IS_CLEANING_INIT(_X)		(_X->cleaning == CLEANING_INIT)
#define IS_CLEANING_STARTED(_X)		(_X->cleaning & CLEANING_STARTED)
#define IS_CLEANING_COMPLETE(_X)	(_X->cleaning & CLEANING_COMPLETE)

/**
 * struct select_jobinfo - data specific to Cray node selection plugin
 * @magic:		magic number, must equal %JOBINFO_MAGIC
 * @other_jobinfo:	hook into attached, "other" node selection plugin.
 */
struct select_jobinfo {
	bitstr_t               *blade_map;
	bool                    killing; /* (NO NEED TO PACK) used on
					    a step to signify it being killed */
	uint16_t                released;
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
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));
extern slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
extern slurmdb_cluster_rec_t *working_cluster_rec  __attribute__((weak_import));
extern struct node_record *node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern time_t last_node_update __attribute__((weak_import));
extern int slurmctld_primary __attribute__((weak_import));
extern void *acct_db_conn  __attribute__((weak_import));
extern bool ignore_state_errors __attribute__((weak_import));
#else
slurmctld_config_t slurmctld_config;
slurm_ctl_conf_t slurmctld_conf;
slurmdb_cluster_rec_t *working_cluster_rec = NULL;
struct node_record *node_record_table_ptr;
int node_record_count;
time_t last_node_update;
int slurmctld_primary;
void *acct_db_conn = NULL;
bool ignore_state_errors = true;
#endif

static blade_info_t *blade_array = NULL;
static bitstr_t *blade_nodes_running_npc = NULL;
static uint32_t blade_cnt = 0;
static pthread_mutex_t blade_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t last_npc_update;

static bool scheduling_disabled = false; //Backup running on external cray node?

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
static size_t topology_num_nodes = 0;
static alpsc_topology_t *topology = NULL;
#endif

#ifdef HAVE_NATIVE_CRAY

/* Used for aeld communication */
static alpsc_ev_app_t *app_list = NULL;	  // List of running/suspended apps
static int32_t app_list_size = 0;	  // Number of running/suspended apps
static size_t app_list_capacity = 0;	  // Capacity of app list
static alpsc_ev_app_t *event_list = NULL; // List of app state changes
static int32_t event_list_size = 0;	  // Number of events
static size_t event_list_capacity = 0;	  // Capacity of event list

// 0 if the aeld thread has exited
// 1 if the session is temporarily down
// 2 if the session is running
static volatile sig_atomic_t aeld_running = 0;

// Mutex for the above
static pthread_mutex_t aeld_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t aeld_thread;

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
			      struct step_record *step_ptr,
			      alpsc_ev_app_state_e state);
static void _copy_event(alpsc_ev_app_t *dest, alpsc_ev_app_t *src);
static void _free_event(alpsc_ev_app_t *event);
static void _add_to_app_list(alpsc_ev_app_t **list, int32_t *size,
			     size_t *capacity, alpsc_ev_app_t *app);
static void _update_app(struct step_record *step_ptr,
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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]	= "Cray/Aries node selection plugin";
const char plugin_type[]	= "select/cray_aries";
uint32_t plugin_id		= SELECT_PLUGIN_CRAY_LINEAR;
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo);

#ifdef HAVE_NATIVE_CRAY
/*
 * Clean up after a fatal error
 */
static void _aeld_cleanup(void)
{
	// Free any used memory
	slurm_mutex_lock(&aeld_mutex);
	_clear_event_list(app_list, &app_list_size);
	app_list_capacity = 0;
	xfree(app_list);
	_clear_event_list(event_list, &event_list_size);
	event_list_capacity = 0;
	xfree(event_list);
	slurm_mutex_unlock(&aeld_mutex);

	aeld_running = 0;
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
	static time_t start_time = (time_t) 0;
	static int start_count = 0;
	time_t now;
	int rv;
	char *errmsg;

	while (1) {
		slurm_mutex_lock(&aeld_mutex);

		// Create the session
		rv = alpsc_ev_create_session(&errmsg, session, app_list,
					     app_list_size);

		slurm_mutex_unlock(&aeld_mutex);

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

	now = time(NULL);
	if (start_time != now) {
		start_time = now;
		start_count = 1;
	} else if (++start_count == 3) {
		error("%s: aeld connection restart exceed threshold, find and "
		      "remove other program using the aeld socket, likely "
		      "another slurmctld instance", __func__);
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
	DEF_TIMERS;

	debug("cray: %s", __func__);

	aeld_running = 1;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	// Start out by creating a session
	_start_session(&session, &sessionfd);

	// Now poll on the session fd
	fds[0].fd = sessionfd;
	fds[0].events = POLLIN | POLLPRI | POLLRDHUP;
	while ((rv = TEMP_FAILURE_RETRY(poll(fds, 1, AELD_EVENT_INTERVAL)))
	       != -1) {
		START_TIMER;

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
		slurm_mutex_lock(&aeld_mutex);
		if (event_list_size > 0) {
			// Send event list to aeld
			rv = alpsc_ev_set_application_info(&errmsg, session,
							   event_list,
							   event_list_size);

			// Clear the event list
			_clear_event_list(event_list, &event_list_size);
			slurm_mutex_unlock(&aeld_mutex);
			/*
			 * For this application info call, some errors do
			 * not require exiting the current session and
			 * creating a new session.
			 */
			if (rv > 2) {
				_handle_aeld_error(
					"alpsc_ev_set_application_info",
					errmsg, rv, &session);
				_start_session(&session, &sessionfd);
				fds[0].fd = sessionfd;
			} else if ((rv == 1) || (rv == 2)) {
				error("alpsc_ev_set_application_info rv %d, %s",
				      rv, errmsg);
			}
		} else {
			slurm_mutex_unlock(&aeld_mutex);
		}
		if (debug_flags & DEBUG_FLAG_TIME_CRAY)
			END_TIMER3("_aeld_event_loop: took", 20000);
	}

	error("%s: poll failed: %m", __func__);
	_aeld_cleanup();

	return NULL;
}

/*
 * Initialize an alpsc_ev_app_t
 */
static void _initialize_event(alpsc_ev_app_t *event,
			      struct step_record *step_ptr,
			      alpsc_ev_app_state_e state)
{
	struct job_record *job_ptr = step_ptr->job_ptr;
	hostlist_t hl = NULL;
	hostlist_iterator_t hlit;
	char *node;
	int rv;
	uint32_t jobid;

	DEF_TIMERS;

	START_TIMER;

	if (job_ptr->pack_job_id && (job_ptr->pack_job_id != NO_VAL))
		jobid = job_ptr->pack_job_id;
	else
		jobid = job_ptr->job_id;

	event->apid = SLURM_ID_HASH(jobid, step_ptr->step_id);
	event->uid = job_ptr->user_id;
	event->app_name = xstrdup(step_ptr->name);
	event->batch_id = xstrdup_printf("%u", job_ptr->job_id);
	event->state = state;
	event->nodes = NULL;
	event->num_nodes = 0;

	// Fill in nodes and num_nodes if available
	if (step_ptr->step_layout)
		hl = hostlist_create(step_ptr->step_layout->node_list);
	else if ((step_ptr->step_id == SLURM_EXTERN_CONT) &&
		 job_ptr->job_resrcs)
		hl = hostlist_create(job_ptr->job_resrcs->nodes);

	if (!hl)
		return;

	hlit = hostlist_iterator_create(hl);
	if (!hlit) {
		hostlist_destroy(hl);
		return;
	}

	event->nodes = xcalloc(hostlist_count(hl), sizeof(int32_t));

	while ((node = hostlist_next(hlit))) {
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

	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

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
		dest->nodes = xcalloc(src->num_nodes, sizeof(int32_t));
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
static void _update_app(struct step_record *step_ptr,
			alpsc_ev_app_state_e state)
{
	struct job_record *job_ptr = step_ptr->job_ptr;
	uint64_t apid;
	int32_t i;
	alpsc_ev_app_t app;
	int found;
	uint32_t jobid;

	DEF_TIMERS;

	START_TIMER;

	// If aeld thread isn't running, do nothing
	if (aeld_running == 0) {
		return;
	}

	// Fill in the new event
	_initialize_event(&app, step_ptr, state);

	// If there are no nodes, set_application_info will fail
	if ((app.nodes == NULL) || (app.num_nodes == 0) ||
	    (app.app_name == NULL) || ((app.app_name)[0] == '\0')) {
		debug("%pS has no nodes or app name, skipping", step_ptr);
		_free_event(&app);
		return;
	}

	slurm_mutex_lock(&aeld_mutex);

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

		if (job_ptr->pack_job_id && (job_ptr->pack_job_id != NO_VAL))
			jobid = job_ptr->pack_job_id;
		else
			jobid = job_ptr->job_id;

		apid = SLURM_ID_HASH(jobid, step_ptr->step_id);
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
		if (job_ptr->pack_job_id && (job_ptr->pack_job_id != NO_VAL))
			jobid = job_ptr->pack_job_id;
		else
			jobid = job_ptr->job_id;

		apid = SLURM_ID_HASH(jobid, step_ptr->step_id);
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

	slurm_mutex_unlock(&aeld_mutex);

	_free_event(&app);

	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return;
}

static void _start_aeld_thread(void)
{
	if (scheduling_disabled)
		return;

	debug("cray: %s", __func__);

	// Spawn the aeld thread, only in slurmctld.
	if (!aeld_running && run_in_daemon("slurmctld")) {
		aeld_running = 1;
		slurm_thread_create(&aeld_thread, _aeld_event_loop, NULL);
	}
}

static void _stop_aeld_thread(void)
{
	if (scheduling_disabled)
		return;

	debug("cray: %s", __func__);

	_aeld_cleanup();

	pthread_cancel(aeld_thread);
	pthread_join(aeld_thread, NULL);
}
#endif

static void _free_blade(blade_info_t *blade_info)
{
	FREE_NULL_BITMAP(blade_info->node_bitmap);
}

static void _pack_blade(blade_info_t *blade_info, Buf buffer,
			uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64(blade_info->id, buffer);
		pack32(blade_info->job_cnt, buffer);
		pack_bit_str_hex(blade_info->node_bitmap, buffer);
	}

}

static int _unpack_blade(blade_info_t *blade_info, Buf buffer,
			 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&blade_info->id, buffer);
		safe_unpack32(&blade_info->job_cnt, buffer);
		unpack_bit_str_hex(&blade_info->node_bitmap, buffer);
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

	for (i = 0; i < node_record_count; i++) {
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

static void _select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
				 uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
	}
}

static int _select_jobinfo_unpack(select_jobinfo_t **jobinfo_pptr,
				  Buf buffer, uint16_t protocol_version)
{
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));

	*jobinfo_pptr = jobinfo;

	jobinfo->magic = JOBINFO_MAGIC;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		unpack_bit_str_hex(&jobinfo->blade_map, buffer);
		safe_unpack16(&jobinfo->cleaning, buffer);
		safe_unpack8(&jobinfo->npc, buffer);
		unpack_bit_str_hex(&jobinfo->used_blades, buffer);
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
	DEF_TIMERS;
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	char *err_msg = NULL;
#endif

	/*
	 * We must call the API here since we call this from other
	 * things other than the slurmctld.
	 */
	other_select_type_param = slurm_get_select_type_param();

	if (other_select_type_param & CR_OTHER_CONS_RES)
		plugin_id = SELECT_PLUGIN_CRAY_CONS_RES;
	else if (other_select_type_param & CR_OTHER_CONS_TRES)
		plugin_id = SELECT_PLUGIN_CRAY_CONS_TRES;
	else
		plugin_id = SELECT_PLUGIN_CRAY_LINEAR;

	debug_flags = slurm_get_debug_flags();

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	/* Read and store the CCM configured partition name(s). */
	if (run_in_daemon("slurmctld")) {
		/* Get any CCM configuration information */
		ccm_get_config();
	}
#endif
	if (run_in_daemon("slurmctld") && !slurmctld_primary) {
		START_TIMER;
		if (slurmctld_config.scheduling_disabled) {
			info("Scheduling disabled on backup");
			scheduling_disabled = true;
		}

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
		else if (alpsc_get_topology(&err_msg, &topology,
					    &topology_num_nodes))
			fatal("Running backup on an external node requires "
			      "the \"no_backup_scheduling\" "
			      "SchedulerParameter.");
#endif
		END_TIMER;
		if (debug_flags & DEBUG_FLAG_TIME_CRAY)
			info("alpsc_get_topology call took: %s", TIME_STR);
	}

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

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	if (topology)
		free(topology);
#endif

	slurm_mutex_unlock(&blade_mutex);

	return other_select_fini();
}

/*
 * The remainder of this file implements the standard Slurm
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
	old_file = xstrdup(dir_name);
	xstrcat(old_file, "/blade_state.old");
	reg_file = xstrdup(dir_name);
	xstrcat(reg_file, "/blade_state");
	new_file = xstrdup(dir_name);
	xstrcat(new_file, "/blade_state.new");

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

#ifdef HAVE_NATIVE_CRAY
	if (slurmctld_config.shutdown_time)
		_stop_aeld_thread();
#endif

	END_TIMER2("select_p_state_save");

	return other_state_save(dir_name);
}

extern int select_p_state_restore(char *dir_name)
{
	static time_t last_config_update = (time_t) 0;
	int i;
	char *state_file = NULL;
	Buf buffer = NULL;
	uint16_t protocol_version = NO_VAL16;
	uint32_t record_count;

	if (scheduling_disabled)
		return SLURM_SUCCESS;

	debug("cray: select_p_state_restore");

	/* only run on startup */
	if (last_config_update)
		return SLURM_SUCCESS;

	last_config_update = time(NULL);

	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/blade_state");
	if (!(buffer = create_mmap_buf(state_file))) {
		error("No blade state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	}
	xfree(state_file);

	safe_unpack16(&protocol_version, buffer);
	debug3("Version in blade_state header is %u", protocol_version);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover blade state, data version incompatible, start with '-i' to ignore this");
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
		if (!blade_info.node_bitmap) {
			error("Blade %"PRIu64"(%d %d %d) doesn't have "
			      "any nodes from the state file!  "
			      "Unexpected results could "
			      "happen if jobs are running!",
			      blade_info.id,
			      GET_BLADE_X(blade_info.id),
			      GET_BLADE_Y(blade_info.id),
			      GET_BLADE_Z(blade_info.id));
		} else if (blade_info.id == blade_array[i].id) {
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
					/*	blade_info.job_cnt; */
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

	if (!ignore_state_errors)
		fatal("Incomplete blade data checkpoint file, you may get unexpected issues if jobs were running. Start with '-i' to ignore this");
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

			if ((IS_CLEANING_STARTED(jobinfo) &&
			     !IS_CLEANING_COMPLETE(jobinfo)) ||
			    IS_JOB_RUNNING(job_ptr))
				_set_job_running_restore(jobinfo);

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
			/* As applicable, rerun CCM prologue during recovery */
			if ((ccm_config.ccm_enabled) &&
			    ccm_check_partitions(job_ptr)) {
				if ((job_ptr->details) &&
				    ((job_ptr->details->prolog_running) ||
				     IS_JOB_CONFIGURING(job_ptr))) {
					debug("CCM %pJ recovery rerun prologue",
					      job_ptr);
					job_ptr->job_state |= JOB_CONFIGURING;
					slurm_thread_create_detached(NULL,
								     ccm_begin,
								     job_ptr);
				}
			}
#endif
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
	DEF_TIMERS;

	if (scheduling_disabled)
		return other_node_init(node_ptr, node_cnt);

	START_TIMER;

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	int nn, end_nn, last_nn = 0;
	bool found = 0;
	char *err_msg = NULL;

	if (!topology) {
		if (alpsc_get_topology(&err_msg, &topology,
				       &topology_num_nodes)) {
			END_TIMER;
			if (debug_flags & DEBUG_FLAG_TIME_CRAY)
				INFO_LINE("call took: %s", TIME_STR);

			if (err_msg) {
				error("(%s: %d: %s) Could not get system "
				      "topology info: %s",
				      THIS_FILE, __LINE__,
				      __func__, err_msg);
				free(err_msg);
			} else {
				error("(%s: %d: %s) Could not get system "
				      "topology info: No error "
				      "message present.",
				      THIS_FILE, __LINE__, __func__);
			}
			return SLURM_ERROR;
		}
	}

#endif

	slurm_mutex_lock(&blade_mutex);

	if (!blade_array)
		blade_array = xcalloc(node_cnt, sizeof(blade_info_t));

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
				      THIS_FILE, __LINE__, __func__,
				      node_rec->name);
				slurm_mutex_unlock(&blade_mutex);
				return SLURM_ERROR;
			}

			nodeinfo->nid = atoll(nid_char);
		}

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
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
			for (nn = 0; nn < topology_num_nodes; nn++) {
				info("ALPS topology, record:%d nid:%d",
				     nn, topology[nn].nid);
			}
			fatal("Node %s(%d) isn't found in the ALPS system topoloogy table",
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
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return other_node_init(node_ptr, node_cnt);
}

extern int select_p_block_init(List part_list)
{
#ifdef HAVE_NATIVE_CRAY
	if (!aeld_running)
		_start_aeld_thread();
#endif

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
#ifdef HAVE_NATIVE_CRAY
	/* Restart if the thread ever has an unrecoverable error and exits. */
	if (!aeld_running)
		_start_aeld_thread();
#endif

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
				bit_and_not(bitmap, blade_nodes_running_npc);
			}
		}
	}

	/* char *tmp = bitmap2node_name(bitmap); */
	/* char *tmp3 = bitmap2node_name(blade_nodes_running_npc); */

	/* info("trying %pJ on %s '%s'", job_ptr, tmp, tmp3); */
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
	jobinfo->cleaning = CLEANING_INIT;	/* Reset needed if requeued */
	jobinfo->released = 0;

	slurm_mutex_lock(&blade_mutex);

	if (!jobinfo->blade_map) {
		jobinfo->blade_map = bit_alloc(blade_cnt);
	} else {	/* Clear vestigial bitmap in case job requeued */
		bit_nclear(jobinfo->blade_map, 0,
			   bit_size(jobinfo->blade_map) - 1);
	}
	_set_job_running(job_ptr);

	/* char *tmp3 = bitmap2node_name(blade_nodes_running_npc); */

	/* info("adding %pJ '%s'", job_ptr, tmp3); */
	/* xfree(tmp3); */
	slurm_mutex_unlock(&blade_mutex);

#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	if (ccm_config.ccm_enabled) {
		/*
		 * Create a thread to do setup activity before running the
		 * CCM prolog for a CCM partition.
		 */
		if (ccm_check_partitions(job_ptr)) {
			if (job_ptr->details == NULL) {
				/* This info is required; abort the launch */
				CRAY_ERR("CCM prolog missing job details, %pJ killed",
					 job_ptr);
				srun_user_message(job_ptr,
				      "CCM prolog missing job details, killed");
				job_signal(job_ptr, SIGKILL, 0, 0, false);
			} else {
				/* Delay job launch until CCM prolog is done */
				debug("CCM %pJ increment prolog_running, current %d",
				      job_ptr,
				      job_ptr->details->prolog_running);
				job_ptr->details->prolog_running++;
				/* Cleared in prolog_running_decr() */
				debug("CCM %pJ setting JOB_CONFIGURING",
				      job_ptr);
				job_ptr->job_state |= JOB_CONFIGURING;
				slurm_thread_create_detached(NULL, ccm_begin,
							     job_ptr);
			}
		}
	}
#endif
	return other_job_begin(job_ptr);
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	xassert(job_ptr);
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	if (ccm_check_partitions(job_ptr)) {
		/* Delay CCM job launch until CCM prolog is done */
		if (IS_JOB_CONFIGURING(job_ptr)) {
			debug("CCM %pJ job configuring set; job not ready",
			      job_ptr);
			return READY_JOB_ERROR;
		}
	}
#endif
	return other_job_ready(job_ptr);
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	return other_job_resized(job_ptr, node_ptr);
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

extern int select_p_job_mem_confirm(struct job_record *job_ptr)
{
	xassert(job_ptr);

	return other_job_mem_confirm(job_ptr);
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
#if defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	/* Create a thread to run the CCM epilog for a CCM partition */
	if (ccm_config.ccm_enabled) {
		if (ccm_check_partitions(job_ptr)) {
			slurm_thread_create_detached(NULL, ccm_fini, job_ptr);
		}
	}
#endif

	other_job_fini(job_ptr);
	return SLURM_SUCCESS;
}

extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
#ifdef HAVE_NATIVE_CRAY
	ListIterator i;
	struct step_record *step_ptr = NULL;
	DEF_TIMERS;

	// Make an event for each job step
	if (aeld_running) {
		START_TIMER;
		i = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *)list_next(i))) {
			_update_app(step_ptr, ALPSC_EV_SUSPEND);
		}
		list_iterator_destroy(i);
		END_TIMER;
		if (debug_flags & DEBUG_FLAG_TIME_CRAY)
			INFO_LINE("call took: %s", TIME_STR);
	}
#endif

	return other_job_suspend(job_ptr, indf_susp);
}

extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
#ifdef HAVE_NATIVE_CRAY
	ListIterator i;
	struct step_record *step_ptr = NULL;
	DEF_TIMERS;

	// Make an event for each job step
	if (aeld_running) {
		START_TIMER;
		i = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *)list_next(i))) {
			_update_app(step_ptr, ALPSC_EV_RESUME);
		}
		list_iterator_destroy(i);
		END_TIMER;
		if (debug_flags & DEBUG_FLAG_TIME_CRAY)
			INFO_LINE("call took: %s", TIME_STR);
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
	DEF_TIMERS;

	START_TIMER;
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
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return other_step_pick_nodes(job_ptr, jobinfo, node_count, avail_nodes);
}

extern int select_p_step_start(struct step_record *step_ptr)
{
	select_jobinfo_t *jobinfo;
	DEF_TIMERS;

	START_TIMER;

#ifdef HAVE_NATIVE_CRAY
	if (aeld_running) {
		_update_app(step_ptr, ALPSC_EV_START);
	}
#endif

	jobinfo = step_ptr->job_ptr->select_jobinfo->data;
	if (jobinfo->npc && (step_ptr->step_id != SLURM_EXTERN_CONT)) {
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
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_TIME_CRAY)
		INFO_LINE("call took: %s", TIME_STR);

	return other_step_start(step_ptr);
}

extern int select_p_step_finish(struct step_record *step_ptr, bool killing_step)
{
#ifdef HAVE_NATIVE_CRAY
	if (aeld_running) {
		_update_app(step_ptr, ALPSC_EV_END);
	}
#endif

	/* Send step to db since the step could be deleted by post_job_step()
	 * before the step is completed and sent to the db (handled in
	 * _internal_step_complete() in step_mgr.c). post_job_step is called
	 * after the NHC is run (or immediately with NHC_NO). */
	/* Note if select_g_step_finish is ever called outside of
	 * _internal_step_complete you most likely will need to call
	 * this.
	 */
	if (killing_step)
		jobacct_storage_g_step_complete(acct_db_conn, step_ptr);

	other_step_finish(step_ptr, killing_step);

	return SLURM_SUCCESS;
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

	if (!nodeinfo) {
		/*
		 * We should never get here,
		 * but avoid abort with bad data structures
		 */
		error("%s: nodeinfo is NULL", __func__);
		rc = other_select_nodeinfo_pack(NULL, buffer, protocol_version);
	} else {
		rc = other_select_nodeinfo_pack(nodeinfo->other_nodeinfo,
						buffer, protocol_version);
	}

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

	if (scheduling_disabled)
		return other_select_nodeinfo_set_all();

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
	case SELECT_JOBDATA_RELEASED:
		jobinfo->released = *uint16;
		break;
	case SELECT_JOBDATA_NETWORK:
		if (!in_char || !strlen(in_char)
		    || !xstrcmp(in_char, "none"))
			jobinfo->npc = NPC_NONE;
		else if (!xstrcmp(in_char, "system"))
			jobinfo->npc = NPC_SYS;
		else if (!xstrcmp(in_char, "blade"))
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
		if (IS_CLEANING_STARTED(jobinfo) &&
		    !IS_CLEANING_COMPLETE(jobinfo))
			*uint16 = 1;
		else
			*uint16 = 0;
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
	/*
	 * Skip call to other_select_jobinfo_sprint, all of the other select
	 * plugins we can layer on top of do this same thing anyways:
	 */
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	} else
		return NULL;
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
