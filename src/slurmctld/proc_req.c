/*****************************************************************************\
 *  proc_req.c - process incoming messages to slurmctld
 *****************************************************************************
 *  Copyright (C) 2010-2017 SchedMD LLC.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/cron.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_persist_conn.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "src/interfaces/acct_gather.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/ext_sensors.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/priority.h"
#include "src/interfaces/sched_plugin.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

static pthread_mutex_t rpc_mutex = PTHREAD_MUTEX_INITIALIZER;
#define RPC_TYPE_SIZE 100
static uint16_t rpc_type_id[RPC_TYPE_SIZE] = { 0 };
static uint32_t rpc_type_cnt[RPC_TYPE_SIZE] = { 0 };
static uint64_t rpc_type_time[RPC_TYPE_SIZE] = { 0 };
#define RPC_USER_SIZE 200
static uint32_t rpc_user_id[RPC_USER_SIZE] = { 0 };
static uint32_t rpc_user_cnt[RPC_USER_SIZE] = { 0 };
static uint64_t rpc_user_time[RPC_USER_SIZE] = { 0 };

static bool do_post_rpc_node_registration = false;

char *slurmd_config_files[] = {
	"slurm.conf", "acct_gather.conf", "cgroup.conf",
	"cli_filter.lua", "ext_sensors.conf", "gres.conf", "helpers.conf",
	"job_container.conf", "knl_cray.conf", "mpi.conf", "oci.conf",
	"plugstack.conf", "topology.conf", NULL
};

static char *client_config_files[] = {
	"slurm.conf", "cli_filter.lua", "plugstack.conf", "topology.conf", NULL
};


config_response_msg_t *config_for_slurmd = NULL;
static config_response_msg_t *config_for_clients = NULL;

static pthread_mutex_t throttle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t throttle_cond = PTHREAD_COND_INITIALIZER;

static void         _create_het_job_id_set(hostset_t jobid_hostset,
					    uint32_t het_job_offset,
					    char **het_job_id_set);
static void         _fill_ctld_conf(slurm_conf_t * build_ptr);
static void         _kill_job_on_msg_fail(uint32_t job_id);
static int          _is_prolog_finished(uint32_t job_id);
static int          _make_step_cred(step_record_t *step_rec,
				    slurm_cred_t **slurm_cred,
				    uint16_t protocol_version);
static int          _route_msg_to_origin(slurm_msg_t *msg, char *job_id_str,
					 uint32_t job_id);
static void         _throttle_fini(int *active_rpc_cnt);
static void         _throttle_start(int *active_rpc_cnt);

extern diag_stats_t slurmctld_diag_stats;

#ifndef NDEBUG
/*
 * Used alongside the testsuite to signal that the RPC should be processed
 * as an untrusted user, rather than the "real" account. (Which in a lot of
 * testing is likely SlurmUser, and thus allowed to bypass many security
 * checks.
 *
 * Implemented with a thread-local variable to apply only to the current
 * RPC handling thread. Set by SLURM_DROP_PRIV bit in the slurm_msg_t flags.
 */
static __thread bool drop_priv = false;
#endif

typedef struct {
	uid_t request_uid;
	uid_t uid;
	const char *id;
	list_t *step_list;
} find_job_by_container_id_args_t;

extern void record_rpc_stats(slurm_msg_t *msg, long delta)
{
	slurm_mutex_lock(&rpc_mutex);
	for (int i = 0; i < RPC_TYPE_SIZE; i++) {
		if (rpc_type_id[i] == 0)
			rpc_type_id[i] = msg->msg_type;
		else if (rpc_type_id[i] != msg->msg_type)
			continue;
		rpc_type_cnt[i]++;
		rpc_type_time[i] += delta;
		break;
	}
	for (int i = 0; i < RPC_USER_SIZE; i++) {
		if ((rpc_user_id[i] == 0) && (i != 0))
			rpc_user_id[i] = msg->auth_uid;
		else if (rpc_user_id[i] != msg->auth_uid)
			continue;
		rpc_user_cnt[i]++;
		rpc_user_time[i] += delta;
		break;
	}
	slurm_mutex_unlock(&rpc_mutex);
}

/* These functions prevent certain RPCs from keeping the slurmctld write locks
 * constantly set, which can prevent other RPCs and system functions from being
 * processed. For example, a steady stream of batch submissions can prevent
 * squeue from responding or jobs from being scheduled. */
static void _throttle_start(int *active_rpc_cnt)
{
	slurm_mutex_lock(&throttle_mutex);
	while (1) {
		if (*active_rpc_cnt == 0) {
			(*active_rpc_cnt)++;
			break;
		}
#if 1
		slurm_cond_wait(&throttle_cond, &throttle_mutex);
#else
		/* While an RPC is being throttled due to a running RPC of the
		 * same type, do not count that thread against the daemon's
		 * thread limit. In extreme environments, this logic can result
		 * in the slurmctld spawning so many pthreads that it exhausts
		 * system resources and fails. */
		server_thread_decr();
		slurm_cond_wait(&throttle_cond, &throttle_mutex);
		server_thread_incr();
#endif
	}
	slurm_mutex_unlock(&throttle_mutex);
	if (LOTS_OF_AGENTS)
		usleep(1000);
	else
		usleep(1);
}
static void _throttle_fini(int *active_rpc_cnt)
{
	slurm_mutex_lock(&throttle_mutex);
	(*active_rpc_cnt)--;
	slurm_cond_broadcast(&throttle_cond);
	slurm_mutex_unlock(&throttle_mutex);
}

/*
 * _fill_ctld_conf - make a copy of current slurm configuration
 *	this is done with locks set so the data can change at other times
 * OUT conf_ptr - place to copy configuration to
 */
static void _fill_ctld_conf(slurm_conf_t *conf_ptr)
{
	slurm_conf_t *conf = &slurm_conf;
	uint32_t next_job_id;
	int i;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	next_job_id   = get_next_job_id(true);

	memset(conf_ptr, 0, sizeof(*conf_ptr));

	conf_ptr->last_update         = time(NULL);
	conf_ptr->accounting_storage_enforce =
		conf->accounting_storage_enforce;
	conf_ptr->accounting_storage_host =
		xstrdup(conf->accounting_storage_host);
	conf_ptr->accounting_storage_ext_host =
		xstrdup(conf->accounting_storage_ext_host);
	conf_ptr->accounting_storage_backup_host =
		xstrdup(conf->accounting_storage_backup_host);
	conf_ptr->accounting_storage_params =
		xstrdup(conf->accounting_storage_params);
	conf_ptr->accounting_storage_port = conf->accounting_storage_port;
	conf_ptr->accounting_storage_tres =
		xstrdup(conf->accounting_storage_tres);
	conf_ptr->accounting_storage_type =
		xstrdup(conf->accounting_storage_type);
	conf_ptr->accounting_storage_user =
		xstrdup(conf->accounting_storage_user);

	conf_ptr->acct_gather_conf = acct_gather_conf_values();
	conf_ptr->acct_gather_energy_type =
		xstrdup(conf->acct_gather_energy_type);
	conf_ptr->acct_gather_filesystem_type =
		xstrdup(conf->acct_gather_filesystem_type);
	conf_ptr->acct_gather_interconnect_type =
		xstrdup(conf->acct_gather_interconnect_type);
	conf_ptr->acct_gather_profile_type =
		xstrdup(conf->acct_gather_profile_type);
	conf_ptr->acct_gather_node_freq = conf->acct_gather_node_freq;

	conf_ptr->authinfo            = xstrdup(conf->authinfo);
	conf_ptr->authtype            = xstrdup(conf->authtype);
	conf_ptr->authalttypes        = xstrdup(conf->authalttypes);
	conf_ptr->authalt_params      = xstrdup(conf->authalt_params);

	conf_ptr->batch_start_timeout = conf->batch_start_timeout;
	conf_ptr->boot_time           = slurmctld_config.boot_time;
	conf_ptr->bb_type             = xstrdup(conf->bb_type);
	conf_ptr->bcast_exclude       = xstrdup(conf->bcast_exclude);
	conf_ptr->bcast_parameters = xstrdup(conf->bcast_parameters);


	if (strstr(conf->job_acct_gather_type, "cgroup") ||
	    strstr(conf->proctrack_type, "cgroup") ||
	    strstr(conf->task_plugin, "cgroup"))
		conf_ptr->cgroup_conf = cgroup_get_conf_list();

	conf_ptr->cli_filter_plugins  = xstrdup(conf->cli_filter_plugins);
	conf_ptr->cluster_name        = xstrdup(conf->cluster_name);
	conf_ptr->comm_params         = xstrdup(conf->comm_params);
	conf_ptr->complete_wait       = conf->complete_wait;
	conf_ptr->conf_flags          = conf->conf_flags;
	conf_ptr->control_cnt         = conf->control_cnt;
	conf_ptr->control_addr = xcalloc(conf->control_cnt + 1, sizeof(char *));
	conf_ptr->control_machine = xcalloc(conf->control_cnt + 1,
					    sizeof(char *));
	for (i = 0; i < conf_ptr->control_cnt; i++) {
		conf_ptr->control_addr[i] = xstrdup(conf->control_addr[i]);
		conf_ptr->control_machine[i] =
			xstrdup(conf->control_machine[i]);
	}
	conf_ptr->core_spec_plugin    = xstrdup(conf->core_spec_plugin);
	conf_ptr->cpu_freq_def        = conf->cpu_freq_def;
	conf_ptr->cpu_freq_govs       = conf->cpu_freq_govs;
	conf_ptr->cred_type           = xstrdup(conf->cred_type);

	conf_ptr->def_mem_per_cpu     = conf->def_mem_per_cpu;
	conf_ptr->debug_flags         = conf->debug_flags;
	conf_ptr->dependency_params = xstrdup(conf->dependency_params);

	conf_ptr->eio_timeout         = conf->eio_timeout;
	conf_ptr->enforce_part_limits = conf->enforce_part_limits;
	conf_ptr->epilog              = xstrdup(conf->epilog);
	conf_ptr->epilog_msg_time     = conf->epilog_msg_time;
	conf_ptr->epilog_slurmctld    = xstrdup(conf->epilog_slurmctld);
	ext_sensors_g_get_config(&conf_ptr->ext_sensors_conf);
	conf_ptr->ext_sensors_type    = xstrdup(conf->ext_sensors_type);
	conf_ptr->ext_sensors_freq    = conf->ext_sensors_freq;

	conf_ptr->fed_params          = xstrdup(conf->fed_params);
	conf_ptr->first_job_id        = conf->first_job_id;
	conf_ptr->fs_dampening_factor = conf->fs_dampening_factor;

	conf_ptr->gres_plugins        = xstrdup(conf->gres_plugins);
	conf_ptr->group_time          = conf->group_time;
	conf_ptr->group_force         = conf->group_force;
	conf_ptr->gpu_freq_def        = xstrdup(conf->gpu_freq_def);

	conf_ptr->inactive_limit      = conf->inactive_limit;
	conf_ptr->interactive_step_opts = xstrdup(conf->interactive_step_opts);

	conf_ptr->hash_val            = conf->hash_val;
	conf_ptr->health_check_interval = conf->health_check_interval;
	conf_ptr->health_check_node_state = conf->health_check_node_state;
	conf_ptr->health_check_program = xstrdup(conf->health_check_program);

	conf_ptr->job_acct_gather_freq  = xstrdup(conf->job_acct_gather_freq);
	conf_ptr->job_acct_gather_type  = xstrdup(conf->job_acct_gather_type);
	conf_ptr->job_acct_gather_params= xstrdup(conf->job_acct_gather_params);
	conf_ptr->job_acct_oom_kill    = conf->job_acct_oom_kill;

	conf_ptr->job_comp_host       = xstrdup(conf->job_comp_host);
	conf_ptr->job_comp_loc        = xstrdup(conf->job_comp_loc);
	conf_ptr->job_comp_params = xstrdup(conf->job_comp_params);
	conf_ptr->job_comp_port       = conf->job_comp_port;
	conf_ptr->job_comp_type       = xstrdup(conf->job_comp_type);
	conf_ptr->job_comp_user       = xstrdup(conf->job_comp_user);
	conf_ptr->job_container_plugin = xstrdup(conf->job_container_plugin);

	conf_ptr->job_credential_private_key =
		xstrdup(conf->job_credential_private_key);
	conf_ptr->job_credential_public_certificate =
		xstrdup(conf->job_credential_public_certificate);
	conf_ptr->job_defaults_list   =
		job_defaults_copy(conf->job_defaults_list);
	conf_ptr->job_file_append     = conf->job_file_append;
	conf_ptr->job_requeue         = conf->job_requeue;
	conf_ptr->job_submit_plugins  = xstrdup(conf->job_submit_plugins);

	conf_ptr->get_env_timeout     = conf->get_env_timeout;

	conf_ptr->keepalive_time = conf->keepalive_time;
	conf_ptr->kill_wait           = conf->kill_wait;
	conf_ptr->kill_on_bad_exit    = conf->kill_on_bad_exit;

	conf_ptr->launch_params       = xstrdup(conf->launch_params);
	conf_ptr->licenses            = xstrdup(conf->licenses);
	conf_ptr->log_fmt             = conf->log_fmt;

	conf_ptr->mail_domain         = xstrdup(conf->mail_domain);
	conf_ptr->mail_prog           = xstrdup(conf->mail_prog);
	conf_ptr->max_array_sz        = conf->max_array_sz;
	conf_ptr->max_batch_requeue   = conf->max_batch_requeue;
	conf_ptr->max_dbd_msgs        = conf->max_dbd_msgs;
	conf_ptr->max_job_cnt         = conf->max_job_cnt;
	conf_ptr->max_job_id          = conf->max_job_id;
	conf_ptr->max_mem_per_cpu     = conf->max_mem_per_cpu;
	conf_ptr->max_node_cnt        = conf->max_node_cnt;
	conf_ptr->max_step_cnt        = conf->max_step_cnt;
	conf_ptr->max_tasks_per_node  = conf->max_tasks_per_node;
	conf_ptr->mcs_plugin          = xstrdup(conf->mcs_plugin);
	conf_ptr->mcs_plugin_params   = xstrdup(conf->mcs_plugin_params);
	conf_ptr->min_job_age         = conf->min_job_age;
	conf_ptr->mpi_conf = mpi_g_conf_get_printable();
	conf_ptr->mpi_default         = xstrdup(conf->mpi_default);
	conf_ptr->mpi_params          = xstrdup(conf->mpi_params);
	conf_ptr->msg_timeout         = conf->msg_timeout;

	conf_ptr->next_job_id         = next_job_id;
	conf_ptr->node_features_conf  = node_features_g_get_config();
	conf_ptr->node_features_plugins = xstrdup(conf->node_features_plugins);
	conf_ptr->node_prefix         = xstrdup(conf->node_prefix);

	conf_ptr->over_time_limit     = conf->over_time_limit;

	conf_ptr->plugindir           = xstrdup(conf->plugindir);
	conf_ptr->plugstack           = xstrdup(conf->plugstack);
	conf_ptr->power_parameters    = xstrdup(conf->power_parameters);
	conf_ptr->power_plugin        = xstrdup(conf->power_plugin);

	conf_ptr->preempt_mode        = conf->preempt_mode;
	conf_ptr->preempt_params = xstrdup(conf->preempt_params);
	conf_ptr->preempt_type        = xstrdup(conf->preempt_type);
	conf_ptr->preempt_exempt_time = conf->preempt_exempt_time;
	conf_ptr->prep_params         = xstrdup(conf->prep_params);
	conf_ptr->prep_plugins        = xstrdup(conf->prep_plugins);
	conf_ptr->priority_decay_hl   = conf->priority_decay_hl;
	conf_ptr->priority_calc_period = conf->priority_calc_period;
	conf_ptr->priority_favor_small= conf->priority_favor_small;
	conf_ptr->priority_flags      = conf->priority_flags;
	conf_ptr->priority_max_age    = conf->priority_max_age;
	conf_ptr->priority_params     = xstrdup(conf->priority_params);
	conf_ptr->priority_reset_period = conf->priority_reset_period;
	conf_ptr->priority_type       = xstrdup(conf->priority_type);
	conf_ptr->priority_weight_age = conf->priority_weight_age;
	conf_ptr->priority_weight_assoc = conf->priority_weight_assoc;
	conf_ptr->priority_weight_fs  = conf->priority_weight_fs;
	conf_ptr->priority_weight_js  = conf->priority_weight_js;
	conf_ptr->priority_weight_part= conf->priority_weight_part;
	conf_ptr->priority_weight_qos = conf->priority_weight_qos;
	conf_ptr->priority_weight_tres = xstrdup(conf->priority_weight_tres);

	conf_ptr->private_data        = conf->private_data;
	conf_ptr->proctrack_type      = xstrdup(conf->proctrack_type);
	conf_ptr->prolog              = xstrdup(conf->prolog);
	conf_ptr->prolog_epilog_timeout = conf->prolog_epilog_timeout;
	conf_ptr->prolog_slurmctld    = xstrdup(conf->prolog_slurmctld);
	conf_ptr->prolog_flags        = conf->prolog_flags;
	conf_ptr->propagate_prio_process = slurm_conf.propagate_prio_process;
	conf_ptr->propagate_rlimits   = xstrdup(conf->propagate_rlimits);
	conf_ptr->propagate_rlimits_except = xstrdup(conf->
						     propagate_rlimits_except);

	conf_ptr->reboot_program      = xstrdup(conf->reboot_program);
	conf_ptr->reconfig_flags      = conf->reconfig_flags;
	conf_ptr->requeue_exit        = xstrdup(conf->requeue_exit);
	conf_ptr->requeue_exit_hold   = xstrdup(conf->requeue_exit_hold);
	conf_ptr->resume_fail_program = xstrdup(conf->resume_fail_program);
	conf_ptr->resume_program      = xstrdup(conf->resume_program);
	conf_ptr->resume_rate         = conf->resume_rate;
	conf_ptr->resume_timeout      = conf->resume_timeout;
	conf_ptr->resv_epilog         = xstrdup(conf->resv_epilog);
	conf_ptr->resv_over_run       = conf->resv_over_run;
	conf_ptr->resv_prolog         = xstrdup(conf->resv_prolog);
	conf_ptr->ret2service         = conf->ret2service;
	conf_ptr->route_plugin        = xstrdup(conf->route_plugin);

	conf_ptr->sched_params        = xstrdup(conf->sched_params);
	conf_ptr->sched_logfile       = xstrdup(conf->sched_logfile);
	conf_ptr->sched_log_level     = conf->sched_log_level;
	conf_ptr->sched_time_slice    = conf->sched_time_slice;
	conf_ptr->schedtype           = xstrdup(conf->schedtype);
	conf_ptr->scron_params = xstrdup(conf->scron_params);
	conf_ptr->select_type         = xstrdup(conf->select_type);
	select_g_get_info_from_plugin(SELECT_CONFIG_INFO, NULL,
				      &conf_ptr->select_conf_key_pairs);
	conf_ptr->select_type_param   = conf->select_type_param;
	conf_ptr->site_factor_params  = xstrdup(conf->site_factor_params);
	conf_ptr->site_factor_plugin  = xstrdup(conf->site_factor_plugin);
	conf_ptr->slurm_user_id       = conf->slurm_user_id;
	conf_ptr->slurm_user_name     = xstrdup(conf->slurm_user_name);
	conf_ptr->slurmctld_addr      = xstrdup(conf->slurmctld_addr);
	conf_ptr->slurmctld_debug     = conf->slurmctld_debug;
	conf_ptr->slurmctld_logfile   = xstrdup(conf->slurmctld_logfile);
	conf_ptr->slurmctld_params    = xstrdup(conf->slurmctld_params);
	conf_ptr->slurmctld_pidfile   = xstrdup(conf->slurmctld_pidfile);
	conf_ptr->slurmctld_port      = conf->slurmctld_port;
	conf_ptr->slurmctld_port_count = conf->slurmctld_port_count;
	conf_ptr->slurmctld_primary_off_prog  =
		xstrdup(conf->slurmctld_primary_off_prog);
	conf_ptr->slurmctld_primary_on_prog  =
		xstrdup(conf->slurmctld_primary_on_prog);
	conf_ptr->slurmctld_syslog_debug = conf->slurmctld_syslog_debug;
	conf_ptr->slurmctld_timeout   = conf->slurmctld_timeout;
	conf_ptr->slurmd_debug        = conf->slurmd_debug;
	conf_ptr->slurmd_logfile      = xstrdup(conf->slurmd_logfile);
	conf_ptr->slurmd_params	      = xstrdup(conf->slurmd_params);
	conf_ptr->slurmd_pidfile      = xstrdup(conf->slurmd_pidfile);
	conf_ptr->slurmd_port         = conf->slurmd_port;
	conf_ptr->slurmd_spooldir     = xstrdup(conf->slurmd_spooldir);
	conf_ptr->slurmd_syslog_debug = conf->slurmd_syslog_debug;
	conf_ptr->slurmd_timeout      = conf->slurmd_timeout;
	conf_ptr->slurmd_user_id      = conf->slurmd_user_id;
	conf_ptr->slurmd_user_name    = xstrdup(conf->slurmd_user_name);
	conf_ptr->slurm_conf          = xstrdup(conf->slurm_conf);
	conf_ptr->srun_epilog         = xstrdup(conf->srun_epilog);

	conf_ptr->srun_port_range = xmalloc(2 * sizeof(uint16_t));
	if (conf->srun_port_range) {
		conf_ptr->srun_port_range[0] = conf->srun_port_range[0];
		conf_ptr->srun_port_range[1] = conf->srun_port_range[1];
	} else {
		conf_ptr->srun_port_range[0] = 0;
		conf_ptr->srun_port_range[1] = 0;
	}

	conf_ptr->srun_prolog         = xstrdup(conf->srun_prolog);
	conf_ptr->state_save_location = xstrdup(conf->state_save_location);
	conf_ptr->suspend_exc_nodes   = xstrdup(conf->suspend_exc_nodes);
	conf_ptr->suspend_exc_parts   = xstrdup(conf->suspend_exc_parts);
	conf_ptr->suspend_exc_states  = xstrdup(conf->suspend_exc_states);
	conf_ptr->suspend_program     = xstrdup(conf->suspend_program);
	conf_ptr->suspend_rate        = conf->suspend_rate;
	conf_ptr->suspend_time        = conf->suspend_time;
	conf_ptr->suspend_timeout     = conf->suspend_timeout;
	conf_ptr->switch_param        = xstrdup(conf->switch_param);
	conf_ptr->switch_type         = xstrdup(conf->switch_type);

	conf_ptr->task_epilog         = xstrdup(conf->task_epilog);
	conf_ptr->task_prolog         = xstrdup(conf->task_prolog);
	conf_ptr->task_plugin         = xstrdup(conf->task_plugin);
	conf_ptr->task_plugin_param   = conf->task_plugin_param;
	conf_ptr->tcp_timeout         = conf->tcp_timeout;
	conf_ptr->tmp_fs              = xstrdup(conf->tmp_fs);
	conf_ptr->topology_param      = xstrdup(conf->topology_param);
	conf_ptr->topology_plugin     = xstrdup(conf->topology_plugin);
	conf_ptr->tree_width          = conf->tree_width;

	conf_ptr->wait_time           = conf->wait_time;

	conf_ptr->unkillable_program  = xstrdup(conf->unkillable_program);
	conf_ptr->unkillable_timeout  = conf->unkillable_timeout;
	conf_ptr->version             = xstrdup(SLURM_VERSION_STRING);
	conf_ptr->vsize_factor        = conf->vsize_factor;
	conf_ptr->x11_params          = xstrdup(conf->x11_params);
}

/*
 * validate_slurm_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_slurm_user(uid_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurm_conf.slurm_user_id))
		return true;
	else
		return false;
}

/*
 * validate_super_user - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_SUPER_USER level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_super_user(uid_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurm_conf.slurm_user_id) ||
	    assoc_mgr_get_admin_level(acct_db_conn, uid) >=
	    SLURMDB_ADMIN_SUPER_USER)
		return true;
	else
		return false;
}

/*
 * validate_operator - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_OPERATOR level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_operator(uid_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurm_conf.slurm_user_id) ||
	    assoc_mgr_get_admin_level(acct_db_conn, uid) >=
	    SLURMDB_ADMIN_OPERATOR)
		return true;
	else
		return false;
}

extern bool validate_operator_user_rec(slurmdb_user_rec_t *user)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((user->uid == 0) ||
	    (user->uid == slurm_conf.slurm_user_id) ||
	    (user->admin_level >= SLURMDB_ADMIN_OPERATOR))
		return true;
	else
		return false;

}

static void _set_hostname(slurm_msg_t *msg, char **alloc_node)
{
	slurm_addr_t addr;

	xfree(*alloc_node);
	if ((*alloc_node = auth_g_get_host(msg->auth_cred)))
		debug3("%s: Using auth hostname for alloc_node: %s",
		       __func__, *alloc_node);
	else if (msg->conn) {
		/* use remote host name if persistent connection */
		*alloc_node = xstrdup(msg->conn->rem_host);
		debug3("%s: Using remote hostname for alloc_node: %s",
		       __func__, msg->conn->rem_host);
	} else if (msg->conn_fd >= 0 &&
		   !slurm_get_peer_addr(msg->conn_fd, &addr)) {
		/* use remote host IP */
		*alloc_node = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(&addr, *alloc_node,
				 INET6_ADDRSTRLEN);
		debug3("%s: Using requester IP for alloc_node: %s",
		       __func__, *alloc_node);
	} else {
		error("%s: Unable to determine alloc_node", __func__);
	}
}

static int _valid_id(char *caller, job_desc_msg_t *msg, uid_t uid, gid_t gid,
		     uint16_t protocol_version)
{
	/* TODO: remove this 2 versions after 23.02 release */
	if (protocol_version <= SLURM_22_05_PROTOCOL_VERSION) {
		/*
		 * Correct uid/gid with value NO_VAL set by
		 * slurm_init_job_desc_msg() in prior releases
		 */
		if (msg->user_id == NO_VAL)
			msg->user_id = uid;
		if (msg->group_id == NO_VAL)
			msg->group_id = gid;
	} else if ((msg->user_id == NO_VAL) || (msg->group_id == NO_VAL)) {
		/*
		 * Catch and reject NO_VAL in >= 23.02.
		 */
		error("%s: rejecting requested UID=NO_VAL or GID=NO_VAL as invalid",
		      caller);
		return ESLURM_USER_ID_MISSING;
	}

	/*
	 * If UID/GID not given use the authenticated values.
	 */
	if (msg->user_id == SLURM_AUTH_NOBODY)
		msg->user_id = uid;
	if (msg->group_id == SLURM_AUTH_NOBODY)
		msg->group_id = gid;

	if (validate_slurm_user(uid))
		return SLURM_SUCCESS;

	if (uid != msg->user_id) {
		error("%s: Requested UID=%u doesn't match user UID=%u.",
		      caller, msg->user_id, uid);
		return ESLURM_USER_ID_MISSING;
	}

	/* if GID not given, then use GID from auth */
	if (gid != msg->group_id) {
		error("%s: Requested GID=%u doesn't match user GID=%u.",
		      caller, msg->group_id, gid);
		return ESLURM_GROUP_ID_MISSING;
	}

	return SLURM_SUCCESS;
}

