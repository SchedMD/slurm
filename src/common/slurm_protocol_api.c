/*****************************************************************************\
 *  slurm_protocol_api.c - high-level slurm communication functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

/* GLOBAL INCLUDES */

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

/* PROJECT INCLUDES */
#include "src/common/assoc_mgr.h"
#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_route.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/log.h"
#include "src/common/forward.h"
#include "src/common/msg_aggr.h"
#include "src/slurmdbd/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_strcasestr.h"

/* EXTERNAL VARIABLES */

/* #DEFINES */
#define _DEBUG	0
#define MAX_SHUTDOWN_RETRY 5

/* STATIC VARIABLES */
/* static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER; */
static slurm_protocol_config_t proto_conf_default;
static slurm_protocol_config_t *proto_conf = &proto_conf_default;
/* static slurm_ctl_conf_t slurmctld_conf; */
static int message_timeout = -1;

/* STATIC FUNCTIONS */
static char *_global_auth_key(void);
static void  _remap_slurmctld_errno(void);
static int   _unpack_msg_uid(Buf buffer);
static bool  _is_port_ok(int, uint16_t);
static void _slurm_set_addr_any(slurm_addr_t * slurm_address, uint16_t port);

#if _DEBUG
static void _print_data(char *data, int len);
#endif

/* define the slurmdbd_options flag */
slurm_dbd_conf_t *slurmdbd_conf = NULL;

/**********************************************************************\
 * protocol configuration functions
\**********************************************************************/
/* slurm_set_api_config
 * sets the slurm_protocol_config object
 * NOT THREAD SAFE
 * IN protocol_conf		-  slurm_protocol_config object
 *
 * XXX: Why isn't the "config_lock" mutex used here?
 */
int slurm_set_api_config(slurm_protocol_config_t * protocol_conf)
{
	proto_conf = protocol_conf;
	return SLURM_SUCCESS;
}

/* slurm_get_api_config
 * returns a pointer to the current slurm_protocol_config object
 * RET slurm_protocol_config_t  - current slurm_protocol_config object
 */
slurm_protocol_config_t *slurm_get_api_config(void)
{
	return proto_conf;
}

/* slurm_api_set_default_config
 *      called by the send_controller_msg function to insure that at least
 *	the compiled in default slurm_protocol_config object is initialized
 * RET int		 - return code
 */
int slurm_api_set_default_config(void)
{
	int rc = SLURM_SUCCESS;
	slurm_ctl_conf_t *conf;

	/*slurm_conf_init(NULL);*/
	conf = slurm_conf_lock();

	if (conf->control_addr == NULL) {
		error("Unable to establish controller machine");
		rc = SLURM_ERROR;
		goto cleanup;
	}
	if (conf->slurmctld_port == 0) {
		error("Unable to establish controller port");
		rc = SLURM_ERROR;
		goto cleanup;
	}

	slurm_set_addr(&proto_conf_default.primary_controller,
		       conf->slurmctld_port,
		       conf->control_addr);
	if (proto_conf_default.primary_controller.sin_port == 0) {
		error("Unable to establish control machine address");
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if (conf->backup_addr) {
		slurm_set_addr(&proto_conf_default.secondary_controller,
			       conf->slurmctld_port,
			       conf->backup_addr);
	}
	proto_conf = &proto_conf_default;

cleanup:
	slurm_conf_unlock();
	return rc;
}

/* slurm_api_clear_config
 * execute this only at program termination to free all memory */
void slurm_api_clear_config(void)
{
	slurm_conf_destroy();
}

/* slurm_get_complete_wait
 * RET CompleteWait value from slurm.conf
 */
uint16_t slurm_get_complete_wait(void)
{
	uint16_t complete_wait = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		complete_wait = conf->complete_wait;
		slurm_conf_unlock();
	}
	return complete_wait;
}

/* slurm_get_cpu_freq_def
 * RET CpuFreqDef value from slurm.conf
 */
uint32_t slurm_get_cpu_freq_def(void)
{
	uint32_t cpu_freq_def = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		cpu_freq_def = conf->cpu_freq_def;
		slurm_conf_unlock();
	}
	return cpu_freq_def;
}

/* slurm_get_cpu_freq_govs
 * RET CpuFreqGovernors value from slurm.conf
 */
uint32_t slurm_get_cpu_freq_govs(void)
{
	uint32_t cpu_freq_govs = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		cpu_freq_govs = conf->cpu_freq_govs;
		slurm_conf_unlock();
	}
	return cpu_freq_govs;
}

/* update internal configuration data structure as needed.
 *	exit with lock set */
/* static inline void _lock_update_config() */
/* { */
/* 	slurm_api_set_default_config(); */
/* 	slurm_mutex_lock(&config_lock); */
/* } */

/* slurm_get_batch_start_timeout
 * RET BatchStartTimeout value from slurm.conf
 */
uint16_t slurm_get_batch_start_timeout(void)
{
	uint16_t batch_start_timeout = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		batch_start_timeout = conf->batch_start_timeout;
		slurm_conf_unlock();
	}
	return batch_start_timeout;
}

/* slurm_get_suspend_timeout
 * RET SuspendTimeout value from slurm.conf
 */
uint16_t slurm_get_suspend_timeout(void)
{
	uint16_t suspend_timeout = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		suspend_timeout = conf->suspend_timeout;
		slurm_conf_unlock();
	}
	return suspend_timeout;
}

/* slurm_get_resume_timeout
 * RET ResumeTimeout value from slurm.conf
 */
uint16_t slurm_get_resume_timeout(void)
{
	uint16_t resume_timeout = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		resume_timeout = conf->resume_timeout;
		slurm_conf_unlock();
	}
	return resume_timeout;
}

/* slurm_get_suspend_time
 * RET SuspendTime value from slurm.conf
 */
uint32_t slurm_get_suspend_time(void)
{
	uint32_t suspend_time = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		suspend_time = conf->suspend_time;
		slurm_conf_unlock();
	}
	return suspend_time;
}

/* slurm_get_def_mem_per_cpu
 * RET DefMemPerCPU/Node value from slurm.conf
 */
uint32_t slurm_get_def_mem_per_cpu(void)
{
	uint32_t mem_per_cpu = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mem_per_cpu = conf->def_mem_per_cpu;
		slurm_conf_unlock();
	}
	return mem_per_cpu;
}

/* slurm_get_kill_on_bad_exit
 * RET KillOnBadExit value from slurm.conf
 */
uint16_t slurm_get_kill_on_bad_exit(void)
{
	uint16_t kill_on_bad_exit = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		kill_on_bad_exit = conf->kill_on_bad_exit;
		slurm_conf_unlock();
	}
	return kill_on_bad_exit;
}

/* slurm_get_prolog_flags
 * RET PrologFlags value from slurm.conf
 */
uint32_t slurm_get_prolog_flags(void)
{
	uint32_t prolog_flags = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		prolog_flags = conf->prolog_flags;
		slurm_conf_unlock();
	}
	return prolog_flags;
}

/* slurm_get_debug_flags
 * RET DebugFlags value from slurm.conf
 */
uint64_t slurm_get_debug_flags(void)
{
	uint64_t debug_flags = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		debug_flags = slurmdbd_conf->debug_flags;
	} else {
		conf = slurm_conf_lock();
		debug_flags = conf->debug_flags;
		slurm_conf_unlock();
	}
	return debug_flags;
}

/* slurm_set_debug_flags
 */
void slurm_set_debug_flags(uint64_t debug_flags)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		conf->debug_flags = debug_flags;
		slurm_conf_unlock();
	}
}

/* slurm_get_max_mem_per_cpu
 * RET MaxMemPerCPU/Node value from slurm.conf
 */
uint32_t slurm_get_max_mem_per_cpu(void)
{
	uint32_t mem_per_cpu = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mem_per_cpu = conf->max_mem_per_cpu;
		slurm_conf_unlock();
	}
	return mem_per_cpu;
}

/* slurm_get_epilog_msg_time
 * RET EpilogMsgTime value from slurm.conf
 */
uint32_t slurm_get_epilog_msg_time(void)
{
	uint32_t epilog_msg_time = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		epilog_msg_time = conf->epilog_msg_time;
		slurm_conf_unlock();
	}
	return epilog_msg_time;
}

/* slurm_get_env_timeout
 * return default timeout for srun/sbatch --get-user-env option
 */
extern int slurm_get_env_timeout(void)
{
	int timeout = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		timeout = conf->get_env_timeout;
		slurm_conf_unlock();
	}
	return timeout;
}

/* slurm_get_max_array_size
 * return MaxArraySize configuration parameter
 */
extern uint32_t slurm_get_max_array_size(void)
{
	int max_array_size = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}
	return max_array_size;
}


/* slurm_get_mpi_default
 * get default mpi value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_default(void)
{
	char *mpi_default = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mpi_default = xstrdup(conf->mpi_default);
		slurm_conf_unlock();
	}
	return mpi_default;
}

/* slurm_get_mpi_params
 * get mpi parameters value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_params(void)
{
	char *mpi_params = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mpi_params = xstrdup(conf->mpi_params);
		slurm_conf_unlock();
	}
	return mpi_params;
}

/* slurm_get_msg_aggr_params
 * get message aggregation parameters value from slurmctld_conf object
 * RET char *   - message aggregation value from slurm.conf,
 * MUST be xfreed by caller
 */
char *slurm_get_msg_aggr_params(void)
{
	char *msg_aggr_params = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		msg_aggr_params = xstrdup(conf->msg_aggr_params);
		slurm_conf_unlock();
	}
	return msg_aggr_params;
}

/* slurm_get_msg_timeout
 * get default message timeout value from slurmctld_conf object
 */
uint16_t slurm_get_msg_timeout(void)
{
	uint16_t msg_timeout = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
		msg_timeout = slurmdbd_conf->msg_timeout;
	} else {
		conf = slurm_conf_lock();
		msg_timeout = conf->msg_timeout;
		slurm_conf_unlock();
#ifdef MEMORY_LEAK_DEBUG
		msg_timeout *= 4;
#endif
	}
	return msg_timeout;
}

/* slurm_get_plugin_dir
 * get plugin directory from slurmctld_conf object
 * RET char *   - plugin directory, MUST be xfreed by caller
 */
char *slurm_get_plugin_dir(void)
{
	char *plugin_dir = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		plugin_dir = xstrdup(slurmdbd_conf->plugindir);
	} else {
		conf = slurm_conf_lock();
		plugin_dir = xstrdup(conf->plugindir);
		slurm_conf_unlock();
	}
	return plugin_dir;
}

/* slurm_get_priority_decay_hl
 * returns the priority decay half life in seconds from slurmctld_conf object
 * RET uint32_t - decay_hl in secs.
 */
uint32_t slurm_get_priority_decay_hl(void)
{
	uint32_t priority_hl = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		priority_hl = conf->priority_decay_hl;
		slurm_conf_unlock();
	}

	return priority_hl;
}

/* slurm_get_priority_calc_period
 * returns the seconds between priority decay calculation from slurmctld_conf
 * RET uint32_t - calc_period in secs.
 */
uint32_t slurm_get_priority_calc_period(void)
{
	uint32_t calc_period = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		calc_period = conf->priority_calc_period;
		slurm_conf_unlock();
	}

	return calc_period;
}

/* slurm_get_priority_favor_small
 * returns weither or not we are favoring small jobs from slurmctld_conf object
 * RET bool - true if favor small, false else.
 */
bool slurm_get_priority_favor_small(void)
{
	bool factor = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_favor_small;
		slurm_conf_unlock();
	}

	return factor;
}

/* slurm_get_priority_max_age
 * returns the priority age max in seconds from slurmctld_conf object
 * RET uint32_t - age_max in secs.
 */
uint32_t slurm_get_priority_max_age(void)
{
	uint32_t age = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		age = conf->priority_max_age;
		slurm_conf_unlock();
	}

	return age;
}

/* slurm_get_priority_params
 * RET char * - Value of PriorityParameters, MUST be xfreed by caller */
char *slurm_get_priority_params(void)
{
	char *params = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		params = xstrdup(conf->priority_params);
		slurm_conf_unlock();
	}
	return params;
}

/* slurm_get_priority_reset_period
 * returns the priority usage reset period from slurmctld_conf object
 * RET uint16_t - flag, see PRIORITY_RESET_* in slurm/slurm.h.
 */
uint16_t slurm_get_priority_reset_period(void)
{
	uint16_t reset_period = (uint16_t) 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		reset_period = conf->priority_reset_period;
		slurm_conf_unlock();
	}

	return reset_period;
}

/* slurm_get_priority_type
 * returns the priority type from slurmctld_conf object
 * RET char *    - priority type, MUST be xfreed by caller
 */
char *slurm_get_priority_type(void)
{
	char *priority_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		priority_type = xstrdup(conf->priority_type);
		slurm_conf_unlock();
	}

	return priority_type;
}

/* slurm_get_priority_weight_age
 * returns the priority weight for age from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_age(void)
{
	uint32_t factor = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_age;
		slurm_conf_unlock();
	}

	return factor;
}


/* slurm_get_priority_weight_fairshare
 * returns the priority weight for fairshare from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_fairshare(void)
{
	uint32_t factor = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_fs;
		slurm_conf_unlock();
	}

	return factor;
}

/* slurm_get_fs_dampening_factor
 * returns the dampening factor for fairshare from slurmctld_conf object
 * RET uint32_t - factor.
 */
