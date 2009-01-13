/*****************************************************************************\
 *  slurm_protocol_api.c - high-level slurm communication functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/log.h"
#include "src/common/forward.h"
#include "src/slurmdbd/read_config.h"

/* EXTERNAL VARIABLES */

/* #DEFINES */
#define _DEBUG	0
#define MAX_SHUTDOWN_RETRY 5
#define MAX_RETRIES 3

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

/* slurm_api_set_conf_file
 *      set slurm configuration file to a non-default value
 * pathname IN - pathname of slurm configuration file to be used
 */
extern void  slurm_api_set_conf_file(char *pathname)
{
	slurm_conf_reinit(pathname);
	return;
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		complete_wait = conf->complete_wait;
		slurm_conf_unlock();
	}
	return complete_wait;
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		batch_start_timeout = conf->batch_start_timeout;
		slurm_conf_unlock();
	}
	return batch_start_timeout;
}

/* slurm_get_def_mem_per_task
 * RET DefMemPerTask value from slurm.conf
 */
uint32_t slurm_get_def_mem_per_task(void)
{
	uint32_t mem_per_task = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mem_per_task = conf->def_mem_per_task;
		slurm_conf_unlock();
	}
	return mem_per_task;
}

/* slurm_get_debug_flags
 * RET DebugFlags value from slurm.conf
 */
uint32_t slurm_get_debug_flags(void)
{
	uint32_t debug_flags = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		debug_flags = conf->debug_flags;
		slurm_conf_unlock();
	}
	return debug_flags;
}

/* slurm_get_max_mem_per_task
 * RET MaxMemPerTask value from slurm.conf
 */
uint32_t slurm_get_max_mem_per_task(void)
{
	uint32_t mem_per_task = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mem_per_task = conf->max_mem_per_task;
		slurm_conf_unlock();
	}
	return mem_per_task;
}

/* slurm_get_epilog_msg_time
 * RET EpilogMsgTime value from slurm.conf
 */
uint32_t slurm_get_epilog_msg_time(void)
{
	uint32_t epilog_msg_time = 0;
	slurm_ctl_conf_t *conf;

 	if(slurmdbd_conf) {
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
int inline slurm_get_env_timeout(void)
{
	int timeout = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		timeout = conf->get_env_timeout;
		slurm_conf_unlock();
	}
	return timeout;
}

/* slurm_get_mpi_default
 * get default mpi value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_default(void)
{
	char *mpi_default = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		mpi_default = xstrdup(conf->mpi_default);
		slurm_conf_unlock();
	}
	return mpi_default;
}

/* slurm_get_msg_timeout
 * get default message timeout value from slurmctld_conf object
 */
uint16_t slurm_get_msg_timeout(void)
{
	uint16_t msg_timeout = 0;
	slurm_ctl_conf_t *conf;

 	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {		
	} else {
		conf = slurm_conf_lock();
		priority_hl = conf->priority_decay_hl;
		slurm_conf_unlock();
	}

	return priority_hl;
}

/* slurm_get_priority_favor_small
 * returns weither or not we are favoring small jobs from slurmctld_conf object
 * RET bool - true if favor small, false else.
 */
bool slurm_get_priority_favor_small(void)
{
	bool factor = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {		
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

	if(slurmdbd_conf) {		
	} else {
		conf = slurm_conf_lock();
		age = conf->priority_max_age;
		slurm_conf_unlock();
	}

	return age;
}

/* slurm_get_priority_type
 * returns the priority type from slurmctld_conf object
 * RET char *    - priority type, MUST be xfreed by caller
 */
char *slurm_get_priority_type(void)
{
	char *priority_type = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {		
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

	if(slurmdbd_conf) {		
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

	if(slurmdbd_conf) {		
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_fs;
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

	if(slurmdbd_conf) {		
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

	if(slurmdbd_conf) {		
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

	if(slurmdbd_conf) {		
	} else {
		conf = slurm_conf_lock();
		factor = conf->priority_weight_qos;
		slurm_conf_unlock();
	}

	return factor;
}


/* slurm_get_private_data
 * get private data from slurmctld_conf object
 */
uint16_t slurm_get_private_data(void)
{
	uint16_t private_data = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		state_save_loc = xstrdup(conf->state_save_location);
		slurm_conf_unlock();
	}
	return state_save_loc;
}

/* slurm_get_auth_type
 * returns the authentication type from slurmctld_conf object
 * RET char *    - auth type, MUST be xfreed by caller
 */
char *slurm_get_auth_type(void)
{
	char *auth_type = NULL;
	slurm_ctl_conf_t *conf = NULL;

	if(slurmdbd_conf) {
		auth_type = xstrdup(slurmdbd_conf->auth_type);
	} else {
		conf = slurm_conf_lock();
		auth_type = xstrdup(conf->authtype);
		slurm_conf_unlock();
	}
	return auth_type;
}

/* slurm_get_checkpoint_type
 * returns the checkpoint_type from slurmctld_conf object
 * RET char *    - checkpoint type, MUST be xfreed by caller
 */
extern char *slurm_get_checkpoint_type(void)
{
	char *checkpoint_type = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		checkpoint_type = xstrdup(conf->checkpoint_type);
		slurm_conf_unlock();
	}
	return checkpoint_type;
}

/* slurm_get_cluster_name
 * returns the cluster name from slurmctld_conf object
 * RET char *    - cluster name,  MUST be xfreed by caller
 */
char *slurm_get_cluster_name(void)
{
	char *name = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		crypto_type = xstrdup(conf->crypto_type);
		slurm_conf_unlock();
	}
	return crypto_type;
}

/* slurm_get_propagate_prio_process
 * return the PropagatePrioProcess flag from slurmctld_conf object
 */
extern uint16_t slurm_get_propagate_prio_process(void)
{
	uint16_t propagate_prio = 0;
	slurm_ctl_conf_t *conf;

 	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		fast_val = conf->fast_schedule;
		slurm_conf_unlock();
	}
	return fast_val;
}

/* slurm_get_track_wckey
 * returns the value of track_wckey in slurmctld_conf object
 */
extern uint16_t slurm_get_track_wckey(void)
{
	uint16_t track_wckey = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		track_wckey = slurmdbd_conf->track_wckey;
	} else {
		conf = slurm_conf_lock();
		track_wckey = conf->track_wckey;
		slurm_conf_unlock();
	}
	return track_wckey;
}

/* slurm_set_tree_width
 * sets the value of tree_width in slurmctld_conf object
 * RET 0 or error code
 */
extern int slurm_set_tree_width(uint16_t tree_width)
{
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		tree_width = conf->tree_width;
		slurm_conf_unlock();
	}
	return tree_width;
}

/* slurm_set_auth_type
 * set the authentication type in slurmctld_conf object
 * used for security testing purposes
 * RET 0 or error code
 */
extern int slurm_set_auth_type(char *auth_type)
{
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

/* slurm_get_health_check_program
 * get health_check_program from slurmctld_conf object from slurmctld_conf object
 * RET char *   - health_check_program, MUST be xfreed by caller
 */
char *slurm_get_health_check_program(void)
{
	char *health_check_program = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		health_check_program = xstrdup(conf->health_check_program);
		slurm_conf_unlock();
	}
	return health_check_program;
}

/* slurm_get_accounting_storage_type
 * returns the accounting storage type from slurmctld_conf object
 * RET char *    - accounting storage type,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_type(void)
{
	char *accounting_type;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		accounting_type = xstrdup(slurmdbd_conf->storage_type);
	} else {
		conf = slurm_conf_lock();
		accounting_type = xstrdup(conf->accounting_storage_type);
		slurm_conf_unlock();
	}
	return accounting_type;
	
}

/* slurm_get_accounting_storage_user
 * returns the storage user from slurmctld_conf object
 * RET char *    - storage user,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_user(void)
{
	char *storage_user;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		storage_user = xstrdup(slurmdbd_conf->storage_user);
	} else {
		conf = slurm_conf_lock();
		storage_user = xstrdup(conf->accounting_storage_user);
		slurm_conf_unlock();
	}
	return storage_user;	
}

/* slurm_get_accounting_storage_host
 * returns the storage host from slurmctld_conf object
 * RET char *    - storage host,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_host(void)
{
	char *storage_host;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		storage_host = xstrdup(slurmdbd_conf->storage_host);
	} else {
		conf = slurm_conf_lock();
		storage_host = xstrdup(conf->accounting_storage_host);
		slurm_conf_unlock();
	}
	return storage_host;	
}

/* slurm_get_accounting_storage_loc
 * returns the storage location from slurmctld_conf object
 * RET char *    - storage location,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_loc(void)
{
	char *storage_loc;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		storage_loc = xstrdup(slurmdbd_conf->storage_loc);
	} else {
		conf = slurm_conf_lock();
		storage_loc = xstrdup(conf->accounting_storage_loc);
		slurm_conf_unlock();
	}
	return storage_loc;	
}

/* slurm_get_accounting_storage_enforce
 * returns what level to enforce associations at
 */
int slurm_get_accounting_storage_enforce(void)
{
	int enforce = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
		return 1;
	} else {
		conf = slurm_conf_lock();
		if(!strcasecmp(conf->accounting_storage_type, 
			      "accounting_storage/slurmdbd")
		   || strcasecmp(conf->accounting_storage_type,
				 "accounting_storage/mysql")) 
			enforce = 1;
		slurm_conf_unlock();
	}
	return enforce;	

}

