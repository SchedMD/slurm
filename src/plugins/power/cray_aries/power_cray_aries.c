/*****************************************************************************\
 *  power_cray_aries.c - Plugin for Cray/Aries power management.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/power/common/power_common.h"
#include "src/slurmctld/locks.h"

#define DEFAULT_BALANCE_INTERVAL  30
#define DEFAULT_CAPMC_PATH        "/opt/cray/capmc/default/bin/capmc"
#define DEFAULT_CAP_WATTS         0
#define DEFAULT_DECREASE_RATE     50
#define DEFAULT_GET_TIMEOUT       5000
#define DEFAULT_INCREASE_RATE     20
#define DEFAULT_LOWER_THRESHOLD   90
#define DEFAULT_SET_TIMEOUT       30000
#define DEFAULT_UPPER_THRESHOLD   95
#define DEFAULT_RECENT_JOB        300

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern struct node_record *node_record_table_ptr __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
#else
struct node_record *node_record_table_ptr = NULL;
List job_list = NULL;
int node_record_count = 0;
#endif

typedef struct power_config_nodes {
	uint32_t accel_max_watts; /* maximum power consumption by accel, in watts */
	uint32_t accel_min_watts; /* minimum power consumption by accel, in watts */
	uint32_t cap_watts;       /* cap on power consumption by node, in watts */
	uint64_t joule_counter;	  /* total energy consumption by node, in joules */
	uint32_t node_max_watts;  /* maximum power consumption by node, in watts */
	uint32_t node_min_watts;  /* minimum power consumption by node, in watts */
	int node_cnt;		  /* length of node_name array */
	char **node_name;	  /* Node names (nid range list values on Cray) */
	uint16_t state;           /* State 1=ready, 0=other */
	uint64_t time_usec;       /* number of microseconds since start of the day */
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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "burst_buffer" for Slurm burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "power Cray/Aries plugin";
const char plugin_type[]        = "power/cray_aries";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*********************** local variables *********************/
static int balance_interval = DEFAULT_BALANCE_INTERVAL;
static char *capmc_path = NULL;
static uint32_t cap_watts = DEFAULT_CAP_WATTS;
static uint32_t set_watts = 0;
static uint64_t debug_flag = 0;
static char *full_nid_string = NULL;
static uint32_t decrease_rate = DEFAULT_DECREASE_RATE;
static uint32_t increase_rate = DEFAULT_INCREASE_RATE;
static uint32_t job_level = NO_VAL;
static time_t last_cap_read = 0;
static time_t last_limits_read = 0;
static uint32_t lower_threshold = DEFAULT_LOWER_THRESHOLD;
static uint32_t recent_job = DEFAULT_RECENT_JOB;
static uint32_t upper_threshold = DEFAULT_UPPER_THRESHOLD;
static bool stop_power = false;
static int get_timeout = DEFAULT_GET_TIMEOUT;
static int set_timeout = DEFAULT_SET_TIMEOUT;
static pthread_t power_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;

/*********************** local functions *********************/
static void _build_full_nid_string(void);
static void _clear_node_caps(void);
static void _get_capabilities(void);
static void _get_caps(void);
static void _get_node_energy_counter(void);
static void _get_nodes_ready(void);
static power_config_nodes_t *
            _json_parse_array_capabilities(json_object *jobj,
					   char *key, int *num);
static power_config_nodes_t *
		_json_parse_array_caps(json_object *jobj, char *key, int *num);
static power_config_nodes_t *
            _json_parse_array_energy(json_object *jobj, char *key, int *num);
static void _json_parse_capabilities(json_object *jobj,
				     power_config_nodes_t *ent);
static void _json_parse_energy(json_object *jobj, power_config_nodes_t *ent);
static void _json_parse_nid(json_object *jobj, power_config_nodes_t *ent);
static power_config_nodes_t *
            _json_parse_ready(json_object *jobj, char *key, int *num);
static void _load_config(void);
static void _log_node_power(void);
static void _parse_capable_control(json_object *j_control,
				   power_config_nodes_t *ent);
static void _parse_capable_controls(json_object *j_control,
				    power_config_nodes_t *ent);
static void _parse_caps_control(json_object *j_control,
				power_config_nodes_t *ent);
static void _parse_caps_controls(json_object *j_control,
				 power_config_nodes_t *ent);
extern void *_power_agent(void *args);
static void _rebalance_node_power(void);
static void _set_node_caps(void);
static void _set_power_caps(void);
static void _stop_power_agent(void);
static uint64_t _time_str2num(char *time_str);

/* Convert a time in the format "2015-02-19 15:50:00.581552-06" to the
 * equivalent to the number of micro-seconds since the start of this day */
static uint64_t _time_str2num(char *time_str)
{
	uint64_t total_usecs = 0;
	int year = 0, month = 0, day = 0;
	int hour = 0, min = 0, sec = 0;
	int u_sec = 0, unk = 0;
	int args;

	args = sscanf(time_str, "%d-%d-%d %d:%d:%d.%d-%d",
		      &year, &month, &day, &hour, &min, &sec, &u_sec, &unk);
	if (args >= 6) {
		total_usecs  = (((hour * 60) + min) * 60) + sec;
		total_usecs *= 1000000;
		total_usecs += u_sec;
	}

	return total_usecs;
}

/* Return a pointer to the numeric value of a node name starting with "nid",
 * also skip over leading zeros in the numeric portion. Returns a pointer
 * into the node_name argument. No data is copied. */
static char *_node_name2nid(char *node_name)
{
	int j;

	if ((node_name[0] != 'n') || (node_name[1] != 'i') ||
	    (node_name[2] != 'd')) {
		error("%s: Invalid node name (%s)", __func__, node_name);
		return (node_name);
	}

	for (j = 3; j < 7; j++) {
		if (node_name[j] != '0')
			break;
	}
	return (node_name + j);
}