uint16_t slurm_get_fs_dampening_factor(void)
{
	uint16_t factor = 1;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->fs_dampening_factor;
		slurm_conf_unlock();
	}

	return factor;
}

/* slurm_get_priority_weight_job_size
 * returns the priority weight for job size from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_job_size(void)
{
	uint32_t factor = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_js;
		slurm_conf_unlock();
	}

	return factor;
}

/* slurm_get_priority_weight_partition
 * returns the priority weight for partitions from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_partition(void)
{
	uint32_t factor = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_part;
		slurm_conf_unlock();
	}

	return factor;
}


/* slurm_get_priority_weight_qos
 * returns the priority weight for QOS from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_qos(void)
{
	uint32_t factor = NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_qos;
		slurm_conf_unlock();
	}

	return factor;
}

/* slurm_get_priority_weight_tres
 * returns the priority weights for TRES' from slurmctld_conf object
 * RET char * string of configured tres weights. MUST be xfreed by caller
 */
char *slurm_get_priority_weight_tres(void)
{
	char *weights = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		weights = xstrdup(conf->priority_weight_tres);
		slurm_conf_unlock();
	}

	return weights;
}

static int _get_tres_id(char *type, char *name)
{
	slurmdb_tres_rec_t tres_rec;
	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = type;
	tres_rec.name = name;

	return assoc_mgr_find_tres_pos(&tres_rec, false);
}

static int _tres_weight_item(double *weights, char *item_str)
{
	char *type = NULL, *value = NULL, *name = NULL;
	int tres_id;

	if (!item_str) {
		error("TRES weight item is null");
		return SLURM_ERROR;
	}

	type = strtok_r(item_str, "=", &value);
	if (strchr(type, '/'))
		type = strtok_r(type, "/", &name);

	if (!value || !*value) {
		error("\"%s\" is an invalid TRES weight entry", item_str);
		return SLURM_ERROR;
	}

	if ((tres_id = _get_tres_id(type, name)) == -1) {
		error("TRES weight '%s%s%s' is not a configured TRES type.",
		      type, (name) ? ":" : "", (name) ? name : "");
		return SLURM_ERROR;
	}

	errno = 0;
	weights[tres_id] = strtod(value, NULL);
	if(errno) {
		error("Unable to convert %s value to double in %s",
		      __func__, value);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* slurm_get_priority_weight_tres_array
 * IN weights_str - string of tres and weights to be parsed.
 * IN tres_cnt - count of how many tres' are on the system (e.g.
 * 		slurmctld_tres_cnt).
 * RET double* of tres weights.
 */
double *slurm_get_tres_weight_array(char *weights_str, int tres_cnt)
{
	double *weights;
	char *tmp_str = xstrdup(weights_str);
	char *token, *last = NULL;

	if (!weights_str || !*weights_str || !tres_cnt)
		return NULL;

	weights = xmalloc(sizeof(double) * tres_cnt);

	token = strtok_r(tmp_str, ",", &last);
	while (token) {
		if (_tres_weight_item(weights, token)) {
			xfree(weights);
			xfree(tmp_str);
			fatal("failed to parse tres weights str '%s'",
			      weights_str);
			return NULL;
		}
		token = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);
	return weights;
}

/* slurm_get_private_data
 * get private data from slurmctld_conf object
 */
uint16_t slurm_get_private_data(void)
{
	uint16_t private_data = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		private_data = slurmdbd_conf->private_data;
	} else {
		conf = slurm_conf_lock();
		private_data = conf->private_data;
		slurm_conf_unlock();
	}
	return private_data;
}

/* slurm_get_state_save_location
 * get state_save_location from slurmctld_conf object from slurmctld_conf object
 * RET char *   - state_save_location directory, MUST be xfreed by caller
 */
char *slurm_get_state_save_location(void)
{
	char *state_save_loc = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		state_save_loc = xstrdup(conf->state_save_location);
		slurm_conf_unlock();
	}
	return state_save_loc;
}

/* slurm_get_tmp_fs
 * returns the TmpFS configuration parameter from slurmctld_conf object
 * RET char *    - tmp_fs, MUST be xfreed by caller
 */
extern char *slurm_get_tmp_fs(void)
{
	char *tmp_fs = NULL;
	slurm_ctl_conf_t *conf = NULL;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		tmp_fs = xstrdup(conf->tmp_fs);
		slurm_conf_unlock();
	}
	return tmp_fs;
}

/* slurm_get_auth_type
 * returns the authentication type from slurmctld_conf object
 * RET char *    - auth type, MUST be xfreed by caller
 */
char *slurm_get_auth_type(void)
{
	char *auth_type = NULL;
	slurm_ctl_conf_t *conf = NULL;

	if (slurmdbd_conf) {
		auth_type = xstrdup(slurmdbd_conf->auth_type);
	} else {
		conf = slurm_conf_lock();
		auth_type = xstrdup(conf->authtype);
		slurm_conf_unlock();
	}
	return auth_type;
}

/* slurm_get_bb_type
 * returns the BurstBufferType (bb_type) from slurmctld_conf object
 * RET char *    - BurstBufferType, MUST be xfreed by caller
 */
extern char *slurm_get_bb_type(void)
{
	char *bb_type = NULL;
	slurm_ctl_conf_t *conf = NULL;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		bb_type = xstrdup(conf->bb_type);
		slurm_conf_unlock();
	}
	return bb_type;
}

/* slurm_get_checkpoint_type
 * returns the checkpoint_type from slurmctld_conf object
 * RET char *    - checkpoint type, MUST be xfreed by caller
 */
extern char *slurm_get_checkpoint_type(void)
{
	char *checkpoint_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		checkpoint_type = xstrdup(conf->checkpoint_type);
		slurm_conf_unlock();
	}
	return checkpoint_type;
}

/* slurm_get_checkpoint_dir
 * returns the job_ckpt_dir from slurmctld_conf object
 * RET char *    - checkpoint dir, MUST be xfreed by caller
 */
extern char *slurm_get_checkpoint_dir(void)
{
	char *checkpoint_dir = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		checkpoint_dir = xstrdup(conf->job_ckpt_dir);
		slurm_conf_unlock();
	}
	return checkpoint_dir;
}

/* slurm_get_cluster_name
 * returns the cluster name from slurmctld_conf object
 * RET char *    - cluster name,  MUST be xfreed by caller
 */
char *slurm_get_cluster_name(void)
{
	char *name = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		name = xstrdup(conf->cluster_name);
		slurm_conf_unlock();
	}
	return name;
}

/* slurm_get_crypto_type
 * returns the crypto_type from slurmctld_conf object
 * RET char *    - crypto type, MUST be xfreed by caller
 */
extern char *slurm_get_crypto_type(void)
{
	char *crypto_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		crypto_type = xstrdup(conf->crypto_type);
		slurm_conf_unlock();
	}
	return crypto_type;
}

/* slurm_get_power_parameters
 * returns the PowerParameters from slurmctld_conf object
 * RET char *    - PowerParameters, MUST be xfreed by caller
 */
extern char *slurm_get_power_parameters(void)
{
	char *power_parameters = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		power_parameters = xstrdup(conf->power_parameters);
		slurm_conf_unlock();
	}
	return power_parameters;
}

/* slurm_set_power_parameters
 * reset the PowerParameters object
 */
extern void slurm_set_power_parameters(char *power_parameters)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		xfree(conf->power_parameters);
		conf->power_parameters = xstrdup(power_parameters);
		slurm_conf_unlock();
	}
}

/* slurm_get_power_plugin
 * returns the PowerPlugin from slurmctld_conf object
 * RET char *    - PowerPlugin, MUST be xfreed by caller
 */
extern char *slurm_get_power_plugin(void)
{
	char *power_plugin = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		power_plugin = xstrdup(conf->power_plugin);
		slurm_conf_unlock();
	}
	return power_plugin;
}

/* slurm_get_topology_param
 * returns the value of topology_param in slurmctld_conf object
 * RET char *    - topology parameters, MUST be xfreed by caller
 */
extern char * slurm_get_topology_param(void)
{
	char *topology_param = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		topology_param = xstrdup(conf->topology_param);
		slurm_conf_unlock();
	}
	return topology_param;
}

/* slurm_get_topology_plugin
 * returns the value of topology_plugin in slurmctld_conf object
 * RET char *    - topology type, MUST be xfreed by caller
 */
extern char * slurm_get_topology_plugin(void)
{
	char *topology_plugin = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		topology_plugin = xstrdup(conf->topology_plugin);
		slurm_conf_unlock();
	}
	return topology_plugin;
}

/* slurm_get_propagate_prio_process
 * return the PropagatePrioProcess flag from slurmctld_conf object
 */
extern uint16_t slurm_get_propagate_prio_process(void)
{
	uint16_t propagate_prio = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		propagate_prio = conf->propagate_prio_process;
		slurm_conf_unlock();
	}
	return propagate_prio;
}

/* slurm_get_fast_schedule
 * returns the value of fast_schedule in slurmctld_conf object
 */
extern uint16_t slurm_get_fast_schedule(void)
{
	uint16_t fast_val = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		fast_val = conf->fast_schedule;
		slurm_conf_unlock();
	}
	return fast_val;
}

/* slurm_get_route_plugin
 * returns the value of route_plugin in slurmctld_conf object
 * RET char *    - route type, MUST be xfreed by caller
 */
extern char * slurm_get_route_plugin(void)
{
	char *route_plugin = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();

		route_plugin = xstrdup(conf->route_plugin);
		slurm_conf_unlock();
	}
	return route_plugin;
}

/* slurm_get_track_wckey
 * returns the value of track_wckey in slurmctld_conf object
 */
extern uint16_t slurm_get_track_wckey(void)
{
	uint16_t track_wckey = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		track_wckey = slurmdbd_conf->track_wckey;
	} else {
		conf = slurm_conf_lock();
		track_wckey = conf->track_wckey;
		slurm_conf_unlock();
	}
	return track_wckey;
}

/* slurm_get_use_spec_resources
 * returns the value of use_spec_resources in slurmctld_conf object
 */
extern uint16_t slurm_get_use_spec_resources(void)
{
	uint16_t use_spec_val = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		use_spec_val = conf->use_spec_resources;
		slurm_conf_unlock();
	}
	return use_spec_val;
}

/* slurm_set_tree_width
 * sets the value of tree_width in slurmctld_conf object
 * RET 0 or error code
 */
extern int slurm_set_tree_width(uint16_t tree_width)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		if (tree_width == 0) {
			error("can't have span count of 0");
			return SLURM_ERROR;
		}
		conf->tree_width = tree_width;
		slurm_conf_unlock();
	}
	return 0;
}
/* slurm_get_tree_width
 * returns the value of tree_width in slurmctld_conf object
 */
extern uint16_t slurm_get_tree_width(void)
{
	uint16_t tree_width = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		tree_width = conf->tree_width;
		slurm_conf_unlock();
	}
	return tree_width;
}

/* slurm_get_vsize_factor
 * returns the value of vsize_factor in slurmctld_conf object
 */
extern uint16_t slurm_get_vsize_factor(void)
{
	uint16_t vsize_factor = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		vsize_factor = conf->vsize_factor;
		slurm_conf_unlock();
	}
	return vsize_factor;
}

/* slurm_set_auth_type
 * set the authentication type in slurmctld_conf object
 * used for security testing purposes
 * RET 0 or error code
 */
extern int slurm_set_auth_type(char *auth_type)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		xfree(slurmdbd_conf->auth_type);
		slurmdbd_conf->auth_type = xstrdup(auth_type);
	} else {
		conf = slurm_conf_lock();
		xfree(conf->authtype);
		conf->authtype = xstrdup(auth_type);
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_hash_val
 * get hash val of the slurm.conf from slurmctld_conf object from
 * slurmctld_conf object
 * RET uint32_t  - hash_val
 */
uint32_t slurm_get_hash_val(void)
{
	uint32_t hash_val;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		hash_val = NO_VAL;
	} else {
		conf = slurm_conf_lock();
		hash_val = conf->hash_val;
		slurm_conf_unlock();
	}
	return hash_val;
}

/* slurm_get_health_check_program
 * get health_check_program from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - health_check_program, MUST be xfreed by caller
 */
char *slurm_get_health_check_program(void)
{
	char *health_check_program = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		health_check_program = xstrdup(conf->health_check_program);
		slurm_conf_unlock();
	}
	return health_check_program;
}

/* slurm_get_gres_plugins
 * get gres_plugins from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - gres_plugins, MUST be xfreed by caller
 */
char *slurm_get_gres_plugins(void)
{
	char *gres_plugins = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		gres_plugins = xstrdup(conf->gres_plugins);
		slurm_conf_unlock();
	}
	return gres_plugins;
}


/* slurm_get_job_submit_plugins
 * get job_submit_plugins from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - job_submit_plugins, MUST be xfreed by caller
 */
