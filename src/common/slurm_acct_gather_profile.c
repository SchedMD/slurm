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

typedef struct profile_task_info {
	uint32_t        gtid;	    /* global task id	*/
	pid_t           pid;	    /* slurm taskid	*/
} profile_task_pid_t;

static int nodetasks = 0;
static profile_task_pid_t* pid2task = NULL;

typedef struct slurm_acct_gather_profile_ops {
	int (*controller_start) ();
	int (*node_step_start)  (slurmd_job_t*);
	int (*node_step_end)    (slurmd_job_t*);
	int (*task_start)       (slurmd_job_t*, uint32_t);
	int (*task_end)         (slurmd_job_t*, pid_t);
	int (*job_sample)       ();
	int (*add_node_data)    (slurmd_job_t*, char*, char*, void*);
	int (*add_sample_data)  (char*, char*, void*);
	int (*add_task_data)    (slurmd_job_t*, uint32_t, char*, char*, void*);
	void (*conf_options)    (s_p_options_t **full_options,
				            int *full_options_cnt);
	void (*conf_set)        (s_p_hashtbl_t *tbl);
	void* (*conf_get)        ();
} slurm_acct_gather_profile_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_acct_gather_profile_ops_t.
 */
static const char *syms[] = {
	"acct_gather_profile_p_controller_start",
	"acct_gather_profile_p_node_step_start",
	"acct_gather_profile_p_node_step_end",
	"acct_gather_profile_p_task_start",
	"acct_gather_profile_p_task_end",
	"acct_gather_profile_p_job_sample",
	"acct_gather_profile_p_add_node_data",
	"acct_gather_profile_p_add_sample_data",
	"acct_gather_profile_p_add_task_data",
	"acct_gather_profile_p_conf_options",
	"acct_gather_profile_p_conf_set",
	"acct_gather_profile_p_conf_get"
};

static slurm_acct_gather_profile_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t profile_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int slurm_acct_gather_profile_init(void)
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
	int rc;
	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
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

extern char *acct_gather_profile_series_to_string(uint32_t series)
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

extern uint32_t acct_gather_profile_series_from_string(char *series_str)
{
	if (strcasecmp(series_str, "energy"))
		return ACCT_GATHER_PROFILE_ENERGY;
	else if (strcasecmp(series_str, "task"))
		return ACCT_GATHER_PROFILE_TASK;
	else if (strcasecmp(series_str, "lustre"))
		return ACCT_GATHER_PROFILE_LUSTRE;
	else if (strcasecmp(series_str, "network"))
		return ACCT_GATHER_PROFILE_NETWORK;

	return ACCT_GATHER_PROFILE_NOT_SET;
}

extern void acct_gather_profile_g_conf_options(s_p_options_t **full_options,
					      int *full_options_cnt)
{
	if (slurm_acct_gather_profile_init() < 0)
		return;
	(*(ops.conf_options))(full_options, full_options_cnt);
	return;
}

extern void acct_gather_profile_g_conf_set(s_p_hashtbl_t *tbl)
{
	if (slurm_acct_gather_profile_init() < 0)
		return;

	(*(ops.conf_set))(tbl);
	return;
}

extern void* acct_gather_profile_g_conf_get() {

	if (!g_context)
		return NULL;

	return (*(ops.conf_get))();
}


extern int acct_gather_profile_g_controller_start()
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_profile_init() < 0)
		return retval;

	retval = (*(ops.controller_start))();
	return retval;
}

extern int acct_gather_profile_g_node_step_start(slurmd_job_t* job)
{
	int tx;
	int retval = SLURM_ERROR;

	if (job->stepid == NO_VAL) {
		return retval;
	}
	if (job->profile) {
		debug3("PROFILE: option --profile=%s",job->profile);
	}
	nodetasks = job->node_tasks;
	pid2task = xmalloc(sizeof(profile_task_pid_t)*nodetasks);
	if (!pid2task) {
		nodetasks = 0;
		return retval;
	}
	for (tx=0;tx<nodetasks;tx++) {
		pid2task[tx].gtid = job->task[tx]->gtid;
		pid2task[tx].pid = job->task[tx]->pid;
	}
	if (slurm_acct_gather_profile_init() < 0)
		return retval;

	retval = (*(ops.node_step_start))(job);
	return retval;
}

extern int acct_gather_profile_g_node_step_end(slurmd_job_t* job)
{
	int retval = SLURM_ERROR;
	if (job->stepid == NO_VAL) {
		return retval;
	}
	if (!g_context) {
		xfree(pid2task);
		return retval;
	}

	retval = (*(ops.node_step_end))(job);
	xfree(pid2task);
	nodetasks = 0;
	return retval;
}

extern int acct_gather_profile_g_task_start(slurmd_job_t* job, uint32_t taskid)
{
	int retval = SLURM_ERROR;
	if (job->stepid == NO_VAL) {
		return retval;
	}
	// task start occurs before node_step_start.
	if (slurm_acct_gather_profile_init() < 0)
		return retval;
	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.task_start))(job, taskid);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_task_end(slurmd_job_t* job, pid_t taskpid)
{
	int retval = SLURM_ERROR;
	if (!g_context) {
		return retval;
	}
	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.task_end))(job, taskpid);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_job_sample()
{
	int retval = SLURM_ERROR;
	if (!g_context) {
		return retval;
	}
	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.job_sample))();
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_add_node_data(slurmd_job_t* job, char* group,
		char* type, void* data)
{
	int retval = SLURM_ERROR;
	if (!g_context) {
		return retval;
	}
	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.add_node_data))(job,group,type,data);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_add_sample_data(char* group, char* type,
		void* data)
{
	int retval = SLURM_ERROR;
	if (!g_context) {
		return retval;
	}
	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.add_sample_data))(group,type,data);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int acct_gather_profile_g_add_task_data(slurmd_job_t* job,
		uint32_t taskid, char* group, char* type, void* data)
{
	int retval = SLURM_ERROR;
	if (!g_context) {
		return retval;
	}
	slurm_mutex_lock(&profile_mutex);
	retval = (*(ops.add_task_data))(job,taskid,group,type,data);
	slurm_mutex_unlock(&profile_mutex);
	return retval;
}

extern int get_taskid_from_pid(pid_t pid, uint32_t *gtid) {
	int tx;
	if (pid2task == NULL)
		return SLURM_ERROR;
	for (tx=0;tx<nodetasks;tx++) {
		if (pid2task[tx].pid == pid) {
			*gtid = pid2task[tx].gtid;
			return SLURM_SUCCESS;
		}
	}
	return SLURM_ERROR;
}