/* Parse PowerParameters configuration */
static void _load_config(void)
{
	char *end_ptr = NULL, *sched_params, *tmp_ptr;

	debug_flag = slurm_get_debug_flags();
	sched_params = slurm_get_power_parameters();

	/*                                   12345678901234567890 */
	if ((tmp_ptr = xstrcasestr(sched_params, "balance_interval="))) {
		balance_interval = atoi(tmp_ptr + 17);
		if (balance_interval < 1) {
			error("PowerParameters: balance_interval=%d invalid",
			      balance_interval);
			balance_interval = DEFAULT_BALANCE_INTERVAL;
		}
	} else {
		balance_interval = DEFAULT_BALANCE_INTERVAL;
	}

	xfree(capmc_path);
	if ((tmp_ptr = xstrcasestr(sched_params, "capmc_path="))) {
		capmc_path = xstrdup(tmp_ptr + 11);
		tmp_ptr = strchr(capmc_path, ',');
		if (tmp_ptr)
			tmp_ptr[0] = '\0';
	} else {
		capmc_path = xstrdup(DEFAULT_CAPMC_PATH);
	}

	/*                                   12345678901234567890 */
	if ((tmp_ptr = xstrcasestr(sched_params, "cap_watts="))) {
		cap_watts = strtol(tmp_ptr + 10, &end_ptr, 10);
		if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K')) {
			cap_watts *= 1000;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			cap_watts *= 1000000;
		}
	} else {
		cap_watts = DEFAULT_CAP_WATTS;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "decrease_rate="))) {
		decrease_rate = atoi(tmp_ptr + 14);
		if (decrease_rate < 1) {
			error("PowerParameters: decrease_rate=%u invalid",
			      balance_interval);
			lower_threshold = DEFAULT_DECREASE_RATE;
		}
	} else {
		decrease_rate = DEFAULT_DECREASE_RATE;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "increase_rate="))) {
		increase_rate = atoi(tmp_ptr + 14);
		if (increase_rate < 1) {
			error("PowerParameters: increase_rate=%u invalid",
			      balance_interval);
			lower_threshold = DEFAULT_INCREASE_RATE;
		}
	} else {
		increase_rate = DEFAULT_INCREASE_RATE;
	}

	if (xstrcasestr(sched_params, "job_level"))
		job_level = 1;
	else if (xstrcasestr(sched_params, "job_no_level"))
		job_level = 0;
	else
		job_level = NO_VAL;

	if ((tmp_ptr = xstrcasestr(sched_params, "get_timeout="))) {
		get_timeout = atoi(tmp_ptr + 12);
		if (get_timeout < 1) {
			error("PowerParameters: get_timeout=%d invalid",
			      get_timeout);
			get_timeout = DEFAULT_GET_TIMEOUT;
		}
	} else {
		get_timeout = DEFAULT_GET_TIMEOUT;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "lower_threshold="))) {
		lower_threshold = atoi(tmp_ptr + 16);
		if (lower_threshold < 1) {
			error("PowerParameters: lower_threshold=%u invalid",
			      lower_threshold);
			lower_threshold = DEFAULT_LOWER_THRESHOLD;
		}
	} else {
		lower_threshold = DEFAULT_LOWER_THRESHOLD;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "recent_job="))) {
		recent_job = atoi(tmp_ptr + 11);
		if (recent_job < 1) {
			error("PowerParameters: recent_job=%u invalid",
			      recent_job);
			recent_job = DEFAULT_RECENT_JOB;
		}
	} else {
		recent_job = DEFAULT_RECENT_JOB;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "set_timeout="))) {
		set_timeout = atoi(tmp_ptr + 12);
		if (set_timeout < 1) {
			error("PowerParameters: set_timeout=%d invalid",
			      set_timeout);
			set_timeout = DEFAULT_SET_TIMEOUT;
		}
	} else {
		set_timeout = DEFAULT_SET_TIMEOUT;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "set_watts="))) {
		set_watts = strtol(tmp_ptr + 10, &end_ptr, 10);
		if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K')) {
			set_watts *= 1000;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			set_watts *= 1000000;
		}
	} else {
		set_watts = 0;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "upper_threshold="))) {
		upper_threshold = atoi(tmp_ptr + 16);
		if (upper_threshold < 1) {
			error("PowerParameters: upper_threshold=%u invalid",
			      upper_threshold);
			upper_threshold = DEFAULT_UPPER_THRESHOLD;
		}
	} else {
		upper_threshold = DEFAULT_UPPER_THRESHOLD;
	}

	xfree(sched_params);
	xfree(full_nid_string);
	if (debug_flag & DEBUG_FLAG_POWER) {
		char *level_str = "";
		if (job_level == 0)
			level_str = "job_no_level,";
		else if (job_level == 1)
			level_str = "job_level,";
		info("PowerParameters=balance_interval=%d,capmc_path=%s,"
		     "cap_watts=%u,decrease_rate=%u,get_timeout=%d,"
		     "increase_rate=%u,%slower_threshold=%u,recent_job=%u,"
		     "set_timeout=%d,set_watts=%u,upper_threshold=%u",
		     balance_interval, capmc_path, cap_watts, decrease_rate,
		     get_timeout, increase_rate, level_str, lower_threshold,
		     recent_job, set_timeout, set_watts, upper_threshold);
	}

	last_limits_read = 0;	/* Read node power limits again */
}

static void _get_capabilities(void)
{
	/* Write nodes */
	slurmctld_lock_t write_node_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	char *cmd_resp, *script_argv[3], node_names[128];
	power_config_nodes_t *ents = NULL;
	int i, j, num_ent = 0, status = 0;
	json_object *j_obj;
	json_object_iter iter;
	struct node_record *node_ptr;
	hostlist_t hl = NULL;
	DEF_TIMERS;

	script_argv[0] = capmc_path;
	script_argv[1] = "get_power_cap_capabilities";
	script_argv[2] = NULL;

	START_TIMER;
	cmd_resp = power_run_script("capmc", capmc_path, script_argv,
				    get_timeout, NULL, &status);
	END_TIMER;
	if (status != 0) {
		error("%s: capmc %s: %s",
		      __func__, script_argv[1], cmd_resp);
		xfree(cmd_resp);
		return;
	} else if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s: capmc %s %s", __func__, script_argv[1], TIME_STR);
	}
	if ((cmd_resp == NULL) || (cmd_resp[0] == '\0')) {
		xfree(cmd_resp);
		return;
	}

	j_obj = json_tokener_parse(cmd_resp);
	if (j_obj == NULL) {
		error("%s: json parser failed on %s", __func__, cmd_resp);
		xfree(cmd_resp);
		return;
	}
	json_object_object_foreachC(j_obj, iter) {
		/* NOTE: The error number "e" and message "err_msg" fields
		 * are currently ignored. */
		if (!xstrcmp(iter.key, "groups")) {
			ents = _json_parse_array_capabilities(j_obj, iter.key,
							      &num_ent);
			break;
		}
	}
	json_object_put(j_obj);	/* Frees json memory */

	lock_slurmctld(write_node_lock);
	for (i = 0; i < num_ent; i++) {
		if (debug_flag & DEBUG_FLAG_POWER)
			hl = hostlist_create(NULL);
		for (j = 0; j < ents[i].node_cnt; j++) {
			if (debug_flag & DEBUG_FLAG_POWER)
				hostlist_push_host(hl, ents[i].node_name[j]);
			node_ptr = find_node_record2(ents[i].node_name[j]);
			if (!node_ptr) {
				debug("%s: Node %s not in Slurm config",
				      __func__, ents[i].node_name[j]);
			} else {
				if (!node_ptr->power) {
					node_ptr->power =
						xmalloc(sizeof(power_mgmt_data_t));
				}
				node_ptr->power->max_watts =
					ents[i].node_max_watts;
				node_ptr->power->min_watts =
					ents[i].node_min_watts;
			}
			xfree(ents[i].node_name[j]);
		}
		xfree(ents[i].node_name);
		if (debug_flag & DEBUG_FLAG_POWER) {
			hostlist_ranged_string(hl, sizeof(node_names),
					       node_names);
			info("AccelWattsAvail:%3.3u-%3.3u "
			     "NodeWattsAvail:%3.3u-%3.3u Nodes=%s",
			     ents[i].accel_min_watts, ents[i].accel_max_watts,
			     ents[i].node_min_watts, ents[i].node_max_watts,
			     node_names);
			hostlist_destroy(hl);
		}
	}
	xfree(ents);
	unlock_slurmctld(write_node_lock);
	xfree(cmd_resp);
}