char *slurm_get_job_submit_plugins(void)
{
	char *job_submit_plugins = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		job_submit_plugins = xstrdup(conf->job_submit_plugins);
		slurm_conf_unlock();
	}
	return job_submit_plugins;
}

/* slurm_get_slurmctld_plugstack
 * get slurmctld_plugstack from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - slurmctld_plugstack, MUST be xfreed by caller
 */
char *slurm_get_slurmctld_plugstack(void)
{
	char *slurmctld_plugstack = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		slurmctld_plugstack = xstrdup(conf->slurmctld_plugstack);
		slurm_conf_unlock();
	}
	return slurmctld_plugstack;
}

/* slurm_get_slurmd_plugstack
 * get slurmd_plugstack from slurmd_conf object from
 * slurmd_conf object
 * RET char *   - slurmd_plugstack, MUST be xfreed by caller
 */
char *slurm_get_slurmd_plugstack(void)
{
	char *slurmd_plugstack = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		slurmd_plugstack = xstrdup(conf->slurmd_plugstack);
		slurm_conf_unlock();
	}
	return slurmd_plugstack;
}

/* slurm_get_accounting_storage_type
 * returns the accounting storage type from slurmctld_conf object
 * RET char *    - accounting storage type,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_type(void)
{
	char *accounting_type;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		accounting_type = xstrdup(slurmdbd_conf->storage_type);
	} else {
		conf = slurm_conf_lock();
		accounting_type = xstrdup(conf->accounting_storage_type);
		slurm_conf_unlock();
	}
	return accounting_type;

}

/* slurm_get_accounting_storage_tres
 * returns the accounting storage tres from slurmctld_conf object
 * RET char *    - accounting storage tres,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_tres(void)
{
	char *accounting_tres;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		accounting_tres = NULL;
	} else {
		conf = slurm_conf_lock();
		accounting_tres = xstrdup(conf->accounting_storage_tres);
		slurm_conf_unlock();
	}
	return accounting_tres;

}

/* slurm_get_accounting_storage_user
 * returns the storage user from slurmctld_conf object
 * RET char *    - storage user,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_user(void)
{
	char *storage_user;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		storage_user = xstrdup(slurmdbd_conf->storage_user);
	} else {
		conf = slurm_conf_lock();
		storage_user = xstrdup(conf->accounting_storage_user);
		slurm_conf_unlock();
	}
	return storage_user;
}

/* slurm_set_accounting_storage_user
 * IN: char *user (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_user(char *user)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		xfree(slurmdbd_conf->storage_user);
		slurmdbd_conf->storage_user = xstrdup(user);
	} else {
		conf = slurm_conf_lock();
		xfree(conf->accounting_storage_user);
		conf->accounting_storage_user = xstrdup(user);
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_accounting_storage_backup_host
 * returns the storage backup host from slurmctld_conf object
 * RET char *    - storage backup host,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_backup_host(void)
{
	char *storage_host;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		storage_host = xstrdup(slurmdbd_conf->storage_backup_host);
	} else {
		conf = slurm_conf_lock();
		storage_host = xstrdup(conf->accounting_storage_backup_host);
		slurm_conf_unlock();
	}
	return storage_host;
}

/* slurm_get_accounting_storage_host
 * returns the storage host from slurmctld_conf object
 * RET char *    - storage host,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_host(void)
{
	char *storage_host;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		storage_host = xstrdup(slurmdbd_conf->storage_host);
	} else {
		conf = slurm_conf_lock();
		storage_host = xstrdup(conf->accounting_storage_host);
		slurm_conf_unlock();
	}
	return storage_host;
}

/* slurm_set_accounting_storage_host
 * IN: char *host (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_host(char *host)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		xfree(slurmdbd_conf->storage_host);
		slurmdbd_conf->storage_host = xstrdup(host);
	} else {
		conf = slurm_conf_lock();
		xfree(conf->accounting_storage_host);
		conf->accounting_storage_host = xstrdup(host);
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_accounting_storage_loc
 * returns the storage location from slurmctld_conf object
 * RET char *    - storage location,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_loc(void)
{
	char *storage_loc;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		storage_loc = xstrdup(slurmdbd_conf->storage_loc);
	} else {
		conf = slurm_conf_lock();
		storage_loc = xstrdup(conf->accounting_storage_loc);
		slurm_conf_unlock();
	}
	return storage_loc;
}

/* slurm_set_accounting_storage_loc
 * IN: char *loc (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_loc(char *loc)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		xfree(slurmdbd_conf->storage_loc);
		slurmdbd_conf->storage_loc = xstrdup(loc);
	} else {
		conf = slurm_conf_lock();
		xfree(conf->accounting_storage_loc);
		conf->accounting_storage_loc = xstrdup(loc);
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_accounting_storage_enforce
 * returns what level to enforce associations at
 */
uint16_t slurm_get_accounting_storage_enforce(void)
{
	uint16_t enforce = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		enforce = conf->accounting_storage_enforce;
		slurm_conf_unlock();
	}
	return enforce;

}

/* slurm_get_is_association_based_accounting
 * returns if we are doing accounting by associations
 */
int slurm_get_is_association_based_accounting(void)
{
	int enforce = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		return 1;
	} else {
		conf = slurm_conf_lock();
		if (!strcasecmp(conf->accounting_storage_type,
			       "accounting_storage/slurmdbd") ||
		    !strcasecmp(conf->accounting_storage_type,
				"accounting_storage/mysql"))
			enforce = 1;
		slurm_conf_unlock();
	}
	return enforce;

}

/* slurm_get_accounting_storage_pass
 * returns the storage password from slurmctld_conf object
 * RET char *    - storage password,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_pass(void)
{
	char *storage_pass;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		storage_pass = xstrdup(slurmdbd_conf->storage_pass);
	} else {
		conf = slurm_conf_lock();
		storage_pass = xstrdup(conf->accounting_storage_pass);
		slurm_conf_unlock();
	}
	return storage_pass;
}

/* slurm_get_auth_info
 * returns the auth_info from slurmctld_conf object (AuthInfo parameter)
 * cache value in local buffer for best performance
 * WARNING: The return of this function can be used in many different
 * places and SHOULD NOT BE FREED!
 */
extern char *slurm_get_auth_info(void)
{
	char *auth_info;
	slurm_ctl_conf_t *conf;

	conf = slurm_conf_lock();
	auth_info = xstrdup(conf->authinfo);
	slurm_conf_unlock();

	return auth_info;
}

/* slurm_get_auth_ttl
 * returns the credential Time To Live option from the AuthInfo parameter
 * cache value in local buffer for best performance
 * RET int - Time To Live in seconds or 0 if not specified
 */
extern int slurm_get_auth_ttl(void)
{
	static int ttl = -1;
	char *auth_info, *tmp;

	if (ttl >= 0)
		return ttl;

	auth_info = slurm_get_auth_info();
        if (!auth_info)
                return 0;

	tmp = strstr(auth_info, "ttl=");
	if (tmp) {
		ttl = atoi(tmp + 4);
		if (ttl < 0)
			ttl = 0;
	} else {
		ttl = 0;
	}
	xfree(auth_info);

	return ttl;
}

/* _global_auth_key
 * returns the storage password from slurmctld_conf or slurmdbd_conf object
 * cache value in local buffer for best performance
 * RET char *    - storage password
 */
static char *_global_auth_key(void)
{
	static bool loaded_storage_pass = false;
	static char storage_pass[512] = "\0";
	static char *storage_pass_ptr = NULL;
	slurm_ctl_conf_t *conf;

	if (loaded_storage_pass)
		return storage_pass_ptr;

	if (slurmdbd_conf) {
		if (slurmdbd_conf->auth_info) {
			if (strlen(slurmdbd_conf->auth_info) >
			    sizeof(storage_pass))
				fatal("AuthInfo is too long");
			strncpy(storage_pass, slurmdbd_conf->auth_info,
				sizeof(storage_pass));
			storage_pass_ptr = storage_pass;
		}
	} else {
		conf = slurm_conf_lock();
		if (conf->accounting_storage_pass) {
			if (strlen(conf->accounting_storage_pass) >
			    sizeof(storage_pass))
				fatal("AccountingStoragePass is too long");
			strncpy(storage_pass, conf->accounting_storage_pass,
				sizeof(storage_pass));
			storage_pass_ptr = storage_pass;
		}
		slurm_conf_unlock();
	}
	loaded_storage_pass = true;
	return storage_pass_ptr;
}

/* slurm_get_accounting_storage_port
 * returns the storage port from slurmctld_conf object
 * RET uint32_t   - storage port
 */
uint32_t slurm_get_accounting_storage_port(void)
{
	uint32_t storage_port;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		storage_port = slurmdbd_conf->storage_port;
	} else {
		conf = slurm_conf_lock();
		storage_port = conf->accounting_storage_port;
		slurm_conf_unlock();
	}
	return storage_port;

}

/* slurm_set_accounting_storage_port
 * sets the storage port in slurmctld_conf object
 * RET 0 or error code
 */
int slurm_set_accounting_storage_port(uint32_t storage_port)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		slurmdbd_conf->storage_port = storage_port;
	} else {
		conf = slurm_conf_lock();
		if (storage_port == 0) {
			error("can't have storage port of 0");
			return SLURM_ERROR;
		}

		conf->accounting_storage_port = storage_port;
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_preempt_mode
 * returns the PreemptMode value from slurmctld_conf object
 * RET uint16_t   - PreemptMode value (See PREEMPT_MODE_* in slurm.h)
 */
uint16_t slurm_get_preempt_mode(void)
{
	uint16_t preempt_mode = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		preempt_mode = conf->preempt_mode;
		slurm_conf_unlock();
	}
	return preempt_mode;
}

/* slurm_get_jobacct_gather_type
 * returns the job accounting type from the slurmctld_conf object
 * RET char *    - job accounting type,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_gather_type(void)
{
	char *jobacct_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		jobacct_type = xstrdup(conf->job_acct_gather_type);
		slurm_conf_unlock();
	}
	return jobacct_type;
}

/* slurm_get_jobacct_gather_params
 * returns the job accounting params from the slurmctld_conf object
 * RET char *    - job accounting params,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_gather_params(void)
{
	char *jobacct_params = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		jobacct_params = xstrdup(conf->job_acct_gather_params);
		slurm_conf_unlock();
	}
	return jobacct_params;
}

/* slurm_get_jobacct_freq
 * returns the job accounting poll frequency from the slurmctld_conf object
 * RET int    - job accounting frequency
 */
char *slurm_get_jobacct_gather_freq(void)
{
	char *freq = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		freq = xstrdup(conf->job_acct_gather_freq);
		slurm_conf_unlock();
	}
	return freq;
}

/* slurm_get_energy_accounting_type
 * get EnergyAccountingType from slurmctld_conf object
 * RET char *   - energy_accounting type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_energy_type(void)
{
	char *acct_gather_energy_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		acct_gather_energy_type =
			xstrdup(conf->acct_gather_energy_type);
		slurm_conf_unlock();
	}
	return acct_gather_energy_type;
}

/* slurm_get_profile_accounting_type
 * get ProfileAccountingType from slurmctld_conf object
 * RET char *   - profile_accounting type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_profile_type(void)
{
	char *acct_gather_profile_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		acct_gather_profile_type =
			xstrdup(conf->acct_gather_profile_type);
		slurm_conf_unlock();
	}
	return acct_gather_profile_type;
}

/* slurm_get_infiniband_accounting_type
 * get InfinibandAccountingType from slurmctld_conf object
 * RET char *   - infiniband_accounting type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_infiniband_type(void)
{
	char *acct_gather_infiniband_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		acct_gather_infiniband_type =
			xstrdup(conf->acct_gather_infiniband_type);
		slurm_conf_unlock();
	}
	return acct_gather_infiniband_type;
}

/* slurm_get_filesystem_accounting_type
 * get FilesystemAccountingType from slurmctld_conf object
 * RET char *   - filesystem_accounting type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_filesystem_type(void)
{
	char *acct_gather_filesystem_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		acct_gather_filesystem_type =
			xstrdup(conf->acct_gather_filesystem_type);
		slurm_conf_unlock();
	}
	return acct_gather_filesystem_type;
}


extern uint16_t slurm_get_acct_gather_node_freq(void)
{
	uint16_t freq = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		freq = conf->acct_gather_node_freq;
		slurm_conf_unlock();
	}
	return freq;
}

/* slurm_get_ext_sensors_type
 * get ExtSensorsType from slurmctld_conf object
 * RET char *   - ext_sensors type, MUST be xfreed by caller
 */
char *slurm_get_ext_sensors_type(void)
{
	char *ext_sensors_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		ext_sensors_type =
			xstrdup(conf->ext_sensors_type);
		slurm_conf_unlock();
	}
	return ext_sensors_type;
}

extern uint16_t slurm_get_ext_sensors_freq(void)
{
	uint16_t freq = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		freq = conf->ext_sensors_freq;
		slurm_conf_unlock();
	}
	return freq;
}