extern void configless_setup(void)
{
	if (!xstrcasestr(slurm_conf.slurmctld_params, "enable_configless"))
		return;

	config_for_slurmd = xmalloc(sizeof(*config_for_slurmd));
	config_for_clients = xmalloc(sizeof(*config_for_clients));

	config_for_slurmd->slurmd_spooldir =
		xstrdup(slurm_conf.slurmd_spooldir);

	load_config_response_list(config_for_slurmd, slurmd_config_files);
	load_config_response_list(config_for_clients, client_config_files);
}

/*
 * This trickery is to avoid contending any further on config_read.
 * Without this _slurm_rpc_config_request() would need to hold
 * conf_read until the response had finished sending (since we're using
 * a single shared copy of the configs).
 *
 * Instead, swap the pointers as quickly as possible. There is, as always,
 * a potential race here, but it's viewed as less problematic than
 * slowing down slurmctld with additional locking pressure.
 */
extern void configless_update(void)
{
	config_response_msg_t new, *old;

	if (!config_for_slurmd)
		return;

	/* handle slurmd first */
	memset(&new, 0, sizeof(new));
	old = xmalloc(sizeof(*old));

	new.slurmd_spooldir = xstrdup(slurm_conf.slurmd_spooldir);
	load_config_response_list(&new, slurmd_config_files);

	memcpy(old, config_for_slurmd, sizeof(*old));
	/* pseudo-atomic update of the pointers */
	memcpy(config_for_slurmd, &new, sizeof(*config_for_slurmd));
	slurm_free_config_response_msg(old);

	/* then the clients */
	memset(&new, 0, sizeof(new));
	old = xmalloc(sizeof(*old));
	load_config_response_list(&new, client_config_files);

	memcpy(old, config_for_clients, sizeof(*old));
	/* pseudo-atomic update of the pointers */
	memcpy(config_for_clients, &new, sizeof(*config_for_clients));
	slurm_free_config_response_msg(old);
}

extern void configless_clear(void)
{
	slurm_free_config_response_msg(config_for_slurmd);
	slurm_free_config_response_msg(config_for_clients);

	FREE_NULL_LIST(conf_includes_list);
}

/* _kill_job_on_msg_fail - The request to create a job record successed,
 *	but the reply message to srun failed. We kill the job to avoid
 *	leaving it orphaned */
static void _kill_job_on_msg_fail(uint32_t job_id)
{
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	error("Job allocate response msg send failure, killing JobId=%u",
	      job_id);
	lock_slurmctld(job_write_lock);
	job_complete(job_id, slurm_conf.slurm_user_id, false, false, SIGTERM);
	unlock_slurmctld(job_write_lock);
}

/* create a credential for a given job step, return error code */
static int _make_step_cred(step_record_t *step_ptr, slurm_cred_t **slurm_cred,
			   uint16_t protocol_version)
{
	slurm_cred_arg_t cred_arg;
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;

	xassert(job_resrcs_ptr && job_resrcs_ptr->cpus);

	setup_cred_arg(&cred_arg, job_ptr);

	memcpy(&cred_arg.step_id, &step_ptr->step_id, sizeof(cred_arg.step_id));
	if (job_resrcs_ptr->memory_allocated) {
		slurm_array64_to_value_reps(job_resrcs_ptr->memory_allocated,
					    job_resrcs_ptr->nhosts,
					    &cred_arg.job_mem_alloc,
					    &cred_arg.job_mem_alloc_rep_count,
					    &cred_arg.job_mem_alloc_size);
	}

	cred_arg.step_gres_list  = step_ptr->gres_list_alloc;

	cred_arg.step_core_bitmap = step_ptr->core_bitmap_job;
#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	cred_arg.step_hostlist   = job_ptr->batch_host;
#else
	cred_arg.step_hostlist   = step_ptr->step_layout->node_list;
#endif
	if (step_ptr->memory_allocated) {
		slurm_array64_to_value_reps(step_ptr->memory_allocated,
					    step_ptr->step_layout->node_cnt,
					    &cred_arg.step_mem_alloc,
					    &cred_arg.step_mem_alloc_rep_count,
					    &cred_arg.step_mem_alloc_size);
	}

	*slurm_cred = slurm_cred_create(slurmctld_config.cred_ctx, &cred_arg,
					true, protocol_version);

	xfree(cred_arg.job_mem_alloc);
	xfree(cred_arg.job_mem_alloc_rep_count);
	xfree(cred_arg.step_mem_alloc);
	xfree(cred_arg.step_mem_alloc_rep_count);
	if (*slurm_cred == NULL) {
		error("slurm_cred_create error");
		return ESLURM_INVALID_JOB_CREDENTIAL;
	}

	return SLURM_SUCCESS;
}

static int _het_job_cancel(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	time_t now = time(NULL);

	info("Cancelling aborted hetjob submit: %pJ", job_ptr);
	job_ptr->job_state	= JOB_CANCELLED;
	job_ptr->start_time	= now;
	job_ptr->end_time	= now;
	job_ptr->exit_code	= 1;
	job_completion_logger(job_ptr, false);
	fed_mgr_job_complete(job_ptr, 0, now);

	return 0;
}

/*
 * build_alloc_msg - Fill in resource_allocation_response_msg_t off job_record.
 * job_ptr IN - job_record to copy members off.
 * error_code IN - error code used for the response.
 * job_submit_user_msg IN - user message from job submit plugin.
 * RET resource_allocation_response_msg_t filled in.
 */
extern resource_allocation_response_msg_t *build_alloc_msg(
	job_record_t *job_ptr, int error_code, char *job_submit_user_msg)
{
	int i;
	resource_allocation_response_msg_t *alloc_msg =
		xmalloc(sizeof(resource_allocation_response_msg_t));

	/* send job_ID and node_name_ptr */
	if (job_ptr->job_resrcs && job_ptr->job_resrcs->cpu_array_cnt) {
		alloc_msg->num_cpu_groups = job_ptr->job_resrcs->cpu_array_cnt;
		alloc_msg->cpu_count_reps = xmalloc(sizeof(uint32_t) *
						    job_ptr->job_resrcs->
						    cpu_array_cnt);
		memcpy(alloc_msg->cpu_count_reps,
		       job_ptr->job_resrcs->cpu_array_reps,
		       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));
		alloc_msg->cpus_per_node  = xmalloc(sizeof(uint16_t) *
						    job_ptr->job_resrcs->
						    cpu_array_cnt);
		memcpy(alloc_msg->cpus_per_node,
		       job_ptr->job_resrcs->cpu_array_value,
		       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	}

	alloc_msg->error_code     = error_code;
	alloc_msg->job_submit_user_msg = xstrdup(job_submit_user_msg);
	alloc_msg->job_id         = job_ptr->job_id;
	alloc_msg->node_cnt       = job_ptr->node_cnt;
	alloc_msg->node_list      = xstrdup(job_ptr->nodes);
	alloc_msg->partition      = xstrdup(job_ptr->partition);
	alloc_msg->alias_list     = xstrdup(job_ptr->alias_list);
	alloc_msg->batch_host = xstrdup(job_ptr->batch_host);
	if (job_ptr->details) {
		if (job_ptr->bit_flags & JOB_MEM_SET) {
			alloc_msg->pn_min_memory =
				job_ptr->details->pn_min_memory;
		}
		alloc_msg->cpu_freq_min = job_ptr->details->cpu_freq_min;
		alloc_msg->cpu_freq_max = job_ptr->details->cpu_freq_max;
		alloc_msg->cpu_freq_gov = job_ptr->details->cpu_freq_gov;
		alloc_msg->ntasks_per_tres = job_ptr->details->ntasks_per_tres;
		if (job_ptr->details->mc_ptr) {
			alloc_msg->ntasks_per_board =
				job_ptr->details->mc_ptr->ntasks_per_board;
			alloc_msg->ntasks_per_core =
				job_ptr->details->mc_ptr->ntasks_per_core;
			alloc_msg->ntasks_per_socket =
				job_ptr->details->mc_ptr->ntasks_per_socket;
		}

		if (job_ptr->details->env_cnt) {
			alloc_msg->env_size = job_ptr->details->env_cnt;
			alloc_msg->environment =
				xcalloc(alloc_msg->env_size + 1,
					sizeof(char *));
			for (i = 0; i < alloc_msg->env_size; i++) {
				alloc_msg->environment[i] =
					xstrdup(job_ptr->details->env_sup[i]);
			}
		}
	} else {
		/* alloc_msg->pn_min_memory = 0; */
		alloc_msg->ntasks_per_board  = NO_VAL16;
		alloc_msg->ntasks_per_core   = NO_VAL16;
		alloc_msg->ntasks_per_tres    = NO_VAL16;
		alloc_msg->ntasks_per_socket = NO_VAL16;
	}
	if (job_ptr->account)
		alloc_msg->account = xstrdup(job_ptr->account);
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos;
		qos = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		alloc_msg->qos = xstrdup(qos->name);
	}
	if (job_ptr->resv_name)
		alloc_msg->resv_name = xstrdup(job_ptr->resv_name);

	set_remote_working_response(alloc_msg, job_ptr,
				    job_ptr->origin_cluster);

	alloc_msg->tres_per_node = xstrdup(job_ptr->tres_per_node);
	alloc_msg->uid = job_ptr->user_id;
	alloc_msg->user_name = uid_to_string_or_null(job_ptr->user_id);
	alloc_msg->gid = job_ptr->group_id;
	alloc_msg->group_name = gid_to_string_or_null(job_ptr->group_id);

	return alloc_msg;
}

static void _del_alloc_het_job_msg(void *x)
{
	resource_allocation_response_msg_t *alloc_msg;

	alloc_msg = (resource_allocation_response_msg_t *) x;
	/* NULL out working_cluster_rec since it's pointing to global memory */
	alloc_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(alloc_msg);
}

static bool _sched_backfill(void)
{
	static int backfill = -1;

	if (backfill == -1) {
		if (!xstrcmp(slurm_conf.schedtype, "sched/backfill"))
			backfill = 1;
		else
			backfill = 0;
	}

	if (backfill)
		return true;
	return false;
}

/*
 * If any job component has required nodes, those nodes must be excluded
 * from all other components to avoid scheduling deadlock
*/
static void _exclude_het_job_nodes(List job_req_list)
{
	job_desc_msg_t *job_desc_msg;
	ListIterator iter;
	int het_job_cnt, req_cnt = 0, i;
	char **req_nodes, *sep;

	het_job_cnt = list_count(job_req_list);
	req_nodes = xmalloc(sizeof(char *) * het_job_cnt);
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (!job_desc_msg->req_nodes || !job_desc_msg->req_nodes[0])
			continue;
		req_nodes[req_cnt++] = job_desc_msg->req_nodes;
	}
	if (req_cnt) {
		list_iterator_reset(iter);
		while ((job_desc_msg = list_next(iter))) {
			for (i = 0; i < req_cnt; i++) {
				if (req_nodes[i] == job_desc_msg->req_nodes)
					continue;     /* required by this job */
				if (job_desc_msg->exc_nodes &&
				    job_desc_msg->exc_nodes[0])
					sep = ",";
				else
					sep = "";
				xstrfmtcat(job_desc_msg->exc_nodes, "%s%s",
					   sep, req_nodes[i]);
			}
		}
	}
	list_iterator_destroy(iter);
	xfree(req_nodes);
}

/*
 * _create_het_job_id_set - Obtain the het_job_id_set
 * het_job_id_set OUT - allocated in the function and must be xfreed
 *                       be the caller.
 */
static void _create_het_job_id_set(hostset_t jobid_hostset,
				    uint32_t het_job_offset,
				    char **het_job_id_set)
{
	char *tmp_str = NULL;
	char *tmp_offset = NULL;

	if (!jobid_hostset)
		return;

	tmp_str = hostset_ranged_string_xmalloc(jobid_hostset);
	tmp_offset = tmp_str;
	if (tmp_str[0] == '[') {
		tmp_offset = strchr(tmp_str, ']');
		if (tmp_offset)
			tmp_offset[0] = '\0';
		tmp_offset = tmp_str + 1;
	}

	*het_job_id_set = xstrdup(tmp_offset);
	xfree(tmp_str);
}

/* _slurm_rpc_allocate_het_job: process RPC to allocate a hetjob resources */
static void _slurm_rpc_allocate_het_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, inx, het_job_cnt = -1;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg;
	list_t *job_req_list = msg->data;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	gid_t gid = auth_g_get_gid(msg->auth_cred);
	uint32_t job_uid = NO_VAL;
	job_record_t *job_ptr, *first_job_ptr = NULL;
	char *err_msg = NULL, **job_submit_user_msg = NULL;
	ListIterator iter;
	List submit_job_list = NULL;
	uint32_t het_job_id = 0, het_job_offset = 0;
	hostset_t jobid_hostset = NULL;
	char tmp_str[32];
	List resp = NULL;
	slurm_addr_t resp_addr;
	char resp_host[INET6_ADDRSTRLEN];
	char *het_job_id_set = NULL;

	START_TIMER;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		goto send_msg;
	}
	if (!_sched_backfill()) {
		info("REQUEST_HET_JOB_ALLOCATION from uid=%u rejected as sched/backfill is not configured",
		     msg->auth_uid);
		error_code = ESLURM_NOT_SUPPORTED;
		goto send_msg;
	}
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_HET_JOB_ALLOCATION from uid=%u with empty job list",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		goto send_msg;
	}
	if (slurm_get_peer_addr(msg->conn_fd, &resp_addr) == 0) {
		slurm_get_ip_str(&resp_addr, resp_host, sizeof(resp_host));
	} else {
		info("REQUEST_HET_JOB_ALLOCATION from uid=%u, can't get peer addr",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		goto send_msg;
	}

	sched_debug3("Processing RPC: REQUEST_HET_JOB_ALLOCATION from uid=%u",
		     msg->auth_uid);

	/*
	 * If any job component has required nodes, those nodes must be excluded
	 * from all other components to avoid scheduling deadlock
	 */
	_exclude_het_job_nodes(job_req_list);

	het_job_cnt = list_count(job_req_list);
	job_submit_user_msg = xmalloc(sizeof(char *) * het_job_cnt);
	submit_job_list = list_create(NULL);
	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	inx = 0;
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (job_uid == NO_VAL)
			job_uid = job_desc_msg->user_id;

		if ((error_code = _valid_id("REQUEST_HET_JOB_ALLOCATION",
					    job_desc_msg, msg->auth_uid, gid,
					    msg->protocol_version))) {
			break;
		}

		_set_hostname(msg, &job_desc_msg->alloc_node);

		if ((job_desc_msg->alloc_node == NULL) ||
		    (job_desc_msg->alloc_node[0] == '\0')) {
			error_code = ESLURM_INVALID_NODE_NAME;
			error("REQUEST_HET_JOB_ALLOCATION lacks alloc_node from uid=%u",
			      msg->auth_uid);
			break;
		}

		if (job_desc_msg->array_inx) {
			error_code = ESLURM_INVALID_ARRAY;
			break;
		}

		if (job_desc_msg->immediate) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			break;
		}

		/* Locks are for job_submit plugin use */
		job_desc_msg->het_job_offset = het_job_offset;
		error_code = validate_job_create_req(job_desc_msg, msg->auth_uid,
						     &job_submit_user_msg[inx]);
		if (error_code)
			break;

		dump_job_desc(job_desc_msg);

		job_ptr = NULL;
		if (!job_desc_msg->resp_host)
			job_desc_msg->resp_host = xstrdup(resp_host);
		if (het_job_offset) {
			/*
			 * Email notifications disable except for the
			 * hetjob leader
			 */
			job_desc_msg->mail_type = 0;
			xfree(job_desc_msg->mail_user);

			/* license request allowed only on leader */
			if (job_desc_msg->licenses) {
				xstrfmtcat(job_submit_user_msg[inx],
					   "%slicense request allowed only on leader job",
					   job_submit_user_msg[inx] ? "\n" : "");
				error("REQUEST_HET_JOB_ALLOCATION from uid=%u, license request on non-leader job",
				      msg->auth_uid);
				error_code = ESLURM_INVALID_LICENSES;
				break;
			}
		}
		job_desc_msg->het_job_offset = het_job_offset;
		error_code = job_allocate(job_desc_msg, false, false, NULL,
					  true, msg->auth_uid, false, &job_ptr,
					  &err_msg, msg->protocol_version);
		if (!job_ptr) {
			if (error_code == SLURM_SUCCESS)
				error_code = SLURM_ERROR;
			break;
		}
		if (error_code && (job_ptr->job_state == JOB_FAILED))
			break;
		error_code = SLURM_SUCCESS;	/* Non-fatal error */
		if (het_job_id == 0) {
			het_job_id = job_ptr->job_id;
			first_job_ptr = job_ptr;
		}
		snprintf(tmp_str, sizeof(tmp_str), "%u", job_ptr->job_id);
		if (jobid_hostset)
			hostset_insert(jobid_hostset, tmp_str);
		else
			jobid_hostset = hostset_create(tmp_str);
		job_ptr->het_job_id     = het_job_id;
		job_ptr->het_job_offset = het_job_offset++;
		list_append(submit_job_list, job_ptr);
		inx++;
	}
	list_iterator_destroy(iter);

	if ((error_code == 0) && (!first_job_ptr)) {
		error("%s: No error, but no het_job_id", __func__);
		error_code = SLURM_ERROR;
	}

	/* Validate limits on hetjob as a whole */
	if ((error_code == SLURM_SUCCESS) &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    !acct_policy_validate_het_job(submit_job_list)) {
		info("Hetjob %u exceeded association/QOS limit for user %u",
		     het_job_id, job_uid);
		error_code = ESLURM_ACCOUNTING_POLICY;
        }

	/* Set the het_job_id_set */
	_create_het_job_id_set(jobid_hostset, het_job_offset,
				&het_job_id_set);

	if (first_job_ptr)
		first_job_ptr->het_job_list = submit_job_list;
	iter = list_iterator_create(submit_job_list);
	while ((job_ptr = list_next(iter))) {
		job_ptr->het_job_id_set = xstrdup(het_job_id_set);
	}
	list_iterator_destroy(iter);
	xfree(het_job_id_set);

	if (error_code) {
		/* Cancel remaining job records */
		(void) list_for_each(submit_job_list, _het_job_cancel, NULL);
		if (!first_job_ptr)
			FREE_NULL_LIST(submit_job_list);
	} else {
		ListIterator iter;
		inx = 0;
		iter = list_iterator_create(submit_job_list);
		while ((job_ptr = list_next(iter))) {
			if (!resp)
				resp = list_create(_del_alloc_het_job_msg);
			list_append(resp,
				    build_alloc_msg(
					    job_ptr, error_code,
					    job_submit_user_msg[inx++]));
			log_flag(HETJOB, "Submit %pJ", job_ptr);
		}
		list_iterator_destroy(iter);
	}
	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);
	END_TIMER2(__func__);

	if (resp) {
		slurm_msg_t response_msg;
		response_init(&response_msg, msg, RESPONSE_HET_JOB_ALLOCATION,
			      resp);

		if (slurm_send_node_msg(msg->conn_fd, &response_msg) < 0)
			_kill_job_on_msg_fail(het_job_id);
		FREE_NULL_LIST(resp);
	} else {
		char *aggregate_user_msg;

send_msg:	info("%s: %s ", __func__, slurm_strerror(error_code));
		aggregate_user_msg = NULL;

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		for (inx = 0; inx < het_job_cnt; inx++) {
			char *line = NULL, *last = NULL;

			if (!job_submit_user_msg[inx])
				continue;

			/*
			 * Break apart any combined sentences and tag with index
			 */
			line = strtok_r(job_submit_user_msg[inx], "\n", &last);
			while (line) {
				xstrfmtcat(aggregate_user_msg, "%s%d: %s",
					   (aggregate_user_msg ? "\n" : ""),
					    inx, line);

				line = strtok_r(NULL, "\n", &last);
			}
		}
		if (aggregate_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = aggregate_user_msg;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	}
	xfree(err_msg);
	for (inx = 0; inx < het_job_cnt; inx++)
		xfree(job_submit_user_msg[inx]);
	xfree(job_submit_user_msg);

	if (jobid_hostset)
		hostset_destroy(jobid_hostset);

	schedule_job_save();	/* has own locks */
}

/* _slurm_rpc_allocate_resources:  process RPC to allocate resources for
 *	a job
 */
static void _slurm_rpc_allocate_resources(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg = msg->data;
	resource_allocation_response_msg_t *alloc_msg = NULL;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	gid_t gid = auth_g_get_gid(msg->auth_cred);
	int immediate = job_desc_msg->immediate;
	bool do_unlock = false;
	bool reject_job = false;
	job_record_t *job_ptr = NULL;
	slurm_addr_t resp_addr;
	char *err_msg = NULL, *job_submit_user_msg = NULL;

	START_TIMER;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}

	if ((error_code = _valid_id("REQUEST_RESOURCE_ALLOCATION", job_desc_msg,
				    msg->auth_uid, gid,
				    msg->protocol_version))) {
		reject_job = true;
		goto send_msg;
	}

	sched_debug3("Processing RPC: REQUEST_RESOURCE_ALLOCATION from uid=%u",
		     msg->auth_uid);

	_set_hostname(msg, &job_desc_msg->alloc_node);

	/* do RPC call */
	if ((job_desc_msg->alloc_node == NULL) ||
	    (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_RESOURCE_ALLOCATE lacks alloc_node from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid, &err_msg);
		unlock_slurmctld(job_read_lock);
	}

	/*
	 * In validate_job_create_req(), err_msg is currently only modified in
	 * the call to job_submit_g_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to fed_mgr_job_allocate and/or job_allocate, and we
	 * need the job submit plugin value to build the resource allocation
	 * response in the call to build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	if (error_code) {
		reject_job = true;
	} else if (!slurm_get_peer_addr(msg->conn_fd, &resp_addr)) {
		/* resp_host could already be set from a federated cluster */
		if (!job_desc_msg->resp_host) {
			job_desc_msg->resp_host = xmalloc(INET6_ADDRSTRLEN);
			slurm_get_ip_str(&resp_addr, job_desc_msg->resp_host,
					 INET6_ADDRSTRLEN);
		}
		dump_job_desc(job_desc_msg);
		do_unlock = true;
		_throttle_start(&active_rpc_cnt);

		lock_slurmctld(job_write_lock);
		if (fed_mgr_fed_rec) {
			uint32_t job_id;
			if (fed_mgr_job_allocate(msg, job_desc_msg, true,
						 &job_id, &error_code,
						 &err_msg)) {
				reject_job = true;
			} else if (!(job_ptr = find_job_record(job_id))) {
				error("%s: can't find fed job that was just created. this should never happen",
				      __func__);
				reject_job = true;
				error_code = SLURM_ERROR;
			}
		} else {
			job_desc_msg->het_job_offset = NO_VAL;
			error_code = job_allocate(job_desc_msg, immediate,
						  false, NULL, true,
						  msg->auth_uid, false,
						  &job_ptr, &err_msg,
						  msg->protocol_version);
			/* unlock after finished using the job structure
			 * data */

			/* return result */
			if (!job_ptr ||
			    (error_code && job_ptr->job_state == JOB_FAILED))
				reject_job = true;
		}
		END_TIMER2(__func__);
	} else {
		reject_job = true;
		if (errno)
			error_code = errno;
		else
			error_code = SLURM_ERROR;
	}