static power_config_nodes_t *
_json_parse_array_capabilities(json_object *jobj, char *key, int *num)
{
	json_object *j_array;
	json_object *j_value;
	int i;
	power_config_nodes_t *ents;

	j_array = jobj;
	json_object_object_get_ex(jobj, key, &j_array);

	*num = json_object_array_length(j_array);
	ents = xmalloc(*num * sizeof(power_config_nodes_t));

	for (i = 0; i < *num; i++) {
		j_value = json_object_array_get_idx(j_array, i);
		_json_parse_capabilities(j_value, &ents[i]);
	}

	return ents;
}

/* Parse a "controls" array element from the "capmc get_power_cap_capabilities"
 * command. Identifies node and accelerator power ranges. */
static void _parse_capable_control(json_object *j_control,
				   power_config_nodes_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	const char *p = NULL;
	int min_watts = 0, max_watts = 0, x;

	json_object_object_foreachC(j_control, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
//				info("%s: Key boolean %s", __func__, iter.key);
				break;
			case json_type_double:
//				info("%s: Key double %s", __func__, iter.key);
				break;
			case json_type_null:
//				info("%s: Key null %s", __func__, iter.key);
				break;
			case json_type_object:
//				info("%s: Key object %s", __func__, iter.key);
				break;
			case json_type_array:
//				info("%s: Key array %s", __func__, iter.key);
				break;
			case json_type_string:
//				info("%s: Key string %s", __func__, iter.key);
				if (!xstrcmp(iter.key, "name"))
					p = json_object_get_string(iter.val);
				break;
			case json_type_int:
//				info("%s: Key int %s", __func__, iter.key);
				x = json_object_get_int64(iter.val);
				if (!xstrcmp(iter.key, "max"))
					max_watts = x;
				else if (!xstrcmp(iter.key, "min"))
					min_watts = x;
				break;
			default:
				break;
		}
	}

	if (p) {
		if (!xstrcmp(p, "accel")) {
			ent->accel_max_watts = max_watts;
			ent->accel_min_watts = min_watts;
		} else if (!xstrcmp(p, "node")) {
			ent->node_max_watts = max_watts;
			ent->node_min_watts = min_watts;
		}
	}
}

/* Parse the "controls" array from the "capmc get_power_cap_capabilities"
 * command. Use _parse_capable_control() to get node and accelerator power
 * ranges. */
static void _parse_capable_controls(json_object *j_control,
				    power_config_nodes_t *ent)
{
	json_object *j_array = NULL;
	json_object *j_value;
	enum json_type j_type;
	int control_cnt, i;

        json_object_object_get_ex(j_control, "controls", &j_array);
	if (!j_array) {
		error("%s: Unable to parse controls specification", __func__);
		return;
	}
	control_cnt = json_object_array_length(j_array);
	for (i = 0; i < control_cnt; i++) {
		j_value = json_object_array_get_idx(j_array, i);
		j_type = json_object_get_type(j_value);
		if (j_type == json_type_object) {
			_parse_capable_control(j_value, ent);
		} else {
			error("%s: Unexpected data type: %d", __func__, j_type);
		}
	}
}

/* Parse the "nids" array from the "capmc get_power_cap_capabilities"
 * command. Identifies each node ID with identical power specifications. */
static void _parse_nids(json_object *jobj, power_config_nodes_t *ent, char *key)
{
	json_object *j_array = NULL;
	json_object *j_value;
	enum json_type j_type;
	int i, nid;

        json_object_object_get_ex(jobj, key, &j_array);
	if (!j_array) {
		error("%s: Unable to parse nid specification", __func__);
		return;
	}
	ent->node_cnt = json_object_array_length(j_array);
	ent->node_name = xmalloc(sizeof(char *) * ent->node_cnt);
	for (i = 0; i < ent->node_cnt; i++) {
		j_value = json_object_array_get_idx(j_array, i);
		j_type = json_object_get_type(j_value);
		if (j_type != json_type_int) {
			error("%s: Unable to parse nid specification",__func__);
		} else {
			nid = json_object_get_int64(j_value);
			xstrfmtcat(ent->node_name[i], "nid%5.5d", nid);
		}
	}
}

/* Parse a "groups" array element from the "capmc get_power_cap_capabilities"
 * command. Use _parse_capable_controls() and _parse_nids() to get node and
 * accelerator power ranges for each node. */
static void _json_parse_capabilities(json_object *jobj,
				     power_config_nodes_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
//				info("%s: Key boolean %s", __func__, iter.key);
				break;
			case json_type_double:
//				info("%s: Key double %s", __func__, iter.key);
				break;
			case json_type_null:
//				info("%s: Key null %s", __func__, iter.key);
				break;
			case json_type_object:
//				info("%s: Key object %s", __func__, iter.key);
				break;
			case json_type_string:
//				info("%s: Key string %s", __func__, iter.key);
				break;
			case json_type_array:
//				info("%s: Key array %s", __func__, iter.key);
				if (!xstrcmp(iter.key, "controls")) {
					_parse_capable_controls(jobj, ent);
				} else if (!xstrcmp(iter.key, "nids")) {
					_parse_nids(jobj, ent, "nids");
				}
				break;
			case json_type_int:
//				info("%s: Key int %s", __func__, iter.key);
				break;
			default:
				break;
		}
	}
}

