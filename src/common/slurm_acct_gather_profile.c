/*****************************************************************************\
 *  slurm_acct_gather_profile.c - implementation-independent job profile
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_strcasestr.h"

typedef struct slurm_acct_gather_profile_ops {
	void (*conf_options)    (s_p_options_t **full_options,
				 int *full_options_cnt);
	void (*conf_set)        (s_p_hashtbl_t *tbl);
	void* (*get)            (enum acct_gather_profile_info info_type,
				 void *data);
	int (*node_step_start)  (slurmd_job_t*);
	int (*node_step_end)    (void);
	int (*task_start)       (uint32_t);
	int (*task_end)         (pid_t);
	int (*add_sample_data)  (uint32_t, void*);
} slurm_acct_gather_profile_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_acct_gather_profile_ops_t.
 */
static const char *syms[] = {
	"acct_gather_profile_p_conf_options",
	"acct_gather_profile_p_conf_set",
	"acct_gather_profile_p_get",
	"acct_gather_profile_p_node_step_start",
	"acct_gather_profile_p_node_step_end",
	"acct_gather_profile_p_task_start",
	"acct_gather_profile_p_task_end",
	"acct_gather_profile_p_add_sample_data",
};

acct_gather_profile_timer_t acct_gather_profile_timer[PROFILE_CNT];
pthread_mutex_t acct_gather_profile_timer_mutex = PTHREAD_MUTEX_INITIALIZER;

static slurm_acct_gather_profile_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t profile_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static bool poll_started = 0;
static pthread_t timer_thread_id = 0;

static void *_timer_thread(void *args)
{
	time_t last_time = time(NULL);
	int i, now, diff;

	while (init_run) {
		now = time(NULL);
		diff = now - last_time;
		last_time = now;

		slurm_mutex_lock(&acct_gather_profile_timer_mutex);
		for (i=0; i<PROFILE_CNT; i++) {
			if (!acct_gather_profile_timer[i].freq
			    || (diff < acct_gather_profile_timer[i].freq))
				continue;
			/* signal poller to start */
			pthread_cond_signal(
				&acct_gather_profile_timer[i].notify);
		}
		slurm_mutex_unlock(&acct_gather_profile_timer_mutex);
		sleep(1);
	}

	return NULL;
}

extern int acct_gather_profile_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "acct_gather_profile";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_acct_gather_profile_type();

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);
	if (retval == SLURM_SUCCESS)
		retval = acct_gather_conf_init();

	return retval;
}