/* slurm_get_jobcomp_type
 * returns the job completion logger type from slurmctld_conf object
 * RET char *    - job completion type,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_type(void)
{
	char *jobcomp_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		jobcomp_type = xstrdup(conf->job_comp_type);
		slurm_conf_unlock();
	}
	return jobcomp_type;
}

/* slurm_get_jobcomp_loc
 * returns the job completion loc from slurmctld_conf object
 * RET char *    - job completion location,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_loc(void)
{
	char *jobcomp_loc = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		jobcomp_loc = xstrdup(conf->job_comp_loc);
		slurm_conf_unlock();
	}
	return jobcomp_loc;
}

/* slurm_get_jobcomp_user
 * returns the storage user from slurmctld_conf object
 * RET char *    - storage user,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_user(void)
{
	char *storage_user = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		storage_user = xstrdup(conf->job_comp_user);
		slurm_conf_unlock();
	}
	return storage_user;
}

/* slurm_get_jobcomp_host
 * returns the storage host from slurmctld_conf object
 * RET char *    - storage host,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_host(void)
{
	char *storage_host = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		storage_host = xstrdup(conf->job_comp_host);
		slurm_conf_unlock();
	}
	return storage_host;
}

/* slurm_get_jobcomp_pass
 * returns the storage password from slurmctld_conf object
 * RET char *    - storage password,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_pass(void)
{
	char *storage_pass = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		storage_pass = xstrdup(conf->job_comp_pass);
		slurm_conf_unlock();
	}
	return storage_pass;
}

/* slurm_get_jobcomp_port
 * returns the storage port from slurmctld_conf object
 * RET uint32_t   - storage port
 */
uint32_t slurm_get_jobcomp_port(void)
{
	uint32_t storage_port = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		storage_port = conf->job_comp_port;
		slurm_conf_unlock();
	}
	return storage_port;

}

/* slurm_set_jobcomp_port
 * sets the jobcomp port in slurmctld_conf object
 * RET 0 or error code
 */
int slurm_set_jobcomp_port(uint32_t port)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		if (port == 0) {
			error("can't have jobcomp port of 0");
			return SLURM_ERROR;
		}

		conf->job_comp_port = port;
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_keep_alive_time
 * returns keep_alive_time slurmctld_conf object
 * RET uint16_t	- keep_alive_time
 */
uint16_t slurm_get_keep_alive_time(void)
{
	uint16_t keep_alive_time = (uint16_t) NO_VAL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		keep_alive_time = conf->keep_alive_time;
		slurm_conf_unlock();
	}
	return keep_alive_time;
}


/* slurm_get_kill_wait
 * returns kill_wait from slurmctld_conf object
 * RET uint16_t	- kill_wait
 */
uint16_t slurm_get_kill_wait(void)
{
	uint16_t kill_wait = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		kill_wait = conf->kill_wait;
		slurm_conf_unlock();
	}
	return kill_wait;
}

/* slurm_get_launch_params
 * get launch_params from slurmctld_conf object
 * RET char *   - launch_params, MUST be xfreed by caller
 */
char *slurm_get_launch_params(void)
{
	char *launch_params = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		launch_params = xstrdup(conf->launch_params);
		slurm_conf_unlock();
	}
	return launch_params;
}

/* slurm_get_launch_type
 * get launch_type from slurmctld_conf object
 * RET char *   - launch_type, MUST be xfreed by caller
 */
char *slurm_get_launch_type(void)
{
	char *launch_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		launch_type = xstrdup(conf->launch_type);
		slurm_conf_unlock();
	}
	return launch_type;
}

/* slurm_set_launch_type
 * set launch_type in slurmctld_conf object
 * RET 0 or error code
 */
int slurm_set_launch_type(char *launch_type)
{
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		xfree(conf->launch_type);
		conf->launch_type = xstrdup(launch_type);
		slurm_conf_unlock();
	}
	return 0;
}

/* slurm_get_preempt_type
 * get PreemptType from slurmctld_conf object
 * RET char *   - preempt type, MUST be xfreed by caller
 */
char *slurm_get_preempt_type(void)
{
	char *preempt_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		preempt_type = xstrdup(conf->preempt_type);
		slurm_conf_unlock();
	}
	return preempt_type;
}

/* slurm_get_proctrack_type
 * get ProctrackType from slurmctld_conf object
 * RET char *   - proctrack type, MUST be xfreed by caller
 */
char *slurm_get_proctrack_type(void)
{
	char *proctrack_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		proctrack_type = xstrdup(conf->proctrack_type);
		slurm_conf_unlock();
	}
	return proctrack_type;
}

/* slurm_get_slurmd_port
 * returns slurmd port from slurmctld_conf object
 * RET uint16_t	- slurmd port
 */
uint16_t slurm_get_slurmd_port(void)
{
	uint16_t slurmd_port = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		slurmd_port = conf->slurmd_port;
		slurm_conf_unlock();
	}
	return slurmd_port;
}

/* slurm_get_slurm_user_id
 * returns slurm uid from slurmctld_conf object
 * RET uint32_t	- slurm user id
 */
uint32_t slurm_get_slurm_user_id(void)
{
	uint32_t slurm_uid = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		slurm_uid = slurmdbd_conf->slurm_user_id;
	} else {
		conf = slurm_conf_lock();
		slurm_uid = conf->slurm_user_id;
		slurm_conf_unlock();
	}
	return slurm_uid;
}

/* slurm_get_slurmd_user_id
 * returns slurmd uid from slurmctld_conf object
 * RET uint32_t	- slurmd user id
 */
uint32_t slurm_get_slurmd_user_id(void)
{
	uint32_t slurmd_uid = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		slurmd_uid = conf->slurmd_user_id;
		slurm_conf_unlock();
	}
	return slurmd_uid;
}

/* slurm_get_root_filter
 * RET uint16_t  - Value of SchedulerRootFilter */
extern uint16_t slurm_get_root_filter(void)
{
	uint16_t root_filter = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		root_filter = conf->schedrootfltr;
		slurm_conf_unlock();
	}
	return root_filter;
}

/* slurm_get_sched_params
 * RET char * - Value of SchedulerParameters, MUST be xfreed by caller */
extern char *slurm_get_sched_params(void)
{
	char *params = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		params = xstrdup(conf->sched_params);
		slurm_conf_unlock();
	}
	return params;
}

/* slurm_get_sched_port
 * RET uint16_t  - Value of SchedulerPort */
extern uint16_t slurm_get_sched_port(void)
{
	uint16_t port = 0;
	slurm_ctl_conf_t *conf;

 	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		port = conf->schedport;
		slurm_conf_unlock();
	}
	return port;
}

/* slurm_get_sched_type
 * get sched type from slurmctld_conf object
 * RET char *   - sched type, MUST be xfreed by caller
 */
char *slurm_get_sched_type(void)
{
	char *sched_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		sched_type = xstrdup(conf->schedtype);
		slurm_conf_unlock();
	}
	return sched_type;
}

/* slurm_get_select_type
 * get select_type from slurmctld_conf object
 * RET char *   - select_type, MUST be xfreed by caller
 */
char *slurm_get_select_type(void)
{
	char *select_type = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		select_type = xstrdup(conf->select_type);
		slurm_conf_unlock();
	}
	return select_type;
}

/* slurm_get_select_type_param
 * get select_type_param from slurmctld_conf object
 * RET uint16_t   - select_type_param
 */
uint16_t slurm_get_select_type_param(void)
{
	uint16_t select_type_param = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		select_type_param = conf->select_type_param;
		slurm_conf_unlock();
	}
	return select_type_param;
}

/** Return true if (remote) system runs Cray XT/XE */
bool is_cray_select_type(void)
{
	bool result = false;

	if (slurmdbd_conf) {
	} else {
		slurm_ctl_conf_t *conf = slurm_conf_lock();
		result = strcasecmp(conf->select_type, "select/cray") == 0;
		slurm_conf_unlock();
	}
	return result;
}

/* slurm_get_switch_type
 * get switch type from slurmctld_conf object
 * RET char *   - switch type, MUST be xfreed by caller
 */
char *slurm_get_switch_type(void)
{
	char *switch_type = NULL;
	slurm_ctl_conf_t *conf;

	conf = slurm_conf_lock();
	switch_type = xstrdup(conf->switch_type);
	slurm_conf_unlock();
	return switch_type;
}

/* slurm_get_wait_time
 * returns wait_time from slurmctld_conf object
 * RET uint16_t	- wait_time
 */
uint16_t slurm_get_wait_time(void)
{
	uint16_t wait_time = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		wait_time = conf->wait_time;
		slurm_conf_unlock();
	}
	return wait_time;
}

/* slurm_get_srun_prolog
 * return the name of the srun prolog program
 * RET char *   - name of prolog program, must be xfreed by caller
 */
char *slurm_get_srun_prolog(void)
{
	char *prolog = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		prolog = xstrdup(conf->srun_prolog);
		slurm_conf_unlock();
	}
	return prolog;
}

/* slurm_get_srun_epilog
 * return the name of the srun epilog program
 * RET char *   - name of epilog program, must be xfreed by caller
 */
char *slurm_get_srun_epilog(void)
{
	char *epilog = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		epilog = xstrdup(conf->srun_epilog);
		slurm_conf_unlock();
	}
	return epilog;
}

/* slurm_get_task_epilog
 * RET task_epilog name, must be xfreed by caller */
char *slurm_get_task_epilog(void)
{
	char *task_epilog = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		task_epilog = xstrdup(conf->task_epilog);
		slurm_conf_unlock();
	}
	return task_epilog;
}

/* slurm_get_task_prolog
 * RET task_prolog name, must be xfreed by caller */
char *slurm_get_task_prolog(void)
{
	char *task_prolog = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		task_prolog = xstrdup(conf->task_prolog);
		slurm_conf_unlock();
	}
	return task_prolog;
}

/*  slurm_get_srun_port_range()
 */
uint16_t *
slurm_get_srun_port_range(void)
{
	uint16_t *ports = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		ports = conf->srun_port_range;
		slurm_conf_unlock();
	}
	return ports;	/* CLANG false positive */
}

/* slurm_get_task_plugin
 * RET task_plugin name, must be xfreed by caller */
char *slurm_get_task_plugin(void)
{
	char *task_plugin = NULL;
	slurm_ctl_conf_t *conf;

	conf = slurm_conf_lock();
	task_plugin = xstrdup(conf->task_plugin);
	slurm_conf_unlock();
	return task_plugin;
}

/* slurm_get_task_plugin_param */
uint32_t slurm_get_task_plugin_param(void)
{
	uint32_t task_plugin_param = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		task_plugin_param = conf->task_plugin_param;
		slurm_conf_unlock();
	}
	return task_plugin_param;
}

/* Get SchedulerTimeSlice (secs) */
uint16_t slurm_get_time_slice(void)
{
	uint16_t sched_time_slice = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		sched_time_slice = conf->sched_time_slice;
		slurm_conf_unlock();
	}
	return sched_time_slice;
}

/* slurm_get_core_spec_plugin
 * RET core_spec plugin name, must be xfreed by caller */
char *slurm_get_core_spec_plugin(void)
{
	char *core_spec_plugin = NULL;
	slurm_ctl_conf_t *conf;

	conf = slurm_conf_lock();
	core_spec_plugin = xstrdup(conf->core_spec_plugin);
	slurm_conf_unlock();
	return core_spec_plugin;
}

/* slurm_get_job_container_plugin
 * RET job_container plugin name, must be xfreed by caller */
char *slurm_get_job_container_plugin(void)
{
	char *job_container_plugin = NULL;
	slurm_ctl_conf_t *conf;

	conf = slurm_conf_lock();
	job_container_plugin = xstrdup(conf->job_container_plugin);
	slurm_conf_unlock();
	return job_container_plugin;
}

/* slurm_get_slurmd_spooldir
 * RET slurmd_spooldir name, must be xfreed by caller */
char *slurm_get_slurmd_spooldir(void)
{
	char *slurmd_spooldir = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		slurmd_spooldir = xstrdup(conf->slurmd_spooldir);
		slurm_conf_unlock();
	}
	return slurmd_spooldir;
}

/* slurm_get_layouts
 * RET comma seperated list of layouts in a string, must be xfreed by caller */
char *slurm_get_layouts(void)
{
	char* layouts = NULL;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
		layouts = xstrdup("");
	} else {
		conf = slurm_conf_lock();
		layouts = xstrdup(conf->layouts);
		slurm_conf_unlock();
	}
	return layouts;
}

/*  slurm_get_srun_eio_timeout()
 */
int16_t
slurm_get_srun_eio_timeout(void)
{
	int16_t eio_timeout = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		eio_timeout = conf->eio_timeout;
		slurm_conf_unlock();
	}
	return eio_timeout;
}

/* Change general slurm communication errors to slurmctld specific errors */
static void _remap_slurmctld_errno(void)
{
	int err = slurm_get_errno();

	if (err == SLURM_COMMUNICATIONS_CONNECTION_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);
	else if (err ==  SLURM_COMMUNICATIONS_SEND_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_SEND_ERROR);
	else if (err == SLURM_COMMUNICATIONS_RECEIVE_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR);
	else if (err == SLURM_COMMUNICATIONS_SHUTDOWN_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR);
}

/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections. Retry if bind() or listen() fail
 *      even if asked for an ephemeral port.
 *
 * IN  port     - port to bind the msg server to
 * RET slurm_fd_t - file descriptor of the connection created
 */
