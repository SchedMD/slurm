/*****************************************************************************\
 *  slurm_acct_gather_profile.h - implementation-independent job profile
 *  accounting plugin definitions
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

#ifndef __SLURM_ACCT_GATHER_PROFILE_H__
#define __SLURM_ACCT_GATHER_PROFILE_H__

#include <inttypes.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_acct_gather.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#define NO_PARENT -1

typedef enum {
	PROFILE_ENERGY,
	PROFILE_TASK,
	PROFILE_FILESYSTEM,
	PROFILE_NETWORK,
	PROFILE_CNT
} acct_gather_profile_type_t;

typedef enum {
	PROFILE_FIELD_NOT_SET,
	PROFILE_FIELD_UINT64,
	PROFILE_FIELD_DOUBLE
} acct_gather_profile_field_type_t;

typedef struct {
	char *name;
	acct_gather_profile_field_type_t type;
} acct_gather_profile_dataset_t;

typedef struct {
	int freq;
	time_t last_notify;
	pthread_cond_t notify;
	pthread_mutex_t notify_mutex;
} acct_gather_profile_timer_t;

extern acct_gather_profile_timer_t acct_gather_profile_timer[PROFILE_CNT];

/*
 * Load the plugin
 */
extern int acct_gather_profile_init(void);

/*
 * Unload the plugin
 */
extern int acct_gather_profile_fini(void);

/* translate uint32_t profile to string (DO NOT free) */
extern char *acct_gather_profile_to_string(uint32_t profile);

/* translate string of words to uint32_t filled in with bits set to profile */
extern uint32_t acct_gather_profile_from_string(const char *profile_str);

/* Return true if acct_gather_profile_running flag is set */
extern bool acct_gather_profile_test(void);

extern char *acct_gather_profile_type_to_string(uint32_t series);
extern uint32_t acct_gather_profile_type_from_string(char *series_str);

extern char *acct_gather_profile_type_t_name(acct_gather_profile_type_t type);
extern char *acct_gather_profile_dataset_str(
	acct_gather_profile_dataset_t *dataset, void *data,
	char *str, int str_len);
extern int acct_gather_profile_startpoll(char *freq, char *freq_def);
extern void acct_gather_profile_endpoll(void);

/* Called from slurmstepd between fork() and exec() of application.
 * Close open files */
extern int acct_gather_profile_g_child_forked(void);

/*
 * Define plugin local conf for acct_gather.conf
 *
 * Parameters
 * 	full_options -- pointer that will receive list of plugin local
 *			definitions
 *	full_options_cnt -- count of plugin local definitions
 */
extern int acct_gather_profile_g_conf_options(s_p_options_t **full_options,
					       int *full_options_cnt);
/*
 * set plugin local conf from acct_gather.conf into its structure
 *
 * Parameters
 * 	tbl - hash table of acct_gather.conf key-values.
 */
extern int acct_gather_profile_g_conf_set(s_p_hashtbl_t *tbl);

/*
 * get info from the profile plugin
 *
 */
extern int acct_gather_profile_g_get(enum acct_gather_profile_info info_type,
				      void *data);

/*
 * Called once per step on each node from slurmstepd, before launching tasks.
 * Provides an opportunity to create files and other node-step level
 * initialization.
 *
 * Parameters
 *	job -- structure defining a slurm job
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_node_step_start(stepd_step_rec_t* job);

/*
 * Called once per step on each node from slurmstepd, after all tasks end.
 * Provides an opportunity to close files, etc.
 *
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_node_step_end(void);

/*
 * Called once per task from slurmstepd, BEFORE node step start is called.
 * Provides an opportunity to gather beginning values from node counters
 * (bytes_read ...)
 * At this point in the life cycle, the value of the --profile option isn't
 * known and and files are not open so calls to the 'add_*_data'
 * functions cannot be made.
 *
 * Parameters
 *	taskid -- slurm taskid
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_task_start(uint32_t taskid);

/*
 * Called once per task from slurmstepd.
 * Provides an opportunity to put final data for a task.
 *
 * Parameters
 *	taskpid -- linux process id of task
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_task_end(pid_t taskpid);

/*
 * Create a new group which can contain datasets.
 *
 * Returns -- the identifier of the group on success,
 *            a negative value on failure
 */
extern int64_t acct_gather_profile_g_create_group(const char* name);

/*
 * Create a new dataset to record profiling data in the group "parent".
 * Must be called by each accounting plugin in order to record data.
 * A "Time" field is automatically added.
 *
 * Parameters
 *  name        -- name of the dataset
 *  parent      -- id of the parent group created with
 *                 acct_gather_profile_g_create_group, or NO_PARENT for
 *                 default group
 *  profile_series -- profile_series_def_t array filled in with the
 *                    series definition
 * Returns -- an identifier to the dataset on success
 *            a negative value on failure
 */
extern int acct_gather_profile_g_create_dataset(
	const char *name, int64_t parent,
	acct_gather_profile_dataset_t *dataset);

/*
 * Put data at the Node Samples level. Typically called from something called
 * at either job_acct_gather interval or acct_gather_energy interval.
 * Time is automatically added.
 *
 * Parameters
 *	dataset_id -- identifies the dataset to add data to.
 *	data       -- data structure to be recorded
 *      sample_time-- when the sample happened
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_add_sample_data(int dataset_id, void *data,
						 time_t sample_time);

/* Get the values from the plugin that are setup in the .conf
 * file. This function should most likely only be called from
 * src/common/slurm_acct_gather.c (acct_gather_get_values())
 */
extern void acct_gather_profile_g_conf_values(void *data);

/* Return true if the given type of plugin must be profiled */
extern bool acct_gather_profile_g_is_active(uint32_t type);

#endif /*__SLURM_ACCT_GATHER_PROFILE_H__*/