static void _build_full_nid_string(void)
{
	/* Read nodes */
	slurmctld_lock_t read_node_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	struct node_record *node_ptr;
	hostset_t hs = NULL;
	char *sep, *tmp_str;
	int i, num_ent = 0;

	if (full_nid_string)
		return;

	lock_slurmctld(read_node_lock);
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_DOWN(node_ptr))
			continue;
		if (!hs)
			hs = hostset_create(_node_name2nid(node_ptr->name));
		else
			hostset_insert(hs, _node_name2nid(node_ptr->name));
		num_ent++;
	}
	unlock_slurmctld(read_node_lock);
	if (!hs) {
		error("%s: No nodes found", __func__);
		return;
	}
	tmp_str = xmalloc(node_record_count * 6 + 2);
	(void) hostset_ranged_string(hs, num_ent * 6, tmp_str);
	hostset_destroy(hs);
	if ((sep = strrchr(tmp_str, ']')))
		sep[0] = '\0';
	if (tmp_str[0] == '[')
		full_nid_string = xstrdup(tmp_str + 1);
	else
		full_nid_string = xstrdup(tmp_str);
	xfree(tmp_str);
}

static void _get_caps(void)
{
	/* Write nodes */
	slurmctld_lock_t write_node_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	char *cmd_resp, *script_argv[5];
	power_config_nodes_t *ents = NULL;
	int i, num_ent = 0, status = 0;
	json_object *j_obj;
	json_object_iter iter;
	struct node_record *node_ptr;
	DEF_TIMERS;

	script_argv[0] = capmc_path;
	script_argv[1] = "get_power_cap";
	script_argv[2] = NULL;

	START_TIMER;
	cmd_resp = power_run_script("capmc", capmc_path, script_argv,
				    get_timeout, NULL, &status);
	END_TIMER;
	if (status != 0) {
		error("%s: capmc %s: %s",
		      __func__, script_argv[1], cmd_resp);
		xfree(cmd_resp);
		return;
	} else if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s: capmc %s %s", __func__, script_argv[1], TIME_STR);
	}
	if ((cmd_resp == NULL) || (cmd_resp[0] == '\0')) {
		xfree(cmd_resp);
		return;
	}

	j_obj = json_tokener_parse(cmd_resp);
	if (j_obj == NULL) {
		error("%s: json parser failed on %s", __func__, cmd_resp);
		xfree(cmd_resp);
		return;
	}
	json_object_object_foreachC(j_obj, iter) {
		/* NOTE: The error number "e" and message "err_msg" fields
		 * are currently ignored. */
		if (!xstrcmp(iter.key, "nids")) {
			ents = _json_parse_array_caps(j_obj, iter.key,
						      &num_ent);
			break;
		}
	}
	json_object_put(j_obj);	/* Frees json memory */

	lock_slurmctld(write_node_lock);
	for (i = 0; i < num_ent; i++) {
		node_ptr = find_node_record2(ents[i].node_name[0]);
		if (!node_ptr) {
			debug2("%s: Node %s not in Slurm config",
			      __func__, ents[i].node_name[0]);
		} else {
			if (!node_ptr->power) {
				node_ptr->power =
					xmalloc(sizeof(power_mgmt_data_t));
			}
			node_ptr->power->cap_watts = ents[i].cap_watts;
		}
		xfree(ents[i].node_name[0]);   /* FUTURE: array of node names */
		xfree(ents[i].node_name);
	}
	xfree(ents);
	unlock_slurmctld(write_node_lock);
	xfree(cmd_resp);
}

/* json_parse_array()
 */
static power_config_nodes_t *
_json_parse_array_caps(json_object *jobj, char *key, int *num)
{
	json_object *j_array;
	json_object *j_value;
	int i;
	power_config_nodes_t *ents;

	j_array = jobj;
	json_object_object_get_ex(jobj, key, &j_array);

	*num = json_object_array_length(j_array);
	ents = xmalloc(*num * sizeof(power_config_nodes_t));

	for (i = 0; i < *num; i++) {
		j_value = json_object_array_get_idx(j_array, i);
		_json_parse_nid(j_value, &ents[i]);
	}

	return ents;
}

static void _parse_caps_control(json_object *j_control,
				power_config_nodes_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	const char *p = NULL;
	int cap_watts = 0, x;

	json_object_object_foreachC(j_control, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
//				info("%s: Key boolean %s", __func__, iter.key);
				break;
			case json_type_double:
//				info("%s: Key double %s", __func__, iter.key);
				break;
			case json_type_null:
//				info("%s: Key null %s", __func__, iter.key);
				break;
			case json_type_object:
//				info("%s: Key object %s", __func__, iter.key);
				break;
			case json_type_array:
//				info("%s: Key array %s", __func__, iter.key);
				break;
			case json_type_string:
//				info("%s: Key string %s", __func__, iter.key);
				if (!xstrcmp(iter.key, "name"))
					p = json_object_get_string(iter.val);
				break;
			case json_type_int:
//				info("%s: Key int %s", __func__, iter.key);
				x = json_object_get_int64(iter.val);
				if (!xstrcmp(iter.key, "val"))
					cap_watts = x;
				break;
			default:
				break;
		}
	}

	if (p) {
		if (!xstrcmp(p, "node")) {
			ent->cap_watts = cap_watts;
		}
	}
}

/* Parse the "controls" array from the "capmc get_power_caps" command.
 * Use _parse_caps_control() to get node and accelerator power ranges. */
static void _parse_caps_controls(json_object *j_control,
				 power_config_nodes_t *ent)
{
	json_object *j_array = NULL;
	json_object *j_value;
	enum json_type j_type;
	int control_cnt, i;

        json_object_object_get_ex(j_control, "controls", &j_array);
	if (!j_array) {
		error("%s: Unable to parse controls specification", __func__);
		return;
	}
	control_cnt = json_object_array_length(j_array);
	for (i = 0; i < control_cnt; i++) {
		j_value = json_object_array_get_idx(j_array, i);
		j_type = json_object_get_type(j_value);
		if (j_type == json_type_object) {
			_parse_caps_control(j_value, ent);
		} else {
			error("%s: Unexpected data type: %d", __func__, j_type);
		}
	}
}

/* Parse a "nids" array element from the "capmc get_power_cap" command. */
static void _json_parse_nid(json_object *jobj, power_config_nodes_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int x;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
//				info("%s: Key boolean %s", __func__, iter.key);
				break;
			case json_type_double:
//				info("%s: Key double %s", __func__, iter.key);
				break;
			case json_type_null:
//				info("%s: Key null %s", __func__, iter.key);
				break;
			case json_type_object:
//				info("%s: Key object %s", __func__, iter.key);
				break;
			case json_type_string:
//				info("%s: Key string %s", __func__, iter.key);
				break;
			case json_type_array:
//				info("%s: Key array %s", __func__, iter.key);
				if (!xstrcmp(iter.key, "controls")) {
					_parse_caps_controls(jobj, ent);
				}
				break;
			case json_type_int:
//				info("%s: Key int %s", __func__, iter.key);
				x = json_object_get_int64(iter.val);
				if (!xstrcmp(iter.key, "nid")) {
					ent->node_name = xmalloc(sizeof(char *));
					xstrfmtcat(ent->node_name[0],
						   "nid%5.5d", x);
				}
				break;
			default:
				break;
		}
	}
}

