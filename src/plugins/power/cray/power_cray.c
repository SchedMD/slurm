/*****************************************************************************\
 *  power_cray.c - Plugin for Cray power management.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#if HAVE_JSON
#include <json-c/json.h>
#endif

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/power/common/power_common.h"
#include "src/slurmctld/locks.h"

#define DEFAULT_BALANCE_INTERVAL 30
#define DEFAULT_CAPMC_PATH  "/opt/cray/capmc/default/bin/capmc"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
struct node_record *node_record_table_ptr __attribute__((weak_import)) = NULL;
List job_list __attribute__((weak_import)) = NULL;
int node_record_count __attribute__((weak_import)) = 0;
#else
struct node_record *node_record_table_ptr = NULL;
List job_list = NULL;
int node_record_count = 0;
#endif

typedef struct power_config_nodes {
	uint32_t max_watts;     /* maximum power consumption by node, in watts */
	uint32_t min_watts;     /* minimum power consumption by node, in watts */
	char *nodes;		/* Node names (nid range list values on Cray) */
} power_config_nodes_t;

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
 * the plugin (e.g., "burst_buffer" for SLURM burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "power cray plugin";
const char plugin_type[]        = "power/cray";
const uint32_t plugin_version   = 100;

/*********************** local variables *********************/
static int balance_interval = DEFAULT_BALANCE_INTERVAL;
static char *capmc_path = NULL;
static uint64_t debug_flag = 0;
static bool stop_power = false;
static pthread_t power_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;

/*********************** local functions *********************/
static void _get_capabilities(void);
static power_config_nodes_t *
            _json_parse_array(json_object *jobj, char *key, int *num);
static void _json_parse_object(json_object *jobj, power_config_nodes_t *ent);
static void _load_config(void);
extern void *_power_agent(void *args);
static void _set_power_caps(List node_power_list);
static void _stop_power_agent(void);

/* Parse PowerParameters configuration */
static void _load_config(void)
{
	char *sched_params, *tmp_ptr;

	debug_flag = slurm_get_debug_flags();
	sched_params = slurm_get_power_parameters();
	if (!sched_params)
		return;

	/*                                   12345678901234567890 */
	if ((tmp_ptr = strstr(sched_params, "balance_interval="))) {
		balance_interval = atoi(tmp_ptr + 17);
		if (balance_interval < 1) {
			error("PowerParameters: balance_interval=%d invalid",
			      balance_interval);
			balance_interval = DEFAULT_BALANCE_INTERVAL;
		}
	}

	xfree(capmc_path);
	if ((tmp_ptr = strstr(sched_params, "capmc_path="))) {
		capmc_path = xstrdup(tmp_ptr + 11);
		tmp_ptr = strchr(capmc_path, ',');
		if (tmp_ptr)
			tmp_ptr[0] = '\0';
	} else {
		capmc_path = xstrdup(DEFAULT_CAPMC_PATH);
	}

	xfree(sched_params);
	if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s configuration: balance_interval=%d capmc_path=%s",
		     __func__, balance_interval, capmc_path);
	}
}

static void _get_capabilities(void)
{
	/* Write nodes */
	slurmctld_lock_t write_locks = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
	char *cmd_resp, *script_argv[3];
	power_config_nodes_t *ents;
	int i, num_ent = 0, status = 0;
	json_object *j;
	json_object_iter iter;
	struct node_record *node_ptr;

	script_argv[0] = capmc_path;
	script_argv[1] = "get_power_cap_capabilities";
	script_argv[2] = NULL;

	cmd_resp = power_run_script("capmc", capmc_path, script_argv, 2000,
				    &status);
	if (status != 0) {
		error("%s: capmc %s %s: %s",
		      __func__, script_argv[1], script_argv[2], cmd_resp);
		xfree(cmd_resp);
		return;
	} else if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s: capmc %s %s",
		      __func__, script_argv[1], script_argv[2]);
	}


	j = json_tokener_parse(cmd_resp);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, cmd_resp);
		xfree(cmd_resp);
		return;
	}
	json_object_object_foreachC(j, iter) {
		ents = _json_parse_array(j, iter.key, &num_ent);
	}
	json_object_put(j);	/* Frees json memory */

	lock_slurmctld(write_locks);
	for (i = 0; i < num_ent; i++) {
		node_ptr = find_node_record(ents[i].nodes);
		if (!node_ptr->power)
			node_ptr->power = xmalloc(sizeof(power_mgmt_data_t));
		node_ptr->power->max_watts = ents[i].max_watts;
		node_ptr->power->min_watts = ents[i].min_watts;
		xfree(ents[i].nodes);
	}
	xfree(ents);
	unlock_slurmctld(write_locks);
	xfree(cmd_resp);
}

/* json_parse_array()
 */
static power_config_nodes_t *
_json_parse_array(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	power_config_nodes_t *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(power_config_nodes_t));

	for (i = 0; i < *num; i++){
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_object(jvalue, &ents[i]);
	}

	return ents;
}

/* json_parse_object()
 */