/* slurm_set_accounting_storage_loc
 * IN: char *loc (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_loc(char *loc)
{
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

/* slurm_get_accounting_storage_pass
 * returns the storage password from slurmctld_conf object
 * RET char *    - storage password,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_pass(void)
{
	char *storage_pass;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		storage_pass = xstrdup(slurmdbd_conf->storage_pass);
	} else {
		conf = slurm_conf_lock();
		storage_pass = xstrdup(conf->accounting_storage_pass);
		slurm_conf_unlock();
	}
	return storage_pass;
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

	if(loaded_storage_pass)
		return storage_pass_ptr;

	if(slurmdbd_conf) {
		if(slurmdbd_conf->auth_info) {
			if(strlen(slurmdbd_conf->auth_info) > 
			   sizeof(storage_pass))
				fatal("AuthInfo is too long");
			strncpy(storage_pass, slurmdbd_conf->auth_info, 
				sizeof(storage_pass));
			storage_pass_ptr = storage_pass;
		}
	} else {
		conf = slurm_conf_lock();
		if(conf->accounting_storage_pass) {
			if(strlen(conf->accounting_storage_pass) > 
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

/* slurm_get_jobacct_gather_type
 * returns the job accounting type from the slurmctld_conf object
 * RET char *    - job accounting type,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_gather_type(void)
{
	char *jobacct_type = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		jobacct_type = xstrdup(conf->job_acct_gather_type);
		slurm_conf_unlock();
	}
	return jobacct_type;
}

/* slurm_get_jobacct_freq
 * returns the job accounting poll frequency from the slurmctld_conf object
 * RET int    - job accounting frequency
 */
uint16_t slurm_get_jobacct_gather_freq(void)
{
	uint16_t freq = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		freq = conf->job_acct_gather_freq;
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

/* slurm_get_proctrack_type
 * get ProctrackType from slurmctld_conf object
 * RET char *   - proctrack type, MUST be xfreed by caller
 */
char *slurm_get_proctrack_type(void)
{
	char *proctrack_type = NULL;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		slurmd_port = conf->slurmd_port;
		slurm_conf_unlock();
	}
	return slurmd_port;
}