/* Identify nodes which are in a state of "ready". Only nodes in a "ready"
 * state can have their power cap modified. */
static void _get_nodes_ready(void)
{
	/* Write nodes */
	slurmctld_lock_t write_node_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	char *cmd_resp, *script_argv[5];
	struct node_record *node_ptr;
	power_config_nodes_t *ents = NULL;
	int i, j, num_ent, status = 0;
	json_object *j_obj;
	json_object_iter iter;
	DEF_TIMERS;

	script_argv[0] = capmc_path;
	script_argv[1] = "node_status";
//	script_argv[2] = "--filter";
//	script_argv[3] = "show_ready";
	script_argv[2] = NULL;

	START_TIMER;
	cmd_resp = power_run_script("capmc", capmc_path, script_argv,
				    get_timeout, NULL, &status);
	END_TIMER;
	if (status != 0) {
		error("%s: capmc %s: %s",  __func__, script_argv[1], cmd_resp);
		xfree(cmd_resp);
		return;
	} else if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s: capmc %s %s",  __func__, script_argv[1], TIME_STR);
	}
	if ((cmd_resp == NULL) || (cmd_resp[0] == '\0')) {
		xfree(cmd_resp);
		return;
	}

	j_obj = json_tokener_parse(cmd_resp);
	if (j_obj == NULL) {
		error("%s: json parser failed on %s", __func__, cmd_resp);
		xfree(cmd_resp);
		return;
	}
	num_ent = 0;
	json_object_object_foreachC(j_obj, iter) {
		/* NOTE: The error number "e", message "err_msg", "off", and
		 * "on" fields are currently ignored. */
		if (!xstrcmp(iter.key, "ready")) {
			ents = _json_parse_ready(j_obj, iter.key, &num_ent);
			break;
		}
	}
	json_object_put(j_obj);	/* Frees json memory */

	lock_slurmctld(write_node_lock);
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!node_ptr->power)
			node_ptr->power = xmalloc(sizeof(power_mgmt_data_t));
		else
			node_ptr->power->state = 0;
	}
	for (i = 0; i < num_ent; i++) {
		for (j = 0; j < ents[i].node_cnt; j++) {
			node_ptr = find_node_record2(ents[i].node_name[j]);
			if (!node_ptr) {
				debug2("%s: Node %s not in Slurm config",
				       __func__, ents[i].node_name[j]);
			} else {
				node_ptr->power->state = ents[i].state;
			}
			xfree(ents[i].node_name[j]);
		}
		xfree(ents[i].node_name);
	}
	xfree(ents);
	unlock_slurmctld(write_node_lock);
	xfree(cmd_resp);
}

static power_config_nodes_t *
_json_parse_ready(json_object *jobj, char *key, int *num)
{
	power_config_nodes_t *ents;
	enum json_type type;
	struct json_object_iter iter;

	*num = 1;
	ents = xmalloc(*num * sizeof(power_config_nodes_t));

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
//				info("%s: Key boolean %s", __func__, iter.key);
				break;
			case json_type_double:
//				info("%s: Key double %s", __func__, iter.key);
				break;
			case json_type_null:
//				info("%s: Key null %s", __func__, iter.key);
				break;
			case json_type_object:
//				info("%s: Key object %s", __func__, iter.key);
				break;
			case json_type_array:
//				info("%s: Key array %s", __func__, iter.key);
				if (!xstrcmp(iter.key, "ready")) {
					ents->state = 1;	/* 1=ready */
					_parse_nids(jobj, ents, "ready");
				}
				break;
			case json_type_int:
//				info("%s: Key int %s", __func__, iter.key);
				break;
			case json_type_string:
//				info("%s: Key string %s", __func__, iter.key);
				break;
		}
	}

	return ents;
}

/* Gather current node power consumption rate. This logic gathers the
 * information using Cray's capmc command. An alternative would be to use
 * Slurm's energy plugin, but that would require additional synchronization
 * logic be developed. Specifically we would operate on the node's energy
 * data after current data is collected, which happens across all compute
 * nodes with a frequency of AcctGatherNodeFreq. */
static void _get_node_energy_counter(void)
{
	/* Write nodes */
	slurmctld_lock_t write_node_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	char *cmd_resp, *script_argv[5];
	power_config_nodes_t *ents = NULL;
	int i, j, num_ent = 0, status = 0;
	uint64_t delta_joules, delta_time, usecs_day;
	json_object *j_obj;
	json_object_iter iter;
	struct node_record *node_ptr;
	DEF_TIMERS;

	_build_full_nid_string();
	if (!full_nid_string)
		return;

	script_argv[0] = capmc_path;
	script_argv[1] = "get_node_energy_counter";
	script_argv[2] = "--nids";
	script_argv[3] = full_nid_string;
	script_argv[4] = NULL;

	START_TIMER;
	cmd_resp = power_run_script("capmc", capmc_path, script_argv,
				    get_timeout, NULL, &status);
	END_TIMER;
	if (status != 0) {
		error("%s: capmc %s %s %s: %s",  __func__,
		      script_argv[1], script_argv[2], script_argv[3], cmd_resp);
		xfree(cmd_resp);
		return;
	} else if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s: capmc %s %s %s %s",  __func__,
		     script_argv[1], script_argv[2], script_argv[3], TIME_STR);
	}
	if ((cmd_resp == NULL) || (cmd_resp[0] == '\0')) {
		xfree(cmd_resp);
		return;
	}

	j_obj = json_tokener_parse(cmd_resp);
	if (j_obj == NULL) {
		error("%s: json parser failed on %s", __func__, cmd_resp);
		xfree(cmd_resp);
		return;
	}
	num_ent = 0;
	json_object_object_foreachC(j_obj, iter) {
		/* NOTE: The error number "e", message "err_msg", and
		 * "nid_count" fields are currently ignored. */
		if (!xstrcmp(iter.key, "nodes")) {
			ents = _json_parse_array_energy(j_obj, iter.key,
							&num_ent);
			break;
		}
	}
	json_object_put(j_obj);	/* Frees json memory */

	lock_slurmctld(write_node_lock);
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!node_ptr->power)
			node_ptr->power = xmalloc(sizeof(power_mgmt_data_t));
		else
			node_ptr->power->current_watts = 0;
	}
	usecs_day  = 24 * 60 * 60;
	usecs_day *= 1000000;
	for (i = 0; i < num_ent; i++) {
		for (j = 0; j < ents[i].node_cnt; j++) {
			node_ptr = find_node_record2(ents[i].node_name[j]);
			if (!node_ptr) {
				debug2("%s: Node %s not in Slurm config",
				       __func__, ents[i].node_name[j]);
			} else {
				delta_time   = 0;
				if ((ents[i].time_usec == 0) ||
				    (node_ptr->power->time_usec == 0)) {
					;
				} else if (ents[i].time_usec >
					   node_ptr->power->time_usec) {
					delta_time =
						ents[i].time_usec -
						node_ptr->power->time_usec;
				} else if ((ents[i].time_usec <
					    node_ptr->power->time_usec) &&
					   ((ents[i].time_usec + usecs_day) >
					    node_ptr->power->time_usec)) {
					delta_time =
						(ents[i].time_usec +
						 usecs_day) -
						node_ptr->power->time_usec;
				}	
				if (delta_time &&
				    (node_ptr->power->joule_counter <
				     ents[i].joule_counter)) {
					delta_joules =
						ents[i].joule_counter -
						node_ptr->power->joule_counter;
					delta_joules *= 1000000;
					node_ptr->power->current_watts =
						delta_joules / delta_time;
				}
				node_ptr->power->joule_counter =
					ents[i].joule_counter;
				node_ptr->power->time_usec =
					ents[i].time_usec;
			}
			xfree(ents[i].node_name[j]);
		}
		xfree(ents[i].node_name);
	}
	xfree(ents);
	unlock_slurmctld(write_node_lock);
	xfree(cmd_resp);
}