slurm_fd_t slurm_init_msg_engine_port(uint16_t port)
{
	slurm_fd_t cc;
	slurm_addr_t addr;
	int cnt;

	cnt = 0;
eagain:
	slurm_setup_sockaddr(&addr, port);
	cc = slurm_init_msg_engine(&addr);
	if (cc < 0 && port == 0) {
		++cnt;
		if (cnt <= 5) {
			usleep(5000);
			goto eagain;
		}
	}
	return cc;
}

/* slurm_init_msg_engine_ports()
 */
slurm_fd_t
slurm_init_msg_engine_ports(uint16_t *ports)
{
	int cc;
	int val;
	int s;
	int port;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0)
		return -1;

	val = 1;
	cc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (cc < 0) {
		close(s);
		return -1;
	}

	port = sock_bind_range(s, ports);
	if (port < 0) {
		close(s);
		return -1;
	}

	cc = listen(s, SLURM_PROTOCOL_DEFAULT_LISTEN_BACKLOG);
	if (cc < 0) {
		close(s);
		return -1;
	}

	return s;
}

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections.
 *
 * IN  addr_name - address to bind the msg server to (NULL means any)
 * IN  port      - port to bind the msg server to
 * RET slurm_fd_t  - file descriptor of the connection created
 */
slurm_fd_t slurm_init_msg_engine_addrname_port(char *addr_name, uint16_t port)
{
	slurm_addr_t addr;
	static uint32_t bind_addr = NO_VAL;

	if (bind_addr == NO_VAL) {
#ifdef BIND_SPECIFIC_ADDR
		bind_addr = 1;
#else
		char *topology_params = slurm_get_topology_param();
		if (topology_params &&
		    slurm_strcasestr(topology_params, "NoInAddrAny"))
			bind_addr = 1;
		else
			bind_addr = 0;
		xfree(topology_params);
#endif
	}

	if (addr_name)
		slurm_set_addr(&addr, port, addr_name);
	else
		_slurm_set_addr_any(&addr, port);

	return slurm_init_msg_engine(&addr);
}

/*
 *  Close an established message engine.
 *    Returns SLURM_SUCCESS or SLURM_FAILURE.
 *
 * IN  fd  - an open file descriptor to close
 * RET int - the return code
 */
int slurm_shutdown_msg_engine(slurm_fd_t fd)
{
	int rc = slurm_close(fd);
	if (rc)
		slurm_seterrno(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR);
	return rc;
}

/*
 *   Close an established message connection.
 *     Returns SLURM_SUCCESS or SLURM_FAILURE.
 *
 * IN  fd  - an open file descriptor to close
 * RET int - the return code
 */
int slurm_shutdown_msg_conn(slurm_fd_t fd)
{
	return slurm_close(fd);
}

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/* In the bsd socket implementation it creates a SOCK_STREAM socket
 *	and calls connect on it a SOCK_DGRAM socket called with connect
 *	is defined to only receive messages from the address/port pair
 *	argument of the connect call slurm_address - for now it is
 *	really just a sockaddr_in
 * IN slurm_address	- slurm_addr_t of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd_t slurm_open_msg_conn(slurm_addr_t * slurm_address)
{
	slurm_fd_t fd = slurm_open_stream(slurm_address, false);
	if (fd >= 0)
		fd_set_close_on_exec(fd);
	return fd;
}

/* Calls connect to make a connection-less datagram connection to the
 *	primary or secondary slurmctld message engine. If the controller
 *	is very busy the connect may fail, so retry a couple of times.
 * IN/OUT addr     - address of controller contacted
 * RET slurm_fd	- file descriptor of the connection created
 */
slurm_fd_t slurm_open_controller_conn(slurm_addr_t *addr)
{
	slurm_fd_t fd = -1;
	slurm_ctl_conf_t *conf;
	slurm_protocol_config_t *myproto = NULL;
	int retry, max_retry_period, have_backup = 0;

	if (!working_cluster_rec) {
		/* This means the addr wasn't set up already.
		*/
		if (slurm_api_set_default_config() < 0)
			return SLURM_FAILURE;
		myproto = xmalloc(sizeof(slurm_protocol_config_t));
		memcpy(myproto, proto_conf, sizeof(slurm_protocol_config_t));
		myproto->primary_controller.sin_port =
				htons(slurmctld_conf.slurmctld_port +
				(((time(NULL) + getpid()) %
				slurmctld_conf.slurmctld_port_count)));
		myproto->secondary_controller.sin_port =
				myproto->primary_controller.sin_port;
	}

#ifdef HAVE_NATIVE_CRAY
	max_retry_period = 180;
#else
	max_retry_period = slurm_get_msg_timeout();
#endif
	for (retry = 0; retry < max_retry_period; retry++) {
		if (retry)
			sleep(1);
		if (working_cluster_rec) {
			if (working_cluster_rec->control_addr.sin_port == 0) {
				slurm_set_addr(
					&working_cluster_rec->control_addr,
					working_cluster_rec->control_port,
					working_cluster_rec->control_host);
			}
			addr = &working_cluster_rec->control_addr;

			fd = slurm_open_msg_conn(addr);
			if (fd >= 0)
				goto end_it;
			debug("Failed to contact controller: %m");
		} else {
			fd = slurm_open_msg_conn(&myproto->primary_controller);
			if (fd >= 0)
				goto end_it;
			debug("Failed to contact primary controller: %m");

			if (retry == 0) {
				conf = slurm_conf_lock();
				if (conf->backup_controller)
					have_backup = 1;
				slurm_conf_unlock();
			}

			if (have_backup) {
				fd = slurm_open_msg_conn(&myproto->
							 secondary_controller);
				if (fd >= 0) {
					debug("Contacted secondary controller");
					goto end_it;
				}
				debug("Failed to contact secondary "
				      "controller: %m");
			}
		}
	}
	addr = NULL;
	slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);

end_it:
	xfree(myproto);
	return fd;
}

/* calls connect to make a connection-less datagram connection to the
 *	primary or secondary slurmctld message engine
 * RET slurm_fd_t - file descriptor of the connection created
 * IN dest      - controller to contact, primary or secondary
 */
slurm_fd_t slurm_open_controller_conn_spec(enum controller_id dest)
{
	slurm_addr_t *addr;
	slurm_fd_t rc;

	if (slurm_api_set_default_config() < 0) {
		debug3("Error: Unable to set default config");
		return SLURM_ERROR;
	}

	if (dest == PRIMARY_CONTROLLER)
		addr = &proto_conf->primary_controller;
	else {	/* (dest == SECONDARY_CONTROLLER) */
		slurm_ctl_conf_t *conf;
		addr = NULL;
		conf = slurm_conf_lock();
		if (conf->backup_addr)
			addr = &proto_conf->secondary_controller;
		slurm_conf_unlock();
		if (!addr)
			return SLURM_ERROR;
	}

	rc = slurm_open_msg_conn(addr);
	if (rc == -1)
		_remap_slurmctld_errno();
	return rc;
}

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/*
 * NOTE: memory is allocated for the returned msg must be freed at
 *       some point using the slurm_free_functions.
 * IN open_fd	- file descriptor to receive msg on
 * OUT msg	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
int slurm_receive_msg(slurm_fd_t fd, slurm_msg_t *msg, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	Buf buffer;

	xassert(fd >= 0);
	slurm_msg_t_init(msg);
	msg->conn_fd = fd;

	if (timeout <= 0)
		/* convert secs to msec */
		timeout  = slurm_get_msg_timeout() * 1000;

	else if (timeout > (slurm_get_msg_timeout() * 10000)) {
		debug("%s: You are receiving a message with very long "
		      "timeout of %d seconds", __func__, (timeout/1000));
	} else if (timeout < 1000) {
		error("%s: You are receiving a message with a very short "
		      "timeout of %d msecs", __func__, timeout);
	}

	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward, NULL);
		rc = errno;
		goto total_return;
	}

#if	_DEBUG
	_print_data (buf, buflen);
#endif
	buffer = create_buf(buf, buflen);

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		slurm_addr_t resp_addr;
		char addr_str[32];
		int uid = _unpack_msg_uid(buffer);

		if (!slurm_get_peer_addr(fd, &resp_addr)) {
			slurm_print_slurm_addr(
				&resp_addr, addr_str, sizeof(addr_str));
			error("%s: Invalid Protocol Version %u from uid=%d at %s",
			      __func__, header.version, uid, addr_str);
		} else {
			error("%s: Invalid Protocol Version %u from uid=%d from "
			      "problem connection: %m", __func__,
			      header.version, uid);
		}

		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	if (header.ret_cnt > 0) {
		error("%s: we received more than one message back use "
		      "slurm_receive_msgs instead", __func__);
		header.ret_cnt = 0;
		FREE_NULL_LIST(header.ret_list);
		header.ret_list = NULL;
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		error("%s: We need to forward this to other nodes use "
		      "slurm_receive_msg_and_forward instead", __func__);
	}

	if ((auth_cred = g_slurm_auth_unpack(buffer)) == NULL) {
		error("%s: authentication: %s ", __func__,
		       g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	if (header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = g_slurm_auth_verify( auth_cred, NULL, 2,
					  _global_auth_key() );
	} else {
		char *auth_info = slurm_get_auth_info();
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, auth_info );
		xfree(auth_info);
	}

	if (rc != SLURM_SUCCESS) {
		error("%s: %s has authentication error: %s ", __func__,
		      rpc_num2string(header.msg_type),
		      g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}

	/*
	 * Unpack message body
	 */
	msg->protocol_version = header.version;
	msg->msg_type = header.msg_type;
	msg->flags = header.flags;

	if ((header.body_length > remaining_buf(buffer)) ||
	    (unpack_msg(msg, buffer) != SLURM_SUCCESS)) {
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		goto total_return;
	}

	msg->auth_cred = (void *)auth_cred;

	free_buf(buffer);
	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	slurm_seterrno(rc);
	if (rc != SLURM_SUCCESS) {
		msg->auth_cred = (void *) NULL;
		error("%s: %s", __func__, slurm_strerror(rc));
		rc = -1;
		usleep(10000);	/* Discourage brute force attack */
	} else {
		rc = 0;
	}
	return rc;

}

/*
 * NOTE: memory is allocated for the returned list
 *       and must be freed at some point using the list_destroy function.
 * IN open_fd	- file descriptor to receive msg on
 * IN steps	- how many steps down the tree we have to wait for
 * IN timeout	- how long to wait in milliseconds
 * RET List	- List containing the responses of the children (if any) we
 *		  forwarded the message to. List containing type
 *		  (ret_data_info_t).
 */
List slurm_receive_msgs(slurm_fd_t fd, int steps, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	slurm_msg_t msg;
	Buf buffer;
	ret_data_info_t *ret_data_info = NULL;
	List ret_list = NULL;
	int orig_timeout = timeout;

	xassert(fd >= 0);

	slurm_msg_t_init(&msg);
	msg.conn_fd = fd;

	if (timeout <= 0) {
		/* convert secs to msec */
		timeout  = slurm_get_msg_timeout() * 1000;
		orig_timeout = timeout;
	}
	if (steps) {
		if (message_timeout < 0)
			message_timeout = slurm_get_msg_timeout() * 1000;
		orig_timeout = (timeout -
				(message_timeout*(steps-1)))/steps;
		steps--;
	}

	debug4("orig_timeout was %d we have %d steps and a timeout of %d",
	       orig_timeout, steps, timeout);
	/* we compare to the orig_timeout here because that is really
	 *  what we are going to wait for each step
	 */
	if (orig_timeout >= (slurm_get_msg_timeout() * 10000)) {
		debug("slurm_receive_msgs: "
		      "You are sending a message with timeout's greater "
		      "than %d seconds, your's is %d seconds",
		      (slurm_get_msg_timeout() * 10),
		      (timeout/1000));
	} else if (orig_timeout < 1000) {
		debug("slurm_receive_msgs: "
		      "You are sending a message with a very short timeout of "
		      "%d milliseconds each step in the tree has %d "
		      "milliseconds", timeout, orig_timeout);
	}


	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward, NULL);
		rc = errno;
		goto total_return;
	}

#if	_DEBUG
	_print_data (buf, buflen);