send_msg:

	if (!reject_job) {
		xassert(job_ptr);
		sched_info("%s %pJ NodeList=%s %s",
			   __func__, job_ptr, job_ptr->nodes, TIME_STR);

		alloc_msg = build_alloc_msg(job_ptr, error_code,
					    job_submit_user_msg);

		/*
		 * This check really isn't needed, but just doing it
		 * to be more complete.
		 */
		if (do_unlock) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}

		response_init(&response_msg, msg, RESPONSE_RESOURCE_ALLOCATION,
			      alloc_msg);

		if (slurm_send_node_msg(msg->conn_fd, &response_msg) < 0)
			_kill_job_on_msg_fail(job_ptr->job_id);

		schedule_job_save();	/* has own locks */
		schedule_node_save();	/* has own locks */

		if (!alloc_msg->node_cnt) /* didn't get an allocation */
			queue_job_scheduler();

		/* NULL out working_cluster_rec since it's pointing to global
		 * memory */
		alloc_msg->working_cluster_rec = NULL;
		slurm_free_resource_allocation_response_msg(alloc_msg);
	} else {	/* allocate error */
		if (do_unlock) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		info("%s: %s ", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_dump_conf - process RPC for Slurm configuration information */
static void _slurm_rpc_dump_conf(slurm_msg_t *msg)
{
	DEF_TIMERS;
	slurm_msg_t response_msg;
	last_update_msg_t *last_time_msg = msg->data;
	slurm_ctl_conf_info_msg_t config_tbl;
	/* Locks: Read config, job, partition, fed */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	lock_slurmctld(config_read_lock);

	/* check to see if configuration data has changed */
	if ((last_time_msg->last_update - 1) >= slurm_conf.last_update) {
		unlock_slurmctld(config_read_lock);
		debug2("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		_fill_ctld_conf(&config_tbl);
		unlock_slurmctld(config_read_lock);
		END_TIMER2(__func__);

		response_init(&response_msg, msg, RESPONSE_BUILD_INFO,
			      &config_tbl);

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		free_slurm_conf(&config_tbl, false);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	job_info_request_msg_t *job_info_request_msg = msg->data;
	/* Locks: Read config job part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);

	if ((job_info_request_msg->last_update - 1) >= last_job_update) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
		debug3("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		if (job_info_request_msg->job_ids) {
			pack_spec_jobs(&dump, &dump_size,
				       job_info_request_msg->job_ids,
				       job_info_request_msg->show_flags,
				       msg->auth_uid, NO_VAL,
				       msg->protocol_version);
		} else {
			pack_all_jobs(&dump, &dump_size,
				      job_info_request_msg->show_flags,
				      msg->auth_uid, NO_VAL,
				      msg->protocol_version);
		}
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
		END_TIMER2(__func__);
#if 0
		info("%s, size=%d %s", __func__, dump_size, TIME_STR);
#endif

		response_init(&response_msg, msg, RESPONSE_JOB_INFO, dump);
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_dump_jobs - process RPC for job state information */
static void _slurm_rpc_dump_jobs_user(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	job_user_id_msg_t *job_info_request_msg = msg->data;
	/* Locks: Read config job part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);
	pack_all_jobs(&dump, &dump_size, job_info_request_msg->show_flags,
		      msg->auth_uid, job_info_request_msg->user_id,
		      msg->protocol_version);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);
#if 0
	info("%s, size=%d %s", __func__, dump_size, TIME_STR);
#endif

	response_init(&response_msg, msg, RESPONSE_JOB_INFO, dump);
	response_msg.data_size = dump_size;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* _slurm_rpc_dump_job_single - process RPC for one job's state information */
static void _slurm_rpc_dump_job_single(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump = NULL;
	int dump_size, rc;
	slurm_msg_t response_msg;
	job_id_msg_t *job_id_msg = msg->data;
	/* Locks: Read config, job, and node info */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);
	rc = pack_one_job(&dump, &dump_size, job_id_msg->job_id,
			  job_id_msg->show_flags, msg->auth_uid,
			  msg->protocol_version);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);
#if 0
	info("%s, size=%d %s", __func__, dump_size, TIME_STR);
#endif

	/* init response_msg structure */
	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		response_init(&response_msg, msg, RESPONSE_JOB_INFO, dump);
		response_msg.data_size = dump_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
	xfree(dump);
}

static void _slurm_rpc_get_shares(slurm_msg_t *msg)
{
	DEF_TIMERS;
	shares_request_msg_t *req_msg = msg->data;
	shares_response_msg_t resp_msg;
	slurm_msg_t response_msg;

	START_TIMER;
	memset(&resp_msg, 0, sizeof(resp_msg));
	assoc_mgr_get_shares(acct_db_conn, msg->auth_uid, req_msg, &resp_msg);

	response_init(&response_msg, msg, RESPONSE_SHARE_INFO, &resp_msg);
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(resp_msg.assoc_shares_list);
	/* don't free the resp_msg.tres_names */
	END_TIMER2(__func__);
	debug2("%s %s", __func__, TIME_STR);
}

static void _slurm_rpc_get_priority_factors(slurm_msg_t *msg)
{
	DEF_TIMERS;
	/* req_msg can be removed 2 versions after 23.02 */
	priority_factors_request_msg_t *req_msg = msg->data;
	priority_factors_response_msg_t resp_msg;
	slurm_msg_t response_msg;
	/* Read lock on jobs, nodes, and partitions */
	slurmctld_lock_t job_read_lock = {
		.job = READ_LOCK,
		.node = READ_LOCK,
		.part = READ_LOCK,
	};
	assoc_mgr_lock_t qos_read_locks = {
		.qos = READ_LOCK,
	};


	START_TIMER;
	lock_slurmctld(job_read_lock);
	assoc_mgr_lock(&qos_read_locks);

	/* req_msg can be removed 2 versions after 23.02 */
	resp_msg.priority_factors_list = priority_g_get_priority_factors_list(
		req_msg, msg->auth_uid);
	response_init(&response_msg, msg, RESPONSE_PRIORITY_FACTORS, &resp_msg);
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	assoc_mgr_unlock(&qos_read_locks);
	unlock_slurmctld(job_read_lock);
	FREE_NULL_LIST(resp_msg.priority_factors_list);
	END_TIMER2(__func__);
	debug2("%s %s", __func__, TIME_STR);
}

/* _slurm_rpc_end_time - Process RPC for job end time */
static void _slurm_rpc_end_time(slurm_msg_t *msg)
{
	DEF_TIMERS;
	job_alloc_info_msg_t *time_req_msg = msg->data;
	srun_timeout_msg_t timeout_msg;
	slurm_msg_t response_msg;
	int rc;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	rc = job_end_time(time_req_msg, &timeout_msg);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		response_init(&response_msg, msg, SRUN_TIMEOUT, &timeout_msg);
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
	debug2("%s JobId=%u %s", __func__, time_req_msg->job_id, TIME_STR);
}

/* _slurm_rpc_get_fd - process RPC for federation state information */
static void _slurm_rpc_get_fed(slurm_msg_t *msg)
{
	DEF_TIMERS;
	slurm_msg_t response_msg;
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(fed_read_lock);

	response_init(&response_msg, msg, RESPONSE_FED_INFO, fed_mgr_fed_rec);

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(fed_read_lock);

	END_TIMER2(__func__);
	debug2("%s %s", __func__, TIME_STR);
}

/* _slurm_rpc_dump_front_end - process RPC for front_end state information */
static void _slurm_rpc_dump_front_end(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump = NULL;
	int dump_size = 0;
	slurm_msg_t response_msg;
	front_end_info_request_msg_t *front_end_req_msg = msg->data;
	/* Locks: Read config, read node */
	slurmctld_lock_t node_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(node_read_lock);

	if ((front_end_req_msg->last_update - 1) >= last_front_end_update) {
		unlock_slurmctld(node_read_lock);
		debug3("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_front_end(&dump, &dump_size, msg->auth_uid,
				   msg->protocol_version);
		unlock_slurmctld(node_read_lock);
		END_TIMER2(__func__);
		debug2("%s, size=%d %s", __func__, dump_size, TIME_STR);

		response_init(&response_msg, msg, RESPONSE_FRONT_END_INFO,
			      dump);
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_dump_nodes - dump RPC for node state information */
static void _slurm_rpc_dump_nodes(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	node_info_request_msg_t *node_req_msg = msg->data;
	/* Locks: Read config, write node (reset allocated CPU count in some
	 * select plugins), read part (for part_is_visible) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(msg->auth_uid))) {
		error("Security violation, REQUEST_NODE_INFO RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(node_write_lock);

	select_g_select_nodeinfo_set_all();

	if ((node_req_msg->last_update - 1) >= last_node_update) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(node_write_lock);
		debug3("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_node(&dump, &dump_size, node_req_msg->show_flags,
			      msg->auth_uid, msg->protocol_version);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);
#if 0
		info("%s, size=%d %s", __func__, dump_size, TIME_STR);
#endif

		response_init(&response_msg, msg, RESPONSE_NODE_INFO, dump);
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_dump_node_single - done RPC state information for one node */
static void _slurm_rpc_dump_node_single(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	node_info_single_msg_t *node_req_msg = msg->data;
	/* Locks: Read config, read node, read part (for part_is_visible) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (!validate_operator(msg->auth_uid))) {
		error("Security violation, REQUEST_NODE_INFO_SINGLE RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	lock_slurmctld(node_write_lock);

#if 0
	/* This function updates each node's alloc_cpus count and too slow for
	 * our use here. Node write lock is needed if this function is used */
	select_g_select_nodeinfo_set_all();
#endif
	pack_one_node(&dump, &dump_size, node_req_msg->show_flags,
		      msg->auth_uid, node_req_msg->node_name,
		      msg->protocol_version);
	unlock_slurmctld(node_write_lock);
	END_TIMER2(__func__);
#if 0
	info("%s, name=%s size=%d %s",
	     __func__, node_req_msg->node_name, dump_size, TIME_STR);
#endif

	response_init(&response_msg, msg, RESPONSE_NODE_INFO, dump);
	response_msg.data_size = dump_size;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* _slurm_rpc_dump_partitions - process RPC for partition state information */
static void _slurm_rpc_dump_partitions(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	part_info_request_msg_t *part_req_msg = msg->data;

	/* Locks: Read configuration and partition */
	slurmctld_lock_t part_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if ((slurm_conf.private_data & PRIVATE_DATA_PARTITIONS) &&
	    !validate_operator(msg->auth_uid)) {
		debug2("Security violation, PARTITION_INFO RPC from uid=%u",
		       msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(part_read_lock);

	if ((part_req_msg->last_update - 1) >= last_part_update) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(part_read_lock);
		debug2("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		pack_all_part(&dump, &dump_size, part_req_msg->show_flags,
			      msg->auth_uid, msg->protocol_version);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(part_read_lock);
		END_TIMER2(__func__);
		debug2("%s, size=%d %s", __func__, dump_size, TIME_STR);

		response_init(&response_msg, msg, RESPONSE_PARTITION_INFO,
			      dump);
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

/* _slurm_rpc_epilog_complete - process RPC noting the completion of
 * the epilog denoting the completion of a job it its entirety */
static void _slurm_rpc_epilog_complete(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	static time_t config_update = 0;
	static bool defer_sched = false;
	DEF_TIMERS;
	/* Locks: Read configuration, write job, write node */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	epilog_complete_msg_t *epilog_msg = msg->data;
	job_record_t *job_ptr;
	bool run_scheduler = false;

	START_TIMER;
	if (!validate_slurm_user(msg->auth_uid)) {
		error("Security violation, EPILOG_COMPLETE RPC from uid=%u",
		      msg->auth_uid);
		return;
	}

	/* Only throttle on non-composite messages, the lock should
	 * already be set earlier. */
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		if (config_update != slurm_conf.last_update) {
			defer_sched = (xstrcasestr(slurm_conf.sched_params,
						   "defer"));
			config_update = slurm_conf.last_update;
		}

		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	log_flag(ROUTE, "%s: node_name = %s, JobId=%u",
		 __func__, epilog_msg->node_name, epilog_msg->job_id);

	if (job_epilog_complete(epilog_msg->job_id, epilog_msg->node_name,
				epilog_msg->return_code))
		run_scheduler = true;

	job_ptr = find_job_record(epilog_msg->job_id);

	if (epilog_msg->return_code)
		error("%s: epilog error %pJ Node=%s Err=%s %s",
		      __func__, job_ptr, epilog_msg->node_name,
		      slurm_strerror(epilog_msg->return_code), TIME_STR);
	else
		debug2("%s: %pJ Node=%s %s",
		       __func__, job_ptr, epilog_msg->node_name, TIME_STR);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	END_TIMER2(__func__);

	/* Functions below provide their own locking */
	if (!(msg->flags & CTLD_QUEUE_PROCESSING) && run_scheduler) {
		/*
		 * In defer mode, avoid triggering the scheduler logic
		 * for every epilog complete message.
		 * As one epilog message is sent from every node of each
		 * job at termination, the number of simultaneous schedule
		 * calls can be very high for large machine or large number
		 * of managed jobs.
		 */
		if (!LOTS_OF_AGENTS && !defer_sched)
			schedule(false);	/* Has own locking */
		else
			queue_job_scheduler();
		schedule_node_save();		/* Has own locking */
		schedule_job_save();		/* Has own locking */
	}

	/* NOTE: RPC has no response */
}

/* _slurm_rpc_job_step_kill - process RPC to cancel an entire job or
 * an individual job step */
static void _slurm_rpc_job_step_kill(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	job_step_kill_msg_t *job_step_kill_msg = msg->data;

	log_flag(STEPS, "Processing RPC details: REQUEST_CANCEL_JOB_STEP %ps",
		 &job_step_kill_msg->step_id);
	_throttle_start(&active_rpc_cnt);

	error_code = kill_job_step(job_step_kill_msg, msg->auth_uid);

	_throttle_fini(&active_rpc_cnt);

	slurm_send_rc_msg(msg, error_code);
}

/* _slurm_rpc_complete_job_allocation - process RPC to note the
 *	completion of a job allocation */
static void _slurm_rpc_complete_job_allocation(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	complete_job_allocation_msg_t *comp_msg = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	job_record_t *job_ptr;

	/* init */
	START_TIMER;
	debug3("Processing RPC details: REQUEST_COMPLETE_JOB_ALLOCATION for JobId=%u rc=%d",
	       comp_msg->job_id, comp_msg->job_rc);

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(comp_msg->job_id);
	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	/* Mark job and/or job step complete */
	error_code = job_complete(comp_msg->job_id, msg->auth_uid,
				  false, false, comp_msg->job_rc);
	if (error_code) {
		if (error_code == ESLURM_INVALID_JOB_ID) {
			info("%s: JobId=%d error %s",
			     __func__, comp_msg->job_id,
			     slurm_strerror(error_code));
		} else {
			info("%s: %pJ error %s",
			     __func__, job_ptr, slurm_strerror(error_code));
		}
	} else {
		debug2("%s: %pJ %s", __func__, job_ptr, TIME_STR);
	}

	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);
	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurmctld_diag_stats.jobs_completed++;
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		(void) schedule_job_save();	/* Has own locking */
		(void) schedule_node_save();	/* Has own locking */
	}

	log_flag(TRACE_JOBS, "%s: return %pJ", __func__, job_ptr);
}

/* _slurm_rpc_complete_prolog - process RPC to note the
 *	completion of a prolog */
static void _slurm_rpc_complete_prolog(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	complete_prolog_msg_t *comp_msg = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* init */
	START_TIMER;
	debug3("Processing RPC details: REQUEST_COMPLETE_PROLOG from JobId=%u",
	       comp_msg->job_id);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_write_lock);
	error_code = prolog_complete(comp_msg->job_id, comp_msg->prolog_rc,
				     comp_msg->node_name);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_write_lock);

	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		info("%s JobId=%u: %s ",
		     __func__, comp_msg->job_id, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s JobId=%u %s", __func__, comp_msg->job_id, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/* _slurm_rpc_complete_batch - process RPC from slurmstepd to note the
 *	completion of a batch script */
static void _slurm_rpc_complete_batch_script(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS, i;
	DEF_TIMERS;
	complete_batch_script_msg_t *comp_msg = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	bool job_requeue = false;
	bool dump_job = false, dump_node = false;
	job_record_t *job_ptr = NULL;
	char *msg_title = "node(s)";
	char *nodes = comp_msg->node_name;

	/* init */
	START_TIMER;
	debug3("Processing RPC details: REQUEST_COMPLETE_BATCH_SCRIPT for JobId=%u",
	       comp_msg->job_id);

	if (!validate_slurm_user(msg->auth_uid)) {
		error("A non superuser %u tried to complete batch JobId=%u",
		      msg->auth_uid, comp_msg->job_id);
		/* Only the slurmstepd can complete a batch script */
		END_TIMER2(__func__);
		return;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	job_ptr = find_job_record(comp_msg->job_id);

	if (job_ptr && job_ptr->batch_host && comp_msg->node_name &&
	    xstrcmp(job_ptr->batch_host, comp_msg->node_name)) {
		/* This can be the result of the slurmd on the batch_host
		 * failing, but the slurmstepd continuing to run. Then the
		 * batch job is requeued and started on a different node.
		 * The end result is one batch complete RPC from each node. */
		error("Batch completion for JobId=%u sent from wrong node (%s rather than %s). Was the job requeued due to node failure?",
		      comp_msg->job_id,
		      comp_msg->node_name, job_ptr->batch_host);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	/*
	 * Send batch step info to accounting, only if the job is
	 * still completing.
	 *
	 * When a job is requeued because of node failure, and there is no
	 * epilog, both EPILOG_COMPLETE and COMPLETE_BATCH_SCRIPT_COMPLETE
	 * messages are sent at the same time and receieved on different
	 * threads. EPILOG_COMPLETE will grab a new db_index for the job. So if
	 * COMPLETE_BATCH_SCRIPT happens after EPILOG_COMPLETE, then adding the
	 * batch step would happen on the new db instance -- which is incorrect.
	 * Rather than try to ensure that COMPLETE_BATCH_SCRIPT happens after
	 * EPILOG_COMPLETE, just throw away the batch step for node failures.
	 *
	 * NOTE: Do not use IS_JOB_PENDING since that doesn't take
	 * into account the COMPLETING FLAG which is valid, but not
	 * always set yet when the step exits normally.
	 */
	if (slurm_with_slurmdbd() && job_ptr &&
	    (job_ptr->job_state != JOB_PENDING)) {
		/* This logic was taken from _slurm_rpc_step_complete() */
		slurm_step_id_t step_id = { .job_id = job_ptr->job_id,
					    .step_id = SLURM_BATCH_SCRIPT,
					    .step_het_comp = NO_VAL };
		step_record_t *step_ptr = find_step_record(job_ptr, &step_id);
		if (!step_ptr) {
			/* Ignore duplicate or late batch complete RPCs */
			debug("%s: Ignoring late or duplicate REQUEST_COMPLETE_BATCH_SCRIPT received for job %pJ",
			      __func__, job_ptr);
			if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
				unlock_slurmctld(job_write_lock);
				_throttle_fini(&active_rpc_cnt);
			}
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			return;
		} else if (step_ptr->step_id.step_id != SLURM_BATCH_SCRIPT) {
			error("%s: %pJ Didn't find batch step, found step %u. This should never happen.",
			      __func__, job_ptr, step_ptr->step_id.step_id);
		} else {
			step_ptr->exit_code = comp_msg->job_rc;
			jobacctinfo_destroy(step_ptr->jobacct);
			step_ptr->jobacct = comp_msg->jobacct;
			comp_msg->jobacct = NULL;
			step_ptr->state |= JOB_COMPLETING;
			jobacct_storage_g_step_complete(acct_db_conn, step_ptr);
			delete_step_record(job_ptr, step_ptr);
		}
	}

#ifdef HAVE_FRONT_END
	if (job_ptr && job_ptr->front_end_ptr)
		nodes = job_ptr->front_end_ptr->name;
	msg_title = "front_end";
#endif

	/* do RPC call */
	/* First set node DOWN if fatal error */
	if ((comp_msg->slurm_rc == ESLURMD_JOB_NOTRUNNING) ||
	    (comp_msg->slurm_rc == ESLURM_ALREADY_DONE) ||
	    (comp_msg->slurm_rc == ESLURMD_CREDENTIAL_REVOKED)) {
		/* race condition on job termination, not a real error */
		info("slurmd error running JobId=%u from %s=%s: %s",
		     comp_msg->job_id,
		     msg_title, nodes,
		     slurm_strerror(comp_msg->slurm_rc));
		comp_msg->slurm_rc = SLURM_SUCCESS;
	} else if ((comp_msg->slurm_rc == SLURM_COMMUNICATIONS_SEND_ERROR) ||
		   (comp_msg->slurm_rc == ESLURM_USER_ID_MISSING) ||
		   (comp_msg->slurm_rc == ESLURMD_INVALID_ACCT_FREQ)) {
		/* Handle non-fatal errors here. All others drain the node. */
		error("Slurmd error running JobId=%u on %s=%s: %s",
		      comp_msg->job_id, msg_title, nodes,
		      slurm_strerror(comp_msg->slurm_rc));
	} else if (comp_msg->slurm_rc != SLURM_SUCCESS) {
		error("slurmd error running JobId=%u on %s=%s: %s",
		      comp_msg->job_id,
		      msg_title, nodes,
		      slurm_strerror(comp_msg->slurm_rc));
		slurmctld_diag_stats.jobs_failed++;
		if (error_code == SLURM_SUCCESS) {
#ifdef HAVE_FRONT_END
			if (job_ptr && job_ptr->front_end_ptr) {
				update_front_end_msg_t update_node_msg;
				memset(&update_node_msg, 0,
				       sizeof(update_node_msg));
				update_node_msg.name = job_ptr->front_end_ptr->
						       name;
				update_node_msg.node_state = NODE_STATE_DRAIN;
				update_node_msg.reason =
					"batch job complete failure";
				error_code = update_front_end(&update_node_msg,
							      msg->auth_uid);
			}
#else
			error_code = drain_nodes(comp_msg->node_name,
			                         "batch job complete failure",
			                         slurm_conf.slurm_user_id);
#endif	/* !HAVE_FRONT_END */
			if ((comp_msg->job_rc != SLURM_SUCCESS) && job_ptr &&
			    job_ptr->details && job_ptr->details->requeue)
				job_requeue = true;
			dump_job = true;
			dump_node = true;
		}
	}

	/* Mark job allocation complete */
	i = job_complete(comp_msg->job_id, msg->auth_uid, job_requeue, false,
			 comp_msg->job_rc);
	error_code = MAX(error_code, i);
	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	/* this has to be done after the job_complete */

	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		debug2("%s JobId=%u: %s ",
		       __func__, comp_msg->job_id, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s JobId=%u %s", __func__, comp_msg->job_id, TIME_STR);
		slurmctld_diag_stats.jobs_completed++;
		dump_job = true;
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	if (dump_job)
		(void) schedule_job_save();	/* Has own locking */
	if (dump_node)
		(void) schedule_node_save();	/* Has own locking */
}

static void _slurm_rpc_dump_batch_script(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	job_record_t *job_ptr;
	buf_t *script;
	job_id_msg_t *job_id_msg = msg->data;
	/* Locks: Read config, job, and node info */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	START_TIMER;
	debug3("Processing RPC details: REQUEST_BATCH_SCRIPT for JobId=%u",
	       job_id_msg->job_id);
	lock_slurmctld(job_read_lock);

	if ((job_ptr = find_job_record(job_id_msg->job_id))) {
		if (!validate_operator(msg->auth_uid) &&
		    (job_ptr->user_id != msg->auth_uid)) {
			rc = ESLURM_USER_ID_MISSING;
		} else {
			script = get_job_script(job_ptr);
			if (!script)
				rc = ESLURM_JOB_SCRIPT_MISSING;
		}
	} else {
		rc = ESLURM_INVALID_JOB_ID;
	}

	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	/* init response_msg structure */
	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		response_init(&response_msg, msg, RESPONSE_BATCH_SCRIPT,
			      script);
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		FREE_NULL_BUFFER(script);
	}
}

/* _slurm_rpc_job_step_create - process RPC to create/register a job step
 *	with the step_mgr */
static void _slurm_rpc_job_step_create(slurm_msg_t *msg)
{
	char *err_msg = NULL;
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	slurm_msg_t resp;
	step_record_t *step_rec;
	job_step_create_response_msg_t job_step_resp;
	job_step_create_request_msg_t *req_step_msg = msg->data;
	slurm_cred_t *slurm_cred = (slurm_cred_t *) NULL;
	/* Locks: Write jobs, read nodes */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;

	xassert(msg->auth_uid_set);

	if (req_step_msg->user_id == SLURM_AUTH_NOBODY) {
		req_step_msg->user_id = msg->auth_uid;

		if (get_log_level() >= LOG_LEVEL_DEBUG3) {
			char *host = auth_g_get_host(msg->auth_cred);
			debug3("%s: [%s] set RPC user_id to %d",
			       __func__, host, msg->auth_uid);
			xfree(host);
		}
	} else if (msg->auth_uid != req_step_msg->user_id) {
		error("Security violation, JOB_STEP_CREATE RPC from uid=%u to run as uid %u",
		      msg->auth_uid, req_step_msg->user_id);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	dump_step_desc(req_step_msg);

#if defined HAVE_FRONT_END
	/* Limited job step support */
	/* Non-super users not permitted to run job steps on front-end.
	 * A single slurmd can not handle a heavy load. */
	if (!validate_slurm_user(msg->auth_uid)) {
		info("Attempt to execute job step by uid=%u", msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_NO_STEPS);
		return;
	}
#endif

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
#if defined HAVE_NATIVE_CRAY
		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		if (LOTS_OF_AGENTS ||
		    (slurmctld_config.server_thread_count >= 128)) {
			/*
			 * Don't start more steps if system is very busy right
			 * now. Getting cray network switch cookie is slow and
			 * happens with job write lock set.
			 */
			slurm_send_rc_msg(msg, EAGAIN);
			slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
			return;
		}
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
#endif

		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}
	error_code = step_create(req_step_msg, &step_rec,
				 msg->protocol_version, &err_msg);

	if (error_code == SLURM_SUCCESS) {
		error_code = _make_step_cred(step_rec, &slurm_cred,
					     step_rec->start_protocol_ver);
		ext_sensors_g_get_stepstartdata(step_rec);
	}
	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		if (error_code == ESLURM_PROLOG_RUNNING)
			log_flag(STEPS, "%s for configuring %ps: %s",
				 __func__, &req_step_msg->step_id,
				 slurm_strerror(error_code));
		else if (error_code == ESLURM_DISABLED)
			log_flag(STEPS, "%s for suspended %ps: %s",
				 __func__, &req_step_msg->step_id,
				 slurm_strerror(error_code));
		else
			log_flag(STEPS, "%s for %ps: %s",
				 __func__, &req_step_msg->step_id,
				 slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		slurm_step_layout_t *step_layout = NULL;
		dynamic_plugin_data_t *select_jobinfo = NULL;
		dynamic_plugin_data_t *switch_job = NULL;

		log_flag(STEPS, "%s: %pS %s %s",
			 __func__, step_rec, req_step_msg->node_list, TIME_STR);

		memset(&job_step_resp, 0, sizeof(job_step_resp));
		job_step_resp.job_id = step_rec->step_id.job_id;
		job_step_resp.job_step_id = step_rec->step_id.step_id;
		job_step_resp.resv_ports  = step_rec->resv_ports;

		step_layout = slurm_step_layout_copy(step_rec->step_layout);
		job_step_resp.step_layout = step_layout;

#ifdef HAVE_FRONT_END
		if (step_rec->job_ptr->batch_host) {
			job_step_resp.step_layout->front_end =
				xstrdup(step_rec->job_ptr->batch_host);
		}
#endif
		if (step_rec->job_ptr && step_rec->job_ptr->details &&
		    (step_rec->job_ptr->details->cpu_bind_type != NO_VAL16)) {
			job_step_resp.def_cpu_bind_type =
				step_rec->job_ptr->details->cpu_bind_type;
		}
		job_step_resp.cred           = slurm_cred;
		job_step_resp.use_protocol_ver = step_rec->start_protocol_ver;

		/*
		 * select_jobinfo can be removed from
		 * job_step_create_response_msg_t 2 versions after 23.02 */
		if (job_step_resp.use_protocol_ver <
		    SLURM_23_02_PROTOCOL_VERSION) {
			select_jobinfo = select_g_select_jobinfo_copy(
				step_rec->select_jobinfo);
			job_step_resp.select_jobinfo = select_jobinfo;
		}

		if (step_rec->switch_job)
			switch_g_duplicate_jobinfo(step_rec->switch_job,
						   &switch_job);
		job_step_resp.switch_job = switch_job;

		if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
			unlock_slurmctld(job_write_lock);
			_throttle_fini(&active_rpc_cnt);
		}
		response_init(&resp, msg, RESPONSE_JOB_STEP_CREATE,
			      &job_step_resp);
		resp.protocol_version = step_rec->start_protocol_ver;

		slurm_send_node_msg(msg->conn_fd, &resp);

		slurm_cred_destroy(slurm_cred);
		slurm_step_layout_destroy(step_layout);
		if (select_jobinfo)
			select_g_select_jobinfo_free(select_jobinfo);
		switch_g_free_jobinfo(switch_job);

		schedule_job_save();	/* Sets own locks */
	}

	xfree(err_msg);
}

/* _slurm_rpc_job_step_get_info - process request for job step info */
static void _slurm_rpc_job_step_get_info(slurm_msg_t *msg)
{
	DEF_TIMERS;
	void *resp_buffer = NULL;
	int resp_buffer_size = 0;
	int error_code = SLURM_SUCCESS;
	job_step_info_request_msg_t *request = msg->data;
	/* Locks: Read config, job, part */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);

	if ((request->last_update - 1) >= last_job_update) {
		unlock_slurmctld(job_read_lock);
		log_flag(STEPS, "%s: no change", __func__);
		error_code = SLURM_NO_CHANGE_IN_DATA;
	} else {
		buf_t *buffer = init_buf(BUF_SIZE);
		error_code = pack_ctld_job_step_info_response_msg(
			&request->step_id, msg->auth_uid, request->show_flags,
			buffer, msg->protocol_version);
		unlock_slurmctld(job_read_lock);
		END_TIMER2(__func__);
		if (error_code) {
			/* job_id:step_id not found or otherwise *\
			\* error message is printed elsewhere    */
			log_flag(STEPS, "%s: %s",
				 __func__, slurm_strerror(error_code));
			FREE_NULL_BUFFER(buffer);
		} else {
			resp_buffer_size = get_buf_offset(buffer);
			resp_buffer = xfer_buf_data(buffer);
			log_flag(STEPS, "%s size=%d %s",
				 __func__, resp_buffer_size, TIME_STR);
		}
	}

	if (error_code)
		slurm_send_rc_msg(msg, error_code);
	else {
		slurm_msg_t response_msg;
		response_init(&response_msg, msg, RESPONSE_JOB_STEP_INFO,
			      resp_buffer);
		response_msg.data_size = resp_buffer_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(resp_buffer);
	}
}

/* _slurm_rpc_job_will_run - process RPC to determine if job with given
 *	configuration can be initiated */
static void _slurm_rpc_job_will_run(slurm_msg_t *msg)
{
	/* init */
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	job_record_t *job_ptr = NULL;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition, read fed*/
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	gid_t gid = auth_g_get_gid(msg->auth_cred);
	slurm_addr_t resp_addr;
	will_run_response_msg_t *resp = NULL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;

	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		goto send_reply;
	}

	START_TIMER;
	if ((error_code = _valid_id("REQUEST_JOB_WILL_RUN", job_desc_msg,
				    msg->auth_uid, gid, msg->protocol_version)))
		goto send_reply;

	_set_hostname(msg, &job_desc_msg->alloc_node);

	if ((job_desc_msg->alloc_node == NULL)
	    ||  (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_JOB_WILL_RUN lacks alloc_node from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		lock_slurmctld(job_read_lock);
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid, &err_msg);
		unlock_slurmctld(job_read_lock);
	}

	if (err_msg)
		job_submit_user_msg = xstrdup(err_msg);

	if (!slurm_get_peer_addr(msg->conn_fd, &resp_addr)) {
		job_desc_msg->resp_host = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(&resp_addr, job_desc_msg->resp_host,
				 INET6_ADDRSTRLEN);
		dump_job_desc(job_desc_msg);
		if (error_code == SLURM_SUCCESS) {
			lock_slurmctld(job_write_lock);
			if (job_desc_msg->job_id == NO_VAL) {
				job_desc_msg->het_job_offset = NO_VAL;
				error_code = job_allocate(job_desc_msg, false,
							  true, &resp, true,
							  msg->auth_uid, false,
							  &job_ptr,
							  &err_msg,
							  msg->protocol_version);
			} else {	/* existing job test */
				job_ptr = find_job_record(job_desc_msg->job_id);
				error_code = job_start_data(job_ptr, &resp);
			}
			unlock_slurmctld(job_write_lock);
			END_TIMER2(__func__);
		}
	} else if (errno)
		error_code = errno;
	else
		error_code = SLURM_ERROR;

send_reply:

	/* return result */
	if (error_code) {
		debug2("%s: %s", __func__, slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else if (resp) {
		slurm_msg_t response_msg;
		response_init(&response_msg, msg, RESPONSE_JOB_WILL_RUN, resp);
		resp->job_submit_user_msg = job_submit_user_msg;
		job_submit_user_msg = NULL;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		slurm_free_will_run_response_msg(resp);
		debug2("%s success %s", __func__, TIME_STR);
	} else {
		debug2("%s success %s", __func__, TIME_STR);
		if (job_desc_msg->job_id == NO_VAL)
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

static bool _node_has_feature(node_record_t *node_ptr, char *feature)
{
	node_feature_t *node_feature;

	if ((node_feature = list_find_first(active_feature_list,
					    list_find_feature, feature))) {
		if (bit_test(node_feature->node_bitmap, node_ptr->index))
		    return true;
	}

	return false;
}

/*
 * Find available future node to associate slurmd with.
 *
 * Sets reg_msg->node_name to found node_name so subsequent calls to
 * find the node work.
 */
static void _find_avail_future_node(slurm_msg_t *msg)
{
	node_record_t *node_ptr;
	slurm_node_registration_status_msg_t *reg_msg = msg->data;

	node_ptr = find_node_record2(reg_msg->node_name);
	if (node_ptr == NULL) {
		int i;
		time_t now;

		debug2("finding available dynamic future node for %s",
		       reg_msg->node_name);

		for (i = 0; (node_ptr = next_node(&i)); i++) {
			slurm_addr_t addr;
			char *comm_name = NULL;

			if (!IS_NODE_FUTURE(node_ptr))
			    continue;

			if (reg_msg->dynamic_feature &&
			    !_node_has_feature(
				node_ptr,reg_msg->dynamic_feature))
				continue;
			else if ((node_ptr->cpus != reg_msg->cpus) ||
				 (node_ptr->boards != reg_msg->boards) ||
				 (node_ptr->tot_sockets != reg_msg->sockets) ||
				 (node_ptr->cores != reg_msg->cores) ||
				 (node_ptr->threads != reg_msg->threads))
				continue;

			/* Get IP of slurmd */
			if (msg->conn_fd >= 0 &&
			    !slurm_get_peer_addr(msg->conn_fd, &addr)) {
				comm_name = xmalloc(INET6_ADDRSTRLEN);
				slurm_get_ip_str(&addr, comm_name,
						 INET6_ADDRSTRLEN);
			}

			set_node_comm_name(node_ptr, comm_name,
					   reg_msg->hostname);
			now = time(NULL);
			node_ptr->node_state = NODE_STATE_IDLE;
			node_ptr->node_state |= NODE_STATE_DYNAMIC_FUTURE;
			node_ptr->last_response = now;

			bit_clear(future_node_bitmap, node_ptr->index);
			xfree(comm_name);

			clusteracct_storage_g_node_up(acct_db_conn, node_ptr,
						      now);

			break;
		}
	} else {
		debug2("found existing node %s for dynamic future node registration",
		       reg_msg->node_name);
	}

	if (node_ptr) {
		debug2("dynamic future node %s/%s/%s assigned to node %s",
		       reg_msg->node_name, node_ptr->node_hostname,
		       node_ptr->comm_name, node_ptr->name);
		/*
		 * We always need to send the hostname back to the slurmd. In
		 * case the slurmd already registered and we found the node_ptr
		 * by the node_hostname.
		 */
		xfree(reg_msg->node_name);
		reg_msg->node_name = xstrdup(node_ptr->name);
	}
}

static void _slurm_post_rpc_node_registration()
{
	if (do_post_rpc_node_registration)
		clusteracct_storage_g_cluster_tres(acct_db_conn, NULL, NULL, 0,
						   SLURM_PROTOCOL_VERSION);
	do_post_rpc_node_registration = false;
}

/* _slurm_rpc_node_registration - process RPC to determine if a node's
 *	actual configuration satisfies the configured specification */
static void _slurm_rpc_node_registration(slurm_msg_t *msg)
{
	/* init */
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	bool newly_up = false;
	bool already_registered = false;
	slurm_node_registration_status_msg_t *node_reg_stat_msg = msg->data;
	slurmctld_lock_t job_write_lock = {
		.conf = READ_LOCK,
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.part = WRITE_LOCK,
		.fed = READ_LOCK
	};

	START_TIMER;
	if (!validate_slurm_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, NODE_REGISTER RPC from uid=%u",
		      msg->auth_uid);
	}

	if (msg->protocol_version != SLURM_PROTOCOL_VERSION)
		info("Node %s appears to have a different version "
		     "of Slurm than ours.  Please update at your earliest "
		     "convenience.", node_reg_stat_msg->node_name);

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		if (!(slurm_conf.debug_flags & DEBUG_FLAG_NO_CONF_HASH) &&
		    (node_reg_stat_msg->hash_val != NO_VAL) &&
		    (node_reg_stat_msg->hash_val != slurm_conf.hash_val)) {
			error("Node %s appears to have a different slurm.conf "
			      "than the slurmctld.  This could cause issues "
			      "with communication and functionality.  "
			      "Please review both files and make sure they "
			      "are the same.  If this is expected ignore, and "
			      "set DebugFlags=NO_CONF_HASH in your slurm.conf.",
			      node_reg_stat_msg->node_name);
		}
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			lock_slurmctld(job_write_lock);

		if (node_reg_stat_msg->dynamic_type &&
		    (node_reg_stat_msg->flags & SLURMD_REG_FLAG_RESP)) {

			if (node_reg_stat_msg->dynamic_type == DYN_NODE_FUTURE) {
				/*
				 * dynamic future nodes doen't know what node
				 * it's mapped to to be able to load all configs
				 * in. slurmctld will tell the slurmd what node
				 * it's mapped to and then the slurmd will then
				 * load in configuration based off of the mapped
				 * name and send another registration.
				 *
				 * Subsequent slurmd registrations will have the
				 * mapped node_name.
				 */
				_find_avail_future_node(msg);

				if (!(msg->flags & CTLD_QUEUE_PROCESSING))
					unlock_slurmctld(job_write_lock);

				goto send_resp;
			} else if (find_node_record2(
					node_reg_stat_msg->node_name)) {
				already_registered = true;
			} else {
				error_code = create_dynamic_reg_node(msg);
			}
		}

#ifdef HAVE_FRONT_END		/* Operates only on front-end */
		error_code = validate_nodes_via_front_end(node_reg_stat_msg,
							  msg->protocol_version,
							  &newly_up);
#else
		validate_jobs_on_node(node_reg_stat_msg);
		error_code = validate_node_specs(msg, &newly_up);
#endif
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_write_lock);
		END_TIMER2(__func__);
		if (newly_up) {
			queue_job_scheduler();
		}
	}


send_resp:

	/* return result */
	if (error_code) {
		error("%s node=%s: %s",
		      __func__, node_reg_stat_msg->node_name,
		      slurm_strerror(error_code));
		/*
		 * Notify slurmd that we got the registration even if we
		 * consider it to be invalid to avoid having slurmd try to
		 * register again continuously.
		 */
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	} else {
		debug2("%s complete for %s %s",
		       __func__, node_reg_stat_msg->node_name, TIME_STR);
		/* If the slurmd is requesting a response send it */
		if (node_reg_stat_msg->flags & SLURMD_REG_FLAG_RESP) {
			slurm_msg_t response_msg;
			slurm_node_reg_resp_msg_t tmp_resp;
			memset(&tmp_resp, 0, sizeof(tmp_resp));

			/*
			 * Don't add the assoc_mgr_tres_list here as it could
			 * get freed later if you do.  The pack functions grab
			 * it for us if it isn't here.
			 */
			//tmp_resp.tres_list = assoc_mgr_tres_list;

			if (node_reg_stat_msg->dynamic_type)
				tmp_resp.node_name =
					node_reg_stat_msg->node_name;

			response_init(&response_msg, msg,
				      RESPONSE_NODE_REGISTRATION, &tmp_resp);

			slurm_send_node_msg(msg->conn_fd, &response_msg);
		} else
			slurm_send_rc_msg(msg, SLURM_SUCCESS);

		if (!already_registered &&
		    (node_reg_stat_msg->dynamic_type == DYN_NODE_NORM)) {
			if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
				/* Must be called outside of locks */
				clusteracct_storage_g_cluster_tres(
					acct_db_conn, NULL, NULL, 0,
					SLURM_PROTOCOL_VERSION);
			} else {
				do_post_rpc_node_registration = true;
			}
		}
	}
}

/* _slurm_rpc_job_alloc_info - process RPC to get details on existing job */
static void _slurm_rpc_job_alloc_info(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	job_record_t *job_ptr;
	DEF_TIMERS;
	job_alloc_info_msg_t *job_info_msg = msg->data;
	resource_allocation_response_msg_t *job_info_resp_msg;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(msg->auth_uid, job_info_msg->job_id,
				    &job_ptr);
	END_TIMER2(__func__);

	/* return result */
	if (error_code || (job_ptr == NULL) || (job_ptr->job_resrcs == NULL)) {
		unlock_slurmctld(job_read_lock);
		debug2("%s: JobId=%u, uid=%u: %s",
		       __func__, job_info_msg->job_id, msg->auth_uid,
		      slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug("%s: JobId=%u NodeList=%s %s", __func__,
		      job_info_msg->job_id, job_ptr->nodes, TIME_STR);

		job_info_resp_msg = build_job_info_resp(job_ptr);
		set_remote_working_response(job_info_resp_msg, job_ptr,
					    job_info_msg->req_cluster);
		unlock_slurmctld(job_read_lock);

		response_init(&response_msg, msg, RESPONSE_JOB_ALLOCATION_INFO,
			      job_info_resp_msg);

		slurm_send_node_msg(msg->conn_fd, &response_msg);

		/* NULL out msg->working_cluster_rec because it's pointing to
		 * the global memory */
		job_info_resp_msg->working_cluster_rec = NULL;
		slurm_free_resource_allocation_response_msg(job_info_resp_msg);
	}
}

static void _het_job_alloc_list_del(void *x)
{
	resource_allocation_response_msg_t *job_info_resp_msg;

	job_info_resp_msg = (resource_allocation_response_msg_t *) x;
	/* NULL out msg->working_cluster_rec because it's pointing to
	 * the global memory */
	job_info_resp_msg->working_cluster_rec = NULL;
	slurm_free_resource_allocation_response_msg(job_info_resp_msg);
}

/*
 * _slurm_rpc_het_job_alloc_info - process RPC to get details on existing
 *				       hetjob.
 */
static void _slurm_rpc_het_job_alloc_info(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	job_record_t *job_ptr, *het_job;
	ListIterator iter;
	void *working_cluster_rec = NULL;
	List resp;
	DEF_TIMERS;
	job_alloc_info_msg_t *job_info_msg = msg->data;
	resource_allocation_response_msg_t *job_info_resp_msg;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(msg->auth_uid, job_info_msg->job_id,
				    &job_ptr);
	END_TIMER2(__func__);

	/* return result */
	if ((error_code == SLURM_SUCCESS) && job_ptr &&
	    (job_ptr->het_job_id && !job_ptr->het_job_list))
		error_code = ESLURM_NOT_HET_JOB_LEADER;
	if (error_code || (job_ptr == NULL) || (job_ptr->job_resrcs == NULL)) {
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
		debug2("%s: JobId=%u, uid=%u: %s",
		       __func__, job_info_msg->job_id, msg->auth_uid,
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	debug2("%s: JobId=%u NodeList=%s %s", __func__,
	       job_info_msg->job_id, job_ptr->nodes, TIME_STR);

	if (!job_ptr->het_job_list) {
		resp = list_create(_het_job_alloc_list_del);
		job_info_resp_msg = build_job_info_resp(job_ptr);
		set_remote_working_response(job_info_resp_msg, job_ptr,
					    job_info_msg->req_cluster);
		list_append(resp, job_info_resp_msg);
	} else {
		resp = list_create(_het_job_alloc_list_del);
		iter = list_iterator_create(job_ptr->het_job_list);
		while ((het_job = list_next(iter))) {
			if (job_ptr->het_job_id != het_job->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, job_ptr);
				continue;
			}
			if (het_job->job_id != job_info_msg->job_id)
				(void) job_alloc_info_ptr(msg->auth_uid,
							  het_job);
			job_info_resp_msg = build_job_info_resp(het_job);
			if (working_cluster_rec) {
				job_info_resp_msg->working_cluster_rec =
					working_cluster_rec;
			} else {
				set_remote_working_response(job_info_resp_msg,
						het_job,
						job_info_msg->req_cluster);
				working_cluster_rec =
					job_info_resp_msg->working_cluster_rec;
			}
			list_append(resp, job_info_resp_msg);
		}
		list_iterator_destroy(iter);
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING))
		unlock_slurmctld(job_read_lock);

	response_init(&response_msg, msg, RESPONSE_HET_JOB_ALLOCATION, resp);
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(resp);
}

/* _slurm_rpc_job_sbcast_cred - process RPC to get details on existing job
 *	plus sbcast credential */
static void _slurm_rpc_job_sbcast_cred(slurm_msg_t *msg)
{
#ifdef HAVE_FRONT_END
	slurm_send_rc_msg(msg, ESLURM_NOT_SUPPORTED);
#else
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	job_record_t *job_ptr = NULL, *het_job_ptr;
	step_record_t *step_ptr = NULL;
	char *local_node_list = NULL, *node_list = NULL;
	DEF_TIMERS;
	step_alloc_info_msg_t *job_info_msg = msg->data;
	job_sbcast_cred_msg_t job_info_resp_msg;
	sbcast_cred_arg_t sbcast_arg;
	sbcast_cred_t *sbcast_cred;
	/* Locks: Read config, job, read node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_read_lock);
	if (job_info_msg->het_job_offset == NO_VAL) {
		bitstr_t *node_bitmap = NULL;
		ListIterator iter;
		error_code = job_alloc_info(msg->auth_uid,
					    job_info_msg->step_id.job_id,
					    &job_ptr);
		if (job_ptr && job_ptr->het_job_list) {  /* Do full HetJob */
			job_info_msg->step_id.step_id = NO_VAL;
			iter = list_iterator_create(job_ptr->het_job_list);
			while ((het_job_ptr = list_next(iter))) {
				error_code = job_alloc_info_ptr(msg->auth_uid,
							        het_job_ptr);
				if (error_code)
					break;
				if (!het_job_ptr->node_bitmap) {
					debug("%s: %pJ lacks node bitmap",
					      __func__, het_job_ptr);
				} else if (!node_bitmap) {
					node_bitmap = bit_copy(
						     het_job_ptr->node_bitmap);
				} else {
					bit_or(node_bitmap,
					       het_job_ptr->node_bitmap);
				}
			}
			list_iterator_destroy(iter);
			if (!error_code) {
				local_node_list = bitmap2node_name(node_bitmap);
				node_list = local_node_list;
			}
			FREE_NULL_BITMAP(node_bitmap);
		}
	} else {
		job_ptr = find_het_job_record(job_info_msg->step_id.job_id,
					      job_info_msg->het_job_offset);
		if (job_ptr) {
			job_info_msg->step_id.job_id = job_ptr->job_id;
			error_code = job_alloc_info(
				msg->auth_uid, job_info_msg->step_id.job_id,
				&job_ptr);
		} else {
			error_code = ESLURM_INVALID_JOB_ID;
		}
	}

	if (job_ptr && !validate_operator(msg->auth_uid) &&
	    (job_ptr->user_id != msg->auth_uid))
		error_code = ESLURM_USER_ID_MISSING;

	if ((error_code == SLURM_SUCCESS) && job_ptr
	    && (job_info_msg->step_id.step_id != NO_VAL)) {
		step_ptr = find_step_record(job_ptr, &job_info_msg->step_id);
		if (!step_ptr) {
			job_ptr = NULL;
			error_code = ESLURM_INVALID_JOB_ID;
		} else if (step_ptr->step_layout &&
			   (step_ptr->step_layout->node_cnt !=
			    job_ptr->node_cnt)) {
			node_list = step_ptr->step_layout->node_list;
		}
	}
	if ((error_code == SLURM_SUCCESS) && job_ptr && !node_list)
		node_list = job_ptr->nodes;
	END_TIMER2(__func__);

	/* return result */
	if (error_code || (job_ptr == NULL)) {
		char job_id_str[64];
		unlock_slurmctld(job_read_lock);
		debug2("%s: JobId=%s, uid=%u: %s",
		       __func__,
		       slurm_get_selected_step_id(job_id_str,
						  sizeof(job_id_str),
						  job_info_msg),
		       msg->auth_uid,
		       slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
		xfree(local_node_list);
		return ;
	}

	/*
	 * Note - using pointers to other xmalloc'd elements owned by other
	 * structures to avoid copy overhead. Do not free them!
	 */
	memset(&sbcast_arg, 0, sizeof(sbcast_arg));
	sbcast_arg.job_id = job_ptr->job_id;
	sbcast_arg.het_job_id = job_ptr->het_job_id;
	if (step_ptr)
		sbcast_arg.step_id = step_ptr->step_id.step_id;
	else
		sbcast_arg.step_id = job_ptr->next_step_id;
	sbcast_arg.uid = job_ptr->user_id;
	sbcast_arg.gid = job_ptr->group_id;
	sbcast_arg.nodes = node_list; /* avoid extra copy */
	sbcast_arg.expiration = job_ptr->end_time;

	if ((sbcast_cred =
		    create_sbcast_cred(slurmctld_config.cred_ctx,
				       &sbcast_arg,
				       msg->protocol_version)) == NULL) {
		unlock_slurmctld(job_read_lock);
		error("%s %pJ cred create error", __func__, job_ptr);
		slurm_send_rc_msg(msg, SLURM_ERROR);
	} else {
		char job_id_str[64];
		info("%s: %s NodeList=%s - %s",
		     __func__,
		     slurm_get_selected_step_id(job_id_str, sizeof(job_id_str),
						job_info_msg),
		     node_list,
		     TIME_STR);

		memset(&job_info_resp_msg, 0, sizeof(job_info_resp_msg));
		job_info_resp_msg.job_id         = job_ptr->job_id;
		job_info_resp_msg.node_list      = xstrdup(node_list);
		job_info_resp_msg.sbcast_cred    = sbcast_cred;
		unlock_slurmctld(job_read_lock);

		response_init(&response_msg, msg, RESPONSE_JOB_SBCAST_CRED,
			      &job_info_resp_msg);

		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(job_info_resp_msg.node_list);
		delete_sbcast_cred(sbcast_cred);
	}
	xfree(local_node_list);
#endif
}

/* _slurm_rpc_ping - process ping RPC */
static void _slurm_rpc_ping(slurm_msg_t *msg)
{
	/* We could authenticate here, if desired */

	/* return result */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_config_request(slurm_msg_t *msg)
{
	config_request_msg_t *req = msg->data;
	slurm_msg_t response_msg;
	DEF_TIMERS;

	START_TIMER;
	if (!config_for_slurmd) {
		error("%s: Rejected request as configless is disabled",
		      __func__);
		slurm_send_rc_msg(msg, ESLURM_CONFIGLESS_DISABLED);
		return;
	}

	if ((req->flags & CONFIG_REQUEST_SLURMD) &&
	    !validate_slurm_user(msg->auth_uid)) {
		error("%s: Rejected request for slurmd configs by uid=%u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}
	END_TIMER2(__func__);

	response_init(&response_msg, msg, RESPONSE_CONFIG, config_for_clients);
	if (req->flags & CONFIG_REQUEST_SLURMD)
		response_msg.data = config_for_slurmd;

	slurm_send_node_msg(msg->conn_fd, &response_msg);
}

/* _slurm_rpc_reconfigure_controller - process RPC to re-initialize
 *	slurmctld from configuration file
 * Anything you add to this function must be added to the
 * slurm_reconfigure function inside controller.c try
 * to keep these in sync.
 */
static void _slurm_rpc_reconfigure_controller(slurm_msg_t *msg)
{
	int error_code;

	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, RECONFIGURE RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	} else
		info("Processing Reconfiguration Request");

	error_code = reconfigure_slurm();

	/* return result */
	slurm_send_rc_msg(msg, error_code);

	/* finish up the configuration */
	reconfigure_slurm_post_send(error_code);
}

/* _slurm_rpc_takeover - process takeover RPC */
static void _slurm_rpc_takeover(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;

	/* We could authenticate here, if desired */
	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, TAKEOVER RPC from uid=%u",
		      msg->auth_uid);
		error_code = ESLURM_USER_ID_MISSING;
	} else {
		/* takeover is not possible in controller mode */
		/* return success */
		info("Performing RPC: REQUEST_TAKEOVER : "
		     "already in controller mode - skipping");
	}

	slurm_send_rc_msg(msg, error_code);

}

/* _slurm_rpc_shutdown_controller - process RPC to shutdown slurmctld */
static void _slurm_rpc_shutdown_controller(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	slurmctld_shutdown_type_t options = SLURMCTLD_SHUTDOWN_ALL;
	time_t now = time(NULL);
	shutdown_msg_t *shutdown_msg = msg->data;
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, SHUTDOWN RPC from uid=%u",
		      msg->auth_uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
	if (error_code);
	else if (msg->msg_type == REQUEST_CONTROL) {
		info("Performing RPC: REQUEST_CONTROL");
		/* resume backup mode */
		slurmctld_config.resume_backup = true;
	} else {
		info("Performing RPC: REQUEST_SHUTDOWN");
		options = shutdown_msg->options;
	}

	/* do RPC call */
	if (error_code)
		;
	else if (options == SLURMCTLD_SHUTDOWN_ABORT)
		info("performing immediate shutdown without state save");
	else if (slurmctld_config.shutdown_time)
		debug2("shutdown RPC issued when already in progress");
	else {
		if ((msg->msg_type == REQUEST_SHUTDOWN) &&
		    (options == SLURMCTLD_SHUTDOWN_ALL)) {
			/* This means (msg->msg_type != REQUEST_CONTROL) */
			lock_slurmctld(node_read_lock);
			msg_to_slurmd(REQUEST_SHUTDOWN);
			unlock_slurmctld(node_read_lock);
		}
		if (slurmctld_config.thread_id_sig)	/* signal clean-up */
			pthread_kill(slurmctld_config.thread_id_sig, SIGTERM);
		else {
			error("thread_id_sig undefined, hard shutdown");
			slurmctld_config.shutdown_time = now;
			slurmctld_shutdown();
		}
	}

	if (msg->msg_type == REQUEST_CONTROL) {
		struct timespec ts = {0, 0};

		/* save_all_state();	performed by _slurmctld_background */

		/*
		 * jobcomp/elasticsearch saves/loads the state to/from file
		 * elasticsearch_state. Since the jobcomp API isn't designed
		 * with save/load state operations, the jobcomp/elasticsearch
		 * _save_state() is highly coupled to its fini() function. This
		 * state doesn't follow the same execution path as the rest of
		 * Slurm states, where in save_all_sate() they are all indepen-
		 * dently scheduled. So we save it manually here.
		 */
		(void) jobcomp_g_fini();

		/*
		 * Wait for the backup to dump state and finish up everything.
		 * This should happen in _slurmctld_background and then release
		 * once we know for sure we are in backup mode in run_backup().
		 * Here we will wait CONTROL_TIMEOUT - 1 before we reply.
		 */
		ts.tv_sec = now + CONTROL_TIMEOUT - 1;

		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		slurm_cond_timedwait(&slurmctld_config.backup_finish_cond,
				     &slurmctld_config.thread_count_lock,
				     &ts);
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		if (slurmctld_config.resume_backup)
			error("%s: REQUEST_CONTROL reply but backup not completely done relinquishing control.  Old state possible", __func__);
	}

	slurm_send_rc_msg(msg, error_code);
	if ((error_code == SLURM_SUCCESS) &&
	    (options == SLURMCTLD_SHUTDOWN_ABORT) &&
	    (slurmctld_config.thread_id_sig))
		pthread_kill(slurmctld_config.thread_id_sig, SIGABRT);
}

static int _find_stepid_by_container_id(void *x, void *arg)
{
	find_job_by_container_id_args_t *args = arg;
	step_record_t *step_ptr = x;
	slurm_step_id_t *step_id;

	if (xstrcmp(args->id, step_ptr->container_id))
		return SLURM_SUCCESS;

	step_id = xmalloc(sizeof(*step_id));
	*step_id = step_ptr->step_id;

	list_append(args->step_list, step_id);

	return SLURM_SUCCESS;
}

static int _find_stepid_by_userid(void *x, void *arg)
{
	find_job_by_container_id_args_t *args = arg;
	job_record_t *job_ptr = x;

	if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != args->request_uid) &&
	    !validate_operator(args->request_uid)) {
		if (slurm_mcs_get_privatedata()) {
			if (mcs_g_check_mcs_label(args->request_uid,
						  job_ptr->mcs_label, false))
				return SLURM_SUCCESS;
		} else if (!assoc_mgr_is_user_acct_coord(acct_db_conn,
							 args->request_uid,
							 job_ptr->account)) {
			return SLURM_SUCCESS;
		}
	}

	if ((args->uid != SLURM_AUTH_NOBODY) &&
	    (args->uid != job_ptr->user_id)) {
		/* skipping per non-matching user */
		return SLURM_SUCCESS;
	}

	/* walk steps for matching container_id */
	if (list_for_each_ro(job_ptr->step_list, _find_stepid_by_container_id,
			     args) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void _find_stepids_by_container_id(uid_t request_uid, uid_t uid,
					  const char *id, list_t **step_list)
{
	slurmctld_lock_t job_read_lock =
		{ .conf = READ_LOCK, .job = READ_LOCK };
	find_job_by_container_id_args_t args =
		{ .request_uid = request_uid, .uid = uid, .id = id };
	DEF_TIMERS;

	xassert(id && id[0]);

	if (!*step_list)
		*step_list = list_create((ListDelF) slurm_free_step_id);
	args.step_list = *step_list;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	list_for_each_ro(job_list, _find_stepid_by_userid, &args);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);
}

static void _slurm_rpc_step_by_container_id(slurm_msg_t *msg)
{
	container_id_request_msg_t *req = msg->data;
	container_id_response_msg_t resp = {0};
	int rc = SLURM_UNEXPECTED_MSG_ERROR;

	log_flag(PROTOCOL, "%s: got REQUEST_STEP_BY_CONTAINER_ID from %s auth_uid=%u flags=0x%x uid=%u container_id=%s",
		 __func__, (msg->auth_uid_set ? "validated" : "suspect"),
		 msg->auth_uid, req->show_flags, req->uid, req->container_id);

	if (!msg->auth_uid_set) {
		/* this should never happen? */
		rc = ESLURM_AUTH_CRED_INVALID;
	} else if (!req->container_id || !req->container_id[0]) {
		rc = ESLURM_INVALID_CONTAINER_ID;
	} else {
		slurm_msg_t response_msg;

		response_init(&response_msg, msg, RESPONSE_STEP_BY_CONTAINER_ID,
			      &resp);
		response_msg.restrict_uid = msg->auth_uid;
		response_msg.restrict_uid_set = true;
		response_msg.data_size = sizeof(resp);

		if (req->container_id && req->container_id[0])
			_find_stepids_by_container_id(msg->auth_uid, req->uid,
						      req->container_id,
						      &resp.steps);

		(void) slurm_send_node_msg(msg->conn_fd, &response_msg);
		return;
	}

	slurm_send_rc_msg(msg, rc);
}

/* _slurm_rpc_step_complete - process step completion RPC to note the
 *      completion of a job step on at least some nodes.
 *	If the job step is complete, it may
 *	represent the termination of an entire job step */
static void _slurm_rpc_step_complete(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int rc, rem;
	uint32_t step_rc;
	DEF_TIMERS;
	step_complete_msg_t *req = msg->data;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	/* init */
	START_TIMER;
	log_flag(STEPS, "Processing RPC details: REQUEST_STEP_COMPLETE for %ps nodes %u-%u rc=%u(%s)",
		 &req->step_id, req->range_first, req->range_last,
		 req->step_rc, slurm_strerror(req->step_rc));

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}

	rc = step_partial_comp(req, msg->auth_uid, true, &rem, &step_rc);

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

	END_TIMER2(__func__);

	log_flag(STEPS, "%s: %ps rc:%s %s",
		 __func__, &req->step_id, slurm_strerror(rc), TIME_STR);

	/* return result */
	(void) slurm_send_rc_msg(msg, rc);

	if (rc == SLURM_SUCCESS)
		(void) schedule_job_save();	/* Has own locking */
}

/* _slurm_rpc_step_layout - return the step layout structure for
 *      a job step, if it currently exists
 */
static void _slurm_rpc_step_layout(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	slurm_msg_t response_msg;
	DEF_TIMERS;
	slurm_step_id_t *req = msg->data;
	slurm_step_layout_t *step_layout = NULL;
	/* Locks: Read config job, write node */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr = NULL;
	step_record_t *step_ptr = NULL;
	ListIterator itr;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code = job_alloc_info(msg->auth_uid, req->job_id, &job_ptr);
	END_TIMER2(__func__);
	/* return result */
	if (error_code || (job_ptr == NULL)) {
		unlock_slurmctld(job_read_lock);
		if (error_code == ESLURM_ACCESS_DENIED) {
			error("Security violation, REQUEST_STEP_LAYOUT for JobId=%u from uid=%u",
			      req->job_id, msg->auth_uid);
		} else {
			log_flag(STEPS, "%s: JobId=%u, uid=%u: %s",
				 __func__, req->job_id, msg->auth_uid,
				 slurm_strerror(error_code));
		}
		slurm_send_rc_msg(msg, error_code);
		return;
	}

	/* We can't call find_step_record here since we may need more than 1 */
	itr = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(itr))) {
		if (!verify_step_id(&step_ptr->step_id, req))
			continue;

		if (step_layout)
			slurm_step_layout_merge(step_layout,
						step_ptr->step_layout);
		else
			step_layout = slurm_step_layout_copy(
				step_ptr->step_layout);

		/* break if don't need to look for further het_steps */
		if (step_ptr->step_id.step_het_comp == NO_VAL)
			break;
		/*
		 * If we are looking for a specific het step we can break here
		 * as well.
		 */
		if (req->step_het_comp != NO_VAL)
			break;
	}
	list_iterator_destroy(itr);

	if (!step_layout) {
		unlock_slurmctld(job_read_lock);
		log_flag(STEPS, "%s: %pJ StepId=%u Not Found",
			 __func__, job_ptr, req->step_id);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
		return;
	}

#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host)
		step_layout->front_end = xstrdup(job_ptr->batch_host);
#endif
	unlock_slurmctld(job_read_lock);

	response_init(&response_msg, msg, RESPONSE_STEP_LAYOUT, step_layout);

	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_step_layout_destroy(step_layout);
}

/* _slurm_rpc_step_update - update a job step
 */
static void _slurm_rpc_step_update(slurm_msg_t *msg)
{
	DEF_TIMERS;
	step_update_request_msg_t *req = msg->data;
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	int rc;

	START_TIMER;
	lock_slurmctld(job_write_lock);
	rc = update_step(req, msg->auth_uid);
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

/* _slurm_rpc_submit_batch_job - process RPC to submit a batch job */
static void _slurm_rpc_submit_batch_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	uint32_t job_id = 0, priority = 0;
	job_record_t *job_ptr = NULL;
	slurm_msg_t response_msg;
	submit_response_msg_t submit_msg;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, read partition, read
	 * federation */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	gid_t gid = auth_g_get_gid(msg->auth_cred);
	char *err_msg = NULL, *job_submit_user_msg = NULL;
	bool reject_job = false;

	START_TIMER;
	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}

	if ((error_code = _valid_id("REQUEST_SUBMIT_BATCH_JOB", job_desc_msg,
				    msg->auth_uid, gid,
				    msg->protocol_version))) {
		reject_job = true;
		goto send_msg;
	}

	_set_hostname(msg, &job_desc_msg->alloc_node);

	if ((job_desc_msg->alloc_node == NULL) ||
	    (job_desc_msg->alloc_node[0] == '\0')) {
		error_code = ESLURM_INVALID_NODE_NAME;
		error("REQUEST_SUBMIT_BATCH_JOB lacks alloc_node from uid=%u",
		      msg->auth_uid);
	}

	dump_job_desc(job_desc_msg);

	if (error_code == SLURM_SUCCESS) {
		/* Locks are for job_submit plugin use */
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			lock_slurmctld(job_read_lock);
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid, &err_msg);
		if (!(msg->flags & CTLD_QUEUE_PROCESSING))
			unlock_slurmctld(job_read_lock);
	}

	/*
	 * In validate_job_create_req(), err_msg is currently only modified in
	 * the call to job_submit_g_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to fed_mgr_job_allocate and/or job_allocate, and we
	 * need the job submit plugin value to build the resource allocation
	 * response in the call to build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	if (error_code) {
		reject_job = true;
		goto send_msg;
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		_throttle_start(&active_rpc_cnt);
		lock_slurmctld(job_write_lock);
	}
	START_TIMER;	/* Restart after we have locks */

	if (fed_mgr_fed_rec) {
		if (fed_mgr_job_allocate(msg, job_desc_msg, false,
					 &job_id, &error_code, &err_msg))
			reject_job = true;
	} else {
		/* Create new job allocation */
		job_desc_msg->het_job_offset = NO_VAL;
		error_code = job_allocate(job_desc_msg,
					  job_desc_msg->immediate,
					  false, NULL, 0, msg->auth_uid, false,
					  &job_ptr, &err_msg,
					  msg->protocol_version);
		if (!job_ptr ||
		    (error_code && job_ptr->job_state == JOB_FAILED))
			reject_job = true;
		else {
			job_id = job_ptr->job_id;
			priority = job_ptr->priority;
		}

		if (job_desc_msg->immediate &&
		    (error_code != SLURM_SUCCESS)) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			reject_job = true;
		}
	}

	if (!(msg->flags & CTLD_QUEUE_PROCESSING)) {
		unlock_slurmctld(job_write_lock);
		_throttle_fini(&active_rpc_cnt);
	}

send_msg:
	END_TIMER2(__func__);

	if (reject_job) {
		info("%s: %s", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		info("%s: JobId=%u InitPrio=%u %s",
		     __func__, job_id, priority, TIME_STR);
		/* send job_ID */
		memset(&submit_msg, 0, sizeof(submit_msg));
		submit_msg.job_id     = job_id;
		submit_msg.step_id    = SLURM_BATCH_SCRIPT;
		submit_msg.error_code = error_code;
		submit_msg.job_submit_user_msg = job_submit_user_msg;
		response_init(&response_msg, msg, RESPONSE_SUBMIT_BATCH_JOB,
			      &submit_msg);
		slurm_send_node_msg(msg->conn_fd, &response_msg);

		schedule_job_save();	/* Has own locks */
		schedule_node_save();	/* Has own locks */
		queue_job_scheduler();
	}
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_submit_batch_het_job - process RPC to submit a batch hetjob */
static void _slurm_rpc_submit_batch_het_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	ListIterator iter;
	int error_code = SLURM_SUCCESS, alloc_only = 0;
	DEF_TIMERS;
	uint32_t het_job_id = 0, het_job_offset = 0;
	job_record_t *job_ptr = NULL, *first_job_ptr = NULL;
	slurm_msg_t response_msg;
	submit_response_msg_t submit_msg;
	job_desc_msg_t *job_desc_msg;
	char *script = NULL;
	/* Locks: Read config, read job, read node, read partition */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node, read partition, read fed */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	list_t *job_req_list = msg->data;
	gid_t gid = auth_g_get_gid(msg->auth_cred);
	uint32_t job_uid = NO_VAL;
	char *err_msg = NULL, *job_submit_user_msg = NULL;
	bool reject_job = false;
	List submit_job_list = NULL;
	hostset_t jobid_hostset = NULL;
	char tmp_str[32];
	char *het_job_id_set = NULL;

	START_TIMER;
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u with empty job list",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		reject_job = true;
		goto send_msg;
	}
	if (!_sched_backfill()) {
		info("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u rejected as sched/backfill is not configured",
		     msg->auth_uid);
		error_code = ESLURM_NOT_SUPPORTED;
		reject_job = true;
		goto send_msg;
	}
	if (slurmctld_config.submissions_disabled) {
		info("Submissions disabled on system");
		error_code = ESLURM_SUBMISSIONS_DISABLED;
		reject_job = true;
		goto send_msg;
	}
	if (!job_req_list || (list_count(job_req_list) == 0)) {
		info("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u with empty job list",
		     msg->auth_uid);
		error_code = SLURM_ERROR;
		reject_job = true;
		goto send_msg;
	}

	/*
	 * If any job component has required nodes, those nodes must be excluded
	 * from all other components to avoid scheduling deadlock
	 */
	_exclude_het_job_nodes(job_req_list);

	/* Validate the individual request */
	lock_slurmctld(job_read_lock);     /* Locks for job_submit plugin use */
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (job_uid == NO_VAL)
			job_uid = job_desc_msg->user_id;

		if ((error_code = _valid_id("REQUEST_SUBMIT_BATCH_JOB",
					    job_desc_msg, msg->auth_uid, gid,
					    msg->protocol_version))) {
			reject_job = true;
			break;
		}

		_set_hostname(msg, &job_desc_msg->alloc_node);

		if ((job_desc_msg->alloc_node == NULL) ||
		    (job_desc_msg->alloc_node[0] == '\0')) {
			error("REQUEST_SUBMIT_BATCH_HET_JOB lacks alloc_node from uid=%u",
			      msg->auth_uid);
			error_code = ESLURM_INVALID_NODE_NAME;
			break;
		}

		dump_job_desc(job_desc_msg);

		job_desc_msg->het_job_offset = het_job_offset;
		error_code = validate_job_create_req(job_desc_msg,
						     msg->auth_uid,
						     &err_msg);
		if (err_msg) {
			char *save_ptr = NULL, *tok;
			tok = strtok_r(err_msg, "\n", &save_ptr);
			while (tok) {
				char *sep = "";
				if (job_submit_user_msg)
					sep = "\n";
				xstrfmtcat(job_submit_user_msg, "%s%d: %s",
					   sep, het_job_offset, tok);
				tok = strtok_r(NULL, "\n", &save_ptr);
			}
			xfree(err_msg);
		}

		if (error_code != SLURM_SUCCESS) {
			reject_job = true;
			break;
		}

		/* license request allowed only on leader */
		if (het_job_offset && job_desc_msg->licenses) {
			xstrfmtcat(job_submit_user_msg,
				   "%s%d: license request allowed only on leader job",
				   job_submit_user_msg ? "\n" : "",
				   het_job_offset);
			error("REQUEST_SUBMIT_BATCH_HET_JOB from uid=%u, license request on non-leader job",
			      msg->auth_uid);
			error_code = ESLURM_INVALID_LICENSES;
			reject_job = true;
			break;
		}

		het_job_offset++;
	}
	list_iterator_destroy(iter);
	unlock_slurmctld(job_read_lock);
	if (error_code != SLURM_SUCCESS)
		goto send_msg;

	/*
	 * In validate_job_create_req, err_msg is currently only modified in
	 * the call to job_submit_g_submit. We save the err_msg in a temp
	 * char *job_submit_user_msg because err_msg can be overwritten later
	 * in the calls to job_allocate, and we need the job submit plugin value
	 * to build the resource allocation response in the call to
	 * build_alloc_msg.
	 */
	if (err_msg) {
		job_submit_user_msg = err_msg;
		err_msg = NULL;
	}

	/* Create new job allocations */
	submit_job_list = list_create(NULL);
	het_job_offset = 0;
	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(job_write_lock);
	START_TIMER;	/* Restart after we have locks */
	iter = list_iterator_create(job_req_list);
	while ((job_desc_msg = list_next(iter))) {
		if (!script)
			script = xstrdup(job_desc_msg->script);
		if (het_job_offset && job_desc_msg->script) {
			info("%s: Hetjob %u offset %u has script, being ignored",
			     __func__, het_job_id, het_job_offset);
			xfree(job_desc_msg->script);

		}
		if (het_job_offset) {
			/*
			 * Email notifications disabled except for
			 * hetjob leader
			 */
			job_desc_msg->mail_type = 0;
			xfree(job_desc_msg->mail_user);
		}
		if (!job_desc_msg->burst_buffer) {
			xfree(job_desc_msg->script);
			if (!(job_desc_msg->script = bb_g_build_het_job_script(
				      script, het_job_offset))) {
				error_code =
					ESLURM_INVALID_BURST_BUFFER_REQUEST;
				reject_job = true;
				break;
			}
		}
		job_desc_msg->het_job_offset = het_job_offset;
		error_code = job_allocate(job_desc_msg,
					  job_desc_msg->immediate, false,
					  NULL, alloc_only, msg->auth_uid,
					  false, &job_ptr, &err_msg,
					  msg->protocol_version);
		if (!job_ptr ||
		    (error_code && job_ptr->job_state == JOB_FAILED)) {
			reject_job = true;
		} else {
			if (het_job_id == 0) {
				het_job_id = job_ptr->job_id;
				first_job_ptr = job_ptr;
				alloc_only = 1;
			}
			snprintf(tmp_str, sizeof(tmp_str), "%u",
				 job_ptr->job_id);
			if (jobid_hostset)
				hostset_insert(jobid_hostset, tmp_str);
			else
				jobid_hostset = hostset_create(tmp_str);
			job_ptr->het_job_id     = het_job_id;
			job_ptr->het_job_offset = het_job_offset++;
			job_ptr->batch_flag      = 1;
			list_append(submit_job_list, job_ptr);
		}

		if (job_desc_msg->immediate &&
		    (error_code != SLURM_SUCCESS)) {
			error_code = ESLURM_CAN_NOT_START_IMMEDIATELY;
			reject_job = true;
		}
		if (reject_job)
			break;
	}
	list_iterator_destroy(iter);
	xfree(script);

	if ((het_job_id == 0) && !reject_job) {
		info("%s: No error, but no het_job_id", __func__);
		error_code = SLURM_ERROR;
		reject_job = true;
	}

	/* Validate limits on hetjob as a whole */
	if (!reject_job &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    !acct_policy_validate_het_job(submit_job_list)) {
		info("Hetjob JobId=%u exceeded association/QOS limit for user %u",
		     het_job_id, job_uid);
		error_code = ESLURM_ACCOUNTING_POLICY;
		reject_job = true;
	}

	_create_het_job_id_set(jobid_hostset, het_job_offset,
			       &het_job_id_set);
	if (first_job_ptr)
		first_job_ptr->het_job_list = submit_job_list;

	iter = list_iterator_create(submit_job_list);
	while ((job_ptr = list_next(iter))) {
		job_ptr->het_job_id_set = xstrdup(het_job_id_set);
		if (error_code == SLURM_SUCCESS)
			log_flag(HETJOB, "Submit %pJ", job_ptr);
	}
	list_iterator_destroy(iter);
	xfree(het_job_id_set);

	unlock_slurmctld(job_write_lock);
	_throttle_fini(&active_rpc_cnt);