static power_config_nodes_t *
_json_parse_array_energy(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	power_config_nodes_t *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(power_config_nodes_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_energy(jvalue, &ents[i]);
	}

	return ents;
}

static void _json_parse_energy(json_object *jobj, power_config_nodes_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p = NULL;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
//				info("%s: Key boolean %s", __func__, iter.key);
				break;
			case json_type_double:
//				info("%s: Key double %s", __func__, iter.key);
				break;
			case json_type_null:
//				info("%s: Key null %s", __func__, iter.key);
				break;
			case json_type_object:
//				info("%s: Key object %s", __func__, iter.key);
				break;
			case json_type_array:
//				info("%s: Key array %s", __func__, iter.key);
				break;
			case json_type_int:
//				info("%s: Key int %s", __func__, iter.key);
				x = json_object_get_int64(iter.val);
				if (!xstrcmp(iter.key, "energy_ctr")) {
					ent->joule_counter = x;
				} else if (!xstrcmp(iter.key, "nid")) {
					ent->node_cnt = 1;
					ent->node_name = xmalloc(sizeof(char*));
					ent->node_name[0] = xmalloc(10);
					snprintf(ent->node_name[0], 10,
						 "nid%5.5"PRId64"", x);
				}
				break;
			case json_type_string:
//				info("%s: Key string %s", __func__, iter.key);
				p = json_object_get_string(iter.val);
				if (!xstrcmp(iter.key, "time")) {
					ent->time_usec =
						_time_str2num((char *) p);
				}
				break;
		}
	}
}

static void _my_sleep(int add_secs)
{
	struct timespec ts = {0, 0};
	struct timeval  tv = {0, 0};

	if (gettimeofday(&tv, NULL)) {		/* Some error */
		sleep(1);
		return;
	}

	ts.tv_sec  = tv.tv_sec + add_secs;
	ts.tv_nsec = tv.tv_usec * 1000;
	slurm_mutex_lock(&term_lock);
	if (!stop_power)
		slurm_cond_timedwait(&term_cond, &term_lock, &ts);
	slurm_mutex_unlock(&term_lock);
}

/* Periodically attempt to re-balance power caps across nodes */
extern void *_power_agent(void *args)
{
	time_t now;
	double wait_time;
	static time_t last_balance_time = 0;
	/* Read jobs and nodes */
	slurmctld_lock_t read_locks = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	last_balance_time = time(NULL);
	while (!stop_power) {
		_my_sleep(1);
		if (stop_power)
			break;

		now = time(NULL);
		wait_time = difftime(now, last_balance_time);
		if (wait_time < balance_interval)
			continue;

		wait_time = difftime(now, last_cap_read);
		if (wait_time > 300) {		/* Every 5 minutes */
			/* Read current power caps for every node */
			_get_caps();		/* Has node write lock */
			last_cap_read = time(NULL);
		}

		wait_time = difftime(now, last_limits_read);
		if (wait_time > 600) {		/* Every 10 minutes */
			/* Read min/max power for every node */
			_get_capabilities();	/* Has node write lock */
			last_limits_read = time(NULL);
		}
		_get_node_energy_counter();	/* Has node write lock */
		_get_nodes_ready();		/* Has node write lock */
		lock_slurmctld(read_locks);
		if (set_watts)
			_set_node_caps();
		else if (cap_watts == 0)
			_clear_node_caps();
		else
			_rebalance_node_power();
		unlock_slurmctld(read_locks);
		if (debug_flag & DEBUG_FLAG_POWER)
			_log_node_power();
		_set_power_caps();
		last_balance_time = time(NULL);
	}
	return NULL;
}

/* Set power cap on all nodes to zero */
static void _clear_node_caps(void)
{
	struct node_record *node_ptr;
	int i;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_DOWN(node_ptr))
			continue;
		if (!node_ptr->power)
			continue;
		if (node_ptr->power->state != 1)  /* Not ready, no change */
			continue;
		node_ptr->power->new_cap_watts = 0;
	}
}

/* Set power cap on all nodes to the same value "set_watts" */
static void _set_node_caps(void)
{
	struct node_record *node_ptr;
	int i;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_DOWN(node_ptr))
			continue;
		if (!node_ptr->power)
			continue;
		if (node_ptr->power->state != 1)  /* Not ready, no change */
			continue;
		node_ptr->power->new_cap_watts =
			MAX(node_ptr->power->min_watts, set_watts);
		node_ptr->power->new_cap_watts =
			MIN(node_ptr->power->max_watts,
			    node_ptr->power->new_cap_watts);
	}
}

/* For every job needing level power caps across it's nodes, set each of its
 * node's power cap to the average cap based upon the global cap and recent
 * usage. */ 
