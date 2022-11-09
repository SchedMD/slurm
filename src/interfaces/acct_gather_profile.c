/*****************************************************************************\
 *  slurm_acct_gather_profile.c - implementation-independent job profile
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/interfaces/acct_gather_filesystem.h"
#include "src/interfaces/acct_gather_interconnect.h"
#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* These 2 should remain the same. */
#define SLEEP_TIME 1
#define USLEEP_TIME 1000000

typedef struct slurm_acct_gather_profile_ops {
	void (*child_forked)    (void);
	void (*conf_options)    (s_p_options_t **full_options,
				 int *full_options_cnt);
	void (*conf_set)        (s_p_hashtbl_t *tbl);
	void* (*get)            (enum acct_gather_profile_info info_type,
				 void *data);
	int (*node_step_start)  (stepd_step_rec_t*);
	int (*node_step_end)    (void);
	int (*task_start)       (uint32_t);
	int (*task_end)         (pid_t);
	int64_t (*create_group)(const char*);
	int (*create_dataset)   (const char*, int64_t,
				 acct_gather_profile_dataset_t *);
	int (*add_sample_data)  (uint32_t, void*, time_t);
	void (*conf_values)     (List *data);
	bool (*is_active)     (uint32_t);

} slurm_acct_gather_profile_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_acct_gather_profile_ops_t.
 */
static const char *syms[] = {
	"acct_gather_profile_p_child_forked",
	"acct_gather_profile_p_conf_options",
	"acct_gather_profile_p_conf_set",
	"acct_gather_profile_p_get",
	"acct_gather_profile_p_node_step_start",
	"acct_gather_profile_p_node_step_end",
	"acct_gather_profile_p_task_start",
	"acct_gather_profile_p_task_end",
	"acct_gather_profile_p_create_group",
	"acct_gather_profile_p_create_dataset",
	"acct_gather_profile_p_add_sample_data",
	"acct_gather_profile_p_conf_values",
	"acct_gather_profile_p_is_active",
};

acct_gather_profile_timer_t acct_gather_profile_timer[PROFILE_CNT];

static bool acct_gather_profile_running = false;
static pthread_mutex_t profile_running_mutex = PTHREAD_MUTEX_INITIALIZER;

static slurm_acct_gather_profile_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t profile_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timer_thread_id = 0;
static pthread_mutex_t timer_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_thread_cond = PTHREAD_COND_INITIALIZER;
static bool init_run = false;

static void _set_freq(int type, char *freq, char *freq_def)
{
	if ((acct_gather_profile_timer[type].freq =
	     acct_gather_parse_freq(type, freq)) == -1)
		if ((acct_gather_profile_timer[type].freq =
		     acct_gather_parse_freq(type, freq_def)) == -1)
			acct_gather_profile_timer[type].freq = 0;
}

/*
 * This thread wakes up other profiling threads in the jobacct plugins,
 * and operates on a 1-second granularity.
 */

static void *_timer_thread(void *args)
{
	int i, now, diff;
	struct timeval tvnow;
	struct timespec abs;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "acctg_prof", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m",
		      __func__, "acctg_prof");
	}
#endif

	/* setup timer */
	gettimeofday(&tvnow, NULL);
	abs.tv_sec = tvnow.tv_sec;
	abs.tv_nsec = tvnow.tv_usec * 1000;

	while (init_run && acct_gather_profile_test()) {
		slurm_mutex_lock(&g_context_lock);
		now = time(NULL);

		for (i=0; i<PROFILE_CNT; i++) {
			if (acct_gather_suspend_test()) {
				/* Handle suspended time as if it
				 * didn't happen */
				if (!acct_gather_profile_timer[i].freq)
					continue;
				if (acct_gather_profile_timer[i].last_notify)
					acct_gather_profile_timer[i].
						last_notify += SLEEP_TIME;
				else
					acct_gather_profile_timer[i].
						last_notify = now;
				continue;
			}

			diff = now - acct_gather_profile_timer[i].last_notify;
			/* info ("%d is %d and %d", i, */
			/*       acct_gather_profile_timer[i].freq, */
			/*       diff); */
			if (!acct_gather_profile_timer[i].freq
			    || (diff < acct_gather_profile_timer[i].freq))
				continue;
			if (!acct_gather_profile_test())
				break;	/* Shutting down */
			debug2("profile signaling type %s",
			       acct_gather_profile_type_t_name(i));

			/* signal poller to start */
			slurm_mutex_lock(&acct_gather_profile_timer[i].
					 notify_mutex);
			slurm_cond_signal(
				&acct_gather_profile_timer[i].notify);
			slurm_mutex_unlock(&acct_gather_profile_timer[i].
					   notify_mutex);
			acct_gather_profile_timer[i].last_notify = now;
		}
		slurm_mutex_unlock(&g_context_lock);

		/*
		 * Sleep until the next second interval, or until signaled
		 * to shutdown by acct_gather_profile_fini().
		 */

		abs.tv_sec += 1;
		slurm_mutex_lock(&timer_thread_mutex);
		slurm_cond_timedwait(&timer_thread_cond, &timer_thread_mutex,
				     &abs);
		slurm_mutex_unlock(&timer_thread_mutex);
	}

	return NULL;
}