extern int acct_gather_profile_fini(void)
{
	int rc, i;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;

	slurm_mutex_lock(&acct_gather_profile_timer_mutex);
	for (i=0; i < PROFILE_CNT; i++) {
		if (acct_gather_profile_timer[i].freq)
			pthread_cond_destroy(
				&acct_gather_profile_timer[i].notify);
		acct_gather_profile_timer[i].freq = 0;
	}
	slurm_mutex_unlock(&acct_gather_profile_timer_mutex);

	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

extern char *acct_gather_profile_to_string(uint32_t profile)
{
	static char profile_str[128];

	profile_str[0] = '\0';
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
	return profile_str;
}

extern uint32_t acct_gather_profile_from_string(char *profile_str)
{
	uint32_t profile = ACCT_GATHER_PROFILE_NOT_SET;

        if (!profile_str) {
	} else if (slurm_strcasestr(profile_str, "none"))
		profile = ACCT_GATHER_PROFILE_NONE;
	else if (slurm_strcasestr(profile_str, "all"))
		profile = ACCT_GATHER_PROFILE_ALL;
	else {
		if (slurm_strcasestr(profile_str, "energy"))
			profile |= ACCT_GATHER_PROFILE_ENERGY;
		if (slurm_strcasestr(profile_str, "task"))
			profile |= ACCT_GATHER_PROFILE_TASK;

		if (slurm_strcasestr(profile_str, "lustre"))
			profile |= ACCT_GATHER_PROFILE_LUSTRE;

		if (slurm_strcasestr(profile_str, "network"))
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
	if (!strcasecmp(series_str, "energy"))
		return ACCT_GATHER_PROFILE_ENERGY;
	else if (!strcasecmp(series_str, "task"))
		return ACCT_GATHER_PROFILE_TASK;
	else if (!strcasecmp(series_str, "lustre"))
		return ACCT_GATHER_PROFILE_LUSTRE;
	else if (!strcasecmp(series_str, "network"))
		return ACCT_GATHER_PROFILE_NETWORK;

	return ACCT_GATHER_PROFILE_NOT_SET;
}

extern int acct_gather_profile_startpoll(slurmd_job_t *job, char *freq,
					 char *freq_def)
{
	int retval = SLURM_SUCCESS;
	pthread_attr_t attr;
	int i;
	uint32_t profile = ACCT_GATHER_PROFILE_NOT_SET;

	if (acct_gather_profile_init() < 0)
		return SLURM_ERROR;

	if (poll_started) {
		error("acct_gather_profile_startpoll: poll already started!");
		return retval;
	}
	poll_started = true;

	(*(ops.get))(ACCT_GATHER_PROFILE_RUNNING, &profile);
	xassert(profile != ACCT_GATHER_PROFILE_NOT_SET);

	for (i=0; i < PROFILE_CNT; i++) {
		char *type = NULL;

		memset(&acct_gather_profile_timer[i], 0,
		       sizeof(acct_gather_profile_timer_t));
		switch (i) {
		case PROFILE_ENERGY:
			if (!(profile & ACCT_GATHER_PROFILE_ENERGY))
				break;
			if ((type = slurm_strcasestr(freq, "energy=")))
				acct_gather_profile_timer[i].freq =
					atol(type+7);
			else if ((type = slurm_strcasestr(
					  freq_def, "energy=")))
				acct_gather_profile_timer[i].freq =
					atol(type+7);

			break;
		case PROFILE_TASK:
			/* Always set up the task (always first) to be
			   done since it is used to control memory
			   consumption and such.  It will check
			   profile inside it's plugin.
			*/
			acct_gather_profile_timer[i].freq = atol(freq);
			if (acct_gather_profile_timer[i].freq == -1) {
				if ((type = slurm_strcasestr(freq, "task=")))
					acct_gather_profile_timer[i].freq =
						atol(type+5);
				else if ((type = slurm_strcasestr(
						  freq_def, "task=")))
					acct_gather_profile_timer[i].freq =
						atol(type+5);
			}

			break;
		case PROFILE_FILESYSTEM:
			if (!(profile & ACCT_GATHER_PROFILE_LUSTRE))
				break;
			if ((type = slurm_strcasestr(freq, "filesystem=")))
				acct_gather_profile_timer[i].freq =
					atol(type+11);
			else if ((type = slurm_strcasestr(
					  freq_def, "filesystem=")))
				acct_gather_profile_timer[i].freq =
					atol(type+11);

			break;
		case PROFILE_NETWORK:
			if (!(profile & ACCT_GATHER_PROFILE_NETWORK))
				break;
			if ((type = slurm_strcasestr(freq, "network=")))
				acct_gather_profile_timer[i].freq =
					atol(type+8);
			else if ((type = slurm_strcasestr(
					  freq_def, "network=")))
				acct_gather_profile_timer[i].freq =
					atol(type+8);

			break;
		default:
			fatal("Unhandled profile option %d please update "
			      "slurm_acct_gather_profile.c "
			      "(acct_gather_profile_startpoll)", i);
		}
		if (acct_gather_profile_timer[i].freq)
			pthread_cond_init(&acct_gather_profile_timer[i].notify,
					  NULL);
	}

	/* create polling thread */
	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	if  (pthread_create(&timer_thread_id, &attr,
			    &_timer_thread, NULL)) {
		debug("acct_gather_profile_startpoll failed to create "
		      "_timer_thread: %m");
	} else
		debug3("acct_gather_profile_startpoll dynamic logging enabled");
	slurm_attr_destroy(&attr);

	return retval;
}

extern void acct_gather_profile_g_conf_options(s_p_options_t **full_options,
					       int *full_options_cnt)
{
	if (acct_gather_profile_init() < 0)
		return;
	(*(ops.conf_options))(full_options, full_options_cnt);
	return;
}

extern void acct_gather_profile_g_conf_set(s_p_hashtbl_t *tbl)
{
	if (acct_gather_profile_init() < 0)
		return;

	(*(ops.conf_set))(tbl);
	return;
}

extern void acct_gather_profile_g_get(enum acct_gather_profile_info info_type,
				      void *data)
{
	if (acct_gather_profile_init() < 0)
		return;

	(*(ops.get))(info_type, data);
	return;
}

extern int acct_gather_profile_g_node_step_start(slurmd_job_t* job)
{
	if (acct_gather_profile_init() < 0)
		return SLURM_ERROR;

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

	if (acct_gather_profile_init() < 0)
		return retval;

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.task_start))(taskid);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_task_end(pid_t taskpid)
{
	int retval = SLURM_ERROR;

	if (acct_gather_profile_init() < 0)
		return retval;

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.task_end))(taskpid);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_add_sample_data(uint32_t type, void* data)
{
	int retval = SLURM_ERROR;

	if (acct_gather_profile_init() < 0)
		return retval;

	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.add_sample_data))(type, data);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}