#endif
	buffer = create_buf(buf, buflen);

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		slurm_addr_t resp_addr;
		char addr_str[32];
		int uid = _unpack_msg_uid(buffer);
		if (!slurm_get_peer_addr(fd, &resp_addr)) {
			slurm_print_slurm_addr(
				&resp_addr, addr_str, sizeof(addr_str));
			error("Invalid Protocol Version %u from uid=%d at %s",
			      header.version, uid, addr_str);
		} else {
			error("Invalid Protocol Version %u from uid=%d from "
			      "problem connection: %m",
			      header.version, uid);
		}

		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	if (header.ret_cnt > 0) {
		if (header.ret_list)
			ret_list = header.ret_list;
		else
			ret_list = list_create(destroy_data_info);
		header.ret_cnt = 0;
		header.ret_list = NULL;
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		error("We need to forward this to other nodes use "
		      "slurm_receive_msg_and_forward instead");
	}

	if ((auth_cred = g_slurm_auth_unpack(buffer)) == NULL) {
		error( "authentication: %s ",
		       g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	if (header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = g_slurm_auth_verify( auth_cred, NULL, 2,
					  _global_auth_key() );
	} else {
		char *auth_info = slurm_get_auth_info();
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, auth_info );
		xfree(auth_info);
	}

	if (rc != SLURM_SUCCESS) {
		error("authentication: %s ",
		      g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}

	/*
	 * Unpack message body
	 */
	msg.protocol_version = header.version;
	msg.msg_type = header.msg_type;
	msg.flags = header.flags;

	if ((header.body_length > remaining_buf(buffer)) ||
	    (unpack_msg(&msg, buffer) != SLURM_SUCCESS)) {
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	g_slurm_auth_destroy(auth_cred);

	free_buf(buffer);
	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	if (rc != SLURM_SUCCESS) {
		if (ret_list) {
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			ret_data_info->err = rc;
			ret_data_info->type = RESPONSE_FORWARD_FAILED;
			ret_data_info->data = NULL;
			list_push(ret_list, ret_data_info);
		}
		error("slurm_receive_msgs: %s", slurm_strerror(rc));
		usleep(10000);	/* Discourage brute force attack */
	} else {
		if (!ret_list)
			ret_list = list_create(destroy_data_info);
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		ret_data_info->err = rc;
		ret_data_info->node_name = NULL;
		ret_data_info->type = msg.msg_type;
		ret_data_info->data = msg.data;
		list_push(ret_list, ret_data_info);
	}


	errno = rc;
	return ret_list;

}

/* try to determine the UID associated with a message with different
 * message header version, return -1 if we can't tell */
static int _unpack_msg_uid(Buf buffer)
{
	int uid = -1;
	void *auth_cred = NULL;

	if ((auth_cred = g_slurm_auth_unpack(buffer)) == NULL)
		return uid;
	uid = (int) g_slurm_auth_get_uid(auth_cred, slurm_get_auth_info());
	g_slurm_auth_destroy(auth_cred);

	return uid;
}

/*
 * NOTE: memory is allocated for the returned msg and the returned list
 *       both must be freed at some point using the slurm_free_functions
 *       and list_destroy function.
 * IN open_fd	- file descriptor to receive msg on
 * IN/OUT msg	- a slurm_msg struct to be filled in by the function
 *		  we use the orig_addr from this var for forwarding.
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
int slurm_receive_msg_and_forward(slurm_fd_t fd, slurm_addr_t *orig_addr,
				  slurm_msg_t *msg, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	Buf buffer;

	xassert(fd >= 0);

	if (msg->forward.init != FORWARD_INIT)
		slurm_msg_t_init(msg);
	/* set msg connection fd to accepted fd. This allows
	 *  possibility for slurmd_req () to close accepted connection
	 */
	msg->conn_fd = fd;
	/* this always is the connection */
	memcpy(&msg->address, orig_addr, sizeof(slurm_addr_t));

	/* where the connection originated from, this
	 * might change based on the header we receive */
	memcpy(&msg->orig_addr, orig_addr, sizeof(slurm_addr_t));

	msg->ret_list = list_create(destroy_data_info);

	if (timeout <= 0)
		/* convert secs to msec */
		timeout  = slurm_get_msg_timeout() * 1000;

	if (timeout >= (slurm_get_msg_timeout() * 10000)) {
		debug("slurm_receive_msg_and_forward: "
		      "You are sending a message with timeout's greater "
		      "than %d seconds, your's is %d seconds",
		      (slurm_get_msg_timeout() * 10),
		      (timeout/1000));
	} else if (timeout < 1000) {
		debug("slurm_receive_msg_and_forward: "
		      "You are sending a message with a very short timeout of "
		      "%d milliseconds", timeout);
	}

	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward, NULL);
		rc = errno;
		goto total_return;
	}

#if	_DEBUG
	_print_data (buf, buflen);