extern int acct_gather_profile_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "acct_gather_profile";

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	g_context = plugin_context_create(plugin_type,
					  slurm_conf.acct_gather_profile_type,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.acct_gather_profile_type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	if (retval != SLURM_SUCCESS)
		fatal("can not open the %s plugin",
		      slurm_conf.acct_gather_profile_type);

	return retval;
}

extern int acct_gather_profile_fini(void)
{
	int rc = SLURM_SUCCESS, i;

	if (!g_context)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);

	if (!g_context)
		goto done;

	init_run = false;

	for (i=0; i < PROFILE_CNT; i++) {
		switch (i) {
		case PROFILE_ENERGY:
			acct_gather_energy_fini();
			break;
		case PROFILE_TASK:
			jobacct_gather_fini();
			break;
		case PROFILE_FILESYSTEM:
			acct_gather_filesystem_fini();
			break;
		case PROFILE_NETWORK:
			acct_gather_interconnect_fini();
			break;
		default:
			fatal("Unhandled profile option %d please update "
			      "slurm_acct_gather_profile.c "
			      "(acct_gather_profile_fini)", i);
		}
	}

	if (timer_thread_id) {
		slurm_mutex_lock(&timer_thread_mutex);
		slurm_cond_signal(&timer_thread_cond);
		slurm_mutex_unlock(&timer_thread_mutex);
		pthread_join(timer_thread_id, NULL);
	}

	rc = plugin_context_destroy(g_context);
	g_context = NULL;
done:
	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

extern void acct_gather_profile_to_string_r(uint32_t profile,
					    char *profile_str)
{
	if (profile == ACCT_GATHER_PROFILE_NOT_SET)
		strcat(profile_str, "NotSet");
	else if (profile == ACCT_GATHER_PROFILE_NONE)
		strcat(profile_str, "None");
	else {
		if (profile & ACCT_GATHER_PROFILE_ENERGY)
			strcat(profile_str, "Energy");
		if (profile & ACCT_GATHER_PROFILE_LUSTRE) {
			if (profile_str[0])
				strcat(profile_str, ",");
			strcat(profile_str, "Lustre");
		}
		if (profile & ACCT_GATHER_PROFILE_NETWORK) {
			if (profile_str[0])
				strcat(profile_str, ",");
			strcat(profile_str, "Network");
		}
		if (profile & ACCT_GATHER_PROFILE_TASK) {
			if (profile_str[0])
				strcat(profile_str, ",");
			strcat(profile_str, "Task");
		}
	}
}

extern char *acct_gather_profile_to_string(uint32_t profile)
{
	static char profile_str[128];

	profile_str[0] = '\0';
	acct_gather_profile_to_string_r(profile, profile_str);

	return profile_str;
}

extern uint32_t acct_gather_profile_from_string(const char *profile_str)
{
	uint32_t profile = ACCT_GATHER_PROFILE_NOT_SET;

        if (!profile_str) {
	} else if (xstrcasestr(profile_str, "none"))
		profile = ACCT_GATHER_PROFILE_NONE;
	else if (xstrcasestr(profile_str, "all"))
		profile = ACCT_GATHER_PROFILE_ALL;
	else {
		if (xstrcasestr(profile_str, "energy"))
			profile |= ACCT_GATHER_PROFILE_ENERGY;
		if (xstrcasestr(profile_str, "task"))
			profile |= ACCT_GATHER_PROFILE_TASK;

		if (xstrcasestr(profile_str, "lustre"))
			profile |= ACCT_GATHER_PROFILE_LUSTRE;

		if (xstrcasestr(profile_str, "network"))
			profile |= ACCT_GATHER_PROFILE_NETWORK;
	}

	return profile;
}