static void _json_parse_object(json_object *jobj, power_config_nodes_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p;

	json_object_object_foreachC(jobj, iter) {

		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
			case json_type_double:
			case json_type_null:
			case json_type_object:
			case json_type_array:
				break;
			case json_type_int:
				x = json_object_get_int64(iter.val);
				if (strcmp(iter.key, "max_watts") == 0) {
					ent->max_watts = x;
				} else if (strcmp(iter.key, "min_watts") == 0) {
					ent->min_watts = x;
				}
				break;
			case json_type_string:
				p = json_object_get_string(iter.val);
				if (strcmp(iter.key, "nid") == 0) {
					ent->nodes = "nid";
					xstrcat(ent->nodes, p);
				}
				break;
		}
	}
}

/* Periodically attempt to re-balance power caps across nodes */
extern void *_power_agent(void *args)
{
	static time_t last_cap_read = 0;
	time_t now;
	double wait_time;
	static time_t last_balance_time = 0;
	/* Read jobs and nodes */
	slurmctld_lock_t read_locks = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	List job_power_list, node_power_list = NULL;
	uint32_t alloc_watts = 0, used_watts = 0;

	last_balance_time = time(NULL);
	while (!stop_power) {
		sleep(1);
		if (stop_power)
			break;

		now = time(NULL);
		wait_time = difftime(now, last_balance_time);
		if (wait_time < balance_interval)
			continue;

		wait_time = difftime(now, last_cap_read);
		_get_capabilities();

		lock_slurmctld(read_locks);
//FIXME: On Cray/ALPS system use "capmc get_node_energy_counter" to get
// “raw accumulated-energy” and calculate power consumption from that
		get_cluster_power(node_record_table_ptr, node_record_count,
				  &alloc_watts, &used_watts);
		job_power_list = get_job_power(job_list, node_record_table_ptr);
//FIXME: power re-balancing decisions here
		FREE_NULL_LIST(job_power_list);
		_set_power_caps(node_power_list);
		last_balance_time = time(NULL);
		unlock_slurmctld(read_locks);
		_load_config();
	}
	return NULL;
}

static void _set_power_caps(List node_power_list)
{
	ListIterator node_iterator;
	power_by_nodes_t *node_power;
	char *cmd_resp, *script_argv[7], watts[32];
	int status = 0;

	if (!node_power_list)
		return;

	script_argv[0] = capmc_path;
	script_argv[1] = "set_power_cap";
	script_argv[2] = "--nids";
	/* script_argv[3] = TBD */
	script_argv[4] = "--watts";
	script_argv[5] = watts;
	script_argv[6] = NULL;

	/* Pass 1, decrease power for select nodes */
	node_iterator = list_iterator_create(node_power_list);
	while ((node_power = (power_by_nodes_t *) list_next(node_iterator))) {
		if (node_power->increase_power)
			continue;
		script_argv[3] = node_power->nodes;
		snprintf(watts, sizeof(watts), "%u", node_power->alloc_watts);
		cmd_resp = power_run_script("capmc", capmc_path, script_argv,
					    2000, &status);
		if (status != 0) {
			error("%s: capmc %s %s %s %s %s: %s",
			      __func__, script_argv[1], script_argv[2],
			      script_argv[3], script_argv[4], script_argv[5],
			      cmd_resp);
			xfree(cmd_resp);
			list_iterator_destroy(node_iterator);
			return;
		} else if (debug_flag & DEBUG_FLAG_POWER) {
			info("%s: capmc %s %s %s %s %s",
			      __func__, script_argv[1], script_argv[2],
			      script_argv[3], script_argv[4], script_argv[5]);
		}
		xfree(cmd_resp);
	}

	/* Pass 2, increase power for select nodes */
	list_iterator_reset(node_iterator);
	while ((node_power = (power_by_nodes_t *) list_next(node_iterator))) {
		if (!node_power->increase_power)
			continue;
		script_argv[3] = node_power->nodes;
		snprintf(watts, sizeof(watts), "%u", node_power->alloc_watts);
		cmd_resp = power_run_script("capmc", capmc_path, script_argv,
					    2000, &status);
		if (status != 0) {
			error("%s: capmc %s %s %s %s %s: %s",
			      __func__, script_argv[1], script_argv[2],
			      script_argv[3], script_argv[4], script_argv[5],
			      cmd_resp);
		} else if (debug_flag & DEBUG_FLAG_POWER) {
			info("%s: capmc %s %s %s %s %s",
			      __func__, script_argv[1], script_argv[2],
			      script_argv[3], script_argv[4], script_argv[5]);
		}
		xfree(cmd_resp);
	}
	list_iterator_destroy(node_iterator);
}

/* Terminate power thread */
static void _stop_power_agent(void)
{
	pthread_mutex_lock(&term_lock);
	stop_power = true;
	pthread_cond_signal(&term_cond);
	pthread_mutex_unlock(&term_lock);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	pthread_attr_t attr;

	if (!run_in_daemon("slurmctld"))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&thread_flag_mutex);
	if (power_thread) {
		debug2("Power thread already running, not starting another");
		pthread_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	slurm_attr_init(&attr);
	/* Since we do a join on this later we don't make it detached */
	if (pthread_create(&power_thread, &attr, _power_agent, NULL))
		error("Unable to start power thread: %m");
	pthread_mutex_unlock(&thread_flag_mutex);
	slurm_attr_destroy(&attr);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern void fini(void)
{
	pthread_mutex_lock(&thread_flag_mutex);
	if (power_thread) {
		_stop_power_agent();
		pthread_join(power_thread, NULL);
		power_thread = 0;
	}
	pthread_mutex_unlock(&thread_flag_mutex);
}
