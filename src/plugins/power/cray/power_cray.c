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

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/plugins/power/common/power_common.h"
#include "src/slurmctld/locks.h"

#define DEFAULT_BALANCE_INTERVAL 30

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
static bool stop_power = false;
static pthread_t power_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;

/*********************** local functions *********************/
static void _load_config(void);
extern void *_power_agent(void *args);
static void _stop_power_agent(void);

/* Parse PowerParameters configuration */
static void _load_config(void)
{
	char *sched_params, *tmp_ptr;

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

	xfree(sched_params);
}

/* Periodically attempt to re-balance power caps across nodes */
extern void *_power_agent(void *args)
{
	time_t now;
	double wait_time;
	static time_t last_balance_time = 0;
	/* Read jobs and nodes */
	slurmctld_lock_t read_locks = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	List job_power_list;
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

		lock_slurmctld(read_locks);
		get_cluster_power(node_record_table_ptr, node_record_count,
				  &alloc_watts, &used_watts);
		job_power_list = get_job_power(job_list, node_record_table_ptr);
//FIXME: power re-balancing decisions here
		FREE_NULL_LIST(job_power_list);
		last_balance_time = time(NULL);
		unlock_slurmctld(read_locks);
		_load_config();
	}
	return NULL;
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

//FIXME	if (bg_recover == NOT_FROM_CONTROLLER)
//		return SLURM_SUCCESS;

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