extern char *acct_gather_profile_type_to_string(uint32_t series)
{
	if (series == ACCT_GATHER_PROFILE_ENERGY)
		return "Energy";
	else if (series == ACCT_GATHER_PROFILE_TASK)
		return "Task";
	else if (series == ACCT_GATHER_PROFILE_LUSTRE)
		return "Lustre";
	else if (series == ACCT_GATHER_PROFILE_NETWORK)
		return "Network";

	return "Unknown";
}

extern uint32_t acct_gather_profile_type_from_string(char *series_str)
{
	if (!xstrcasecmp(series_str, "energy"))
		return ACCT_GATHER_PROFILE_ENERGY;
	else if (!xstrcasecmp(series_str, "task"))
		return ACCT_GATHER_PROFILE_TASK;
	else if (!xstrcasecmp(series_str, "lustre"))
		return ACCT_GATHER_PROFILE_LUSTRE;
	else if (!xstrcasecmp(series_str, "network"))
		return ACCT_GATHER_PROFILE_NETWORK;

	return ACCT_GATHER_PROFILE_NOT_SET;
}

extern char *acct_gather_profile_type_t_name(acct_gather_profile_type_t type)
{
	switch (type) {
	case PROFILE_ENERGY:
		return "Energy";
		break;
	case PROFILE_TASK:
		return "Task";
		break;
	case PROFILE_FILESYSTEM:
		return "Lustre";
		break;
	case PROFILE_NETWORK:
		return "Network";
		break;
	case PROFILE_CNT:
		return "CNT?";
		break;
	default:
		fatal("Unhandled profile option %d please update "
		      "slurm_acct_gather_profile.c "
		      "(acct_gather_profile_type_t_name)", type);
	}

	return "Unknown";
}

extern char *acct_gather_profile_dataset_str(
	acct_gather_profile_dataset_t *dataset, void *data,
	char *str, int str_len)
{
	int cur_loc = 0;

        while (dataset && (dataset->type != PROFILE_FIELD_NOT_SET)) {
		switch (dataset->type) {
		case PROFILE_FIELD_UINT64:
			cur_loc += snprintf(str+cur_loc, str_len-cur_loc,
					    "%s%s=%"PRIu64,
					    cur_loc ? " " : "",
					    dataset->name, *(uint64_t *)data);
			data += sizeof(uint64_t);
			break;
		case PROFILE_FIELD_DOUBLE:
			cur_loc += snprintf(str+cur_loc, str_len-cur_loc,
					    "%s%s=%lf",
					    cur_loc ? " " : "",
					    dataset->name, *(double *)data);
			data += sizeof(double);
			break;
		case PROFILE_FIELD_NOT_SET:
			break;
		}

		if (cur_loc >= str_len)
			break;
		dataset++;
	}

	return str;
}

extern int acct_gather_profile_startpoll(char *freq, char *freq_def)
{
	int i;
	uint32_t profile = ACCT_GATHER_PROFILE_NOT_SET;

	xassert(init_run);

	slurm_mutex_lock(&profile_running_mutex);
	if (acct_gather_profile_running) {
		slurm_mutex_unlock(&profile_running_mutex);
		error("acct_gather_profile_startpoll: poll already started!");
		return SLURM_SUCCESS;
	}
	acct_gather_profile_running = true;
	slurm_mutex_unlock(&profile_running_mutex);

	(*(ops.get))(ACCT_GATHER_PROFILE_RUNNING, &profile);
	xassert(profile != ACCT_GATHER_PROFILE_NOT_SET);

	for (i=0; i < PROFILE_CNT; i++) {
		memset(&acct_gather_profile_timer[i], 0,
		       sizeof(acct_gather_profile_timer_t));
		slurm_cond_init(&acct_gather_profile_timer[i].notify, NULL);
		slurm_mutex_init(&acct_gather_profile_timer[i].notify_mutex);

		switch (i) {
		case PROFILE_ENERGY:
			if (!(profile & ACCT_GATHER_PROFILE_ENERGY))
				break;
			_set_freq(i, freq, freq_def);

			acct_gather_energy_startpoll(
				acct_gather_profile_timer[i].freq);
			break;
		case PROFILE_TASK:
			/* Always set up the task (always first) to be
			   done since it is used to control memory
			   consumption and such.  It will check
			   profile inside it's plugin.
			*/
			_set_freq(i, freq, freq_def);

			jobacct_gather_startpoll(
				acct_gather_profile_timer[i].freq);

			break;
		case PROFILE_FILESYSTEM:
			if (!(profile & ACCT_GATHER_PROFILE_LUSTRE))
				break;
			_set_freq(i, freq, freq_def);

			acct_gather_filesystem_startpoll(
				acct_gather_profile_timer[i].freq);
			break;
		case PROFILE_NETWORK:
			if (!(profile & ACCT_GATHER_PROFILE_NETWORK))
				break;
			_set_freq(i, freq, freq_def);

			acct_gather_interconnect_startpoll(
				acct_gather_profile_timer[i].freq);
			break;
		default:
			fatal("Unhandled profile option %d please update "
			      "slurm_acct_gather_profile.c "
			      "(acct_gather_profile_startpoll)", i);
		}
	}

	/* create polling thread */
	slurm_thread_create(&timer_thread_id, _timer_thread, NULL);

	debug3("acct_gather_profile_startpoll dynamic logging enabled");

	return SLURM_SUCCESS;
}