/* slurm_get_slurm_user_id
 * returns slurmd uid from slurmctld_conf object
 * RET uint32_t	- slurm user id
 */
uint32_t slurm_get_slurm_user_id(void)
{
	uint32_t slurm_uid = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
		slurm_uid = slurmdbd_conf->slurm_user_id;
	} else {
		conf = slurm_conf_lock();
		slurm_uid = conf->slurm_user_id;
		slurm_conf_unlock();
	}
	return slurm_uid;
}

/* slurm_get_root_filter
 * RET uint16_t  - Value of SchedulerRootFilter */
extern uint16_t slurm_get_root_filter(void)
{
	uint16_t root_filter = 0;
	slurm_ctl_conf_t *conf;
 
 	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		root_filter = conf->schedrootfltr;
		slurm_conf_unlock();
	}
	return root_filter;
}
/* slurm_get_sched_port
 * RET uint16_t  - Value of SchedulerPort */
extern uint16_t slurm_get_sched_port(void)
{
	uint16_t port = 0;
	slurm_ctl_conf_t *conf;

 	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		select_type = xstrdup(conf->select_type);
		slurm_conf_unlock();
	}
	return select_type;
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
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

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		task_prolog = xstrdup(conf->task_prolog);
		slurm_conf_unlock();
	}
	return task_prolog;
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
uint16_t slurm_get_task_plugin_param(void)
{
	uint16_t task_plugin_param = 0;
	slurm_ctl_conf_t *conf;

	if(slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		task_plugin_param = conf->task_plugin_param;
		slurm_conf_unlock();
	}
	return task_plugin_param;
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

/* 
 *  Initialize a slurm server at port "port"
 * 
 * IN  port     - port to bind the msg server to
 * RET slurm_fd - file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine_port(uint16_t port)
{
	slurm_addr addr;

	slurm_set_addr_any(&addr, port);
	return _slurm_init_msg_engine(&addr);
}

/* 
 *  Same as above, but initialize using a slurm address "addr"
 *
 * IN  addr     - slurm_addr to bind the msg server to 
 * RET slurm_fd - file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine(slurm_addr *addr)
{
	return _slurm_init_msg_engine(addr);
}

/* 
 *  Close an established message engine.
 *    Returns SLURM_SUCCESS or SLURM_FAILURE.
 *
 * IN  fd  - an open file descriptor to close
 * RET int - the return code
 */
int slurm_shutdown_msg_engine(slurm_fd fd)
{
	int rc = _slurm_close(fd);
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
int slurm_shutdown_msg_conn(slurm_fd fd)
{
	return _slurm_close(fd);
}

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/* In the bsd socket implementation it creates a SOCK_STREAM socket  
 *	and calls connect on it a SOCK_DGRAM socket called with connect   
 *	is defined to only receive messages from the address/port pair  
 *	argument of the connect call slurm_address - for now it is  
 *	really just a sockaddr_in
 * IN slurm_address	- slurm_addr of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_open_msg_conn(slurm_addr * slurm_address)
{
	return _slurm_open_msg_conn(slurm_address);
}

/* Calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine. If the controller
 *	is very busy the connect may fail, so retry a couple of times.
 * OUT addr     - address of controller contacted
 * RET slurm_fd	- file descriptor of the connection created
 */
slurm_fd slurm_open_controller_conn(slurm_addr *addr)
{
	slurm_fd fd;
	slurm_ctl_conf_t *conf;
	int retry, have_backup = 0;

	if (slurm_api_set_default_config() < 0)
		return SLURM_FAILURE;

	for (retry=0; retry<4; retry++) {
		if (retry)
			sleep(1);

		addr = &proto_conf->primary_controller;
		fd = slurm_open_msg_conn(&proto_conf->primary_controller);
		if (fd >= 0)
			return fd;
		debug("Failed to contact primary controller: %m");

		if (retry == 0) {
			conf = slurm_conf_lock();
			if (conf->backup_controller)
				have_backup = 1;
			slurm_conf_unlock();
		}

		if (have_backup) {
			addr = &proto_conf->secondary_controller;
			fd = slurm_open_msg_conn(&proto_conf->
						 secondary_controller);
			if (fd >= 0)
				return fd;
			debug("Failed to contact secondary controller: %m");
		}
	}

	addr = NULL;
	slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);
}

/* calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine
 * RET slurm_fd - file descriptor of the connection created
 * IN dest      - controller to contact, primary or secondary
 */
slurm_fd slurm_open_controller_conn_spec(enum controller_id dest)
{
	slurm_addr *addr;
	slurm_fd rc;

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

/* gets the slurm_addr of the specified controller
 *	primary or secondary slurmctld message engine
 * IN dest      - controller to contact, primary or secondary
 * OUT addr     - slurm_addr to the specified controller
 */
void slurm_get_controller_addr_spec(enum controller_id dest, slurm_addr *addr)
{
	addr = (dest == PRIMARY_CONTROLLER) ? 
		  &proto_conf->primary_controller : 
		  &proto_conf->secondary_controller;
} 

/* In the bsd implmentation maps directly to a accept call 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_accept_msg_conn(slurm_fd open_fd,
			       slurm_addr * slurm_address)
{
	return _slurm_accept_msg_conn(open_fd, slurm_address);
}

/* In the bsd implmentation maps directly to a close call, to close 
 *	the socket that was accepted
 * IN open_fd		- an open file descriptor to close
 * RET int		- the return code
 */
int slurm_close_accepted_conn(slurm_fd open_fd)
{
	return _slurm_close_accepted_conn(open_fd);
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
int slurm_receive_msg(slurm_fd fd, slurm_msg_t *msg, int timeout)
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

	else if(timeout > (slurm_get_msg_timeout() * 10000)) {
		debug("You are receiving a message with very long "
		      "timeout of %d seconds", (timeout/1000));
	} else if(timeout < 1000) {
		error("You are receiving a message with a very short "
		      "timeout of %d msecs", timeout);
	} 
	

	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message. 
	 */
	if (_slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward, NULL);
		rc = errno;		
		goto total_return;
	}
	
#if	_DEBUG
	_print_data (buftemp, rc);
#endif
	buffer = create_buf(buf, buflen);

	if(unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}
	
	if (check_header_version(&header) < 0) {
		int uid = _unpack_msg_uid(buffer);
		error("Invalid Protocol Version %u from uid=%d", 
			header.version, uid);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	if(header.ret_cnt > 0) {
		error("we received more than one message back use "
		      "slurm_receive_msgs instead");
		header.ret_cnt = 0;
		list_destroy(header.ret_list);
		header.ret_list = NULL;
	}
	
	
	/* Forward message to other nodes */
	if(header.forward.cnt > 0) {
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
	if(header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, 
					  _global_auth_key() );
	} else
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, NULL );
	
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
	msg->msg_type = header.msg_type;
	msg->flags = header.flags;
	
	if ( (header.body_length > remaining_buf(buffer)) ||
	     (unpack_msg(msg, buffer) != SLURM_SUCCESS) ) {
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	
	msg->auth_cred = (void *)auth_cred;

	free_buf(buffer);
	rc = SLURM_SUCCESS;
	
total_return:
	destroy_forward(&header.forward);
	
	slurm_seterrno(rc);
	if(rc != SLURM_SUCCESS) {
		msg->auth_cred = (void *) NULL;
		error("slurm_receive_msg: %s", slurm_strerror(rc));
		rc = -1;
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
 * RET List	- List containing the responses of the childern (if any) we
 *		  forwarded the message to. List containing type
 *		  (ret_data_info_t).
 */
List slurm_receive_msgs(slurm_fd fd, int steps, int timeout)
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
	
	if(timeout <= 0) {
		/* convert secs to msec */
		timeout  = slurm_get_msg_timeout() * 1000; 
		orig_timeout = timeout;
	}
	if(steps) {
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
	if(orig_timeout >= (slurm_get_msg_timeout() * 10000)) {
		debug("slurm_receive_msgs: "
		      "You are sending a message with timeout's greater "
		      "than %d seconds, your's is %d seconds", 
		      (slurm_get_msg_timeout() * 10), 
		      (timeout/1000));
	} else if(orig_timeout < 1000) {
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
	if(_slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward, NULL);
		rc = errno;		
		goto total_return;
	}
	
#if	_DEBUG
	_print_data (buftemp, rc);
#endif
	buffer = create_buf(buf, buflen);

	if(unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}
	
	if(check_header_version(&header) < 0) {
		int uid = _unpack_msg_uid(buffer);
		error("Invalid Protocol Version %u from uid=%d",
			header.version, uid);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	if(header.ret_cnt > 0) {
		ret_list = list_create(destroy_data_info);
		while((ret_data_info = list_pop(header.ret_list)))
			list_push(ret_list, ret_data_info);
		header.ret_cnt = 0;
		list_destroy(header.ret_list);
		header.ret_list = NULL;
	}
	
	/* Forward message to other nodes */
	if(header.forward.cnt > 0) {
		error("We need to forward this to other nodes use "
		      "slurm_receive_msg_and_forward instead");
	}
	
	if((auth_cred = g_slurm_auth_unpack(buffer)) == NULL) {
		error( "authentication: %s ",
			g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	if(header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, 
					  _global_auth_key() );
	} else
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, NULL );
	
	if(rc != SLURM_SUCCESS) {
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
	msg.msg_type = header.msg_type;
	msg.flags = header.flags;
	
	if((header.body_length > remaining_buf(buffer)) ||
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
	
	if(rc != SLURM_SUCCESS) {
		if(ret_list) {
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			ret_data_info->err = rc;
			ret_data_info->type = RESPONSE_FORWARD_FAILED;
			ret_data_info->data = NULL;
			list_push(ret_list, ret_data_info);
		}
		error("slurm_receive_msgs: %s", slurm_strerror(rc));
	} else {
		if(!ret_list)
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
	uid = (int) g_slurm_auth_get_uid(auth_cred, NULL);
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
int slurm_receive_msg_and_forward(slurm_fd fd, slurm_addr *orig_addr, 
				  slurm_msg_t *msg, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	Buf buffer;

	xassert(fd >= 0);

	if(msg->forward.init != FORWARD_INIT)
		slurm_msg_t_init(msg);
	/* set msg connection fd to accepted fd. This allows 
	 *  possibility for slurmd_req () to close accepted connection
	 */
	msg->conn_fd = fd;
	/* this always is the connection */
	memcpy(&msg->address, orig_addr, sizeof(slurm_addr));

	/* where the connection originated from, this
	 * might change based on the header we receive */
	memcpy(&msg->orig_addr, orig_addr, sizeof(slurm_addr));

	msg->ret_list = list_create(destroy_data_info);

	if (timeout <= 0)
		/* convert secs to msec */
		timeout  = slurm_get_msg_timeout() * 1000; 
		
	if(timeout >= (slurm_get_msg_timeout() * 10000)) {
		debug("slurm_receive_msg_and_forward: "
		      "You are sending a message with timeout's greater "
		      "than %d seconds, your's is %d seconds", 
		      (slurm_get_msg_timeout() * 10), 
		      (timeout/1000));
	} else if(timeout < 1000) {
		debug("slurm_receive_msg_and_forward: "
		      "You are sending a message with a very short timeout of "
		      "%d milliseconds", timeout);
	} 	

	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message. 
	 */
	if (_slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward, NULL);
		rc = errno;		
		goto total_return;
	}
	
#if	_DEBUG
	_print_data (buftemp, rc);
#endif
	buffer = create_buf(buf, buflen);

	if(unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}
	
	if (check_header_version(&header) < 0) {
		int uid = _unpack_msg_uid(buffer);
		error("Invalid Protocol Version %u from uid=%d", 
			header.version, uid);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	if(header.ret_cnt > 0) {
		error("we received more than one message back use "
		      "slurm_receive_msgs instead");
		header.ret_cnt = 0;
		list_destroy(header.ret_list);
		header.ret_list = NULL;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	/* if(header.ret_cnt > 0) { */
/* 		while((ret_data_info = list_pop(header.ret_list))) */
/* 			list_push(msg->ret_list, ret_data_info); */
/* 		header.ret_cnt = 0; */
/* 		list_destroy(header.ret_list); */
/* 		header.ret_list = NULL; */
/* 	} */
	/* 
	 * header.orig_addr will be set to where the first message
	 * came from if this is a forward else we set the
	 * header.orig_addr to our addr just incase we need to send it off.
	 */
	if(header.orig_addr.sin_addr.s_addr != 0) {
		memcpy(&msg->orig_addr, &header.orig_addr, sizeof(slurm_addr));
	} else {
		memcpy(&header.orig_addr, orig_addr, sizeof(slurm_addr));
	}
	
	/* Forward message to other nodes */
	if(header.forward.cnt > 0) {
		debug("forwarding to %u", header.forward.cnt);
		msg->forward_struct = xmalloc(sizeof(forward_struct_t));
		msg->forward_struct->buf_len = remaining_buf(buffer);
		msg->forward_struct->buf = 
			xmalloc(sizeof(char) * msg->forward_struct->buf_len);
		memcpy(msg->forward_struct->buf, 
		       &buffer->head[buffer->processed], 
		       msg->forward_struct->buf_len);
		
		msg->forward_struct->ret_list = msg->ret_list;
		/* take out the amount of timeout from this hop */
		msg->forward_struct->timeout = header.forward.timeout;
		if(msg->forward_struct->timeout <= 0)
			msg->forward_struct->timeout = message_timeout;
		msg->forward_struct->fwd_cnt = header.forward.cnt;

		debug3("forwarding messages to %u nodes with timeout of %d", 
		       msg->forward_struct->fwd_cnt,
		       msg->forward_struct->timeout);
		
		if(forward_msg(msg->forward_struct, &header) == SLURM_ERROR) {
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
	if(header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, 
					  _global_auth_key() );
	} else
		rc = g_slurm_auth_verify( auth_cred, NULL, 2, NULL );
	
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
	msg->msg_type = header.msg_type;
	msg->flags = header.flags;
	
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
	if(rc != SLURM_SUCCESS) {
		msg->msg_type = RESPONSE_FORWARD_FAILED;
		msg->auth_cred = (void *) NULL;
		msg->data = NULL;
		error("slurm_receive_msg_and_forward: %s",
		      slurm_strerror(rc));
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
int slurm_send_node_msg(slurm_fd fd, slurm_msg_t * msg)
{
	header_t header;
	Buf      buffer;
	int      rc;
	void *   auth_cred;
	uint16_t auth_flags = SLURM_PROTOCOL_NO_FLAGS;
	
	/* 
	 * Initialize header with Auth credential and message type.
	 */
	if (msg->flags & SLURM_GLOBAL_AUTH_KEY) {
		auth_flags = SLURM_GLOBAL_AUTH_KEY;
		auth_cred = g_slurm_auth_create(NULL, 2, _global_auth_key());
	} else
		auth_cred = g_slurm_auth_create(NULL, 2, NULL);
	if (auth_cred == NULL) {
		error("authentication: %s",
		       g_slurm_auth_errstr(g_slurm_auth_errno(NULL)) );
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	if(msg->forward.init != FORWARD_INIT) {
		forward_init(&msg->forward, NULL);
		msg->ret_list = NULL;
	}
	forward_wait(msg);
	
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
	rc = _slurm_msg_sendto( fd, get_buf_data(buffer), 
				get_buf_offset(buffer),
				SLURM_PROTOCOL_NO_SEND_RECV_FLAGS );
	
	if (rc < 0) 
		error("slurm_msg_sendto: %m");
		
	free_buf(buffer);
	return rc;
}

/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_listen_stream
 * opens a stream server and listens on it
 * IN slurm_address	- slurm_addr to bind the server stream to
 * RET slurm_fd		- file descriptor of the stream created
 */
slurm_fd slurm_listen_stream(slurm_addr * slurm_address)
{
	return _slurm_listen_stream(slurm_address);
}

/* slurm_accept_stream
 * accepts a incomming stream connection on a stream server slurm_fd 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the accepted connection 
 */
slurm_fd slurm_accept_stream(slurm_fd open_fd, slurm_addr * slurm_address)
{
	return _slurm_accept_stream(open_fd, slurm_address);
}

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address     - slurm_addr of the connection destination
 * RET slurm_fd	 - file descriptor of the connection created
 * NOTE: Retry with various ports as needed if connection is refused
 */
slurm_fd slurm_open_stream(slurm_addr * slurm_address)
{
	return _slurm_open_stream(slurm_address, true);
}

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t slurm_write_stream(slurm_fd open_fd, char *buffer, size_t size)
{
	return _slurm_send_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   (slurm_get_msg_timeout() * 1000));
}
size_t slurm_write_stream_timeout(slurm_fd open_fd, char *buffer,
				  size_t size, int timeout)
{
	return _slurm_send_timeout(open_fd, buffer, size,
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
size_t slurm_read_stream(slurm_fd open_fd, char *buffer, size_t size)
{
	return _slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   (slurm_get_msg_timeout() * 1000));
}
size_t slurm_read_stream_timeout(slurm_fd open_fd, char *buffer,
				 size_t size, int timeout)
{
	return _slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   timeout);
}

/* slurm_get_stream_addr
 * esentially a encapsilated get_sockname  
 * IN open_fd		- file descriptor to retreive slurm_addr for
 * OUT address		- address that open_fd to bound to
 */
int slurm_get_stream_addr(slurm_fd open_fd, slurm_addr * address)
{
	return _slurm_get_stream_addr(open_fd, address);
}

/* slurm_close_stream
 * closes either a server or client stream file_descriptor
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int slurm_close_stream(slurm_fd open_fd)
{
	return _slurm_close_stream(open_fd);
}

/* make an open slurm connection blocking or non-blocking
 *	(i.e. wait or do not wait for i/o completion )
 * IN open_fd	- an open file descriptor to change the effect
 * RET int	- the return code
 */
int slurm_set_stream_non_blocking(slurm_fd open_fd)
{
	return _slurm_set_stream_non_blocking(open_fd);
}
int slurm_set_stream_blocking(slurm_fd open_fd)
{
	return _slurm_set_stream_blocking(open_fd);
}

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr_uint
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN ip_address	- ipv4 address in uint32 host order form
 */
void slurm_set_addr_uint(slurm_addr * slurm_address, uint16_t port,
			 uint32_t ip_address)
{
	_slurm_set_addr_uint(slurm_address, port, ip_address);
}

/* slurm_set_addr_any
 * initialized the slurm_address with the supplied port on INADDR_ANY
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 */
void slurm_set_addr_any(slurm_addr * slurm_address, uint16_t port)
{
	_slurm_set_addr_uint(slurm_address, port, SLURM_INADDR_ANY);
}

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and host name
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void slurm_set_addr(slurm_addr * slurm_address, uint16_t port, char *host)
{
	_slurm_set_addr_char(slurm_address, port, host);
}

/* reset_slurm_addr
 * resets the address field of a slurm_addr, port and family unchanged
 * OUT slurm_address	- slurm_addr to be reset in
 * IN new_address	- source of address to write into slurm_address
 */
void reset_slurm_addr(slurm_addr * slurm_address, slurm_addr new_address)
{
	_reset_slurm_addr(slurm_address, new_address);
}

/* slurm_set_addr_char
 * initializes the slurm_address with the supplied port and host
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void slurm_set_addr_char(slurm_addr * slurm_address, uint16_t port,
			 char *host)
{
	_slurm_set_addr_char(slurm_address, port, host);
}

/* slurm_get_addr 
 * given a slurm_address it returns its port and hostname
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT host		- hostname
 * IN buf_len		- length of hostname buffer
 */
void slurm_get_addr(slurm_addr * slurm_address, uint16_t * port,
		    char *host, unsigned int buf_len)
{
	_slurm_get_addr(slurm_address, port, host, buf_len);
}

/* slurm_get_ip_str 
 * given a slurm_address it returns its port and ip address string
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
void slurm_get_ip_str(slurm_addr * slurm_address, uint16_t * port,
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
int slurm_get_peer_addr(slurm_fd fd, slurm_addr * slurm_address)
{
	struct sockaddr name;
	socklen_t namelen = (socklen_t) sizeof(struct sockaddr);
	int rc;

	if ((rc = _slurm_getpeername((int) fd, &name, &namelen)))
		return rc;
	memcpy(slurm_address, &name, sizeof(slurm_addr));
	return 0;
}

/* slurm_print_slurm_addr
 * prints a slurm_addr into a buf
 * IN address		- slurm_addr to print
 * IN buf		- space for string representation of slurm_addr
 * IN n			- max number of bytes to write (including NUL)
 */
void slurm_print_slurm_addr(slurm_addr * address, char *buf, size_t n)
{
	_slurm_print_slurm_addr(address, buf, n);
}

/**********************************************************************\
 * slurm_addr pack routines
\**********************************************************************/

/* 
 *  Pack just the message with no header and send back the buffer.
 */
Buf slurm_pack_msg_no_header(slurm_msg_t * msg)
{
	Buf      buffer = NULL;

	buffer = init_buf(0);
	
	/*
	 * Pack message into buffer
	 */
	pack_msg(msg, buffer);
	
	return buffer;
}

/* slurm_pack_slurm_addr
 * packs a slurm_addr into a buffer to serialization transport
 * IN slurm_address	- slurm_addr to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr into
 */
void slurm_pack_slurm_addr(slurm_addr * slurm_address, Buf buffer)
{
	_slurm_pack_slurm_addr(slurm_address, buffer);
}

/* slurm_unpack_slurm_addr
 * unpacks a buffer into a slurm_addr after serialization transport
 * OUT slurm_address	- slurm_addr to unpack to
 * IN/OUT buffer	- buffer to unpack the slurm_addr from
 * returns		- SLURM error code
 */
int slurm_unpack_slurm_addr_no_alloc(slurm_addr * slurm_address,
				     Buf buffer)
{
	return _slurm_unpack_slurm_addr_no_alloc(slurm_address, buffer);
}

/* slurm_pack_slurm_addr_array
 * packs an array of slurm_addrs into a buffer
 * OUT slurm_address	- slurm_addr to pack
 * IN size_val  	- how many to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr from
 * returns		- SLURM error code
 */
void slurm_pack_slurm_addr_array(slurm_addr * slurm_address,
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
 * OUT slurm_address	- slurm_addr to unpack to
 * IN size_val  	- how many to unpack
 * IN/OUT buffer	- buffer to upack the slurm_addr from
 * returns		- SLURM error code
 */
int slurm_unpack_slurm_addr_array(slurm_addr ** slurm_address,
			    uint32_t * size_val, Buf buffer)
{
	int i = 0;
	uint32_t nl;

	*slurm_address = NULL;
	safe_unpack32(&nl, buffer);
	*size_val = ntohl(nl);
	*slurm_address = xmalloc((*size_val) * sizeof(slurm_addr));

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
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;
	
	if (msg->conn_fd < 0) {
		slurm_seterrno(ENOTCONN);
		return SLURM_ERROR;
	}
	rc_msg.return_code = rc;

	slurm_msg_t_init(&resp_msg);
	resp_msg.address  = msg->address;
	resp_msg.msg_type = RESPONSE_SLURM_RC;
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
_send_and_recv_msg(slurm_fd fd, slurm_msg_t *req, 
		   slurm_msg_t *resp, int timeout)
{
	int retry = 0;
	int rc = -1; 
	slurm_msg_t_init(resp);

	if(slurm_send_node_msg(fd, req) >= 0) {
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
 * RET List	- List containing the responses of the childern (if any) we
 *		  forwarded the message to. List containing type
 *		  (ret_data_info_t). 
 */
static List
_send_and_recv_msgs(slurm_fd fd, slurm_msg_t *req, int timeout)
{
	int retry = 0;
	List ret_list = NULL;
	int steps = 0;
	
	if (!req->forward.timeout) {
		if(!timeout)
			timeout = slurm_get_msg_timeout() * 1000;
		req->forward.timeout = timeout;
	}
	if(slurm_send_node_msg(fd, req) >= 0) {
		if(req->forward.cnt>0) {
			/* figure out where we are in the tree and set
			 * the timeout for to wait for our childern
			 * correctly
			 * (timeout+message_timeout sec per step)
			 * to let the child timeout */
			if (message_timeout < 0)
				message_timeout = slurm_get_msg_timeout() * 1000;
			steps = (req->forward.cnt+1)/slurm_get_tree_width();
			timeout = (message_timeout*steps);
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
	slurm_fd fd = -1;
	int rc = 0;
	time_t start_time = time(NULL);
	int retry = 1;
	slurm_ctl_conf_t *conf;
	bool backup_controller_flag;
	uint16_t slurmctld_timeout;
	slurm_addr ctrl_addr;

	/* Just in case the caller didn't initialize his slurm_msg_t, and
	 * since we KNOW that we are only sending to one node (the controller),
	 * we initialize some forwarding variables to disable forwarding.
	 */
	forward_init(&req->forward, NULL);
	req->ret_list = NULL;
	req->forward_struct = NULL;
	
	if ((fd = slurm_open_controller_conn(&ctrl_addr)) < 0) {
		rc = -1;
		goto cleanup;
	}
	
	conf = slurm_conf_lock();
	backup_controller_flag = conf->backup_controller ? true : false;
	slurmctld_timeout = conf->slurmctld_timeout;
	slurm_conf_unlock();

	while(retry) {
		/* If the backup controller is in the process of assuming 
		 * control, we sleep and retry later */
		retry = 0;
		rc = _send_and_recv_msg(fd, req, resp, 0);
		if (resp->auth_cred)
			g_slurm_auth_destroy(resp->auth_cred);
		else 
			rc = -1;

		if ((rc == 0)
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
	slurm_fd fd = -1;

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
 */
int slurm_send_only_controller_msg(slurm_msg_t *req)
{
	int      rc = SLURM_SUCCESS;
	int      retry = 0;
	slurm_fd fd = -1;
	slurm_addr ctrl_addr;

	/*
	 *  Open connection to SLURM controller:
	 */
	if ((fd = slurm_open_controller_conn(&ctrl_addr)) < 0) {
		rc = SLURM_SOCKET_ERROR;
		goto cleanup;
	}

	if((rc = slurm_send_node_msg(fd, req) < 0)) {
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
	slurm_fd fd = -1;
	
	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		return SLURM_SOCKET_ERROR;
	}

	if((rc = slurm_send_node_msg(fd, req) < 0)) {
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
 * RET List	  - List containing the responses of the childern
 *		    (if any) we forwarded the message to. List
 *		    containing type (ret_data_info_t).
 */
List slurm_send_recv_msgs(const char *nodelist, slurm_msg_t *msg, 
			  int timeout, bool quiet)
{
	List ret_list = NULL;
	List tmp_ret_list = NULL;
	slurm_fd fd = -1;
	char *name = NULL;
	char buf[8192];
	hostlist_t hl = NULL;
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr;

	if(!nodelist || !strlen(nodelist)) {
		error("slurm_send_recv_msgs: no nodelist given");
		return NULL;
	}
#ifdef HAVE_FRONT_END
	/* only send to the front end node */
	name = nodelist_nth_host(nodelist, 0);
	if (!name) {
		error("slurm_send_recv_msgs: "
		      "can't get the first name out of %s",
		      nodelist);
		return NULL;
	}
	hl = hostlist_create(name);
	free(name);
#else
/* 	info("total sending to %s",nodelist); */
	hl = hostlist_create(nodelist);
#endif
	while((name = hostlist_shift(hl))) {
		
		if(slurm_conf_get_addr(name, &msg->address) == SLURM_ERROR) {
			if (quiet) {
				debug("slurm_send_recv_msgs: can't find "
				      "address for host %s, check slurm.conf", 
				      name);
			} else {
				error("slurm_send_recv_msgs: can't find "
				      "address for host %s, check slurm.conf", 
				      name);
			}
			mark_as_failed_forward(&tmp_ret_list, name, 
					SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			free(name);
			continue;
		}
		
		if ((fd = slurm_open_msg_conn(&msg->address)) < 0) {
			if (quiet)
				debug("slurm_send_recv_msgs to %s: %m", name);
			else
				error("slurm_send_recv_msgs to %s: %m", name);
			mark_as_failed_forward(&tmp_ret_list, name, 
					SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			free(name);
			continue;
		}

		hostlist_ranged_string(hl, sizeof(buf), buf);
		forward_init(&msg->forward, NULL);
		msg->forward.nodelist = xstrdup(buf);
		msg->forward.timeout = timeout;
		msg->forward.cnt = hostlist_count(hl);
		if (msg->forward.nodelist[0]) {
			debug3("sending to %s along with to %s", 
			       name, msg->forward.nodelist);
		} else
			debug3("sending to %s", name);
		
		if(!(ret_list = _send_and_recv_msgs(fd, msg, timeout))) {
			xfree(msg->forward.nodelist);
			if (quiet) {
				debug("slurm_send_recv_msgs"
				      "(_send_and_recv_msgs) to %s: %m", 
				      name);
			} else {
				error("slurm_send_recv_msgs"
				      "(_send_and_recv_msgs) to %s: %m", 
				      name);
			}
			mark_as_failed_forward(&tmp_ret_list, name, errno);
			free(name);
			continue;
		} else {
			itr = list_iterator_create(ret_list);
			while((ret_data_info = list_next(itr))) 
				if(!ret_data_info->node_name) {
					ret_data_info->node_name =
						xstrdup(name);
				}
			list_iterator_destroy(itr);
		}
		xfree(msg->forward.nodelist);
		free(name);
		break;		
	}
	hostlist_destroy(hl);

	if(tmp_ret_list) {
		if(!ret_list)
			ret_list = tmp_ret_list;
		else {
			while((ret_data_info = list_pop(tmp_ret_list))) 
				list_push(ret_list, ret_data_info);
			list_destroy(tmp_ret_list);
		}
	} 
	return ret_list;
}

/*
 *  Send a message to msg->address
 *    Then return List containing type (ret_data_info_t). 
 * IN msg	  - a slurm_msg struct to be sent by the function
 * IN timeout	  - how long to wait in milliseconds
 * RET List	  - List containing the responses of the childern
 *		    (if any) we forwarded the message to. List
 *		    containing type (ret_types_t).
 */
List slurm_send_addr_recv_msgs(slurm_msg_t *msg, char *name, int timeout)
{
	List ret_list = NULL;
	List tmp_ret_list = NULL;
	slurm_fd fd = -1;
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr;

	if ((fd = slurm_open_msg_conn(&msg->address)) < 0) {
		mark_as_failed_forward(&ret_list, name, 
				       SLURM_COMMUNICATIONS_CONNECTION_ERROR);
		return ret_list;
	}

	/*just to make sure */
	forward_init(&msg->forward, NULL);
	msg->ret_list = NULL;
	msg->forward_struct = NULL;
	if(!(ret_list = _send_and_recv_msgs(fd, msg, timeout))) {
		mark_as_failed_forward(&tmp_ret_list, name, errno);
		return ret_list;
	} else {
		itr = list_iterator_create(ret_list);
		while((ret_data_info = list_next(itr))) 
			if(!ret_data_info->node_name) {
				ret_data_info->node_name = xstrdup(name);
			}
		list_iterator_destroy(itr);
	}
	return ret_list;
}



/*
 *  Open a connection to the "address" specified in the the slurm msg "req"
 *    Then read back an "rc" message returning the "return_code" specified
 *    in the response in the "rc" parameter.
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT rc	- return code from the sent message
 * IN timeout	- how long to wait in milliseconds
 * RET int either 0 for success or -1 for failure.
 */
int slurm_send_recv_rc_msg_only_one(slurm_msg_t *req, int *rc, int timeout)
{
	slurm_fd fd = -1;
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
		
	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		return -1;
	}
			
	if(!_send_and_recv_msg(fd, req, &resp, timeout)) {
		if(resp.auth_cred)
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

	if(!slurm_send_recv_controller_msg(req, &resp)) {
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
	if(total <= tree_width) {
		return span;
	} 
	
	while(left > 0) {
		for(i = 0; i < tree_width; i++) {
			if((tree_width-i) >= left) {
				if(span[i] == 0) {
					left = 0;
					break;
				} else {
					span[i] += left;
					left = 0;
					break;
				}
			} else if(left <= tree_width) {
				span[i] += left;
				left = 0;
				break;
			}
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
	if(msg->auth_cred)
		(void) g_slurm_auth_destroy(msg->auth_cred);

	if(msg->ret_list) {
		list_destroy(msg->ret_list);
		msg->ret_list = NULL;
	}

	xfree(msg);
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

extern void convert_num_unit(float num, char *buf, int buf_size, int orig_type)
{
	char *unit = "\0KMGP?";
	int i = (int)num % 512;

	if((i > 0 && num < 1024) || (int)num == 0) {
		snprintf(buf, buf_size, "%d%c", (int)num, unit[orig_type]);
		return;
	}
	
	while(num>1024) {
		num /= 1024;
		orig_type++;
	}
       
	if(orig_type < UNIT_NONE || orig_type > UNIT_PETA)
		orig_type = UNIT_UNKNOWN;
	i = (int)num;
	if(i == num)
		snprintf(buf, buf_size, "%d%c", i, unit[orig_type]);
	else
		snprintf(buf, buf_size, "%.2f%c", num, unit[orig_type]);
}

extern int revert_num_unit(const char *buf)
{
	char *unit = "\0KMGP\0";
	int i = 1, j = 0, number = 0;
	
	if(!buf)
		return -1;
	j = strlen(buf) - 1;
	while(unit[i]) {
		if(toupper((int)buf[j]) == unit[i]) 
			break;
		i++;
	}
	
	number = atoi(buf);
	if(unit[i]) 
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
 * vi: shiftwidth=8 tabstop=8 expandtab
 */