#endif
	buffer = create_buf(buf, buflen);

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		slurm_addr_t resp_addr;
		char addr_str[32];
		int uid = _unpack_msg_uid(buffer);

		if (!slurm_get_peer_addr(fd, &resp_addr)) {
			slurm_print_slurm_addr(
				&resp_addr, addr_str, sizeof(addr_str));
			error("Invalid Protocol Version %u from uid=%d at %s",
			      header.version, uid, addr_str);
		} else {
			error("Invalid Protocol Version %u from uid=%d from "
			      "problem connection: %m",
			      header.version, uid);
		}

		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	if (header.ret_cnt > 0) {
		error("we received more than one message back use "
		      "slurm_receive_msgs instead");
		header.ret_cnt = 0;
		FREE_NULL_LIST(header.ret_list);
		header.ret_list = NULL;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	/* if (header.ret_cnt > 0) { */
/* 		while ((ret_data_info = list_pop(header.ret_list))) */
/* 			list_push(msg->ret_list, ret_data_info); */
/* 		header.ret_cnt = 0; */
/* 		FREE_NULL_LIST(header.ret_list); */
/* 		header.ret_list = NULL; */
/* 	} */
	/*
	 * header.orig_addr will be set to where the first message
	 * came from if this is a forward else we set the
	 * header.orig_addr to our addr just incase we need to send it off.
	 */
	if (header.orig_addr.sin_addr.s_addr != 0) {
		memcpy(&msg->orig_addr, &header.orig_addr, sizeof(slurm_addr_t));
	} else {
		memcpy(&header.orig_addr, orig_addr, sizeof(slurm_addr_t));
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		debug2("forwarding to %u", header.forward.cnt);
		msg->forward_struct = xmalloc(sizeof(forward_struct_t));
		slurm_mutex_init(&msg->forward_struct->forward_mutex);
		pthread_cond_init(&msg->forward_struct->notify, NULL);

		msg->forward_struct->buf_len = remaining_buf(buffer);
		msg->forward_struct->buf =
			xmalloc(sizeof(char) * msg->forward_struct->buf_len);
		memcpy(msg->forward_struct->buf,
		       &buffer->head[buffer->processed],
		       msg->forward_struct->buf_len);

		msg->forward_struct->ret_list = msg->ret_list;
		/* take out the amount of timeout from this hop */
		msg->forward_struct->timeout = header.forward.timeout;
		if (msg->forward_struct->timeout <= 0)
			msg->forward_struct->timeout = message_timeout;
		msg->forward_struct->fwd_cnt = header.forward.cnt;

		debug3("forwarding messages to %u nodes with timeout of %d",
		       msg->forward_struct->fwd_cnt,
		       msg->forward_struct->timeout);

		if (forward_msg(msg->forward_struct, &header) == SLURM_ERROR) {
			error("problem with forward msg");
		}
	}

	if ((auth_cred = g_slurm_auth_unpack(buffer)) == NULL) {
		error( "authentication: %s ",
		       g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	if (header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = g_slurm_auth_verify( auth_cred, NULL, 2,
					  _global_auth_key() );
	} else {
		char *auth_info = slurm_get_auth_info();
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, auth_info );
		xfree(auth_info);
	}

	if (rc != SLURM_SUCCESS) {
		error( "authentication: %s ",
		       g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}

	/*
	 * Unpack message body
	 */
	msg->protocol_version = header.version;
	msg->msg_type = header.msg_type;
	msg->flags = header.flags;

	if (header.msg_type == MESSAGE_COMPOSITE) {
		msg_aggr_add_comp(buffer, auth_cred, &header);
		goto total_return;
	}

	if ( (header.body_length > remaining_buf(buffer)) ||
	     (unpack_msg(msg, buffer) != SLURM_SUCCESS) ) {
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	msg->auth_cred = (void *) auth_cred;

	free_buf(buffer);
	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	slurm_seterrno(rc);
	if (rc != SLURM_SUCCESS) {
		msg->msg_type = RESPONSE_FORWARD_FAILED;
		msg->auth_cred = (void *) NULL;
		msg->data = NULL;
		error("slurm_receive_msg_and_forward: %s",
		      slurm_strerror(rc));
		usleep(10000);	/* Discourage brute force attack */
	} else {
		rc = 0;
	}
	return rc;

}

/**********************************************************************\
 * send message functions
\**********************************************************************/

/*
 *  Do the wonderful stuff that needs be done to pack msg
 *  and hdr into buffer
 */
static void
_pack_msg(slurm_msg_t *msg, header_t *hdr, Buf buffer)
{
	unsigned int tmplen, msglen;

	tmplen = get_buf_offset(buffer);
	pack_msg(msg, buffer);
	msglen = get_buf_offset(buffer) - tmplen;

	/* update header with correct cred and msg lengths */
	update_header(hdr, msglen);

	/* repack updated header */
	tmplen = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack_header(hdr, buffer);
	set_buf_offset(buffer, tmplen);
}

/*
 *  Send a slurm message over an open file descriptor `fd'
 *    Returns the size of the message sent in bytes, or -1 on failure.
 */
int slurm_send_node_msg(slurm_fd_t fd, slurm_msg_t * msg)
{
	header_t header;
	Buf      buffer;
	int      rc;
	void *   auth_cred;
	time_t   start_time = time(NULL);

	/*
	 * Initialize header with Auth credential and message type.
	 * We get the credential now rather than later so the work can
	 * can be done in parallel with waiting for message to forward,
	 * but we may need to generate the credential again later if we
	 * wait too long for the incoming message.
	 */
	if (msg->flags & SLURM_GLOBAL_AUTH_KEY) {
		auth_cred = g_slurm_auth_create(NULL, 2, _global_auth_key());
	} else {
		char *auth_info = slurm_get_auth_info();
		auth_cred = g_slurm_auth_create(NULL, 2, auth_info);
		xfree(auth_info);
	}

	if (msg->forward.init != FORWARD_INIT) {
		forward_init(&msg->forward, NULL);
		msg->ret_list = NULL;
	}
	forward_wait(msg);

	if (difftime(time(NULL), start_time) >= 60) {
		(void) g_slurm_auth_destroy(auth_cred);
		if (msg->flags & SLURM_GLOBAL_AUTH_KEY) {
			auth_cred = g_slurm_auth_create(NULL, 2,
							_global_auth_key());
		} else {
			char *auth_info = slurm_get_auth_info();
			auth_cred = g_slurm_auth_create(NULL, 2, auth_info);
			xfree(auth_info);
		}
	}
	if (auth_cred == NULL) {
		error("authentication: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(NULL)) );
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	init_header(&header, msg, msg->flags);

	/*
	 * Pack header into buffer for transmission
	 */
	buffer = init_buf(BUF_SIZE);
	pack_header(&header, buffer);

	/*
	 * Pack auth credential
	 */
	rc = g_slurm_auth_pack(auth_cred, buffer);
	(void) g_slurm_auth_destroy(auth_cred);
	if (rc) {
		error("authentication: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		free_buf(buffer);
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	/*
	 * Pack message into buffer
	 */
	_pack_msg(msg, &header, buffer);

#if	_DEBUG
	_print_data (get_buf_data(buffer),get_buf_offset(buffer));
#endif
	/*
	 * Send message
	 */
	rc = slurm_msg_sendto( fd, get_buf_data(buffer),
			       get_buf_offset(buffer),
			       SLURM_PROTOCOL_NO_SEND_RECV_FLAGS );

	if ((rc < 0) && (errno == ENOTCONN)) {
		debug3("slurm_msg_sendto: peer has disappeared for msg_type=%u",
		       msg->msg_type);
	} else if (rc < 0) {
		slurm_addr_t peer_addr;
		char addr_str[32];
		if (!slurm_get_peer_addr(fd, &peer_addr)) {
			slurm_print_slurm_addr(
				&peer_addr, addr_str, sizeof(addr_str));
			error("slurm_msg_sendto: address:port=%s "
			      "msg_type=%u: %m",
			      addr_str, msg->msg_type);
		} else if (errno == ENOTCONN)
			debug3("slurm_msg_sendto: peer has disappeared "
			       "for msg_type=%u",
			       msg->msg_type);
		else
			error("slurm_msg_sendto: msg_type=%u: %m",
			      msg->msg_type);
	}

	free_buf(buffer);
	return rc;
}

/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t slurm_write_stream(slurm_fd_t open_fd, char *buffer, size_t size)
{
	return slurm_send_timeout(open_fd, buffer, size,
				  SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				  (slurm_get_msg_timeout() * 1000));
}
size_t slurm_write_stream_timeout(slurm_fd_t open_fd, char *buffer,
				  size_t size, int timeout)
{
	return slurm_send_timeout(open_fd, buffer, size,
				  SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				  timeout);
}

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd	- file descriptor to read from
 * OUT buffer   - buffer to receive into
 * IN size	- size of buffer
 * IN timeout	- how long to wait in milliseconds
 * RET size_t	- bytes read , or -1 on errror
 */
size_t slurm_read_stream(slurm_fd_t open_fd, char *buffer, size_t size)
{
	return slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   (slurm_get_msg_timeout() * 1000));
}
size_t slurm_read_stream_timeout(slurm_fd_t open_fd, char *buffer,
				 size_t size, int timeout)
{
	return slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   timeout);
}

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr_any
 * initialized the slurm_address with the supplied port on INADDR_ANY
 * OUT slurm_address	- slurm_addr_t to be filled in
 * IN port		- port in host order
 */
static void _slurm_set_addr_any(slurm_addr_t * slurm_address, uint16_t port)
{
	slurm_set_addr_uint(slurm_address, port, SLURM_INADDR_ANY);
}

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and host name
 * OUT slurm_address	- slurm_addr_t to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name
 */
void slurm_set_addr(slurm_addr_t * slurm_address, uint16_t port, char *host)
{
	slurm_set_addr_char(slurm_address, port, host);
}

/* slurm_get_ip_str
 * given a slurm_address it returns its port and ip address string
 * IN slurm_address	- slurm_addr_t to be queried
 * OUT port		- port number
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
void slurm_get_ip_str(slurm_addr_t * slurm_address, uint16_t * port,
		      char *ip, unsigned int buf_len)
{
	unsigned char *uc = (unsigned char *)&slurm_address->sin_addr.s_addr;
	*port = slurm_address->sin_port;
	snprintf(ip, buf_len, "%u.%u.%u.%u", uc[0], uc[1], uc[2], uc[3]);
}

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
int slurm_get_peer_addr(slurm_fd_t fd, slurm_addr_t * slurm_address)
{
	struct sockaddr name;
	socklen_t namelen = (socklen_t) sizeof(struct sockaddr);
	int rc;

	if ((rc = slurm_getpeername((int) fd, &name, &namelen)))
		return rc;
	memcpy(slurm_address, &name, sizeof(slurm_addr_t));
	return 0;
}

/**********************************************************************\
 * slurm_addr_t pack routines
\**********************************************************************/

/* slurm_pack_slurm_addr_array
 * packs an array of slurm_addrs into a buffer
 * OUT slurm_address	- slurm_addr_t to pack
 * IN size_val  	- how many to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr_t from
 * returns		- SLURM error code
 */
void slurm_pack_slurm_addr_array(slurm_addr_t * slurm_address,
				 uint32_t size_val, Buf buffer)
{
	int i = 0;
	uint32_t nl = htonl(size_val);
	pack32(nl, buffer);

	for (i = 0; i < size_val; i++) {
		slurm_pack_slurm_addr(slurm_address + i, buffer);
	}

}

/* slurm_unpack_slurm_addr_array
 * unpacks an array of slurm_addrs from a buffer
 * OUT slurm_address	- slurm_addr_t to unpack to
 * IN size_val  	- how many to unpack
 * IN/OUT buffer	- buffer to upack the slurm_addr_t from
 * returns		- SLURM error code
 */
int slurm_unpack_slurm_addr_array(slurm_addr_t ** slurm_address,
				  uint32_t * size_val, Buf buffer)
{
	int i = 0;
	uint32_t nl;

	*slurm_address = NULL;
	safe_unpack32(&nl, buffer);
	*size_val = ntohl(nl);
	*slurm_address = xmalloc((*size_val) * sizeof(slurm_addr_t));

	for (i = 0; i < *size_val; i++) {
		if (slurm_unpack_slurm_addr_no_alloc((*slurm_address) + i,
						     buffer))
			goto unpack_error;

	}
	return SLURM_SUCCESS;

unpack_error:
	xfree(*slurm_address);
	*slurm_address = NULL;
	return SLURM_ERROR;
}

static void _rc_msg_setup(slurm_msg_t *msg, slurm_msg_t *resp_msg,
			  return_code_msg_t *rc_msg, int rc)
{
	memset(rc_msg, 0, sizeof(return_code_msg_t));
	rc_msg->return_code = rc;

	slurm_msg_t_init(resp_msg);
	resp_msg->protocol_version = msg->protocol_version;
	resp_msg->address  = msg->address;
	resp_msg->msg_type = RESPONSE_SLURM_RC;
	resp_msg->data     = rc_msg;
	resp_msg->flags = msg->flags;
	resp_msg->forward = msg->forward;
	resp_msg->forward_struct = msg->forward_struct;
	resp_msg->ret_list = msg->ret_list;
	resp_msg->orig_addr = msg->orig_addr;
}


/**********************************************************************\
 * simplified communication routines
 * They open a connection do work then close the connection all within
 * the function
\**********************************************************************/

/* slurm_send_rc_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 */
int slurm_send_rc_msg(slurm_msg_t *msg, int rc)
{
	if (msg->msg_index && msg->ret_list) {
		slurm_msg_t *resp_msg = xmalloc_nz(sizeof(slurm_msg_t));
		return_code_msg_t *rc_msg =
			xmalloc_nz(sizeof(return_code_msg_t));

		_rc_msg_setup(msg, resp_msg, rc_msg, rc);

		resp_msg->msg_index = msg->msg_index;
		resp_msg->ret_list = NULL;
		/* The return list here is the list we are sending to
		   the node, so after we attach this message to it set
		   it to NULL to remove it.
		*/
		list_append(msg->ret_list, resp_msg);
		return SLURM_SUCCESS;
	} else {
		slurm_msg_t resp_msg;
		return_code_msg_t rc_msg;

		if (msg->conn_fd < 0) {
			slurm_seterrno(ENOTCONN);
			return SLURM_ERROR;
		}
		_rc_msg_setup(msg, &resp_msg, &rc_msg, rc);

		/* send message */
		return slurm_send_node_msg(msg->conn_fd, &resp_msg);
	}
}

/* slurm_send_rc_err_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 * IN err_msg   	- message for user
 */
int slurm_send_rc_err_msg(slurm_msg_t *msg, int rc, char *err_msg)
{
	slurm_msg_t resp_msg;
	return_code2_msg_t rc_msg;

	if (msg->conn_fd < 0) {
		slurm_seterrno(ENOTCONN);
		return SLURM_ERROR;
	}
	rc_msg.return_code = rc;
	rc_msg.err_msg     = err_msg;

	slurm_msg_t_init(&resp_msg);
	resp_msg.protocol_version = msg->protocol_version;
	resp_msg.address  = msg->address;
	resp_msg.msg_type = RESPONSE_SLURM_RC_MSG;
	resp_msg.data     = &rc_msg;
	resp_msg.flags = msg->flags;
	resp_msg.forward = msg->forward;
	resp_msg.forward_struct = msg->forward_struct;
	resp_msg.ret_list = msg->ret_list;
	resp_msg.orig_addr = msg->orig_addr;

	/* send message */
	return slurm_send_node_msg(msg->conn_fd, &resp_msg);
}

/*
 * Send and recv a slurm request and response on the open slurm descriptor
 * IN fd	- file descriptor to receive msg on
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT resp	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
static int
_send_and_recv_msg(slurm_fd_t fd, slurm_msg_t *req,
		   slurm_msg_t *resp, int timeout)
{
	int retry = 0;
	int rc = -1;
	slurm_msg_t_init(resp);

	if (slurm_send_node_msg(fd, req) >= 0) {
		/* no need to adjust and timeouts here since we are not
		   forwarding or expecting anything other than 1 message
		   and the regular timeout will be altered in
		   slurm_receive_msg if it is 0 */
		rc = slurm_receive_msg(fd, resp, timeout);
	}


	/*
	 *  Attempt to close an open connection
	 */
	while ((slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY) {
			break;
		}
	}

	return rc;
}

/*
 * Send and recv a slurm request and response on the open slurm descriptor
 * with a list containing the responses of the children (if any) we
 * forwarded the message to. List containing type (ret_data_info_t).
 * IN fd	- file descriptor to receive msg on
 * IN req	- a slurm_msg struct to be sent by the function
 * IN timeout	- how long to wait in milliseconds
 * RET List	- List containing the responses of the children (if any) we
 *		  forwarded the message to. List containing type
 *		  (ret_data_info_t).
 */
static List
_send_and_recv_msgs(slurm_fd_t fd, slurm_msg_t *req, int timeout)
{
	int retry = 0;
	List ret_list = NULL;
	int steps = 0;
	int width;

	if (!req->forward.timeout) {
		if (!timeout)
			timeout = slurm_get_msg_timeout() * 1000;
		req->forward.timeout = timeout;
	}
	if (slurm_send_node_msg(fd, req) >= 0) {
		if (req->forward.cnt > 0) {
			/* figure out where we are in the tree and set
			 * the timeout for to wait for our children
			 * correctly
			 * (timeout+message_timeout sec per step)
			 * to let the child timeout */
			if (message_timeout < 0)
				message_timeout = slurm_get_msg_timeout() * 1000;
			steps = req->forward.cnt + 1;
			width = slurm_get_tree_width();
			if (width)
				steps /= width;
			timeout = (message_timeout * steps);
			steps++;

			timeout += (req->forward.timeout*steps);
		}
		ret_list = slurm_receive_msgs(fd, steps, timeout);
	}


	/*
	 *  Attempt to close an open connection
	 */
	while ((slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY) {
			break;
		}
	}

	return ret_list;
}


/*
 * slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message,
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int		- returns 0 on success, -1 on failure and sets errno
 */
int slurm_send_recv_controller_msg(slurm_msg_t *req, slurm_msg_t *resp)
{
	slurm_fd_t fd = -1;
	int rc = 0;
	time_t start_time = time(NULL);
	int retry = 1;
	slurm_ctl_conf_t *conf;
	bool backup_controller_flag;
	uint16_t slurmctld_timeout;
	slurm_addr_t ctrl_addr;

	/* Just in case the caller didn't initialize his slurm_msg_t, and
	 * since we KNOW that we are only sending to one node (the controller),
	 * we initialize some forwarding variables to disable forwarding.
	 */
	forward_init(&req->forward, NULL);
	req->ret_list = NULL;
	req->forward_struct = NULL;

	if (working_cluster_rec)
		req->flags |= SLURM_GLOBAL_AUTH_KEY;

	if ((fd = slurm_open_controller_conn(&ctrl_addr)) < 0) {
		rc = -1;
		goto cleanup;
	}

	conf = slurm_conf_lock();
	backup_controller_flag = conf->backup_controller ? true : false;
	slurmctld_timeout = conf->slurmctld_timeout;
	slurm_conf_unlock();

	while (retry) {
		/* If the backup controller is in the process of assuming
		 * control, we sleep and retry later */
		retry = 0;
		rc = _send_and_recv_msg(fd, req, resp, 0);
		if (resp->auth_cred)
			g_slurm_auth_destroy(resp->auth_cred);
		else
			rc = -1;

		if ((rc == 0) && (!working_cluster_rec)
		    && (resp->msg_type == RESPONSE_SLURM_RC)
		    && ((((return_code_msg_t *) resp->data)->return_code)
			== ESLURM_IN_STANDBY_MODE)
		    && (backup_controller_flag)
		    && (difftime(time(NULL), start_time)
			< (slurmctld_timeout + (slurmctld_timeout / 2)))) {

			debug("Neither primary nor backup controller "
			      "responding, sleep and retry");
			slurm_free_return_code_msg(resp->data);
			sleep(30);
			if ((fd = slurm_open_controller_conn(&ctrl_addr))
			    < 0) {
				rc = -1;
			} else {
				retry = 1;
			}
		}

		if (rc == -1)
			break;
	}

cleanup:
	if (rc != 0)
 		_remap_slurmctld_errno();

	return rc;
}

/* slurm_send_recv_node_msg
 * opens a connection to node, sends the node a message, listens
 * for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * IN timeout		- how long to wait in milliseconds
 * RET int		- returns 0 on success, -1 on failure and sets errno
 */
int slurm_send_recv_node_msg(slurm_msg_t *req, slurm_msg_t *resp, int timeout)
{
	slurm_fd_t fd = -1;

	resp->auth_cred = NULL;
	if ((fd = slurm_open_msg_conn(&req->address)) < 0)
		return -1;

	return _send_and_recv_msg(fd, req, resp, timeout);

}

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * RET int		- return code
 * NOTE: NOT INTENDED TO BE CROSS-CLUSTER
 */
int slurm_send_only_controller_msg(slurm_msg_t *req)
{
	int      rc = SLURM_SUCCESS;
	int      retry = 0;
	slurm_fd_t fd = -1;
	slurm_addr_t ctrl_addr;

	/*
	 *  Open connection to SLURM controller:
	 */
	if ((fd = slurm_open_controller_conn(&ctrl_addr)) < 0) {
		rc = SLURM_SOCKET_ERROR;
		goto cleanup;
	}

	if ((rc = slurm_send_node_msg(fd, req) < 0)) {
		rc = SLURM_ERROR;
	} else {
		debug3("slurm_send_only_controller_msg: sent %d", rc);
		rc = SLURM_SUCCESS;
	}

	/*
	 *  Attempt to close an open connection
	 */
	while ( (slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY) {
			rc = SLURM_SOCKET_ERROR;
			goto cleanup;
		}
	}

cleanup:
	if (rc != SLURM_SUCCESS)
		_remap_slurmctld_errno();
	return rc;
}

/*
 *  Open a connection to the "address" specified in the slurm msg `req'
 *   Then, immediately close the connection w/out waiting for a reply.
 *
 *   Returns SLURM_SUCCESS on success SLURM_FAILURE (< 0) for failure.
 */
int slurm_send_only_node_msg(slurm_msg_t *req)
{
	int      rc = SLURM_SUCCESS;
	int      retry = 0;
	slurm_fd_t fd = -1;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		return SLURM_SOCKET_ERROR;
	}

	if ((rc = slurm_send_node_msg(fd, req) < 0)) {
		rc = SLURM_ERROR;
	} else {
		debug3("slurm_send_only_node_msg: sent %d", rc);
		rc = SLURM_SUCCESS;
	}
	/*
	 *  Attempt to close an open connection
	 */
	while ( (slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY)
			return SLURM_SOCKET_ERROR;
	}

	return rc;
}

/*
 *  Send a message to the nodelist specificed using fanout
 *    Then return List containing type (ret_data_info_t).
 * IN nodelist	  - list of nodes to send to.
 * IN msg	  - a slurm_msg struct to be sent by the function
 * IN timeout	  - how long to wait in milliseconds
 * IN quiet       - if set, reduce logging details
 * RET List	  - List containing the responses of the children
 *		    (if any) we forwarded the message to. List
 *		    containing type (ret_data_info_t).
 */
List slurm_send_recv_msgs(const char *nodelist, slurm_msg_t *msg,
			  int timeout, bool quiet)
{
	List ret_list = NULL;
	hostlist_t hl = NULL;

	if (!nodelist || !strlen(nodelist)) {
		error("slurm_send_recv_msgs: no nodelist given");
		return NULL;
	}

	hl = hostlist_create(nodelist);
	if (!hl) {
		error("slurm_send_recv_msgs: problem creating hostlist");
		return NULL;
	}

	ret_list = start_msg_tree(hl, msg, timeout);
	hostlist_destroy(hl);

	return ret_list;
}

/*
 *  Send a message to msg->address
 *    Then return List containing type (ret_data_info_t).
 * IN msg	  - a slurm_msg struct to be sent by the function
 * IN timeout	  - how long to wait in milliseconds
 * RET List	  - List containing the responses of the children
 *		    (if any) we forwarded the message to. List
 *		    containing type (ret_types_t).
 */
List slurm_send_addr_recv_msgs(slurm_msg_t *msg, char *name, int timeout)
{
	static pthread_mutex_t conn_lock = PTHREAD_MUTEX_INITIALIZER;
	static uint16_t conn_timeout = (uint16_t) NO_VAL;
	List ret_list = NULL;
	slurm_fd_t fd = -1;
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr;
	int i;

	slurm_mutex_lock(&conn_lock);
	if (conn_timeout == (uint16_t) NO_VAL)
		conn_timeout = MIN(slurm_get_msg_timeout(), 10);
	slurm_mutex_unlock(&conn_lock);

	/* This connect retry logic permits Slurm hierarchical communications
	 * to better survive slurmd restarts */
	for (i = 0; i <= conn_timeout; i++) {
		if (i)
			sleep(1);
		fd = slurm_open_msg_conn(&msg->address);
		if ((fd >= 0) || (errno != ECONNREFUSED))
			break;
		if (i == 0)
			debug3("connect refused, retrying");
	}
	if (fd < 0) {
		mark_as_failed_forward(&ret_list, name,
				       SLURM_COMMUNICATIONS_CONNECTION_ERROR);
		errno = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
		return ret_list;
	}

	msg->ret_list = NULL;
	msg->forward_struct = NULL;
	if (!(ret_list = _send_and_recv_msgs(fd, msg, timeout))) {
		mark_as_failed_forward(&ret_list, name, errno);
		errno = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
		return ret_list;
	} else {
		itr = list_iterator_create(ret_list);
		while ((ret_data_info = list_next(itr)))
			if (!ret_data_info->node_name) {
				ret_data_info->node_name = xstrdup(name);
			}
		list_iterator_destroy(itr);
	}
	return ret_list;
}

/*
 *  Open a connection to the "address" specified in the slurm msg "req".
 *    Then read back an "rc" message returning the "return_code" specified
 *    in the response in the "rc" parameter.
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT rc	- return code from the sent message
 * IN timeout	- how long to wait in milliseconds
 * RET int either 0 for success or -1 for failure.
 */
int slurm_send_recv_rc_msg_only_one(slurm_msg_t *req, int *rc, int timeout)
{
	slurm_fd_t fd = -1;
	int ret_c = 0;
	slurm_msg_t resp;

	slurm_msg_t_init(&resp);

	/* Just in case the caller didn't initialize his slurm_msg_t, and
	 * since we KNOW that we are only sending to one node,
	 * we initialize some forwarding variables to disable forwarding.
	 */
	forward_init(&req->forward, NULL);
	req->ret_list = NULL;
	req->forward_struct = NULL;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0)
		return -1;
	if (!_send_and_recv_msg(fd, req, &resp, timeout)) {
		if (resp.auth_cred)
			g_slurm_auth_destroy(resp.auth_cred);
		*rc = slurm_get_return_code(resp.msg_type, resp.data);
		slurm_free_msg_data(resp.msg_type, resp.data);
		ret_c = 0;
	} else
		ret_c = -1;
	return ret_c;
}

/*
 *  Send message to controller and get return code.
 *  Make use of slurm_send_recv_controller_msg(), which handles
 *  support for backup controller and retry during transistion.
 */
int slurm_send_recv_controller_rc_msg(slurm_msg_t *req, int *rc)
{
	int ret_c;
	slurm_msg_t resp;

	if (!slurm_send_recv_controller_msg(req, &resp)) {
		*rc = slurm_get_return_code(resp.msg_type, resp.data);
		slurm_free_msg_data(resp.msg_type, resp.data);
		ret_c = 0;
	} else {
		ret_c = -1;
	}

	return ret_c;
}

/* this is used to set how many nodes are going to be on each branch
 * of the tree.
 * IN total       - total number of nodes to send to
 * IN tree_width  - how wide the tree should be on each hop
 * RET int *	  - int array tree_width in length each space
 *		    containing the number of nodes to send to each hop
 *		    on the span.
 */
extern int *set_span(int total,  uint16_t tree_width)
{
	int *span = NULL;
	int left = total;
	int i = 0;

	if (tree_width == 0)
		tree_width = slurm_get_tree_width();

	span = xmalloc(sizeof(int) * tree_width);
	//info("span count = %d", tree_width);
	if (total <= tree_width) {
		return span;
	}

	while (left > 0) {
		for (i = 0; i < tree_width; i++) {
			if ((tree_width-i) >= left) {
				if (span[i] == 0) {
					left = 0;
					break;
				} else {
					span[i] += left;
					left = 0;
					break;
				}
			} else if (left <= tree_width) {
				if (span[i] == 0)
					left--;

				span[i] += left;
				left = 0;
				break;
			}

			if (span[i] == 0)
				left--;

			span[i] += tree_width;
			left -= tree_width;
		}
	}

	return span;
}

/*
 * Free a slurm message
 */
extern void slurm_free_msg(slurm_msg_t * msg)
{
	if (msg) {
		if (msg->auth_cred) {
			(void) g_slurm_auth_destroy(msg->auth_cred);
		}
		FREE_NULL_LIST(msg->ret_list);
		xfree(msg);
	}
}

extern char *nodelist_nth_host(const char *nodelist, int inx)
{
	hostlist_t hl = hostlist_create(nodelist);
	char *name = hostlist_nth(hl, inx);
	hostlist_destroy(hl);
	return name;
}

extern int nodelist_find(const char *nodelist, const char *name)
{
	hostlist_t hl = hostlist_create(nodelist);
	int id = hostlist_find(hl, name);
	hostlist_destroy(hl);
	return id;
}

extern void convert_num_unit2(double num, char *buf, int buf_size,
			      int orig_type, int divisor, uint32_t flags)
{
	char *unit = "\0KMGTP?";
	uint64_t i;

	if ((int64_t)num == 0) {
		snprintf(buf, buf_size, "0");
		return;
	} else if (flags & CONVERT_NUM_UNIT_EXACT) {
		i = (uint64_t)num % (divisor / 2);

		if (i > 0) {
			snprintf(buf, buf_size, "%"PRIu64"%c",
				 (uint64_t)num, unit[orig_type]);
			return;
		}
	}

	if (!(flags & CONVERT_NUM_UNIT_NO)) {
		while (num > divisor) {
			num /= divisor;
			orig_type++;
		}
	}

	if (orig_type < UNIT_NONE || orig_type > UNIT_PETA)
		orig_type = UNIT_UNKNOWN;
	i = (uint64_t)num;
	/* Here we are checking to see if these numbers are the same,
	 * meaning the float has not floating point.  If we do have
	 * floating point print as a float.
	*/
	if ((double)i == num)
		snprintf(buf, buf_size, "%"PRIu64"%c", i, unit[orig_type]);
	else
		snprintf(buf, buf_size, "%.2f%c", num, unit[orig_type]);
}

extern void convert_num_unit(double num, char *buf, int buf_size,
			     int orig_type, uint32_t flags)
{
	convert_num_unit2(num, buf, buf_size, orig_type, 1024, flags);
}

extern int revert_num_unit(const char *buf)
{
	char *unit = "\0KMGTP\0";
	int i = 1, j = 0, number = 0;

	if (!buf)
		return -1;
	j = strlen(buf) - 1;
	while (unit[i]) {
		if (toupper((int)buf[j]) == unit[i])
			break;
		i++;
	}

	number = atoi(buf);
	if (unit[i])
		number *= (i*1024);

	return number;
}

#if _DEBUG

static void _print_data(char *data, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if ((i % 10 == 0) && (i != 0))
			printf("\n");
		printf("%2.2x ", ((int) data[i] & 0xff));
		if (i >= 200)
			break;
	}
	printf("\n\n");
}