static void _level_power_by_job(void)
{
	int i, i_first, i_last;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	struct node_record *node_ptr;
	uint32_t ave_watts, total_watts, total_nodes;
	uint32_t max_watts, min_watts;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr) || !job_ptr->node_bitmap)
			continue;
		if ((job_level == NO_VAL) &&
		    ((job_ptr->power_flags & SLURM_POWER_FLAGS_LEVEL) == 0))
			continue;

		max_watts = 0;
		min_watts = INFINITE;
		total_watts = 0;
		total_nodes = 0;
		i_first = bit_ffs(job_ptr->node_bitmap);
		if (i_first < 0)
			continue;
		i_last = bit_fls(job_ptr->node_bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (!node_ptr->power)
				continue;
			if (node_ptr->power->state != 1)/*Not ready, no change*/
				continue;
			total_watts += node_ptr->power->new_cap_watts;
			total_nodes++;
			if (max_watts < node_ptr->power->new_cap_watts)
				max_watts = node_ptr->power->new_cap_watts;
			if (min_watts > node_ptr->power->new_cap_watts)
				min_watts = node_ptr->power->new_cap_watts;
		}

		if (total_nodes < 2)
			continue;
		if (min_watts == max_watts)
			continue;
		ave_watts = total_watts / total_nodes;
		if (debug_flag & DEBUG_FLAG_POWER) {
			debug("%s: leveling power caps for %pJ (node_cnt:%u min:%u max:%u ave:%u)",
			      __func__, job_ptr, total_nodes,
			      min_watts, max_watts, ave_watts);
		}
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (!node_ptr->power)
				continue;
			if (node_ptr->power->state != 1)/*Not ready, no change*/
				continue;
			node_ptr->power->new_cap_watts = ave_watts;
		}
	}
	list_iterator_destroy(job_iterator);
}

/* Determine the new power cap required on each node based upon recent usage
 * and any power leveling by job */
static void _rebalance_node_power(void)
{
	struct node_record *node_ptr;
	uint32_t alloc_power = 0, avail_power = 0, ave_power, new_cap, tmp_u32;
	uint32_t node_power_raise_cnt = 0, node_power_needed = 0;
	uint32_t node_power_same_cnt = 0, node_power_lower_cnt = 0;
	time_t recent = time(NULL) - recent_job;
	int i;

	/* Lower caps on under used nodes */
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (!node_ptr->power)
			continue;
		if (IS_NODE_DOWN(node_ptr) ||
		    (node_ptr->power->state != 1)) {/* Not ready -> no change */
			if (node_ptr->power->cap_watts == 0) {
				node_ptr->power->new_cap_watts =
					node_ptr->power->max_watts;
			} else {
				node_ptr->power->new_cap_watts =
					node_ptr->power->cap_watts;
			}
			alloc_power += node_ptr->power->new_cap_watts;
			continue;
		}
		node_ptr->power->new_cap_watts = 0;
		if (node_ptr->power->new_job_time >= recent) {
			node_power_raise_cnt++;	/* Reset for new job below */
			continue;
		}
		if ((node_ptr->power->cap_watts == 0) ||   /* Not initialized */
		     (node_ptr->power->current_watts == 0)) {
			node_power_raise_cnt++;	/* Reset below */
			continue;
		}
		if (node_ptr->power->current_watts <
		    (node_ptr->power->cap_watts * lower_threshold/100)) {
			/* Lower cap by lower of
			 * 1) decrease_rate OR
			 * 2) half the excess power in the cap */
			ave_power = (node_ptr->power->cap_watts -
				     node_ptr->power->current_watts) / 2;
			tmp_u32 = node_ptr->power->max_watts -
				  node_ptr->power->min_watts;
			tmp_u32 = (tmp_u32 * decrease_rate) / 100;
			new_cap = node_ptr->power->cap_watts -
				  MIN(tmp_u32, ave_power);
			node_ptr->power->new_cap_watts =
				MAX(new_cap, node_ptr->power->min_watts);
			alloc_power += node_ptr->power->new_cap_watts;
			node_power_lower_cnt++;
		} else if (node_ptr->power->current_watts <=
			   (node_ptr->power->cap_watts * upper_threshold/100)) {
			/* In desired range. Retain previous cap */
			node_ptr->power->new_cap_watts =
				MAX(node_ptr->power->cap_watts,
				    node_ptr->power->min_watts);
			alloc_power += node_ptr->power->new_cap_watts;
			node_power_same_cnt++;
		} else {
			/* Node should get more power */
			node_power_raise_cnt++;
			node_power_needed += node_ptr->power->min_watts;
		}
	}

	if (cap_watts > alloc_power)
		avail_power = cap_watts - alloc_power;
	if ((alloc_power > cap_watts) || (node_power_needed > avail_power)) {
		/* When CapWatts changes, we might need to lower nodes more
		 * than the configured change rate specifications */
		uint32_t red1 = 0, red2 = 0, node_num;
		if (alloc_power > cap_watts)
			red1 = alloc_power - cap_watts;
		if (node_power_needed > avail_power)
			red2 = node_power_needed - avail_power;
		red1 = MAX(red1, red2);
		node_num = node_power_lower_cnt + node_power_same_cnt;
		if (node_num == 0)
			node_num = node_record_count;
		red1 /= node_num;
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (IS_NODE_DOWN(node_ptr))
				continue;
			if (!node_ptr->power || !node_ptr->power->new_cap_watts)
				continue;
			tmp_u32 = node_ptr->power->new_cap_watts -
				  node_ptr->power->min_watts;
			tmp_u32 = MIN(tmp_u32, red1);
			node_ptr->power->new_cap_watts -= tmp_u32;
			alloc_power -= tmp_u32;
		}
		avail_power = cap_watts - alloc_power;
	}
	if (debug_flag & DEBUG_FLAG_POWER) {
		info("%s: distributing %u watts over %d nodes",
		     __func__, avail_power, node_power_raise_cnt);
	}

	/* Distribute rest of power cap on remaining nodes. */
	if (node_power_raise_cnt) {
		ave_power = avail_power / node_power_raise_cnt;
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (IS_NODE_DOWN(node_ptr))
				continue;
			if (!node_ptr->power || (node_ptr->power->state != 1))
				continue;
			if (node_ptr->power->new_cap_watts)    /* Already set */
				continue;
			if (node_ptr->power->new_job_time >= recent) {
				/* Recent change in workload, do full reset */
				new_cap = ave_power;
			} else {
				/* No recent change in workload, do partial
				 * power cap reset (add up to increase_rate) */
				tmp_u32 = node_ptr->power->max_watts -
					  node_ptr->power->min_watts;
				tmp_u32 = (tmp_u32 * increase_rate) / 100;
				new_cap = node_ptr->power->cap_watts + tmp_u32;
				new_cap = MIN(new_cap, ave_power);
			}
			node_ptr->power->new_cap_watts =
				MAX(new_cap, node_ptr->power->min_watts);
			node_ptr->power->new_cap_watts =
				MIN(node_ptr->power->new_cap_watts,
				    node_ptr->power->max_watts);
			if (avail_power > node_ptr->power->new_cap_watts)
				avail_power -= node_ptr->power->new_cap_watts;
			else
				avail_power = 0;
			node_power_raise_cnt--;
			if (node_power_raise_cnt == 0)
				break;	/* No more nodes to modify */
			if (node_ptr->power->new_cap_watts != ave_power) {
				/* Re-normalize */
				ave_power = avail_power / node_power_raise_cnt;
			}
		}
	}

	if (job_level != 0)
		_level_power_by_job();
}