send_msg:
	END_TIMER2(__func__);
	if (reject_job) {
		info("%s: %s", __func__, slurm_strerror(error_code));

		/*
		 * If job is rejected, add the job submit message to the error
		 * message to avoid it getting lost. Was saved off earlier.
		 */
		if (job_submit_user_msg) {
			char *tmp_err_msg = err_msg;
			err_msg = job_submit_user_msg;
			job_submit_user_msg = NULL;
			if (tmp_err_msg) {
				xstrfmtcat(err_msg, "\n%s", tmp_err_msg);
				xfree(tmp_err_msg);
			}
		}

		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
		if (submit_job_list) {
			(void) list_for_each(submit_job_list, _het_job_cancel,
					     NULL);
			if (first_job_ptr)
				first_job_ptr->het_job_list = submit_job_list;
			else
				FREE_NULL_LIST(submit_job_list);
		}
	} else {
		info("%s: JobId=%u %s", __func__, het_job_id, TIME_STR);
		/* send job_ID */
		memset(&submit_msg, 0, sizeof(submit_msg));
		submit_msg.job_id     = het_job_id;
		submit_msg.step_id    = SLURM_BATCH_SCRIPT;
		submit_msg.error_code = error_code;
		submit_msg.job_submit_user_msg = job_submit_user_msg;
		response_init(&response_msg, msg, RESPONSE_SUBMIT_BATCH_JOB,
			      &submit_msg);
		slurm_send_node_msg(msg->conn_fd, &response_msg);

		schedule_job_save();	/* Has own locks */
	}
	if (jobid_hostset)
		hostset_destroy(jobid_hostset);
	xfree(err_msg);
	xfree(job_submit_user_msg);
}