#endif

/*
 * slurm_forward_data - forward arbitrary data to unix domain sockets on nodes
 * IN nodelist: nodes to forward data to
 * IN address: address of unix domain socket
 * IN len: length of data
 * IN data: real data
 * RET: error code
 */
extern int
slurm_forward_data(char *nodelist, char *address, uint32_t len, char *data)
{
	List ret_list = NULL;
	int temp_rc = 0, rc = 0;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));
	forward_data_msg_t req;

	slurm_msg_t_init(msg);

	debug("slurm_forward_data: nodelist=%s, address=%s, len=%u",
	      nodelist, address, len);
	req.address = address;
	req.len = len;
	req.data = data;

	msg->msg_type = REQUEST_FORWARD_DATA;
	msg->data = &req;

	if ((ret_list = slurm_send_recv_msgs(nodelist, msg, 0, false))) {
		while ((ret_data_info = list_pop(ret_list))) {
			temp_rc = slurm_get_return_code(ret_data_info->type,
							ret_data_info->data);
			if (temp_rc)
				rc = temp_rc;
		}
	} else {
		error("slurm_forward_data: no list was returned");
		rc = SLURM_ERROR;
	}

	slurm_free_msg(msg);
	return rc;
}

extern void slurm_setup_sockaddr(struct sockaddr_in *sin, uint16_t port)
{
	static uint32_t s_addr = NO_VAL;

	memset(sin, 0, sizeof(struct sockaddr_in));
	sin->sin_family = AF_SLURM;
	sin->sin_port = htons(port);

	if (s_addr == NO_VAL) {
		/* On systems with multiple interfaces we might not
		 * want to get just any address.  This is the case on
		 * a Cray system with RSIP.
		 */
		char *topology_params = slurm_get_topology_param();
		if (topology_params &&
		    slurm_strcasestr(topology_params, "NoInAddrAny")) {
			char host[MAXHOSTNAMELEN];

			if (!gethostname(host, MAXHOSTNAMELEN)) {
				slurm_set_addr_char(sin, port, host);
				s_addr = sin->sin_addr.s_addr;
			} else
				fatal("slurm_setup_sockaddr: "
				      "Can't get hostname or addr: %m");
		} else
			s_addr = htonl(INADDR_ANY);

		xfree(topology_params);
	}

	sin->sin_addr.s_addr = s_addr;
}

/* sock_bind_range()
 */
int
sock_bind_range(int s, uint16_t *range)
{
	uint32_t count;
	uint32_t min;
	uint32_t max;
	uint32_t port;
	uint32_t num;

	min = range[0];
	max = range[1];

	srand(getpid());
	num = max - min + 1;
	port = min + (random() % num);
	count = num;

	do {
		if (_is_port_ok(s, port))
			return port;

		if (port == max)
			port = min;
		else
			++port;
		--count;
	} while (count > 0);

	error("%s: ohmygosh all ports in range (%d, %d) exhausted",
	      __func__, min, max);

	return -1;
}

/* _is_port_ok()
 *
 * Check if we can bind() the socket s to port port.
 */
static bool
_is_port_ok(int s, uint16_t port)
{
	struct sockaddr_in sin;

	slurm_setup_sockaddr(&sin, port);

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		debug("%s: bind() failed port %d sock %d %m",
		      __func__, port, s);
		return false;
	}

	return true;
}

/* slurm_get_prolog_timeout
 * Get prolog/epilog timeout
 */
uint16_t slurm_get_prolog_timeout(void)
{
	uint16_t timeout = 0;
	slurm_ctl_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		timeout = conf->prolog_epilog_timeout;
		slurm_conf_unlock();
	}

	return timeout;
}