static void _log_node_power(void)
{
	struct node_record *node_ptr;
	uint32_t total_current_watts = 0, total_min_watts = 0;
	uint32_t total_max_watts = 0, total_cap_watts = 0;
	uint32_t total_new_cap_watts = 0, total_ready_cnt = 0;
	int i;

	/* Build and log summary table of required updates to power caps */
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		char *ready_str;
		if (!node_ptr->power)
			continue;
		if (node_ptr->power->state == 1) {
			ready_str = "YES";
			total_ready_cnt++;
		} else
			ready_str = "NO";
		info("Node:%s CurWatts:%3u MinWatts:%3u "
		     "MaxWatts:%3u OldCap:%3u NewCap:%3u Ready:%s",
		     node_ptr->name, node_ptr->power->current_watts,
		     node_ptr->power->min_watts,
		     node_ptr->power->max_watts,
		     node_ptr->power->cap_watts,
		     node_ptr->power->new_cap_watts, ready_str);
		total_current_watts += node_ptr->power->current_watts;
		total_min_watts     += node_ptr->power->min_watts;
		total_max_watts     += node_ptr->power->max_watts;
		if (node_ptr->power->cap_watts)
			total_cap_watts     += node_ptr->power->cap_watts;
		else
			total_cap_watts     += node_ptr->power->max_watts;
		if (node_ptr->power->new_cap_watts)
			total_new_cap_watts += node_ptr->power->new_cap_watts;
		else if (node_ptr->power->cap_watts)
			total_new_cap_watts += node_ptr->power->cap_watts;
		else
			total_new_cap_watts += node_ptr->power->max_watts;
	}
	info("TOTALS CurWatts:%u MinWatts:%u MaxWatts:%u OldCap:%u "
	     "NewCap:%u ReadyCnt:%u",
	     total_current_watts, total_min_watts, total_max_watts,
	     total_cap_watts, total_new_cap_watts, total_ready_cnt);
}

static void _set_power_caps(void)
{
	struct node_record *node_ptr;
	char *cmd_resp, *json = NULL, *script_argv[4];
	int i, status = 0;
	DEF_TIMERS;

	script_argv[0] = capmc_path;
	script_argv[1] = "json";
	script_argv[2] = "--resource=/capmc/set_power_cap";
	script_argv[3] = NULL;

	/* Pass 1, decrease power for select nodes */
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_DOWN(node_ptr) ||
		    !node_ptr->power ||
		    (node_ptr->power->state != 1) ||
		    (node_ptr->power->cap_watts <=
		     node_ptr->power->new_cap_watts))
			continue;
		node_ptr->power->cap_watts = node_ptr->power->new_cap_watts;
		if (json)
			xstrcat(json, ",\n ");
		else
			xstrcat(json, "{ \"nids\":[\n ");
		xstrfmtcat(json,
			   "{ \"nid\":%s, \"controls\":[ "
			   "{ \"name\":\"node\", \"val\":%u } ] }",
			   _node_name2nid(node_ptr->name),
			   node_ptr->power->new_cap_watts);
	}
	if (json) {
		xstrcat(json, "\n ]\n}\n");
		START_TIMER;
		cmd_resp = power_run_script("capmc", capmc_path, script_argv,
					    set_timeout, json, &status);
		END_TIMER;
		if (status != 0) {
			error("%s: capmc %s %s: %s",
			      __func__, script_argv[1], script_argv[2],
			      cmd_resp);
			xfree(cmd_resp);
			last_cap_read = 0;	/* Read node caps again */
			return;
		} else if (debug_flag & DEBUG_FLAG_POWER) {
			info("%s: capmc %s %s %s",
			     __func__, script_argv[1], script_argv[2],
			     TIME_STR);
		}
		xfree(cmd_resp);
		xfree(json);
	}

	/* Pass 2, increase power for select nodes */
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (IS_NODE_DOWN(node_ptr) ||
		    !node_ptr->power ||
		    (node_ptr->power->state != 1) ||
		    (node_ptr->power->cap_watts >=
		     node_ptr->power->new_cap_watts))
			continue;
		node_ptr->power->cap_watts = node_ptr->power->new_cap_watts;
		if (json)
			xstrcat(json, ",\n ");
		else
			xstrcat(json, "{ \"nids\":[\n ");
		xstrfmtcat(json,
			   "{ \"nid\":%s, \"controls\":[ "
			   "{ \"name\":\"node\", \"val\":%u } ] }",
			   _node_name2nid(node_ptr->name),
			   node_ptr->power->new_cap_watts);
	}
	if (json) {
		xstrcat(json, "\n ]\n}\n");
		START_TIMER;
		cmd_resp = power_run_script("capmc", capmc_path, script_argv,
					    set_timeout, json, &status);
		END_TIMER;
		if (status != 0) {
			error("%s: capmc %s %s: %s",
			      __func__, script_argv[1], script_argv[2],
			      cmd_resp);
			xfree(cmd_resp);
			last_cap_read = 0;	/* Read node caps again */
			return;
		} else if (debug_flag & DEBUG_FLAG_POWER) {
			info("%s: capmc %s %s %s",
			     __func__, script_argv[1], script_argv[2],
			     TIME_STR);
		}
		xfree(cmd_resp);
		xfree(json);
	}
}

/* Terminate power thread */
static void _stop_power_agent(void)
{
	slurm_mutex_lock(&term_lock);
	stop_power = true;
	slurm_cond_signal(&term_cond);
	slurm_mutex_unlock(&term_lock);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	if (!run_in_daemon("slurmctld"))
		return SLURM_SUCCESS;

	slurm_mutex_lock(&thread_flag_mutex);
	if (power_thread) {
		debug2("Power thread already running, not starting another");
		slurm_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	_load_config();
	/* Since we do a join on thread later, don't make it detached */
	slurm_thread_create(&power_thread, _power_agent, NULL);
	slurm_mutex_unlock(&thread_flag_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern void fini(void)
{
	slurm_mutex_lock(&thread_flag_mutex);
	if (power_thread) {
		_stop_power_agent();
		pthread_join(power_thread, NULL);
		power_thread = 0;
		xfree(capmc_path);
		xfree(full_nid_string);
	}
	slurm_mutex_unlock(&thread_flag_mutex);
}

/* Read the configuration file */
extern void power_p_reconfig(void)
{
	slurm_mutex_lock(&thread_flag_mutex);
	_load_config();
	slurm_mutex_unlock(&thread_flag_mutex);
}

/* Note that a suspended job has been resumed */
extern void power_p_job_resume(struct job_record *job_ptr)
{
	set_node_new_job(job_ptr, node_record_table_ptr);
}

/* Note that a job has been allocated resources and is ready to start */
extern void power_p_job_start(struct job_record *job_ptr)
{
	set_node_new_job(job_ptr, node_record_table_ptr);
}