/* _slurm_rpc_update_job - process RPC to update the configuration of a
 * job (e.g. priority)
 */
static void _slurm_rpc_update_job(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	job_desc_msg_t *job_desc_msg = msg->data;
	/* Locks: Read config, write job, write node, read partition, read fed*/
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = msg->auth_uid;

	lock_slurmctld(fed_read_lock);
	if (!_route_msg_to_origin(msg, job_desc_msg->job_id_str,
				  job_desc_msg->job_id)) {
		unlock_slurmctld(fed_read_lock);
		return;
	}
	unlock_slurmctld(fed_read_lock);

	START_TIMER;
	if ((job_desc_msg->user_id == NO_VAL) &&
	    (msg->protocol_version < SLURM_23_02_PROTOCOL_VERSION)) {
		/* older scontrol used NO_VAL instead of SLURM_AUTH_NOBODY */
		job_desc_msg->user_id = SLURM_AUTH_NOBODY;
	}
	if ((job_desc_msg->group_id == NO_VAL) &&
	    (msg->protocol_version < SLURM_23_02_PROTOCOL_VERSION)) {
		/* older scontrol used NO_VAL instead of SLURM_AUTH_NOBODY */
		job_desc_msg->group_id = SLURM_AUTH_NOBODY;
	}

	/* job_desc_msg->user_id is set when the uid has been overriden with
	 * -u <uid> or --uid=<uid>. NO_VAL is default. Verify the request has
	 * come from an admin */
	if (job_desc_msg->user_id != SLURM_AUTH_NOBODY) {
		if (!validate_super_user(uid)) {
			error_code = ESLURM_USER_ID_MISSING;
			error("Security violation, REQUEST_UPDATE_JOB RPC from uid=%u",
			      uid);
			/* Send back the error message for this case because
			 * update_job also sends back an error message */
			slurm_send_rc_msg(msg, error_code);
		} else {
			/* override uid allowed */
			uid = job_desc_msg->user_id;
		}
	}

	if (error_code == SLURM_SUCCESS) {
		int db_inx_max_cnt = 5, i=0;
		/* do RPC call */
		dump_job_desc(job_desc_msg);
		/* Ensure everything that may be written to database is lower
		 * case */
		xstrtolower(job_desc_msg->account);
		xstrtolower(job_desc_msg->wckey);
		error_code = ESLURM_JOB_SETTING_DB_INX;
		while (error_code == ESLURM_JOB_SETTING_DB_INX) {
			lock_slurmctld(job_write_lock);
			/* Use UID provided by scontrol. May be overridden with
			 * -u <uid>  or --uid=<uid> */
			if (job_desc_msg->job_id_str)
				error_code = update_job_str(msg, uid);
			else
				error_code = update_job(msg, uid, true);
			unlock_slurmctld(job_write_lock);
			if (error_code == ESLURM_JOB_SETTING_DB_INX) {
				if (i >= db_inx_max_cnt) {
					if (job_desc_msg->job_id_str) {
						info("%s: can't update job, waited %d seconds for JobId=%s to get a db_index, but it hasn't happened yet. Giving up and informing the user",
						      __func__, db_inx_max_cnt,
						      job_desc_msg->job_id_str);
					} else {
						info("%s: can't update job, waited %d seconds for JobId=%u to get a db_index, but it hasn't happened yet. Giving up and informing the user",
						      __func__, db_inx_max_cnt,
						      job_desc_msg->job_id);
					}
					slurm_send_rc_msg(msg, error_code);
					break;
				}
				i++;
				if (job_desc_msg->job_id_str) {
					debug("%s: We cannot update JobId=%s at the moment, we are setting the db index, waiting",
					      __func__,
					      job_desc_msg->job_id_str);
				} else {
					debug("%s: We cannot update JobId=%u at the moment, we are setting the db index, waiting",
					      __func__, job_desc_msg->job_id);
				}
				sleep(1);
			}
		}
	}
	END_TIMER2(__func__);

	/* return result */
	if (error_code) {
		if (job_desc_msg->job_id_str) {
			info("%s: JobId=%s uid=%u: %s",
			     __func__, job_desc_msg->job_id_str, uid,
			     slurm_strerror(error_code));
		} else {
			info("%s: JobId=%u uid=%u: %s",
			     __func__, job_desc_msg->job_id, uid,
			     slurm_strerror(error_code));
		}
	} else {
		if (job_desc_msg->job_id_str) {
			info("%s: complete JobId=%s uid=%u %s",
			     __func__, job_desc_msg->job_id_str, uid, TIME_STR);
		} else {
			info("%s: complete JobId=%u uid=%u %s",
			     __func__, job_desc_msg->job_id, uid, TIME_STR);
		}
		/* Below functions provide their own locking */
		schedule_job_save();
		schedule_node_save();
		queue_job_scheduler();
	}
}