extern void acct_gather_profile_endpoll(void)
{
	int i;

	slurm_mutex_lock(&profile_running_mutex);
	if (!acct_gather_profile_running) {
		slurm_mutex_unlock(&profile_running_mutex);
		debug2("acct_gather_profile_startpoll: poll already ended!");
		return;
	}
	acct_gather_profile_running = false;
	slurm_mutex_unlock(&profile_running_mutex);

	for (i=0; i < PROFILE_CNT; i++) {
		/* end remote threads */
		slurm_mutex_lock(&acct_gather_profile_timer[i].notify_mutex);
		slurm_cond_signal(&acct_gather_profile_timer[i].notify);
		slurm_mutex_unlock(&acct_gather_profile_timer[i].notify_mutex);
		acct_gather_profile_timer[i].freq = 0;
		switch (i) {
		case PROFILE_ENERGY:
			break;
		case PROFILE_TASK:
			jobacct_gather_endpoll();
			break;
		case PROFILE_FILESYSTEM:
			break;
		case PROFILE_NETWORK:
			break;
		default:
			fatal("Unhandled profile option %d please update "
			      "slurm_acct_gather_profile.c "
			      "(acct_gather_profile_endpoll)", i);
		}
	}
}

extern int acct_gather_profile_g_child_forked(void)
{
	xassert(init_run);

	(*(ops.child_forked))();
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_g_conf_options(s_p_options_t **full_options,
					       int *full_options_cnt)
{
	xassert(init_run);

	(*(ops.conf_options))(full_options, full_options_cnt);
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_g_conf_set(s_p_hashtbl_t *tbl)
{
	xassert(init_run);

	(*(ops.conf_set))(tbl);
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_g_get(enum acct_gather_profile_info info_type,
				      void *data)
{
	xassert(init_run);

	(*(ops.get))(info_type, data);
	return SLURM_SUCCESS;
}

extern int acct_gather_profile_g_node_step_start(stepd_step_rec_t* job)
{
	xassert(init_run);

	return (*(ops.node_step_start))(job);
}

extern int acct_gather_profile_g_node_step_end(void)
{
	int retval = SLURM_ERROR;


	retval = (*(ops.node_step_end))();
	return retval;
}

extern int acct_gather_profile_g_task_start(uint32_t taskid)
{
	int retval = SLURM_ERROR;

	xassert(init_run);

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.task_start))(taskid);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_task_end(pid_t taskpid)
{
	int retval = SLURM_ERROR;

	xassert(init_run);

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.task_end))(taskpid);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int64_t acct_gather_profile_g_create_group(const char *name)
{
	int64_t retval = SLURM_ERROR;

	xassert(init_run);

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.create_group))(name);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_create_dataset(
	const char *name, int64_t parent,
	acct_gather_profile_dataset_t *dataset)
{
	int retval = SLURM_ERROR;

	xassert(init_run);

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.create_dataset))(name, parent, dataset);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_add_sample_data(int dataset_id, void* data,
						 time_t sample_time)
{
	int retval = SLURM_ERROR;

	xassert(init_run);

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.add_sample_data))(dataset_id, data, sample_time);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern void acct_gather_profile_g_conf_values(void *data)
{
	xassert(init_run);

	(*(ops.conf_values))(data);
}

extern bool acct_gather_profile_g_is_active(uint32_t type)
{
	xassert(init_run);

	return (*(ops.is_active))(type);
}

extern bool acct_gather_profile_test(void)
{
	bool rc;
	slurm_mutex_lock(&profile_running_mutex);
	rc = acct_gather_profile_running;
	slurm_mutex_unlock(&profile_running_mutex);
	return rc;
}