/*
 * _slurm_rpc_update_front_end - process RPC to update the configuration of a
 *	front_end node (e.g. UP/DOWN)
 */
static void _slurm_rpc_update_front_end(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_front_end_msg_t *update_front_end_msg_ptr = msg->data;
	/* Locks: write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_FRONT_END RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_front_end(update_front_end_msg_ptr,
					      msg->auth_uid);
		unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, update_front_end_msg_ptr->name,
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, update_front_end_msg_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

/*
 * _slurm_rpc_create_node - process RPC to create node(s).
 */
static void _slurm_rpc_create_node(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_node_msg_t *node_msg = msg->data;
	char *err_msg = NULL;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_NODE RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = create_nodes(node_msg->extra, &err_msg);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, node_msg->node_names,
		     slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, node_msg->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);

	/* Below functions provide their own locks */
	schedule_node_save();
	validate_all_reservations(false);
	queue_job_scheduler();
	trigger_reconfig();
}

/*
 * _slurm_rpc_update_node - process RPC to update the configuration of a
 *	node (e.g. UP/DOWN)
 */
static void _slurm_rpc_update_node(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_node_msg_t *update_node_msg_ptr = msg->data;
	/* Locks: Write job, partition and node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_NODE RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = update_node(update_node_msg_ptr, msg->auth_uid);
		unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, update_node_msg_ptr->node_names,
		     slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, update_node_msg_ptr->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

	/* Below functions provide their own locks */
	schedule_node_save();
	validate_all_reservations(false);
	queue_job_scheduler();
	trigger_reconfig();
}

/*
 * _slurm_rpc_delete_node - process RPC to delete node.
 */
static void _slurm_rpc_delete_node(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	update_node_msg_t *node_msg = msg->data;
	char *err_msg = NULL;
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_NODE RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = delete_nodes(node_msg->node_names, &err_msg);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s for %s: %s",
		     __func__, node_msg->node_names,
		     slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, node_msg->node_names, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
	xfree(err_msg);

	/* Below functions provide their own locks */
	schedule_node_save();
	validate_all_reservations(false);
	queue_job_scheduler();
	trigger_reconfig();
}

/* _slurm_rpc_update_partition - process RPC to update the configuration
 *	of a partition (e.g. UP/DOWN) */
static void _slurm_rpc_update_partition(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	update_part_msg_t *part_desc_ptr = msg->data;
	/* Locks: Read config, write job, write node, write partition
	 * NOTE: job write lock due to gang scheduler support */
	slurmctld_lock_t part_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, UPDATE_PARTITION RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		if (msg->msg_type == REQUEST_CREATE_PARTITION) {
			lock_slurmctld(part_write_lock);
			error_code = update_part(part_desc_ptr, true);
			unlock_slurmctld(part_write_lock);
		} else {
			lock_slurmctld(part_write_lock);
			error_code = update_part(part_desc_ptr, false);
			unlock_slurmctld(part_write_lock);
		}
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s partition=%s: %s",
		     __func__, part_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s complete for %s %s",
		       __func__, part_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		schedule_part_save();		/* Has its locking */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_delete_partition - process RPC to delete a partition */
static void _slurm_rpc_delete_partition(slurm_msg_t *msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	delete_part_msg_t *part_desc_ptr = msg->data;
	/* Locks: write job, write node, write partition */
	slurmctld_lock_t part_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_PARTITION RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(part_write_lock);
		error_code = delete_partition(part_desc_ptr);
		unlock_slurmctld(part_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		info("%s partition=%s: %s",
		     __func__, part_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("%s complete for %s %s",
		     __func__, part_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		save_all_state();	/* Has own locking */
		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_create - process RPC to create a reservation */
static void _slurm_rpc_resv_create(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	char *err_msg = NULL;
	DEF_TIMERS;
	resv_desc_msg_t *resv_desc_ptr = msg->data;
	/* Locks: read config, read job, write node, read partition */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	if (!validate_operator(msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, CREATE_RESERVATION RPC from uid=%u",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		lock_slurmctld(node_write_lock);
		error_code = create_resv(resv_desc_ptr, &err_msg);
		unlock_slurmctld(node_write_lock);
		END_TIMER2(__func__);
	}

	/* return result */
	if (error_code) {
		if (resv_desc_ptr->name) {
			info("%s reservation=%s: %s",
			     __func__, resv_desc_ptr->name,
			     slurm_strerror(error_code));
		} else {
			info("%s: %s", __func__, slurm_strerror(error_code));
		}
		slurm_send_rc_err_msg(msg, error_code, err_msg);
	} else {
		slurm_msg_t response_msg;
		reservation_name_msg_t resv_resp_msg;

		debug2("%s complete for %s %s",
		       __func__, resv_desc_ptr->name, TIME_STR);
		/* send reservation name */
		memset(&resv_resp_msg, 0, sizeof(resv_resp_msg));
		resv_resp_msg.name    = resv_desc_ptr->name;
		response_init(&response_msg, msg, RESPONSE_CREATE_RESERVATION,
			      &resv_resp_msg);
		slurm_send_node_msg(msg->conn_fd, &response_msg);

		queue_job_scheduler();
	}
	xfree(err_msg);
}

/* _slurm_rpc_resv_update - process RPC to update a reservation */
static void _slurm_rpc_resv_update(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	char *err_msg = NULL;
	DEF_TIMERS;
	resv_desc_msg_t *resv_desc_ptr = msg->data;
	/* Locks: read config, read job, write node, read partition */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(node_write_lock);
	if (!validate_operator(msg->auth_uid)) {
		if (!validate_resv_uid(resv_desc_ptr->name, msg->auth_uid) ||
		    !(resv_desc_ptr->flags & RESERVE_FLAG_SKIP)) {
			error_code = ESLURM_USER_ID_MISSING;
			error("Security violation, UPDATE_RESERVATION RPC from uid=%u",
			      msg->auth_uid);
		} else {
			resv_desc_msg_t *resv_desc_ptr2 =
				xmalloc_nz(sizeof(*resv_desc_ptr2));
			slurm_init_resv_desc_msg(resv_desc_ptr2);
			/*
			 * Santitize the structure since a regular user is doing
			 * this and is only allowed to skip the reservation and
			 * not update anything else.
			 */
			resv_desc_ptr2->name = resv_desc_ptr->name;
			resv_desc_ptr->name = NULL;
			resv_desc_ptr2->flags = RESERVE_FLAG_SKIP;
			slurm_free_resv_desc_msg(resv_desc_ptr);
			resv_desc_ptr = msg->data = resv_desc_ptr2;
		}
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		error_code = update_resv(resv_desc_ptr, &err_msg);
		END_TIMER2(__func__);
	}
	unlock_slurmctld(node_write_lock);

	/* return result */
	if (error_code) {
		info("%s reservation=%s: %s",
		     __func__, resv_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_err_msg(msg, error_code, err_msg);
	} else {
		debug2("%s complete for %s %s",
		       __func__, resv_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		queue_job_scheduler();
	}
	xfree(err_msg);
}

/* _slurm_rpc_resv_delete - process RPC to delete a reservation */
static void _slurm_rpc_resv_delete(slurm_msg_t *msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	reservation_name_msg_t *resv_desc_ptr = msg->data;
	/* Locks: write job, write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	/* node_write_lock needed for validate_resv_uid */
	lock_slurmctld(node_write_lock);
	if (!validate_operator(msg->auth_uid) &&
	    !validate_resv_uid(resv_desc_ptr->name, msg->auth_uid)) {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, DELETE_RESERVATION RPC from uid=%u",
		      msg->auth_uid);
	} else if (!resv_desc_ptr->name) {
		error_code = ESLURM_INVALID_PARTITION_NAME;
		error("Invalid DELETE_RESERVATION RPC from uid=%u, name is null",
		      msg->auth_uid);
	}

	if (error_code == SLURM_SUCCESS) {
		/* do RPC call */
		error_code = delete_resv(resv_desc_ptr);
		END_TIMER2(__func__);
	}
	unlock_slurmctld(node_write_lock);

	/* return result */
	if (error_code) {
		info("%s reservation=%s: %s",
		     __func__, resv_desc_ptr->name, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("%s complete for %s %s",
		     __func__, resv_desc_ptr->name, TIME_STR);
		slurm_send_rc_msg(msg, SLURM_SUCCESS);

		queue_job_scheduler();
	}
}

/* _slurm_rpc_resv_show - process RPC to dump reservation info */
static void _slurm_rpc_resv_show(slurm_msg_t *msg)
{
	resv_info_request_msg_t *resv_req_msg = msg->data;
	DEF_TIMERS;
	/* Locks: read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	slurm_msg_t response_msg;
	char *dump;
	int dump_size;

	START_TIMER;
	if ((resv_req_msg->last_update - 1) >= last_resv_update) {
		debug2("%s, no change", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);
	} else {
		lock_slurmctld(node_read_lock);
		show_resv(&dump, &dump_size, msg->auth_uid,
			  msg->protocol_version);
		unlock_slurmctld(node_read_lock);
		END_TIMER2(__func__);

		response_init(&response_msg, msg, RESPONSE_RESERVATION_INFO,
			      dump);
		response_msg.data_size = dump_size;

		/* send message */
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(dump);
	}
}

static void _slurm_rpc_node_registration_status(slurm_msg_t *msg)
{
	error("slurmctld is talking with itself. SlurmctldPort == SlurmdPort");
	slurm_send_rc_msg(msg, EINVAL);
}

/* determine of nodes are ready for the job */
static void _slurm_rpc_job_ready(slurm_msg_t *msg)
{
	int error_code, result;
	job_id_msg_t *id_msg = msg->data;
	DEF_TIMERS;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	slurm_msg_t response_msg;
	return_code_msg_t rc_msg;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	error_code = job_node_ready(id_msg->job_id, &result);
	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (error_code) {
		debug2("%s: %s", __func__, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		debug2("%s(%u)=%d %s",
		       __func__, id_msg->job_id, result, TIME_STR);
		memset(&rc_msg, 0, sizeof(rc_msg));
		rc_msg.return_code = result;
		response_init(&response_msg, msg, RESPONSE_JOB_READY, &rc_msg);
		if (!_is_prolog_finished(id_msg->job_id))
			response_msg.msg_type = RESPONSE_PROLOG_EXECUTING;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
	}
}

/* Check if prolog has already finished */
static int _is_prolog_finished(uint32_t job_id)
{
	int is_running = 0;
	job_record_t *job_ptr;

	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	lock_slurmctld(job_read_lock);
	job_ptr = find_job_record(job_id);
	if (job_ptr) {
		is_running = (job_ptr->state_reason != WAIT_PROLOG);
	}
	unlock_slurmctld(job_read_lock);
	return is_running;
}

/* get node select info plugin */
static void _slurm_rpc_burst_buffer_info(slurm_msg_t *msg)
{
	void *resp_buffer = NULL;
	int resp_buffer_size = 0;
	int error_code = SLURM_SUCCESS;
	buf_t *buffer;
	uid_t uid = msg->auth_uid;
	DEF_TIMERS;

	START_TIMER;
	buffer = init_buf(BUF_SIZE);
	if (validate_super_user(msg->auth_uid))
		uid = 0;
	error_code = bb_g_state_pack(uid, buffer, msg->protocol_version);
	END_TIMER2(__func__);

	if (error_code) {
		debug("%s: %s", __func__, slurm_strerror(error_code));
		slurm_send_rc_msg(msg, error_code);
	} else {
		slurm_msg_t response_msg;

		resp_buffer_size = get_buf_offset(buffer);
		resp_buffer = xfer_buf_data(buffer);
		response_init(&response_msg, msg, RESPONSE_BURST_BUFFER_INFO,
			      resp_buffer);
		response_msg.data_size = resp_buffer_size;
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		xfree(resp_buffer);
	}
}

static void _slurm_rpc_suspend(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	suspend_msg_t *sus_ptr = msg->data;
	/* Locks: write job and node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr;
	char *op;

	START_TIMER;
	switch (sus_ptr->op) {
	case SUSPEND_JOB:
		op = "suspend";
		break;
	case RESUME_JOB:
		op = "resume";
		break;
	default:
		op = "unknown";
	}
	debug3("Processing RPC details: REQUEST_SUSPEND(%s)", op);

	/* Get the job id part of the jobid. It could be an array id. Currently
	 * in a federation, job arrays only run on the origin cluster so we just
	 * want to find if the array, not a specific task, is on the origin
	 * cluster. */
	if ((sus_ptr->job_id == NO_VAL) && sus_ptr->job_id_str)
		sus_ptr->job_id = strtol(sus_ptr->job_id_str, NULL, 10);

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(sus_ptr->job_id);

	/* If job is found on the cluster, it could be pending, the origin
	 * cluster, or running on the sibling cluster. If it's not there then
	 * route it to the origin, otherwise try to suspend the job. If it's
	 * pending an error should be returned. If it's running then it should
	 * suspend the job. */
	if (!job_ptr && !_route_msg_to_origin(msg, NULL, sus_ptr->job_id)) {
		unlock_slurmctld(job_write_lock);
		return;
	}
	if (!job_ptr)
		error_code = ESLURM_INVALID_JOB_ID;
	else if (fed_mgr_job_started_on_sib(job_ptr)) {

		/* Route to the cluster that is running the job. */
		slurmdb_cluster_rec_t *dst =
			fed_mgr_get_cluster_by_id(
					job_ptr->fed_details->cluster_lock);
		if (dst) {
			slurm_send_reroute_msg(msg, dst);
			info("%s: %s %pJ uid %u routed to %s",
			     __func__, rpc_num2string(msg->msg_type),
			     job_ptr, msg->auth_uid, dst->name);

			unlock_slurmctld(job_write_lock);
			END_TIMER2(__func__);
			return;
		}

		error("couldn't find cluster by cluster id %d",
		      job_ptr->fed_details->cluster_lock);
		error_code = ESLURM_INVALID_CLUSTER_NAME;
	} else if (sus_ptr->job_id_str) {
		error_code = job_suspend2(sus_ptr, msg->auth_uid, msg->conn_fd,
					  true, msg->protocol_version);
	} else {
		error_code = job_suspend(sus_ptr, msg->auth_uid, msg->conn_fd,
					 true, msg->protocol_version);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	if (!sus_ptr->job_id_str)
		xstrfmtcat(sus_ptr->job_id_str, "%u", sus_ptr->job_id);

	if (error_code) {
		info("%s(%s) for %s %s",
		     __func__, op, sus_ptr->job_id_str,
		     slurm_strerror(error_code));
	} else {
		info("%s(%s) for %s %s",
		     __func__, op, sus_ptr->job_id_str, TIME_STR);

		schedule_job_save();	/* Has own locking */
		if (sus_ptr->op == SUSPEND_JOB)
			queue_job_scheduler();
	}
}

static void _slurm_rpc_top_job(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	top_job_msg_t *top_ptr = msg->data;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	START_TIMER;
	lock_slurmctld(job_write_lock);
	error_code = job_set_top(top_ptr, msg->auth_uid, msg->conn_fd,
				 msg->protocol_version);
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	if (error_code) {
		info("%s for %s %s",
		     __func__, top_ptr->job_id_str, slurm_strerror(error_code));
	} else {
		info("%s for %s %s",
		     __func__, top_ptr->job_id_str, TIME_STR);
	}
}

static void _slurm_rpc_auth_token(slurm_msg_t *msg)
{
	DEF_TIMERS;
	token_request_msg_t *request_msg = msg->data;
	slurm_msg_t response_msg;
	token_response_msg_t *resp_data;
	char *auth_username = NULL, *username = NULL;
	int lifespan = 0;
	static int max_lifespan = -1;

	START_TIMER;
	if (xstrstr(slurm_conf.authalt_params, "disable_token_creation") &&
	    !validate_slurm_user(msg->auth_uid)) {
		error("%s: attempt to retrieve a token while token creation disabled UID=%u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	if (!auth_is_plugin_type_inited(AUTH_PLUGIN_JWT)) {
		slurm_send_rc_msg(msg, ESLURM_PLUGIN_NOT_LOADED);
		return;
	}

	if (max_lifespan == -1) {
		char *tmp_ptr;

		max_lifespan = 0;
		if ((tmp_ptr = xstrcasestr(slurm_conf.authalt_params,
					   "max_token_lifespan="))) {
			max_lifespan = atoi(tmp_ptr + 19);
			if (max_lifespan < 1) {
				error("Invalid AuthAltParameters max_token_lifespan option, no limit enforced");
				max_lifespan = 0;
			}
		}
	}

	auth_username = uid_to_string_or_null(msg->auth_uid);

	if (request_msg->username) {
		if (validate_slurm_user(msg->auth_uid)) {
			username = request_msg->username;
		} else if (!xstrcmp(request_msg->username, auth_username)) {
			/* user explicitly provided their own username */
			username = request_msg->username;
		} else {
			error("%s: attempt to retrieve a token for a different user=%s by UID=%u",
			      __func__, request_msg->username, msg->auth_uid);
			xfree(auth_username);
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
			return;
		}
	} else if (!auth_username) {
		error("%s: attempt to retrieve a token for a missing username by UID=%u",
		      __func__, msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	} else {
		username = auth_username;
	}

	if (request_msg->lifespan)
		lifespan = request_msg->lifespan;
	else if (max_lifespan)
		lifespan = MIN(DEFAULT_AUTH_TOKEN_LIFESPAN, max_lifespan);
	else
		lifespan = DEFAULT_AUTH_TOKEN_LIFESPAN;

	if (!validate_slurm_user(msg->auth_uid)) {
		if ((max_lifespan > 0) && (lifespan > max_lifespan)) {
			error("%s: rejecting token lifespan %d for user:%s[%d] requested, exceeds limit of %d",
			      __func__, request_msg->lifespan, username,
			      msg->auth_uid, max_lifespan);
			xfree(auth_username);
			slurm_send_rc_msg(msg, ESLURM_INVALID_TIME_LIMIT);
			return;
		}
	}

	resp_data = xmalloc(sizeof(*resp_data));
	resp_data->token = auth_g_token_generate(AUTH_PLUGIN_JWT, username,
						 lifespan);
	xfree(auth_username);
	END_TIMER2(__func__);

	if (!resp_data->token) {
		error("%s: error generating auth token: %m", __func__);
		xfree(resp_data);
		slurm_send_rc_msg(msg, ESLURM_AUTH_UNABLE_TO_GENERATE_TOKEN);
		return;
	}

	response_init(&response_msg, msg, RESPONSE_AUTH_TOKEN, resp_data);
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_free_token_response_msg(resp_data);
}

static void _slurm_rpc_requeue(slurm_msg_t *msg)
{
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	requeue_msg_t *req_ptr = msg->data;
	/* Locks: write job and node */
	slurmctld_lock_t fed_read_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

	lock_slurmctld(fed_read_lock);
	if (!_route_msg_to_origin(msg, req_ptr->job_id_str, req_ptr->job_id)) {
		unlock_slurmctld(fed_read_lock);
		return;
	}
	unlock_slurmctld(fed_read_lock);

	/*
	 * Pre-23.02 slurmd would send either JOB_PENDING or
	 * (JOB_REQUEUE_HOLD | JOB_LAUNCH_FAILED) from _launch_job_fail()
	 * depending on whether nohold_on_prolog_fail was set.
	 * (Handling for that option is now in _job_requeue_op().)
	 *
	 * Fortunately nothing else used the JOB_PENDING or JOB_LAUNCH_FAILED
	 * flags so we can safely normalize to the new 23.02 behavior here.
	 *
	 * Remove this two versions after 23.02.
	 */
	if (msg->protocol_version <= SLURM_22_05_PROTOCOL_VERSION) {
		if ((req_ptr->flags == JOB_PENDING) ||
		    (req_ptr->flags & JOB_LAUNCH_FAILED))
			req_ptr->flags = JOB_LAUNCH_FAILED;
	}

	START_TIMER;
	lock_slurmctld(job_write_lock);
	if (req_ptr->job_id_str) {
		error_code = job_requeue2(msg->auth_uid, req_ptr, msg, false);
	} else {
		error_code = job_requeue(msg->auth_uid, req_ptr->job_id, msg,
					 false, req_ptr->flags);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	if (error_code) {
		if (!req_ptr->job_id_str)
			xstrfmtcat(req_ptr->job_id_str, "%u", req_ptr->job_id);

		info("%s: Requeue of JobId=%s returned an error: %s",
		     __func__, req_ptr->job_id_str, slurm_strerror(error_code));
	}

	/* Functions below provide their own locking
	 */
	schedule_job_save();
}

/* Copy an array of type char **, xmalloc() the array and xstrdup() the
 * strings in the array */
extern char **
xduparray(uint32_t size, char ** array)
{
	int i;
	char ** result;

	if (size == 0)
		return (char **)NULL;

	result = xcalloc(size, sizeof(char *));
	for (i=0; i<size; i++)
		result[i] = xstrdup(array[i]);

	return result;
}

static void _slurm_rpc_trigger_clear(slurm_msg_t *msg)
{
	int rc;
	trigger_info_msg_t *trigger_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	rc = trigger_clear(msg->auth_uid, trigger_ptr);
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_trigger_get(slurm_msg_t *msg)
{
	trigger_info_msg_t *resp_data;
	trigger_info_msg_t *trigger_ptr = msg->data;
	slurm_msg_t response_msg;
	DEF_TIMERS;

	START_TIMER;
	resp_data = trigger_get(msg->auth_uid, trigger_ptr);
	END_TIMER2(__func__);

	response_init(&response_msg, msg, RESPONSE_TRIGGER_GET, resp_data);
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_free_trigger_msg(resp_data);
}

static void _slurm_rpc_trigger_set(slurm_msg_t *msg)
{
	int rc;
	gid_t gid = auth_g_get_gid(msg->auth_cred);
	trigger_info_msg_t *trigger_ptr = msg->data;
	bool allow_user_triggers = xstrcasestr(slurm_conf.slurmctld_params,
	                                       "allow_user_triggers");
	DEF_TIMERS;

	START_TIMER;
	if (validate_slurm_user(msg->auth_uid) || allow_user_triggers) {
		rc = trigger_set(msg->auth_uid, gid, trigger_ptr);
	} else {
		rc = ESLURM_ACCESS_DENIED;
		error("Security violation, REQUEST_TRIGGER_SET RPC from uid=%u",
		      msg->auth_uid);
	}
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_trigger_pull(slurm_msg_t *msg)
{
	int rc;
	trigger_info_msg_t *trigger_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	/* NOTE: No locking required here, trigger_pull only needs to lock
	 * it's own internal trigger structure */
	if (!validate_slurm_user(msg->auth_uid)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_TRIGGER_PULL RPC from uid=%u",
		      msg->auth_uid);
	} else
		rc = trigger_pull(trigger_ptr);
	END_TIMER2(__func__);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_get_topo(slurm_msg_t *msg)
{
	topo_info_response_msg_t *topo_resp_msg;
	slurm_msg_t response_msg;
	int i;
	/* Locks: read node lock */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;

	START_TIMER;
	lock_slurmctld(node_read_lock);
	topo_resp_msg = xmalloc(sizeof(topo_info_response_msg_t));
	topo_resp_msg->record_count = switch_record_cnt;
	topo_resp_msg->topo_array = xmalloc(sizeof(topo_info_t) *
					    topo_resp_msg->record_count);
	for (i=0; i<topo_resp_msg->record_count; i++) {
		topo_resp_msg->topo_array[i].level      =
			switch_record_table[i].level;
		topo_resp_msg->topo_array[i].link_speed =
			switch_record_table[i].link_speed;
		topo_resp_msg->topo_array[i].name       =
			xstrdup(switch_record_table[i].name);
		topo_resp_msg->topo_array[i].nodes      =
			xstrdup(switch_record_table[i].nodes);
		topo_resp_msg->topo_array[i].switches   =
			xstrdup(switch_record_table[i].switches);
	}
	unlock_slurmctld(node_read_lock);
	END_TIMER2(__func__);

	response_init(&response_msg, msg, RESPONSE_TOPO_INFO, topo_resp_msg);
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	slurm_free_topo_info_msg(topo_resp_msg);
}

static void _slurm_rpc_job_notify(slurm_msg_t *msg)
{
	int error_code;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	job_notify_msg_t *notify_msg = msg->data;
	job_record_t *job_ptr;
	DEF_TIMERS;

	START_TIMER;
	lock_slurmctld(job_read_lock);
	job_ptr = find_job_record(notify_msg->step_id.job_id);

	/* If job is found on the cluster, it could be pending, the origin
	 * cluster, or running on the sibling cluster. If it's not there then
	 * route it to the origin. */
	if (!job_ptr &&
	    !_route_msg_to_origin(msg, NULL, notify_msg->step_id.job_id)) {
		unlock_slurmctld(job_read_lock);
		return;
	}

	if (!job_ptr)
		error_code = ESLURM_INVALID_JOB_ID;
	else if (job_ptr->batch_flag &&
		 fed_mgr_job_started_on_sib(job_ptr)) {

		/* Route to the cluster that is running the batch job. srun jobs
		 * don't need to be routed to the running cluster since the
		 * origin cluster knows how to contact the listening srun. */
		slurmdb_cluster_rec_t *dst =
			fed_mgr_get_cluster_by_id(
					job_ptr->fed_details->cluster_lock);
		if (dst) {
			slurm_send_reroute_msg(msg, dst);
			info("%s: %s %pJ uid %u routed to %s",
			     __func__, rpc_num2string(msg->msg_type),
			     job_ptr, msg->auth_uid, dst->name);

			unlock_slurmctld(job_read_lock);
			END_TIMER2(__func__);
			return;
		}

		error("couldn't find cluster by cluster id %d",
		      job_ptr->fed_details->cluster_lock);
		error_code = ESLURM_INVALID_CLUSTER_NAME;

	} else if ((job_ptr->user_id == msg->auth_uid) ||
		   validate_slurm_user(msg->auth_uid))
		error_code = srun_user_message(job_ptr, notify_msg->message);
	else {
		error_code = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_JOB_NOTIFY RPC from uid=%u for %pJ owner %u",
		      msg->auth_uid, job_ptr, job_ptr->user_id);
	}
	unlock_slurmctld(job_read_lock);

	END_TIMER2(__func__);
	slurm_send_rc_msg(msg, error_code);
}

static void _slurm_rpc_set_debug_flags(slurm_msg_t *msg)
{
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, READ_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	set_debug_flags_msg_t *request_msg = msg->data;
	char *flag_string;

	if (!validate_super_user(msg->auth_uid)) {
		error("set debug flags request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	lock_slurmctld (config_write_lock);
	slurm_conf.debug_flags &= (~request_msg->debug_flags_minus);
	slurm_conf.debug_flags |= request_msg->debug_flags_plus;
	slurm_conf.last_update = time(NULL);
	slurmscriptd_update_debug_flags(slurm_conf.debug_flags);

	/* Reset cached debug_flags values */
	gs_reconfig();
	gres_reconfig();
	priority_g_reconfig(false);
	select_g_reconfigure();
	(void) sched_g_reconfig();
	(void) switch_g_reconfig();

	unlock_slurmctld (config_write_lock);
	flag_string = debug_flags2str(slurm_conf.debug_flags);
	info("Set DebugFlags to %s", flag_string ? flag_string : "none");
	xfree(flag_string);
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_debug_level(slurm_msg_t *msg)
{
	int debug_level;
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	set_debug_level_msg_t *request_msg = msg->data;

	if (!validate_super_user(msg->auth_uid)) {
		error("set debug level request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/* NOTE: not offset by LOG_LEVEL_INFO, since it's inconvenient
	 * to provide negative values for scontrol */
	debug_level = MIN (request_msg->debug_level, (LOG_LEVEL_END - 1));
	debug_level = MAX (debug_level, LOG_LEVEL_QUIET);

	lock_slurmctld(config_write_lock);
	update_log_levels(debug_level, debug_level);
	slurmscriptd_update_log_level(debug_level, false);

	info("Set debug level to '%s'", log_num2string(debug_level));

	slurm_conf.slurmctld_debug = debug_level;
	slurm_conf.last_update = time(NULL);
	unlock_slurmctld(config_write_lock);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static char *_update_hostset_from_mode(char *update_str,
				       update_mode_t mode,
				       char *current_str)
{
	char *new_str = NULL;

	if (mode == UPDATE_SET) {
		if (*update_str)
			new_str = xstrdup(update_str);
	} else {
		hostset_t current_hostset = hostset_create(current_str);
		if (mode == UPDATE_ADD) {
			hostset_insert(current_hostset, update_str);
		} else if (mode == UPDATE_REMOVE) {
			hostset_delete(current_hostset, update_str);
		} /* If bad mode is sent do nothing */

		if (hostset_count(current_hostset))
			new_str =
				hostset_ranged_string_xmalloc(current_hostset);
		hostset_destroy(current_hostset);
	}
	return new_str;
}

static char *_update_string_from_mode(char *update_str,
				      update_mode_t mode,
				      char *current_str,
				      bool lower_case_normalization)
{
	char *new_str = NULL;

	if (mode == UPDATE_ADD) {
		if (current_str && *current_str) {
			list_t *current_list = list_create(xfree_ptr);

			slurm_addto_char_list_with_case(
				current_list, current_str,
				lower_case_normalization);
			if (*update_str)
				slurm_addto_char_list_with_case(
					current_list, update_str,
					lower_case_normalization);
			new_str = slurm_char_list_to_xstr(current_list);

			FREE_NULL_LIST(current_list);
		} else if (*update_str) {
			new_str = xstrdup(update_str);
		}
	} else if (mode == UPDATE_REMOVE) {
		if (current_str && *current_str) {
			list_t *current_list = list_create(xfree_ptr);
			list_t *rem_list = list_create(xfree_ptr);

			slurm_addto_char_list_with_case(
				current_list, current_str,
				lower_case_normalization);
			slurm_addto_char_list_with_case(
				rem_list, update_str,
				lower_case_normalization);

			slurm_remove_char_list_from_char_list(current_list,
							      rem_list);
			new_str = slurm_char_list_to_xstr(current_list);

			FREE_NULL_LIST(current_list);
			FREE_NULL_LIST(rem_list);
		}
	} else if (mode == UPDATE_SET) {
		if (*update_str)
			new_str = xstrdup(update_str);
	} else	{ /* If bad mode is sent do nothing */
		error("bad update mode %d", mode);
		if (current_str && *current_str)
			new_str = xstrdup(current_str);
	}

	return new_str;
}

static void _set_power_save_settings(char *new_str, char **slurm_conf_setting)
{
	slurmctld_lock_t locks = {
		.conf = WRITE_LOCK,
		.node = READ_LOCK,
		.part = READ_LOCK
	};

	lock_slurmctld(locks);
	xfree(*slurm_conf_setting);
	*slurm_conf_setting = new_str;
	slurm_conf.last_update = time(NULL);
	power_save_exc_setup(); /* Reload power save settings */
	unlock_slurmctld(locks);
}

static void _slurm_rpc_set_suspend_exc_nodes(slurm_msg_t *msg)
{
	suspend_exc_update_msg_t *update_msg = msg->data;
	char *new_str;

	if (!validate_super_user(msg->auth_uid)) {
		error("set SuspendExcNodes request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	if ((update_msg->mode != UPDATE_SET) &&
	    (xstrchr(slurm_conf.suspend_exc_nodes, ':') ||
	     xstrchr(update_msg->update_str, ':'))) {
		error("Append and remove from SuspendExcNodes with ':' is not supported. Please use direct assignment instead.");
		slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
		return;
	}

	new_str = _update_hostset_from_mode(update_msg->update_str,
					    update_msg->mode,
					    slurm_conf.suspend_exc_nodes);

	if (!xstrcmp(new_str, slurm_conf.suspend_exc_nodes)) {
		info("SuspendExcNodes did not change from %s with update: %s",
		      slurm_conf.suspend_exc_nodes, update_msg->update_str);
		xfree(new_str);
	} else {
		info("Setting SuspendExcNodes to '%s'", new_str);
		_set_power_save_settings(new_str,
					 &slurm_conf.suspend_exc_nodes);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_suspend_exc_parts(slurm_msg_t *msg)
{
	suspend_exc_update_msg_t *update_msg = msg->data;
	char *new_str;

	if (!validate_super_user(msg->auth_uid)) {
		error("set SuspendExcParts request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	new_str = _update_string_from_mode(update_msg->update_str,
					   update_msg->mode,
					   slurm_conf.suspend_exc_parts, false);

	if (!xstrcmp(new_str, slurm_conf.suspend_exc_parts)) {
		info("SuspendExcParts did not change from %s with update: %s",
		      slurm_conf.suspend_exc_parts, update_msg->update_str);
		xfree(new_str);
	} else {
		info("Setting SuspendExcParts to '%s'", new_str);
		_set_power_save_settings(new_str,
					 &slurm_conf.suspend_exc_parts);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_suspend_exc_states(slurm_msg_t *msg)
{
	suspend_exc_update_msg_t *update_msg = msg->data;
	char *new_str;

	if (!validate_super_user(msg->auth_uid)) {
		error("set SuspendExcStates request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	new_str = _update_string_from_mode(update_msg->update_str,
					   update_msg->mode,
					   slurm_conf.suspend_exc_states,
					   true);

	if (!xstrcmp(new_str, slurm_conf.suspend_exc_states)) {
		info("SuspendExcStates did not change from %s with update: %s",
		      slurm_conf.suspend_exc_states, update_msg->update_str);
		xfree(new_str);
	} else {
		info("Setting SuspendExcStates to '%s'", new_str);
		_set_power_save_settings(new_str, &slurm_conf.suspend_exc_states);
	}

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_set_schedlog_level(slurm_msg_t *msg)
{
	int schedlog_level;
	slurmctld_lock_t config_read_lock =
		{ READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	set_debug_level_msg_t *request_msg = msg->data;
	log_options_t log_opts = SCHEDLOG_OPTS_INITIALIZER;

	if (!validate_super_user(msg->auth_uid)) {
		error("set scheduler log level request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	/*
	 * If slurm_conf.sched_logfile is NULL, then this operation
	 *  will fail, since there is no sched logfile for which to alter
	 *  the log level. (Calling sched_log_alter with a NULL filename
	 *  is likely to cause a segfault at the next sched log call)
	 *  So just give up and return "Operation Disabled"
	 */
	if (slurm_conf.sched_logfile == NULL) {
		error("set scheduler log level failed: no log file!");
		slurm_send_rc_msg (msg, ESLURM_DISABLED);
		return;
	}

	schedlog_level = MIN (request_msg->debug_level, (LOG_LEVEL_QUIET + 1));
	schedlog_level = MAX (schedlog_level, LOG_LEVEL_QUIET);

	lock_slurmctld(config_read_lock);
	log_opts.logfile_level = schedlog_level;
	sched_log_alter(log_opts, LOG_DAEMON, slurm_conf.sched_logfile);

	sched_info("Set scheduler log level to %d", schedlog_level);

	slurm_conf.sched_log_level = schedlog_level;
	slurm_conf.last_update = time(NULL);
	unlock_slurmctld(config_read_lock);

	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_accounting_update_msg(slurm_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	accounting_update_msg_t *update_ptr = msg->data;
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("Update Association request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}

	if (!update_ptr->update_list || !list_count(update_ptr->update_list)) {
		slurm_send_rc_msg(msg, rc);
		return;
	}

	/*
	 * Before we send an rc we are transfering the update_list to a common
	 * list to avoid the potential of messages from the dbd getting out of
	 * order. The list lock here should protect us here as we only access
	 * this list in list_transfer and list_delete_all.
	 */
	xassert(slurmctld_config.acct_update_list);
	list_transfer(slurmctld_config.acct_update_list,
		      update_ptr->update_list);

	/*
	 * Send message back to the caller letting him know we got it.
	 * Since we have the update list in the order we got it we should be
	 * good to respond.  There should be no need to wait since the end
	 * result would be the same if we wait or not since the update has
	 * already happened in the database.
	 */
	slurm_send_rc_msg(msg, rc);

	/* Signal acct_update_thread to process list */
	slurm_mutex_lock(&slurmctld_config.acct_update_lock);
	slurm_cond_broadcast(&slurmctld_config.acct_update_cond);
	slurm_mutex_unlock(&slurmctld_config.acct_update_lock);

	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS)
		error("assoc_mgr_update gave error: %s",
		      slurm_strerror(rc));
}

/* _slurm_rpc_reboot_nodes - process RPC to schedule nodes reboot */
static void _slurm_rpc_reboot_nodes(slurm_msg_t *msg)
{
	int rc;
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
	reboot_msg_t *reboot_msg = msg->data;
	char *nodelist = NULL;
	bitstr_t *bitmap = NULL;
	/* Locks: write node lock */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	time_t now = time(NULL);
#endif
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("Security violation, REBOOT_NODES RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}
#ifdef HAVE_FRONT_END
	rc = ESLURM_NOT_SUPPORTED;
#else
	/* do RPC call */
	if (reboot_msg)
		nodelist = reboot_msg->node_list;
	if (!nodelist || !xstrcasecmp(nodelist, "ALL")) {
		bitmap = node_conf_get_active_bitmap();
	} else if (node_name2bitmap(nodelist, false, &bitmap) != 0) {
		FREE_NULL_BITMAP(bitmap);
		error("%s: Bad node list in REBOOT_NODES request: \"%s\"",
		      __func__, nodelist);
		slurm_send_rc_msg(msg, ESLURM_INVALID_NODE_NAME);
		return;
	}

	lock_slurmctld(node_write_lock);
	for (int i = 0; (node_ptr = next_node_bitmap(bitmap, &i)); i++) {
		if (IS_NODE_FUTURE(node_ptr) ||
		    IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		    IS_NODE_REBOOT_ISSUED(node_ptr) ||
		    IS_NODE_POWER_DOWN(node_ptr) ||
		    IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_DOWN(node_ptr)) {
			bit_clear(bitmap, node_ptr->index);
			continue;
		}
		node_ptr->node_state |= NODE_STATE_REBOOT_REQUESTED;
		if (reboot_msg) {
			node_ptr->next_state = reboot_msg->next_state;
			if (node_ptr->next_state == NODE_RESUME)
				bit_set(rs_node_bitmap, node_ptr->index);

			if (reboot_msg->reason) {
				xfree(node_ptr->reason);
				node_ptr->reason = xstrdup(reboot_msg->reason);
				node_ptr->reason_time = now;
				node_ptr->reason_uid = msg->auth_uid;
			}
			if (reboot_msg->flags & REBOOT_FLAGS_ASAP) {
				if (node_ptr->next_state == NO_VAL)
					node_ptr->next_state = NODE_RESUME;
				if (!IS_NODE_DRAIN(node_ptr))
					node_ptr->next_state |=
						NODE_STATE_UNDRAIN;

				node_ptr->node_state |= NODE_STATE_DRAIN;
				bit_clear(avail_node_bitmap, node_ptr->index);

				if (node_ptr->reason == NULL) {
					node_ptr->reason =
						xstrdup("Reboot ASAP");
					node_ptr->reason_time = now;
					node_ptr->reason_uid = msg->auth_uid;
				}
			}
			if (!node_ptr->reason) {
				node_ptr->reason = xstrdup("reboot requested");
				node_ptr->reason_time = now;
				node_ptr->reason_uid = msg->auth_uid;
			}
		}
		want_nodes_reboot = true;
	}

	if (want_nodes_reboot == true)
		schedule_node_save();
	unlock_slurmctld(node_write_lock);
	if (want_nodes_reboot == true) {
		nodelist = bitmap2node_name(bitmap);
		info("reboot request queued for nodes %s", nodelist);
		xfree(nodelist);
	}
	FREE_NULL_BITMAP(bitmap);
	rc = SLURM_SUCCESS;
#endif
	END_TIMER2(__func__);
	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_accounting_first_reg(slurm_msg_t *msg)
{
	time_t event_time = time(NULL);

	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("First Registration request from non-super user uid=%u",
		      msg->auth_uid);
		return;
	}

	acct_storage_g_send_all(acct_db_conn, event_time, ACCOUNTING_FIRST_REG);

	END_TIMER2(__func__);
}

static void _slurm_rpc_accounting_register_ctld(slurm_msg_t *msg)
{
	DEF_TIMERS;

	START_TIMER;
	if (!validate_super_user(msg->auth_uid)) {
		error("Registration request from non-super user uid=%u",
		      msg->auth_uid);
		return;
	}

	clusteracct_storage_g_register_ctld(acct_db_conn,
	                                    slurm_conf.slurmctld_port);

	END_TIMER2(__func__);
}

static void _clear_rpc_stats(void)
{
	slurm_mutex_lock(&rpc_mutex);
	memset(rpc_type_cnt, 0, sizeof(rpc_type_cnt));
	memset(rpc_type_id, 0, sizeof(rpc_type_id));
	memset(rpc_type_time, 0, sizeof(rpc_type_time));
	memset(rpc_user_cnt, 0, sizeof(rpc_user_cnt));
	memset(rpc_user_id, 0, sizeof(rpc_user_id));
	memset(rpc_user_time, 0, sizeof(rpc_user_time));
	slurm_mutex_unlock(&rpc_mutex);
}

static void _pack_rpc_stats(char **buffer_ptr, int *buffer_size,
			    uint16_t protocol_version)
{
	uint32_t i;
	buf_t *buffer;

	slurm_mutex_lock(&rpc_mutex);
	buffer = create_buf(*buffer_ptr, *buffer_size);
	set_buf_offset(buffer, *buffer_size);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		for (i = 0; i < RPC_TYPE_SIZE; i++) {
			if (rpc_type_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack16_array(rpc_type_id,   i, buffer);
		pack32_array(rpc_type_cnt,  i, buffer);
		pack64_array(rpc_type_time, i, buffer);

		for (i = 1; i < RPC_USER_SIZE; i++) {
			if (rpc_user_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack32_array(rpc_user_id,   i, buffer);
		pack32_array(rpc_user_cnt,  i, buffer);
		pack64_array(rpc_user_time, i, buffer);

		agent_pack_pending_rpc_stats(buffer);

	}

	slurm_mutex_unlock(&rpc_mutex);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

static void _slurm_rpc_burst_buffer_status(slurm_msg_t *msg)
{
	uid_t auth_gid;
	slurm_msg_t response_msg;
	bb_status_resp_msg_t status_resp_msg;
	bb_status_req_msg_t *status_req_msg = msg->data;

	auth_gid = auth_g_get_gid(msg->auth_cred);

	memset(&status_resp_msg, 0, sizeof(status_resp_msg));
	status_resp_msg.status_resp = bb_g_get_status(status_req_msg->argc,
						      status_req_msg->argv,
						      msg->auth_uid,
						      auth_gid);
	response_init(&response_msg, msg, RESPONSE_BURST_BUFFER_STATUS,
		      &status_resp_msg);
	if (status_resp_msg.status_resp)
		response_msg.data_size =
			strlen(status_resp_msg.status_resp) + 1;
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(status_resp_msg.status_resp);
}

/* _slurm_rpc_dump_stats - process RPC for statistics information */
static void _slurm_rpc_dump_stats(slurm_msg_t *msg)
{
	char *dump;
	int dump_size;
	stats_info_request_msg_t *request_msg = msg->data;
	slurm_msg_t response_msg;

	if ((request_msg->command_id == STAT_COMMAND_RESET) &&
	    !validate_operator(msg->auth_uid)) {
		error("Security violation: REQUEST_STATS_INFO reset from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	debug3("Processing RPC details: REQUEST_STATS_INFO command=%u",
	       request_msg->command_id);

	if (request_msg->command_id == STAT_COMMAND_RESET) {
		reset_stats(1);
		_clear_rpc_stats();
	}

	pack_all_stat((request_msg->command_id != STAT_COMMAND_RESET),
		      &dump, &dump_size, msg->protocol_version);
	_pack_rpc_stats(&dump, &dump_size, msg->protocol_version);

	response_init(&response_msg, msg, RESPONSE_STATS_INFO, dump);
	response_msg.data_size = dump_size;

	/* send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

static void _slurm_rpc_dump_licenses(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg;
	license_info_request_msg_t *lic_req_msg = msg->data;

	START_TIMER;
	if ((lic_req_msg->last_update - 1) >= last_license_update) {
		/* Dont send unnecessary data
		 */
		debug2("%s: no change SLURM_NO_CHANGE_IN_DATA", __func__);
		slurm_send_rc_msg(msg, SLURM_NO_CHANGE_IN_DATA);

		return;
	}

	get_all_license_info(&dump, &dump_size, msg->auth_uid,
			     msg->protocol_version);

	END_TIMER2(__func__);
	debug2("%s: size=%d %s", __func__, dump_size, TIME_STR);

	response_init(&response_msg, msg, RESPONSE_LICENSE_INFO, dump);
	response_msg.data_size = dump_size;

	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

static void _slurm_rpc_kill_job(slurm_msg_t *msg)
{
	static int active_rpc_cnt = 0;
	DEF_TIMERS;
	job_step_kill_msg_t *kill = msg->data;
	slurmctld_lock_t fed_job_read_lock =
		{NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	slurmctld_lock_t lock = {READ_LOCK, WRITE_LOCK,
				 WRITE_LOCK, NO_LOCK, READ_LOCK };
	int cc;

	/*
	 * If the cluster is part of a federation and it isn't the origin of the
	 * job then if it doesn't know about the federated job, then route the
	 * request to the origin cluster via the client. If the cluster does
	 * know about the job and it owns the job, the this cluster will cancel
	 * the job and it will report the cancel back to the origin.
	 */
	lock_slurmctld(fed_job_read_lock);
	if (fed_mgr_fed_rec) {
		uint32_t job_id, origin_id;
		job_record_t *job_ptr;
		slurmdb_cluster_rec_t *origin;

		job_id    = strtol(kill->sjob_id, NULL, 10);
		origin_id = fed_mgr_get_cluster_id(job_id);
		origin    = fed_mgr_get_cluster_by_id(origin_id);

		/*
		 * only reroute to the origin if the connection is up. If it
		 * isn't then _signal_job will signal the sibling jobs
		 */
		if (origin && origin->fed.send &&
		    (((slurm_persist_conn_t *)origin->fed.send)->fd != -1) &&
		    (origin != fed_mgr_cluster_rec) &&
		    (!(job_ptr = find_job_record(job_id)) ||
		     (job_ptr && fed_mgr_job_started_on_sib(job_ptr)))) {

			slurmdb_cluster_rec_t *dst =
				fed_mgr_get_cluster_by_id(origin_id);
			if (!dst) {
				error("couldn't find cluster by cluster id %d",
				      origin_id);
				slurm_send_rc_msg(msg, SLURM_ERROR);
			} else {
				slurm_send_reroute_msg(msg, dst);
				info("%s: REQUEST_KILL_JOB JobId=%s uid %u routed to %s",
				     __func__, kill->sjob_id, msg->auth_uid,
				     dst->name);
			}

			unlock_slurmctld(fed_job_read_lock);
			return;
		}
	}
	unlock_slurmctld(fed_job_read_lock);

	START_TIMER;
	info("%s: REQUEST_KILL_JOB JobId=%s uid %u",
	     __func__, kill->sjob_id, msg->auth_uid);

	_throttle_start(&active_rpc_cnt);
	lock_slurmctld(lock);
	if (kill->sibling) {
		uint32_t job_id = strtol(kill->sjob_id, NULL, 10);
		cc = fed_mgr_remove_active_sibling(job_id, kill->sibling);
	} else {
		cc = job_str_signal(kill->sjob_id, kill->signal, kill->flags,
				    msg->auth_uid, 0);
	}
	unlock_slurmctld(lock);
	_throttle_fini(&active_rpc_cnt);

	if (cc == ESLURM_ALREADY_DONE) {
		debug2("%s: job_str_signal() uid=%u JobId=%s sig=%d returned: %s",
		       __func__, msg->auth_uid, kill->sjob_id,
		       kill->signal, slurm_strerror(cc));
	} else if (cc != SLURM_SUCCESS) {
		info("%s: job_str_signal() uid=%u JobId=%s sig=%d returned: %s",
		     __func__, msg->auth_uid, kill->sjob_id,
		     kill->signal, slurm_strerror(cc));
	} else {
		slurmctld_diag_stats.jobs_canceled++;
	}

	slurm_send_rc_msg(msg, cc);

	END_TIMER2(__func__);
}

/* _slurm_rpc_assoc_mgr_info()
 *
 * Pack the assoc_mgr lists and return it back to the caller.
 */
static void _slurm_rpc_assoc_mgr_info(slurm_msg_t *msg)
{
	DEF_TIMERS;
	char *dump = NULL;
	int dump_size = 0;
	slurm_msg_t response_msg;

	START_TIMER;
	/* Security is handled in the assoc_mgr */
	assoc_mgr_info_get_pack_msg(&dump, &dump_size,
				    (assoc_mgr_info_request_msg_t *)msg->data,
				    msg->auth_uid, acct_db_conn,
				    msg->protocol_version);

	END_TIMER2(__func__);
	debug2("%s: size=%d %s", __func__, dump_size, TIME_STR);

	response_init(&response_msg, msg, RESPONSE_ASSOC_MGR_INFO, dump);
	response_msg.data_size = dump_size;

	slurm_send_node_msg(msg->conn_fd, &response_msg);
	xfree(dump);
}

/* Take a persist_msg_t and handle it like a normal slurm_msg_t */
static int _process_persist_conn(void *arg,
				 persist_msg_t *persist_msg,
				 buf_t **out_buffer, uint32_t *uid)
{
	slurm_msg_t msg;
	slurm_persist_conn_t *persist_conn = arg;

	if (*uid == NO_VAL)
		*uid = auth_g_get_uid(persist_conn->auth_cred);

	*out_buffer = NULL;

	slurm_msg_t_init(&msg);

	msg.auth_cred = persist_conn->auth_cred;
	msg.auth_uid = *uid;
	msg.auth_uid_set = true;

	msg.conn = persist_conn;
	msg.conn_fd = persist_conn->fd;

	msg.msg_type = persist_msg->msg_type;
	msg.data = persist_msg->data;

	if (persist_conn->persist_type == PERSIST_TYPE_ACCT_UPDATE) {
		if (msg.msg_type == ACCOUNTING_UPDATE_MSG) {
			DEF_TIMERS;
			START_TIMER;
			_slurm_rpc_accounting_update_msg(&msg);
			END_TIMER;
			record_rpc_stats(&msg, DELTA_TIMER);
		} else {
			slurm_send_rc_msg(&msg, EINVAL);
		}
	} else
		slurmctld_req(&msg);

	return SLURM_SUCCESS;
}

static void _slurm_rpc_persist_init(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	buf_t *ret_buf;
	slurm_persist_conn_t *persist_conn = NULL, p_tmp;
	persist_init_req_msg_t *persist_init = msg->data;
	slurm_addr_t rem_addr;

	if (msg->conn)
		error("We already have a persistent connect, this should never happen");

	START_TIMER;

	if (persist_init->version > SLURM_PROTOCOL_VERSION)
		persist_init->version = SLURM_PROTOCOL_VERSION;

	if (!validate_slurm_user(msg->auth_uid)) {
		memset(&p_tmp, 0, sizeof(p_tmp));
		p_tmp.fd = msg->conn_fd;
		p_tmp.cluster_name = persist_init->cluster_name;
		p_tmp.version = persist_init->version;
		p_tmp.shutdown = &slurmctld_config.shutdown_time;

		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, REQUEST_PERSIST_INIT RPC from uid=%u",
		      msg->auth_uid);
		goto end_it;
	}

	persist_conn = xmalloc(sizeof(slurm_persist_conn_t));

	persist_conn->auth_cred = msg->auth_cred;
	msg->auth_cred = NULL;

	persist_conn->cluster_name = persist_init->cluster_name;
	persist_init->cluster_name = NULL;

	persist_conn->fd = msg->conn_fd;
	msg->conn_fd = -1;

	persist_conn->callback_proc = _process_persist_conn;

	persist_conn->persist_type = persist_init->persist_type;
	persist_conn->rem_port = persist_init->port;

	persist_conn->rem_host = xmalloc(INET6_ADDRSTRLEN);
	(void) slurm_get_peer_addr(persist_conn->fd, &rem_addr);
	slurm_get_ip_str(&rem_addr, persist_conn->rem_host, INET6_ADDRSTRLEN);

	/* info("got it from %d %s %s(%u)", persist_conn->fd, */
	/*      persist_conn->cluster_name, */
	/*      persist_conn->rem_host, persist_conn->rem_port); */
	persist_conn->shutdown = &slurmctld_config.shutdown_time;
	//persist_conn->timeout = 0; /* we want this to be 0 */

	persist_conn->version = persist_init->version;
	memcpy(&p_tmp, persist_conn, sizeof(slurm_persist_conn_t));

	if (persist_init->persist_type == PERSIST_TYPE_FED)
		rc = fed_mgr_add_sibling_conn(persist_conn, &comment);
	else if (persist_init->persist_type == PERSIST_TYPE_ACCT_UPDATE) {
		persist_conn->flags |= PERSIST_FLAG_ALREADY_INITED;
		slurm_persist_conn_recv_thread_init(
			persist_conn, -1, persist_conn);
	} else
		rc = SLURM_ERROR;
end_it:

	/* If people are really hammering the fed_mgr we could get into trouble
	 * with the persist_conn we sent in, so use the copy instead
	 */
	ret_buf = slurm_persist_make_rc_msg(&p_tmp, rc, comment, p_tmp.version);
	if (slurm_persist_send_msg(&p_tmp, ret_buf) != SLURM_SUCCESS) {
		debug("Problem sending response to connection %d uid(%u)",
		      p_tmp.fd, msg->auth_uid);
	}

	if (rc && persist_conn) {
		/* Free AFTER message has been sent back to remote */
		persist_conn->fd = -1;
		slurm_persist_conn_destroy(persist_conn);
	}
	xfree(comment);
	FREE_NULL_BUFFER(ret_buf);
	END_TIMER;

	/* Don't free this here, it will be done elsewhere */
	//slurm_persist_conn_destroy(persist_conn);
}

static void _slurm_rpc_sib_job_lock(slurm_msg_t *msg)
{
	int rc;
	sib_msg_t *sib_msg = msg->data;

	if (!msg->conn) {
		error("Security violation, SIB_JOB_LOCK RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	rc = fed_mgr_job_lock_set(sib_msg->job_id, sib_msg->cluster_id);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_sib_job_unlock(slurm_msg_t *msg)
{
	int rc;
	sib_msg_t *sib_msg = msg->data;

	if (!msg->conn) {
		error("Security violation, SIB_JOB_UNLOCK RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	rc = fed_mgr_job_lock_unset(sib_msg->job_id, sib_msg->cluster_id);

	slurm_send_rc_msg(msg, rc);
}

static void _slurm_rpc_sib_msg(uint32_t uid, slurm_msg_t *msg) {
	if (!msg->conn) {
		error("Security violation, SIB_SUBMISSION RPC from uid=%u",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_sib_msg(msg, uid);
}

static void _slurm_rpc_dependency_msg(uint32_t uid, slurm_msg_t *msg)
{
	if (!msg->conn || !validate_slurm_user(uid)) {
		error("Security violation, REQUEST_SEND_DEP RPC from uid=%u",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_dep_msg(msg);
}

static void _slurm_rpc_update_origin_dep_msg(uint32_t uid, slurm_msg_t *msg)
{
	if (!msg->conn || !validate_slurm_user(uid)) {
		error("Security violation, REQUEST_UPDATE_ORIGIN_DEP RPC from uid=%u",
		      uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	fed_mgr_q_update_origin_dep_msg(msg);
}

static buf_t *_build_rc_buf(int rc, uint16_t rpc_version)
{
	buf_t *buf = NULL;
	slurm_msg_t msg;
	return_code_msg_t data;

	memset(&data, 0, sizeof(data));
	data.return_code = rc;
	slurm_msg_t_init(&msg);
	msg.msg_type = RESPONSE_SLURM_RC;
	msg.data = &data;
	buf = init_buf(128);
	pack16(msg.msg_type, buf);
	if (pack_msg(&msg, buf) != SLURM_SUCCESS)
		FREE_NULL_BUFFER(buf);

	return buf;
}

/* Free buf_t *record from a list */
static void _ctld_free_list_msg(void *x)
{
	FREE_NULL_BUFFER(x);
}

static void _proc_multi_msg(slurm_msg_t *msg)
{
	slurm_msg_t sub_msg, response_msg;
	ctld_list_msg_t *ctld_req_msg = msg->data;
	ctld_list_msg_t ctld_resp_msg;
	List full_resp_list = NULL;
	buf_t *single_req_buf = NULL;
	buf_t *ret_buf, *resp_buf = NULL;
	ListIterator iter = NULL;
	int rc;

	if (!msg->conn) {
		error("Security violation, REQUEST_CTLD_MULT_MSG RPC from uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_ACCESS_DENIED);
		return;
	}

	full_resp_list = list_create(_ctld_free_list_msg);
	iter = list_iterator_create(ctld_req_msg->my_list);
	while ((single_req_buf = list_next(iter))) {
		slurm_msg_t_init(&sub_msg);
		if (unpack16(&sub_msg.msg_type, single_req_buf) ||
		    unpack_msg(&sub_msg, single_req_buf)) {
			error("Sub-message unpack error for REQUEST_CTLD_MULT_MSG %u RPC",
			      sub_msg.msg_type);
			ret_buf = _build_rc_buf(SLURM_ERROR,
						msg->protocol_version);
			list_append(full_resp_list, ret_buf);
			continue;
		}
		sub_msg.conn = msg->conn;
		sub_msg.auth_cred = msg->auth_cred;
		ret_buf = NULL;

		log_flag(PROTOCOL, "%s: received opcode %s",
			 __func__, rpc_num2string(sub_msg.msg_type));

		switch (sub_msg.msg_type) {
		case REQUEST_PING:
			rc = SLURM_SUCCESS;
			ret_buf = _build_rc_buf(rc, msg->protocol_version);
			break;
		case REQUEST_SIB_MSG:
			_slurm_rpc_sib_msg(msg->auth_uid, &sub_msg);
			ret_buf = _build_rc_buf(SLURM_SUCCESS,
						msg->protocol_version);
			break;
		case REQUEST_SEND_DEP:
			_slurm_rpc_dependency_msg(msg->auth_uid, &sub_msg);
			ret_buf = _build_rc_buf(SLURM_SUCCESS,
						msg->protocol_version);
			break;
		case REQUEST_UPDATE_ORIGIN_DEP:
			_slurm_rpc_update_origin_dep_msg(msg->auth_uid,
							 &sub_msg);
			ret_buf = _build_rc_buf(SLURM_SUCCESS,
						msg->protocol_version);
			break;
		default:
			error("%s: Unsupported Message Type:%s",
			      __func__, rpc_num2string(sub_msg.msg_type));
		}
		(void) slurm_free_msg_data(sub_msg.msg_type, sub_msg.data);

		if (!ret_buf) {
			ret_buf = _build_rc_buf(SLURM_ERROR,
						msg->protocol_version);
		}
		list_append(full_resp_list, ret_buf);
	}
	list_iterator_destroy(iter);

	ctld_resp_msg.my_list = full_resp_list;

	response_init(&response_msg, msg, RESPONSE_CTLD_MULT_MSG,
		      &ctld_resp_msg);

	/* Send message */
	slurm_send_node_msg(msg->conn_fd, &response_msg);
	FREE_NULL_LIST(full_resp_list);
	FREE_NULL_BUFFER(resp_buf);
	return;
}

/* Route msg to federated job's origin.
 * RET returns SLURM_SUCCESS if the msg was routed.
 */
static int _route_msg_to_origin(slurm_msg_t *msg, char *src_job_id_str,
				uint32_t src_job_id)
{
	xassert(msg);

	/* route msg to origin cluster if a federated job */
	if (!msg->conn && fed_mgr_fed_rec) {
		/* Don't send reroute if coming from a federated cluster (aka
		 * has a msg->conn). */
		uint32_t job_id, origin_id;

		if (src_job_id_str)
			job_id = strtol(src_job_id_str, NULL, 10);
		else
			job_id = src_job_id;
		origin_id = fed_mgr_get_cluster_id(job_id);

		if (origin_id && (origin_id != fed_mgr_cluster_rec->fed.id)) {
			slurmdb_cluster_rec_t *dst =
				fed_mgr_get_cluster_by_id(origin_id);
			if (!dst) {
				error("couldn't find cluster by cluster id %d",
				      origin_id);
				slurm_send_rc_msg(msg, SLURM_ERROR);
			} else {
				slurm_send_reroute_msg(msg, dst);
				info("%s: %s JobId=%u uid %u routed to %s",
				     __func__, rpc_num2string(msg->msg_type),
				     job_id, msg->auth_uid, dst->name);
			}

			return SLURM_SUCCESS;
		}
	}

	return SLURM_ERROR;
}

static void _slurm_rpc_set_fs_dampening_factor(slurm_msg_t *msg)
{
	slurmctld_lock_t config_write_lock =
		{ WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };
	set_fs_dampening_factor_msg_t *request_msg = msg->data;
	uint16_t factor;

	if (!validate_super_user(msg->auth_uid)) {
		error("set FairShareDampeningFactor request from non-super user uid=%u",
		      msg->auth_uid);
		slurm_send_rc_msg(msg, EACCES);
		return;
	}
	factor = request_msg->dampening_factor;

	lock_slurmctld(config_write_lock);
	slurm_conf.fs_dampening_factor = factor;
	slurm_conf.last_update = time(NULL);
	priority_g_reconfig(false);
	unlock_slurmctld(config_write_lock);

	info("Set FairShareDampeningFactor to %u", factor);
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _slurm_rpc_request_crontab(slurm_msg_t *msg)
{
	DEF_TIMERS;
	int rc = SLURM_SUCCESS;
	crontab_request_msg_t *req_msg = msg->data;
	buf_t *crontab = NULL;
	char *disabled_lines = NULL;
	slurm_msg_t response_msg;
	crontab_response_msg_t resp_msg;
	slurmctld_lock_t job_read_lock = { .job = READ_LOCK };

	START_TIMER;
	debug3("Processing RPC details: REQUEST_CRONTAB for uid=%u",
	       req_msg->uid);

	if (!xstrcasestr(slurm_conf.scron_params, "enable")) {
		error("%s: scrontab is disabled on this cluster", __func__);
		slurm_send_rc_msg(msg, SLURM_ERROR);
		return;
	}

	lock_slurmctld(job_read_lock);

	if ((req_msg->uid != msg->auth_uid) &&
	    !validate_operator(msg->auth_uid)) {
		rc = ESLURM_USER_ID_MISSING;
	} else {
		char *file = NULL;
		xstrfmtcat(file, "%s/crontab/crontab.%u",
			   slurm_conf.state_save_location, req_msg->uid);

		if (!(crontab = create_mmap_buf(file)))
			rc = ESLURM_JOB_SCRIPT_MISSING;
		else {
			int len = strlen(crontab->head) + 1;
			disabled_lines = xstrndup(crontab->head + len,
						  crontab->size - len);
			/*
			 * Remove extra trailing command which would be
			 * parsed as an extraneous 0.
			 */
			if (disabled_lines) {
				len = strlen(disabled_lines) - 1;
				disabled_lines[len] = '\0';
			}
		}
		xfree(file);
	}

	unlock_slurmctld(job_read_lock);
	END_TIMER2(__func__);

	if (rc != SLURM_SUCCESS) {
		slurm_send_rc_msg(msg, rc);
	} else {
		resp_msg.crontab = crontab->head;
		resp_msg.disabled_lines = disabled_lines;
		response_init(&response_msg, msg, RESPONSE_CRONTAB, &resp_msg);
		slurm_send_node_msg(msg->conn_fd, &response_msg);
		FREE_NULL_BUFFER(crontab);
		xfree(disabled_lines);
	}
}

static void _slurm_rpc_update_crontab(slurm_msg_t *msg)
{
	DEF_TIMERS;
	crontab_update_request_msg_t *req_msg = msg->data;
	slurm_msg_t response_msg;
	crontab_update_response_msg_t *resp_msg;
	/* probably need to mirror _slurm_rpc_dump_batch_script() */
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	gid_t gid = auth_g_get_gid(msg->auth_cred);

	START_TIMER;
	debug3("Processing RPC details: REQUEST_UPDATE_CRONTAB for uid=%u",
	       req_msg->uid);

	if (!xstrcasestr(slurm_conf.scron_params, "enable")) {
		error("%s: scrontab is disabled on this cluster", __func__);
		slurm_send_rc_msg(msg, SLURM_ERROR);
		return;
	}

	resp_msg = xmalloc(sizeof(*resp_msg));
	resp_msg->err_msg = NULL;
	resp_msg->job_submit_user_msg = NULL;
	resp_msg->failed_lines = NULL;
	resp_msg->return_code = SLURM_SUCCESS;

	lock_slurmctld(job_write_lock);

	if (((req_msg->uid != msg->auth_uid) || (req_msg->gid != gid)) &&
	    !validate_slurm_user(msg->auth_uid)) {
		resp_msg->return_code = ESLURM_USER_ID_MISSING;
	}

	if (!resp_msg->return_code) {
		char *alloc_node = NULL;
		_set_hostname(msg, &alloc_node);
		if (!alloc_node || (alloc_node[0] == '\0'))
			resp_msg->return_code = ESLURM_INVALID_NODE_NAME;
		else
			crontab_submit(req_msg, resp_msg, alloc_node,
				       msg->protocol_version);
		xfree(alloc_node);
	}

	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	response_init(&response_msg, msg, RESPONSE_UPDATE_CRONTAB, resp_msg);
	slurm_send_node_msg(msg->conn_fd, &response_msg);

	slurm_free_crontab_update_response_msg(resp_msg);
}

slurmctld_rpc_t slurmctld_rpcs[] =
{
	{
		.msg_type = REQUEST_RESOURCE_ALLOCATION,
		.func = _slurm_rpc_allocate_resources,
	},{
		.msg_type = REQUEST_HET_JOB_ALLOCATION,
		.func = _slurm_rpc_allocate_het_job,
	},{
		.msg_type = REQUEST_BUILD_INFO,
		.func = _slurm_rpc_dump_conf,
	},{
		.msg_type = REQUEST_JOB_INFO,
		.func = _slurm_rpc_dump_jobs,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_USER_INFO,
		.func = _slurm_rpc_dump_jobs_user,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_INFO_SINGLE,
		.func = _slurm_rpc_dump_job_single,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_BATCH_SCRIPT,
		.func = _slurm_rpc_dump_batch_script,
	},{
		.msg_type = REQUEST_SHARE_INFO,
		.func = _slurm_rpc_get_shares,
	},{
		.msg_type = REQUEST_PRIORITY_FACTORS,
		.func = _slurm_rpc_get_priority_factors,
	},{
		.msg_type = REQUEST_JOB_END_TIME,
		.func = _slurm_rpc_end_time,
	},{
		.msg_type = REQUEST_FED_INFO,
		.func = _slurm_rpc_get_fed,
		.queue_enabled = true,
		.locks = {
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_FRONT_END_INFO,
		.func = _slurm_rpc_dump_front_end,
	},{
		.msg_type = REQUEST_NODE_INFO,
		.func = _slurm_rpc_dump_nodes,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.node = WRITE_LOCK,
			.part = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_NODE_INFO_SINGLE,
		.func = _slurm_rpc_dump_node_single,
	},{
		.msg_type = REQUEST_PARTITION_INFO,
		.func = _slurm_rpc_dump_partitions,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.part = READ_LOCK,
		},
	},{
		.msg_type = MESSAGE_EPILOG_COMPLETE,
		.func = _slurm_rpc_epilog_complete,
	},{
		.msg_type = REQUEST_CANCEL_JOB_STEP,
		.func = _slurm_rpc_job_step_kill,
	},{
		.msg_type = REQUEST_COMPLETE_JOB_ALLOCATION,
		.func = _slurm_rpc_complete_job_allocation,
	},{
		.msg_type = REQUEST_COMPLETE_PROLOG,
		.func = _slurm_rpc_complete_prolog,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
		},
	},{
		.msg_type = REQUEST_COMPLETE_BATCH_SCRIPT,
		.func = _slurm_rpc_complete_batch_script,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_STEP_CREATE,
		.func = _slurm_rpc_job_step_create,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
			.node = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_STEP_INFO,
		.func = _slurm_rpc_job_step_get_info,
	},{
		.msg_type = REQUEST_JOB_WILL_RUN,
		.func = _slurm_rpc_job_will_run,
	},{
		.msg_type = REQUEST_SIB_JOB_LOCK,
		.func = _slurm_rpc_sib_job_lock,
	},{
		.msg_type = REQUEST_SIB_JOB_UNLOCK,
		.func = _slurm_rpc_sib_job_unlock,
	},{
		.msg_type = REQUEST_CTLD_MULT_MSG,
		.func = _proc_multi_msg,
	},{
		.msg_type = MESSAGE_NODE_REGISTRATION_STATUS,
		.func = _slurm_rpc_node_registration,
		.post_func = _slurm_post_rpc_node_registration,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.part = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_ALLOCATION_INFO,
		.func = _slurm_rpc_job_alloc_info,
	},{
		.msg_type = REQUEST_HET_JOB_ALLOC_INFO,
		.func = _slurm_rpc_het_job_alloc_info,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = READ_LOCK,
			.node = READ_LOCK,
			.part = NO_LOCK,
		},
	},{
		.msg_type = REQUEST_JOB_SBCAST_CRED,
		.func = _slurm_rpc_job_sbcast_cred,
	},{
		.msg_type = REQUEST_PING,
		.func = _slurm_rpc_ping,
	},{
		.msg_type = REQUEST_RECONFIGURE,
		.func = _slurm_rpc_reconfigure_controller,
	},{
		.msg_type = REQUEST_CONTROL,
		.func = _slurm_rpc_shutdown_controller,
	},{
		.msg_type = REQUEST_TAKEOVER,
		.func = _slurm_rpc_takeover,
	},{
		.msg_type = REQUEST_SHUTDOWN,
		.func = _slurm_rpc_shutdown_controller,
	},{
		.msg_type = REQUEST_SUBMIT_BATCH_JOB,
		.func = _slurm_rpc_submit_batch_job,
		.queue_enabled = true,
		.locks = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.part = READ_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_SUBMIT_BATCH_HET_JOB,
		.func = _slurm_rpc_submit_batch_het_job,
	},{
		.msg_type = REQUEST_UPDATE_FRONT_END,
		.func = _slurm_rpc_update_front_end,
	},{
		.msg_type = REQUEST_UPDATE_JOB,
		.func = _slurm_rpc_update_job,
	},{
		.msg_type = REQUEST_CREATE_NODE,
		.func = _slurm_rpc_create_node,
	},{
		.msg_type = REQUEST_UPDATE_NODE,
		.func = _slurm_rpc_update_node,
	},{
		.msg_type = REQUEST_DELETE_NODE,
		.func = _slurm_rpc_delete_node,
	},{
		.msg_type = REQUEST_CREATE_PARTITION,
		.func = _slurm_rpc_update_partition,
	},{
		.msg_type = REQUEST_UPDATE_PARTITION,
		.func = _slurm_rpc_update_partition,
	},{
		.msg_type = REQUEST_DELETE_PARTITION,
		.func = _slurm_rpc_delete_partition,
	},{
		.msg_type = REQUEST_CREATE_RESERVATION,
		.func = _slurm_rpc_resv_create,
	},{
		.msg_type = REQUEST_UPDATE_RESERVATION,
		.func = _slurm_rpc_resv_update,
	},{
		.msg_type = REQUEST_DELETE_RESERVATION,
		.func = _slurm_rpc_resv_delete,
	},{
		.msg_type = REQUEST_RESERVATION_INFO,
		.func = _slurm_rpc_resv_show,
	},{
		.msg_type = REQUEST_NODE_REGISTRATION_STATUS,
		.func = _slurm_rpc_node_registration_status,
	},{
		.msg_type = REQUEST_SUSPEND,
		.func = _slurm_rpc_suspend,
	},{
		.msg_type = REQUEST_TOP_JOB,
		.func = _slurm_rpc_top_job,
	},{
		.msg_type = REQUEST_AUTH_TOKEN,
		.func = _slurm_rpc_auth_token,
	},{
		.msg_type = REQUEST_JOB_REQUEUE,
		.func = _slurm_rpc_requeue,
	},{
		.msg_type = REQUEST_JOB_READY,
		.func = _slurm_rpc_job_ready,
	},{
		.msg_type = REQUEST_BURST_BUFFER_INFO,
		.func = _slurm_rpc_burst_buffer_info,
	},{
		.msg_type = REQUEST_STEP_BY_CONTAINER_ID,
		.func = _slurm_rpc_step_by_container_id,
	},{
		.msg_type = REQUEST_STEP_COMPLETE,
		.func = _slurm_rpc_step_complete,
		.queue_enabled = true,
		.locks = {
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.fed = READ_LOCK,
		},
	},{
		.msg_type = REQUEST_STEP_LAYOUT,
		.func = _slurm_rpc_step_layout,
	},{
		.msg_type = REQUEST_UPDATE_JOB_STEP,
		.func = _slurm_rpc_step_update,
	},{
		.msg_type = REQUEST_CONFIG,
		.func = _slurm_rpc_config_request,
	},{
		.msg_type = REQUEST_TRIGGER_SET,
		.func = _slurm_rpc_trigger_set,
	},{
		.msg_type = REQUEST_TRIGGER_GET,
		.func = _slurm_rpc_trigger_get,
	},{
		.msg_type = REQUEST_TRIGGER_CLEAR,
		.func = _slurm_rpc_trigger_clear,
	},{
		.msg_type = REQUEST_TRIGGER_PULL,
		.func = _slurm_rpc_trigger_pull,
	},{
		.msg_type = REQUEST_JOB_NOTIFY,
		.func = _slurm_rpc_job_notify,
	},{
		.msg_type = REQUEST_SET_DEBUG_FLAGS,
		.func = _slurm_rpc_set_debug_flags,
	},{
		.msg_type = REQUEST_SET_DEBUG_LEVEL,
		.func = _slurm_rpc_set_debug_level,
	},{
		.msg_type = REQUEST_SET_SCHEDLOG_LEVEL,
		.func = _slurm_rpc_set_schedlog_level,
	},{
		.msg_type = REQUEST_SET_SUSPEND_EXC_NODES,
		.func = _slurm_rpc_set_suspend_exc_nodes,
	},{
		.msg_type = REQUEST_SET_SUSPEND_EXC_PARTS,
		.func = _slurm_rpc_set_suspend_exc_parts,
	},{
		.msg_type = REQUEST_SET_SUSPEND_EXC_STATES,
		.func = _slurm_rpc_set_suspend_exc_states,
	},{
		.msg_type = ACCOUNTING_UPDATE_MSG,
		.func = _slurm_rpc_accounting_update_msg,
	},{
		.msg_type = ACCOUNTING_FIRST_REG,
		.func = _slurm_rpc_accounting_first_reg,
	},{
		.msg_type = ACCOUNTING_REGISTER_CTLD,
		.func = _slurm_rpc_accounting_register_ctld,
	},{
		.msg_type = REQUEST_TOPO_INFO,
		.func = _slurm_rpc_get_topo,
	},{
		.msg_type = REQUEST_REBOOT_NODES,
		.func = _slurm_rpc_reboot_nodes,
	},{
		.msg_type = REQUEST_STATS_INFO,
		.func = _slurm_rpc_dump_stats,
	},{
		.msg_type = REQUEST_LICENSE_INFO,
		.func = _slurm_rpc_dump_licenses,
	},{
		.msg_type = REQUEST_KILL_JOB,
		.func = _slurm_rpc_kill_job,
	},{
		.msg_type = REQUEST_ASSOC_MGR_INFO,
		.func = _slurm_rpc_assoc_mgr_info,
	},{
		.msg_type = REQUEST_PERSIST_INIT,
		.func = _slurm_rpc_persist_init,
	},{
		.msg_type = REQUEST_SET_FS_DAMPENING_FACTOR,
		.func = _slurm_rpc_set_fs_dampening_factor,
	},{
		.msg_type = REQUEST_CONTROL_STATUS,
		.func = slurm_rpc_control_status,
	},{
		.msg_type = REQUEST_BURST_BUFFER_STATUS,
		.func = _slurm_rpc_burst_buffer_status,
	},{
		.msg_type = REQUEST_CRONTAB,
		.func = _slurm_rpc_request_crontab,
	},{
		.msg_type = REQUEST_UPDATE_CRONTAB,
		.func = _slurm_rpc_update_crontab,
	},{	/* terminate the array. this must be last. */
		.msg_type = 0,
		.func = NULL,
	}
};

/*
 * slurmctld_req  - Process an individual RPC request
 * IN/OUT msg - the request message, data associated with the message is freed
 */
void slurmctld_req(slurm_msg_t *msg)
{
	DEF_TIMERS;
	slurmctld_rpc_t *this_rpc = NULL;

	if (msg->conn_fd >= 0)
		fd_set_nonblocking(msg->conn_fd);

#ifndef NDEBUG
	if ((msg->flags & SLURM_DROP_PRIV))
		drop_priv = true;
#endif

	if (!msg->auth_uid_set) {
		error("%s: received message without previously validated auth",
		      __func__);
		return;
	}

	/* Debug the protocol layer.
	 */
	START_TIMER;
	if (slurm_conf.debug_flags & DEBUG_FLAG_PROTOCOL) {
		char *p = rpc_num2string(msg->msg_type);
		if (msg->conn) {
			info("%s: received opcode %s from persist conn on (%s)%s uid %u",
			     __func__, p, msg->conn->cluster_name,
			     msg->conn->rem_host, msg->auth_uid);
		} else {
			slurm_addr_t cli_addr;
			(void) slurm_get_peer_addr(msg->conn_fd, &cli_addr);
			info("%s: received opcode %s from %pA uid %u",
			     __func__, p, &cli_addr, msg->auth_uid);
		}
	}

	debug2("Processing RPC: %s from UID=%u",
	       rpc_num2string(msg->msg_type), msg->auth_uid);

	for (int i = 0; slurmctld_rpcs[i].msg_type; i++) {
		if (slurmctld_rpcs[i].msg_type != msg->msg_type)
			continue;

		xassert(slurmctld_rpcs[i].func);
		this_rpc = &slurmctld_rpcs[i];
		break;
	}

	if (this_rpc) {
		(*(this_rpc->func))(msg);
		END_TIMER;
		record_rpc_stats(msg, DELTA_TIMER);
	} else {
		error("invalid RPC msg_type=%u", msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
	}
}
